#!/usr/bin/env python3
"""P2 of the sound-model handoff: measure the characteristic set (docs/plans/data-universe/CHARACTERISTICS.md) of any sound or phrase.

    characteristics.py rows FILE.jsonl [--out F] [--limit N]     audio rows: mel rows give curves, 32-band rows give one frame
    characteristics.py midi ROOT [--out F] [--per-folder 20] [--budget 600]
    characteristics.py audio FILE.wav ...                         print the summary of audio files
    characteristics.py test                                       rule checks on synthetic sounds, cross-form agreement, repeat
    characteristics.py coverage OUT.jsonl ...                     which axis each output file gives, and for how many rows

Every spectral axis goes through one form, (band centre Hz, band power, band width Hz) x frames, so a mel row, a 32-band row and a wav
are measured by the same functions. A row out: {src, kind, frame_s, axes: {axis: {mean, std, slope}}, curves: {axis: [...]}}; slope is
per second, and one-number axes (attack, decay) carry only mean. Nothing is random: the same input gives the same numbers, and every
run re-measures its first 50 inputs and stops if any number differs."""
import argparse, collections, glob, json, math, os, sys, time, zlib
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sound_data as sd  # noqa: E402
np.seterr(divide="ignore", invalid="ignore")  # silent frames give NaN by design; they are dropped from the summaries

AXES = ["loudness", "brightness", "sharpness", "warmth", "weight", "fullness", "noisiness", "roughness", "attack", "decay",
        "movement", "width", "pitch", "spread", "tension", "harmonic_rhythm", "density", "contour"]
# G2 columns (detail.py): one number per note window; the three planned axes 20-22 plus the finer sound data
DETAIL = ["space", "f1", "f2", "glide", "inharmonicity", "oddeven", "partial_roughness", "partial_decay", "vibrato_rate", "vibrato_depth",
          "tremolo_rate", "tremolo_depth", "tuning", "noise_share", "f0"]
AXES += DETAIL
AUDIO_DT, MIDI_DT, MIDI_MAX_S = 0.0625, 0.25, 120.0
SILENT_DB = 60.0  # frames this far under the peak are dropped from the spectral axes


# ---- band forms ---------------------------------------------------------------------------------------------------------

def _mel_edges(rate=48000, n=64):
    hz = lambda m: 700 * (10 ** (m / 2595) - 1)
    mel = lambda f: 2595 * math.log10(1 + f / 700)
    return np.array([hz(m) for m in np.linspace(mel(20), mel(min(16000, rate / 2)), n + 2)])


_E = _mel_edges()
MEL_FC, MEL_FW = _E[1:-1], (_E[2:] - _E[:-2]) / 2   # area-normalised triangles: band power = mean density x half the base
_B = 20.0 * (1000.0 ** (np.arange(33) / 32))         # audio_features: 32 log bands 20 Hz - 20 kHz
B32_FC, B32_FW = np.sqrt(_B[:-1] * _B[1:]), np.diff(_B)


def bark(f):
    return 13 * np.arctan(0.00076 * f) + 3.5 * np.arctan((f / 7500) ** 2)


