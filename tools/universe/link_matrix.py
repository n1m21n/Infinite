#!/usr/bin/env python3
"""W5 link layer, stage (b): low-rank matrix link (next-phase.md section 8). Same events and split as link_baseline.py.
score(role r, tenth t | context g, mood bin m) = bias[r,t] + U[r] . (Wg[g] + Wm[m] + Wt[t] + Wgt[g,t]-free) ; logistic, rank K, Adam full batch.
Reports held-out bits/event next to the best count baseline (B2); the bar is >= 0.10 bits better."""
import argparse, hashlib, json, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from link_baseline import TD, ROLES, bin3, context

def load():
    songs = [json.loads(l) for l in open(os.path.join(TD, "link", "songs.jsonl"))]
    h = lambda s: int(hashlib.md5(s["src"].encode()).hexdigest(), 16) % 5
    tr = [s for s in songs if h(s) != 0]; te = [s for s in songs if h(s) == 0]
    vals = {r: [] for r in ROLES}
    for s in tr:
        for r, v in s["act"].items(): vals[r] += v
    med = {r: float(np.median(v)) for r, v in vals.items()}
    ctx = {c: i for i, c in enumerate(sorted({context(s) for s in songs}))}
    def arr(S):
        G, M, R, T, Y = [], [], [], [], []
        for s in S:
            g = ctx[context(s)]
            m = (bin3(s["mood"]["valence"]) or 0) * 3 + (bin3(s["mood"]["energy"]) or 0)
            for r, v in s["act"].items():
                for t, x in enumerate(v):
                    G.append(g); M.append(m); R.append(ROLES.index(r)); T.append(t); Y.append(x > med[r])
        return [np.array(a) for a in (G, M, R, T)] + [np.array(Y, float)]
    return arr(tr), arr(te), len(ctx)

def fit(a):
    (G, M, R, T, Y), te, ng = load()
    K = a.rank; rng = np.random.default_rng(0)
    P = {"b": np.zeros((9, 10)), "U": rng.normal(0, .1, (9, K)), "Wg": rng.normal(0, .1, (ng, K)), "Wm": rng.normal(0, .1, (9, K)), "Wt": rng.normal(0, .1, (10, K))}
    m1 = {k: np.zeros_like(v) for k, v in P.items()}; m2 = {k: np.zeros_like(v) for k, v in P.items()}
    def fwd(P, G, M, R, T):
        h = P["Wg"][G] + P["Wm"][M] + P["Wt"][T]
        return P["b"][R, T] + (P["U"][R] * h).sum(1), h
    def bits(P, D):
        z, _ = fwd(P, *D[:4]); p = np.clip(1 / (1 + np.exp(-z)), 1e-4, 1 - 1e-4)
        return float(-(D[4] * np.log2(p) + (1 - D[4]) * np.log2(1 - p)).mean())
    n = len(Y)
    for it in range(1, a.iters + 1):
        z, h = fwd(P, G, M, R, T); d = (1 / (1 + np.exp(-z)) - Y) / n
        g = {k: np.zeros_like(v) for k, v in P.items()}
        np.add.at(g["b"], (R, T), d)
        np.add.at(g["U"], R, d[:, None] * h)
        dh = d[:, None] * P["U"][R]
        np.add.at(g["Wg"], G, dh); np.add.at(g["Wm"], M, dh); np.add.at(g["Wt"], T, dh)
        for k in P:
            g[k] += a.l2 * P[k] if k != "b" else 0
            m1[k] = .9 * m1[k] + .1 * g[k]; m2[k] = .999 * m2[k] + .001 * g[k] ** 2
            P[k] -= a.lr * (m1[k] / (1 - .9 ** it)) / (np.sqrt(m2[k] / (1 - .999 ** it)) + 1e-8)
        if it % 50 == 0: print("it %d train %.4f test %.4f" % (it, bits(P, (G, M, R, T, Y)), bits(P, te)), flush=True)
    print("rank %d held-out %.4f bits/event (count baseline B2 0.9725; bar <= 0.8725)" % (K, bits(P, te)))

if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("--rank", type=int, default=16); ap.add_argument("--iters", type=int, default=300)
    ap.add_argument("--lr", type=float, default=.05); ap.add_argument("--l2", type=float, default=1e-4)
    fit(ap.parse_args())
