#!/usr/bin/env python3
"""Visual universe (V1 spine, V2 spine + modulation, V3 spines joined by a blender).
Genome: I3.5>F9.2>F4.7>O ; L2.1@1.radius      [I1.2>F3.4] + [I5.6] => B0.1>F2.2>O
Letters: I image source, F image filter (one image in/out), B blender (2+ image inputs), L modulator, O Output.
Probe: Infinite --frame at three times; classify black / white / flat / ok and record whether it moves."""
import argparse, json, os, random, subprocess, sys, tempfile, time
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import universe as u

SKIP_F = {"Comment", "Group", "Null", "Audio Texture", "Optical Flow", "VMPC"}  # need live audio/camera/UI input


def vpools(facts):
    P = {"I": [], "F": [], "B": [], "L": []}
    base = u.pools(facts)
    for name in sorted(facts):
        t = facts[name]
        if t["hardware_driven"] or name in SKIP_F or name in ("Plugin", "Output", "Syphon Out", "NDI Out", "Projection"):
            continue
        ins = [i["kind"] for i in t["inputs"]]
        out_img = any(o["kind"] == "image" for o in t["outputs"])
        if not out_img:
            continue
        n_img = ins.count("image")
        if t["category"] == "Source" and n_img == 0:
            P["I"].append(name)
        elif n_img == 1 and len(ins) == 1 and t["category"] in ("Effects", "Compositing", "Color"):
            P["F"].append(name)
        elif n_img >= 2 and all(k == "image" for k in ins):
            P["B"].append(name)
    P["L"] = [n for n in base["L"] if all(i["kind"] == "modulator" for i in facts[n]["inputs"])]
    return P


def compile_visual(code, facts, P, order_out=None):
    branches, join, tail, mods = u.parse(code)
    lines, names, used, order = ["infinite-patch 1"], {}, {}, []
    n = 0

    def add(name, seed):
        nonlocal n
        n += 1
        t = facts[name]
        lines.append("node %d %s %s" % (n, t["category"], name.split(":")[0]))
        for k, (tag, v) in sorted(u.seeded_params(t, seed).items()):
            lines.append("  %s %s %s" % (tag, k, ("%.6g" % v) if tag == "f" else v if tag == "s" else int(v)))
        lines.append("end")
        names[n] = name
        return n

    def wire(src, dst):
        slots = [i["slot"] for i in facts[names[dst]]["inputs"] if i["kind"] == "image"]
        free = [s for s in slots if (dst, s) not in used]
        slot = (free or slots or [0])[0]
        used[(dst, slot)] = 1
        lines.append("cable %d %d %d" % (dst, slot, src))

    ends = []
    for b in branches:
        prev = None
        for letter, idx, seed in b:
            nid = add(P[letter][idx % len(P[letter])], seed)
            order.append(nid)
            if prev is not None:
                wire(prev, nid)
            prev = nid
        ends.append(prev)
    end = ends[0]
    if join:
        jl, ji, js = join
        end = add(P[jl][ji % len(P[jl])], js)
        order.append(end)
        for e in ends:
            wire(e, end)
        for letter, idx, seed in tail:
            nid = add(P[letter][idx % len(P[letter])], seed)
            order.append(nid)
            wire(end, nid)
            end = nid
    out = add("Output", 0)
    wire(end, out)
    for idx, seed, k, key, _ in mods:
        m = add(P["L"][idx % len(P["L"])], seed)
        lines.append("mod %d %s %d 0 0 %.3f 0.5" % (order[k % len(order)], key, m, random.Random(seed).uniform(0.1, 1)))
    if order_out is not None:
        order_out.extend(order)
    return u.index_code_mods("\n".join(lines) + "\n")


