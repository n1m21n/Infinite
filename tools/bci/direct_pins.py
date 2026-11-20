#!/usr/bin/env python3
"""Python mirror of Brain Source's direct Image and Audio pins, plus the M3 blind test.

Image pin: a state strip (calibrated eyes-closed score 0..1, state_mirror.py; grey until
  calibrated) over 4 rows (AF7 AF8 TP9 TP10) x 40 bins (1-40 Hz), scrolling left, one column per
  engine PSD update (1 s Hann periodogram every 12 samples, 21.3 columns/s).
  Pixel = log10 power. Two normalisations:
    raw  : per channel, range = 5th..95th percentile of the calibration log power (all bins)
    z    : per channel and bin, (x - median) / (1.4826 * MAD) of the calibration, mapped -3..+3
           (used by the pin: flattens 1/f so the alpha stripe shows). Columns smoothed, EMA tau 0.5 s.
  Calibration = first 30 s of the eyes-open segment (no labels needed, nothing fitted).
Audio pin: each channel HP 2 Hz + LP 40 Hz (Butterworth 4), causal FIR Hilbert (65 taps), y = a * cos(32 * phase):
  10 Hz alpha -> 320 Hz. Amplitude / calibration median amplitude, soft-limited.
  AF7 + TP9 left, AF8 + TP10 right.

M3 (2AFC): a pair = 10 s eyes open + 10 s eyes closed from the same session in random order;
the viewer picks the eyes-closed clip. Proxy observer = brighter 8-12 Hz band (image) /
more 256-384 Hz energy (audio). Bar: 9/10.

usage: direct_pins.py proxy  CSV_DIR            proxy accuracy over every HEROIC session
       direct_pins.py blind  CSV_DIR OUT_DIR    10 pairs: sheet.png + wavs + key.json
"""
import json
import os
import random
import sys
import wave

import numpy as np
from scipy import signal

FS, HOP, N = 256, 12, 256
CAL, CLIP, AUDIO_FS, MULT = 30.0, 10.0, 48000, 32
BINS = np.arange(1, 41)  # 1 Hz bins (N = FS)
CH = ["AF7", "AF8", "TP9", "TP10"]

# Inferno-like anchors, 0..1 -> RGB
_CM = np.array([[0, 0, 4], [40, 11, 84], [101, 21, 110], [159, 42, 99], [212, 72, 66],
                [245, 125, 21], [250, 193, 39], [252, 255, 164]], float)


def cmap(v):
    v = np.clip(v, 0, 1) * (len(_CM) - 1)
    i = np.minimum(v.astype(int), len(_CM) - 2)
    t = (v - i)[..., None]
    return ((1 - t) * _CM[i] + t * _CM[i + 1]).astype(np.uint8)


IMG_A = 1 - np.exp(-(HOP / FS) / 0.5)


def spectro(x, smooth=True):
    """x (4, n) -> (cols, 4, 40) log10 power, one column per engine update."""
    hann = 0.5 * (1 - np.cos(2 * np.pi * np.arange(N) / (N - 1)))
    starts = range(0, x.shape[1] - N + 1, HOP)
    out = np.empty((len(starts), 4, len(BINS)))
    for k, s in enumerate(starts):
        p = np.abs(np.fft.rfft(x[:, s:s + N] * hann, axis=1)) ** 2
        out[k] = np.log10(np.maximum(p[:, BINS], 1e-6))
        if smooth and k:
            out[k] = out[k - 1] + IMG_A * (out[k] - out[k - 1])
    return out


class Norm:
    def __init__(self, cal_spec):
        self.lo = np.percentile(cal_spec, 5, axis=(0, 2))[:, None]
        self.hi = np.percentile(cal_spec, 95, axis=(0, 2))[:, None]
        self.med = np.median(cal_spec, axis=0)
        self.mad = 1.4826 * np.median(np.abs(cal_spec - self.med), axis=0) + 1e-6

    def raw(self, s):
        return (s - self.lo) / (self.hi - self.lo + 1e-9)

    def z(self, s):
        return ((s - self.med) / self.mad + 3) / 6


