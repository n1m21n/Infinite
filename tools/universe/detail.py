#!/usr/bin/env python3
"""G1 + G2 of EXECUTION.md: the fine measurements of one note window, from raw audio. No labels, no models: rules only.

    detail.py test            one synthetic check per measurement (bell inharmonic, square odd-only, 808 glide, reverb tail ...)

measure(seg, rate, midi, hold) takes ONE note window (mono samples, the note starts at 0 and is released at `hold` seconds) and gives

  scalars (axis columns, G2)   f0 (Hz, YIN), f0_hs (Hz, harmonic sum; the better pitch, pitch_check.py), tuning (cents against the played note), inharmonicity (cents), oddeven (dB odd - even
                               power of the harmonic partials), partial_roughness (Sethares on the real partials), partial_decay
                               (dB/s, median of the 8 strongest), f1 / f2 (Hz, axis 21), glide (semitones/s, axis 22), space (dB,
                               axis 20), vibrato_rate / vibrato_depth (Hz, cents), tremolo_rate / tremolo_depth (Hz, dB),
                               noise_share (dB of the power left when the 64 partial peaks are masked)
  partials (G1)                top 64 spectral peaks, by frequency: [frequency Hz, level dB re the strongest, decay dB/s]
  noise (G1)                   residual noise (peaks masked) in 32 log bands x 6 slices of 0.5 s, dB, uint8 at 0.5 dB, 0 = -120 dB
  tail (G1)                    loudness after the release, 20 ms steps for 1.5 s, dB re the held level
  mel128 (G1)                  128 mel bands 20 Hz - 20 kHz at 10 ms, uint8 0.5 dB steps, zlib + base64

Everything is deterministic: the same samples give the same numbers (checked by `own_sweeps.py check`)."""
import base64, math, os, sys, zlib
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
np.seterr(divide="ignore", invalid="ignore")

NPART = 64
SLICE_S = 0.5
B32 = 20.0 * (1000.0 ** (np.arange(33) / 32))          # the 32 log bands of audio_features, 20 Hz - 20 kHz
MEL_N, MEL_HOP, MEL_NFFT = 128, 0.010, 2048


