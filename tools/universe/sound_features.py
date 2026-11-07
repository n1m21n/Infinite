#!/usr/bin/env python3
"""One feature vector per sound, per role, for the kit compatibility model (statistics only, no audio kept).
    ~/Infinite/media/.ytvenv/bin/python3 -I tools/universe/sound_features.py [--per-group 60] [--budget 1200] [--jobs 3]
Sources (read only, WD-Black drive):
  pack   one-shots / tonal loops listed in training-data/audio/features.jsonl, grouped by kit: a drum machine folder (KB6),
         a pack code (Vengeance VEC1...), a Logic drum kit, a Bitwig project (its bounces are one track's stems), else the pack
  rack   Ableton drum racks (.adg): the samples one rack loads
  set    Ableton sets (.als, training-data/mix/als_mix.jsonl): the samples one set plays (paths resolved by file name)
Roles: kick, snare (snare, clap, rim), hat, bass, keys, pad, lead (lead, synth, pluck). Drums count one-shots only.
Per sound, first 2 s: pitch (midi; kick 30-150 Hz / snare 120-500 Hz spectral peak, tonal autocorrelation f0, hat none),
attack_ms (10% -> 90% of the envelope peak), decay_ms (peak -> -30 dB), centroid_hz, crest_db, noise (spectral flatness),
low / mid / high share (the app's 32 bands, 0-10 / 11-21 / 22-31), loudness_db (RMS of 400 ms at the peak), bands (32, sum 1).
Writes training-data/compat/sounds.jsonl (resumable; paths stay local, never committed)."""
import argparse, collections, glob, gzip, json, math, os, random, re, sys, time
from multiprocessing import Pool
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import audio_features as af  # noqa: E402

TD = os.path.expanduser("~/Infinite/training-data")
OUT = os.path.join(TD, "compat/sounds.jsonl")
W = "/Volumes/Wd-Black/"
ROLE_OF = {"kick": "kick", "snare": "snare", "clap": "snare", "hat": "hat", "bass": "bass", "keys": "keys", "chord": "keys",
           "pad": "pad", "strings": "pad", "lead": "lead", "synth": "lead", "pluck": "lead"}
DRUMS = {"kick", "snare", "hat"}
NAME_ROLE = [("kick", re.compile(r"kick|bassdrum|\bbd[ _\-0-9]|\bbd\b", re.I)), ("snare", re.compile(r"snare|clap|rim|\bsd[ _\-0-9]|\bcp\b", re.I)),
             ("hat", re.compile(r"hat|\bhh|\boh[ _\-0-9]|\bch[ _\-0-9]", re.I)), ("bass", re.compile(r"bass|\bsub\b|808|reese", re.I)),
             ("keys", re.compile(r"piano|keys|rhodes|organ|chord|wurli|\bep\b", re.I)), ("pad", re.compile(r"pad|atmos|string|choir|drone", re.I)),
             ("lead", re.compile(r"lead|pluck|arp|synth|stab|melody", re.I))]
GENRES = [("dnb", r"drum.?(and|n|&).?bass|\bdnb\b|jungle|neuro|liquid"), ("garage", r"garage|\bukg\b|2.?step"),
          ("dubstep", r"dubstep|\bdub\b step"), ("techno", r"techno|minimal|industrial|berlin"), ("trance", r"trance"),
          ("house", r"house|disco|piano.?house|club"), ("hiphop", r"hip.?hop|boom.?bap|trap|lo.?fi|lofi|soul|rnb|r&b|vinyl"),
          ("ambient", r"ambient|downtempo|texture|organic|cinematic|atmos"), ("electro", r"electro|rave|acid|breaks|breakbeat")]
GENRES = [(g, re.compile(r, re.I)) for g, r in GENRES]


def genre_of(path):
    return next((g for g, rx in GENRES if rx.search(path)), None)


def group_of(src):
    rel = src[len(W):] if src.startswith(W) else src
    p = rel.split("/")
    top = p[0]
    if top == "KB6_Drum_Samples" and len(p) > 2:
        return "kb6/" + p[1]
    if top == "Bitwig Studio" and len(p) > 3 and p[1] == "Projects":
        return "bitwig/" + p[2]
    for c in p[:-1]:
        m = re.match(r"^(V[A-Z]{2,4}\d)\b", c)
        if m:
            return "veng/" + m[1]
    if top == "Library":
        for i, c in enumerate(p[:-1]):
            if "Drum Kits" in c and i + 1 < len(p) - 1:
                return "lib/" + p[i + 1]
        return "lib/" + "/".join(p[1:5])
    if top in ("Extra Packs", "Samples", "Apple Loops", "Music Downloads", "my music") and len(p) > 2:
        if top == "Extra Packs" and p[1].startswith("Vengaence") and len(p) > 3:
            return "xp/" + p[2]
        return top + "/" + p[1]
    return top