STRIP = 12


def state_track(cal_open, cal_closed, x):
    """Calibrate the engine mirror on 30 s + 30 s, then score x: one value per update after 1 s."""
    import state_mirror as sm
    e = sm.Engine()
    ov, cv = [], []
    e.run(cal_open, lambda t, v: ov.append(v) if t >= sm.SETTLE else None)
    e.run(cal_closed, lambda t, v: cv.append(v) if t >= sm.SETTLE else None)
    e.w, e.bias, e.scale = sm.fit(ov, cv)
    out = []
    e.run(x, lambda t, v: out.append(e.score(v)) if t >= N / FS else None)
    return np.array(out)


def image(s01, state=None):
    """(cols, 4, 40) 0..1 -> RGB (STRIP + 4*40 + 4 gaps, cols); low freq at the bottom of each row."""
    cols = s01.shape[0]
    st = np.full(cols, 0.5) if state is None else np.resize(state, cols)
    rows = [np.repeat(cmap(st)[None], STRIP, 0) if state is not None
            else np.full((STRIP, cols, 3), 128, np.uint8), np.full((3, cols, 3), 255, np.uint8)]
    for c in range(4):
        rows.append(cmap(s01[:, c, ::-1].T))
        if c < 3:
            rows.append(np.full((3, s01.shape[0], 3), 255, np.uint8))
    return np.vstack(rows)


# Same as BciDspEngine: 4th-order Butterworth high-pass 2 Hz + low-pass 40 Hz, then a 65-tap
# Hamming-windowed ideal Hilbert transformer (h[n] = 2 / (pi n), odd n), delay 32 samples (125 ms).
_BP = np.vstack([signal.butter(4, 2, "highpass", fs=FS, output="sos"),
                 signal.butter(4, 40, "lowpass", fs=FS, output="sos")])
_n = np.arange(65) - 32
_HIL = np.where(_n % 2 != 0, 2 / (np.pi * np.where(_n == 0, 1, _n)), 0.0) * np.hamming(65)


def audio(x, cal_amp=None):
    """x (4, n) -> stereo int16 at 48 kHz, plus the per-channel amplitude used to normalise."""
    xb = signal.sosfilt(_BP, x, axis=1)
    d = (len(_HIL) - 1) // 2
    im = signal.lfilter(_HIL, 1, xb, axis=1)
    re = np.concatenate([np.zeros((4, d)), xb[:, :-d]], axis=1)
    amp, ph = np.hypot(re, im), np.unwrap(np.arctan2(im, re), axis=1)
    if cal_amp is None:
        cal_amp = np.median(amp, axis=1) + 1e-9
    n_out = int(x.shape[1] * AUDIO_FS / FS)
    t_in, t_out = np.arange(x.shape[1]) / FS, np.arange(n_out) / AUDIO_FS
    voices = np.array([np.tanh(0.5 * np.interp(t_out, t_in, amp[c]) / cal_amp[c]) *
                       np.cos(MULT * np.interp(t_out, t_in, ph[c])) for c in range(4)])
    st = np.stack([voices[0] + voices[2], voices[1] + voices[3]]) * 0.35
    return (np.clip(st, -1, 1) * 32767).astype(np.int16), cal_amp


def alpha_ratio(st):
    f, p = signal.welch(st.astype(float).sum(0), AUDIO_FS, nperseg=8192)
    return p[(f >= 256) & (f <= 384)].sum() / p[(f >= 64) & (f <= 1280)].sum()


def sessions(d):
    for f in sorted(os.listdir(d)):
        if f.endswith("_open.csv"):
            tag = f[:-9]
            xo = np.loadtxt(os.path.join(d, f), delimiter=",").T[:4]
            xc = np.loadtxt(os.path.join(d, tag + "_closed.csv"), delimiter=",").T[:4]
            yield tag, xo, xc