def spectral(P, fc, fw):
    """P: (bands, frames) power. Curves of loudness and the spectral axes 2-7, over 20 Hz - 16 kHz (the stored mel range) for every
    form. Bands more than 60 dB under the frame's loudest band count as silent (a stand-in for the hearing threshold), so each form's
    own noise floor (mel rows store -120 dB as 0) does not enter sharpness or flatness."""
    keep = fc <= 16500; P, fc, fw = np.asarray(P, np.float64)[keep], fc[keep], fw[keep]
    P = np.maximum(P, 1e-20); tot = P.sum(0); L = 10 * np.log10(tot)
    L = np.maximum(L, L.max() - SILENT_DB)  # silence reads as 60 dB under the peak, not as each form's own floor
    P = np.where(P > P.max(0) * 10 ** (-SILENT_DB / 10), P, 1e-20); tot = P.sum(0)
    live = L > L.max() - SILENT_DB
    lf = np.log2(fc)[:, None]; w = P / tot
    c = (w * lf).sum(0)
    z = bark(fc); zw = np.maximum(np.diff(bark(np.concatenate([[fc[0] - fw[0] / 2], fc + fw / 2]))), 1e-3)
    N = np.where(P > 1e-19, (P / zw[:, None]) ** 0.23 * zw[:, None], 0)
    g = np.where(z < 15.8, 1.0, 0.066 * np.exp(0.171 * z))[:, None]
    dens = np.maximum(P / fw[:, None], tot / fw.sum() * 10 ** (-SILENT_DB / 10))
    out = {"loudness": L,
           "brightness": c,
           "sharpness": 0.11 * (N * g * z[:, None]).sum(0) / N.sum(0),
           "warmth": 10 * np.log10(np.maximum(w[(fc >= 120) & (fc < 500)].sum(0), 1e-12)),
           "weight": 10 * np.log10(np.maximum(w[fc < 120].sum(0), 1e-12)),
           "fullness": np.sqrt(np.maximum((w * (lf - c) ** 2).sum(0), 0)),
           "noisiness": np.exp(np.log(dens).mean(0)) / dens.mean(0)}
    for k in out:
        if k != "loudness":
            out[k] = np.where(live, out[k], np.nan)
    return out


def movement(P, dt):
    """Spectral flux: mean |dB change| per second over bands within 60 dB of the frame's loudest band."""
    D = 10 * np.log10(np.maximum(P, 1e-20))
    if D.shape[1] < 2:
        return np.array([np.nan])
    aud = (D > D.max(0) - SILENT_DB)[:, 1:] | (D > D.max(0) - SILENT_DB)[:, :-1]
    f = np.where(aud, np.abs(np.diff(D, axis=1)), np.nan)
    m = np.nanmean(np.where(aud.any(0), f, 0), 0) / dt
    tot = 10 * np.log10(np.maximum(P.sum(0), 1e-20))
    return np.concatenate([[np.nan], np.where(tot[1:] > tot.max() - SILENT_DB, m, np.nan)])


def onsets(L, dt):
    """Onset indicator per frame (loudness rise > 6 dB within 40 dB of the peak), as a rate per second."""
    on = np.zeros_like(L)
    on[1:] = (np.diff(L) > 6) & (L[1:] > L.max() - 40)
    return on / dt


def attack(env, dt):
    """log10 s from 20% to 90% of the peak amplitude (log attack time)."""
    a = np.asarray(env, np.float64)
    if a.size < 2 or a.max() <= 0:
        return np.nan
    pk = int(a.argmax()); t20 = int(np.argmax(a[:pk + 1] >= 0.2 * a[pk])); t90 = int(np.argmax(a[:pk + 1] >= 0.9 * a[pk]))
    return math.log10(max((t90 - t20) * dt, dt / 2))


def decay(L, dt):
    """Seconds from the peak until 20 dB under it; censored at the end of the window."""
    pk = int(np.argmax(L)); after = np.nonzero(L[pk:] < L[pk] - 20)[0]
    return (after[0] if after.size else len(L) - pk) * dt


def sethares(f, a):
    """Plomp-Levelt dissonance (Sethares 1993) of partials f (Hz) with amplitudes a."""
    f, a = np.asarray(f, np.float64), np.asarray(a, np.float64)
    if f.size < 2:
        return 0.0
    i, j = np.triu_indices(f.size, 1)
    fmin, df = np.minimum(f[i], f[j]), np.abs(f[i] - f[j])
    s = 0.24 / (0.021 * fmin + 19)
    return float((np.minimum(a[i], a[j]) * (np.exp(-3.5 * s * df) - np.exp(-5.75 * s * df))).sum())


