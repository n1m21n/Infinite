#!/usr/bin/env python3
"""Block 2 follow-up: try to lift valence. Adds harmony features per bar (chord quality from pitch-class sets, minor/major bar share,
dissonant-bar share, mode x tempo interaction) to fit_ve's features, caches the matrix, compares models on 10 song-grouped splits.
Reports r and accuracy as mean +- sd. A miss stays a miss."""
import glob, json, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import fit_ve as fv, midi_axes as ma, midi_full as mf
CACHE = os.path.expanduser("~/Infinite/training-data/mix/ve_feats.npz")
TRIADS = {(0, 4, 7): 1, (0, 3, 7): -1, (0, 3, 6): -1.5, (0, 4, 8): 0.3, (0, 2, 7): 0.2, (0, 5, 7): 0.2}

def extra(path, f):
    fmt, div, ev, meta = mf.parse(open(path, "rb").read()); nt, end = ma.notes_of(ev); fs = ma.fast_sec(meta, div)
    t0 = fs(np.array([n[0] for n in nt], float)); t1 = fs(np.array([n[1] for n in nt], float)); p = np.array([n[2] for n in nt], int)
    bpm = f["bpm"] or 100; beat = 60.0 / max(bpm, 40); win = beat * 2
    q, d = [], []
    for w0 in np.arange(0, t1.max(), win):
        sel = (t0 < w0 + win) & (t1 > w0)
        if sel.sum() < 3: continue
        pcs = np.bincount(p[sel] % 12, weights=np.maximum(np.minimum(t1[sel], w0 + win) - np.maximum(t0[sel], w0), 0.01), minlength=12)
        top = set(np.argsort(pcs)[-3:]); best = 0.0
        for root in top:
            s = tuple(sorted(((x - root) % 12) for x in top))
            best = TRIADS.get(s, best) if s in TRIADS else best
        q.append(best); d.append(float(pcs[[1, 6, 10]].sum() / max(pcs.sum(), 1e-9)))
    q = np.array(q) if q else np.zeros(1)
    mj = 1.0 if f["major_ness"] > 0 else -1.0
    f2 = dict(f); f2.update(maj_bar=float((q > 0.5).mean()), min_bar=float((q < -0.5).mean()), qual_mean=float(q.mean()), dis_bar=float(np.mean(d) if d else 0),
                            mode_x_tempo=mj * np.log(max(bpm, 40) / 100), mode_x_density=mj * np.log1p(f["notes_per_s"]), tempo_log=np.log(max(bpm, 40)),
                            vel_slope=float(np.polyfit(t0, [n[3] for n in nt], 1)[0]) if len(nt) > 5 else 0.0)
    return f2

def build():
    if os.path.exists(CACHE):
        z = np.load(CACHE, allow_pickle=True); return z["X"], z["V"], z["E"], z["G"], list(z["names"])
    X, V, E, G, names = [], [], [], [], None
    for p in sorted(glob.glob(os.path.join(fv.ROOT, "**", "Q[1-4]_*.mid"), recursive=True)):
        f = fv.feats(p)
        if f is None: continue
        f = extra(p, f); names = names or sorted(f); q = os.path.basename(p)[:2]
        X.append([f[k] for k in names]); V.append(1.0 if q in ("Q1", "Q4") else -1.0); E.append(1.0 if q in ("Q1", "Q2") else -1.0); G.append(os.path.basename(p).split("_")[1])
    X, V, E, G = np.array(X, float), np.array(V), np.array(E), np.array(G)
    np.savez(CACHE, X=X, V=V, E=E, G=G, names=np.array(names)); return X, V, E, G, names

def main():
    from sklearn.linear_model import RidgeCV
    from sklearn.ensemble import GradientBoostingRegressor, ExtraTreesRegressor, RandomForestRegressor
    from sklearn.preprocessing import StandardScaler
    from sklearn.pipeline import make_pipeline
    from sklearn.model_selection import GroupShuffleSplit
    X, V, E, G, names = build(); print(X.shape, "features", len(names))
    models = {"ridge": lambda: make_pipeline(StandardScaler(), RidgeCV(alphas=np.logspace(-1, 3, 15))),
              "gbr": lambda: GradientBoostingRegressor(n_estimators=250, max_depth=3, learning_rate=0.04, subsample=0.7, random_state=0),
              "extra": lambda: ExtraTreesRegressor(400, min_samples_leaf=3, max_features=0.4, random_state=0, n_jobs=2),
              "rf": lambda: RandomForestRegressor(400, min_samples_leaf=3, max_features=0.4, random_state=0, n_jobs=2)}
    out = {}
    for tgt, y in (("V", V), ("E", E)):
        for mn, mk in models.items():
            rs, acc, pr = [], [], []
            for tr, te in GroupShuffleSplit(10, test_size=0.2, random_state=1).split(X, y, G):
                s = mk().fit(X[tr], y[tr]).predict(X[te]); rs.append(np.corrcoef(s, y[te])[0, 1]); acc.append(np.mean(np.sign(s) == y[te]))
            out[tgt + "_" + mn] = {"r": round(float(np.mean(rs)), 3), "sd": round(float(np.std(rs)), 3), "acc": round(float(np.mean(acc)), 3)}; print(tgt, mn, out[tgt + "_" + mn], flush=True)
    json.dump(out, open(os.path.join(HERE, "..", "..", "docs/plans/data-universe/feel/ve_boost.json"), "w"), indent=1)

if __name__ == "__main__":
    main()
