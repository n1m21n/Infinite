#!/usr/bin/env python3
"""Summarise atlas jsonl files: audible rate per node type, per adjacent pair, worst offenders.
Rows written before `names` existed are skipped (their indices may not decode)."""
import argparse, collections, json, sys


def load(paths):
    for p in paths:
        for l in open(p):
            r = json.loads(l)
            if r.get("names"):
                yield r


def audible(r):
    return r["class"] in ("ok", "clipped")


def table(title, stats, minn, top, worst=True):
    rows = [(k, v[0], v[1]) for k, v in stats.items() if v[1] >= minn]
    rows.sort(key=lambda x: (x[1] / x[2], -x[2]), reverse=not worst)
    print("\n%s (n>=%d)" % (title, minn))
    for k, a, n in rows[:top]:
        print("  %5.1f%%  %3d/%-3d  %s" % (100 * a / n, a, n, k))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--min", type=int, default=5)
    ap.add_argument("--top", type=int, default=12)
    a = ap.parse_args()
    rows = list(load(a.files))
    if not rows:
        sys.exit("no rows with node names")
    cls = collections.Counter(r["class"] for r in rows)
    print("patches %d  audible %.1f%%  %s" % (len(rows), 100 * sum(audible(r) for r in rows) / len(rows), dict(cls)))
    node, pair = collections.defaultdict(lambda: [0, 0]), collections.defaultdict(lambda: [0, 0])
    for r in rows:
        for n in set(r["names"]):
            node[n][1] += 1; node[n][0] += audible(r)
        for x, y in zip(r["names"], r["names"][1:]):
            k = "%s > %s" % (x, y)
            pair[k][1] += 1; pair[k][0] += audible(r)
    table("worst node types", node, a.min, a.top)
    table("worst adjacent pairs", pair, a.min, a.top)
    table("best node types", node, a.min, 5, worst=False)
    errs = collections.Counter(r["class"] for r in rows if r["class"].startswith("error"))
    if errs:
        print("\nerrors:", dict(errs))


if __name__ == "__main__":
    main()
