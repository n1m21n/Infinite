#!/usr/bin/env python3
"""Block 2 (NEXT-SESSION): fit valence / energy weights from music cues on EMOPIA (1,078 clips, quadrant labels Q1-Q4), held out by song.
    fit_ve.py [--splits 10] [--out feel/ve_weights.json]
Features = axes 23-31 (midi_axes.row) + register, density, articulation, interval roughness from the notes. Targets: V = Q1|Q4, E = Q1|Q2 (EMOPIA's own quadrants).
Reports held-out point-biserial r and accuracy for V and E (mean +- sd over group splits), and the weights of the final all-data fit.
The bar (NEXT-SESSION block 2): held-out r >= 0.8 on both. A miss is reported as a miss."""
import argparse, glob, json, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import midi_axes as ma, midi_full as mf
ROOT = os.path.join(mf.ROOT, "emopia_piano_emotions")
DISS = np.array([0, 1, 0.6, 0.2, 0.15, 0.1, 0.9, 0.0, 0.1, 0.15, 0.4, 0.7])  # roughness weight per interval class (rough Plomp-Levelt / Huron ordering)


def feats(path):
    src = os.path.relpath(path, mf.ROOT); r = ma.row(src)
    if "error" in r or "a23" not in r: return None
    fmt, div, ev, meta = mf.parse(open(path, "rb").read()); nt, end = ma.notes_of(ev); fs = ma.fast_sec(meta, div)
    t0 = fs(np.array([n[0] for n in nt], float)); t1 = fs(np.array([n[1] for n in nt], float)); p = np.array([n[2] for n in nt], float); v = np.array([n[3] for n in nt], float)
    secs = max(float(t1.max()), 1e-3); ioi = np.diff(np.sort(np.unique(np.round(t0, 3))))
    rough = []
    for i in range(len(nt)):
        sel = np.where(np.abs(t0 - t0[i]) < 0.03)[0]
        if len(sel) > 1: rough.append(np.mean([DISS[int(abs(p[i] - p[j])) % 12] for j in sel if j != i]))
    f = {"major_ness": r["a23"]["major_ness"], "key_r": r["a23"]["key_r"], "bpm": r["a24"]["bpm_onset"] or r["a24"]["bpm"],
         "notes_per_s": len(nt) / secs, "vel_mean": v.mean(), "vel_std": v.std(), "vel_crest": r["a29"]["vel_crest"], "loud_range": r["a29"]["loudness_range"] or 0,
         "pitch_mean": p.mean(), "pitch_std": p.std(), "pitch_lo": np.percentile(p, 5), "pitch_hi": np.percentile(p, 95),
         "legato": float(np.median((t1 - t0) / np.maximum(np.append(ioi, ioi[-1] if len(ioi) else 1), 0.05))[0]) if False else float(np.median(t1 - t0)),
         "ioi_med": float(np.median(ioi)) if len(ioi) else 1.0, "ioi_cv": float(ioi.std() / max(ioi.mean(), 1e-3)) if len(ioi) else 0.0,
         "silence": r["a27"]["silence_ratio"], "longest_rest": r["a27"]["longest_rest_s"], "poly_rough": float(np.mean(rough)) if rough else 0.0,
         "step_mean": float(np.mean(np.abs(np.diff(p)))), "secs": secs}
    pcw = np.bincount((p.astype(int) - r["a23"]["tonic"]) % 12, weights=np.maximum(t1 - t0, 0.02) * v, minlength=12); pcw = pcw / max(pcw.sum(), 1e-9)
    for d in range(12): f["deg%d" % d] = float(pcw[d])
    f["third_balance"] = float(pcw[4] - pcw[3]); f["fifth"] = float(pcw[7]); f["leading"] = float(pcw[11]); f["chromatic"] = float(1 - pcw[[0, 2, 4, 5, 7, 9, 11]].sum() if r["a23"]["mode"] == "major" else 1 - pcw[[0, 2, 3, 5, 7, 8, 10]].sum())
    for k, x in r["a28"].items(): f["band_" + k] = x
    if r.get("a31"): f.update(build_slope=r["a31"]["build_slope"], drop_height=r["a31"]["drop_height"], peak_pos=r["a31"]["peak_pos"])
    else: f.update(build_slope=0.0, drop_height=0.0, peak_pos=0.5)
    return f


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--splits", type=int, default=10); ap.add_argument("--out", default=os.path.join(HERE, "..", "..", "docs/plans/data-universe/feel/ve_weights.json")); a = ap.parse_args()
    X, V, E, G, names = [], [], [], [], None
    for p in sorted(glob.glob(os.path.join(ROOT, "**", "Q[1-4]_*.mid"), recursive=True)):
        q = os.path.basename(p)[:2]; f = feats(p)
        if f is None: continue
        names = names or sorted(f); X.append([f[k] for k in names]); V.append(1.0 if q in ("Q1", "Q4") else -1.0); E.append(1.0 if q in ("Q1", "Q2") else -1.0); G.append(os.path.basename(p).split("_")[1])
    X = np.array(X, float); V = np.array(V); E = np.array(E); G = np.array(G); print("%d clips, %d songs, %d features" % (len(X), len(set(G)), len(names)))
    from sklearn.linear_model import RidgeCV, LogisticRegression
    from sklearn.ensemble import GradientBoostingRegressor
    from sklearn.preprocessing import StandardScaler
    from sklearn.pipeline import make_pipeline
    from sklearn.model_selection import GroupShuffleSplit
    models = {"ridge": lambda: make_pipeline(StandardScaler(), RidgeCV(alphas=np.logspace(-1, 3, 15))), "gbr": lambda: GradientBoostingRegressor(n_estimators=150, max_depth=2, subsample=0.8, random_state=0)}
    res = {}
    for tgt, y in (("V", V), ("E", E)):
        for mn, mk in models.items():
            rs, acc = [], []
            for tr, te in GroupShuffleSplit(a.splits, test_size=0.2, random_state=1).split(X, y, G):
                m = mk().fit(X[tr], y[tr]); s = m.predict(X[te]); rs.append(np.corrcoef(s, y[te])[0, 1]); acc.append(float(np.mean(np.sign(s) == y[te])))
            res["%s_%s" % (tgt, mn)] = {"r_mean": round(float(np.mean(rs)), 3), "r_sd": round(float(np.std(rs)), 3), "acc_mean": round(float(np.mean(acc)), 3)}
            print(tgt, mn, res["%s_%s" % (tgt, mn)])
    w = {}
    for tgt, y in (("V", V), ("E", E)):
        m = models["ridge"]().fit(X, y); sc = m[0]; coef = m[1].coef_ / sc.scale_
        w[tgt] = {n: round(float(c), 5) for n, c in sorted(zip(names, coef), key=lambda t: -abs(t[1] * 1))[:12]}; w[tgt]["_alpha"] = float(m[1].alpha_)
        top = sorted(zip(names, m[1].coef_), key=lambda t: -abs(t[1]))[:6]; print(tgt, "top standardised weights:", [(n, round(float(c), 3)) for n, c in top])
    json.dump({"n_clips": len(X), "heldout": res, "ridge_weights_raw_units": w, "bar": "held-out r >= 0.8 on V and E"}, open(a.out, "w"), indent=1)


if __name__ == "__main__":
    main()
