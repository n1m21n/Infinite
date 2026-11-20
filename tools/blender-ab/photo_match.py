#!/usr/bin/env python3
"""Row 5.6 (REALISM section 1): photo match. Statistics that say whether two pictures read alike without needing
them aligned pixel for pixel, so a render can be held against a real photo as well as against Cycles:
  contrast    std of log luminance, render over reference (1 = same spread of light and dark)
  hist        Wasserstein distance between the two luminance histograms (0 = same tonal distribution, 0..1)
  detail      per scale (1, 2, 4, 8, 16 px bands of a Laplacian pyramid): mean band energy, render over reference
              (1 = as much texture at that size; below 1 = smoother, above 1 = grain or harder edges)
Each is taken on the whole frame and on four quadrant crops; `worst` is the crop furthest from a match.

  python3 tools/blender-ab/photo_match.py reference.jpg render.png [--crop x0 y0 x1 y1]
The harness adds these to every case with "photo": True (truth = Cycles); a case also scored against the owner's own
photo passes that photo here with the crop it shares with the render.
"""
import argparse, json, sys
import numpy as np
from PIL import Image

SCALES = (1, 2, 4, 8, 16)


def luminance(rgb):
    """Linear luminance from an sRGB float picture (0..1)."""
    c = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4)
    return c[..., 0] * 0.2126 + c[..., 1] * 0.7152 + c[..., 2] * 0.0722


def _down(a):
    h, w = a.shape[0] // 2 * 2, a.shape[1] // 2 * 2
    a = a[:h, :w]
    return 0.25 * (a[0::2, 0::2] + a[1::2, 0::2] + a[0::2, 1::2] + a[1::2, 1::2])


def _up(a, shape):
    b = np.repeat(np.repeat(a, 2, 0), 2, 1)   # even size; an odd source gets its last row / column repeated
    return np.pad(b, ((0, shape[0] - b.shape[0]), (0, shape[1] - b.shape[1])), mode="edge")


def band_energy(l):
    """Mean absolute Laplacian band per scale, on display-referred (sRGB-like) values so shadows count as eyes see them."""
    g = l ** (1 / 2.2)
    out = []
    for _ in SCALES:
        d = _down(g)
        out.append(float(np.abs(g - _up(d, g.shape)).mean()))
        g = d
        if min(g.shape) < 4:
            break
    return out


def stats(l):
    lg = np.log(np.maximum(l, 1e-4))
    return {"std": float(lg.std()), "sorted": np.sort((l ** (1 / 2.2)).ravel()), "bands": band_energy(l)}


def compare_crop(ref, ren):
    a, b = stats(ref), stats(ren)
    n = min(len(a["sorted"]), len(b["sorted"]))
    qa = a["sorted"][np.linspace(0, len(a["sorted"]) - 1, n).astype(int)]
    qb = b["sorted"][np.linspace(0, len(b["sorted"]) - 1, n).astype(int)]
    return {"contrast": round(b["std"] / max(a["std"], 1e-6), 3),
            "hist": round(float(np.abs(qa - qb).mean()), 4),
            "detail": [round(y / max(x, 1e-6), 3) for x, y in zip(a["bands"], b["bands"])]}


def crops(l):
    h, w = l.shape
    return {"all": l, "tl": l[:h // 2, :w // 2], "tr": l[:h // 2, w // 2:], "bl": l[h // 2:, :w // 2], "br": l[h // 2:, w // 2:]}


def distance(c):
    """How far one crop is from a match: log ratios of contrast and detail plus the histogram distance."""
    return abs(np.log(c["contrast"])) + 4 * c["hist"] + float(np.mean([abs(np.log(max(d, 1e-3))) for d in c["detail"]]))


def photo_match(ref_rgb, ren_rgb):
    """Both sRGB float (h, w, 3) pictures; the render is resized to the reference when they differ."""
    if ref_rgb.shape != ren_rgb.shape:
        im = Image.fromarray((np.clip(ren_rgb, 0, 1) * 255).astype(np.uint8)).resize((ref_rgb.shape[1], ref_rgb.shape[0]), Image.LANCZOS)
        ren_rgb = np.asarray(im, np.float32) / 255
    lr, ln = luminance(ref_rgb), luminance(ren_rgb)
    cr, cn = crops(lr), crops(ln)
    per = {k: compare_crop(cr[k], cn[k]) for k in cr}
    worst = max((k for k in per if k != "all"), key=lambda k: distance(per[k]))
    return {**per["all"], "worst": worst, "worst_crop": per[worst], "distance": round(float(distance(per["all"])), 3)}


def load(path, crop=None):
    im = Image.open(path).convert("RGB")
    if crop:
        im = im.crop(tuple(crop))
    return np.asarray(im, np.float32) / 255


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("reference")
    ap.add_argument("render")
    ap.add_argument("--crop", nargs=4, type=int, help="x0 y0 x1 y1 on the reference (the render is resized to it)")
    a = ap.parse_args()
    print(json.dumps(photo_match(load(a.reference, a.crop), load(a.render))))


if __name__ == "__main__":
    sys.exit(main())
