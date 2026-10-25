#!/usr/bin/env python3
"""Patch universe: a one-way genome -> .inf compiler plus a probe runner.

Genome (music orders M1..M3):
    N3.17>N9.2>S4.203>E7.9>E2.41>O ; L2.5@2.cutoff                      M1, M2
    [N1.4>S2.9>E3.1] + [N0.2>S5.7] => X0.3 > E6.8 > O                    M3 audio join
    [N1.4>N2.2] + [N0.2] => J1.5 > S3.1 > E4.4 > O                       M3 note join
  letter  = pool: N note (first in a branch = generator, else transformer), S synth, E audio effect,
            J note joiner (Note Merge, Note Switcher), X audio joiner (Mixer, Blend Audio, ...),
            L modulator, O Audio Out
  index   = position in that pool's sorted type list (describe_all.json)
  .seed   = parameter seed (0 = defaults)
  ; Lm.s@k.key = modulator m (seed s) drives param `key` of node k (0-based, in compile order)

One-way on purpose: it only writes patches, it never reads them back.
Samples: nodes that need a file get one from the bundled drum kit (audio) or assets/*.mid (MIDI);
nodes that need a picture get a Source node.
"""
import argparse, glob, json, math, os, random, subprocess, sys, tempfile, time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_BIN = "build/Infinite.app/Contents/MacOS/Infinite"
P_SET = 0.35  # chance that a seeded param leaves its default
WAVS = sorted(glob.glob(os.path.join(ROOT, "assets/drumkits/infinite-basic/*.wav")))
MIDIS = sorted(glob.glob(os.path.join(HERE, "assets/*.mid")))
NEEDS_LIVE = {"Keyboard", "Grain Molder"}  # silent headless: key presses, or UI-side record/load
AUDIO_PATH_KEYS = ("path", "pathB", "filePath", "loopFile", "file")


def load_facts(path=None):
    d = json.load(open(path or os.path.join(HERE, "describe_all.json")))
    return {t["type"]: t for t in d["types"]}


def kinds(t, which):
    return [p["kind"] for p in t[which]]


def pools(facts):
    P = {k: [] for k in "NGSEJXLI"}  # I = image sources feeding picture-fed synths
    for name, t in sorted(facts.items()):
        if t["hardware_driven"] or name == "Plugin" or name in NEEDS_LIVE:
            continue
        ins, outs, cat = kinds(t, "inputs"), kinds(t, "outputs"), t["category"]
        if cat == "Notes" and "note" in outs:
            if ins.count("note") >= 2:
                P["J"].append(name)
            else:
                P["N" if "note" in ins else "G"].append(name)
        elif cat == "Synths" and "note" in ins and "audio" in outs:
            P["S"].append(name)
        elif cat == "AudioEffects" and ins[:1] == ["audio"] and "audio" in outs:
            P["E"].append(name)
            if ins.count("audio") >= 2:
                P["X"].append(name)
        elif cat == "Utility" and ins.count("audio") >= 2 and "audio" in outs:
            P["X"].append(name)
        elif cat == "Modulators" and "modulator" in outs:
            P["L"].append(name)
        elif cat == "Source" and not ins and outs[:1] == ["image"]:
            P["I"].append(name)
    return P


def pick_file(r, node_type):
    return r.choice(MIDIS if node_type == "MIDI File" else WAVS)


def seeded_params(t, seed):
    """key -> (tag, value) for a node type, deterministic in (type, seed)."""
    r = random.Random("%s:%d" % (t["type"], seed))
    out = {}
    for row in t["rows"]:
        k = row["key"]
        if row["tag"] == "s" and (k in AUDIO_PATH_KEYS or k.endswith("_path")) and t["type"] != "Palette":
            out[k] = ("s", pick_file(r, t["type"]))
    if not seed:
        return out
    for row in t["rows"]:
        if not row.get("ui", True) or row["tag"] not in ("f", "i", "b"):
            continue
        if r.random() > P_SET:
            continue
        lo, hi = row.get("min", 0), row.get("max", 1)
        if row["tag"] == "b":
            out[row["key"]] = ("b", r.randrange(2))
        elif row.get("options"):
            out[row["key"]] = ("i", r.randrange(len(row["options"])))
        elif hi > lo:
            if row["tag"] == "i":
                out[row["key"]] = ("i", r.randint(int(lo), int(hi)))
            elif lo > 0 and hi / lo > 50:
                out[row["key"]] = ("f", math.exp(r.uniform(math.log(lo), math.log(hi))))
            else:
                out[row["key"]] = ("f", r.uniform(lo, hi))
    return out


