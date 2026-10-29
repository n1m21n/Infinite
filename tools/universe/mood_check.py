#!/usr/bin/env python3
"""Machine check of how the live generator says mood and energy, before anyone listens (HANDOFF, C5 round 3).
Renders parts x styles x seeds in the four mood quadrants with the wasm generator (wasm_render_mood.js) and measures each clip:
mode (Krumhansl major minus minor), major and minor triad share per beat, onsets per second, brightness (log2 centroid),
register (chroma-weighted pitch centre from a CQT), tail (share of energy 300 ms after the onsets) and level checks for polish
(peak, sub under 40 Hz for parts that are not bass, DC). Prints a table per part, quadrant by quadrant, and writes JSON.
    ~/Infinite/ml-venv/bin/python3 -I tools/universe/mood_check.py --gen build-wasm [--parts 9,10,2,3,0] [--seeds 2] [--out FILE]
Pass: each cue moves the way the mood asks (mode Q1/Q4 above Q2/Q3, onsets Q1/Q2 above Q3/Q4, ...); the table says which do."""
import argparse, json, os, subprocess, sys, tempfile
import numpy as np
import numba, librosa, librosa.core.audio  # before the path change: numba probes `coverage`, and tools/universe/coverage.py would answer
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.append(HERE)
import frame_features as ff
import deam_valence as dv

DOTS = {"Q1": (0.8, 0.7), "Q2": (-0.8, 0.7), "Q3": (-0.8, -0.7), "Q4": (0.8, -0.7)}   # (mood, energy)
PARTS = {0: "all", 2: "pads", 3: "plucks", 9: "keys", 10: "leads", 1: "synths"}
# Which way each cue should move, as (higher quadrants, lower quadrants).
WANT = {"mode": (("Q1", "Q4"), ("Q2", "Q3")), "maj_minus_min": (("Q1", "Q4"), ("Q2", "Q3")), "onsets": (("Q1", "Q2"), ("Q3", "Q4")),
        "bright": (("Q1", "Q2"), ("Q3", "Q4")), "register": (("Q1",), ("Q3",)), "tail": (("Q3", "Q4"), ("Q1", "Q2"))}


def read(path):
    import soundfile as sf
    x, fs = sf.read(path, dtype="float64", always_2d=True)
    return x[:, 0], x[:, 1], fs


def register(L, R, fs):
    y = librosa.resample((0.5 * (L + R)).astype(np.float32), orig_sr=fs, target_sr=22050)
    C = np.abs(librosa.cqt(y, sr=22050, hop_length=512, fmin=librosa.note_to_hz("C1"), n_bins=84))
    w = C.sum(1); return float((np.arange(84) * w).sum() / (w.sum() + 1e-12) + 24)   # MIDI note of the energy centre


def tail(L, R, fs):
    m = np.abs(0.5 * (L + R)); hop = int(0.01 * fs); env = np.array([m[i:i + hop].mean() for i in range(0, len(m) - hop, hop)])
    on = librosa.onset.onset_detect(onset_envelope=np.maximum(0, np.diff(env, prepend=env[0])), sr=100, hop_length=1, units="frames")
    if len(on) == 0: return 0.0
    late = [env[o + 30:o + 60].mean() / (env[o:o + 10].max() + 1e-12) for o in on if o + 60 < len(env)]
    return float(np.mean(late)) if late else 0.0


def features(path):
    L, R, fs = read(path)
    h = dict(zip(dv.HARM, dv.harmony(*(librosa.resample(np.vstack([L, R]), orig_sr=fs, target_sr=dv.SR)))))
    d = dict(zip(ff.SCALARS, ff.measure(L, R, fs)["scalars"]))
    mono = 0.5 * (L + R); spec = np.abs(np.fft.rfft(mono)) ** 2; f = np.fft.rfftfreq(len(mono), 1 / fs)
    return {"mode": h["mode"], "maj_minus_min": h["maj_triad"] - h["min_triad"], "onsets": d["onsets_per_s"], "bright": d["centroid"],
            "register": register(L, R, fs), "tail": tail(L, R, fs), "peak_db": float(20 * np.log10(np.abs(np.vstack([L, R])).max() + 1e-12)),
            "sub_share": float(spec[f < 40].sum() / (spec.sum() + 1e-20)), "dc": float(abs(mono.mean()))}


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--gen", required=True); ap.add_argument("--parts", default="9,10,2,3,0")
    ap.add_argument("--styles", default="0,11,22,24,29"); ap.add_argument("--seeds", type=int, default=2); ap.add_argument("--bars", type=int, default=8)
    ap.add_argument("--out", default=os.path.expanduser("~/Infinite/training-data/eval/mood_check.json")); a = ap.parse_args()
    d = tempfile.mkdtemp(prefix="moodcheck_"); jobs = []
    for p in map(int, a.parts.split(",")):
        for s in map(int, a.styles.split(",")):
            for k in range(a.seeds):
                for q, (mo, en) in DOTS.items():
                    jobs.append({"style": s, "part": p, "seed": 1000 + 97 * k + s, "bars": a.bars, "mood": mo, "energy": en, "q": q,
                                 "out": os.path.join(d, "%d_%d_%d_%s.wav" % (p, s, k, q))})
    jf = os.path.join(d, "jobs.json"); json.dump(jobs, open(jf, "w"))
    r = subprocess.run(["node", os.path.join(HERE, "wasm_render_mood.js"), os.path.abspath(a.gen), jf], capture_output=True, text=True, check=True)
    info = {json.loads(l)["out"]: json.loads(l) for l in r.stdout.splitlines() if l.startswith("{")}
    rows = []
    for j in jobs:
        if not info.get(j["out"], {}).get("frames"): continue
        rows.append({**{k: j[k] for k in ("style", "part", "seed", "q")}, "label": info[j["out"]]["label"], "bpm": info[j["out"]]["bpm"], **features(j["out"])})
    json.dump(rows, open(a.out, "w"), indent=1)
    cues = list(WANT); hits = tot = 0
    for p in sorted({r["part"] for r in rows}):
        print("\n%s" % PARTS.get(p, p)); print("      " + "".join("%14s" % c for c in cues + ["bpm", "peak_db", "sub_share"]))
        means = {}
        for q in DOTS:
            rs = [r for r in rows if r["part"] == p and r["q"] == q]
            means[q] = {c: float(np.mean([r[c] for r in rs])) for c in cues + ["bpm", "peak_db", "sub_share"]} if rs else None
            if rs: print("  %s  " % q + "".join("%14.3f" % means[q][c] for c in cues + ["bpm", "peak_db", "sub_share"]))
        ok = []
        for c, (hi, lo) in WANT.items():
            if all(means[q] for q in DOTS):
                good = min(means[q][c] for q in hi) > max(means[q][c] for q in lo); ok.append("%s %s" % (c, "ok" if good else "MISS")); hits += good; tot += 1
        print("  " + ", ".join(ok))
    print("\ncues that move the right way: %d / %d   -> %s" % (hits, tot, a.out))


if __name__ == "__main__":
    main()
