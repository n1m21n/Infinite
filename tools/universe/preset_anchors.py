#!/usr/bin/env python3
"""Pick anchor presets from measured human presets: k-means in standardized axis space, one medoid per cluster.

    preset_anchors.py --file ~/Infinite/training-data/characteristics/preset_diva_v1.jsonl [-k 20] [--out anchors_diva.json]

Reads rows {src, axes:{name:{mean}}}; rows missing an axis are dropped. Prints cluster sizes, the medoid src and its axis z-scores.
Medoids are real human presets (source path kept), so each anchor has a known origin."""
import argparse, json, os
import numpy as np


def load(path):
    rows = [json.loads(l) for l in open(os.path.expanduser(path)) if l.strip()]
    cov = {}
    for r in rows:
        for k, v in r.get("axes", {}).items():
            if v.get("mean") is not None: cov[k] = cov.get(k, 0) + 1
    names = sorted(k for k, c in cov.items() if c >= 0.85 * len(rows))  # axes most rows have; sparse ones (movement) dropped
    keep = [r for r in rows if all(n in r.get("axes", {}) and r["axes"][n].get("mean") is not None for n in names)]
    X = np.array([[r["axes"][n]["mean"] for n in names] for r in keep], float)
    ok = np.isfinite(X).all(1)
    return [r["src"] for r, o in zip(keep, ok) if o], names, X[ok]


def kmeans(Z, k, seed=0, it=50):
    rng = np.random.default_rng(seed); C = Z[rng.choice(len(Z), k, replace=False)]
    for _ in range(it):
        a = ((Z[:, None, :] - C[None]) ** 2).sum(2).argmin(1)
        N = np.array([Z[a == j].mean(0) if (a == j).any() else C[j] for j in range(k)])
        if np.allclose(N, C): break
        C = N
    return a, C


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--file", required=True); ap.add_argument("-k", type=int, default=20); ap.add_argument("--out")
    a = ap.parse_args(); src, names, X = load(a.file)
    lo, hi = np.percentile(X, [1, 99], 0); X = np.clip(X, lo, hi)
    Z = (X - X.mean(0)) / (X.std(0) + 1e-9)
    lab, C = kmeans(Z, a.k); out = []
    print("%d presets, %d axes (%s), k=%d" % (len(src), len(names), ",".join(names), a.k))
    for j in np.argsort(-np.bincount(lab, minlength=a.k)):
        idx = np.flatnonzero(lab == j)
        if not len(idx): continue
        m = idx[((Z[idx] - C[j]) ** 2).sum(1).argmin()]
        out.append({"src": src[m], "cluster_size": int(len(idx)), "z": dict(zip(names, np.round(Z[m], 2).tolist()))})
        print("%5d  %s" % (len(idx), src[m][:90]))
    if a.out: json.dump(out, open(os.path.expanduser(a.out), "w"), indent=1)


if __name__ == "__main__":
    main()
