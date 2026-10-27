#!/usr/bin/env python3
"""Measure real audio (sample packs, loops, one-shots) with the app's own AudioSummary maths, so the spectral gate,
the critic and the generator's knob targets can be fitted on real sounds as well as on our renders.
    audio_features.py [ROOT ...] [--out ~/Infinite/training-data/audio/features.jsonl] [--per-folder 40]
                      [--seconds 30] [--jobs 3] [--budget 20] [--resume]
One JSON row per file:
  tags      part (kick, snare, hat, bass, pad, lead, keys, vocal, fx ...) and loop / one-shot from the path,
            bpm and key from the name, the pack (two folders under the root)
  summary   the app's numbers (src/core/AudioSummary.cpp): 32 log bands 20 Hz - 20 kHz from Hann 4096 / hop 1024
            frames, peak, rms, silence ratio (100 ms blocks under -60 dBFS), stereo correlation, spectral-flux onsets
  timbre    spectral centroid, roll-off (85%), flatness, low / mid / high share (the gate's 11 / 11 / 10 bands)
  envelope  one-shots: attack (to the peak), decay (peak to -20 dB), the 10 ms RMS curve's first 40 points
  groove    loops with a bpm: each onset's 16th-step and its offset from the grid (in steps), its strength;
            swing = mean offset of the off-beat 16ths, accent = strength per 16th step
Statistics only: no audio leaves the drive. Sample-pack licences cover making music, not redistribution, so every
row is local-only (license "sample pack, local only"; the owner's own folders are "owner", bought records
"commercial recording, local only"). Personal media folders (NOT_MUSIC) are skipped."""
import argparse, json, math, os, re, struct, subprocess, sys, time
from concurrent.futures import ProcessPoolExecutor, as_completed
import numpy as np

FS = 48000
FFT, HOP = 4096, 1024
BANDS = 32
AUDIO = {".wav", ".aif", ".aiff", ".flac", ".ogg", ".mp3", ".m4a", ".caf"}
SKIP = {".Spotlight-V100", ".Trashes", ".fseventsd", ".TemporaryItems", ".DocumentRevisions-V100"}
# personal media and film folders on the drive: not music material
NOT_MUSIC = {"Pictures", "blender", "Design", "Books", "spark ar", "library.fcpbundle", "Final Cut Original Media",
             "High Life (2018) [BluRay] [1080p] [YTS.AM]", "Silver Linings Playbook (2012) [1080p]", "PIONEER"}
# first match wins, so the specific words come before the general ones
PARTS = [("kick", r"kick|bassdrum|bass drum|\bbd\b|\bkck"), ("snare", r"snare|\bsd\b|\bsnr"), ("clap", r"clap"),
         ("hat", r"hat|\bhh\b|\bohh?\b|\bchh?\b"), ("cymbal", r"cymbal|crash|ride|splash"), ("tom", r"\btoms?\b"),
         ("perc", r"perc|shaker|conga|bongo|tamb|rim|cowbell|clave|djembe"), ("drums", r"drum|beat|break|top loop|groove"),
         ("bass", r"bass|sub\b|808|reese"), ("pad", r"pad|atmos|drone|texture|ambien"), ("lead", r"lead|arp|seq"),
         ("pluck", r"pluck|stab"), ("keys", r"piano|keys|rhodes|organ|wurl|epiano|\bep\b"), ("chord", r"chord"),
         ("vocal", r"vox|vocal|voice|choir|chant"), ("guitar", r"guitar|gtr"), ("strings", r"string|violin|cello|viola"),
         ("brass", r"brass|horn|trumpet|sax"), ("fx", r"\bfx\b|sfx|riser|uplift|downlift|sweep|impact|noise|foley|whoosh"),
         ("synth", r"synth")]
PARTS = [(p, re.compile(rx, re.I)) for p, rx in PARTS]
BPM = re.compile(r"(?<![0-9])([6-9][0-9]|1[0-9][0-9])[ _-]?bpm|bpm[ _-]?([6-9][0-9]|1[0-9][0-9])(?![0-9])", re.I)
KEY = re.compile(r"(?<![a-z])([a-g](?:#|b)?)[ _-]?(maj|min|m|major|minor)?(?![a-z0-9#])", re.I)

