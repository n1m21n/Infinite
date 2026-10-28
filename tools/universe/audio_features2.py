#!/usr/bin/env python3
"""Audio features v2 (W1 of docs/plans/data-universe/next-phase.md): the fields audio_features.py does not measure.
    audio_features2.py [--src features.jsonl] [--out features2.jsonl] [--seconds 15] [--jobs 2] [--budget 20] [--resume]
                       [--limit N] [--list paths.txt]
One JSON row per file already measured in features.jsonl (same `src`), written to features2.jsonl:
  label     filename / folder tokens: instrument word, note name + midi number, velocity, round robin, loop / one-shot,
            key, bpm (raw tokens kept so a wrong parse can be audited)
  pitch     one-shots: YIN on the voiced part after the attack: pitch_hz, midi (float), pitch_conf (1 - d'), cents off
            the nearest note; label_agree = measured pitch class equals the filename note (octave-free)
  key       pitched files and loops: 12-bin chroma, Krumhansl key and mode, key_conf (best minus second correlation)
  tempo     onset-envelope autocorrelation folded into 70-160 bpm: bpm, tempo_conf, grid_fit (onset share within
            1/8 of a 16th from the grid), pulse (beat-strength: autocorr peak over its mean)
  timbre    13 MFCC means, 13 MFCC delta spreads, rolloff, spectral flux mean, zero-crossing rate, harmonic ratio
            (median-filter split: harmonic energy / (harmonic + percussive))
Statistics only; rows are local-only like features.jsonl. numpy only."""
import argparse, json, math, os, re, sys, time
from concurrent.futures import ProcessPoolExecutor, as_completed
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audio_features as af

FS = af.FS
N, H = 1024, 512
NOTE = re.compile(r"(?<![A-Za-z])([A-G])([#b]?)(-?[0-8])?(?![A-Za-z#])")
PCS = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
VEL = re.compile(r"(?:vel|vl|v)[ _-]?(\d{1,3})(?![0-9])|(?<![a-z])(pp|p|mp|mf|f|ff)(?![a-z])", re.I)
RR = re.compile(r"(?:rr|round[ _-]?robin|rnd)[ _-]?(\d+)", re.I)
MIDIN = re.compile(r"(?<![A-Za-z0-9])(?:note|midi|n)[ _-]?(\d{2,3})(?![0-9])", re.I)
MAJ = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
MIN = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])
NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
_win = np.hanning(N)
_mel = None


def labels(path, root):
    rel = os.path.relpath(path, root)
    stem = os.path.splitext(os.path.basename(path))[0].replace("_", " ")
    t = af.tags(path, root)
    out = {"part": t["part"], "loop": t["loop"], "bpm_name": t["bpm"], "key_name": t["key"]}
    best = None
    for m in NOTE.finditer(stem):
        if m[2] or m[3]:  # a lone capital letter is a word more often than a note
            best = m
    if best:
        pc = (PCS[best[1]] + {"#": 1, "b": -1, "": 0}[best[2]]) % 12
        out["note"] = best[1] + best[2] + (best[3] or "")
        out["note_pc"] = pc
        if best[3] is not None:
            out["note_midi"] = 12 * (int(best[3]) + 1) + pc
    elif not t["loop"]:
        for m in MIDIN.finditer(stem):
            if 24 <= int(m[1]) <= 108:
                out["note_midi"] = int(m[1])
                out["note_pc"] = int(m[1]) % 12
    m = VEL.search(stem)
    if m:
        out["velocity"] = int(m[1]) if m[1] else m[2].lower()
    m = RR.search(stem)
    if m:
        out["round_robin"] = int(m[1])
    out["tokens"] = [w for w in re.split(r"[ \-./]+", rel.replace("_", " ")) if w][-8:]
    return out


