#!/usr/bin/env python3
"""Sound judge data (internal/plans/generate-samples/judge.md): 3 s clips -> gain-normalised log-mel + DSP technical targets.
    judge_data.py lib  [--per 1500] [--other 3000] [--jobs 3]   library family samples (string_families FAM + WIND) and a random
                                                                 'other' set, first 3 s from the onset         -> judge/lib.npz
    judge_data.py own  --clips <dir> --node Piano [--tag x]      sweep windows kept with INF_KEEP_WIN            -> judge/own_<node><tag>.npz
npz: mel uint8 (n, 128, T) in 0.5 dB steps under the clip's loudest bin (0 = -127.5 dB), targets float32 (n, len(TGT)),
fam (family name or 'other' or 'own'), loop (bool), src. Gain is normalised away, so a level change alone cannot move the judge.
Targets are measured, never hand-labelled; each is one technical head of the judge:
    transient  attack_ms (log10, 10%->90% of the 5 ms envelope peak), onset (peak over mean flux, dB, 0-40)
    decay      t60 (log10 s, line fit of the envelope dB after the peak), hf_decay (high minus low band dB slope per s)
    spectral   slope (dB per octave, 100 Hz-12 kHz), centroid (log2 Hz), hf (2.5-6 kHz dB under the loudest band)
    harmonic   hnr (dB, normalised autocorrelation peak), flat (spectral flatness, log10)
    pitch      f0 (log2 Hz from the autocorrelation), f0_wobble (cents, robust sd over 50 ms frames, octave slips folded)
    space      width (side/mid dB), late (dB of the last 1 s against the first 1 s)
    pressure   crest (dB), arc (dB range of the 50 ms loudness, p95-p5)
Exit 0."""
import argparse, json, multiprocessing as mp, os, random, re, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import audio_features as af  # noqa: E402
import string_families as sf  # noqa: E402

FS, SEC, NFFT, HOP, BINS = 48000, 3.0, 2048, 768, 128
N = int(FS * SEC); T = 1 + (N - NFFT) // HOP
OUT = os.path.expanduser("~/Infinite/training-data/judge")
TGT = ["attack_ms", "onset", "t60", "hf_decay", "slope", "centroid", "hf", "hnr", "flat", "f0", "f0_wobble", "width", "late", "crest", "arc"]
HEAD = {"transient": ["attack_ms", "onset"], "decay": ["t60", "hf_decay"], "spectral": ["slope", "centroid", "hf"],
        "harmonic": ["hnr", "flat"], "pitch": ["f0", "f0_wobble"], "space": ["width", "late"], "pressure": ["crest", "arc"]}
FAMS = {**sf.FAM, **sf.WIND}
_fb = None
_fq = np.fft.rfftfreq(NFFT, 1 / FS)
_win = np.hanning(NFFT)


def melbank(lo=30.0, hi=20000.0):
    """BINS triangular bands, HTK mel spacing lo-hi, area-normalised."""
    mel = lambda f: 2595 * np.log10(1 + f / 700); inv = lambda m: 700 * (10 ** (m / 2595) - 1)
    e = inv(np.linspace(mel(lo), mel(hi), BINS + 2)); fb = np.zeros((BINS, _fq.size))
    for i in range(BINS):
        a, c, b = e[i], e[i + 1], e[i + 2]
        fb[i] = np.clip(np.minimum((_fq - a) / (c - a), (b - _fq) / (b - c)), 0, None)
        fb[i] /= fb[i].sum() + 1e-12
    return fb


