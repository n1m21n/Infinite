#!/usr/bin/env python3
"""Body modes for the String node fitted from one instrument's recorded notes (PLAN-2026-10-30 item 7).
    string_body_fit.py --dir "<folder of one instrument's single notes>" --name guitar [--modes 10] [--out FILE]
Each note: the first second after its onset, power spectrum. Averaged in dB over every note, the string's harmonic combs (each at
its own pitch) blur out and what stays is what every note shares: the body (and the microphone). That average, smoothed to
1/6 octave, minus a straight tilt in log frequency, is the body curve. Peaks are taken greedily from it (frequency, Q from the
-3 dB width, gain from the height), each peak removed before the next, at least 1/4 octave apart, 80 Hz - 5 kHz.
Prints a C++ row for StringNode's body tables and the fit (correlation of the fitted modes' response with the curve; the same for
today's kBodyModes, as the baseline). Writes JSON (curve, modes) to --out."""
import argparse, glob, json, os, sys
import numpy as np
import soundfile as sf

SR = 44100
N = 1 << 16
# today's kBodyModes in StringNode.cpp (hz, q, gain), the baseline
CLASSIC = [(98, 14, 1.0), (204, 22, .85), (283, 26, .6), (399, 28, .55), (552, 30, .42), (728, 30, .35), (1020, 26, .3), (1430, 22, .26),
           (2200, 14, .22), (3300, 8, .18)]


def note_spectrum(path):
    x, fs = sf.read(path, dtype="float64", always_2d=True)
    x = x.mean(1)
    if fs != SR:
        import librosa
        x = librosa.resample(x, orig_sr=fs, target_sr=SR)
    env = np.abs(x); k = int(np.argmax(env > env.max() * 0.1)) if env.max() > 0 else 0
    seg = x[k:k + SR]
    if len(seg) < SR // 4:
        return None
    seg = seg * np.hanning(len(seg))
    p = np.abs(np.fft.rfft(seg, N)) ** 2
    return 10 * np.log10(p / (p.sum() + 1e-30) + 1e-30)


def smooth_oct(f, y, frac=6):
    """Mean of y over +-1/(2*frac) octave around each point of a log-spaced grid."""
    grid = 40 * 2 ** (np.arange(0, np.log2(8000 / 40), 1 / 48))
    out = np.array([y[(f >= g * 2 ** (-0.5 / frac)) & (f < g * 2 ** (0.5 / frac))].mean() for g in grid])
    return grid, out


def bp_response(f, modes):
    """Sum of RBJ constant-0-dB-peak bandpasses (what StringNode's body bank adds), in dB."""
    s = 1j * f
    h = np.zeros(len(f), complex)
    for hz, q, g in modes:
        w0 = hz
        h += g * (s * w0 / q) / (s * s + s * w0 / q + w0 * w0)
    return 20 * np.log10(np.abs(h) + 1e-9)


def fit(grid, curve, n):
    lg = np.log2(grid); band = (grid >= 80) & (grid <= 5000)
    A = np.vstack([lg[band], np.ones(band.sum())]).T
    tilt = np.linalg.lstsq(A, curve[band], rcond=None)[0]
    res = curve - (tilt[0] * lg + tilt[1])
    r = res.copy(); modes = []
    for _ in range(n):
        cand = [i for i in np.flatnonzero(band) if all(abs(lg[i] - np.log2(m[0])) >= 0.25 for m in modes)]
        if not cand:
            break
        i = max(cand, key=lambda j: r[j]); top = r[i]
        lo = i
        while lo > 0 and r[lo] > top - 3: lo -= 1
        hi = i
        while hi < len(r) - 1 and r[hi] > top - 3: hi += 1
        bw = max(grid[hi] - grid[lo], grid[i] * 0.02)
        q = float(np.clip(grid[i] / bw, 2, 40))
        modes.append((float(grid[i]), q, top))
        sig = max(np.log2(grid[hi] / grid[lo]), 1 / 24) / 2.355  # the -3 dB width as a Gaussian in octaves
        r = r - max(top, 0) * np.exp(-0.5 * ((lg - lg[i]) / sig) ** 2)
    gmax = max(m[2] for m in modes)
    modes = [(hz, q, float(10 ** ((g - gmax) / 20))) for hz, q, g in sorted(modes)]
    return res, modes


def corr(a, b):
    a = a - a.mean(); b = b - b.mean()
    return float((a * b).sum() / np.sqrt((a * a).sum() * (b * b).sum()))


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--dir", required=True); ap.add_argument("--name", required=True)
    ap.add_argument("--modes", type=int, default=10); ap.add_argument("--out", default=""); a = ap.parse_args()
    files = sorted(p for e in ("wav", "aif", "aiff") for p in glob.glob(os.path.join(a.dir, "*." + e)))
    specs = [s for s in (note_spectrum(p) for p in files) if s is not None]
    if len(specs) < 8:
        raise SystemExit("%d usable notes in %s; need 8" % (len(specs), a.dir))
    f = np.fft.rfftfreq(N, 1 / SR)
    grid, curve = smooth_oct(f, np.mean(specs, 0))
    res, modes = fit(grid, curve, a.modes)
    band = (grid >= 80) & (grid <= 5000)
    r_fit = corr(bp_response(grid, modes)[band], res[band]); r_classic = corr(bp_response(grid, CLASSIC)[band], res[band])
    print("%s: %d notes from %s" % (a.name, len(specs), a.dir))
    print("fit vs body curve r %.3f   today's kBodyModes r %.3f" % (r_fit, r_classic))
    print("   { // %s, string_body_fit.py, %d notes" % (a.name, len(specs)))
    print("      " + " ".join("{ %.1ff, %.1ff, %.2ff }," % m for m in modes))
    print("   },")
    out = a.out or os.path.expanduser("~/Infinite/training-data/eval/string_body_%s.json" % a.name)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    json.dump({"name": a.name, "dir": a.dir, "notes": len(specs), "grid": grid.tolist(), "curve": res.tolist(), "modes": modes,
               "r_fit": r_fit, "r_classic": r_classic}, open(out, "w"))


if __name__ == "__main__":
    sys.exit(main())
