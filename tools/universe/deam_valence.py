#!/usr/bin/env python3
"""Valence (and arousal) from audio on DEAM (STATUS-2026-10-29.md section 5): 1,802 songs with mean valence / arousal ratings (1-9).
    deam_valence.py measure [--limit N]     decode each mp3 (ffmpeg, 44.1 kHz stereo), measure -> ~/Infinite/training-data/mood/deam_features.jsonl
    deam_valence.py fit [--splits 10]       held-out r for valence and arousal (KFold by song) -> docs/plans/data-universe/feel/deam_valence.json
Run with the ml venv and -I (the audio is downloaded data): ~/Infinite/ml-venv/bin/python3 -I tools/universe/deam_valence.py measure
Features: frame_features.vector (timbre shape, envelope, onsets, brightness, notes) + harmony and tempo from librosa: Krumhansl major/minor
key correlation (mode), chord colour (share of major vs minor triad templates per beat), harmonic change rate, tempo, beat strength,
and brightness / loudness over time (start, middle, end thirds). Bar: held-out r >= 0.8 for valence. A miss is reported as a miss;
the literature ceiling for audio valence is about r 0.6-0.7. Licence: DEAM is CC BY-NC-SA 4.0; audio stays local, never committed."""
import argparse, glob, json, os, subprocess, sys
import numpy as np
import numba, librosa, librosa.core.audio  # before the path change: numba probes `coverage`, and tools/universe/coverage.py would answer
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.append(HERE)
import frame_features as ff

DEAM = os.path.expanduser("~/Infinite/training-data/downloads/deam")
FEATS = os.path.expanduser("~/Infinite/training-data/mood/deam_features.jsonl")
OUT = os.path.join(os.path.dirname(os.path.dirname(HERE)), "docs/plans/data-universe/feel/deam_valence.json")
SR = 44100
MAJ = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
MIN = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])
HARM = ["maj_r", "min_r", "mode", "key_clarity", "maj_triad", "min_triad", "dim_triad", "chord_change", "chroma_entropy",
        "tempo", "beat_strength", "pulse_clarity", "bright_0", "bright_1", "bright_2", "loud_0", "loud_1", "loud_2", "loud_slope", "width"]


def labels():
    import csv
    out = {}
    for r in csv.DictReader(open(os.path.join(DEAM, "annotations_summary.csv"))):
        out[int(r["song_id"])] = (float(r["valence_mean"]), float(r["arousal_mean"]))
    return out


def decode(path):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-ac", "2", "-ar", str(SR), "-f", "f32le", "-"], capture_output=True, check=True).stdout
    x = np.frombuffer(raw, np.float32).reshape(-1, 2).astype(np.float64)
    return x[:, 0], x[:, 1]


def _corr(a, b):
    a = a - a.mean(); b = b - b.mean(); d = np.sqrt((a * a).sum() * (b * b).sum())
    return float((a * b).sum() / d) if d > 0 else 0.0


def harmony(L, R):
    m =(0.5 * (L + R)).astype(np.float32)
    y = librosa.resample(m, orig_sr=SR, target_sr=22050); sr = 22050
    chroma = librosa.feature.chroma_cqt(y=y, sr=sr, hop_length=512)                       # (12, frames)
    prof = chroma.mean(1)
    maj = max(_corr(prof, np.roll(MAJ, k)) for k in range(12)); mnr = max(_corr(prof, np.roll(MIN, k)) for k in range(12))
    tempo, beats = librosa.beat.beat_track(y=y, sr=sr, hop_length=512)
    tempo = float(np.atleast_1d(tempo)[0])
    sync = librosa.util.sync(chroma, beats, aggregate=np.median) if len(beats) > 2 else chroma
    tri = {"maj": [0, 4, 7], "min": [0, 3, 7], "dim": [0, 3, 6]}
    best = []
    for c in sync.T:
        c = c / (c.sum() + 1e-9); sc = {q: max(c[[(k + i) % 12 for i in iv]].sum() for k in range(12)) for q, iv in tri.items()}
        best.append(max(sc, key=sc.get) + str(int(np.argmax([c[[(k + i) % 12 for i in tri["maj"]]].sum() for k in range(12)]))))
    q = [b[:3] for b in best] or ["maj"]
    change = float(np.mean([best[i] != best[i - 1] for i in range(1, len(best))])) if len(best) > 1 else 0.0
    p = prof / (prof.sum() + 1e-9); ent = float(-(p * np.log(p + 1e-12)).sum() / np.log(12))
    onset = librosa.onset.onset_strength(y=y, sr=sr, hop_length=512)
    bs = float(onset[beats].mean() / (onset.mean() + 1e-9)) if len(beats) else 1.0
    ac = librosa.autocorrelate(onset - onset.mean()); pulse = float(ac[10:200].max() / (ac[0] + 1e-9)) if len(ac) > 200 else 0.0
    cen = librosa.feature.spectral_centroid(y=y, sr=sr, hop_length=512)[0]; rms = librosa.feature.rms(y=y, hop_length=512)[0]
    th = np.array_split(np.arange(len(cen)), 3)
    lc = [float(np.log2(cen[i].mean() + 1)) for i in th]; ll = [float(20 * np.log10(rms[i].mean() + 1e-9)) for i in th]
    S = 0.5 * (L - R); M = 0.5 * (L + R); width = float(10 * np.log10(((S * S).mean() + 1e-12) / ((M * M).mean() + 1e-12)))
    return [maj, mnr, maj - mnr, max(maj, mnr), q.count("maj") / len(q), q.count("min") / len(q), q.count("dim") / len(q), change, ent,
            tempo, bs, pulse, *lc, *ll, ll[2] - ll[0], width]


