#!/usr/bin/env python3
"""Each Generate Samples part against real sample-pack audio of the same part (owner's 2/10 review, 2026-10-29).
Renders every style x part with the wasm generator (wasm_render_parts.js, dots at rest) and measures each take with the numbers
audio_features.py gives the sample library (centroid, low / mid / high share, crest, flatness, onsets), plus the low-mid share
(250 Hz - 2 kHz), sub share (< 40 Hz), clipped-sample share, and for drum parts how alike two seeds of one style are.
Prints, per part, generator median vs library median (loops of that part) and the gap in library spreads (IQR).
    ~/Infinite/ml-venv/bin/python3 -I tools/universe/part_vs_library.py --gen build-wasm [--seeds 2] [--out FILE]"""
import argparse, json, math, os, subprocess, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.expanduser("~/Infinite/training-data/audio/features.jsonl")
FS, FFT, HOP, BANDS = 48000, 4096, 1024, 32
NAMES = {1: "synths", 2: "pads", 3: "plucks", 4: "percussion", 5: "drums", 6: "bass", 7: "fx", 8: "foley", 9: "keys", 10: "leads",
         11: "textures", 12: "kicks", 13: "snares", 14: "hats", 15: "toms"}
ROLE = {1: "synth", 2: "pad", 3: "pluck", 4: "perc", 5: "drums", 6: "bass", 7: "fx", 9: "keys", 10: "lead", 12: "kick", 13: "snare",
        14: "hat", 15: "tom"}
DRUMS = {4, 5, 12, 13, 14, 15}
KEYS = ["centroid", "low", "lowmid", "high", "sub", "crest", "flatness", "onsets_per_s"]

_win = 0.5 * (1 - np.cos(2 * np.pi * np.arange(FFT) / FFT))
_freqs = np.arange(FFT // 2 + 1) * FS / FFT
_edges = 20.0 * (1000.0 ** (np.arange(BANDS + 1) / BANDS))


def measure(x):
    """x: (2, n) float. The library's numbers (audio_features.analyse) plus lowmid, sub, clip and the onset envelope."""
    n = x.shape[1]; peak = float(np.abs(x).max()); rms = float(np.sqrt((x ** 2).mean())) or 1e-12
    starts = np.arange(0, n - FFT + 1, HOP)
    fr = np.stack([x[:, s:s + FFT] for s in starts], 1) * _win
    sp = np.fft.rfft(fr, axis=2); pw = (0.5 * (sp.real ** 2 + sp.imag ** 2)).sum(0)
    m = pw.mean(0); s = float(m.sum()) or 1e-30
    share = lambda lo, hi: float(m[(_freqs >= lo) & (_freqs < hi)].sum()) / s
    lm = np.log1p(1000 * np.sqrt(pw / (FFT * 0.375 * FFT)))
    flux = np.maximum(0, np.diff(lm, axis=0)).sum(1)
    thr = flux.mean() + 1.5 * flux.std()
    on = [i for i in range(1, len(flux) - 1) if flux[i] > thr and flux[i] >= flux[i - 1] and flux[i] >= flux[i + 1]]
    lin = np.sqrt(m[1:] + 1e-20)
    return {"centroid": float((_freqs * m).sum()) / s, "low": share(20, 20 * 1000 ** (11 / 32)),
            "lowmid": share(250, 2000), "high": share(20 * 1000 ** (22 / 32), 20000), "sub": share(0, 40),
            "crest": peak / rms, "flatness": float(np.exp(np.log(lin).mean()) / lin.mean()),
            "onsets_per_s": len(on) / (n / FS), "clip": float((np.abs(x) > 0.99).mean()), "peak": peak}, flux


def library():
    """Median and IQR per role over the library's loops (one-shots measure differently)."""
    acc = {}
    with open(LIB) as f:
        for line in f:
            r = json.loads(line)
            if not r.get("loop") or r.get("part") not in ROLE.values() or not r.get("bands"): continue
            b = np.array(r["bands"]); t = b.sum() or 1e-30
            lo = 20 * 1000 ** (np.arange(32) / 32); hi = 20 * 1000 ** (np.arange(1, 33) / 32)
            v = {"centroid": r["centroid"], "low": r["low"], "high": r["high"], "crest": r["crest"], "flatness": r["flatness"],
                 "lowmid": float(b[(lo >= 250) & (hi <= 2100)].sum() / t), "sub": float(b[hi <= 41].sum() / t),
                 "onsets_per_s": r["onsets"] / max(0.5, r["seconds"])}
            acc.setdefault(r["part"], []).append(v)
    return {p: {k: (float(np.median([v[k] for v in rows])), float(np.subtract(*np.percentile([v[k] for v in rows], [75, 25]))), len(rows))
                for k in KEYS} for p, rows in acc.items()}


def renders(gen, seeds):
    p = subprocess.Popen(["node", os.path.join(HERE, "wasm_render_parts.js"), gen, "9000", str(seeds), os.environ.get("PARTS", ",".join(map(str, NAMES)))],
                         stdout=subprocess.PIPE)
    while True:
        h = p.stdout.readline()
        if not h: break
        h = json.loads(h); raw = p.stdout.read(h["frames"] * 8)
        yield h, np.frombuffer(raw, np.float32).reshape(-1, 2).T.astype(np.float64)


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--gen", default="build-wasm"); ap.add_argument("--seeds", type=int, default=2)
    ap.add_argument("--out", default=None); a = ap.parse_args()
    rows, flux = [], {}
    for h, x in renders(a.gen, a.seeds):
        if np.abs(x).max() < 1e-6: rows.append({**h, "silent": True}); continue
        v, fl = measure(x); rows.append({**{k: h[k] for k in ("style", "id", "part", "seed", "label")}, **v})
        flux.setdefault((h["style"], h["part"]), []).append(fl)
    lib = library()
    print("%-11s %5s | %s" % ("part", "n", "  ".join("%-22s" % k for k in KEYS)))
    for part, name in NAMES.items():
        g = [r for r in rows if r["part"] == part and not r.get("silent")]
        if not g: continue
        cells = []
        for k in KEYS:
            med = float(np.median([r[k] for r in g]))
            if ROLE.get(part) in lib:
                lm, iqr, _ = lib[ROLE[part]][k]; z = (med - lm) / (iqr or 1e-9)
                cells.append("%7.3g vs %-7.3g %+4.1f" % (med, lm, z))
            else: cells.append("%7.3g %14s" % (med, ""))
        print("%-11s %5d | %s" % (name, len(g), "  ".join("%-22s" % c for c in cells)))
        clip = np.mean([r["clip"] > 1e-4 for r in g]); print("%-11s       | clipped takes %.0f%%, silent %d" %
              ("", 100 * clip, sum(1 for r in rows if r["part"] == part and r.get("silent"))))
        if part in DRUMS:   # how alike two seeds of one style are: onset-envelope correlation
            cs = []
            for (st, pt), fl in flux.items():
                if pt == part and len(fl) >= 2:
                    k = min(len(fl[0]), len(fl[1])); cs.append(float(np.corrcoef(fl[0][:k], fl[1][:k])[0, 1]))
            if cs: print("%-11s       | seed-to-seed onset correlation median %.2f (1 = same groove)" % ("", np.nanmedian(cs)))
    if a.out:
        json.dump({"rows": rows, "library": lib}, open(a.out, "w"))


if __name__ == "__main__":
    main()