def peaks(mag, freqs, k=24):
    """The k largest local maxima of a magnitude spectrum above 20 Hz, normalised to the loudest."""
    m = mag[1:-1]; ok = (m > mag[:-2]) & (m >= mag[2:]) & (freqs[1:-1] > 20)
    idx = np.nonzero(ok)[0] + 1
    if not idx.size:
        return np.array([]), np.array([])
    idx = idx[np.argsort(mag[idx])[::-1][:k]]
    return freqs[idx], mag[idx] / mag[idx].max()


# ---- summaries ------------------------------------------------------------------------------------------------------------

def summary(curves, dt, one=()):
    out = {}
    for k, v in curves.items():
        if k in one:
            if v is not None and np.isfinite(v):
                out[k] = {"mean": round(float(v), 4)}
            continue
        v = np.asarray(v, np.float64); t = np.nonzero(np.isfinite(v))[0]
        if not t.size:
            continue
        s = {"mean": round(float(v[t].mean()), 4), "std": round(float(v[t].std()), 4)}
        if t.size >= 3:
            s["slope"] = round(float(np.polyfit(t * dt, v[t], 1)[0]), 4)
        out[k] = s
    return out


def pack(src, kind, dt, curves, one=(), keep=True):
    r = {"src": src, "kind": kind, "frame_s": dt, "axes": summary(curves, dt, one)}
    if keep:
        r["curves"] = {k: [None if not np.isfinite(x) else round(float(x), 3) for x in np.atleast_1d(v)]
                       for k, v in curves.items() if k not in one and np.size(v) > 1}
    return r


# ---- sources --------------------------------------------------------------------------------------------------------------

def from_mel(q, seconds=3.0):
    """Stored log-mel (64 x 48 uint8, 0.5 dB steps, 0 = -120 dB) -> curves. No roughness: mel bands are too coarse for partials."""
    db = np.asarray(q, np.float64) / 2 - 120
    P = np.where(np.asarray(q) > 0, 10 ** (db / 10), 0) * MEL_FW[:, None]  # 0 = under the stored floor: silent, not -120 dB
    dt = seconds / db.shape[1]
    c = spectral(P, MEL_FC, MEL_FW)
    c["movement"] = movement(P, dt); c["density"] = onsets(c["loudness"], dt)
    c["attack"] = attack(10 ** (c["loudness"] / 20), dt); c["decay"] = decay(c["loudness"], dt)
    return c, dt


def from_row(r):
    """One audio row (render_pass / audio_features). Mel when stored, else the 32 averaged bands as one frame. The 10 ms envelope
    (first 400 ms) and decay_ms of the row use the same rules as attack() and decay() at finer time, so they replace the frame ones."""
    if r.get("mel"):
        import base64
        c, dt = from_mel(np.frombuffer(base64.b64decode(r["mel"]), np.uint8).reshape(sd.MEL_SHAPE), r.get("seconds") or 3.0)
    elif r.get("bands"):
        c, dt = spectral(np.array(r["bands"])[:, None], B32_FC, B32_FW), r.get("seconds") or 0.0
        c = {k: v[0] for k, v in c.items()}
        if r.get("seconds"):
            c["density"] = (r.get("onsets") or 0) / r["seconds"]
    else:
        return None
    if r.get("env") and max(r["env"]) > 0:
        c["attack"] = attack(r["env"], 0.01)
    if r.get("decay_ms") is not None:
        c["decay"] = r["decay_ms"] / 1000
    if r.get("corr") is not None:
        c["width"] = 1 - r["corr"]
    if r.get("midi") is not None:
        c["pitch"] = float(r["midi"])
    for k in DETAIL:                                  # G2: stored by own_sweeps.py in the row's detail, one number per window
        v = (r.get("detail") or {}).get(k)
        if v is not None:
            c[k] = float(v)
    one = {k for k, v in c.items() if np.ndim(v) == 0}
    src = "%s#%s" % (r.get("src", ""), r.get("note", "")) if r.get("note") else r.get("src", "")
    return pack(src, "mel" if r.get("mel") else "bands", dt, c, one, keep=bool(r.get("mel")))