_win = 0.5 * (1 - np.cos(2 * np.pi * np.arange(FFT) / FFT))
_scale = 2.0 / (FFT * float((_win ** 2).sum()))
_df = FS / FFT
_freqs = np.arange(FFT // 2 + 1) * _df


def _band_matrix():
    """bins -> 32 bands with the same fractional bin overlap as AudioSummary::Finish"""
    lo, hi = 20.0, min(20000.0, FS / 2)
    m = np.zeros((BANDS, FFT // 2 + 1))
    for b in range(BANDS):
        e0, e1 = lo * (hi / lo) ** (b / BANDS), lo * (hi / lo) ** ((b + 1) / BANDS)
        for k in range(max(0, math.floor(e0 / _df - 0.5)), min(FFT // 2, math.ceil(e1 / _df + 0.5)) + 1):
            ov = min((k + 0.5) * _df, e1) - max((k - 0.5) * _df, e0)
            if ov > 0:
                m[b, k] = ov / _df
    return m


_BM = _band_matrix()


def read_pcm(path, seconds):
    """WAV / AIFF PCM or float read in Python: (channels x samples float64, rate), or None for anything else.
    Skips the two ffmpeg/ffprobe processes, which took about 75% of each file's time"""
    with open(path, "rb") as f:
        head = f.read(12)
        if head[:4] == b"RIFF" and head[8:12] == b"WAVE":
            big, fmt = False, None
        elif head[:4] == b"FORM" and head[8:12] in (b"AIFF", b"AIFC"):
            big, fmt = True, None
        else:
            return None
        e = ">" if big else "<"
        while True:
            ch = f.read(8)
            if len(ch) < 8:
                return None
            cid, size = ch[:4], struct.unpack(e + "I", ch[4:])[0]
            if not big and cid == b"fmt ":
                b = f.read(size)
                tag, nch, rate = struct.unpack("<HHI", b[:8])
                bits = struct.unpack("<H", b[14:16])[0]
                if tag == 0xFFFE and len(b) >= 26:
                    tag = struct.unpack("<H", b[24:26])[0]
                if tag not in (1, 3):
                    return None
                fmt = (nch, rate, bits, tag == 3)
            elif big and cid == b"COMM":
                b = f.read(size)
                nch, bits = struct.unpack(">h", b[:2])[0], struct.unpack(">h", b[6:8])[0]
                ex, hi, lo = struct.unpack(">HII", b[8:18])  # 80-bit extended rate
                rate = int(round((hi * 2.0 ** 32 + lo) * 2.0 ** ((ex & 0x7FFF) - 16383 - 63)))
                comp = b[18:22] if head[8:12] == b"AIFC" else b"NONE"
                if comp == b"sowt":
                    e = "<"
                elif comp in (b"fl32", b"FL32"):
                    bits = -32
                elif comp != b"NONE":
                    return None
                fmt = (nch, rate, abs(bits), bits < 0)
            elif cid in (b"data", b"SSND") and fmt:
                nch, rate, bits, flt = fmt
                if cid == b"SSND":
                    off = struct.unpack(">I", f.read(8)[:4])[0]
                    f.read(off)
                    size -= 8 + off
                w = bits // 8
                if nch < 1 or w not in (1, 2, 3, 4, 8) or rate < 8000:
                    return None
                n = min(size // (w * nch), int(seconds * rate))
                raw = f.read(n * w * nch)
                n = len(raw) // (w * nch)
                raw = raw[:n * w * nch]
                if flt:
                    x = np.frombuffer(raw, dtype=e + ("f4" if w == 4 else "f8")).astype(np.float64)
                elif w == 3:
                    u = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
                    v = (u[:, 2] << 16 | u[:, 1] << 8 | u[:, 0]) if e == "<" else (u[:, 0] << 16 | u[:, 1] << 8 | u[:, 2])
                    x = ((v ^ 0x800000) - 0x800000) / 8388608.0
                elif w == 1:
                    x = (np.frombuffer(raw, dtype=np.uint8 if not big else np.int8).astype(np.float64) - (128 if not big else 0)) / 128.0
                else:
                    x = np.frombuffer(raw, dtype=e + "i%d" % w).astype(np.float64) / float(2 ** (bits - 1))
                return x.reshape(-1, nch).T, rate
            else:
                f.seek(size + (size & 1), 1)


def resample(x, rate):
    """band-limited FFT resample to FS; zero padding keeps the end from wrapping into the attack"""
    if rate == FS:
        return x
    n = x.shape[1]
    pad = 1 << int(math.ceil(math.log2(n + 8192)))
    m = int(round(pad * FS / rate))
    X = np.fft.rfft(x, pad)
    k = min(X.shape[1], m // 2 + 1)
    Y = np.zeros((x.shape[0], m // 2 + 1), dtype=complex)
    Y[:, :k] = X[:, :k]
    return np.fft.irfft(Y, m)[:, :int(round(n * FS / rate))] * (m / pad)


def decode(path, seconds):
    """stereo float64 at 48 kHz, at most `seconds` long; WAV / AIFF in Python, the rest via ffmpeg; mono is copied to
    both sides at full level (ffmpeg's own -ac 2 upmix lowers it 3 dB)"""
    try:
        r = read_pcm(path, seconds) if path.lower().endswith((".wav", ".aif", ".aiff")) else None
    except (OSError, struct.error, ValueError):
        r = None
    if r is not None and r[0].shape[1] >= 2:
        x = resample(r[0][:2], r[1])
        return np.stack([x[0], x[0]]) if x.shape[0] == 1 else x
    ch = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries", "stream=channels", "-of", "csv=p=0",
                         path], capture_output=True, text=True, timeout=30).stdout.strip()
    mono = ch == "1"
    p = subprocess.run(["ffmpeg", "-v", "error", "-nostdin", "-i", path, "-t", str(seconds), "-f", "f32le", "-ac", "1" if mono else "2",
                        "-ar", str(FS), "-"], capture_output=True, timeout=60)
    x = np.frombuffer(p.stdout, dtype=np.float32).astype(np.float64)
    if x.size < 2:
        return None
    return np.stack([x, x]) if mono else x[:x.size // 2 * 2].reshape(-1, 2).T


def tags(path, root):
    rel = os.path.relpath(path, root)
    stem = os.path.splitext(os.path.basename(path))[0]
    words = rel.replace("_", " ")
    part = next((p for p, rx in PARTS if rx.search(stem.replace("_", " "))), None) or \
        next((p for p, rx in PARTS if rx.search(words)), "unknown")
    m = BPM.search(words)
    bpm = int(m[1] or m[2]) if m else None
    k = None
    for mm in KEY.finditer(stem.replace("_", " ")):
        if mm[2] or mm[1][1:] in ("#", "b"):  # a lone letter is too often a word; need an accidental or maj / min
            k = mm[1].capitalize() + ("m" if (mm[2] or "").lower() in ("m", "min", "minor") else "")
    loop = bool(bpm) or bool(re.search(r"loop|groove|break|beat\b|top\b", words, re.I))
    return {"part": part, "bpm": bpm, "key": k, "loop": loop, "pack": os.sep.join(rel.split(os.sep)[:2])}


def analyse(path, root, seconds):
    x = decode(path, seconds)
    if x is None or x.shape[1] < 2048:
        return None
    L, R = x
    n = x.shape[1]
    peak = float(np.abs(x).max())
    rms = float(np.sqrt((x ** 2).mean()))
    blk = FS // 10
    nb = max(1, n // blk)
    sub = np.maximum((L[:nb * blk].reshape(nb, -1) ** 2).mean(1), (R[:nb * blk].reshape(nb, -1) ** 2).mean(1)) if n >= blk \
        else np.array([max((L ** 2).mean(), (R ** 2).mean())])
    silence = float((sub < 1e-6).mean())
    den = math.sqrt(float((L * L).sum()) * float((R * R).sum()))
    corr = float((L * R).sum()) / den if den > 0 else float("nan")
    # spectrum frames as the app takes them: a full ring of FFT samples, then one frame every HOP
    pad = x if n >= FFT else np.pad(x, ((0, 0), (0, FFT - n)))
    starts = np.arange(0, pad.shape[1] - FFT + 1, HOP)
    fr = np.stack([pad[:, s:s + FFT] for s in starts], 1) * _win  # (2, frames, FFT)
    spec = np.fft.rfft(fr, axis=2)
    pw = (0.5 * (spec.real ** 2 + spec.imag ** 2)).sum(0) * _scale  # (frames, bins)
    bands = (_BM @ pw.mean(0))
    lm = np.log1p(1000 * np.sqrt(pw))
    flux = np.concatenate([[lm[0].sum()], np.maximum(0, np.diff(lm, axis=0)).sum(1)]) / pw.shape[1]
    onsets = onset_frames(flux)
    tot = float(bands.sum()) or 1e-30
    mean_pw = pw.mean(0)
    s = float(mean_pw.sum()) or 1e-30
    cum = np.cumsum(mean_pw)
    lin = np.sqrt(mean_pw[1:] + 1e-20)
    row = {"src": path, "seconds": round(n / FS, 2), **tags(path, root),
           "bands": [float("%.4g" % b) for b in bands], "peak": round(peak, 4), "rms": float("%.4g" % rms),
           "silence": round(silence, 3), "corr": round(corr, 3) if corr == corr else None, "onsets": len(onsets),
           "crest": round(peak / max(1e-12, rms), 2),
           "low": round(float(bands[:11].sum()) / tot, 4), "mid": round(float(bands[11:22].sum()) / tot, 4),
           "high": round(float(bands[22:].sum()) / tot, 4),
           "centroid": round(float((_freqs * mean_pw).sum()) / s), "rolloff": round(float(_freqs[np.searchsorted(cum, 0.85 * s)])),
           "flatness": round(float(np.exp(np.log(lin).mean()) / lin.mean()), 4),
           "license": licence(path, root)}
    if not row["loop"] or row["seconds"] < 1.5:  # one-shot: the envelope
        e = np.sqrt(np.maximum((L ** 2), (R ** 2))[:n // 480 * 480].reshape(-1, 480).mean(1) + 1e-20)  # 10 ms RMS
        if e.size:
            pk = int(e.argmax())
            after = np.nonzero(e[pk:] < e[pk] * 0.1)[0]
            row.update(attack_ms=10 * pk, decay_ms=10 * int(after[0]) if after.size else None,
                       env=[round(float(v / e[pk]), 3) for v in e[:40]])
    if row["bpm"] and row["loop"] and len(onsets) >= 4:
        row["groove"] = groove(refine(onsets, L, R), flux[onsets], row["bpm"])
    return row


def onset_frames(f):
    """AudioSummary's onset rule: flux peaks over 10% of the biggest, local max in +-60 ms, 1.5x the +-200 ms mean"""
    peak = float(f.max()) if f.size else 0.0
    floor = max(1e-4, 0.1 * peak)
    rate = FS / HOP
    reach, span = max(1, round(0.06 * rate)), max(2, round(0.2 * rate))
    out, last = [], -10 ** 6
    for t in range(len(f)):
        if f[t] < floor:
            continue
        a, b = max(0, t - reach), min(len(f) - 1, t + reach)
        w = f[a:b + 1]
        if w.max() > f[t] or (np.nonzero(w == f[t])[0][0] + a) < t:
            continue
        if f[t] < 1.5 * f[max(0, t - span):t + span + 1].mean() or t - last <= reach:
            continue
        out.append(t)
        last = t
    return out


def refine(onsets, L, R):
    """each flux onset (21 ms frames) to the sample of the steepest 1 ms rise near it, in seconds: a 16th at 128 bpm
    is 117 ms, so frame times alone would blur swing. A frame sees a hit hardest when it reaches the window's middle."""
    env = np.maximum(np.abs(L), np.abs(R))
    env = env[:env.size // 48 * 48].reshape(-1, 48).max(1)  # 1 ms peaks
    out = []
    for t in onsets:
        c = (t * HOP + FFT // 2) // 48
        a, b = max(1, c - 2 * HOP // 48), min(env.size - 1, c + 2 * HOP // 48)
        if b <= a:
            out.append(c / 1000.0)
            continue
        d = np.diff(env[a - 1:b + 1])
        out.append((a + int(d.argmax())) / 1000.0)
    return out


def groove(times, strength, bpm):
    """hits on the 16th grid of the name's bpm: step, offset from the grid (steps), strength (of the loudest hit).
    The grid's phase is the one that puts the quarter-note hits closest to their beats."""
    step = 60.0 / bpm / 4
    q = [t / step for t in times]
    best = None
    for ph in np.linspace(0, 1, 40, endpoint=False):
        on = [abs((x - ph) / 4 - round((x - ph) / 4)) for x in q]  # distance to the nearest beat, in beats
        sc = sorted(on)[:max(1, len(on) // 3)]
        v = sum(sc) / len(sc)
        if best is None or v < best[0]:
            best = (v, ph)
    ph = best[1]
    k0 = math.floor(q[0] - ph + 0.5)
    hits = []
    top = float(max(strength)) or 1
    for x, s in zip(q, strength):
        k = round(x - ph)
        hits.append((k - k0, round(float(x - ph - k), 3), round(float(s) / top, 3)))
    off = [h[1] for h in hits if h[0] % 2 == 1]
    acc = [0.0] * 16
    cnt = [0] * 16
    for k, _, s in hits:
        acc[k % 16] += s
        cnt[k % 16] += 1
    bars = max(1, (hits[-1][0] + 16) // 16)
    return {"hits": hits[:128], "swing": round(sum(off) / len(off), 3) if off else None,
            "accent": [round(acc[i] / bars, 3) for i in range(16)], "tight": round(sum(abs(h[1]) for h in hits) / len(hits), 3)}


def files(roots, per_folder):
    for root in roots:
        for d, subs, fs in os.walk(root):
            subs[:] = sorted(s for s in subs if s not in SKIP and not s.startswith(".") and not (d == root and s in NOT_MUSIC))
            au = sorted(f for f in fs if os.path.splitext(f)[1].lower() in AUDIO and not f.startswith("._"))
            if per_folder and len(au) > per_folder:  # spread the pick over the folder, not its first names
                au = [au[i * len(au) // per_folder] for i in range(per_folder)]
            for f in au:
                yield root, os.path.join(d, f)


def licence(path, root):
    top = os.path.relpath(path, root).split(os.sep)[0]
    if top in ("my music", "Discography", "random unreleased"):
        return "owner"
    if top in ("Music Downloads", "VA - 80s House Classics (VEXDIGI034) WEB"):
        return "commercial recording, local only"
    return "sample pack, local only"


def _one(args):
    root, path, seconds = args
    try:
        return analyse(path, root, seconds)
    except Exception as e:
        return {"src": path, "error": str(e)[:120]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("roots", nargs="*", default=["/Volumes/Wd-Black"])
    ap.add_argument("--out", default="~/Infinite/training-data/audio/features.jsonl")
    ap.add_argument("--per-folder", type=int, default=40, help="0 = every file")
    ap.add_argument("--seconds", type=float, default=30)
    ap.add_argument("--jobs", type=int, default=3)
    ap.add_argument("--budget", type=float, default=20, help="minutes")
    ap.add_argument("--resume", action="store_true")
    a = ap.parse_args()
    out = os.path.expanduser(a.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    done = set()
    if a.resume and os.path.exists(out):
        for ln in open(out):
            if ln.endswith("\n"):
                done.add(json.loads(ln)["src"])
    t0, n, bad = time.time(), 0, 0
    todo = ((r, p, a.seconds) for r, p in files(a.roots, a.per_folder) if p not in done)
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
            if time.time() - t0 > a.budget * 60:
                print("budget hit: run again with --resume", flush=True)
                break
        for fut in as_completed(pending):
            r = fut.result()
            if r:
                fo.write(json.dumps(r, separators=(",", ":")) + "\n")
                n += 1
                bad += "error" in r
    print("wrote %d rows (%d unreadable) to %s in %.1f min" % (n, bad, out, (time.time() - t0) / 60))


if __name__ == "__main__":
    main()