def yin(x, fs, fmin=30.0, fmax=2000.0, thr=0.15):
    """(f0, confidence) from one frame, or (None, conf). Difference function by FFT, cumulative mean normalised."""
    w = len(x) // 2
    tmax = min(w - 1, int(fs / fmin))
    tmin = max(2, int(fs / fmax))
    n = 1 << int(math.ceil(math.log2(2 * len(x))))
    acf = np.fft.irfft(np.conj(np.fft.rfft(x[:w], n)) * np.fft.rfft(x, n), n)[:tmax + 1]  # sum_{j<w} x_j x_{j+t}
    e = np.concatenate([[0.0], np.cumsum(x * x)])
    d = np.zeros(tmax + 1)
    for_t = np.arange(1, tmax + 1)
    # d(t) = sum_{j<w} (x_j - x_{j+t})^2; the acf here covers all lags in the frame, use the windowed energy terms
    r0 = e[w]
    rt = e[for_t + w] - e[for_t]
    d[1:] = r0 + rt - 2 * acf[1:]
    cm = np.cumsum(d[1:])
    dp = np.ones(tmax + 1)
    dp[1:] = d[1:] * for_t / np.maximum(cm, 1e-20)
    cand = np.nonzero(dp[tmin:tmax] < thr)[0]
    if cand.size:
        t = int(cand[0]) + tmin
        while t + 1 < tmax and dp[t + 1] < dp[t]:
            t += 1
    else:
        t = int(dp[tmin:tmax].argmin()) + tmin
    if dp[t] > 0.5:
        return None, float(max(0, 1 - dp[t]))
    a, b, c = dp[t - 1], dp[t], dp[min(t + 1, tmax)]
    den = a - 2 * b + c
    conf = float(max(0, 1 - b))
    if den > 1e-12:
        t = t + max(-1.0, min(1.0, 0.5 * (a - c) / den))
    return fs / t, conf


def stft_mag(m):
    n = m.size
    if n < N:
        m = np.pad(m, (0, N - n))
    idx = np.arange(0, m.size - N + 1, H)[:, None] + np.arange(N)[None, :]
    return np.abs(np.fft.rfft(m[idx] * _win, axis=1))  # (frames, 513)


def mel_bank(n_mel=40):
    f = np.fft.rfftfreq(N, 1 / FS)
    mel = lambda hz: 2595 * np.log10(1 + hz / 700)
    pts = 700 * (10 ** (np.linspace(mel(40), mel(min(16000, FS / 2)), n_mel + 2) / 2595) - 1)
    b = np.zeros((n_mel, f.size))
    for i in range(n_mel):
        lo, c, hi = pts[i:i + 3]
        b[i] = np.clip(np.minimum((f - lo) / (c - lo), (hi - f) / (hi - c)), 0, None)
    return b


def mfcc(S):
    global _mel
    if _mel is None:
        _mel = mel_bank()
    lm = np.log(S ** 2 @ _mel.T + 1e-10)
    k = np.arange(13)[:, None]
    dct = np.cos(np.pi * k * (np.arange(lm.shape[1])[None, :] + 0.5) / lm.shape[1])
    c = lm @ dct.T
    d = np.diff(c, axis=0) if c.shape[0] > 1 else np.zeros((1, 13))
    return c.mean(0), d.std(0)


