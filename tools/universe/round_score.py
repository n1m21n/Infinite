#!/usr/bin/env python3
"""Step 4 prediction test for one round: stored predictions (propose.py queue) vs the measured characteristics of the rendered points.

    round_score.py --node Analog --round r01        (ml-venv python)

Per design (uniform = honest check, propose = chosen where the seeds disagree): R2 of the mean-columns and correlation over all
columns; for one / pair moves also the sign of the change from the base is not available (bases are not stored), so size and level
only. Prints a table for HANDOFF.md."""
import argparse, json, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import own_sweeps as o, p3_characteristics as p3, sound_data as sd  # noqa: E402


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--node", required=True); ap.add_argument("--round", required=True); a = ap.parse_args()
    q = json.load(open(os.path.join(o.CACHE_DIR, "queue_%s_%s.json" % (a.node.lower(), a.round))))
    table, sw, fixed = o.param_table(a.node); d = sd.load(o.out_path(a.node)); idx = {k: i for i, k in enumerate(d["preset_id"])}
    rows = {}
    for it in q["items"]:
        rid = o.rid(a.node, o.recipe(a.node, sw, fixed, it["u"]))
        if rid in idx and d["ok"][idx[rid], 0] and "pred" in it:
            rows.setdefault((it["design"], it["kind"]), []).append((it["pred"], p3.vec(d["mel"][idx[rid], 0].astype(float) / 2 - 120)))
    means = [j for j, n in enumerate(p3.COLS) if n.endswith(".mean")]
    print("| design | kind | n | R2 of means (median) | r over all columns |\n|---|---|---|---|---|")
    for (des, kind), v in sorted(rows.items()):
        P, Y = np.array([x[0] for x in v]), np.array([x[1] for x in v])
        r2 = p3.r2cols(Y, P); ok = np.isfinite(Y) & np.isfinite(P)
        print("| %s | %s | %d | %.3f | %.3f |" % (des, kind, len(v), np.nanmedian(r2[means]), np.corrcoef((Y[ok] - np.nanmean(Y, 0)[np.nonzero(ok)[1]]) / (np.nanstd(Y, 0)[np.nonzero(ok)[1]] + 1e-9),
                                                                      (P[ok] - np.nanmean(Y, 0)[np.nonzero(ok)[1]]) / (np.nanstd(Y, 0)[np.nonzero(ok)[1]] + 1e-9))[0, 1]))


if __name__ == "__main__":
    main()