def from_audio(x, rate, src="", note=None):
    """(channels x samples) -> curves on the 62.5 ms grid, through the same mel bands as the stored rows, plus roughness and width
    per frame from the linear spectrum and the two channels."""
    import render_pass as rp
    x = np.atleast_2d(np.asarray(x, np.float64)); m = x.mean(0); nfft, hop = 2048, 512
    if m.size < nfft:
        return None
    n = 1 + (m.size - nfft) // hop; win = np.hanning(nfft)
    fr = np.lib.stride_tricks.as_strided(m, (n, nfft), (m.strides[0] * hop, m.strides[0])) * win
    mag = np.abs(np.fft.rfft(fr, axis=1)); p = mag ** 2 / (win.sum() ** 2 / 4)
    mel = rp.mel_fb(rate, nfft) @ p.T
    per = max(1, int(round(AUDIO_DT * rate / hop))); F = n // per; dt = per * hop / rate
    P = mel[:, :F * per].reshape(64, F, per).mean(2) * MEL_FW[:, None]
    c = spectral(P, MEL_FC, MEL_FW)
    c["movement"] = movement(P, dt); c["density"] = onsets(c["loudness"], dt)
    freqs = np.fft.rfftfreq(nfft, 1 / rate); M = mag[:F * per].reshape(F, per, -1).mean(1)
    c["roughness"] = np.array([sethares(*peaks(M[i], freqs)) for i in range(F)])
    c["roughness"][~np.isfinite(c["brightness"])] = np.nan
    if x.shape[0] >= 2:
        seg = x[:, :F * per * hop].reshape(2, F, -1)
        num = (seg[0] * seg[1]).sum(1); den = np.sqrt((seg[0] ** 2).sum(1) * (seg[1] ** 2).sum(1))
        c["width"] = np.where(den > 0, 1 - num / np.maximum(den, 1e-30), np.nan)
    e = np.sqrt(np.convolve(m ** 2, np.ones(int(rate * 0.01)) / int(rate * 0.01), "valid")[::int(rate * 0.01)])
    c["attack"] = attack(e, 0.01)
    L10 = 20 * np.log10(np.maximum(e, 1e-10)); c["decay"] = decay(L10, 0.01)
    if note is not None:
        c["pitch"] = float(note)
    return pack(src, "audio", dt, c, ("attack", "decay", "pitch"))


KK_MAJ = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
KK_MIN = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])
KEYS = np.array([np.roll(p, k) for p in (KK_MAJ, KK_MIN) for k in range(12)])


def _corr(a, B):
    a = a - a.mean(); B = B - B.mean(1, keepdims=True)
    d = np.sqrt((a ** 2).sum() * (B ** 2).sum(1))
    return np.where(d > 0, B @ a / np.maximum(d, 1e-12), np.nan)


