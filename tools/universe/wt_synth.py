#!/usr/bin/env python3
"""W3 synthetic wavetable frames (docs/plans/data-universe/next-phase.md). Frames are 64 harmonic magnitudes (wt.jsonl).
    wt_synth.py [--n 20000] [--out ~/Infinite/training-data/wavetables/wt_synth.jsonl]
Samplers: morph (log-domain blend of two real frames of one table), pca (Gaussian in the leading components of log-harmonics),
formula (saw, square, pulse, formant, comb with jitter). Every row is tagged synthetic:true. Check: a cross-validated linear
real-vs-synthetic classifier must stay at AUC <= 0.60 (W3 bar); formula frames are easy to tell apart, so they are capped at 10% of the mix. Training-only: never enters --scope shippable without the owner."""
import argparse, json, os
import numpy as np

TD = os.path.expanduser("~/Infinite/training-data/wavetables")


def feats(h):
    h = np.asarray(h, float)
    k = np.arange(1, 65)
    odd, even = h[0::2].sum(), h[1::2].sum()
    return np.concatenate([np.log(h + 1e-3), [odd / (odd + even + 1e-9), (h * k).sum() / (h.sum() + 1e-9) / 64]])


def norm(h):
    h = np.clip(h, 0, None)
    return np.round(h / max(h.max(), 1e-9), 4)


def load():
    tabs = []
    for ln in open(os.path.join(TD, "wt.jsonl")):
        r = json.loads(ln)
        tabs.append([np.array(s["harm"], float) for s in r["shapes"] if len(s["harm"]) == 64])
    return [t for t in tabs if t]


def formula(rng):
    k = np.arange(1, 65, dtype=float)
    f = rng.choice(["saw", "square", "pulse", "formant", "comb"])
    if f == "saw":
        h = k ** -rng.uniform(0.6, 1.4)
    elif f == "square":
        h = np.where(k % 2 == 1, k ** -rng.uniform(0.8, 1.6), rng.uniform(0, 0.05) * k ** -1.0)
    elif f == "pulse":
        w = rng.uniform(0.05, 0.5)
        h = np.abs(np.sin(np.pi * k * w)) / k
    elif f == "formant":
        h = sum(np.exp(-0.5 * ((k - c) / rng.uniform(1, 4)) ** 2) * rng.uniform(0.3, 1) for c in rng.uniform(1, 30, rng.integers(1, 4)))
        h = h + 0.02 * k ** -1.0
    else:
        p = rng.integers(2, 6)
        h = np.where(k % p == 0, 1.0, rng.uniform(0.02, 0.2)) * k ** -rng.uniform(0.2, 0.9)
    return h * np.exp(rng.normal(0, 0.15, 64))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=20000)
    ap.add_argument("--out", default=os.path.join(TD, "wt_synth.jsonl"))
    a = ap.parse_args()
    rng = np.random.default_rng(7)
    tabs = load()
    real = np.array([h for t in tabs for h in t])
    L = np.log(real + 1e-3)
    mu = L.mean(0)
    u, s, vt = np.linalg.svd(L - mu, full_matrices=False)
    nc = int((np.cumsum(s ** 2) / (s ** 2).sum() < 0.95).sum()) + 1
    sd = s[:nc] / np.sqrt(len(L))
    out = []
    for i in range(a.n):
        m = ("morph", "pca", "morph", "pca", "morph", "pca", "morph", "pca", "morph", "formula")[i % 10]  # formulas capped at 10%
        if m == "morph":
            t = tabs[rng.integers(len(tabs))]
            x, y = t[rng.integers(len(t))], t[rng.integers(len(t))]
            w = rng.uniform()
            h = np.exp((1 - w) * np.log(x + 1e-3) + w * np.log(y + 1e-3)) - 1e-3
        elif m == "pca":
            h = np.exp(mu + (rng.normal(size=nc) * sd) @ vt[:nc]) - 1e-3
        else:
            h = formula(rng)
        out.append({"harm": norm(h).tolist(), "method": m, "synthetic": True})
    with open(a.out, "w") as f:
        for r in out:
            f.write(json.dumps(r, separators=(",", ":")) + "\n")
    print("wrote %d synthetic frames (pca comps %d) to %s" % (len(out), nc, a.out))
    # real-vs-synthetic classifier, 5-fold, ridge-regularised linear model on standardised features
    Xr = np.array([feats(h) for h in real])
    Xs = np.array([feats(r["harm"]) for r in out])
    meth = np.array([r["method"] for r in out])
    nr = min(len(Xr), 6000)
    Xr = Xr[rng.permutation(len(Xr))[:nr]]

    def auc(Xa, Xb):
        X = np.vstack([Xa, Xb]); y = np.r_[np.zeros(len(Xa)), np.ones(len(Xb))]
        idx = rng.permutation(len(y)); X, y = X[idx], y[idx]
        sc = np.zeros(len(y))
        for f in range(5):
            te = np.arange(len(y)) % 5 == f
            m, sdv = X[~te].mean(0), X[~te].std(0) + 1e-9
            A = np.c_[(X[~te] - m) / sdv, np.ones((~te).sum())]
            w = np.linalg.solve(A.T @ A + 10 * np.eye(A.shape[1]), A.T @ (2 * y[~te] - 1))
            sc[te] = np.c_[(X[te] - m) / sdv, np.ones(te.sum())] @ w
        r = sc.argsort().argsort() + 1
        n1, n0 = y.sum(), (1 - y).sum()
        return (r[y == 1].sum() - n1 * (n1 + 1) / 2) / (n1 * n0)
    print("AUC real vs synthetic, all: %.3f (bar <= 0.60)" % auc(Xr, Xs[rng.permutation(len(Xs))[:nr]]))
    for m in ("morph", "pca", "formula"):
        print("  %-8s %.3f" % (m, auc(Xr, Xs[meth == m][:nr])))


if __name__ == "__main__":
    main()
