#!/usr/bin/env python3
"""Render order for a preset library: farthest-first (k-center greedy) over the preset's own knob values.

    coreset_order.py --engine "u-he diva" --root "/Library/Audio/Presets/u-he/Diva" --out order_diva.txt

Every parsed recipe row (training-data/presets/*.jsonl) becomes a vector of its numeric knob values (standardised, PCA to --dims).
Farthest-first picks the preset least like everything chosen so far, so ANY prefix of the order is a good cover of the whole library:
render in this order and stop when the coverage radius stops falling. Writes one relative path per line (the src minus --root) and a
report of the coverage radius at k = 100 ... N (fraction of the starting radius; k-center greedy is within 2x of the best possible).
Refs: Sener & Savarese 2018 (core-set / k-center greedy); torchsynth (Turian 2021) on uniform parameter sampling wasting renders."""
import argparse, glob, json, os, sys
import numpy as np


def vec(modules):
    out = {}
    for m, ps in (modules or {}).items():
        for k, v in (ps or {}).items():
            if isinstance(v, (int, float)) and not isinstance(v, bool):
                out["%s.%s" % (m, k)] = float(v)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", required=True)
    ap.add_argument("--root", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--presets", default="~/Infinite/training-data/presets")
    ap.add_argument("--dims", type=int, default=32)
    ap.add_argument("--seed-src", default="")
    a = ap.parse_args()
    rows, feats = [], []
    for f in sorted(glob.glob(os.path.join(os.path.expanduser(a.presets), "*.jsonl"))):
        for ln in open(f):
            if '"engine":"%s"' % a.engine not in ln.replace('": "', '":"'):
                continue
            r = json.loads(ln)
            if r.get("src", "").startswith(a.root):
                rows.append(r["src"][len(a.root):].lstrip("/"))
                feats.append(vec(r.get("modules")))
    seen = {}
    keep = [i for i, s in enumerate(rows) if not (s in seen or seen.setdefault(s, i) != i)]
    rows, feats = [rows[i] for i in keep], [feats[i] for i in keep]
    cols = sorted({k for d in feats for k in d})
    X = np.array([[d.get(c, np.nan) for c in cols] for d in feats], dtype=np.float32)
    mu = np.nanmean(X, 0)
    X = np.where(np.isnan(X), mu, X)
    sd = X.std(0)
    ok = sd > 1e-9
    X = (X[:, ok] - X[:, ok].mean(0)) / sd[ok]
    k = min(a.dims, X.shape[1])
    U, S, Vt = np.linalg.svd(X - X.mean(0), full_matrices=False)
    Z = (U[:, :k] * S[:k]).astype(np.float32)
    n = len(Z)
    first = rows.index(a.seed_src) if a.seed_src in rows else int(np.argmin(((Z - Z.mean(0)) ** 2).sum(1)))
    order, d = [first], ((Z - Z[first]) ** 2).sum(1)
    radii = [float(np.sqrt(d.max()))]
    for _ in range(n - 1):
        j = int(np.argmax(d))
        order.append(j)
        radii.append(float(np.sqrt(d[j])))
        d = np.minimum(d, ((Z - Z[j]) ** 2).sum(1))
    open(a.out, "w").write("\n".join(rows[i] for i in order) + "\n")
    print("%d presets, %d knob columns (%d varying), PCA %d dims" % (n, len(cols), int(ok.sum()), k))
    for kk in (50, 100, 250, 500, 1000, 2000, 3000, 5000, 8000):
        if kk < n:
            print("  k=%5d  coverage radius %.3f of start" % (kk, radii[kk] / radii[1]))


if __name__ == "__main__":
    main()
