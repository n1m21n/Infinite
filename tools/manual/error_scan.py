#!/usr/bin/env python3
"""Flag node shots that show an error state (red outline on the node border, as the app draws it when a node has
a problem, e.g. an unwired required input). Reads docs/shots/raw/*.png only; prints flagged files and the share of
red pixels on the outer border ring. Used after each capture pass so error states never reach the manual.

  python3 tools/manual/error_scan.py            # report
  python3 tools/manual/error_scan.py --list     # print flagged names only (for the wiring pass)
"""
import glob
import os
import sys
import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
RAW = os.path.join(ROOT, "docs", "shots", "raw")
RING = 24          # px inside the crop edge that holds the node outline
RED_FRAC = 0.04    # share of ring pixels that must read as error red


def red_share(path):
    a = np.asarray(Image.open(path).convert("RGB")).astype(int)
    h, w, _ = a.shape
    mask = np.zeros((h, w), bool)
    mask[:RING, :] = mask[-RING:, :] = True
    mask[:, :RING] = mask[:, -RING:] = True
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    red = (r > 150) & (g < 110) & (b < 110) & (r - g > 90)
    return float((red & mask).sum() / mask.sum())


def main():
    flagged = []
    rows = []
    for p in sorted(glob.glob(os.path.join(RAW, "*.png"))):
        s = red_share(p)
        rows.append((s, os.path.basename(p)))
        if s >= RED_FRAC:
            flagged.append(os.path.basename(p))
    if "--list" in sys.argv:
        print("\n".join(flagged))
    else:
        for s, n in rows:
            if s >= RED_FRAC:
                print(f"FLAG {s:6.3f}  {n}")
        print(f"{len(rows)} scanned, {len(flagged)} flagged (red border share >= {RED_FRAC})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
