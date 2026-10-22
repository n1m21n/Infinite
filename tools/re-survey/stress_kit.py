#!/usr/bin/env python3
"""Robustness stress kit: break Infinite on purpose, headless, and count what falls over.

  stress_kit.py nodes [--only TYPE] [--jobs 4]   every node type alone and wired to an Output: validate, render 2 frames, save twice
  stress_kit.py fuzz [--rounds 300] [--seed 1]   damaged copies of real patches (cut, flipped bytes, dropped/doubled lines, bad numbers)
  stress_kit.py random [--rounds 200]            random graphs with legal cables and in-range parameters: render, listen, save
  stress_kit.py killsave [--rounds 40]           kill a save at a random moment; the file on disk must still open
  stress_kit.py all                              the four above, one table, written to ~/re-output/stress/results.md

What counts as a failure: a crash (signal exit), a hang (timeout), a save that leaves a file that no
longer opens, or a second save that differs from the first (the format is not stable). A clean refusal
(exit 3/4/5 with an error message) is NOT a failure: the app said no and stayed up.

Behaviour only; nothing here reads source. Outputs go to ~/re-output/stress. Test: `stress_kit.py selftest`.
"""

import argparse
import concurrent.futures as cf
import os
import random
import re
import signal
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import REPO, RE_OUTPUT, infinite_bin, run  # noqa: E402

OUT = os.path.join(RE_OUTPUT, "stress")
SKILL = os.path.join(REPO, ".claude/skills/infinite-patch-authoring/SKILL.md")
SEEDS = ["tests/render/audio.inf", "tests/render/compositing.inf", "tests/render/field_pixel.inf",
         "tests/render/modulation.inf", "tests/render/render3d.inf"]
# Exit codes the app uses to say "no" cleanly (SKILL.md, Reading the result).
CLEAN = {0, 2, 3, 4, 5}


def is_crash(rc):
    return rc < 0 or rc >= 128 or rc == 124


def classify(rc, err=""):
    if rc == 124:
        return "hang"
    if rc < 0 or rc >= 128:
        return "crash"
    if rc not in CLEAN and rc != 6:
        return "odd-exit-%d" % rc
    if rc == 6:
        return "hang"
    return "ok"


def node_table():
    """[(category, type, [input kinds], [output kinds])] from the generated section of the patch skill."""
    cat, rows = None, []
    for line in open(SKILL, encoding="utf-8"):
        m = re.match(r"### (\w+)", line)
        if m:
            cat = m.group(1)
            continue
        m = re.match(r"\| `([^`]+)` \| (.*?) \| (.*?) \| \d+ \|", line)
        if m and cat:
            ins = [] if m.group(2).strip() == "none" else [x.split(":")[-1] for x in m.group(2).split(", ")]
            outs = [x.split(":")[-1] for x in m.group(3).split(", ") if x.strip() and x.strip() != "none"]
            rows.append((cat, m.group(1), ins, outs))
    return rows


def node_patch(cat, typ, outs):
    t = "infinite-patch 1\nnode 1 %s %s\n  id n\nend\nnode 2 Utility Output\n  id out\nend\n" % (cat, typ)
    if "image" in outs:
        t += "cable out 0 n\n"
    return t


def _fail(kind, name, detail):
    return {"kind": kind, "name": name, "detail": detail.strip().replace("\n", " ")[:160]}


