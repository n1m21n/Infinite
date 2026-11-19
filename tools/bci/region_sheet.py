#!/usr/bin/env python3
"""Per-region spectrogram proof sheet: shows what averaging 4 channels erases.

usage: region_sheet.py OPEN.csv CLOSED.csv OUT.png
CSV = 4 columns uV (AF7, AF8, TP9, TP10) at 256 Hz.
"""
import sys
import numpy as np
from PIL import Image, ImageDraw

FS, WIN, HOP, FMAX = 256, 256, 32, 40
CH = ["AF7", "AF8", "TP9", "TP10"]


def spec(x):
    w = np.hanning(WIN)
    frames = [x[i:i + WIN] * w for i in range(0, len(x) - WIN, HOP)]
    p = np.abs(np.fft.rfft(np.array(frames), axis=1)) ** 2
    return p[:, 1:FMAX + 1].T  # rows = 1..40 Hz


def alpha_ratio(p):
    return p[7:12].mean()  # 8..12 Hz


def to_img(logp, lo, hi, w, h):
    a = np.clip((logp - lo) / (hi - lo), 0, 1)[::-1]
    # perceptual-ish ramp: navy -> coral -> cream
    r = np.interp(a, [0, .5, 1], [16, 232, 255])
    g = np.interp(a, [0, .5, 1], [20, 110, 240])
    b = np.interp(a, [0, .5, 1], [48, 90, 210])
    im = Image.fromarray(np.dstack([r, g, b]).astype(np.uint8))
    return im.resize((w, h), Image.NEAREST)


def main(op, cl, out):
    d = {k: np.loadtxt(p, delimiter=",") for k, p in (("open", op), ("closed", cl))}
    S = {k: [spec(v[:, c] - v[:, c].mean()) for c in range(4)] for k, v in d.items()}
    for k in S:
        S[k].append(np.mean(S[k][:4], axis=0))  # the "averaged soup"
    rows = CH + ["AVG (today)"]
    allv = np.log10(np.concatenate([s.ravel() for k in S for s in S[k]]) + 1e-9)
    lo, hi = np.percentile(allv, 5), np.percentile(allv, 99.5)
    W, H, L, T = 520, 120, 130, 40
    sheet = Image.new("RGB", (L + 2 * W + 60, T + len(rows) * (H + 30) + 20), (12, 14, 28))
    dr = ImageDraw.Draw(sheet)
    dr.text((L, 12), "EYES OPEN", fill=(230, 230, 240))
    dr.text((L + W + 30, 12), "EYES CLOSED", fill=(230, 230, 240))
    lines = []
    for i, name in enumerate(rows):
        y = T + i * (H + 30)
        ro = alpha_ratio(S["closed"][i]) / alpha_ratio(S["open"][i])
        lines.append(f"{name:12s} alpha closed/open = {ro:5.2f}x")
        dr.text((8, y + 40), name, fill=(240, 200, 180))
        dr.text((8, y + 60), f"alpha x{ro:.2f}", fill=(180, 180, 200))
        for j, k in enumerate(("open", "closed")):
            sheet.paste(to_img(np.log10(S[k][i] + 1e-9), lo, hi, W, H), (L + j * (W + 30), y))
            dr.text((L + j * (W + 30) + W + 2, y + H - 10), "1Hz", fill=(120, 120, 140))
            dr.text((L + j * (W + 30) + W + 2, y), "40", fill=(120, 120, 140))
    sheet.save(out)
    print("\n".join(lines))


if __name__ == "__main__":
    main(*sys.argv[1:4])
