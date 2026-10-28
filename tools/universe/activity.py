#!/usr/bin/env python3
"""Activity map: which knobs / blocks of a node move which characteristic axes, from stored one-knob moves (no render).

    activity.py --node Analog [--top 15]        (ml-venv python)

For every pair of renders in the same bank whose knob vectors differ in exactly one knob, the move is the change of the 11 mean
characteristics (each scaled by its spread over the engine). A knob's activity = mean |move| over its pairs, per axis and overall;
blocks = the knob key prefix up to the first '_' or capital. Also prints how concentrated the activity is (share of the top 3 knobs)
and how many knobs do almost nothing (dead)."""
import argparse, os, re, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import own_sweeps as o, p3_characteristics as p3, sound_data as sd  # noqa: E402


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--node", required=True); ap.add_argument("--top", type=int, default=15); a = ap.parse_args()
    table, sw, fixed = o.param_table(a.node); d = sd.load(o.out_path(a.node)); keys = [p["key"] for p in sw]
    keep = np.flatnonzero(d["ok"][:, 0]); V = {i: p3.vec(d["mel"][i, 0].astype(float) / 2 - 120) for i in keep}
    means = [j for j, n in enumerate(p3.COLS) if n.endswith(".mean")]; names = [p3.COLS[j].split(".")[0] for j in means]
    sd_ = np.nanstd(np.array([V[i][means] for i in keep]), 0) + 1e-9
    bygrp = {}
    for i in keep:
        bygrp.setdefault(d["group"][i], []).append(i)
    eff = {j: [] for j in range(len(keys))}
    for g, ix in bygrp.items():
        K = d["knobs"][ix]
        for x in range(len(ix)):
            diff = np.abs(K - K[x]) > 0.005; one = np.flatnonzero(diff.sum(1) == 1)
            for y in one:
                if y > x:
                    eff[int(np.flatnonzero(diff[y])[0])].append(np.abs(V[ix[y]][means] - V[ix[x]][means]) / sd_)
    rows = []
    for j, e in eff.items():
        if len(e) >= 5:
            m = np.nanmean(np.array(e), 0); rows.append((keys[j], len(e), float(np.nanmean(m)), m))
    rows.sort(key=lambda r: -r[2]); tot = sum(r[2] for r in rows)
    print("%s: %d knobs measured of %d, %d one-knob pairs\n| knob | pairs | activity | top axes |\n|---|---|---|---|" % (a.node, len(rows), len(keys), sum(r[1] for r in rows)))
    for k, n, act, m in rows[:a.top]:
        print("| %s | %d | %.3f | %s |" % (k, n, act, ", ".join(names[i] for i in np.argsort(-np.nan_to_num(m))[:3])))
    print("\ntop 3 knobs hold %.0f%% of all activity; %d knobs below a tenth of the median (dead)" % (100 * sum(r[2] for r in rows[:3]) / tot, sum(r[2] < .1 * np.median([x[2] for x in rows]) for r in rows)))
    blocks = {}
    for k, n, act, m in rows:
        b = re.split(r"[_A-Z0-9]", k)[0] or k; blocks.setdefault(b, []).append(act)
    print("\n| block | knobs | mean activity | share |\n|---|---|---|---|")
    for b, v in sorted(blocks.items(), key=lambda t: -sum(t[1])):
        print("| %s | %d | %.3f | %.0f%% |" % (b, len(v), np.mean(v), 100 * sum(v) / tot))


if __name__ == "__main__":
    main()