def phrase(notes, src="", dt=MIDI_DT):
    """notes: (on s, off s, pitch, velocity) of pitched notes -> music-axis curves on a 250 ms grid (first 120 s)."""
    if not len(notes):
        return None
    N = np.array(notes, np.float64); N = N[N[:, 0] < MIDI_MAX_S]
    if not len(N):
        return None
    end = min(N[:, 1].max(), MIDI_MAX_S); F = max(1, int(math.ceil(end / dt)))
    t0 = np.arange(F) * dt
    ov = np.clip(np.minimum(N[:, 1:2], t0 + dt) - np.maximum(N[:, 0:1], t0), 0, None)  # (notes, frames) seconds sounding
    snd = ov > 0; pitch, vel = N[:, 2], N[:, 3]
    w = ov * vel[:, None]; ws = w.sum(0)
    c = {"loudness": np.where(snd.any(0), 40 * np.log10(np.maximum((snd * vel[:, None]).sum(0), 1) / np.maximum(snd.sum(0), 1) / 127), np.nan),
         "pitch": np.where(ws > 0, (w * pitch[:, None]).sum(0) / np.maximum(ws, 1e-9), np.nan)}
    hi = np.where(snd, pitch[:, None], -1).max(0); lo = np.where(snd, pitch[:, None], 999).min(0)
    c["spread"] = np.where(snd.any(0), hi - lo, np.nan)
    on = np.minimum((N[:, 0] / dt).astype(int), F - 1)
    c["density"] = np.bincount(on, minlength=F)[:F] / dt
    top = np.where(snd.any(0), hi, np.nan)
    c["contour"] = np.concatenate([[np.nan], np.diff(top) / dt])
    pc = np.zeros((12, F)); np.add.at(pc, (pitch.astype(int) % 12,), ov)
    key = KEYS[int(np.nanargmax(_corr(pc.sum(1), KEYS)))] if pc.sum() else None
    c["tension"] = np.array([(1 - _corr(pc[:, i], key[None])[0]) / 2 if pc[:, i].any() and key is not None else np.nan for i in range(F)])
    hr, prev = np.full(F, np.nan), None
    for i in range(F):
        s = set(np.nonzero(pc[:, i] > 0.02)[0])
        if s:
            hr[i] = 0.0 if prev is None or len(s & prev) / len(s | prev) >= 0.5 else 1 / dt
            prev = s
    c["harmonic_rhythm"] = hr
    rough = np.full(F, np.nan); k = np.arange(1, 7); amp = 0.88 ** (k - 1)
    for i in range(F):
        idx = np.nonzero(snd[:, i])[0]
        if idx.size:
            idx = idx[np.argsort(vel[idx])[::-1][:12]]
            ps = np.unique(pitch[idx]); f0 = 440 * 2 ** ((ps - 69) / 12)
            rough[i] = sethares((f0[:, None] * k).ravel(), np.tile(amp, ps.size))
    c["roughness"] = rough
    r = pack(src, "midi", dt, c)
    ts = np.nonzero(np.isfinite(top))[0]
    if ts.size >= 3:
        r["axes"]["contour"]["slope_fit"] = round(float(np.polyfit(ts * dt, top[ts], 1)[0]), 4)  # the phrase's contour, semitones/s
    return r


def from_midi(path):
    import expression_pieces as E, midi_ingest as MI, song_detail as S
    tracks, div = E.parse_midi(path)
    tm = S.tempo_map(tracks); sec, _ = S.clock(tm, div)
    ns = [(sec(on), sec(off), p, v) for on, off, p, ch, v, prog in MI.notes(tracks) if ch != 9]
    return phrase(ns, path)


# ---- commands -------------------------------------------------------------------------------------------------------------

def _same(a, b):
    return json.dumps(a, sort_keys=True) == json.dumps(b, sort_keys=True)


def run(items, fn, out, budget, repeat=50):
    t0, n, bad, first = time.time(), 0, 0, []
    with open(out, "w") as f:
        for it in items:
            if budget and time.time() - t0 > budget:
                print("budget %.0f s reached after %d items" % (budget, n)); break
            try:
                r = fn(it)
            except Exception as e:  # noqa: BLE001 - one bad file never stops the pass
                bad += 1; r = None
                if bad <= 3:
                    print("skip:", str(it)[:80], type(e).__name__, e)
            if r is None:
                continue
            f.write(json.dumps(r) + "\n"); n += 1
            if len(first) < repeat:
                first.append((it, r))
    diff = sum(not _same(fn(it), r) for it, r in first)
    print("%d rows -> %s in %.0f s (%d skipped); repeat check on %d: %s" % (n, out, time.time() - t0, bad, len(first),
                                                                         "identical" if not diff else "%d DIFFER" % diff))
    if diff:
        raise SystemExit(1)


