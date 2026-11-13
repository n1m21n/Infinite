#!/usr/bin/env python3
"""Pixel audit of one row of a 2D View shot: every chip / well box and where its ink sits inside it.

  python3 tools/ui/audit_view2d.py SHOT.png Y0 Y1 [X0 X1]

Y/X are pixels of the 2x shot (or its _2d crop). Boxes are column runs whose fill differs from the row background;
ink is the bright text/glyph inside. Flags boxes whose height differs from the row's most common height by more
than 2 px (borders anti-alias half a point out), square icon chips whose ink centre sits more than 1 px off the box
centre, and text boxes (wider than tall) whose ink top differs from the other text boxes' (same baseline, so
descenders do not count). Boxes far shorter than the row are colour dots and are skipped. Works on any build: it reads the picture, not the code.
"""
import sys
from collections import Counter

import numpy as np
from PIL import Image


def audit(path, y0, y1, x0=0, x1=None):
    a = np.asarray(Image.open(path).convert("L")).astype(int)[y0:y1, x0:x1]
    bg = int(np.median(a[:, :4]))
    filled = np.abs(a - bg) > 6
    cols = filled.sum(0) > (y1 - y0) * 0.3
    boxes, start = [], None
    for x, on in enumerate(list(cols) + [False]):
        if on and start is None:
            start = x
        elif not on and start is not None:
            if x - start >= 8:
                boxes.append((start, x))
            start = None
    rows = []
    for bx0, bx1 in boxes:
        mid = filled[:, bx0 + 2:bx1 - 2]
        ys = np.where(mid.sum(1) > (bx1 - bx0) * 0.5)[0]
        if len(ys) == 0:
            continue
        top, bot = ys.min(), ys.max()
        inner = a[top + 3:bot - 2, bx0 + 3:bx1 - 3]
        ink = np.where((inner > 140).sum(1) > 0)[0]
        off = inktop = None
        if len(ink):
            off = (ink.min() + ink.max()) / 2 + top + 3 - (top + bot) / 2
            inktop = ink.min() + 3
        rows.append((bx0 + x0, bx1 + x0, top + y0, bot + y0, off, inktop))
    common = Counter(r[3] - r[2] for r in rows).most_common(1)[0][0] if rows else 0
    texttops = Counter(r[5] for r in rows if r[5] is not None and (r[1] - r[0]) > 1.5 * (r[3] - r[2]))
    texttop = texttops.most_common(1)[0][0] if texttops else None
    bad = 0
    for x_0, x_1, t, b, off, it in rows:
        flags = []
        h = b - t
        if h < common * 0.7:
            print(f"x {x_0:5d}-{x_1:5d}  y {t}-{b}  dot")
            continue
        if abs(h - common) > 2:
            flags.append(f"height {h + 1} vs {common + 1}")
        istext = (x_1 - x_0) > 1.5 * h
        if istext and it is not None and texttop is not None and abs(it - texttop) > 1:
            flags.append(f"text top {it - texttop:+d} px")
        if not istext and off is not None and abs(off) > 1.0:
            flags.append(f"icon {off:+.1f} px")
        bad += bool(flags)
        kind = "text" if istext else "icon"
        print(f"x {x_0:5d}-{x_1:5d}  y {t}-{b}  {kind} top {it}  ctr {'' if off is None else f'{off:+.1f}':>5}  {'  '.join(flags)}")
    print(f"{len(rows)} boxes, {bad} flagged")
    return bad


if __name__ == "__main__":
    args = [int(v) for v in sys.argv[2:]]
    sys.exit(1 if audit(sys.argv[1], *args) else 0)