def one_node(binp, row, td):
    cat, typ, ins, outs = row
    name = "%s/%s" % (cat, typ)
    safe = re.sub(r"\W", "_", typ)
    p = os.path.join(td, safe + ".inf")
    open(p, "w").write(node_patch(cat, typ, outs))
    res = []
    rc, _, err = run([binp, "--validate", p, "--lenient"], timeout=60)
    c = classify(rc)
    if c != "ok":
        res.append(_fail(c, name + " validate", err))
    rc, _, err = run([binp, "--frame", p, "0,1", os.path.join(td, safe + "_f")], timeout=90)
    c = classify(rc)
    if c in ("crash", "hang") or c.startswith("odd"):
        res.append(_fail(c, name + " frame", err))
    a, b = os.path.join(td, safe + ".c1"), os.path.join(td, safe + ".c2")
    rc1, _, e1 = run([binp, "--canonicalize", p, a], timeout=60)
    if rc1 == 0 and os.path.exists(a):
        rc2, _, e2 = run([binp, "--canonicalize", a, b], timeout=60)
        if rc2 != 0 or not os.path.exists(b):
            res.append(_fail("resave-fails", name, e2))
        elif open(a, "rb").read() != open(b, "rb").read():
            res.append(_fail("unstable-save", name, "second save differs from first"))
    elif classify(rc1) != "ok":
        res.append(_fail(classify(rc1), name + " canonicalize", e1))
    return name, res


def cmd_nodes(args):
    binp = infinte_or_die()
    rows = [r for r in node_table() if not args.only or r[1] == args.only]
    fails, n = [], 0
    with tempfile.TemporaryDirectory(dir=_tmp()) as td, cf.ThreadPoolExecutor(args.jobs) as ex:
        for name, res in ex.map(lambda r: one_node(binp, r, td), rows):
            n += 1
            fails += res
            for f in res:
                print("FAIL %-14s %s: %s" % (f["kind"], f["name"], f["detail"]))
    print("nodes: %d types, %d failures" % (n, len(fails)))
    return "nodes", n, fails


MUTATORS = {}


def mut(fn):
    MUTATORS[fn.__name__] = fn
    return fn


@mut
def cut(b, r):
    return b[:r.randrange(0, max(1, len(b)))]


@mut
def flip(b, r):
    b = bytearray(b)
    for _ in range(r.randint(1, 8)):
        if b:
            b[r.randrange(len(b))] = r.randrange(256)
    return bytes(b)


@mut
def drop_line(b, r):
    ls = b.split(b"\n")
    del ls[r.randrange(len(ls))]
    return b"\n".join(ls)


@mut
def dup_line(b, r):
    ls = b.split(b"\n")
    i = r.randrange(len(ls))
    ls.insert(i, ls[i])
    return b"\n".join(ls)


@mut
def bad_number(b, r):
    bad = [b"nan", b"inf", b"-inf", b"1e999", b"-1e999", b"99999999999999999999", b"-1", b"0"]
    ls = b.split(b"\n")
    idx = [i for i, l in enumerate(ls) if re.match(rb"\s+[fi] ", l)]
    if idx:
        i = r.choice(idx)
        parts = ls[i].split(b" ")
        parts[-1] = r.choice(bad)
        ls[i] = b" ".join(parts)
    return b"\n".join(ls)


@mut
def wild_index(b, r):
    ls = b.split(b"\n")
    idx = [i for i, l in enumerate(ls) if re.match(rb"(cable|geo|aud|note|mod) ", l)]
    if idx:
        i = r.choice(idx)
        ls[i] = re.sub(rb"\d+", lambda m: r.choice([b"-1", b"999999", b"2147483648", m.group(0)]), ls[i], count=1)
    return b"\n".join(ls)


@mut
def self_cable(b, r):
    return b + b"\ncable 1 0 1\ngeo 1 0 1\naud 1 0 1\nnote 1 0 1\n"