def cmd_rows(a):
    def items():
        k = 0
        for line in open(os.path.expanduser(a.file)):
            r = json.loads(line)
            if r.get("schema", 2) == 2 or "bands" in r:
                yield r; k += 1
                if a.limit and k >= a.limit:
                    return
    run(items(), from_row, a.out, a.budget)


def cmd_midi(a):
    by = collections.defaultdict(list)
    for p in glob.glob(os.path.join(os.path.expanduser(a.root), "**", "*.mid*"), recursive=True):
        by[os.path.relpath(p, a.root).split(os.sep)[0]].append(p)
    pick = []
    for k in sorted(by):  # a stable spread per folder: the files with the smallest crc of their name
        pick += sorted(by[k], key=lambda p: zlib.crc32(p.encode()))[:a.per_folder]
    print("%d folders, %d files picked" % (len(by), len(pick)))
    run(pick, from_midi, a.out, a.budget)


def cmd_audio(a):
    import audio_features as af
    for p in a.files:
        x = af.decode(p, 30)
        r = from_audio(x, af.FS, p) if x is not None else None
        print(p, json.dumps(r["axes"] if r else None))


def cmd_coverage(a):
    print("| file | rows | " + " | ".join(AXES) + " |"); print("|---|---|" + "---|" * len(AXES))
    for p in a.files:
        n, has, curve = 0, collections.Counter(), collections.Counter()
        for line in open(p):
            r = json.loads(line); n += 1
            has.update(list(r["axes"])); curve.update(list(r.get("curves", {})))
        cell = lambda k: "-" if not has[k] else ("curve" if curve[k] >= has[k] / 2 else "1") + (" %d%%" % round(100 * has[k] / n) if has[k] < n else "")
        print("| %s | %d | %s |" % (os.path.basename(p), n, " | ".join(cell(k) for k in AXES)))