def medfilt(a, w, axis):
    pad = [(0, 0), (0, 0)]
    pad[axis] = (w // 2, w // 2)
    v = np.lib.stride_tricks.sliding_window_view(np.pad(a, pad, mode="edge"), w, axis=axis)
    return np.median(v, axis=-1)


def harmonic_ratio(S):
    if S.shape[0] < 12:
        return None
    S = S[:600]
    h, p = medfilt(S, 9, 0), medfilt(S, 9, 1)
    eh, ep = float((h ** 2).sum()), float((p ** 2).sum())
    return eh / (eh + ep) if eh + ep > 0 else None


def chroma(m):
    """12-bin pitch-class profile from long frames (16384 samples, 2.9 Hz bins): the 1024-sample STFT used for timbre has 47 Hz bins,
    which cannot tell semitones apart below ~1 kHz and put every song in the key of its sub-bass. Spectral peaks only, 110 Hz-2 kHz,
    square-root compression so one loud partial does not decide the key."""
    L = 16384
    if m.size < L:
        m = np.pad(m, (0, L - m.size))
    starts = np.linspace(0, m.size - L, min(48, max(1, m.size // L))).astype(int)
    f = np.fft.rfftfreq(L, 1 / FS)
    ok = (f >= 110) & (f <= 2000)
    win = np.hanning(L)
    acc = np.zeros(12)
    for a in starts:
        mag = np.abs(np.fft.rfft(m[a:a + L] * win))
        pk = (mag[1:-1] > mag[:-2]) & (mag[1:-1] >= mag[2:])
        pk = np.concatenate([[False], pk, [False]]) & ok
        if not pk.any():
            continue
        fr = f[pk]
        w = np.sqrt(mag[pk])
        cents = 1200 * np.log2(fr / 440.0) + 6900
        pc = np.round(cents / 100).astype(int) % 12
        acc += np.bincount(pc, weights=w, minlength=12)
    return acc / (acc.sum() or 1)


def key_of(c):
    best = []
    for mode, prof in (("maj", MAJ), ("min", MIN)):
        for k in range(12):
            r = np.corrcoef(c, np.roll(prof, k))[0, 1] if c.std() > 0 else 0.0
            best.append((r, k, mode))
    best.sort(reverse=True)
    return best[0], best[1][0]


def tempo(S):
    """(bpm, conf, grid_fit, pulse) from the positive spectral flux envelope at hop H"""
    lm = np.log1p(100 * S)
    env = np.maximum(0, np.diff(lm, axis=0)).sum(1)
    if env.size < 64 or env.std() < 1e-9:
        return None
    env = env - env.mean()
    ac = np.fft.irfft(np.abs(np.fft.rfft(env, 2 * env.size)) ** 2)[:env.size]
    fps = FS / H
    lo, hi = int(fps * 60 / 200), min(env.size - 1, int(fps * 60 / 50))
    if hi <= lo + 2 or ac[0] <= 0:
        return None
    seg = ac[lo:hi]
    lag = int(seg.argmax()) + lo
    bpm = 60 * fps / lag
    while bpm < 70:
        bpm *= 2
    while bpm > 160:
        bpm /= 2
    pulse = float(seg.max() / max(1e-12, np.abs(seg).mean()))
    conf = float(ac[lag] / ac[0])
    step = fps * 60 / bpm / 4  # frames per 16th
    pk = np.nonzero((env[1:-1] > env[:-2]) & (env[1:-1] >= env[2:]) & (env[1:-1] > 0.5 * env.std()))[0] + 1
    fit = None
    if pk.size >= 4:
        best = 0.0
        for ph in np.linspace(0, step, 16, endpoint=False):
            r = ((pk - ph) / step) % 1.0
            best = max(best, float((np.minimum(r, 1 - r) < 0.125).mean()))
        fit = best
    return bpm, conf, fit, pulse


def analyse(path, root, seconds):
    x = af.decode(path, seconds)
    if x is None or x.shape[1] < 2048:
        return None
    m = x.mean(0)
    lab = labels(path, root)
    row = {"src": path, **lab}
    S = stft_mag(m)
    mf, md = mfcc(S)
    f = np.fft.rfftfreq(N, 1 / FS)
    pw = (S ** 2).mean(0)
    cum = np.cumsum(pw)
    zc = float((np.diff(np.signbit(m).astype(np.int8)) != 0).mean())
    lm = np.log1p(100 * S)
    flux = float(np.maximum(0, np.diff(lm, axis=0)).sum(1).mean() / S.shape[1]) if S.shape[0] > 1 else 0.0
    row.update(mfcc=[round(float(v), 2) for v in mf], mfcc_d=[round(float(v), 2) for v in md],
               rolloff=round(float(f[np.searchsorted(cum, 0.85 * cum[-1])])), flux=round(flux, 4), zcr=round(zc, 4))
    hr = harmonic_ratio(S)
    row["harm"] = round(hr, 3) if hr is not None else None
    one_shot = not lab["loop"] or m.size < 1.5 * FS
    if one_shot:
        e = np.sqrt((m[:m.size // 480 * 480].reshape(-1, 480) ** 2).mean(1) + 1e-20)
        pk = int(e.argmax())
        a = (pk + 3) * 480  # 30 ms past the peak: past the click, still voiced
        seg = m[a:a + 4096]
        if seg.size < 2048:
            seg = m[max(0, pk * 480 - 2048):][:4096]
        if seg.size >= 2048 and float((seg ** 2).mean()) > 1e-8:
            f0, conf = yin(seg, FS)
            row["pitch_conf"] = round(conf, 3)
            if f0:
                mid = 69 + 12 * math.log2(f0 / 440.0)
                row.update(pitch_hz=round(f0, 2), midi=round(mid, 2), cents=round(100 * (mid - round(mid)), 1))
                if "note_pc" in lab:
                    row["label_agree"] = int(round(mid)) % 12 == lab["note_pc"]
                if "note_midi" in lab:
                    row["label_oct_agree"] = int(round(mid)) == lab["note_midi"]
    if not one_shot or row.get("pitch_hz"):
        c = chroma(m)
        (r, k, mode), second = key_of(c)
        row.update(chroma=[round(float(v), 3) for v in c], key=NAMES[k] + ("m" if mode == "min" else ""),
                   key_conf=round(float(r - second), 3))
    if not one_shot:
        t = tempo(S)
        if t:
            row.update(bpm=round(t[0], 1), tempo_conf=round(t[1], 3), grid_fit=None if t[2] is None else round(t[2], 3),
                       pulse=round(t[3], 2))
    return row


def _one(a):
    try:
        return analyse(*a)
    except Exception as e:
        return {"src": a[1], "error": str(e)[:120]}


def sources(src, roots):
    for ln in open(os.path.expanduser(src)):
        if not ln.endswith("\n"):
            continue
        j = json.loads(ln)
        if "bands" in j:  # measured rows only; dup_of / error rows have nothing to read
            yield next((r for r in roots if j["src"].startswith(r)), roots[0]), j["src"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("roots", nargs="*", default=["/Volumes/Wd-Black"])
    ap.add_argument("--src", default="~/Infinite/training-data/audio/features.jsonl")
    ap.add_argument("--out", default="~/Infinite/training-data/audio/features2.jsonl")
    ap.add_argument("--seconds", type=float, default=15)
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--budget", type=float, default=20, help="minutes")
    ap.add_argument("--limit", type=int, default=0, help="stop after N files (tests)")
    ap.add_argument("--resume", action="store_true")
    a = ap.parse_args()
    out = os.path.expanduser(a.out)
    done = set()
    if a.resume and os.path.exists(out):
        for ln in open(out):
            if ln.endswith("\n"):
                done.add(json.loads(ln)["src"])
    t0, n, bad = time.time(), 0, 0
    todo = ((r, p, a.seconds) for r, p in sources(a.src, a.roots) if p not in done)
    with open(out, "a" if a.resume else "w") as fo, ProcessPoolExecutor(a.jobs) as ex:
        pending = set()
        for job in todo:
            pending.add(ex.submit(_one, job))
            if len(pending) >= a.jobs * 8:
                for fut in as_completed(pending):
                    pending.discard(fut)
                    r = fut.result()
                    if r:
                        fo.write(json.dumps(r, separators=(",", ":")) + "\n")
                        n += 1
                        bad += "error" in r
                        if n % 1000 == 0:
                            print("%d files, %.1f min" % (n, (time.time() - t0) / 60), flush=True)
                    break
            if time.time() - t0 > a.budget * 60 or (a.limit and n + len(pending) >= a.limit):
                print("stopping: run again with --resume", flush=True)
                break
        for fut in as_completed(pending):
            r = fut.result()
            if r:
                fo.write(json.dumps(r, separators=(",", ":")) + "\n")
                n += 1
                bad += "error" in r
    print("wrote %d rows (%d errors) to %s in %.1f min" % (n, bad, out, (time.time() - t0) / 60))


if __name__ == "__main__":
    main()
