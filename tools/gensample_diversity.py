#!/usr/bin/env python3
"""Generate Samples diversity metrics (docs/plans/generate-samples/README.md, sections 6-7).

  1. Render takes:  INFINITE_GENSAMPLEDIVERSITY=<dir> INFINITE_GENSAMPLETAKES=200 build/Infinite.app/Contents/MacOS/Infinite
  2. Measure:       python3 tools/gensample_diversity.py <dir> [--baseline baseline.json] [--write-baseline baseline.json] [--md out.md]

Per take: spectral centroid, flatness, crest, onset count, median onset spacing, decay time (-20 dB),
stereo width, low-band (<150 Hz) share, 12-bin pitch-class histogram. Per part: mean pairwise
standardized distance, mean nearest-neighbour distance, distinct patch graphs, distinct voice-type
sets, distinct (types, root, scale) tuples, near-duplicate share. The standardization (feature std over
all takes) and the near-duplicate threshold (a quarter of the mean pairwise distance) are taken from
the baseline file when one is given, so a later run is measured on the baseline's own ruler.
"""
import argparse
import json
import os
import struct
import sys

import numpy as np

FEATURES = ["centroid", "flatness", "crest", "onsets", "spacing", "decay", "width", "low", "chroma"]
SCALAR = FEATURES[:-1]


def read_wav(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:4] == b"RIFF" and data[8:12] == b"WAVE"
    pos, fmt, pcm = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", body[:16])
        elif cid == b"data":
            pcm = body
        pos += 8 + size + (size & 1)
    tag, ch, rate, _, _, bits = fmt
    if tag == 3 or (tag == 0xFFFE and bits == 32):
        x = np.frombuffer(pcm, dtype="<f4").astype(np.float64)
    elif bits == 16:
        x = np.frombuffer(pcm, dtype="<i2").astype(np.float64) / 32768.0
    elif bits == 24:
        b = np.frombuffer(pcm, dtype=np.uint8).reshape(-1, 3)
        v = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
        v = np.where(v >= 1 << 23, v - (1 << 24), v)
        x = v.astype(np.float64) / (1 << 23)
    else:
        raise ValueError(f"{path}: unsupported wav {tag}/{bits}")
    return x.reshape(-1, ch).T, rate


def features(path):
    x, rate = read_wav(path)
    l, r = (x[0], x[1]) if x.shape[0] > 1 else (x[0], x[0])
    mid, side = 0.5 * (l + r), 0.5 * (l - r)
    m = mid
    n = len(m)
    spec = np.abs(np.fft.rfft(m * np.hanning(n))) + 1e-12
    freqs = np.fft.rfftfreq(n, 1.0 / rate)
    p = spec ** 2
    centroid = float(np.log2(max(1.0, (freqs * p).sum() / p.sum())))
    flatness = float(np.exp(np.mean(np.log(p + 1e-18))) / np.mean(p + 1e-18))
    flatness = float(np.log10(flatness + 1e-12))
    rms = float(np.sqrt(np.mean(m ** 2)) + 1e-12)
    crest = float(20 * np.log10((np.abs(m).max() + 1e-12) / rms))
    low = float(p[freqs < 150].sum() / p.sum())
    width = float(20 * np.log10((np.sqrt(np.mean(side ** 2)) + 1e-9) / rms))

    hop = int(rate * 0.01)
    env = np.sqrt(np.convolve(m ** 2, np.ones(hop) / hop, mode="same"))[::hop]
    env_db = 20 * np.log10(env + 1e-9)
    flux = np.maximum(0.0, np.diff(env_db))
    thr = max(3.0, float(np.percentile(flux, 90)) * 0.5)
    onsets, last = [], -10
    for i, v in enumerate(flux):
        if v > thr and i - last >= 5:
            onsets.append(i)
            last = i
    spacing = float(np.median(np.diff(onsets)) * 0.01) if len(onsets) > 1 else 2.0
    # decay: the time from the loudest point until the envelope is 20 dB down (capped at 2 s)
    pk = int(np.argmax(env_db))
    below = np.where(env_db[pk:] < env_db[pk] - 20.0)[0]
    decay = float(min(2.0, (below[0] if len(below) else 200) * 0.01))

    chroma = np.zeros(12)
    sel = (freqs > 40) & (freqs < 2000)
    midi = 69 + 12 * np.log2(freqs[sel] / 440.0)
    np.add.at(chroma, np.round(midi).astype(int) % 12, p[sel])
    chroma = chroma / (chroma.sum() + 1e-18)
    return dict(centroid=centroid, flatness=flatness, crest=crest, onsets=float(len(onsets)), spacing=spacing,
                decay=decay, width=width, low=low, chroma=chroma.tolist())


