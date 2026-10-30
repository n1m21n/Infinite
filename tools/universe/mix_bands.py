#!/usr/bin/env python3
"""Where the generator's whole mixes put their power, band by band, against real songs (DEAM). PLAN-2026-10-30 item 1.
Renders every style x seeds as a whole mix with the wasm generator at rest (wasm_render_mood.js, mood and energy 0, which is
what every take is since the dots left the UI) and measures the share of power per band. DEAM songs are measured once and cached.
    ~/Infinite/ml-venv/bin/python3 -I tools/universe/mix_bands.py --gen build-wasm [--seeds 2] [--bars 8] [--deam 150] [--out FILE]
Prints generator vs DEAM and vs library full mixes (median share per band); writes JSON (per-render rows plus the summary)."""
import argparse, json, os, re, subprocess, sys, tempfile
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
EDGES = [0, 40, 80, 250, 500, 2000, 6000, 24000]
NAMES = ["sub <40", "40-80", "80-250", "250-500", "500-2k", "2k-6k", "6k+"]
DEAM = os.path.expanduser("~/Infinite/training-data/downloads/deam/audio/MEMD_audio")
CACHE = os.path.expanduser("~/Infinite/training-data/eval/deam_bands.json")
# Electronic full mixes from the sample library: pack song starters, construction-kit demos and music-kit full loops.
# DEAM is mostly rock, pop and classical; these are the same kind of track the generator makes.
LIB = os.path.expanduser("~/Infinite/training-data/audio/features.jsonl")
LIB_CACHE = os.path.expanduser("~/Infinite/training-data/eval/lib_fullmix_bands.json")
LIB_NAME = re.compile(r"(?i)(full[ _-]?mix|\bdemo\b|songstarter|song[ _]starter|kit\d+_\w+_full_loop)")
LIB_SKIP = re.compile(r"(?i)(drum[ _]?loop|percussion|ukulele|crash)")


def shares(mono, fs):
    spec = np.abs(np.fft.rfft(mono)) ** 2; f = np.fft.rfftfreq(len(mono), 1 / fs); tot = spec.sum() + 1e-20
    return [float(spec[(f >= lo) & (f < hi)].sum() / tot) for lo, hi in zip(EDGES[:-1], EDGES[1:])]


