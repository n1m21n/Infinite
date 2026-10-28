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


def by_knobs(a, q, sw, fixed, d, idx):
    """R2 against how many knobs differ from the nearest render that existed before the round (all items, propose + uniform)."""
    new = {o.rid(a.node, o.recipe(a.node, sw, fixed, it["u"])) for it in q["items"]}
    old = np.array([i for i, p in enumerate(d["preset_id"]) if p not in new and d["ok"][i, 0]]); K = d["knobs"][old]
    means = [j for j, n in enumerate(p3.COLS) if n.endswith(".mean")]; bins = {}
    for it in q["items"]:
        r = o.rid(a.node, o.recipe(a.node, sw, fixed, it["u"]))
        if r not in idx or not d["ok"][idx[r], 0]: continue
        u = np.array(it["u"]); nd = int(((np.abs(K - u) > 0.005).sum(1)).min())
        b = "1" if nd <= 1 else "2" if nd == 2 else "3" if nd == 3 else "4-6" if nd <= 6 else "7-12" if nd <= 12 else "13+"
        bins.setdefault(b, []).append((it["pred"], p3.vec(d["mel"][idx[r], 0].astype(float) / 2 - 120)))
    print("| knobs changed from nearest earlier render | n | R2 of means (median) |\n|---|---|---|")
    for b in ("1", "2", "3", "4-6", "7-12", "13+"):
        v = bins.get(b)
        if v and len(v) >= 20:
            P, Y = np.array([x[0] for x in v]), np.array([x[1] for x in v]); print("| %s | %d | %.3f |" % (b, len(v), np.nanmedian(p3.r2cols(Y, P)[means])))


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--node", required=True); ap.add_argument("--round", required=True); ap.add_argument("--by-knobs", action="store_true"); a = ap.parse_args()
    q = json.load(open(os.path.join(o.CACHE_DIR, "queue_%s_%s.json" % (a.node.lower(), a.round))))
    table, sw, fixed = o.param_table(a.node); d = sd.load(o.out_path(a.node)); idx = {k: i for i, k in enumerate(d["preset_id"])}
    rows = {}
    for it in q["items"]:
        rid = o.rid(a.node, o.recipe(a.node, sw, fixed, it["u"]))
        if rid in idx and d["ok"][idx[rid], 0] and "pred" in it:
            rows.setdefault((it["design"], it["kind"]), []).append((it["pred"], p3.vec(d["mel"][idx[rid], 0].astype(float) / 2 - 120)))
    if a.by_knobs:
        by_knobs(a, q, sw, fixed, d, idx); return
    means = [j for j, n in enumerate(p3.COLS) if n.endswith(".mean")]
    print("| design | kind | n | R2 of means (median) | r over all columns |\n|---|---|---|---|---|")
    for (des, kind), v in sorted(rows.items()):
        P, Y = np.array([x[0] for x in v]), np.array([x[1] for x in v])
        r2 = p3.r2cols(Y, P); ok = np.isfinite(Y) & np.isfinite(P)
        print("| %s | %s | %d | %.3f | %.3f |" % (des, kind, len(v), np.nanmedian(r2[means]), np.corrcoef((Y[ok] - np.nanmean(Y, 0)[np.nonzero(ok)[1]]) / (np.nanstd(Y, 0)[np.nonzero(ok)[1]] + 1e-9),
                                                                      (P[ok] - np.nanmean(Y, 0)[np.nonzero(ok)[1]]) / (np.nanstd(Y, 0)[np.nonzero(ok)[1]] + 1e-9))[0, 1]))


if __name__ == "__main__":
    main()