def cmd_fuzz(args):
    binp = infinte_or_die()
    r = random.Random(args.seed)
    seeds = [(s, open(os.path.join(REPO, s), "rb").read()) for s in SEEDS if os.path.exists(os.path.join(REPO, s))]
    jobs = []
    for i in range(args.rounds):
        s, data = seeds[i % len(seeds)]
        m = r.choice(list(MUTATORS))
        jobs.append((i, s, m, MUTATORS[m](data, r)))
    fails = []
    with tempfile.TemporaryDirectory(dir=_tmp()) as td:
        def go(job):
            i, s, m, blob = job
            p = os.path.join(td, "f%d.inf" % i)
            open(p, "wb").write(blob)
            out = []
            for flag in ("--validate", "--canonicalize"):
                cmd = [binp, flag, p] + ([os.path.join(td, "f%d.out" % i)] if flag == "--canonicalize" else [])
                rc, _, err = run(cmd, timeout=60)
                c = classify(rc)
                if c != "ok":
                    keep = os.path.join(OUT, "fuzz-%s-%d-%s.inf" % (m, i, flag.strip("-")))
                    os.makedirs(OUT, exist_ok=True)
                    open(keep, "wb").write(blob)
                    out.append(_fail(c, "%s %s on %s" % (flag, m, s), "kept %s" % keep))
            return out
        with cf.ThreadPoolExecutor(args.jobs) as ex:
            for res in ex.map(go, jobs):
                fails += res
                for f in res:
                    print("FAIL %-14s %s: %s" % (f["kind"], f["name"], f["detail"]))
    print("fuzz: %d damaged files, %d failures" % (args.rounds, len(fails)))
    return "fuzz", args.rounds, fails


def big_patch(n=3000):
    ls = ["infinite-patch 1", "node 1 Source Shape", "  id s", "end"]
    prev = "s"
    for i in range(n):
        ls += ["node %d Effects bloom" % (i + 2), "  id e%d" % i, "end", "cable e%d 0 %s" % (i, prev)]
        prev = "e%d" % i
    ls += ["node %d Utility Output" % (n + 2), "  id out", "end", "cable out 0 %s" % prev]
    return "\n".join(ls) + "\n"