def pair(xo, xc, end_offset=0.0):
    """Last 10 s (minus end_offset) of each segment, plus the open-segment calibration."""
    n, e = int(CLIP * FS), int(end_offset * FS)
    cal = xo[:, :int(CAL * FS)]
    pair.cal_closed = xc[:, :int(CAL * FS)]
    co = xo[:, xo.shape[1] - e - n - N:xo.shape[1] - e]
    cc = xc[:, xc.shape[1] - e - n - N:xc.shape[1] - e]
    return cal, co, cc


def proxy(d):
    res = {"raw": [], "z": [], "audio": []}
    for tag, xo, xc in sessions(d):
        for off in (0.0, 10.0):
            if xo.shape[1] - (off + CLIP) * FS < CAL * FS:
                continue
            cal, co, cc = pair(xo, xc, off)
            nm = Norm(spectro(cal))
            so, sc = spectro(co), spectro(cc)
            a = (BINS >= 8) & (BINS <= 12)
            res["raw"].append(nm.raw(sc)[:, :, a].mean() > nm.raw(so)[:, :, a].mean())
            res["z"].append(nm.z(sc)[:, :, a].mean() > nm.z(so)[:, :, a].mean())
            _, amp = audio(cal)
            res["audio"].append(alpha_ratio(audio(cc, amp)[0]) > alpha_ratio(audio(co, amp)[0]))
    for k, v in res.items():
        print(f"M3_PROXY {k:5s} {np.mean(v):.3f}  ({sum(v)}/{len(v)} pairs)")


def write_wav(path, st):
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(AUDIO_FS)
        w.writeframes(st.T.tobytes())


def blind(d, out):
    from PIL import Image, ImageDraw
    os.makedirs(out, exist_ok=True)
    rng = random.Random(7)
    allp = list(sessions(d))
    picks = rng.sample(allp, 10)
    key, tiles = {}, []
    for i, (tag, xo, xc) in enumerate(picks, 1):
        cal, co, cc = pair(xo, xc)
        nm = Norm(spectro(cal))
        _, amp = audio(cal)
        clips = [("open", co), ("closed", cc)]
        rng.shuffle(clips)
        key[f"pair{i:02d}"] = {"session": tag, "closed": "A" if clips[0][0] == "closed" else "B"}
        row = []
        for lab, (state, x) in zip("AB", clips):
            write_wav(os.path.join(out, f"pair{i:02d}_{lab}.wav"), audio(x, amp)[0])
            lead = (xo if state == "open" else xc)
            end = lead.shape[1]
            xl = lead[:, end - int((CLIP + 6) * FS) - N:end]
            trk = state_track(cal, pair.cal_closed, xl)[-spectro(x).shape[0]:]
            row.append(image(nm.z(spectro(x)), trk))
        tiles.append((i, row))
    w = tiles[0][1][0].shape[1]
    h = tiles[0][1][0].shape[0]
    pw = 25 + 4 * w + 40
    sheet = Image.new("RGB", (10 + 2 * pw, 30 + 5 * (h * 2 + 20)), "white")
    dr = ImageDraw.Draw(sheet)
    dr.text((10, 8), "M3 blind test: which clip of each pair is eyes CLOSED?  top strip = calibrated score, rows AF7 AF8 TP9 TP10, 1-40 Hz up, 10 s, wav per clip",
            fill="black")
    for i, row in tiles:
        col, r = (i - 1) // 5, (i - 1) % 5
        x0, y0 = 10 + col * pw, 30 + r * (h * 2 + 20)
        dr.text((x0, y0 + h), f"{i:02d}", fill="black")
        for j, im in enumerate(row):
            big = Image.fromarray(im).resize((w * 2 - 10, h * 2), Image.NEAREST)
            xx = x0 + 25 + j * (w * 2)
            sheet.paste(big, (xx, y0))
            dr.text((xx + 2, y0 + 2), "AB"[j], fill="white")
    sheet.save(os.path.join(out, "sheet.png"))
    with open(os.path.join(out, "key.json"), "w") as f:
        json.dump(key, f, indent=1)
    print(f"wrote {out}/sheet.png, 20 wavs, key.json")


if __name__ == "__main__":
    {"proxy": lambda: proxy(sys.argv[2]), "blind": lambda: blind(sys.argv[2], sys.argv[3])}[sys.argv[1]]()