def tok(t):
    t = t.strip()
    idx, _, seed = t[1:].partition(".")
    return (t[0], int(idx or 0), int(seed or 0))


def parse(code):
    """-> (branches, join, tail, mods). A plain chain is one branch, no join."""
    body, _, mods_s = code.partition(";")
    body = body.strip()
    join, tail = None, []
    if "=>" in body:
        head, _, rest = body.partition("=>")
        parts = [tok(x) for x in rest.split(">") if x.strip() and x.strip() != "O"]
        join, tail = parts[0], parts[1:]
        branches = [[tok(x) for x in b.strip().strip("[]").split(">")] for b in head.split("+")]
    else:
        branches = [[tok(x) for x in body.split(">") if x.strip() != "O"]]
    mods = []
    for m in mods_s.split():
        src, _, dst = m.partition("@")
        src, _, frm = src.partition("<")  # `<j`: the modulator reads compiled node j (M4)
        idx, _, seed = src[1:].partition(".")
        k, _, key = dst.partition(".")
        mods.append((int(idx), int(seed or 0), int(k), key, int(frm) if frm else None))
    return branches, join, tail, mods


def node_names(code, P):
    """Type name of every chain node, in compile order (what `@k` indexes)."""
    branches, join, tail, _ = parse(code)
    names = []
    for b in branches:
        for j, (letter, idx, _) in enumerate(b):
            pool = P["G" if (letter == "N" and j == 0) else letter]
            names.append(pool[idx % len(pool)])
    for letter, idx, _ in ([join] if join else []) + tail:
        names.append(P[letter][idx % len(P[letter])])
    return names


def compile_inf(code, facts, P):
    branches, join, tail, mods = parse(code)
    lines, names, used, order = ["infinite-patch 1"], {}, {}, []
    n = 0

    def add(name, seed, force=None):
        nonlocal n
        n += 1
        t = facts[name]
        lines.append("node %d %s %s" % (n, t["category"], name))
        ps = seeded_params(t, seed)
        ps.update(force or {})
        for k, (tag, v) in sorted(ps.items()):
            lines.append("  %s %s %s" % (tag, k, ("%.6g" % v) if tag == "f" else v if tag == "s" else int(v)))
        lines.append("end")
        names[n] = name
        return n

    def wire(src, dst, kind):
        word = "note" if kind == "note" else "aud"
        slots = [i["slot"] for i in facts[names[dst]]["inputs"] if i["kind"] == ("note" if word == "note" else "audio")]
        free = [s for s in slots if (dst, s) not in used]
        slot = (free or slots or [0])[0]
        used[(dst, slot)] = 1
        lines.append("%s %d %d %d" % (word, dst, slot, src))

    def feed_images(node):
        for i in facts[names[node]]["inputs"]:
            if i["kind"] == "image" and P["I"]:
                src = add(P["I"][(node * 7 + i["slot"]) % len(P["I"])], 0)
                lines.append("cable %d %d %d" % (node, i["slot"], src))

    def kind_of(letter):
        return "note" if letter in "NGJ" else "aud"

    # pad-type synths only answer notes 36..51: steer the branch's generator there
    pad_synths = {"MPC", "Slicer"}
    tail_names = [P[l][i % len(P[l])] for l, i, _ in tail]
    ends = []
    for b in branches:
        prev = None
        wants_pads = any(P[l][i % len(P[l])] in pad_synths for l, i, _ in b[1:] if l == "S") or \
            (join and join[0] == "J" and any(x in pad_synths for x in tail_names))
        for j, (letter, idx, seed) in enumerate(b):
            pool = P["G" if (letter == "N" and j == 0) else letter]
            force = None
            if wants_pads and j == 0:
                gk = {r["key"] for r in facts[pool[idx % len(pool)]]["rows"]}
                if "rangeLow" in gk:
                    force = {"rangeLow": ("i", 36), "rangeHigh": ("i", 51)}
                elif "root" in gk:
                    force = {"root": ("i", 36)}
                elif "transpose" in gk:
                    force = {"path": ("s", os.path.join(HERE, "assets/pads.mid")), "transpose": ("i", 0)}
                elif "note0" in gk:
                    force = {"note%d" % q: ("i", 36 + q) for q in range(16)}
            if pool[idx % len(pool)] == "Slicer":
                force = {"sliceBy": ("i", 1), "volume": ("f", 0.8), "onsets": ("i", 8), "attack": ("f", 0)}  # grid slices answer every pad; onset slicing is sparse
            nid = add(pool[idx % len(pool)], seed, force)
            order.append(nid)
            if prev is not None:
                wire(prev, nid, kind_of(b[j - 1][0]))
            feed_images(nid)
            prev = nid
        ends.append((prev, kind_of(b[-1][0])))
    if join:
        jl, ji, js = join
        jn = add(P[jl][ji % len(P[jl])], js)
        order.append(jn)
        for e, k in ends:
            wire(e, jn, k)
        end, ek, prev_letter = jn, kind_of(jl), jl
        for letter, idx, seed in tail:
            nid = add(P[letter][idx % len(P[letter])], seed)
            order.append(nid)
            wire(end, nid, kind_of(prev_letter))
            feed_images(nid)
            end, prev_letter = nid, letter
    else:
        end = ends[0][0]
    out = add("Audio Out", 0)
    wire(end, out, "aud")
    for idx, seed, k, key, frm in mods:
        m = add(P["L"][idx % len(P["L"])], seed)
        if frm is not None:
            src = order[frm % len(order)]
            skind = facts[names[src]]["outputs"][0]["kind"]
            wire(src, m, "note" if skind == "note" else "aud")
        lines.append("mod %d %s %d 0 0 %.3f 0.5" % (order[k % len(order)], key, m, random.Random(seed).uniform(0.1, 1)))
    return "\n".join(lines) + "\n"


