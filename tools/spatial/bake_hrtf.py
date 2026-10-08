#!/usr/bin/env python3
"""Bake the MIT KEMAR compact HRTF set into src/audio/dsp/HrtfKemarData.cpp.

Source: Gardner & Martin, MIT Media Lab 1994, "HRTF Measurements of a KEMAR
Dummy-Head Microphone" - "provided without any usage restrictions; cite the
authors". Download https://sound.media.mit.edu/resources/KEMAR/compact.zip and
unzip it, then:  python3 tools/spatial/bake_hrtf.py <unzipped dir>

What the bake does (all offline, so the app ships a plain table, no SOFA/HDF5
reader on three platforms):
  1. mirror the measured 0..180 deg azimuths to the left side (ears swapped);
  2. strip each ear's onset delay (kept separately, so interpolation between
     neighbouring azimuths never smears the ITD);
  3. diffuse-field equalise (cos(el)-weighted mean power over every direction,
     1/3-octave smoothed, +-12 dB, minimum-phase) so a source at any angle has
     the same average loudness - this is what removes the KEMAR/mic colouration;
  4. resample onto a regular grid: elevation -40..90 step 10, azimuth 0..355
     step 5, then normalise the mean power gain to 1.
"""
import math
import os
import sys
import wave

import numpy as np

TAPS = 128
EL = list(range(-40, 91, 10))
AZ_STEP = 5
NAZ = 360 // AZ_STEP
FS = 44100


def read_pair(path):
    with wave.open(path, "rb") as w:
        assert w.getnchannels() == 2 and w.getsampwidth() == 2
        raw = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64) / 32768.0
    raw = raw.reshape(-1, 2)
    return raw[:, 0], raw[:, 1]


def load_measured(root):
    data = {}  # el -> sorted [(az, L, R)]
    for el in EL:
        d = os.path.join(root, "elev%d" % el)
        rows = []
        for f in sorted(os.listdir(d)):
            if not f.endswith(".wav"):
                continue
            az = int(f[f.index("e") + 1:f.index("a.")])
            l, r = read_pair(os.path.join(d, f))
            rows.append((az, l, r))
        rows.sort(key=lambda t: t[0])
        data[el] = rows
    return data


def onset(h, thr):
    idx = np.nonzero(np.abs(h) >= thr)[0]
    return int(idx[0]) if len(idx) else 0


def align(l, r):
    thr = 0.12 * max(np.abs(l).max(), np.abs(r).max())
    ol, orr = onset(l, thr) - 2, onset(r, thr) - 2
    ol, orr = max(ol, 0), max(orr, 0)
    base = min(ol, orr)
    out = []
    for h, o in ((l, ol), (r, orr)):
        a = np.zeros(TAPS)
        seg = h[o:o + TAPS]
        a[:len(seg)] = seg
        out.append(a)
    return out[0], out[1], float(ol - base), float(orr - base)