def cmd_killsave(args):
    binp = infinte_or_die()
    r = random.Random(args.seed)
    fails = []
    with tempfile.TemporaryDirectory(dir=_tmp()) as td:
        src = os.path.join(td, "big.inf")
        open(src, "w").write(big_patch())
        tgt = os.path.join(td, "target.inf")
        rc, _, _ = run([binp, "--canonicalize", src, tgt], timeout=120)
        if rc != 0:
            print("killsave: cannot make baseline (rc %d); is Effects bloom a node here?" % rc)
            return "killsave", 0, [_fail("setup", "killsave", "baseline canonicalize rc %d" % rc)]
        good = open(tgt, "rb").read()
        cuts = 0
        for i in range(args.rounds):
            t0 = time.time()
            p = subprocess.Popen([binp, "--canonicalize", src, tgt], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            time.sleep(r.uniform(0.03, 0.2))
            if p.poll() is None:
                p.send_signal(signal.SIGKILL)
                cuts += 1
            p.wait()
            data = open(tgt, "rb").read() if os.path.exists(tgt) else b""
            ok = len(data) > 0 and run([binp, "--validate", tgt, "--lenient"], timeout=60)[0] == 0
            if not ok:
                keep = os.path.join(OUT, "killsave-%d.inf" % i)
                os.makedirs(OUT, exist_ok=True)
                open(keep, "wb").write(data)
                fails.append(_fail("file-lost", "killsave round %d" % i,
                                   "target %d bytes after kill at %.2fs, no longer opens; kept %s" % (len(data), time.time() - t0, keep)))
            leftovers = [f for f in os.listdir(td) if f.startswith("target") and f != "target.inf"]
            for f in leftovers:
                os.remove(os.path.join(td, f))
        print("killsave: %d rounds, %d killed mid-save, %d bad files left on disk" % (args.rounds, cuts, len(fails)))
    return "killsave", args.rounds, fails


def load_describe(binp, td):
    import json
    path = os.path.join(td, "describe.json")
    run([binp, "--describe", "--json", path], timeout=120)
    return [t for t in json.load(open(path))["types"] if not t.get("hardware_driven")]


WIRE = {"image": "cable", "geometry": "geo", "camera": "geo", "light": "geo", "audio": "aud", "note": "note"}


def random_patch(types, r):
    """A small random graph with legal cables (kinds match) and parameters inside their declared range.

    About 60% of graphs also get an LFO bound to 1-3 random modulatable controls per node (random polarity,
    depth and centre): the only way to reach a control registered against the wrong storage (B-K-018).
    About 5% of inputs are wired from a LATER node instead (a cycle): the app must refuse or break it
    cleanly, never crash or hang."""
    pick = [r.choice(types) for _ in range(r.randint(3, 9))]
    lines, outs, wires = ["infinite-patch 1"], [], []
    for i, t in enumerate(pick, 1):
        lines += ["node %d %s %s" % (i, t["category"], t["type"]), "  id n%d" % i]
        for m in t.get("modulatable", []):
            if r.random() < 0.3 and m.get("key") and m["max"] > m["min"]:
                row = next((x for x in t["params"] if x.get("key") == m["key"]), None)
                if row and row["tag"] == "f":
                    lines.append("  f %s %.6g" % (m["key"], r.uniform(m["min"], m["max"])))
        lines.append("end")
        for inp in t["inputs"]:
            w = WIRE.get(inp["kind"])
            srcs = [(j, o) for j, o in outs if o["kind"] == inp["kind"]]
            if w and srcs and r.random() < 0.85:
                j, o = r.choice(srcs)
                wires.append("%s n%d %d n%d" % (w, i, inp["slot"], j))
        outs += [(i, o) for o in t["outputs"]]
    n = len(pick)
    for i, t in enumerate(pick, 1):
        for inp in t["inputs"]:
            w = WIRE.get(inp["kind"])
            later = [(j, o) for j, o in outs if j > i and o["kind"] == inp["kind"]]
            if w and later and r.random() < 0.05:
                j, o = r.choice(later)
                wires.append("%s n%d %d n%d" % (w, i, inp["slot"], j))
    if r.random() < 0.6:
        lines += ["node %d Modulators LFO" % (n + 3), "  id lfo", "  f rateBeats %.3g" % r.choice([0.25, 1, 8, 32]), "end"]
        for i, t in enumerate(pick, 1):
            keys = [m["key"] for m in t.get("modulatable", []) if m.get("key")]
            for k in r.sample(keys, min(len(keys), r.randint(1, 3))):
                wires.append("mod n%d %s lfo 0 %d %.3g %.3g" % (i, k, r.randint(0, 1), r.uniform(0.1, 1.0), r.random()))
    imgs = [j for j, o in outs if o["kind"] == "image"]
    auds = [j for j, o in outs if o["kind"] == "audio"]
    lines += ["node %d Utility Output" % (n + 1), "  id out", "end", "node %d Utility Audio Out" % (n + 2), "  id spk", "end"]
    if imgs:
        wires.append("cable out 0 n%d" % r.choice(imgs))
    if auds:
        wires.append("aud spk 0 n%d" % r.choice(auds))
    return "\n".join(lines + wires) + "\n"


def cmd_random(args):
    binp = infinte_or_die()
    r = random.Random(args.seed)
    fails = []
    os.makedirs(OUT, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=_tmp()) as td:
        types = load_describe(binp, td)
        jobs = [(i, random_patch(types, r)) for i in range(args.rounds)]

        def go(job):
            i, text = job
            p = os.path.join(td, "r%d.inf" % i)
            open(p, "w").write(text)
            out = []
            for name, cmd in (("frame", [binp, "--frame", p, "0,1", os.path.join(td, "r%d_f" % i)]),
                              ("audio", [binp, "--audio-summary", p, os.path.join(td, "r%d.json" % i)]),
                              ("save", [binp, "--canonicalize", p, os.path.join(td, "r%d.c" % i)])):
                rc, _, err = run(cmd, timeout=120)
                c = classify(rc)
                if c in ("crash", "hang") or c.startswith("odd"):
                    keep = os.path.join(OUT, "random-%d-%s.inf" % (i, name))
                    open(keep, "w").write(text)
                    out.append(_fail(c, "random patch %d %s" % (i, name), "kept %s" % keep))
            return out
        with cf.ThreadPoolExecutor(args.jobs) as ex:
            for res in ex.map(go, jobs):
                fails += res
                for f in res:
                    print("FAIL %-14s %s: %s" % (f["kind"], f["name"], f["detail"]))
    print("random: %d patches, %d failures" % (args.rounds, len(fails)))
    return "random", args.rounds, fails


def infinte_or_die():
    b = infinite_bin()
    if not b:
        raise SystemExit("stress_kit: no Infinite build (set INFINITE_BIN)")
    return b


def _tmp():
    d = os.path.join(RE_OUTPUT, "tmp")
    os.makedirs(d, exist_ok=True)
    return d


def cmd_all(args):
    os.makedirs(OUT, exist_ok=True)
    results = [cmd_nodes(args), cmd_fuzz(args), cmd_random(args)]
    args.rounds = 40
    results.append(cmd_killsave(args))
    lines = ["# Stress kit results", "", "| Test | Cases | Failures |", "|---|---|---|"]
    for name, n, fails in results:
        lines.append("| %s | %d | %d |" % (name, n, len(fails)))
    lines += ["", "## Failures", ""]
    allf = [f for _, _, fs in results for f in fs]
    lines += ["- **%s** %s: %s" % (f["kind"], f["name"], f["detail"]) for f in allf] or ["none"]
    lines += [""] + ['QUALITY id=stress.failures subject="Infinite: %s" value=%.4f' % (name, len(fails)) for name, _, fails in results]
    with open(os.path.join(OUT, "results.md"), "w") as fh:
        fh.write("\n".join(lines) + "\n")
    print("wrote %s" % os.path.join(OUT, "results.md"))


def selftest():
    rows = node_table()
    assert len(rows) > 250, len(rows)
    assert classify(-11) == "crash" and classify(124) == "hang" and classify(3) == "ok" and classify(139) == "crash"
    r = random.Random(1)
    d = b"node 1 A B\n  f x 1.0\nend\ncable 1 0 1\n"
    for m in MUTATORS.values():
        m(d, r)
    assert big_patch(3).count("node ") == 5
    fake = [{"category": "C", "type": "T%d" % k, "params": [], "inputs": [{"kind": "audio", "slot": 0}],
             "outputs": [{"kind": "audio", "output": 0}], "modulatable": [{"key": "gain", "min": 0, "max": 1}]}
            for k in range(3)]
    texts = [random_patch(fake, random.Random(s)) for s in range(200)]
    assert any("mod n" in t and "id lfo" in t for t in texts), "random_patch never modulates"
    assert any(re.search(r"aud n(\d+) 0 n(\d+)", t) and any(int(a) < int(b) for a, b in re.findall(r"aud n(\d+) 0 n(\d+)", t))
               for t in texts), "random_patch never makes a back-edge"
    print("stress_kit selftest ok (%d node types)" % len(rows))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for n in ("nodes", "fuzz", "random", "killsave", "all"):
        s = sub.add_parser(n)
        s.add_argument("--jobs", type=int, default=4)
        s.add_argument("--only")
        s.add_argument("--rounds", type=int, default=300 if n != "killsave" else 40)
        s.add_argument("--seed", type=int, default=1)
    sub.add_parser("selftest")
    a = ap.parse_args()
    if a.cmd == "selftest":
        return selftest()
    if a.cmd == "all":
        a.rounds = 300
    {"nodes": cmd_nodes, "fuzz": cmd_fuzz, "random": cmd_random, "killsave": cmd_killsave, "all": cmd_all}[a.cmd](a)


if __name__ == "__main__":
    main()
