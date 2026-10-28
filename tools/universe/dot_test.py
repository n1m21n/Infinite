#!/usr/bin/env python3
"""Held-out test of a 'dot' (one sound axis) on REAL voices: the Analog voices of the live style cards (never in the sweeps).
For each voice and each axis with a trusted knob (cons >= 0.8, not noise/drive/fm/sub): move that knob so the model predicts +/-1 spread on the
axis, render base / up / down, measure with p3.vec, and score
  sign   the axis moved the predicted way
  gain   achieved change / requested change (median)
  drift  |change| on noisiness and loudness, in spreads (a dot must not buy its axis with noise or volume)
Pass: sign >= 85% over >= 60 trials, median gain >= 0.5, noisiness drift < 0.3 in >= 90% of trials. Fail: sign < 75% or noisiness drift >= 0.3 in > 20%.
    dot_test.py [--voices 12] [--glob 'DIR/*_1.inf']"""
import argparse, glob, json, os, re, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import knob_direction as kd, own_sweeps as ow, p3_characteristics as p3, sound_data as sd
NODE = "Analog"; BAN = ("noise", "drive", "fm", "sub"); AX = [n[:-5] for n in p3.COLS if n.endswith(".mean")]
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../docs/plans/data-universe/feel/dot_test.json")

def voices(pattern):
    out = []
    for f in sorted(glob.glob(pattern)):
        txt = open(f).read()
        for m in re.finditer(r"node \d+ Synths Analog\n(.*?)\nend", txt, re.S):
            rec = {}
            for ln in m.group(1).splitlines():
                p = ln.split()
                if len(p) == 3 and p[0] in "fib": rec[p[1]] = float(p[2])
            if rec: out.append((os.path.basename(f), rec))
    return out

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--voices", type=int, default=12)
    ap.add_argument("--glob", default=os.path.expanduser("~/Infinite/render-runs/styled/*_1.inf")); a = ap.parse_args()
    table, sw, fixed = ow.param_table(NODE); tags = ow.tag_of(table); keys = [p["key"] for p in sw]
    P, nk = kd.pairs(NODE); means = [j for j, n in enumerate(p3.COLS) if n.endswith(".mean")]
    d = sd.load(ow.out_path(NODE)); keep = np.flatnonzero(d["ok"][:, 0])
    sd_ = np.nanstd(np.array([p3.vec(d["mel"][i, 0].astype(float) / 2 - 120)[means] for i in keep]), 0) + 1e-9
    A = len(AX); num = np.zeros((nk, A)); den = np.zeros(nk); up = np.zeros((nk, A)); n = np.zeros((nk, A))
    for g, j, dk, mv in P:
        num[j] += mv * dk; den[j] += dk * dk; m = np.abs(mv) >= 0.3; n[j] += m; up[j] += m * (np.sign(mv) * np.sign(dk) > 0)
    slope = num / np.maximum(den, 1e-9)[:, None]; cons = np.where(n >= 8, np.maximum(up, n - up) / np.maximum(n, 1), 0)
    vs = voices(a.glob)[: a.voices]; print(len(vs), "style-card Analog voices")
    trials = []; recs = []
    for name, v in vs:
        base = dict(fixed); 
        for p in sw: base[p["key"]] = v.get(p["key"], float(p["default"]))
        u0 = np.array([min(max(ow.to_u(p, base[p["key"]]), 0.02), 0.98) for p in sw])
        recs.append(ow.recipe(NODE, sw, fixed, list(u0))); bi = len(recs) - 1
        for ax in range(A):
            if AX[ax] in ("attack", "decay", "density"): continue
            c = [j for j in np.argsort(-np.abs(slope[:, ax])) if cons[j, ax] >= 0.8 and keys[j] not in BAN and abs(slope[j, ax]) > 0.5]
            if not c: continue
            j = c[0]
            for sgn in (1, -1):
                u = u0.copy(); u[j] = np.clip(u[j] + sgn / slope[j, ax], 0.02, 0.98); req = (u[j] - u0[j]) * slope[j, ax]
                if abs(req) < 0.3: continue
                recs.append(ow.recipe(NODE, sw, fixed, list(u))); trials.append((name, AX[ax], keys[j], req, bi, len(recs) - 1))
    print(len(recs), "renders for", len(trials), "trials"); rows = ow.rerender(NODE, recs)
    def V(i):
        r = next((x for x in (rows[i] or []) if x.get("mel")), None)
        return None if r is None else p3.vec(ow.mel_of(r).reshape(64, 48) / 2 - 120)[means]
    res = []
    for name, ax, knob, req, bi, ti in trials:
        b, t = V(bi), V(ti)
        if b is None or t is None: continue
        dv = np.nan_to_num((t - b) / sd_); k = AX.index(ax)
        res.append({"voice": name, "axis": ax, "knob": knob, "req": float(req), "got": float(dv[k]), "noise_drift": float(abs(dv[AX.index("noisiness")])), "loud_drift": float(abs(dv[AX.index("loudness")]))})
    nt = len(res); sign = np.mean([np.sign(r["got"]) == np.sign(r["req"]) for r in res]); gain = np.median([r["got"] / r["req"] for r in res])
    nd = np.mean([r["noise_drift"] < 0.3 for r in res if r["axis"] != "noisiness"]); ld = np.mean([r["loud_drift"] < 0.3 for r in res if r["axis"] != "loudness"])
    byax = {x: (round(float(np.mean([np.sign(r["got"]) == np.sign(r["req"]) for r in res if r["axis"] == x])), 2), sum(r["axis"] == x for r in res)) for x in AX if any(r["axis"] == x for r in res)}
    verdict = "PASS" if nt >= 60 and sign >= .85 and gain >= .5 and nd >= .9 else "FAIL" if sign < .75 or nd < .8 else "INCONCLUSIVE"
    out = {"trials": nt, "sign": round(float(sign), 3), "median_gain": round(float(gain), 2), "noise_drift_ok": round(float(nd), 3), "loud_drift_ok": round(float(ld), 3), "by_axis": byax, "verdict": verdict}
    json.dump({"summary": out, "rows": res}, open(OUT, "w"), indent=1); print(json.dumps(out, indent=1))

main()
