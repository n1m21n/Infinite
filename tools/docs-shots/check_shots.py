#!/usr/bin/env python3
"""Blank-area gate for derived shots. Splits each image into a 4x4 grid; a cell is blank when its grey
pixel std-dev is below 4. An image FAILS with more than 5 of 16 blank cells.
Usage: python3 tools/docs-shots/check_shots.py [file ...]   (no files = every README crop and website webp)"""
import glob
import os
import sys

import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def blank_cells(path):
    g = np.asarray(Image.open(path).convert("L"), dtype=np.float32)
    h, w = g.shape
    blank = 0
    for i in range(4):
        for j in range(4):
            cell = g[i * h // 4:(i + 1) * h // 4, j * w // 4:(j + 1) * w // 4]
            if cell.std() < 4:
                blank += 1
    return blank


def main(paths):
    if not paths:
        paths = sorted(glob.glob(os.path.join(ROOT, "docs", "screenshots", "v2", "crop", "*.png")) +
                       glob.glob(os.path.join(ROOT, "website", "assets", "showcase", "*.webp")))
    fails = 0
    for p in paths:
        n = blank_cells(p)
        verdict = "FAIL" if n > 5 else "ok"
        fails += n > 5
        print(f"{verdict:4} {n:2}/16 blank  {os.path.relpath(p, ROOT)}")
    print("blank-area gate:", "FAILED" if fails else "passed", f"({len(paths)} images)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
