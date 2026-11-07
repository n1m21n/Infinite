#!/usr/bin/env python3
"""Per-genre drum sounds from the genre-named sample packs on the drive (read only; statistics only, no audio kept).
    kit_scan.py [--root /Volumes/Wd-Black] [--per-pack 40] [--budget 900] [--out refs/kits.json]
Packs map to card styles by their folder name. One-shots under kick / snare / clap / hat paths are measured:
pitch_hz (spectral peak of the body: kick 30-150 Hz, snare/clap 120-500 Hz), decay_ms (peak to -30 dB),
centroid_hz (first 150 ms), crest_db. The bundled kit's own eight hits are measured too, so the engine can move
each lane by the difference (semitones, decay ratio). Writes median / q1 / q3 per style family and per style."""
import argparse, json, os, re, sys, time, statistics
import numpy as np, soundfile as sf
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PACKS = [  # (folder keyword, styles)
    ("dub techno", ["dub_techno"]), ("dark techno", ["industrial_techno", "detroit_techno"]), ("melodic techno", ["detroit_techno"]),
    ("tech house", ["tech_house"]), ("deep house", ["deep_house"]), ("acid house", ["acid_house"]), ("old skool house", ["acid_house", "deep_house"]),
    ("progressive house", ["progressive_house"]), ("garage", ["uk_garage", "future_garage"]), ("jungle", ["jungle"]),
    ("drum and bass", ["liquid_dnb", "neurofunk"]), ("drum.and.bass", ["liquid_dnb", "neurofunk"]), ("dnb", ["liquid_dnb", "neurofunk"]),
    ("trance", ["classic_trance"]), ("house", ["deep_house", "tech_house"]), ("techno", ["detroit_techno", "minimal_techno"]),
    ("afro", ["amapiano"]), ("lofi", ["lofi_beats"]), ("dubstep", ["dubstep"]), ("breaks", ["breakbeat"]), ("electro", ["electro"])]
ROLE = [("kick", re.compile(r"kick|bassdrum|bd[ _\-0-9]")), ("snare", re.compile(r"snare|clap|rim")), ("hat", re.compile(r"hat|hh[ _\-0-9]"))]
AUDIO = {".wav", ".aif", ".aiff", ".flac"}


def measure(path, role):
    info = sf.info(path)
    if info.duration > 4.0 or info.duration < 0.02:
        return None
    x, sr = sf.read(path, dtype="float32", always_2d=True, frames=int(min(info.frames, sr_cap(info))))
    x = x.mean(axis=1)
    pk = float(np.max(np.abs(x)))
    if pk < 1e-4:
        return None
    hop = max(1, int(sr * 0.005))
    env = np.sqrt(np.convolve(x * x, np.ones(hop) / hop, mode="same"))
    ip = int(np.argmax(env)); top = env[ip]
    below = np.where(env[ip:] < top * 10 ** (-30 / 20))[0]
    decay = (below[0] if len(below) else len(env) - ip) / sr * 1000
    seg = x[ip:ip + int(sr * 0.15)]
    if len(seg) < 256:
        return None
    w = seg * np.hanning(len(seg)); n = 1 << int(np.ceil(np.log2(len(seg) * 4)))
    sp = np.abs(np.fft.rfft(w, n)); fr = np.fft.rfftfreq(n, 1 / sr)
    cen = float((sp * fr).sum() / max(sp.sum(), 1e-12))
    lo, hi = (30, 150) if role == "kick" else (120, 500)
    m = (fr >= lo) & (fr <= hi)
    pitch = float(fr[m][np.argmax(sp[m])]) if role != "hat" else 0.0
    rms = float(np.sqrt(np.mean(seg * seg)))
    return {"pitch_hz": pitch, "decay_ms": float(decay), "centroid_hz": cen, "crest_db": float(20 * np.log10(pk / max(rms, 1e-9)))}


def sr_cap(info):
    return info.samplerate * 1.2


def q(v):
    v = sorted(v); n = len(v)
    return {"n": n, "q1": v[n // 4], "median": v[n // 2], "q3": v[(3 * n) // 4]}


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--root", default="/Volumes/Wd-Black"); ap.add_argument("--per-pack", type=int, default=40)
    ap.add_argument("--budget", type=int, default=900); ap.add_argument("--out", default=os.path.join(ROOT, "internal/plans/generate-samples/refs/kits.json"))
    a = ap.parse_args(); t0 = time.time(); rows = {}  # style -> role -> metric -> [values]
    kitdir = os.path.join(ROOT, "assets/drumkits/infinite-basic")
    base = {}
    for i, (nm, role) in enumerate([("01-kick", "kick"), ("02-snare", "snare"), ("03-closed-hat", "hat"), ("05-clap", "snare")]):
        base[nm] = measure(os.path.join(kitdir, nm + ".wav"), role)
    for pack in sorted(os.listdir(a.root)):
        low = pack.lower()
        styles = next((s for k, s in PACKS if k in low), None)
        if not styles or time.time() - t0 > a.budget:
            continue
        files = {r: [] for r, _ in ROLE}
        for d, _, fs in os.walk(os.path.join(a.root, pack)):
            dl = d.lower()
            if "loop" in dl or "preset" in dl:
                continue
            for f in fs:
                if os.path.splitext(f)[1].lower() not in AUDIO or f.startswith("._"):
                    continue
                p = (dl + "/" + f.lower())
                if "loop" in p:
                    continue
                for r, rx in ROLE:
                    if rx.search(p):
                        files[r].append(os.path.join(d, f)); break
        got = 0
        for r, lst in files.items():
            step = max(1, len(lst) // a.per_pack) if hasattr(a, "per_pack") else 1
            for f in lst[::step][:a.per_pack]:
                try:
                    m = measure(f, r)
                except Exception:
                    m = None
                if m:
                    got += 1
                    for s in styles:
                        for k, v in m.items():
                            rows.setdefault(s, {}).setdefault(r, {}).setdefault(k, []).append(v)
        print("%-60s %3d hits -> %s (%.0fs)" % (pack[:60], got, ",".join(styles), time.time() - t0), flush=True)
    out = {"bundled_kit": base, "styles": {s: {r: {k: q(v) for k, v in ms.items()} for r, ms in rr.items()} for s, rr in rows.items()}}
    os.makedirs(os.path.dirname(a.out), exist_ok=True); json.dump(out, open(a.out, "w"), indent=1)
    print("wrote", a.out, "styles:", len(rows))


if __name__ == "__main__":
    main()