def role_of_name(path):
    stem = os.path.basename(path)
    return next((r for r, rx in NAME_ROLE if rx.search(stem)), None) or next((r for r, rx in NAME_ROLE if rx.search(path)), None)


def pitch_tonal(x):
    """f0 by normalised autocorrelation over 60 ms frames, median of voiced frames -> midi, or None"""
    fs = af.FS
    n = int(0.06 * fs)
    lags = np.arange(int(fs / 1000), int(fs / 30))
    out = []
    for s in range(int(0.03 * fs), min(len(x) - n - lags[-1], int(1.2 * fs)), int(0.08 * fs)):
        f = x[s:s + n + lags[-1]]
        a = f[:n]
        e0 = float((a * a).sum())
        if e0 < 1e-7:
            continue
        sp = np.fft.rfft(f, 1 << int(np.ceil(np.log2(2 * len(f)))))
        ac = np.fft.irfft(sp * np.conj(np.fft.rfft(a, len(sp) * 2 - 2)))[:lags[-1] + 1]
        r = ac[lags] / (e0 + 1e-12)
        i = int(np.argmax(r))
        if r[i] > 0.6:
            # first lag within 90% of the best avoids octave-down errors
            j = int(np.argmax(r > 0.9 * r[i]))
            out.append(fs / lags[j])
    if len(out) < 2:
        return None
    return 69 + 12 * math.log2(float(np.median(out)) / 440.0)


def measure(path, role):
    try:
        x = af.decode(path, 2.0)
    except Exception:
        return None
    return measure_array(x, role)


def measure_array(x, role):
    """x: (channels, samples) at af.FS"""
    if x is None or x.shape[1] < 2048:
        return None
    m = x.mean(0)
    pk = float(np.abs(m).max())
    if pk < 1e-4:
        return None
    fs = af.FS
    hop = int(fs * 0.005)
    nb = len(m) // hop
    env = np.sqrt((m[:nb * hop].reshape(nb, hop) ** 2).mean(1))
    ip = int(np.argmax(env)); top = float(env[ip])
    a10 = int(np.argmax(env[:ip + 1] >= 0.1 * top)); a90 = int(np.argmax(env[:ip + 1] >= 0.9 * top))
    attack = max(0.0, (a90 - a10) * 5.0)
    below = np.where(env[ip:] < top * 10 ** (-30 / 20))[0]
    decay = min(2000.0, (below[0] if len(below) else nb - ip) * 5.0)
    w0 = ip * hop; seg = m[max(0, w0 - int(0.05 * fs)):w0 + int(0.35 * fs)]
    loud = 20 * math.log10(max(1e-9, float(np.sqrt((seg ** 2).mean()))))
    pad = x if x.shape[1] >= af.FFT else np.pad(x, ((0, 0), (0, af.FFT - x.shape[1])))
    starts = np.arange(0, pad.shape[1] - af.FFT + 1, af.HOP)
    fr = np.stack([pad[:, s:s + af.FFT] for s in starts], 1) * af._win
    sp = np.fft.rfft(fr, axis=2)
    pw = (0.5 * (sp.real ** 2 + sp.imag ** 2)).sum(0) * af._scale
    # frames weighted by energy, so a short hit is not averaged away by its tail
    fe = pw.sum(1); wts = fe / max(fe.sum(), 1e-30)
    mp = (pw * wts[:, None]).sum(0)
    bands = af._BM @ mp
    tot = float(bands.sum()) or 1e-30
    s = float(mp.sum()) or 1e-30
    cen = float((af._freqs * mp).sum()) / s
    lin = np.sqrt(mp[1:] + 1e-20)
    flat = float(np.exp(np.log(lin).mean()) / lin.mean())
    rms = float(np.sqrt((m ** 2).mean()))
    if role in ("kick", "snare"):
        lo, hi = (30, 150) if role == "kick" else (120, 500)
        k = (af._freqs >= lo) & (af._freqs <= hi)
        f0 = float(af._freqs[k][np.argmax(mp[k])])
        pitch = 69 + 12 * math.log2(f0 / 440.0)
    elif role == "hat":
        pitch = None
    else:
        pitch = pitch_tonal(m)
    b = bands / tot
    return {"pitch": None if pitch is None else round(pitch, 2), "attack_ms": round(attack, 1), "decay_ms": round(decay, 1),
            "centroid_hz": round(cen, 1), "crest_db": round(20 * math.log10(pk / max(rms, 1e-9)), 2), "noise": round(flat, 4),
            "low": round(float(b[:11].sum()), 4), "mid": round(float(b[11:22].sum()), 4), "high": round(float(b[22:].sum()), 4),
            "loudness_db": round(loud, 2), "bands": [float("%.4g" % v) for v in b]}


