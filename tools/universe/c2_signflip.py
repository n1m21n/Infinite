#!/usr/bin/env python3
"""Which axes are composites? Among bank renders that differ in exactly one knob, how consistently does that knob push each axis the same way?   (ml-venv python)

    c2_signflip.py --node Analog [--rows 6000]

A knob that moves 'fullness' up in one context and down in another makes the axis hard to predict by sign. Per axis: over knobs with >= 8 one-knob pairs whose
axis change is >= 0.5 sd of that axis' changes, mean of max(share up, share down) (1.0 = always the same way, 0.5 = coin flip), and how many knobs are below 0.8.
Report only: no claim, no frozen set touched. Rows are outside the held-out banks."""
import argparse, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import own_sweeps as o, p3_characteristics as p3, sound_data as sd  # noqa: E402
from c2_delta import pairs  # noqa: E402


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--node", required=True); ap.add_argument("--rows", type=int, default=6000); a = ap.parse_args()
    d = sd.load(o.out_path(a.node)); table, sw, fixed = o.param_table(a.node); keys = [p["key"] for p in sw]
    held = sd.split(d)["group"]; keep = np.flatnonzero(d["ok"][:, 0] & ~held)
    keep = np.random.default_rng(0).permutation(keep)[:a.rows]; K = d["knobs"][keep]
    means = [j for j, n in enumerate(p3.COLS) if n.endswith(".mean")]; names = [p3.COLS[j].split(".")[0] for j in means]
    V = np.array([p3.vec(d["mel"][i, 0].astype(np.float64) / 2 - 120)[means] for i in keep])
    P = pairs(K, maxd=1); print("%d one-knob pairs among %d rows" % (len(P), len(keep)))
    diff = np.abs(K[P[:, 0]] - K[P[:, 1]]) > 0.005; kn = diff.argmax(1); dk = np.sign(K[P[:, 0], kn] - K[P[:, 1], kn]); dV = V[P[:, 0]] - V[P[:, 1]]
    print("%-18s %6s %6s %6s" % ("axis", "knobs", "<0.8", "consistency"))
    for ax, n in enumerate(names):
        ch = dV[:, ax]; ok = np.isfinite(ch); sdv = np.nanstd(ch) + 1e-12; cons = []
        for j in range(len(keys)):
            m = ok & (kn == j) & (np.abs(ch) >= 0.5 * sdv)
            if m.sum() >= 8:
                s = dk[m] * np.sign(ch[m]); cons.append(max((s > 0).mean(), (s < 0).mean()))
        print("%-18s %6d %6d %.2f" % (n, len(cons), sum(c < 0.8 for c in cons), float(np.mean(cons)) if cons else float("nan")))


if __name__ == "__main__":
    main()
