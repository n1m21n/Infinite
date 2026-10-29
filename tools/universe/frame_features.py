#!/usr/bin/env python3
"""Frame-by-frame measurement of a sound: what a whole-file summary loses (how each hit starts and dies, how the timbre moves,
whether it is pitched, how regular the hits are). Shared by the generator renders and, when the drive is back, real samples.
    from frame_features import measure; v = measure(L, R, fs)   ->  dict name -> float or list, and vector(d) -> np.array
Frames: Hann 2048 / hop 512 at 48 kHz (scaled with fs), 32 log bands 20 Hz - 20 kHz. Loudness is removed from every timbre number."""
import math
import numpy as np

BANDS, ENV = 32, 40   # bands; onset envelope points of 10 ms

def _bands(fs, fft):
    df = fs / fft; lo, hi = 20.0, min(20000.0, fs / 2); m = np.zeros((BANDS, fft // 2 + 1))
    for b in range(BANDS):
        e0, e1 = lo * (hi / lo) ** (b / BANDS), lo * (hi / lo) ** ((b + 1) / BANDS)
        for k in range(max(0, math.floor(e0 / df - 0.5)), min(fft // 2, math.ceil(e1 / df + 0.5)) + 1):
            ov = min((k + 0.5) * df, e1) - max((k - 0.5) * df, e0)
            if ov > 0: m[b, k] = ov / df
    return m

_cache = {}

def measure(L, R, fs=48000):
    fft = 2048 if fs > 30000 else 1024; hop = fft // 4
    if (fs, fft) not in _cache: _cache[(fs, fft)] = (_bands(fs, fft), 0.5 * (1 - np.cos(2 * np.pi * np.arange(fft) / fft)), np.arange(fft // 2 + 1) * fs / fft)
    BM, win, freqs = _cache[(fs, fft)]
    m = 0.5 * (L + R); n = len(m)
    if n < fft * 2: m = np.pad(m, (0, fft * 2 - n)); n = len(m)
    idx = np.arange(0, n - fft + 1, hop)
    fr = np.lib.stride_tricks.sliding_window_view(m, fft)[idx] * win
    pw = np.abs(np.fft.rfft(fr, axis=1)) ** 2 + 1e-20                       # (frames, bins)
    B = np.log10(pw @ BM.T + 1e-20)                                           # (frames, 32) log band power
    loud = np.log10(pw.sum(1)); act = loud > loud.max() - 4                   # frames within 40 dB of the loudest
    if act.sum() < 2: act[:] = True
    shape = B - np.log10(np.power(10, B).sum(1, keepdims=True))               # loudness removed
    w = np.power(10, loud - loud.max())[:, None] * act[:, None]
    sh_mean = (shape * w).sum(0) / w.sum(); sh_std = np.sqrt(((shape - sh_mean) ** 2 * w).sum(0) / w.sum())
    cen = np.log2((pw * freqs).sum(1) / pw.sum(1) + 1); flat = np.exp(np.log(pw[:, 1:]).mean(1)) / pw[:, 1:].mean(1)
    tf = np.r_[0, np.sqrt(((np.diff(shape, axis=0)) ** 2).sum(1))]           # timbre change per frame
    lm = np.log1p(1000 * np.sqrt(pw)); flux = np.r_[0, np.maximum(0, np.diff(lm, axis=0)).sum(1)] / pw.shape[1]
    rate = fs / hop; reach = max(1, round(0.05 * rate)); fl = max(1e-4, 0.15 * flux.max())
    on = [t for t in range(1, len(flux)) if flux[t] >= fl and flux[t] == flux[max(0, t - reach):t + reach + 1].max()
          and flux[t] >= 1.5 * flux[max(0, t - 4 * reach):t + 4 * reach + 1].mean()]
    on = [t for i, t in enumerate(on) if i == 0 or t - on[i - 1] > reach]
    dur = n / fs
    # 10 ms RMS envelope, and the median envelope after each onset (normalised to its own peak)
    hs = fs // 100; e = np.sqrt((m[: n // hs * hs].reshape(-1, hs) ** 2).mean(1) + 1e-20)
    curves, att, dec, sus = [], [], [], []
    for t in on or [0]:
        s = int(t * hop / hs); seg = e[s: s + ENV + 20]
        if len(seg) < 4: continue
        pk = int(seg[:15].argmax()); c = seg[pk: pk + ENV] / seg[pk]; c = np.pad(c, (0, ENV - len(c)))
        curves.append(np.log10(c + 1e-4)); att.append(pk * 10)
        below = np.nonzero(e[s + pk:] < seg[pk] * 0.1)[0]; dec.append(min(2000, 10 * int(below[0])) if below.size else 2000)
        sus.append(float(e[s + pk + 20] / seg[pk]) if s + pk + 20 < len(e) else 0.0)
    env = np.median(curves, 0) if curves else np.full(ENV, -4.0)
    # pitchedness: normalised autocorrelation peak (lags 30 Hz - 2 kHz) on the loudest frames
    top = np.argsort(loud)[-16:]; hv = []
    for t in top:
        x = fr[t]; ac = np.fft.irfft(np.abs(np.fft.rfft(x, 2 * fft)) ** 2)[:fft]
        if ac[0] <= 0: continue
        lo, hi = int(fs / 2000), min(fft - 1, int(fs / 30)); hv.append(float(ac[lo:hi].max() / ac[0]))
    ioi = np.diff(on) / rate if len(on) > 2 else np.array([dur])
    pk_abs = float(np.abs(np.r_[L, R]).max()); rms = float(np.sqrt((m ** 2).mean()) + 1e-12)
    den = math.sqrt(float((L * L).sum()) * float((R * R).sum())); corr = float((L * R).sum()) / den if den > 0 else 1.0
    tot = np.power(10, sh_mean)
    notes = _notes(pw[act], freqs, rate)
    return {"notes": notes, "shape": sh_mean.tolist(), "shape_sd": sh_std.tolist(), "env": env.tolist(),
            "scalars": [len(on) / dur, float(np.median(att)) if att else 0, float(np.median(dec)) if dec else 0, float(np.median(sus)) if sus else 0,
                        float(act.mean()), float(np.std(loud[act])), float(np.median(hv)) if hv else 0, float(np.mean(cen[act])), float(np.std(cen[act])),
                        float(np.mean(flat[act])), float(np.std(flat[act])), float(np.mean(tf[act])), float(np.mean(flux)), float(np.std(flux)),
                        float(np.std(ioi) / (np.mean(ioi) + 1e-9)), math.log10(pk_abs / rms + 1e-9), corr,
                        float(tot[:11].sum() / tot.sum()), float(tot[11:22].sum() / tot.sum()), float(tot[22:].sum() / tot.sum())]}

_chroma_cache = {}

def _notes(pw, freqs, rate):
    """What separates pad / pluck / lead / synth: how many notes sound at once, how long a note holds, how steady the pitch is.
    12-bin pitch profile per frame from 55 Hz - 4 kHz bins; peaks over 30% of the frame's largest = notes sounding."""
    key = (len(freqs), float(freqs[1]))
    if key not in _chroma_cache:
        ok = (freqs >= 55) & (freqs <= 4000); pc = np.zeros((12, len(freqs)))
        pc[(np.round(12 * np.log2(freqs[ok] / 440.0)).astype(int) % 12), np.flatnonzero(ok)] = 1
        _chroma_cache[key] = pc
    if len(pw) < 2: return [0.0] * 8
    pk = np.zeros_like(pw); pk[:, 1:-1] = (pw[:, 1:-1] > pw[:, :-2]) & (pw[:, 1:-1] >= pw[:, 2:]) & (pw[:, 1:-1] > 1e-3 * pw.max(1, keepdims=True))
    C = (pw * pk) @ _chroma_cache[key].T; C = C / (C.max(1, keepdims=True) + 1e-20)   # spectral peaks only: window leakage is not a note
    if len(C) >= 3: C = (C + np.r_[C[:1], C[:-1]] + np.r_[C[1:], C[-1:]]) / 3   # 3-frame smoothing
    C = C / (C.max(1, keepdims=True) + 1e-20); poly = (C > 0.3).sum(1); top = C.argmax(1)
    runs, r = [], 1
    for i in range(1, len(top)):
        if top[i] == top[i - 1] or C[i, top[i - 1]] > 0.6: r += 1   # a note holds while its pitch stays near the top
        else: runs.append(r); r = 1
    runs.append(r)
    Cn = C / (np.linalg.norm(C, axis=1, keepdims=True) + 1e-12); steady = (Cn[1:] * Cn[:-1]).sum(1)
    p = C.mean(0); p = p / p.sum(); ent = float(-(p * np.log(p + 1e-12)).sum() / np.log(12))
    return [float(np.mean(poly)), float(np.percentile(poly, 90)), float(np.mean(runs) / rate), float(np.percentile(runs, 90) / rate),
            float(np.mean(steady)), float(np.std(steady)), ent, float(len(set(top.tolist())) / 12)]

NOTES = ["poly_mean", "poly_p90", "note_len_s", "note_len_p90_s", "pitch_steady", "pitch_steady_sd", "pitch_entropy", "pitch_classes_used"]

SCALARS = ["onsets_per_s", "attack_ms", "decay_ms", "sustain_200ms", "active_share", "loud_sd", "pitchedness", "centroid", "centroid_sd",
           "flatness", "flatness_sd", "timbre_change", "flux", "flux_sd", "ioi_cv", "crest_log", "corr", "low", "mid", "high"]

def vector(d):
    return np.r_[d["shape"], d["shape_sd"], d["env"], d["scalars"], d.get("notes", [])]