def vec(f):
    return np.array([f[k] for k in SCALAR] + f["chroma"])


def pairwise(v):
    d = np.sqrt(((v[:, None, :] - v[None, :, :]) ** 2).sum(-1))
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--baseline")
    ap.add_argument("--write-baseline")
    ap.add_argument("--md")
    a = ap.parse_args()
    takes = json.load(open(os.path.join(a.dir, "takes.json")))
    feats = []
    for t in takes:
        f = features(os.path.join(a.dir, t["file"] + ".wav"))
        f.update(part=t["part"], types=t["types"], hash=t["hash"], root=t["root"], scale=t["scale"], style=t["style"], bpm=t["bpm"])
        feats.append(f)
    allv = np.array([vec(f) for f in feats])
    base = json.load(open(a.baseline)) if a.baseline else None
    if base:
        mu, sd, thr = np.array(base["mean"]), np.array(base["std"]), base["near_dup_threshold"]
    else:
        mu, sd = allv.mean(0), allv.std(0) + 1e-9
        # chroma bins share one scale so no single pitch class counts as a feature of its own
        sd[len(SCALAR):] = sd[len(SCALAR):].mean() * np.sqrt(12) / 2.0
        thr = None
    z = (allv - mu) / sd
    parts = sorted(set(f["part"] for f in feats))
    if thr is None:
        ds = []
        for p in parts:
            idx = [i for i, f in enumerate(feats) if f["part"] == p]
            d = pairwise(z[idx])
            ds.append(d[np.triu_indices(len(idx), 1)])
        thr = 0.25 * float(np.concatenate(ds).mean())
    rows, out = [], {}
    for p in parts:
        idx = [i for i, f in enumerate(feats) if f["part"] == p]
        d = pairwise(z[idx])
        tri = d[np.triu_indices(len(idx), 1)]
        np.fill_diagonal(d, np.inf)
        nn = d.min(1)
        sub = [feats[i] for i in idx]
        rows.append(dict(
            part=p, takes=len(idx), mean_dist=float(tri.mean()), nn_dist=float(nn.mean()),
            near_dup=float((nn < thr).mean()),
            graphs=len(set(f["hash"] for f in sub)),
            voice_sets=len(set(f["types"] for f in sub)),
            tuples=len(set((f["types"], f["root"], f["scale"]) for f in sub)),
            means={k: float(np.mean([f[k] for f in sub])) for k in SCALAR},
            ranges={k: [float(np.percentile([f[k] for f in sub], 5)), float(np.percentile([f[k] for f in sub], 95))] for k in SCALAR}))
    out["parts"] = rows
    if a.write_baseline:
        json.dump(dict(mean=mu.tolist(), std=sd.tolist(), near_dup_threshold=thr, parts=rows), open(a.write_baseline, "w"), indent=1)
    lines = ["| part | takes | mean pairwise distance | mean nearest-neighbour | near-duplicate share | distinct graphs | distinct voice sets | distinct (voices, root, scale) |",
             "|---|---|---|---|---|---|---|---|"]
    for r in rows:
        lines.append(f"| {r['part']} | {r['takes']} | {r['mean_dist']:.2f} | {r['nn_dist']:.2f} | {r['near_dup']*100:.0f}% | {r['graphs']} | {r['voice_sets']} | {r['tuples']} |")
    lines += ["", f"near-duplicate threshold (a quarter of the baseline's mean pairwise distance): {thr:.3f}", "",
              "| part | " + " | ".join(SCALAR) + " |", "|---|" + "---|" * len(SCALAR)]
    for r in rows:
        lines.append(f"| {r['part']} | " + " | ".join(f"{r['means'][k]:.2f}" for k in SCALAR) + " |")
    txt = "\n".join(lines)
    print(txt)
    if a.md:
        open(a.md, "w").write(txt + "\n")


if __name__ == "__main__":
    sys.exit(main())