class Pick:
    """Random index into a pool; with cover=True, cycles through every type before repeating."""
    def __init__(self, r, P, cover):
        self.r, self.P, self.cover, self.q = r, P, cover, {}

    def __call__(self, letter):
        n = len(self.P[letter])
        if not self.cover:
            return self.r.randrange(n)
        if not self.q.get(letter):
            self.q[letter] = list(range(n))
            self.r.shuffle(self.q[letter])
        return self.q[letter].pop()


def sd(r):
    return r.randrange(1, 1 << 20)


def note_branch(r, pick):
    toks = ["N%d.%d" % (pick("G"), sd(r))]
    for _ in range(r.choice([0, 0, 1])):
        toks.append("N%d.%d" % (pick("N"), sd(r)))
    return toks


def audio_branch(r, pick):
    toks = note_branch(r, pick)
    toks.append("S%d.%d" % (pick("S"), sd(r)))
    for _ in range(r.choice([0, 1, 1, 2])):
        toks.append("E%d.%d" % (pick("E"), sd(r)))
    return toks


def sample_code(r, P, facts, order=1, pick=None):
    pick = pick or Pick(r, P, False)
    if order >= 3:
        audio = r.random() < 0.5  # audio join of full spines, or note join into one synth chain
        jt = "X%d.%d" % (pick("X"), sd(r)) if audio else "J%d.%d" % (pick("J"), sd(r))
        jname = P[jt[0]][int(jt[1:].split(".")[0]) % len(P[jt[0]])]
        slots = sum(1 for i in facts[jname]["inputs"] if i["kind"] == ("audio" if audio else "note"))
        nb = min(r.choice([2, 2, 3]), slots)
        if audio:
            bs = [audio_branch(r, pick) for _ in range(nb)]
            tail = [jt] + ["E%d.%d" % (pick("E"), sd(r)) for _ in range(r.choice([0, 1, 2]))]
        else:
            bs = [note_branch(r, pick) for _ in range(nb)]
            tail = [jt, "S%d.%d" % (pick("S"), sd(r))] + ["E%d.%d" % (pick("E"), sd(r)) for _ in range(r.choice([0, 1, 2]))]
        code = " + ".join("[" + ">".join(b) + "]" for b in bs) + " => " + ">".join(tail) + ">O"
    else:
        code = ">".join(audio_branch(r, pick)) + ">O"
    if order >= 4 and "=>" in code:
        picks = cross_mods(r, P, facts, code, pick)
        if picks:
            code += " ; " + " ".join(picks)
    elif order >= 2:
        names = node_names(code, P)
        picks = []
        for _ in range(r.choice([1, 2, 3])):
            k = r.randrange(len(names))
            keys = [m["key"] for m in facts[names[k]]["modulatable"] if m.get("key")]
            if keys:
                picks.append("L%d.%d@%d.%s" % (pick("L"), sd(r), k, r.choice(keys)))
        if picks:
            code += " ; " + " ".join(picks)
    return code


def spine_ids(code):
    """Spine index of each compiled node, in compile order; -1 for join and tail."""
    branches, join, tail, _ = parse(code)
    ids = [i for i, b in enumerate(branches) for _ in b]
    return ids + [-1] * ((1 if join else 0) + len(tail))


