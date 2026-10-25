#!/usr/bin/env python3
"""Robustness check of the generator itself. Fast, no probing unless --validate N is given.
    selftest.py [--n 300] [--validate 40 --bin <Infinite>]
Checks: genomes round-trip through compile deterministically; every cable names existing nodes; no node has an input wired twice;
every mod targets a node that exists; code params are numeric-indexed; every kind appears; diversity stays above floor."""
import argparse, os, random, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import universe as u, scene, diversity

FLOORS = {"motif_unique": 0.9, "mean_dist": 0.85}


def lint(text):
    ids, wired, errs, custom, cur = set(), set(), [], set(), None
    for line in text.split("\n"):
        m = re.match(r"node (\d+) ", line)
        if m:
            ids.add(int(m.group(1))); cur = int(m.group(1))
        elif line.startswith("  s code "):
            custom.add(cur)  # custom code: p.<name> keys only resolve as numeric indices; default-code nodes resolve by key
    for line in text.split("\n"):
        f = line.split(" ")
        if f[0] in ("cable", "aud", "note", "geo") and len(f) == 4:
            dst, slot, src = int(f[1]), int(f[2]), int(f[3])
            if dst not in ids or src not in ids: errs.append("cable to missing node: " + line)
            if (dst, slot, f[0]) in wired: errs.append("input wired twice: " + line)
            wired.add((dst, slot, f[0]))
        elif f[0] == "mod" and len(f) > 2:
            if f[1].isdigit() and int(f[1]) not in ids: errs.append("mod on missing node: " + line)
            if f[1].isdigit() and int(f[1]) in custom and f[2].startswith("p."): errs.append("unindexed code param: " + line)
    return errs


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--n", type=int, default=300); ap.add_argument("--validate", type=int, default=0); ap.add_argument("--bin", default=u.DEFAULT_BIN)
    a = ap.parse_args()
    facts = u.load_facts(); P = scene.pools_v2(facts); r = random.Random(4242)
    codes = [scene.sample_scene(r, P, facts) for _ in range(a.n)]
    fails, kinds, rows = [], set(), []
    for c in codes:
        kinds.add(c.split(" ")[0])
        try:
            t1, t2 = scene.compile_scene(c, facts, P), scene.compile_scene(c, facts, P)
            if t1 != t2: fails.append(("nondeterministic", c))
            fails += [(e, c) for e in lint(t1)]
            rows.append({"code": c, "names": scene.names_of(c, facts, P)})
        except Exception as e:
            fails.append(("compile raised %s: %s" % (type(e).__name__, e), c))
    if kinds != {"A", "V", "AV"}: fails.append(("missing kinds %s" % kinds, ""))
    m = diversity.measure(rows)
    for k, fl in FLOORS.items():
        if m[k] < fl: fails.append(("diversity %s %.3f < %.2f" % (k, m[k], fl), ""))
    if a.validate:
        for c in random.Random(1).sample(codes, min(a.validate, len(codes))):
            p = "/tmp/selftest.inf"; open(p, "w").write(scene.compile_scene(c, facts, P))
            cp = subprocess.run([a.bin, "--validate", p], capture_output=True, text=True)
            if cp.returncode: fails.append(("validate exit %d" % cp.returncode, c))
    print("genomes %d  diversity %s  failures %d" % (a.n, {k: m[k] for k in FLOORS}, len(fails)))
    for f in fails[:10]: print(" ", f[0], "|", f[1][:100])
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
