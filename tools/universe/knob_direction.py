#!/usr/bin/env python3
"""Goal item 3: knob-direction accuracy on held-out banks, for every synth engine with one-knob pairs.
    knob_direction.py [--engines Analog,Oscillator,...] [--folds 5]
Model = measured slope per (knob, axis) (feel_measure.synth's estimator) fitted on the training banks only. A test pair = two presets of one bank that
differ in exactly one knob. Prediction: sign of slope*dk on each axis. Scored two ways, folds split by bank (no variant of a training base leaks):
  dominant  the axis with the largest true |change| (in spreads); right if the sign matches        (the C2 'direction right' reading)
  gated     same, only for moves whose largest |change| >= 0.5 spread (above the noise floor)
Reports mean +- sd over folds. The bar is 90%; a miss is a miss. Writes feel/knob_direction.json."""
import argparse, collections, json, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import own_sweeps as o, p3_characteristics as p3, sound_data as sd
OUT = os.path.join(HERE, "../../docs/plans/data-universe/feel/knob_direction.json")
ALL = ["Analog", "Oscillator", "FM", "Metallic", "String", "Wavetable", "Equation Synth", "Partials", "Sampler", "Slicer", "Grain Molder", "Field Synth"]

def pairs(node):
    table, sw, fixed = o.param_table(node); d = sd.load(o.out_path(node)); nk = len(sw)
    keep = np.flatnonzero(d["ok"][:, 0]); V = {i: p3.vec(d["mel"][i, 0].astype(float) / 2 - 120) for i in keep}
    means = [j for j, n in enumerate(p3.COLS) if n.endswith(".mean")]
    sd_ = np.nanstd(np.array([V[i][means] for i in keep]), 0) + 1e-9
    by = {}
    for i in keep: by.setdefault(d["group"][i], []).append(i)
    P = []  # (bank, knob j, dk, delta vector in spreads)
    for g, ix in by.items():
        K = d["knobs"][ix]
        for x in range(len(ix)):
            diff = np.abs(K - K[x]) > 0.005
            for y in np.flatnonzero(diff.sum(1) == 1):
                if y > x:
                    j = int(np.flatnonzero(diff[y])[0]); dk = float(K[y, j] - K[x, j])
                    P.append((g, j, dk, np.nan_to_num((V[ix[y]][means] - V[ix[x]][means]) / sd_)))
    return P, nk

def reliable(P, nk, banks, fold_of, folds, tau=0.8, eff=0.3):
    """Selective prediction: a (knob, axis) is 'reliable' if >= tau of its TRAINING pairs moved the axis the same way (|change| >= eff spreads).
    Held-out accuracy and coverage are measured over reliable (knob, axis) cells only, so compose can restrict itself to knobs the model can be trusted on."""
    hit, tot, cells, allcells = 0, 0, 0, 0
    A = len(P[0][3])
    for f in range(folds):
        tr = [p for p in P if fold_of[p[0]] != f]; te = [p for p in P if fold_of[p[0]] == f]
        up = np.zeros((nk, A)); n = np.zeros((nk, A))
        for g, j, dk, mv in tr:
            m = np.abs(mv) >= eff; up[j] += np.where(m, np.sign(mv) * np.sign(dk) > 0, 0); n[j] += m
        cons = np.where(n >= 8, np.maximum(up, n - up) / np.maximum(n, 1), 0); maj = np.where(up * 2 >= n, 1, -1)
        allcells += int((n >= 8).sum()); cells += int((cons >= tau).sum())
        for g, j, dk, mv in te:
            for a in np.flatnonzero((cons[j] >= tau) & (np.abs(mv) >= eff)):
                tot += 1; hit += int(np.sign(dk) * maj[j, a] == np.sign(mv[a]))
    return {"tau": tau, "acc": round(hit / tot, 3) if tot else None, "scored_moves": tot, "reliable_cells_per_fold": round(cells / folds, 1), "cells_with_data": round(allcells / folds, 1)}

def run(node, folds):
    P, nk = pairs(node)
    if len(P) < 50: return {"pairs": len(P), "error": "too few one-knob pairs"}
    banks = sorted({p[0] for p in P}); rng = np.random.RandomState(0); rng.shuffle(banks); fold_of = {b: i % folds for i, b in enumerate(banks)}
    accs, gacc, gn = [], [], []
    for f in range(folds):
        tr = [p for p in P if fold_of[p[0]] != f]; te = [p for p in P if fold_of[p[0]] == f]
        if not te: continue
        num = np.zeros((nk, len(P[0][3]))); den = np.zeros(nk)
        for g, j, dk, mv in tr: num[j] += mv * dk; den[j] += dk * dk
        S = num / np.maximum(den, 1e-9)[:, None]
        ok, gok = [], []
        for g, j, dk, mv in te:
            a = int(np.argmax(np.abs(mv)))
            if mv[a] == 0 or S[j, a] == 0: continue
            hit = np.sign(S[j, a] * dk) == np.sign(mv[a]); ok.append(hit)
            if abs(mv[a]) >= 0.5: gok.append(hit)
        if ok: accs.append(np.mean(ok))
        if gok: gacc.append(np.mean(gok)); gn.append(len(gok))
    rel = reliable(P, nk, banks, fold_of, folds)
    return {"reliable": rel, "pairs": len(P), "banks": len(banks), "dominant_acc": round(float(np.mean(accs)), 3), "dominant_sd": round(float(np.std(accs)), 3),
            "gated_acc": round(float(np.mean(gacc)), 3) if gacc else None, "gated_sd": round(float(np.std(gacc)), 3) if gacc else None, "gated_moves_per_fold": int(np.mean(gn)) if gn else 0}

if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("--engines", default=",".join(ALL)); ap.add_argument("--folds", type=int, default=5); a = ap.parse_args()
    res = {}
    for e in a.engines.split(","):
        try: res[e] = run(e, a.folds)
        except Exception as ex: res[e] = {"error": repr(ex)[:160]}
        print(e, res[e], flush=True)
    json.dump(res, open(OUT, "w"), indent=1)
