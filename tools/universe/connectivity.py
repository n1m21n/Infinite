#!/usr/bin/env python3
"""Does the forward model know what each knob does? Model activity vs measured activity, plus knob-pair interactions.   (ml-venv python)

    connectivity.py --node Analog [--bases 200] [--top 10] [--epochs 300]

1. Train the forward model (same recipe as propose.py) on the engine's renders (one seed).
2. Model activity of knob j = mean over `bases` random rendered points of |f(x with knob j redrawn) - f(x)| on the 11 mean columns,
   each scaled by its spread. Compared with the measured activity of activity.py (stored one-knob moves, never shown to this step)
   by rank correlation: if the model understands the parts, the busy knobs are the same.
3. Interaction of a pair (a, b) = mean |f(ab) - f(a) - f(b) + f(x)| (what two moves do beyond the sum of each), top knobs only.
   A pair with high interaction is 'connected' (the effect of one depends on the other), like regions that fire together."""
import argparse, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import own_sweeps as o, p3_characteristics as p3, m1_forward as m1, sound_data as sd, sound_eval as se, activity as act  # noqa: E402


def rank(x):
    r = np.empty(len(x)); r[np.argsort(x)] = np.arange(len(x)); return r


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--node", required=True); ap.add_argument("--bases", type=int, default=200)
    ap.add_argument("--top", type=int, default=10); ap.add_argument("--epochs", type=int, default=300); a = ap.parse_args()
    keys, d, V, means, names, eff = act.measure(a.node)
    keep = np.flatnonzero(d["ok"][:, 0]); K = len(keys)
    Va = np.full((len(d["preset_id"]), len(p3.COLS)), np.nan)
    for i in keep: Va[i] = V[i]
    V = Va; rng = np.random.default_rng(0)
    B = d["knobs"][rng.choice(keep, a.bases)]                       # base points: rendered knob vectors
    draw = lambda n: np.where(rng.random(n) < .5, rng.beta(.5, .5, n), rng.random(n))
    blocks = [B]                                                    # 0: bases
    for j in range(K):
        M = B.copy(); M[:, j] = draw(len(B)); blocks.append(M)      # 1..K: knob j redrawn
    meas = np.array([np.nanmean(np.array(eff[j])) if len(eff[j]) >= 5 else np.nan for j in range(K)])
    order = np.argsort(-np.nan_to_num(meas))[:a.top]; pairs = [(order[i], order[k]) for i in range(len(order)) for k in range(i + 1, len(order))]
    for (p, q) in pairs:
        M = B.copy(); M[:, p] = blocks[1 + p][:, p]; M[:, q] = blocks[1 + q][:, q]; blocks.append(M)
    allc = np.vstack(blocks); XX = se.encode(np.vstack([d["knobs"], allc])); X, XC = XX[:len(d["knobs"])], XX[len(d["knobs"]):]
    mv = np.array([[48, 100]], float) / 127
    perm = np.random.default_rng(0).permutation(keep); nva = max(1, int(.15 * len(perm))); va, fit = perm[:nva], perm[nva:]
    Xa, _, Ma, Ia, Ta = m1.samples(d, X, fit[d["ok"][fit, 0]]); Xv, _, Mv, Iv, Tv = m1.samples(d, X, va[d["ok"][va, 0]])
    c3a, c3v = Ta == 0, Tv == 0; Xa, Ma, Ia, Xv, Mv, Iv = Xa[c3a], Ma[c3a], Ia[c3a], Xv[c3v], Mv[c3v], Iv[c3v]
    mu = np.nanmean(V[Ia], 0); fill = lambda A: np.where(np.isfinite(A), A, mu)
    f, _ = m1.train(Xa, fill(V[Ia]).astype(np.float32), Ma, Xv, fill(V[Iv]).astype(np.float32), Mv, 0, a.epochs, lambda s: None)
    P = f(np.hstack([XC, np.repeat(mv, len(XC), 0)]).astype(np.float32))[0][:, means]
    spread = np.nanstd(np.array([V[i][means] for i in keep]), 0) + 1e-9; n = len(B)
    P0 = P[:n]; Pj = [P[n * (1 + j): n * (2 + j)] for j in range(K)]
    model = np.array([np.mean(np.abs(Pj[j] - P0) / spread) for j in range(K)])
    ok = np.isfinite(meas); rho = float(np.corrcoef(rank(meas[ok]), rank(model[ok]))[0, 1])
    print("%s: model activity vs measured activity over %d knobs: rank correlation %.2f" % (a.node, ok.sum(), rho))
    print("| knob | measured | model |\n|---|---|---|")
    for j in np.argsort(-np.nan_to_num(meas))[:12]:
        print("| %s | %.3f | %.3f |" % (keys[j], meas[j], model[j]))
    base = n * (1 + K); res = []
    for t, (p, q) in enumerate(pairs):
        Pab = P[base + t * n: base + (t + 1) * n]
        res.append((np.mean(np.abs(Pab - Pj[p] - Pj[q] + P0) / spread), keys[p], keys[q]))
    res.sort(reverse=True)
    print("\n| pair | interaction (beyond the sum of each) |\n|---|---|")
    for v, p, q in res[:8]:
        print("| %s + %s | %.3f |" % (p, q, v))
    print("median pair interaction %.3f vs median single-knob activity %.3f" % (np.median([r[0] for r in res]), np.median(model)))


if __name__ == "__main__":
    main()