def _strided(x, n, hop):
    return np.lib.stride_tricks.as_strided(x, ((len(x) - n) // hop + 1, n), (x.strides[0] * hop, x.strides[0]))


# ---- mel 128 at 10 ms -----------------------------------------------------------------------------------------------------

_mel_cache = {}


def mel_bank(rate, nfft=MEL_NFFT, n=MEL_N):
    k = (rate, nfft, n)
    if k not in _mel_cache:
        hz = lambda m: 700 * (10 ** (m / 2595) - 1)
        mel = lambda f: 2595 * math.log10(1 + f / 700)
        e = np.array([hz(m) for m in np.linspace(mel(20), mel(min(20000, rate / 2)), n + 2)])
        f = np.fft.rfftfreq(nfft, 1 / rate)
        lo, mid, hi = e[:-2, None], e[1:-1, None], e[2:, None]
        _mel_cache[k] = np.maximum(0, np.minimum((f - lo) / (mid - lo), (hi - f) / (hi - mid))) * (2 / (hi - lo))
    return _mel_cache[k]


def mel128(x, rate):
    """(128 x frames) uint8 log-mel at 10 ms: Hann 2048, hop 480 at 48 kHz; 0.5 dB steps, 0 = -120 dB (relative level)."""
    hop = int(round(MEL_HOP * rate)); w = np.hanning(MEL_NFFT)
    fr = _strided(np.ascontiguousarray(x), MEL_NFFT, hop) * w
    p = np.abs(np.fft.rfft(fr, axis=1)) ** 2 / (w.sum() ** 2 / 4)
    m = mel_bank(rate) @ p.T
    return np.clip(np.round((10 * np.log10(np.maximum(m, 1e-12)) + 120) * 2), 0, 255).astype(np.uint8)


def pack_u8(q):
    return base64.b64encode(zlib.compress(np.ascontiguousarray(q).tobytes(), 6)).decode()


def unpack_u8(s, shape):
    return np.frombuffer(zlib.decompress(base64.b64decode(s)), np.uint8).reshape(shape)


# ---- pitch (YIN) ----------------------------------------------------------------------------------------------------------

def yin(x, rate, t0, t1, fmin=30.0, fmax=1500.0, thr=0.15, hop_s=0.01, W=2048):
    """YIN (de Cheveigne and Kawahara 2002) on x between t0 and t1 seconds. Returns (times, f0 Hz, nan where unvoiced)."""
    tmin, tmax = int(rate / fmax), int(rate / fmin)
    hop = int(round(hop_s * rate)); a = int(t0 * rate); L = W + tmax
    starts = np.arange(a, min(int(t1 * rate), len(x)) - L, hop)
    if starts.size == 0:
        return np.array([]), np.array([])
    F = x[starts[:, None] + np.arange(L)[None, :]].astype(np.float64)
    nf = 1 << (L + W).bit_length()
    X1 = np.fft.rfft(F[:, :W], nf, axis=1); X2 = np.fft.rfft(F, nf, axis=1)
    c = np.fft.irfft(np.conj(X1) * X2, nf, axis=1)[:, :tmax + 1]           # sum_j x_j x_{j+tau}, j < W
    e = np.cumsum(F ** 2, axis=1); e = np.concatenate([np.zeros((len(F), 1)), e], 1)
    e0 = e[:, W][:, None]; et = e[:, W:W + tmax + 1] - e[:, 0:tmax + 1]
    d = e0 + et - 2 * c; d[:, 0] = 0
    cm = np.ones_like(d); cs = np.cumsum(d[:, 1:], axis=1)
    cm[:, 1:] = d[:, 1:] * np.arange(1, tmax + 1)[None, :] / np.maximum(cs, 1e-30)
    f0 = np.full(len(F), np.nan)
    for i in range(len(F)):
        if e0[i, 0] < 1e-12:
            continue
        ci = cm[i]; tau = -1
        below = np.nonzero(ci[tmin:] < thr)[0]
        if below.size:
            tau = tmin + below[0]
            while tau + 1 <= tmax and ci[tau + 1] < ci[tau]:
                tau += 1
        else:
            g = tmin + int(np.argmin(ci[tmin:]))
            tau = g if ci[g] < 0.3 else -1
        if tau <= 0 or tau >= tmax:
            continue
        a_, b_, c_ = ci[tau - 1], ci[tau], ci[tau + 1]                      # parabolic refinement
        den = a_ - 2 * b_ + c_
        f0[i] = rate / (tau + (0.5 * (a_ - c_) / den if abs(den) > 1e-12 else 0.0))
    return (starts + L / 2) / rate, f0


def hsum(x, rate, t0, t1, fmin=27.0, fmax=2100.0, H=12, w=0.84, tol_c=30.0):
    """Subharmonic summation (Hermes 1988) on x between t0 and t1 seconds: the average amplitude spectrum moved onto a 10-cent
    log grid (max over +-tol_c cents, so stretched piano partials still count), then score(f) = sum_h w^(h-1) A(h f) over the
    candidates fmin..fmax. A sub-octave only collects every other harmonic at a weight w lower, so the true f wins where YIN can
    lock onto a partial. Returns (f0 Hz, salience 0..1 = best score / score with every harmonic at full height), nan if silent."""
    a, b = int(t0 * rate), min(int(t1 * rate), len(x))
    seg = np.ascontiguousarray(x[a:b], np.float64); n = 16384
    if len(seg) < n:
        seg = np.concatenate([seg, np.zeros(n - len(seg))])
    P = (np.abs(np.fft.rfft(_strided(seg, n, n // 2) * np.hanning(n), axis=1)) ** 2).mean(0)
    if P.max() <= 1e-18:
        return np.nan, 0.0
    A = np.sqrt(P / P.max()); fr = np.fft.rfftfreq(n, 1 / rate)
    top = min(8000.0, 0.45 * rate); g = fmin * 2 ** (np.arange(int(1200 * np.log2(top / fmin) / 10)) / 120)
    lo = np.searchsorted(fr, g * 2 ** (-tol_c / 1200)); hi = np.searchsorted(fr, g * 2 ** (tol_c / 1200))
    L = np.interp(g, fr, A)
    for i in np.nonzero(hi > lo)[0]:
        L[i] = max(L[i], A[lo[i]:hi[i]].max())
    nc = int(1200 * np.log2(fmax / fmin) / 10) + 1; S = np.zeros(nc); full = sum(w ** h for h in range(H))
    for h in range(1, H + 1):
        k = int(round(120 * np.log2(h)))
        m = min(nc, len(L) - k)
        if m > 0:
            S[:m] += w ** (h - 1) * L[k:k + m]
    i = int(np.argmax(S))
    if 0 < i < nc - 1:
        den = S[i - 1] - 2 * S[i] + S[i + 1]; off = 0.5 * (S[i - 1] - S[i + 1]) / den if den < -1e-12 else 0.0
    else:
        off = 0.0
    return float(fmin * 2 ** ((i + off) / 120)), float(S[i] / full)


# ---- partials -------------------------------------------------------------------------------------------------------------

def sethares(f, a):
    f, a = np.asarray(f, np.float64), np.asarray(a, np.float64)
    if f.size < 2:
        return 0.0
    i, j = np.triu_indices(f.size, 1)
    fmin, df = np.minimum(f[i], f[j]), np.abs(f[i] - f[j])
    s = 0.24 / (0.021 * fmin + 19)
    return float((np.minimum(a[i], a[j]) * (np.exp(-3.5 * s * df) - np.exp(-5.75 * s * df))).sum())


def partials(x, rate, hold, k=NPART):
    """Top-k spectral peaks of the held part: (freq Hz, level dB re strongest, decay dB/s), sorted by frequency, plus the sustained
    spectrum for the noise estimate. Decay per partial = slope of its level over the 8192-sample frames of the held part."""
    a, b = int(0.05 * rate), int(hold * rate)
    seg = x[a:b]
    if len(seg) < 16384:
        return np.zeros((0, 3)), None
    n = 16384; w = np.hanning(n)
    P = (np.abs(np.fft.rfft(_strided(np.ascontiguousarray(seg), n, n // 2) * w, axis=1)) ** 2).mean(0)
    fr = np.fft.rfftfreq(n, 1 / rate)
    if P.max() <= 1e-18:
        return np.zeros((0, 3)), P
    ok = (P[1:-1] > P[:-2]) & (P[1:-1] >= P[2:]) & (fr[1:-1] > 20) & (P[1:-1] > P.max() * 1e-9)
    idx = np.nonzero(ok)[0] + 1
    idx = idx[np.argsort(P[idx])[::-1][:k]]
    la, lb, lc = np.log(P[idx - 1] + 1e-30), np.log(P[idx] + 1e-30), np.log(P[idx + 1] + 1e-30)
    off = 0.5 * (la - lc) / np.minimum(la - 2 * lb + lc, -1e-9)             # parabolic peak position (bins)
    f = (idx + np.clip(off, -0.5, 0.5)) * rate / n
    lev = 10 * np.log10(P[idx] / P.max())
    n2 = 8192; w2 = np.hanning(n2); S = np.abs(np.fft.rfft(_strided(np.ascontiguousarray(x[:b]), n2, 2048) * w2, axis=1))
    t = np.arange(len(S)) * 2048 / rate; bw = rate / n2
    dec = np.full(len(f), np.nan)
    for i, fi in enumerate(f):
        c = int(round(fi / bw)); m = 20 * np.log10(np.maximum(S[:, max(c - 1, 0):c + 2].max(1), 1e-12))
        pk = int(np.argmax(m)); seg_m, seg_t = m[pk:], t[pk:]
        good = seg_m > m[pk] - 40
        if good.sum() >= 3:
            dec[i] = np.polyfit(seg_t[good], seg_m[good], 1)[0]
    o = np.argsort(f)
    return np.stack([f[o], lev[o], dec[o]], 1), P


def harmonic_stats(part, f0):
    """inharmonicity (cents, level-weighted over the 16 strongest peaks), odd / even balance (dB) of the peaks that sit on a harmonic."""
    out = {"inharmonicity": np.nan, "oddeven": np.nan}
    if not part.shape[0] or not (f0 and np.isfinite(f0)):
        return out
    strong = part[np.argsort(part[:, 1])[::-1][:16]]
    wgt = 10 ** (strong[:, 1] / 10)
    def dev(f):                                                   # level-weighted distance of the peaks from the series f, 2f, 3f ...
        n = np.maximum(np.round(strong[:, 0] / f), 1)
        return float((np.abs(1200 * np.log2(strong[:, 0] / (n * f))) * wgt).sum() / wgt.sum())
    best = min((dev(c), c) for c in (f0, f0 / 2, f0 * 2, strong[0, 0]))   # the best-fitting series: a bell is inharmonic under every f0
    out["inharmonicity"] = best[0]
    f0 = best[1] if best[1] != f0 and best[0] < 0.5 * dev(f0) else f0
    on = part[(np.abs(1200 * np.log2(part[:, 0] / (np.maximum(np.round(part[:, 0] / f0), 1) * f0))) < 50) & (part[:, 0] > 0.7 * f0)]
    if on.shape[0]:
        h = np.maximum(np.round(on[:, 0] / f0), 1).astype(int); pw = 10 ** (on[:, 1] / 10)
        od, ev = pw[h % 2 == 1].sum(), pw[h % 2 == 0].sum()
        out["oddeven"] = float(np.clip(10 * np.log10(max(od, 1e-12) / max(ev, 1e-12)), -40, 40))
    return out


# ---- noise, tail, space ---------------------------------------------------------------------------------------------------

def noise_env(x, rate, part, slices=6):
    """Residual spectrum with the partial peaks masked (+-4 bins), 32 log bands, per 0.5 s slice. dB, uint8 at 0.5 dB, 0 = -120 dB."""
    n = 4096; w = np.hanning(n); fr = np.fft.rfftfreq(n, 1 / rate); bw = rate / n
    mask = np.ones(len(fr), bool)
    for f in part[:, 0] if part.shape[0] else []:
        c = int(round(f / bw)); mask[max(c - 4, 0):c + 5] = False
    band = np.digitize(fr, B32) - 1
    out = np.zeros((32, slices), np.uint8); tot = []
    for s in range(slices):
        a, b = int(s * SLICE_S * rate), int((s + 1) * SLICE_S * rate)
        if b - a < n:
            continue
        P = (np.abs(np.fft.rfft(_strided(np.ascontiguousarray(x[a:b]), n, n // 2) * w, axis=1)) ** 2).mean(0) / (w.sum() ** 2 / 4)
        for j in range(32):
            sel = (band == j) & mask
            v = P[sel].mean() if sel.any() else 0.0
            out[j, s] = int(np.clip(round((10 * np.log10(max(v, 1e-12)) + 120) * 2), 0, 255))
        tot.append((P[mask].sum(), P.sum()))
    return out, tot


def tail_curve(x, rate, hold, steps=75, step_s=0.02):
    """Loudness after the release: 20 ms RMS steps for 1.5 s, dB re the level of the held part (its last 0.5 s)."""
    h = int(step_s * rate)
    def rms(a):
        s = x[a:a + h]; return math.sqrt(float((s ** 2).mean())) if len(s) == h else float("nan")
    r0 = int(hold * rate); held = [rms(r0 - int(0.5 * rate) + i * h) for i in range(int(0.5 / step_s))]
    held = np.nanmean(held) if np.isfinite(held).any() else float("nan")
    t = np.array([rms(r0 + i * h) for i in range(steps)])
    return 20 * np.log10(np.maximum(t, 1e-9) / max(held, 1e-9)), held


# ---- formants, modulation -------------------------------------------------------------------------------------------------

def formants(x, rate, hold, f0=120.0):
    """Axis 21: the first two peaks (150 Hz - 4 kHz) of a cepstrally smoothed spectral envelope, median over 100 ms frames of the held part."""
    n = 4096; w = np.hanning(n); a, b = int(0.1 * rate), int(hold * rate)
    if b - a < n:
        return np.nan, np.nan
    fr = np.fft.rfftfreq(n, 1 / rate); sel = (fr >= 150) & (fr <= 4000); bw_ = rate / n
    l1, l2 = [], []
    lift = int(min(0.0035, 0.45 / max(f0, 50.0)) * rate)                  # finer than the pitch period, so the harmonics are smoothed away
    for s in range(a, b - n, int(0.1 * rate)):
        spec = np.abs(np.fft.rfft(x[s:s + n] * w)) + 1e-9
        if spec.max() < 1e-7:
            continue
        cep = np.fft.irfft(np.log(spec)); tap = np.zeros(n); tap[:lift] = 0.5 * (1 + np.cos(np.pi * np.arange(lift) / lift))
        tap[n - lift + 1:] = tap[1:lift][::-1]; cep = cep * tap
        env = np.fft.rfft(cep).real[sel]
        near = int(150 / bw_)                                                       # a formant must stand 1 dB above the envelope 150 Hz away
        pk = np.nonzero((env[1:-1] > env[:-2]) & (env[1:-1] >= env[2:]))[0] + 1
        pk = np.array([p for p in pk if env[p] - max(env[max(p - near, 0)], env[min(p + near, len(env) - 1)]) > 0.01])
        pk = pk[env[pk] > env.max() - 1.4] if pk.size else pk                       # within about 12 dB of the strongest
        if pk.size >= 2:
            l1.append(fr[sel][pk[0]]); l2.append(fr[sel][pk[1]])
        elif pk.size == 1:
            l1.append(fr[sel][pk[0]])
    return (float(np.median(l1)) if l1 else np.nan), (float(np.median(l2)) if len(l2) >= max(1, len(l1) // 2) else np.nan)


def _mod_peak(v, step_s, lo=3.0, hi=9.0):
    """Rate (Hz) and amplitude of the strongest 3-9 Hz component of a detrended curve v sampled every step_s seconds."""
    v = np.asarray(v, np.float64)
    if v.size < 60:
        return np.nan, np.nan
    v = v - np.polyval(np.polyfit(np.arange(v.size), v, 1), np.arange(v.size))
    w = np.hanning(v.size); n = 1 << (4 * v.size).bit_length()
    S = np.abs(np.fft.rfft(v * w, n)) * 2 / w.sum(); f = np.fft.rfftfreq(n, step_s)
    m = (f >= lo) & (f <= hi)
    i = int(np.argmax(S[m])); return float(f[m][i]), float(S[m][i])


def modulation(x, rate, hold, tt, f0s, midi):
    """Pitch contour from YIN -> tuning, glide (axis 22), vibrato; amplitude contour -> tremolo."""
    out = dict.fromkeys(("f0", "tuning", "glide", "vibrato_rate", "vibrato_depth", "tremolo_rate", "tremolo_depth"), np.nan)
    v = np.isfinite(f0s)
    if v.sum() >= 5:
        st = 69 + 12 * np.log2(f0s[v] / 440)
        out["f0"] = float(np.median(f0s[v]))
        out["tuning"] = float((np.median(st) - midi) * 100)
        # octave errors would read as an enormous glide: drop frames more than 18 semitones from the median
        keep = np.abs(st - np.median(st)) < 18
        if keep.sum() >= 5:
            tk, sk = tt[v][keep], st[keep]; best = 0.0                  # steepest slope over any 0.3 s: a pitch envelope, not the note's mean
            for a0 in np.arange(tk[0], max(tk[-1] - 0.3, tk[0]) + 1e-9, 0.05):
                m = (tk >= a0) & (tk <= a0 + 0.3)
                if m.sum() >= 5:
                    sl = float(np.polyfit(tk[m], sk[m], 1)[0])
                    if abs(sl) > abs(best):
                        best = sl
            out["glide"] = best
        full = np.isfinite(f0s[(tt > 0.15) & (tt < hold)]) if ((tt > 0.15) & (tt < hold)).any() else np.array([])
        if full.size >= 60 and full.all():
            c = 100 * (69 + 12 * np.log2(f0s[(tt > 0.15) & (tt < hold)] / 440))
            r, d = _mod_peak(c, 0.01)
            out["vibrato_depth"] = d; out["vibrato_rate"] = r if d >= 2 else np.nan
    h = int(0.01 * rate); a, b = int(0.05 * rate), int(hold * rate)
    e = np.sqrt(np.convolve(x[a:b] ** 2, np.ones(h) / h, "valid")[::h])
    if e.size >= 60 and e.min() > 1e-6:
        db = 20 * np.log10(e); db = db - np.convolve(db, np.ones(41) / 41, "same")     # remove the slow envelope (0.4 s)
        r, d = _mod_peak(db[20:-20], 0.01)
        out["tremolo_depth"] = d; out["tremolo_rate"] = r if d >= 0.3 else np.nan
    return out


# ---- the one entry point --------------------------------------------------------------------------------------------------

def measure(seg, rate, midi, hold=1.5, heavy=True):
    """seg: mono samples of one window (note on at 0, released at `hold`). Returns a JSON-able dict (see the module doc)."""
    x = np.ascontiguousarray(np.asarray(seg, np.float64))
    if x.size < int(2 * rate) or not np.isfinite(x).all() or np.abs(x).max() < 1e-5:
        return None
    tt, f0s = yin(x, rate, 0.02, hold)
    d = modulation(x, rate, hold, tt, f0s, midi)
    d["f0_hs"] = hsum(x, rate, 0.02, hold)[0]                              # harmonic sum: right more often than YIN per family (pitch_check.py)
    part, _ = partials(x, rate, hold)
    f0 = d["f0"] if np.isfinite(d["f0"]) else 440 * 2 ** ((midi - 69) / 12)
    d.update(harmonic_stats(part, f0))
    if part.shape[0]:
        lin = 10 ** (part[:24, 1] / 20) if part.shape[0] <= 24 else 10 ** (part[np.argsort(part[:, 1])[::-1][:24], 1] / 20)
        fs = part[:, 0] if part.shape[0] <= 24 else part[np.argsort(part[:, 1])[::-1][:24], 0]
        d["partial_roughness"] = sethares(fs, lin)
        top = part[np.argsort(part[:, 1])[::-1][:8], 2]
        d["partial_decay"] = float(np.nanmedian(top)) if np.isfinite(top).any() else np.nan
    d["f1"], d["f2"] = formants(x, rate, hold, f0)
    tail, held = tail_curve(x, rate, hold)
    tail = np.maximum(tail, -90.0)
    d["space"] = float(np.mean(tail[10:35])) if np.isfinite(tail[10:35]).all() else np.nan
    noise, tot = noise_env(x, rate, part)
    if tot:
        pm, pt = tot[min(1, len(tot) - 1)]
        d["noise_share"] = float(10 * np.log10(max(pm, 1e-18) / max(pt, 1e-18)))
    out = {k: (None if not np.isfinite(v) else float("%.5g" % v)) for k, v in d.items()}
    out["partials"] = [[round(float(f), 2), round(float(l), 2), None if not np.isfinite(dd) else round(float(dd), 2)] for f, l, dd in part]
    out["noise"] = pack_u8(noise)
    out["tail"] = [int(round(float(t) * 2)) for t in np.clip(tail, -90, 20)]
    if heavy:
        out["mel128"] = pack_u8(mel128(x, rate))
    return out


# ---- tests ----------------------------------------------------------------------------------------------------------------

def _resonator(x, rate, f, bw):
    r = math.exp(-math.pi * bw / rate); c1 = -2 * r * math.cos(2 * math.pi * f / rate); c2 = r * r
    y = np.zeros_like(x); y1 = y2 = 0.0
    for i, v in enumerate(x):
        yi = v - c1 * y1 - c2 * y2; y[i] = yi; y2 = y1; y1 = yi
    return y


def run_checks():
    rate, T, hold = 48000, 3.0, 1.5; t = np.arange(int(rate * T)) / rate
    gate = (t < hold).astype(float); env = np.minimum(t / 0.005, 1) * gate
    f0 = 220.0; midi = 57
    def harm(ns, amp=lambda n: 1 / n, f=f0):
        return sum(amp(n) * np.sin(2 * np.pi * f * n * t) for n in ns)
    R = {}
    R["saw"] = measure(harm(range(1, 40)) * env, rate, midi)
    R["square"] = measure(harm(range(1, 40, 2)) * env, rate, midi)
    bell = sum(a * np.sin(2 * np.pi * f0 * r * t) * np.exp(-t * dcy) for r, a, dcy in ((1, 1, 1.0), (2.76, 0.8, 2.0), (5.4, 0.6, 3.5), (8.93, 0.4, 5.0)))
    R["bell"] = measure(bell * np.minimum(t / 0.003, 1), rate, midi)
    fall = 12 * np.log2(1) + 24 * np.exp(-t / 0.15)                                  # 808: 2 octaves down over the first 0.4 s
    ph = 2 * np.pi * np.cumsum(55 * 2 ** (fall / 12)) / rate
    R["808"] = measure(np.sin(ph) * env, rate, 33)
    sine = np.sin(2 * np.pi * f0 * t) * env
    R["sine"] = measure(sine, rate, midi)
    tail = np.sin(2 * np.pi * f0 * t) * np.where(t < hold, 1.0, np.exp(-(t - hold) / 0.9))
    R["reverb"] = measure(tail * np.minimum(t / 0.005, 1), rate, midi)
    R["flat"] = measure(sine, rate, midi)
    vib = np.sin(2 * np.pi * np.cumsum(f0 * 2 ** (0.2 * np.sin(2 * np.pi * 5.5 * t) / 12)) / rate) * env
    R["vibrato"] = measure(vib, rate, midi)
    trem = np.sin(2 * np.pi * f0 * t) * (1 + 0.25 * np.sin(2 * np.pi * 6.0 * t)) * env
    R["tremolo"] = measure(trem, rate, midi)
    flat_det = np.sin(2 * np.pi * f0 * 2 ** (30 / 1200) * t) * env
    R["sharp30"] = measure(flat_det, rate, midi)
    imp = np.zeros_like(t); imp[::int(rate / 120)] = 1.0
    vow = _resonator(imp[:int(rate * 1.8)], rate, 700, 90) + _resonator(imp[:int(rate * 1.8)], rate, 1200, 110) * 0.8
    vow = np.concatenate([vow, np.zeros(len(t) - len(vow))]) * env
    R["vowel"] = measure(vow / np.abs(vow).max(), rate, 47)
    rng = np.random.default_rng(1)
    R["noise"] = measure(rng.standard_normal(t.size) * 0.1 * env, rate, midi)
    R["m2"] = measure((np.sin(2 * np.pi * 220 * t) + np.sin(2 * np.pi * 233.08 * t)) * env, rate, midi)
    R["p5"] = measure((np.sin(2 * np.pi * 220 * t) + np.sin(2 * np.pi * 330 * t)) * env, rate, midi)
    g = lambda k, a: R[k][a]
    checks = [("inharmonicity: bell >> saw", g("bell", "inharmonicity") > 15 and g("saw", "inharmonicity") < 2),
              ("odd/even: square odd-only (> 20 dB), saw about 5 dB", g("square", "oddeven") > 20 and 1 < g("saw", "oddeven") < 12),
              ("glide: 808 falls (< -10 st/s), flat sine about 0", g("808", "glide") < -10 and abs(g("sine", "glide")) < 2),
              ("space: decaying tail > held-note-then-silence by 10 dB", g("reverb", "space") > g("flat", "space") + 10),
              ("tuning: +30 cents read within 6", abs(g("sharp30", "tuning") - 30) < 6 and abs(g("sine", "tuning")) < 6),
              ("vibrato: 5.5 Hz, 20 cents read within 1 Hz and 8 cents", g("vibrato", "vibrato_rate") is not None and abs(g("vibrato", "vibrato_rate") - 5.5) < 1
               and abs(g("vibrato", "vibrato_depth") - 20) < 8 and (g("sine", "vibrato_rate") is None)),
              ("tremolo: 6 Hz, 25% (about 2.2 dB) read within 1 Hz, 0.8 dB", g("tremolo", "tremolo_rate") is not None and abs(g("tremolo", "tremolo_rate") - 6) < 1
               and abs(g("tremolo", "tremolo_depth") - 2.2) < 0.8),
              ("formants: 700 / 1200 Hz read within 150 / 250 Hz", g("vowel", "f1") is not None and abs(g("vowel", "f1") - 700) < 150
               and g("vowel", "f2") is not None and abs(g("vowel", "f2") - 1200) < 250),
              ("harmonic-sum f0: saw and square at 220 Hz within 1%", abs(g("saw", "f0_hs") - 220) < 2.2 and abs(g("square", "f0_hs") - 220) < 2.2),
              ("partials: saw gives 39 peaks, first at 220 Hz", len(R["saw"]["partials"]) >= 30 and abs(R["saw"]["partials"][0][0] - 220) < 3),
              ("partial decay: bell partials fall faster than the sustained sine", g("bell", "partial_decay") < g("sine", "partial_decay") - 3),
              ("noise share: noise >> sine (> 20 dB more)", g("noise", "noise_share") > g("sine", "noise_share") + 20),
              ("roughness of partials: minor second > fifth", (g("m2", "partial_roughness") or 0) > 3 * (g("p5", "partial_roughness") or 0))]
    again = measure(harm(range(1, 40)) * env, rate, midi)
    checks.append(("repeat: identical numbers", again == R["saw"]))
    ok = True
    for name, good in checks:
        print("%s  %s" % ("PASS" if good else "FAIL", name)); ok &= bool(good)
    for k in ("saw", "square", "bell", "808", "vibrato", "tremolo", "vowel", "reverb", "flat", "sharp30"):
        print(k, {a: R[k][a] for a in ("f0", "tuning", "inharmonicity", "oddeven", "glide", "space", "f1", "f2", "vibrato_rate", "vibrato_depth",
                                         "tremolo_rate", "tremolo_depth", "partial_decay")})
    return ok


def cmd_test():
    raise SystemExit(0 if run_checks() else 1)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "test":
        cmd_test()
    print(__doc__)