def cross_mods(r, P, facts, code, pick):
    """M4: audio/note readers fed from one spine that drive a param of another spine."""
    names, ids = node_names(code, P), spine_ids(code)
    readers = [i for i, n in enumerate(P["L"]) if any(x["kind"] in ("audio", "note") for x in facts[n]["inputs"])]
    out = []
    for _ in range(r.choice([1, 2, 3])):
        j = r.randrange(len(names))
        if ids[j] < 0:
            continue
        kind = facts[names[j]]["outputs"][0]["kind"]
        ok = [i for i in readers if any(x["kind"] == kind for x in facts[P["L"][i]]["inputs"])]
        tgts = [k for k in range(len(names)) if ids[k] != ids[j] and any(m.get("key") for m in facts[names[k]]["modulatable"])]
        if not ok or not tgts:
            continue
        k = r.choice(tgts)
        key = r.choice([m["key"] for m in facts[names[k]]["modulatable"] if m.get("key")])
        out.append("L%d.%d<%d@%d.%s" % (r.choice(ok), sd(r), j, k, key))
    return out


def classify(res):
    if not res["ok"]:
        return "error:" + (res.get("err") or "?")
    a = res["a"]
    if a.get("non_finite_frames"):
        return "nan"
    lufs = a.get("integrated_lufs")
    if a.get("silence_ratio", 1) > 0.99 or lufs is None or lufs < -70:
        return "silent"
    if a.get("clipped_percent", 0) > 5:
        return "clipped"
    return "ok"


def probe(code, facts, P, binary, secs, workdir):
    path = os.path.join(workdir, "p%d.inf" % (hash(code) & 0xffffffff))
    open(path, "w").write(compile_inf(code, facts, P))
    t0 = time.time()
    try:
        cp = subprocess.run([binary, "--audio-summary", path, path + ".json", "--duration", str(secs)],
                            capture_output=True, text=True, timeout=60)
        line = cp.stdout.strip().splitlines()[-1] if cp.stdout.strip() else "{}"
        j = json.loads(line)
        res = {"ok": cp.returncode == 0 and j.get("ok", False), "exit": cp.returncode,
               "err": (j.get("errors") or [{}])[0].get("code"), "a": j.get("audio_summary", {})}
        if res["err"]:
            res["msg"] = j["errors"][0].get("message")
    except subprocess.TimeoutExpired:
        res = {"ok": False, "exit": 6, "err": "timeout", "a": {}}
    except Exception as e:
        res = {"ok": False, "exit": -1, "err": type(e).__name__, "a": {}}
    res.update(code=code, sec=round(time.time() - t0, 2))
    try:
        res["names"] = node_names(code, P)  # decode-proof: indices shift if a pool changes
        res["order"] = 1 if "=>" not in code and ";" not in code else (2 if "=>" not in code else (3 if ";" not in code else 4))
    except Exception:
        pass
    try:
        res["class"] = classify(res)
    except Exception as e:
        res["class"] = "classify-error:" + type(e).__name__
    for p in (path, path + ".json"):
        os.path.exists(p) and os.remove(p)
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["pools", "compile", "run"])
    ap.add_argument("code", nargs="?")
    ap.add_argument("--bin", default=DEFAULT_BIN)
    ap.add_argument("-n", type=int, default=50)
    ap.add_argument("--order", type=int, default=1)
    ap.add_argument("--secs", type=float, default=4)
    ap.add_argument("--workers", type=int, default=2)
    ap.add_argument("--budget", type=float, default=1200, help="max seconds")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--cover", action="store_true", help="cycle every type in each pool before repeating")
    ap.add_argument("--out", default="atlas.jsonl")
    a = ap.parse_args()
    facts = load_facts()
    P = pools(facts)
    if a.cmd == "pools":
        for k, v in P.items():
            print(k, len(v), v)
    elif a.cmd == "compile":
        sys.stdout.write(compile_inf(a.code, facts, P))
    else:
        r = random.Random(a.seed)
        pick = Pick(r, P, a.cover)
        codes = [sample_code(r, P, facts, a.order, pick) for _ in range(a.n)]
        t0, done = time.time(), 0
        with tempfile.TemporaryDirectory() as wd, open(a.out, "a") as f, ThreadPoolExecutor(a.workers) as ex:
            futs = [ex.submit(lambda c=c: probe(c, facts, P, a.bin, a.secs, wd) if time.time() - t0 < a.budget else None)
                    for c in codes]
            for fu in futs:
                res = fu.result()
                if res:
                    f.write(json.dumps(res) + "\n")
                    done += 1
        print("probed", done, "of", a.n, "in", round(time.time() - t0), "s ->", a.out)


if __name__ == "__main__":
    main()
