#!/usr/bin/env python3
"""Finished tracks on the WD drive -> per-song mix targets (band shares, LUFS, crest, width, section arc).
Replaces the missing Harmonix Set. Resumable; stop file ~/STOP_MIDI.
    mix_audio.py run [--budget MIN] [--limit N]     mix_audio.py table
Rows: training-data/mix/audio_mix.jsonl.  LUFS = BS.1770 K-weighting, ungated mean (within ~1 LU of gated for music)."""
import argparse, subprocess, glob, json, os, random, re, statistics as st, sys, time
import numpy as np
from scipy.signal import lfilter, stft as _stft
OUT = os.path.expanduser("~/Infinite/training-data/mix/audio_mix.jsonl")
TABLE = os.path.expanduser("~/Infinite/repo/docs/plans/data-universe/feel/mix_audio_table.json")
STOP = os.path.expanduser("~/STOP_MIDI")
W = "/Volumes/Wd-Black/"
DIRS = ["my music", "random unreleased", "Music Downloads", "Discography"]
EXT = (".wav", ".mp3", ".flac", ".aif", ".aiff", ".m4a")
SR = 22050
BANDS = [("sub", 20, 60), ("bass", 60, 250), ("lowmid", 250, 500), ("mid", 500, 2000), ("himid", 2000, 6000), ("high", 6000, 11000)]

def kw(x, sr):
    # BS.1770 stage-1 shelf + RLB high-pass, coefficients for 48k re-derived by bilinear transform at sr
    f0, G, Q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    K = np.tan(np.pi * f0 / sr); Vh = 10 ** (G / 20); Vb = Vh ** 0.4996667741545416
    a0 = 1 + K / Q + K * K
    b = np.array([(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0])
    a = np.array([1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0])
    y = lfilter(b, a, x)
    f0, Q = 38.13547087602444, 0.5003270373238773
    K = np.tan(np.pi * f0 / sr); a0 = 1 + K / Q + K * K
    return lfilter([1, -2, 1], [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0], y)

def spec(x):
    return _stft(x, nperseg=2048, noverlap=1024, boundary=None)[2]

def row(path):
    y = np.frombuffer(subprocess.run(["ffmpeg", "-v", "error", "-t", "240", "-i", path, "-ac", "2", "-ar", str(SR), "-f", "f32le", "-"],
                                     capture_output=True, check=True).stdout, np.float32).reshape(-1, 2).T.copy()
    if y.ndim == 1:
        y = np.stack([y, y])
    if y.shape[1] < SR * 20:
        return None
    L, R = y[0], y[1]; m = (L + R) / 2; s = (L - R) / 2
    pw = (kw(L, SR) ** 2 + kw(R, SR) ** 2)
    blocks = pw[: len(pw) // (SR * 3 // 4) * (SR * 3 // 4)].reshape(-1, SR * 3 // 4).mean(1)
    lufs = -0.691 + 10 * np.log10(blocks.mean() + 1e-12)
    S = np.abs(spec(m)) ** 2
    f = np.fft.rfftfreq(2048, 1 / SR)
    e = {n: float(S[(f >= lo) & (f < hi)].sum()) for n, lo, hi in BANDS}
    tot = sum(e.values()) + 1e-12
    pk = float(np.abs(y).max()); rms = float(np.sqrt((y ** 2).mean()) + 1e-12)
    seg = SR * 8; n = len(m) // seg
    arc = [round(float(20 * np.log10(np.sqrt((m[i * seg:(i + 1) * seg] ** 2).mean()) + 1e-9)), 1) for i in range(n)]
    arc_d = [round(a - max(arc), 1) for a in arc]
    low = (f >= 20) & (f < 150)
    lc = float(np.corrcoef(np.abs(spec(L))[low].sum(0), np.abs(spec(R))[low].sum(0))[0, 1])
    return {"path": path, "genre": path.split("/")[-2], "dur": round(len(m) / SR, 1), "lufs": round(float(lufs), 1),
            "crest_db": round(20 * np.log10(pk / rms), 1), "side_mid": round(float(np.sqrt((s ** 2).mean() / ((m ** 2).mean() + 1e-12))), 3),
            "low_corr": round(lc, 3), "bands": {k: round(v / tot, 4) for k, v in e.items()},
            "tilt_db": round(10 * np.log10((e["himid"] + e["high"]) / (e["sub"] + e["bass"] + 1e-12)), 1),
            "arc_db": arc_d, "arc_range_db": round(max(arc) - min(arc), 1)}

def run(budget, limit):
    done = {json.loads(l)["path"] for l in open(OUT)} if os.path.exists(OUT) else set()
    fs = [p for d in DIRS for p in glob.glob(W + d + "/**/*", recursive=True)
          if p.lower().endswith(EXT) and "/._" not in p and "Backup" not in p and "/Samples/" not in p]
    random.Random(7).shuffle(fs)
    todo = [p for p in fs if p not in done][: limit or None]
    print(len(fs), "files,", len(done), "done,", len(todo), "this run", flush=True)
    t0 = time.time()
    with open(OUT, "a") as o:
        for i, p in enumerate(todo):
            if os.path.exists(STOP) or time.time() - t0 > budget * 60:
                break
            try:
                r = row(p)
                if r:
                    o.write(json.dumps(r) + "\n"); o.flush()
            except Exception as ex:
                print("skip", os.path.basename(p), str(ex)[:60])
            if i % 25 == 0:
                print(i, round(time.time() - t0), "s", flush=True)

def table():
    rows = [json.loads(l) for l in open(OUT)]
    q = lambda xs: [round(v, 2) for v in (np.percentile(xs, 25), np.median(xs), np.percentile(xs, 75))]
    t = {"n": len(rows), "lufs": q([r["lufs"] for r in rows]), "crest_db": q([r["crest_db"] for r in rows]),
         "side_mid": q([r["side_mid"] for r in rows]), "low_corr": q([r["low_corr"] for r in rows]),
         "tilt_db": q([r["tilt_db"] for r in rows]), "arc_range_db": q([r["arc_range_db"] for r in rows]),
         "bands": {b: q([r["bands"][b] for r in rows]) for b, _, _ in BANDS}, "by_genre": {}}
    for g in sorted({r["genre"] for r in rows}):
        gr = [r for r in rows if r["genre"] == g]
        if len(gr) >= 8:
            t["by_genre"][g] = {"n": len(gr), "lufs": q([r["lufs"] for r in gr])[1], "crest_db": q([r["crest_db"] for r in gr])[1],
                                "side_mid": q([r["side_mid"] for r in gr])[1], "bands": {b: q([r["bands"][b] for r in gr])[1] for b, _, _ in BANDS}}
    json.dump(t, open(TABLE, "w"), indent=1)
    print(json.dumps({k: v for k, v in t.items() if k != "by_genre"}, indent=1)); print(len(t["by_genre"]), "genre folders with >=8")

if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("cmd"); ap.add_argument("--budget", type=float, default=20); ap.add_argument("--limit", type=int, default=0)
    a = ap.parse_args()
    run(a.budget, a.limit) if a.cmd == "run" else table()
