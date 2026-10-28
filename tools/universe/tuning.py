#!/usr/bin/env python3
"""Tuning curves: what does each knob do, per the forward model?   (ml-venv python)

    tuning.py --node Analog [--top 8] [--bases 150]

For the busiest knobs (activity.py) sweep the knob 0 -> 1 in 11 steps on `bases` rendered points (others unchanged), predict the 11 mean
axes, and report for each knob its strongest axis, the direction (up / down / peaks in the middle), and the size of the change over the
sweep in spreads. Selector knobs (filterType, waveform: few levels) are read as steps. Also prints the same from the measured one-knob
moves where a knob has enough of them, as a check on the direction (sign of the axis change with the knob's change)."""
import argparse, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import own_sweeps as o, p3_characteristics as p3, m1_forward as m1, sound_eval as se, activity as act  # noqa: E402


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--node", required=True); ap.add_argument("--top", type=int, default=8)
    ap.add_argument("--bases", type=int, default=150); ap.add_argument("--epochs", type=int, default=300); a = ap.parse_args()
    keys, d, V, means, names, eff = act.measure(a.node); keep = np.flatnonzero(d["ok"][:, 0]); K = len(keys); rng = np.random.default_rng(1)
    Va = np.full((len(d["preset_id"]), len(p3.COLS)), np.nan)
    for i in keep: Va[i] = V[i]
    meas = np.array([np.nanmean(np.array(eff[j])) if len(eff[j]) >= 5 else -1 for j in range(K)]); order = np.argsort(-meas)[:a.top]
    B = d["knobs"][rng.choice(keep, a.bases)]; grid = np.linspace(0, 1, 11); blocks = [B]
    for j in order:
        for g in grid:
            M = B.copy(); M[:, j] = g; blocks.append(M)
    allc = np.vstack(blocks); XX = se.encode(np.vstack([d["knobs"], allc])); X, XC = XX[:len(d["knobs"])], XX[len(d["knobs"]):]
    mv = np.array([[48, 100]], float) / 127
    perm = np.random.default_rng(0).permutation(keep); nva = max(1, int(.15 * len(perm))); va, fit = perm[:nva], perm[nva:]
    Xa, _, Ma, Ia, Ta = m1.samples(d, X, fit[d["ok"][fit, 0]]); Xv, _, Mv, Iv, Tv = m1.samples(d, X, va[d["ok"][va, 0]])
    c3a, c3v = Ta == 0, Tv == 0; Xa, Ma, Ia, Xv, Mv, Iv = Xa[c3a], Ma[c3a], Ia[c3a], Xv[c3v], Mv[c3v], Iv[c3v]
    mu = np.nanmean(Va[Ia], 0); fill = lambda A: np.where(np.isfinite(A), A, mu)
    f, _ = m1.train(Xa, fill(Va[Ia]).astype(np.float32), Ma, Xv, fill(Va[Iv]).astype(np.float32), Mv, 0, a.epochs, lambda s: None)
    P = f(np.hstack([XC, np.repeat(mv, len(XC), 0)]).astype(np.float32))[0][:, means]; n = len(B)
    spread = np.nanstd(Va[keep][:, means], 0) + 1e-9
    print("%s tuning curves (model; change over the 0->1 sweep in spreads, mean of %d bases)\n| knob | strongest axis | shape | change | 2nd axis |\n|---|---|---|---|---|" % (a.node, n))
    for r, j in enumerate(order):
        C = np.array([P[n * (1 + r * 11 + s): n * (2 + r * 11 + s)].mean(0) for s in range(11)]) / spread   # 11 x axes
        rng_ = C.max(0) - C.min(0); ax = np.argsort(-rng_)[:2]; c = C[:, ax[0]]
        shape = "up" if c[-1] - c[0] > .6 * (c.max() - c.min()) else "down" if c[0] - c[-1] > .6 * (c.max() - c.min()) else "peak" if c.argmax() not in (0, 10) else "dip" if c.argmin() not in (0, 10) else "mixed"
        print("| %s | %s | %s | %.2f | %s |" % (keys[j], names[ax[0]], shape, rng_[ax[0]], names[ax[1]]))


if __name__ == "__main__":
    main()