def onset_cut(x):
    """x (2, n): start 20 ms before the first 5 ms block over 10% of the loudest, then exactly SEC long (zero padded)."""
    m = np.abs(x).max(0); b = m[:m.size // 240 * 240].reshape(-1, 240).max(1)
    if not b.size or b.max() < 1e-5:
        return None
    i = max(0, int(np.argmax(b > 0.1 * b.max())) * 240 - 960)
    y = x[:, i:i + N]
    return np.pad(y, ((0, 0), (0, N - y.shape[1]))) if y.shape[1] < N else y


def frames(m):
    fr = np.lib.stride_tricks.as_strided(m, (T, NFFT), (m.strides[0] * HOP, m.strides[0])) * _win
    return np.abs(np.fft.rfft(fr, axis=1)) ** 2


def features(x):
    """x (2, N) float64 -> (mel uint8 (BINS, T), targets float32) or None for silence."""
    global _fb
    if _fb is None:
        _fb = melbank()
    m = np.ascontiguousarray(x.mean(0))
    pk = np.abs(m).max()
    if pk < 1e-5 or not np.isfinite(m).all():
        return None
    P = frames(m)
    mel = 10 * np.log10(_fb @ P.T + 1e-20); mel -= mel.max()
    mel_u8 = np.clip(np.round((mel + 127.5) * 2), 0, 255).astype(np.uint8)
    t = {}
    e = np.sqrt((m[:N // 240 * 240].reshape(-1, 240) ** 2).mean(1) + 1e-20)  # 5 ms rms
    ip = int(e.argmax()); a10 = np.argmax(e[:ip + 1] > 0.1 * e[ip]); a90 = np.argmax(e[:ip + 1] > 0.9 * e[ip])
    t["attack_ms"] = np.log10(max(1.0, 5.0 * (a90 - a10)))
    lb = np.log10(P + 1e-20); fl = np.maximum(np.diff(lb, axis=0), 0).sum(1)
    t["onset"] = float(np.clip(10 * np.log10(fl.max() / (fl.mean() + 1e-3)), 0, 40))
    ed = 20 * np.log10(e[ip:] / e[ip]); tt = np.arange(ed.size) * 0.005; live = ed > -60
    sl = np.polyfit(tt[live], ed[live], 1)[0] if live.sum() > 10 else -60.0
    t["t60"] = np.log10(np.clip(-60.0 / min(sl, -1e-3), 0.02, 60.0))
    lo, hi = P[:, (_fq > 100) & (_fq < 800)].sum(1), P[:, (_fq > 3000) & (_fq < 12000)].sum(1)
    k0 = min(max(0, ip * 240 // HOP), T - 8); ft = np.arange(T - k0) * HOP / FS
    def dslope(b):
        d = 10 * np.log10(b[k0:] + 1e-20); ok = d > d.max() - 60
        return np.polyfit(ft[ok], d[ok], 1)[0] if ok.sum() > 5 else -60.0
    t["hf_decay"] = float(np.clip(dslope(hi) - dslope(lo), -200, 200))
    S = P.mean(0); sel = (_fq > 100) & (_fq < 12000)
    t["slope"] = np.polyfit(np.log2(_fq[sel]), 10 * np.log10(S[sel] + 1e-20), 1)[0]
    t["centroid"] = np.log2((S * _fq).sum() / S.sum() + 1)
    bands = np.array([S[(_fq >= a) & (_fq < b)].sum() for a, b in zip(np.geomspace(30, 20000, 33)[:-1], np.geomspace(30, 20000, 33)[1:])])
    bd = 10 * np.log10(bands + 1e-20)
    t["hf"] = max(-90.0, 10 * np.log10(S[(_fq >= 2500) & (_fq < 6000)].sum() / (bands.max() + 1e-20) + 1e-20))
    t["flat"] = np.log10(np.exp(np.log(S[1:] + 1e-20).mean()) / S[1:].mean() + 1e-12)
    # pitch and harmonicity: normalised autocorrelation of 50 ms frames in the loud part, lags 40 Hz-2 kHz
    L = 2400; lag0, lag1 = FS // 2000, FS // 40
    f0s, hs = [], []
    for s in range(ip * 240, min(N - L - lag1, ip * 240 + int(1.5 * FS)), L):
        fr = m[s:s + L + lag1]; base = fr[:L]
        if np.sqrt((base ** 2).mean()) < 0.03 * pk:
            continue
        sp = np.fft.rfft(fr, 2 * (L + lag1)); ac = np.fft.irfft(sp * np.conj(np.fft.rfft(base, 2 * (L + lag1))))[:lag1 + 1]
        en = np.sqrt((base ** 2).sum() * np.convolve(fr ** 2, np.ones(L), "valid")[:lag1 + 1]) + 1e-20
        r = ac / en; k = lag0 + int(np.argmax(r[lag0:lag1]))
        f0s.append(FS / k); hs.append(np.clip(r[k], 1e-4, 0.9999))
    if f0s:
        t["f0"] = np.log2(np.median(f0s))
        c = 1200 * np.log2(np.array(f0s) / np.median(f0s)); c -= 1200 * np.round(c / 1200)  # octave slips are not wobble
        t["f0_wobble"] = float(1.4826 * np.median(np.abs(c - np.median(c)))) if len(f0s) > 2 else 0.0
        h = float(np.median(hs)); t["hnr"] = 10 * np.log10(h / (1 - h))
    else:
        t["f0"], t["f0_wobble"], t["hnr"] = np.log2(100.0), 0.0, -10.0
    t["f0_wobble"] = np.log10(1 + min(t["f0_wobble"], 600))
    sd, md = x[0] - x[1], x[0] + x[1]
    t["width"] = 10 * np.log10(((sd ** 2).mean() + 1e-12) / ((md ** 2).mean() + 1e-12))
    t["width"] = max(t["width"], -60.0)
    t["late"] = max(-90.0, 10 * np.log10((m[-FS:] ** 2).mean() / ((m[:FS] ** 2).mean() + 1e-20) + 1e-12))
    t["crest"] = 20 * np.log10(pk / (np.sqrt((m ** 2).mean()) + 1e-12))
    e50 = 20 * np.log10(np.sqrt((m[:N // 2400 * 2400].reshape(-1, 2400) ** 2).mean(1) + 1e-12))
    t["arc"] = float(np.clip(np.percentile(e50, 95) - np.percentile(e50, 5), 0, 120))
    return mel_u8, np.array([t[k] for k in TGT], np.float32)


def lib_job(it):
    src, fam, loop = it
    try:
        x = af.decode(src, SEC + 2)
    except Exception:
        return None
    if x is None:
        return None
    x = onset_cut(x)
    try:
        f = features(x) if x is not None else None
    except (ValueError, np.linalg.LinAlgError, FloatingPointError):
        f = None
    return None if f is None else (f[0], f[1], fam, loop, src)


def family(name):
    b = os.path.basename(name).lower()
    return next((f for f, p in FAMS.items() if re.search(p, b)), None)


def save(path, res, extra=None):
    res = [r for r in res if r is not None]
    np.savez_compressed(path, mel=np.stack([r[0] for r in res]), targets=np.stack([r[1] for r in res]),
                        fam=np.array([r[2] for r in res]), loop=np.array([r[3] for r in res]), src=np.array([r[4] for r in res]),
                        **(extra or {}))
    print("wrote %s (%d clips)" % (path, len(res)))


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("cmd", choices=["lib", "own"])
    ap.add_argument("--per", type=int, default=1500); ap.add_argument("--other", type=int, default=3000)
    ap.add_argument("--jobs", type=int, default=3); ap.add_argument("--clips"); ap.add_argument("--node"); ap.add_argument("--tag", default="")
    a = ap.parse_args(); os.makedirs(OUT, exist_ok=True)
    if a.cmd == "lib":
        by, other = {}, []
        for ln in open(os.path.join(af.OUT_DIR if hasattr(af, "OUT_DIR") else os.path.expanduser("~/Infinite/training-data/audio"), "features.jsonl")):
            r = json.loads(ln); f = family(r["src"])
            (by.setdefault(f, []) if f else other).append((r["src"], f or "other", bool(r.get("loop"))))
        rnd = random.Random(0); items = []
        for f, v in sorted(by.items()):
            rnd.shuffle(v); items += v[:a.per]
        rnd.shuffle(other); items += other[:a.other]
        print("%d clips to decode" % len(items), flush=True)
        with mp.Pool(a.jobs) as pool:
            res = []
            for i, r in enumerate(pool.imap(lib_job, items, chunksize=8)):
                res.append(r)
                if i % 1000 == 0:
                    print("  %d / %d" % (i, len(items)), flush=True)
        save(os.path.join(OUT, "lib.npz"), res)
    else:
        d = os.path.join(a.clips, a.node); res = []
        for fn in sorted(os.listdir(d)):
            if not fn.endswith(".wav"):
                continue
            r = af.read_pcm(os.path.join(d, fn), 10)
            if r is None:
                continue
            x = r[0] if r[0].shape[0] == 2 else np.vstack([r[0][0], r[0][0]])
            x = onset_cut(x); f = features(x) if x is not None else None
            if f is not None:
                res.append((f[0], f[1], "own", False, fn[:-4]))
        save(os.path.join(OUT, "own_%s%s.npz" % (a.node, a.tag)), res)
    return 0


if __name__ == "__main__":
    sys.exit(main())