def deam_rows(n):
    if os.path.exists(CACHE):
        rows = json.load(open(CACHE))
        if len(rows) >= n: return rows[:n]
    import librosa
    files = sorted(os.listdir(DEAM), key=lambda s: int(s.split(".")[0]) if s.split(".")[0].isdigit() else 0)
    step = max(1, len(files) // n); rows = []
    for name in files[::step][:n]:
        y, fs = librosa.load(os.path.join(DEAM, name), sr=44100, mono=True, offset=15.0, duration=30.0)
        if len(y) > fs: rows.append({"song": name, "bands": shares(y, fs)})
    os.makedirs(os.path.dirname(CACHE), exist_ok=True); json.dump(rows, open(CACHE, "w"))
    return rows


def rebin32(b32):
    """The library's 32 log bands (20 Hz-20 kHz, audio_features.py) folded into EDGES, splitting a band by log width."""
    e = 20.0 * 1000.0 ** (np.arange(33) / 32); out = np.zeros(len(NAMES))
    for k, p in enumerate(b32):
        for i, (lo, hi) in enumerate(zip(EDGES[:-1], EDGES[1:])):
            ov = np.log(min(hi, e[k + 1])) - np.log(max(lo, e[k]))
            if ov > 0: out[i] += p * ov / np.log(e[k + 1] / e[k])
    return (out / (out.sum() + 1e-20)).tolist()


def bands32(mono, fs):
    spec = np.abs(np.fft.rfft(mono)) ** 2; f = np.fft.rfftfreq(len(mono), 1 / fs); e = 20.0 * 1000.0 ** (np.arange(33) / 32)
    return [float(spec[(f >= lo) & (f < hi)].sum()) for lo, hi in zip(e[:-1], e[1:])]


def lib_rows():
    """Library full mixes: measured from the audio like the generator (cached) when the drive is mounted, else folded from
    the stored 32 bands (20 Hz-20 kHz only)."""
    picks = []
    for line in open(LIB):
        r = json.loads(line)
        f = r["src"].split("/")[-1]
        if r.get("loop") and r.get("bands") and LIB_NAME.search(f) and not LIB_SKIP.search(f): picks.append(r)
    if os.path.exists(LIB_CACHE): return json.load(open(LIB_CACHE)), True
    if not all(os.path.exists(r["src"]) for r in picks[:5]):
        return [{"file": r["src"], "bands": rebin32(r["bands"])} for r in picks], False
    import librosa
    rows = []
    for r in picks:
        try: y, fs = librosa.load(r["src"], sr=44100, mono=True, duration=30.0)
        except Exception: continue
        if len(y) > fs: rows.append({"file": r["src"], "bands": shares(y, fs)})
    os.makedirs(os.path.dirname(LIB_CACHE), exist_ok=True); json.dump(rows, open(LIB_CACHE, "w"))
    return rows, True


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--gen", required=True); ap.add_argument("--seeds", type=int, default=2)
    ap.add_argument("--bars", type=int, default=8); ap.add_argument("--part", type=int, default=0); ap.add_argument("--deam", type=int, default=150)
    ap.add_argument("--out", default=os.path.expanduser("~/Infinite/training-data/eval/mix_bands.json")); a = ap.parse_args()
    import soundfile as sf
    gen = os.path.abspath(a.gen)
    count = int(subprocess.run(["node", "-e", "require(process.argv[1])({locateFile:f=>require('path').join(process.argv[2],f),print:()=>{},printErr:()=>{}})"
                                ".then(M=>{console.log(M._gen_style_count());process.exit(0)})", os.path.join(gen, "infinite_gen_wasm.js"), gen],
                               capture_output=True, text=True, check=True).stdout.strip())
    d = tempfile.mkdtemp(prefix="mixbands_"); jobs = []
    for s in range(count):
        for k in range(a.seeds):
            jobs.append({"style": s, "part": a.part, "seed": 1000 + 97 * k + s, "bars": a.bars, "mood": 0.0, "energy": 0.0,
                         "out": os.path.join(d, "%d_%d.wav" % (s, k))})
    jf = os.path.join(d, "jobs.json"); json.dump(jobs, open(jf, "w"))
    r = subprocess.run(["node", os.path.join(HERE, "wasm_render_mood.js"), gen, jf], capture_output=True, text=True, check=True)
    info = {json.loads(l)["out"]: json.loads(l) for l in r.stdout.splitlines() if l.startswith("{")}
    rows = []
    for j in jobs:
        if not info.get(j["out"], {}).get("frames"): continue
        x, fs = sf.read(j["out"], dtype="float64", always_2d=True)
        m = x.mean(1)
        rows.append({"style": j["style"], "seed": j["seed"], "label": info[j["out"]]["label"], "bands": shares(m, fs), "bands20": rebin32(bands32(m, fs)),
                     "power_db": float(10 * np.log10(np.mean(m ** 2) + 1e-20))})
        os.remove(j["out"])
    os.remove(jf); os.rmdir(d)
    ref = deam_rows(a.deam); lib, from_audio = lib_rows()
    g = np.median([r["bands"] for r in rows], 0); dm = np.median([r["bands"] for r in ref], 0); lb = np.median([r["bands"] for r in lib], 0)
    g20 = g if from_audio else np.median([r["bands20"] for r in rows], 0)
    print("%d generator mixes, %d DEAM songs, %d library full mixes (median power share)" % (len(rows), len(ref), len(lib)))
    print("library columns: measured from the audio" if from_audio else "library columns: 20 Hz-20 kHz only, folded from the stored 32 bands, the generator measured the same way")
    print("%-9s %9s %9s %7s | %9s %9s %7s" % ("band", "gen", "DEAM", "ratio", "gen 20+", "library", "ratio"))
    for n, x, y, x2, z in zip(NAMES, g, dm, g20, lb):
        print("%-9s %8.1f%% %8.1f%% %7.2f | %8.1f%% %8.1f%% %7.2f" % (n, 100 * x, 100 * y, x / (y + 1e-12), 100 * x2, 100 * z, x2 / (z + 1e-12)))
    by = {}
    for rw in rows: by.setdefault(rw["style"], []).append(rw["bands"][1])
    worst = sorted(((float(np.mean(v)), k) for k, v in by.items()), reverse=True)[:6]
    print("most 40-80 Hz (style index: share):", ", ".join("%d: %.0f%%" % (k, 100 * v) for v, k in worst))
    print("median level %.1f dB" % float(np.median([r["power_db"] for r in rows])))
    json.dump({"bands": NAMES, "gen_median": g.tolist(), "deam_median": dm.tolist(), "lib_median": lb.tolist(), "gen20_median": g20.tolist(), "rows": rows}, open(a.out, "w"), indent=1)


if __name__ == "__main__":
    main()