def _one(job):
    path, role, group, source, genre = job
    r = measure(path, role)
    if r is None:
        return None
    return {"src": path, "role": role, "group": group, "source": source, "genre": genre, **r}


def refs_in(path):
    """sample file paths an .adg / .als references (absolute, or relative to the file's folder)"""
    try:
        x = gzip.open(path).read().decode("utf8", "ignore")
    except Exception:
        return []
    out = set()
    for blk in re.findall(r"<FileRef>(.*?)</FileRef>", x, re.S):
        p = re.search(r'<Path Value="([^"]*)"', blk) or re.search(r'<Name Value="([^"]*)"', blk)
        rp = re.search(r'<RelativePath Value="([^"]*)"', blk)
        cands = []
        if p and p[1]:
            cands.append(p[1])
        if rp and rp[1]:
            cands.append(os.path.normpath(os.path.join(os.path.dirname(path), rp[1])))
        for c in cands:
            if c.lower().endswith((".wav", ".aif", ".aiff", ".flac", ".mp3", ".m4a")):
                out.add(c); break
    return sorted(out)


def jobs(per_group, seed=1):
    rng = random.Random(seed)
    by = collections.defaultdict(lambda: collections.defaultdict(list))
    name_index = collections.defaultdict(list)
    for line in open(os.path.join(TD, "audio/features.jsonl")):
        r = json.loads(line)
        src = r.get("src") or ""
        if not src.startswith(W):
            continue
        name_index[os.path.basename(src).lower()].append(src)
        role = ROLE_OF.get(r.get("part"))
        if role is None or (role in DRUMS and r.get("loop")) or (r.get("seconds") or 0) < 0.03:
            continue
        by[group_of(src)][role].append(src)
    out = []
    for g, rr in by.items():
        if len(rr) < 2:  # a group only makes pairs with two roles
            continue
        gen = genre_of(g)
        for role, lst in rr.items():
            rng.shuffle(lst)
            for p in lst[:per_group]:
                out.append((p, role, "pack:" + g, "pack", gen or genre_of(p)))
    for kind, files in (("rack", [l.strip() for l in open(os.path.join(TD, "compat/adg_list.txt"))] if os.path.exists(os.path.join(TD, "compat/adg_list.txt")) else []),
                        ("set", sorted({json.loads(l)["set"] for l in open(os.path.join(TD, "mix/als_mix.jsonl"))}))):
        for f in files:
            f = f if f.startswith("/") else os.path.join(W, f.lstrip("./"))
            got = []
            for p in refs_in(f):
                if not os.path.exists(p):
                    c = name_index.get(os.path.basename(p).lower())
                    if not c:
                        continue
                    # the copy in the rack's or set's own pack first
                    top = f[len(W):].split("/")[0] if f.startswith(W) else ""
                    p = next((q for q in c if q[len(W):].split("/")[0] == top), c[0])
                role = role_of_name(p)
                if role:
                    got.append((p, role))
            if len({r for _, r in got}) >= 2:
                gen = genre_of(f)
                for p, role in got[:4 * per_group]:
                    out.append((p, role, kind + ":" + f[len(W):] if f.startswith(W) else kind + ":" + f, kind, gen or genre_of(p)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--per-group", type=int, default=60); ap.add_argument("--budget", type=int, default=1200)
    ap.add_argument("--jobs", type=int, default=3); ap.add_argument("--out", default=OUT)
    a = ap.parse_args()
    if not os.path.isdir(W):
        print("drive not mounted"); sys.exit(2)
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    done = set()
    if os.path.exists(a.out):
        for l in open(a.out):
            r = json.loads(l); done.add((r["src"], r["group"]))
    todo = [j for j in jobs(a.per_group) if (j[0], j[2]) not in done]
    # drums and racks first: the drum pairs are the ones the engine uses first
    todo.sort(key=lambda j: (j[3] == "pack", j[1] not in DRUMS))
    print("todo", len(todo), "done", len(done), flush=True)
    t0 = time.time(); n = 0
    with open(a.out, "a") as fo, Pool(a.jobs) as pool:
        for r in pool.imap_unordered(_one, todo, chunksize=8):
            if r:
                fo.write(json.dumps(r) + "\n"); n += 1
            if n % 500 == 0 and r:
                fo.flush(); print("%d rows %.0fs" % (n, time.time() - t0), flush=True)
            if time.time() - t0 > a.budget:
                print("budget hit"); pool.terminate(); break
    print("wrote", n, "rows in %.0fs ->" % (time.time() - t0), a.out)


if __name__ == "__main__":
    main()
