#!/usr/bin/env python3
"""How repetitive is an atlas? Structural fingerprints of patches and set-level diversity.
    diversity.py atlas.jsonl [more.jsonl]
Metrics (higher = more varied unless noted):
  motif_unique   distinct node-type 3-grams / total 3-grams   (low = same chains again and again)
  top_motif      share of the single most common 3-gram        (high = one chain dominates)
  size_spread    stdev of node counts / mean
  mean_dist      mean Jaccard distance between random patch pairs (node-type bags)
  shape_unique   distinct (kind, node-count bucket, #branches-ish) shapes / patches"""
import json, random, statistics as st, sys
from collections import Counter


def grams(names, n=3):
    return [tuple(names[i:i + n]) for i in range(max(0, len(names) - n + 1))]


def fingerprint(r):
    return {"bag": frozenset(r.get("names", [])), "grams": grams(r.get("names", []))}


def measure(rows, pairs=2000, seed=1):
    rows = [r for r in rows if r.get("names")]
    fps = [fingerprint(r) for r in rows]
    allg = [g for f in fps for g in f["grams"]]
    c = Counter(allg)
    sizes = [len(r["names"]) for r in rows]
    rnd = random.Random(seed)
    d = []
    for _ in range(pairs):
        a, b = rnd.sample(fps, 2)
        u = len(a["bag"] | b["bag"]) or 1
        d.append(1 - len(a["bag"] & b["bag"]) / u)
    shapes = {(r["code"].split(" ")[0], len(r["names"]) // 8) for r in rows}
    return {"patches": len(rows), "motif_unique": round(len(c) / max(1, len(allg)), 3), "top_motif": round(c.most_common(1)[0][1] / max(1, len(allg)), 3),
            "top3": [(" > ".join(g), n) for g, n in c.most_common(3)], "size_spread": round(st.pstdev(sizes) / st.mean(sizes), 2),
            "mean_dist": round(st.mean(d), 3), "shape_unique": round(len(shapes) / len(rows), 3)}


if __name__ == "__main__":
    rows = [json.loads(l) for p in sys.argv[1:] for l in open(p)]
    print(json.dumps(measure(rows), indent=1))