def cmd_test(_a):
    """Each rule moves the way its definition says, and the three forms (wav, mel row, 32-band row) agree on the same sound."""
    import render_pass as rp, audio_features as af
    rate, T = 48000, 3.0; t = np.arange(int(rate * T)) / rate; rng = np.random.default_rng(0)
    env = np.minimum(t / 0.005, 1) * np.exp(-t * 1.5)
    tone = lambda f0, n=1, roll=1.0: sum(roll ** (k - 1) * np.sin(2 * np.pi * f0 * k * t) for k in range(1, n + 1))
    S = {"sine 220": tone(220) * env, "saw 220 (bright)": tone(220, 40) / 3 * env, "saw 220 dark": tone(220, 40, 0.5) * env,
         "noise": rng.standard_normal(t.size) * 0.2 * env, "sine 55 (heavy)": tone(55) * env,
         "slow attack": tone(220, 10, 0.7) * np.minimum(t / 0.8, 1), "fifth": (tone(220, 6, 0.88) + tone(330, 6, 0.88)) * env,
         "minor second": (tone(220, 6, 0.88) + tone(233.08, 6, 0.88)) * env}
    R = {k: from_audio(np.stack([v, v]), rate, k)["axes"] for k, v in S.items()}
    m = lambda k, ax: R[k][ax]["mean"]
    checks = [("brightness saw > dark saw > sine", m("saw 220 (bright)", "brightness") > m("saw 220 dark", "brightness") > m("sine 220", "brightness")),
              ("sharpness noise > saw > sine", m("noise", "sharpness") > m("saw 220 (bright)", "sharpness") > m("sine 220", "sharpness")),
              ("noisiness noise > saw", m("noise", "noisiness") > 2 * m("saw 220 (bright)", "noisiness")),
              ("weight sine 55 > sine 220", m("sine 55 (heavy)", "weight") > m("sine 220", "weight") + 10),
              ("fullness saw > sine", m("saw 220 (bright)", "fullness") > m("sine 220", "fullness")),
              ("attack slow > fast", m("slow attack", "attack") > m("sine 220", "attack") + 1),
              ("roughness minor second > fifth", m("minor second", "roughness") > m("fifth", "roughness")),
              ("width mono = 0", abs(m("sine 220", "width")) < 1e-6)]
    tens = phrase([(0, 1, 60, 90), (0, 1, 64, 90), (0, 1, 67, 90), (1, 2, 61, 90), (1, 2, 66, 90), (1, 2, 70, 90), (2, 3, 60, 90), (2, 3, 64, 90), (2, 3, 67, 90)])
    tc = tens["curves"]["tension"]
    checks += [("tension: C major, F# major, C major -> middle chord tenser", tc[5] > tc[1] and tc[5] > tc[9]),
               ("harmonic rhythm 3 chords in 3 s = 2 changes", abs(tens["axes"]["harmonic_rhythm"]["mean"] * 3 - 2) < 0.6),
               ("midi roughness: minor second > fifth", phrase([(0, 1, 57, 90), (0, 1, 58, 90)])["axes"]["roughness"]["mean"]
                > phrase([(0, 1, 57, 90), (0, 1, 64, 90)])["axes"]["roughness"]["mean"]),
               ("contour rising scale > 0", phrase([(i * 0.5, i * 0.5 + 0.5, 60 + 2 * i, 90) for i in range(8)])["axes"]["contour"]["slope_fit"] > 0)]
    agree = []
    for k, v in S.items():  # the same sound as a stored mel row and as a 32-band row, measured by from_row
        seg = np.stack([v, v]); q = np.frombuffer(__import__("base64").b64decode(rp.logmel(seg, rate)), np.uint8).reshape(sd.MEL_SHAPE)
        mel = from_mel(q)[0]
        x = seg[:, :af.FFT * (seg.shape[1] // af.FFT)]; fr = x.mean(0).reshape(-1, af.FFT)
        pw = (np.abs(np.fft.rfft(fr * af._win, axis=1)) ** 2) * af._scale
        b32 = {kk: vv[0] for kk, vv in spectral((af._BM @ pw.mean(0))[:, None], B32_FC, B32_FW).items()}
        for ax in ("brightness", "sharpness", "fullness"):
            agree.append((k, ax, m(k, ax), float(np.nanmean(mel[ax])), float(b32[ax])))
    ok = True
    import detail as det
    print("-- detail.py (G2: partials, formants, glide, space, vibrato, tremolo, tuning, velocity response inputs)")
    ok &= bool(det.run_checks()); print("-- spectral axes")
    for name, good in checks:
        print("%s  %s" % ("PASS" if good else "FAIL", name)); ok &= bool(good)
    print("\nwav vs mel row vs 32-band row (bands are a time average, so they weight loud frames more):")
    print("| sound | axis | wav | mel row | 32-band row |\n|---|---|---|---|---|")
    for k, ax, a_, b_, c_ in agree:
        print("| %s | %s | %.3f | %.3f | %.3f |" % (k, ax, a_, b_, c_))
    rep = _same(from_audio(np.stack([S["noise"]] * 2), rate), from_audio(np.stack([S["noise"]] * 2), rate))
    print("%s  repeat: identical numbers" % ("PASS" if rep else "FAIL"))
    raise SystemExit(0 if ok and rep else 1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    r = sp.add_parser("rows"); r.add_argument("file"); r.add_argument("--out", required=True); r.add_argument("--limit", type=int, default=0)
    r.add_argument("--budget", type=float, default=1200)
    m = sp.add_parser("midi"); m.add_argument("root"); m.add_argument("--out", required=True); m.add_argument("--per-folder", type=int, default=20)
    m.add_argument("--budget", type=float, default=600)
    au = sp.add_parser("audio"); au.add_argument("files", nargs="+")
    c = sp.add_parser("coverage"); c.add_argument("files", nargs="+")
    sp.add_parser("test")
    a = ap.parse_args()
    {"rows": cmd_rows, "midi": cmd_midi, "audio": cmd_audio, "coverage": cmd_coverage, "test": cmd_test}[a.cmd](a)


if __name__ == "__main__":
    main()
