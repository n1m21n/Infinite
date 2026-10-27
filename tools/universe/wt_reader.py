#!/usr/bin/env python3
"""Wavetables (.wt) -> per-frame harmonic shapes, so wavetable oscillators can be seeded with real tables.
    wt_reader.py [ROOT ...] [--out ~/Infinite/training-data/wavetables/wt.jsonl]
A .wt (Surge / Bitwig 'vawt') is: 'vawt', u32 frame length, u16 frame count, u16 flags (bit 2: int16 samples,
else float32), then the frames (read only). Per table: src, name, frames, length, and for up to 16 frames spread
over the table the magnitudes of harmonics 1..64 (normalised to the loudest) plus odd/even balance and brightness
(harmonic centroid). Identical tables (the same pack copied twice) are written once."""
import argparse, collections, hashlib, json, os, struct, sys, time
import numpy as np

SKIP = {".Spotlight-V100", ".Trashes", ".fseventsd", ".TemporaryItems", ".DocumentRevisions-V100"}


def read_wt(path):
    b = open(path, "rb").read()
    if b[:4] != b"vawt" or len(b) < 12:
        return None
    size, count, flags = struct.unpack("<IHH", b[4:12])
    if not (2 <= size <= 1 << 16 and 1 <= count <= 1024):
        return None
    dt, w = (np.int16, 2) if flags & 4 else (np.float32, 4)
    n = size * count
    data = np.frombuffer(b[12:12 + n * w], dtype=dt)
    if len(data) < n:
        return None
    x = data.astype(np.float64).reshape(count, size)
    if dt == np.int16:
        x /= 32768.0
    return x, hashlib.sha1(b[12:12 + n * w]).hexdigest()


def shape(frame):
    h = np.abs(np.fft.rfft(frame))[1:65]
    m = h.max() or 1.0
    h = h / m
    k = np.arange(1, len(h) + 1)
    return {"harm": [round(float(v), 4) for v in h], "odd_even": round(float(h[0::2].sum() / max(1e-9, h[1::2].sum())), 3),
            "bright": round(float((k * h).sum() / max(1e-9, h.sum())), 2)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("roots", nargs="*", default=["/Volumes/Wd-Black"])
    ap.add_argument("--out", default="~/Infinite/training-data/wavetables/wt.jsonl")
    a = ap.parse_args()
    out = os.path.expanduser(a.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    t0, n, seen = time.time(), collections.Counter(), set()
    with open(out, "w") as fo:
        for root in a.roots:
            for d, subs, fs in os.walk(root):
                subs[:] = sorted(s for s in subs if s not in SKIP and not s.startswith("."))
                for f in sorted(fs):
                    if not f.lower().endswith(".wt") or f.startswith("._"):
                        continue
                    p = os.path.join(d, f)
                    try:
                        r = read_wt(p)
                    except OSError:
                        r = None
                    if not r:
                        n["unreadable"] += 1
                        continue
                    x, digest = r
                    if digest in seen:
                        n["duplicate"] += 1
                        continue
                    seen.add(digest)
                    pick = sorted({i * (len(x) - 1) // 15 for i in range(16)}) if len(x) > 16 else range(len(x))
                    fo.write(json.dumps({"src": p, "name": os.path.splitext(f)[0], "frames": len(x), "length": x.shape[1],
                                         "shapes": [dict(shape(x[i]), frame=i) for i in pick],
                                         "license": "sample pack, local only"}, separators=(",", ":")) + "\n")
                    n["tables"] += 1
    print(dict(n), "in %.0f s" % (time.time() - t0))
    print("wrote", out)


if __name__ == "__main__":
    main()
