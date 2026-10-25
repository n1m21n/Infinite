#!/usr/bin/env python3
"""Patch universe: a one-way genome -> .inf compiler plus a probe runner.

Genome (music, orders M1/M2):
    N3.17>N9.2>S4.203>E7.9>E2.41>O ; L2.5@2.cutoff
  letter  = category pool (N note gen/xform, S synth, E audio effect, L modulator, O Audio Out)
  index   = position in that pool's sorted type list (describe_all.json)
  .seed   = parameter seed (0 = defaults)
  ; Lm.s@k.key = modulator m (seed s) drives param `key` of chain node k (0-based)

One-way on purpose: it only writes patches, it never reads them back.
"""
import argparse, json, math, os, random, subprocess, sys, tempfile, time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_BIN = "build/Infinite.app/Contents/MacOS/Infinite"
P_SET = 0.35  # chance that a seeded param leaves its default


def load_facts(path=None):
    d = json.load(open(path or os.path.join(HERE, "describe_all.json")))
    return {t["type"]: t for t in d["types"]}


def kinds(t, which):
    return [p["kind"] for p in t[which]]


def pools(facts):
    P = {k: [] for k in "NGSELX"}  # N=note xform, G=note generator
    for name, t in sorted(facts.items()):
        if t["hardware_driven"] or name == "Plugin":
            continue
        ins, outs, cat = kinds(t, "inputs"), kinds(t, "outputs"), t["category"]
        if cat == "Notes" and "note" in outs:
            P["N" if "note" in ins else "G"].append(name)
        elif cat == "Synths" and "note" in ins and "audio" in outs:
            P["S"].append(name)
        elif cat == "AudioEffects" and ins[:1] == ["audio"] and "audio" in outs:
            P["E"].append(name)
        elif cat == "Modulators" and "modulator" in outs:
            P["L"].append(name)
    P["X"] = ["Mixer"]
    return P


def seeded_params(t, seed):
    """key -> (tag, value) for a node type, deterministic in (type, seed)."""
    if not seed:
        return {}
    r = random.Random("%s:%d" % (t["type"], seed))
    out = {}
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


def parse(code):
    chain_s, _, mods_s = code.partition(";")
    chain = []
    for tok in chain_s.strip().split(">"):
        tok = tok.strip()
        if tok == "O":
            continue
        letter, rest = tok[0], tok[1:]
        idx, _, seed = rest.partition(".")
        chain.append((letter, int(idx), int(seed or 0)))
    mods = []
    for tok in mods_s.split():
        src, _, dst = tok.partition("@")
        idx, _, seed = src[1:].partition(".")
        k, _, key = dst.partition(".")
        mods.append((int(idx), int(seed or 0), int(k), key))
    return chain, mods


def compile_inf(code, facts, P):
    chain, mods = parse(code)
    names, lines = {}, ["infinite-patch 1"]
    n = 0

    def add(name, seed):
        nonlocal n
        n += 1
        t = facts[name]
        lines.append("node %d %s %s" % (n, t["category"], name))
        for k, (tag, v) in sorted(seeded_params(t, seed).items()):
            lines.append("  %s %s %s" % (tag, k, ("%.6g" % v) if tag == "f" else int(v)))
        lines.append("end")
        return n

    ids = []
    for letter, idx, seed in chain:
        letter = "N" if letter == "N" else letter
        pool = P["G" if (letter == "N" and not ids) else letter]
        ids.append(add(pool[idx % len(pool)], seed))
        names[ids[-1]] = pool[idx % len(pool)]
    out = add("Audio Out", 0)
    names[out] = "Audio Out"
    kind = None
    for a, b in zip(ids, ids[1:] + [out]):
        ka = chain[ids.index(a)][0] if a != out else "S"
        kb = chain[ids.index(b)][0] if b != out else "O"
        word = "note" if ka in "NG" and kb in "NS" else "aud"
        slot = next((i["slot"] for i in facts[names[b]]["inputs"] if i["kind"] == word), 0)
        lines.append("%s %d %d %d" % (word, b, slot, a))
    for idx, seed, k, key in mods:
        m = add(P["L"][idx % len(P["L"])], seed)
        lines.append("mod %d %s %d 0 0 %.3f 0.5" % (ids[k % len(ids)], key, m, random.Random(seed).uniform(0.1, 1)))
    return "\n".join(lines) + "\n"


def sample_code(r, P, facts, order=1):
    ns = r.choice([1, 1, 1, 2])
    chain = ["N%d.%d" % (r.randrange(len(P["G"])), r.randrange(1 << 20))]
    for _ in range(ns - 1):
        chain.append("N%d.%d" % (r.randrange(len(P["N"])), r.randrange(1 << 20)))
    chain.append("S%d.%d" % (r.randrange(len(P["S"])), r.randrange(1 << 20)))
    for _ in range(r.choice([1, 1, 2, 3, 4])):
        chain.append("E%d.%d" % (r.randrange(len(P["E"])), r.randrange(1 << 20)))
    code = ">".join(chain) + ">O"
    if order >= 2:
        picks = []
        for _ in range(r.choice([1, 2, 3])):
            k = r.randrange(len(chain))
            letter = chain[k][0]
            pool = P["G" if (letter == "N" and k == 0) else letter]
            name = pool[int(chain[k][1:].split(".")[0]) % len(pool)]
            keys = [m["key"] for m in facts[name]["modulatable"] if m.get("key")]
            if keys:
                picks.append("L%d.%d@%d.%s" % (r.randrange(len(P["L"])), r.randrange(1 << 20), k, r.choice(keys)))
        if picks:
            code += " ; " + " ".join(picks)
    return code


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
    except subprocess.TimeoutExpired:
        res = {"ok": False, "exit": 6, "err": "timeout", "a": {}}
    except Exception as e:
        res = {"ok": False, "exit": -1, "err": type(e).__name__, "a": {}}
    res.update(code=code, sec=round(time.time() - t0, 2))
    try:
        res["class"] = classify(res)
    except Exception as e:
        res["class"] = "classify-error:" + type(e).__name__
    os.path.exists(path) and os.remove(path)
    os.path.exists(path + ".json") and os.remove(path + ".json")
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
    ap.add_argument("--out", default="atlas.jsonl")
    a = ap.parse_args()
    facts = load_facts()
    P = pools(facts)
    if a.cmd == "pools":
        for k, v in P.items():
            print(k, len(v), v[:6])
    elif a.cmd == "compile":
        sys.stdout.write(compile_inf(a.code, facts, P))
    else:
        r = random.Random(a.seed)
        codes = [sample_code(r, P, facts, a.order) for _ in range(a.n)]
        t0, done = time.time(), 0
        with tempfile.TemporaryDirectory() as wd, open(a.out, "a") as f, ThreadPoolExecutor(a.workers) as ex:
            futs = []
            for c in codes:
                futs.append(ex.submit(lambda c=c: probe(c, facts, P, a.bin, a.secs, wd) if time.time() - t0 < a.budget else None))
            for fu in futs:
                res = fu.result()
                if res:
                    f.write(json.dumps(res) + "\n")
                    done += 1
        print("probed", done, "of", a.n, "in", round(time.time() - t0), "s ->", a.out)


if __name__ == "__main__":
    main()