def minphase_from_mag(mag):
    n = len(mag)
    cep = np.fft.irfft(np.log(np.maximum(mag, 1e-6)), n)
    fold = np.zeros(n)
    fold[0] = cep[0]
    fold[1:n // 2] = 2 * cep[1:n // 2]
    fold[n // 2] = cep[n // 2]
    return np.fft.irfft(np.exp(np.fft.rfft(fold)), n)


def main():
    root = sys.argv[1]
    meas = load_measured(root)

    # step 1+2: aligned IRs on the measured azimuths, mirrored to 0..360
    full = {}  # el -> dict az -> (L, R, dl, dr)  (az as measured, 0..355)
    for el, rows in meas.items():
        d = {}
        for az, l, r in rows:
            al, ar, dl, dr = align(l, r)
            d[az] = (al, ar, dl, dr)
            if 0 < az < 180:
                d[360 - az] = (ar, al, dr, dl)  # mirrored: ears swap
        full[el] = d

    # step 3: diffuse-field EQ
    nfft = 512
    acc, wsum = np.zeros(nfft // 2 + 1), 0.0
    for el, d in full.items():
        w = math.cos(math.radians(el)) + 1e-3
        for az, (l, r, _, _) in d.items():
            for h in (l, r):
                acc += w * np.abs(np.fft.rfft(h, nfft)) ** 2
                wsum += w
    avg = np.sqrt(acc / wsum)
    f = np.fft.rfftfreq(nfft, 1.0 / FS)
    sm = avg.copy()
    for i, fc in enumerate(f):
        if fc <= 0:
            continue
        lo, hi = fc / 2 ** (1 / 6), fc * 2 ** (1 / 6)
        sel = (f >= lo) & (f <= hi)
        sm[i] = math.sqrt(np.mean(avg[sel] ** 2)) if sel.any() else avg[i]
    corr = np.clip(1.0 / np.maximum(sm, 1e-6), 10 ** (-12 / 20), 10 ** (12 / 20))
    corr /= np.exp(np.mean(np.log(corr[(f > 200) & (f < 8000)])))  # unity mid-band
    corr[f < 60] = corr[(f >= 60).argmax()]
    eq = minphase_from_mag(corr)[:TAPS]
    eq *= np.hanning(2 * TAPS)[TAPS:] ** 0.5  # gentle tail taper
    for el, d in full.items():
        for az, (l, r, dl, dr) in list(d.items()):
            d[az] = (np.convolve(l, eq)[:TAPS], np.convolve(r, eq)[:TAPS], dl, dr)

    # step 4: regular grid
    grid_ir = np.zeros((len(EL), NAZ, 2, TAPS))
    grid_dl = np.zeros((len(EL), NAZ, 2))
    for ei, el in enumerate(EL):
        d = full[el]
        azs = sorted(d.keys())
        if len(azs) == 1:  # pole: one measurement everywhere
            l, r, dl, dr = d[azs[0]]
            for ai in range(NAZ):
                grid_ir[ei, ai, 0], grid_ir[ei, ai, 1] = l, r
                grid_dl[ei, ai] = (dl, dr)
            continue
        azs_ext = azs + [azs[0] + 360]
        for ai in range(NAZ):
            a = ai * AZ_STEP
            k = max(i for i, v in enumerate(azs_ext) if v <= a) if a >= azs_ext[0] else 0
            a0, a1 = azs_ext[k], azs_ext[min(k + 1, len(azs_ext) - 1)]
            w = 0.0 if a1 == a0 else (a - a0) / (a1 - a0)
            p0 = d[a0 % 360]
            p1 = d[a1 % 360]
            grid_ir[ei, ai, 0] = (1 - w) * p0[0] + w * p1[0]
            grid_ir[ei, ai, 1] = (1 - w) * p0[1] + w * p1[1]
            grid_dl[ei, ai] = ((1 - w) * p0[2] + w * p1[2], (1 - w) * p0[3] + w * p1[3])

    # Loudness-flat: equalise the two-ear level of every direction over
    # 100 Hz - 6 kHz with pink (equal-per-octave) weighting. Gain only, so ITD,
    # ILD and the spectral shape of each direction are untouched.
    fr = np.fft.rfftfreq(nfft, 1.0 / FS)
    band = (fr >= 100) & (fr <= 6000)
    wpink = 1.0 / np.maximum(fr, 1.0)
    spec = np.abs(np.fft.rfft(grid_ir, nfft, axis=3)) ** 2  # [el, az, ear, bins]
    e_dir = np.sum(spec[..., band] * wpink[band], axis=(2, 3))
    ref = np.exp(np.mean(np.log(e_dir)))
    broad_before = np.sum(grid_ir ** 2, axis=(2, 3))
    grid_ir *= np.sqrt(ref / e_dir)[:, :, None, None]
    broad_after = np.sum(grid_ir ** 2, axis=(2, 3))
    ring = EL.index(0)
    print("horizontal ring broadband spread dB: before %.2f after %.2f" % (
        10 * np.log10(broad_before[ring].max() / broad_before[ring].min()),
        10 * np.log10(broad_after[ring].max() / broad_after[ring].min())))
    power = np.mean(np.sum(grid_ir ** 2, axis=3))
    grid_ir /= math.sqrt(power)
    peak = np.abs(grid_ir).max()
    scale = peak / 32767.0
    q = np.round(grid_ir / scale).astype(np.int16)

    out = os.path.join(os.path.dirname(__file__), "..", "..", "src", "audio", "dsp", "HrtfKemarData.cpp")
    with open(out, "w") as fh:
        fh.write("// GENERATED by tools/spatial/bake_hrtf.py - do not edit.\n")
        fh.write("// MIT KEMAR (Gardner & Martin, MIT Media Lab 1994): provided without usage\n")
        fh.write("// restrictions; credit requested. Diffuse-field equalised, onset-aligned.\n")
        fh.write('#include "audio/dsp/HrtfKemarData.h"\n\nnamespace HrtfKemar\n{\n')
        fh.write("const float kScale = %.9ef;\n" % scale)
        fh.write("const float kDelay[kNumEl * kNumAz * 2] = {\n")
        flat = grid_dl.reshape(-1)
        for i in range(0, len(flat), 16):
            fh.write(",".join("%.3f" % v for v in flat[i:i + 16]) + ",\n")
        fh.write("};\nconst short kIr[kNumEl * kNumAz * 2 * kTaps] = {\n")
        flat = q.reshape(-1)
        for i in range(0, len(flat), 32):
            fh.write(",".join(str(int(v)) for v in flat[i:i + 32]) + ",\n")
        fh.write("};\n}\n")
    print("wrote", out, "peak", peak, "taps", TAPS, "grid", grid_ir.shape)


if __name__ == "__main__":
    main()
