#!/usr/bin/env python3
"""Is the knobs -> sound score limited by the data or by the yardstick model?

    sound_model_probe.py --engine "u-he diva" --root /Library/Audio/Presets/u-he/Diva --renders preset_diva.jsonl --prefix state:diva:

Same join, target and held-out 20% as marginal_gate.py (C3 row: 32 log bands + 11 scalars, mean R2 over outputs). Compares, on that
held-out set: mean (R2 0 by definition), linear ridge, k-nearest presets in knob space, and a small MLP (numpy, Adam, early stopping on a
slice of the training pool). Learning curve for the best model, so the stop line of the gate is set against a real model, not ridge."""
import math, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import marginal_gate as mg


def r2(Yt, P):
    v = ((Yt - Yt.mean(0)) ** 2).sum(0)
    return 1 - ((Yt - P) ** 2).sum(0) / np.maximum(v, 1e-12)


def ridge(X, Y, Xq, lam):
    mu = Y.mean(0)
    return Xq @ np.linalg.solve(X.T @ X + lam * np.eye(X.shape[1]), X.T @ (Y - mu)) + mu


def knn(X, Y, Xq, k):
    d = (Xq ** 2).sum(1)[:, None] - 2 * Xq @ X.T + (X ** 2).sum(1)[None]
    idx = np.argsort(d, 1)[:, :k]
    w = 1 / (np.take_along_axis(d, idx, 1).clip(1e-6) ** 0.5)
    return (Y[idx] * w[..., None]).sum(1) / w.sum(1, keepdims=True)


def mlp(X, Y, Xq, rng, hidden=256, drop=0.2, wd=1e-4, epochs=400, lr=1e-3):
    """Two hidden layers, GELU-free (ReLU), dropout, AdamW, early stop on 15% of X. Targets standardised per output."""
    ym, ys = Y.mean(0), Y.std(0) + 1e-9; Yn = (Y - ym) / ys
    p = rng.permutation(len(X)); nv = max(len(X) // 7, 20); va, tr = p[:nv], p[nv:]
    sz = [X.shape[1], hidden, hidden, Y.shape[1]]
    W = [rng.normal(0, math.sqrt(2 / a), (a, b)) for a, b in zip(sz, sz[1:])]; B = [np.zeros(b) for b in sz[1:]]
    m = [np.zeros_like(w) for w in W + B]; v = [np.zeros_like(w) for w in W + B]; t = 0
    def fwd(x, train):
        hs = [x]
        for i in range(2):
            h = np.maximum(hs[-1] @ W[i] + B[i], 0)
            if train: h = h * (rng.random(h.shape) > drop) / (1 - drop)
            hs.append(h)
        return hs, hs[-1] @ W[2] + B[2]
    best, keep, bad = 1e9, None, 0
    for ep in range(epochs):
        for s in np.array_split(rng.permutation(tr), max(len(tr) // 64, 1)):
            hs, out = fwd(X[s], True); g = 2 * (out - Yn[s]) / len(s)
            gW, gB = [None] * 3, [None] * 3
            for i in (2, 1, 0):
                gW[i] = hs[i].T @ g; gB[i] = g.sum(0)
                if i: g = (g @ W[i].T) * (hs[i] > 0)
            t += 1
            for j, (w, gw) in enumerate(zip(W + B, gW + gB)):
                m[j] = 0.9 * m[j] + 0.1 * gw; v[j] = 0.999 * v[j] + 0.001 * gw * gw
                w -= lr * (m[j] / (1 - 0.9 ** t) / (np.sqrt(v[j] / (1 - 0.999 ** t)) + 1e-8) + (wd * w if j < 3 else 0))
        loss = float(((fwd(X[va], False)[1] - Yn[va]) ** 2).mean())
        if loss < best - 1e-4: best, keep, bad = loss, ([w.copy() for w in W], [b.copy() for b in B]), 0
        else:
            bad += 1
            if bad >= 25: break
    W, B = keep
    return fwd(Xq, False)[1] * ys + ym


def main():
    a = mg.args()
    X, Y, ids, te, pool, rng = mg.load(a)
    print("%d presets, %d knob columns, %d held out, %d outputs (32 bands + 11 scalars)" % (len(ids), X.shape[1], len(te), Y.shape[1]))
    def report(name, P):
        s = r2(Y[te], P)
        print("  %-22s R2 %.3f   bands %.3f   scalars %.3f" % (name, s.mean(), s[:32].mean(), s[32:].mean()))
        return s.mean()
    Xp, Yp = X[pool], Y[pool]
    report("ridge (linear)", max((ridge(Xp, Yp, X[te], l) for l in (30, 300, 3000)), key=lambda P: r2(Y[te], P).mean()))
    for k in (1, 5, 20):
        report("knn k=%d" % k, knn(Xp, Yp, X[te], k))
    report("mlp 256x2", mlp(Xp, Yp, X[te], rng))
    print("learning curve, mlp (held-out R2):")
    n = 400
    while n < len(pool):
        print("  N=%5d  %.3f" % (n, r2(Y[te], mlp(X[pool[:n]], Y[pool[:n]], X[te], rng)).mean())); n *= 2
    print("  N=%5d  (all, above)" % len(pool))


if __name__ == "__main__":
    main()
