#!/usr/bin/env python3
"""F3: re-probe audible atlas rows and compare the audio summary. Prints non-deterministic patches."""
import argparse, json, os, random, sys, tempfile
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import universe as u


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("atlas")
    ap.add_argument("--bin", default=u.DEFAULT_BIN)
    ap.add_argument("-n", type=int, default=40)
    ap.add_argument("--secs", type=float, default=4)
    ap.add_argument("--workers", type=int, default=2)
    ap.add_argument("--out", default="determinism.jsonl")
    a = ap.parse_args()
    facts = u.load_facts()
    P = u.pools(facts)
    rows = [json.loads(l) for l in open(a.atlas)]
    rows = [r for r in rows if r["class"] in ("ok", "clipped")]
    random.Random(1).shuffle(rows)
    rows = rows[: a.n]
    with tempfile.TemporaryDirectory() as wd, ThreadPoolExecutor(a.workers) as ex:
        again = list(ex.map(lambda r: u.probe(r["code"], facts, P, a.bin, a.secs, wd), rows))
    bad = exact = near = 0
    with open(a.out, "a") as f:
        for r, b in zip(rows, again):
            same = r["a"] == b["a"]
            l1, l2 = r["a"].get("integrated_lufs"), b["a"].get("integrated_lufs")
            close = l1 is not None and l2 is not None and abs(l1 - l2) <= 1.0 and r["class"] == b["class"]
            exact += same
            near += close
            bad += not close
            f.write(json.dumps({"code": r["code"], "same": same, "a1": r["a"], "a2": b["a"]}) + "\n")
            if not close:
                diff = {k: (r["a"].get(k), b["a"].get(k)) for k in set(r["a"]) | set(b["a"]) if r["a"].get(k) != b["a"].get(k)}
                print("DIFF", r["code"][:90], list(diff.items())[:3])
    print("bit-identical %d, within 1 LU %d, unstable %d of %d -> %s" % (exact, near, bad, len(rows), a.out))


if __name__ == "__main__":
    main()