def sample(r, P, facts, order, pick):
    def spine(first=True):
        t = ["I%d.%d" % (pick("I"), u.sd(r))]
        t += ["F%d.%d" % (pick("F"), u.sd(r)) for _ in range(r.choice([1, 1, 2, 3]))]
        return t
    if order >= 3:
        jt = "B%d.%d" % (pick("B"), u.sd(r))
        slots = sum(1 for i in facts[P["B"][int(jt[1:].split(".")[0]) % len(P["B"])]]["inputs"] if i["kind"] == "image")
        nb = min(r.choice([2, 2, 3]), slots)
        code = " + ".join("[" + ">".join(spine()) + "]" for _ in range(nb)) + " => " + ">".join([jt] + ["F%d.%d" % (pick("F"), u.sd(r)) for _ in range(r.choice([0, 1, 2]))]) + ">O"
    else:
        code = ">".join(spine()) + ">O"
    if order >= 2:
        names = u.node_names(code, P)
        picks = []
        for _ in range(r.choice([1, 2, 3])):
            k = r.randrange(len(names))
            keys = [m["key"] for m in facts[names[k]]["modulatable"] if m.get("key")]
            if keys:
                picks.append("L%d.%d@%d.%s" % (pick("L"), u.sd(r), k, r.choice(keys)))
        if picks:
            code += " ; " + " ".join(picks)
    return code


def classify_frames(stats):
    lum = [s["mean_luma"] for s in stats]
    if max(s["max_luma"] for s in stats) < 0.01:
        return "black"
    if min(lum) > 0.99:
        return "white"
    if all(s["max_luma"] - s["min_luma"] < 0.02 for s in stats):
        return "flat"
    return "ok"


def probe(code, facts, P, binary, workdir):
    h = "v%d" % (hash(code) & 0xffffffff)
    path = os.path.join(workdir, h + ".inf")
    open(path, "w").write(compile_visual(code, facts, P))
    t0 = time.time()
    res = {"code": code}
    try:
        cp = subprocess.run([binary, "--frame", path, "0,1,2", os.path.join(workdir, h + "/")], capture_output=True, text=True, timeout=90)
        j = json.loads(u.last_json(cp.stdout))
        if cp.returncode or not j.get("ok"):
            res["class"] = "error:" + ((j.get("errors") or [{}])[0].get("code") or "?")
            res["msg"] = (j.get("errors") or [{}])[0].get("message")
        else:
            st = j["frame_stats"]
            res["class"] = classify_frames(st)
            res["moves"] = len({round(s["mean_luma"], 3) for s in st}) > 1
            res["stats"] = [{k: s[k] for k in ("mean_luma", "min_luma", "max_luma", "black_percent")} for s in st]
    except subprocess.TimeoutExpired:
        res["class"] = "error:timeout"
    except Exception as e:
        res["class"] = "classify-error:" + type(e).__name__
    res["sec"] = round(time.time() - t0, 2)
    try:
        res["names"] = u.node_names(code, P)
        res["order"] = (3 if "=>" in code else 1) + (1 if ";" in code else 0)
    except Exception:
        pass
    os.path.exists(path) and os.remove(path)
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["pools", "compile", "run"])
    ap.add_argument("code", nargs="?")
    ap.add_argument("--bin", default=u.DEFAULT_BIN)
    ap.add_argument("-n", type=int, default=50)
    ap.add_argument("--order", type=int, default=1)
    ap.add_argument("--workers", type=int, default=2)
    ap.add_argument("--budget", type=float, default=600)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--cover", action="store_true")
    ap.add_argument("--out", default="atlas_visual.jsonl")
    a = ap.parse_args()
    facts = u.load_facts()
    P = vpools(facts)
    if a.cmd == "pools":
        for k, v in P.items():
            print(k, len(v), v)
    elif a.cmd == "compile":
        sys.stdout.write(compile_visual(a.code, facts, P))
    else:
        r = random.Random(a.seed)
        pick = u.Pick(r, P, a.cover)
        codes = [sample(r, P, facts, a.order, pick) for _ in range(a.n)]
        t0, done = time.time(), 0
        with tempfile.TemporaryDirectory() as wd, open(a.out, "a") as f, ThreadPoolExecutor(a.workers) as ex:
            futs = [ex.submit(lambda c=c: probe(c, facts, P, a.bin, wd) if time.time() - t0 < a.budget else None) for c in codes]
            for fu in futs:
                res = fu.result()
                if res:
                    f.write(json.dumps(res) + "\n")
                    done += 1
        print("probed", done, "of", a.n, "in", round(time.time() - t0), "s ->", a.out)


if __name__ == "__main__":
    main()