def cmd_measure(a):
    lab = labels(); done = set()
    if os.path.exists(FEATS):
        for line in open(FEATS):
            try: done.add(json.loads(line)["id"])
            except ValueError: pass
    files = {}
    for p in glob.glob(os.path.join(DEAM, "**", "*.mp3"), recursive=True):
        try: files[int(os.path.splitext(os.path.basename(p))[0])] = p
        except ValueError: pass
    todo = [s for s in sorted(files) if s in lab and s not in done][: a.limit or None]
    print("%d songs with labels, %d measured, %d to go" % (len(set(files) & set(lab)), len(done), len(todo)), flush=True)
    os.makedirs(os.path.dirname(FEATS), exist_ok=True)
    with open(FEATS, "a") as f:
        for i, s in enumerate(todo):
            if os.path.exists(os.path.expanduser("~/STOP_OWN")): print("stop file, stopping"); break
            try:
                L, R = decode(files[s])
                row = {"id": s, "v": lab[s][0], "a": lab[s][1], "frame": ff.vector(ff.measure(L, R, SR)).tolist(), "harm": harmony(L, R)}
            except Exception as e:  # one bad file never stops the run
                print("song %d: %s" % (s, e), flush=True); continue
            f.write(json.dumps(row) + "\n"); f.flush()
            if i % 50 == 0: print("%d / %d" % (i, len(todo)), flush=True)


def cmd_fit(a):
    from sklearn.model_selection import KFold
    from sklearn.linear_model import RidgeCV
    from sklearn.ensemble import HistGradientBoostingRegressor, ExtraTreesRegressor
    from sklearn.pipeline import make_pipeline
    from sklearn.preprocessing import StandardScaler
    rows = [json.loads(l) for l in open(FEATS)]
    X = np.array([r["frame"] + r["harm"] for r in rows]); Xh = np.array([r["harm"] for r in rows])
    X = np.nan_to_num(X); Xh = np.nan_to_num(Xh)
    ys = {"valence": np.array([r["v"] for r in rows]), "arousal": np.array([r["a"] for r in rows])}
    models = {"ridge": lambda: make_pipeline(StandardScaler(), RidgeCV(alphas=np.logspace(-2, 4, 20))),
              "gbr": lambda: HistGradientBoostingRegressor(max_iter=300, learning_rate=0.05, max_leaf_nodes=15, l2_regularization=1.0),
              "extra": lambda: ExtraTreesRegressor(n_estimators=300, min_samples_leaf=3, max_features=0.3, n_jobs=2)}
    res = {"songs": len(rows), "bar": 0.8, "split": "KFold by song, %d splits" % a.splits}
    for tname, y in ys.items():
        for fname, XX in (("all", X), ("harmony_only", Xh)):
            for mname, mk in models.items():
                rs = []
                for tr, te in KFold(a.splits, shuffle=True, random_state=0).split(XX):
                    m = mk().fit(XX[tr], y[tr]); rs.append(_corr(m.predict(XX[te]), y[te]))
                res["%s_%s_%s" % (tname, fname, mname)] = {"r": round(float(np.mean(rs)), 3), "sd": round(float(np.std(rs)), 3)}
                print(tname, fname, mname, res["%s_%s_%s" % (tname, fname, mname)], flush=True)
    for tname in ys:
        best = max((k for k in res if k.startswith(tname + "_")), key=lambda k: res[k]["r"])
        res[tname + "_best"] = {"model": best, **res[best], "met": res[best]["r"] >= 0.8}
    # which single harmony numbers carry valence (plain correlation, all songs)
    res["valence_corr_by_harmony_feature"] = {n: round(_corr(Xh[:, i], ys["valence"]), 3) for i, n in enumerate(HARM)}
    json.dump(res, open(OUT, "w"), indent=1)
    print("valence best", res["valence_best"], "\narousal best", res["arousal_best"], "\n->", OUT)


def main():
    p = argparse.ArgumentParser(); sub = p.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("measure"); m.add_argument("--limit", type=int, default=0)
    f = sub.add_parser("fit"); f.add_argument("--splits", type=int, default=10)
    a = p.parse_args(); {"measure": cmd_measure, "fit": cmd_fit}[a.cmd](a)


if __name__ == "__main__":
    main()
