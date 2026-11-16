#!/usr/bin/env python3
"""Before/after sheets for UI shots.

usage: shot_compare.py BEFORE_DIR AFTER_DIR OUT_DIR [--crop x,y,w,h ...]

For every NAME.png in both dirs writes OUT_DIR/NAME.png: before | after | changed pixels (red on the dimmed after).
Prints one line per shot with the share of changed pixels and the bounding box of the change, so a step that
moved nothing (or everything) shows without opening the image. --crop adds zoomed strips (2x) of each region.
"""
import argparse
import os
import sys

from PIL import Image, ImageChops, ImageDraw


def sheet(a, b, crops):
    w, h = max(a.width, b.width), max(a.height, b.height)
    a2 = Image.new("RGB", (w, h)); a2.paste(a, (0, 0))
    b2 = Image.new("RGB", (w, h)); b2.paste(b, (0, 0))
    diff = ImageChops.difference(a2, b2).convert("L").point(lambda v: 255 if v > 24 else 0)
    marked = Image.blend(b2, Image.new("RGB", (w, h)), 0.6)
    marked.paste(Image.new("RGB", (w, h), (255, 40, 40)), (0, 0), diff)
    rows = [[a2, b2, marked]]
    for (x, y, cw, ch) in crops:
        rows.append([im.crop((x, y, x + cw, y + ch)).resize((cw * 2, ch * 2), Image.NEAREST) for im in (a2, b2, marked)])
    gap = 8
    tw = max(sum(im.width for im in r) + gap * 2 for r in rows)
    th = sum(r[0].height for r in rows) + gap * (len(rows) - 1)
    out = Image.new("RGB", (tw, th), (40, 40, 40))
    yy = 0
    for r in rows:
        xx = 0
        for im in r:
            out.paste(im, (xx, yy)); xx += im.width + gap
        yy += r[0].height + gap
    hist = diff.histogram()
    share = hist[255] / float(w * h)
    return out, share, diff.getbbox()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("before"); p.add_argument("after"); p.add_argument("out")
    p.add_argument("--crop", action="append", default=[])
    p.add_argument("--scale", type=float, default=0.5, help="scale of the full-window row")
    a = p.parse_args()
    crops = [tuple(int(v) for v in c.split(",")) for c in a.crop]
    os.makedirs(a.out, exist_ok=True)
    names = sorted(set(os.listdir(a.before)) & set(os.listdir(a.after)))
    names = [n for n in names if n.endswith(".png")]
    if not names:
        sys.exit("no shots in both dirs")
    for n in names:
        im, share, bbox = sheet(Image.open(os.path.join(a.before, n)).convert("RGB"),
                                Image.open(os.path.join(a.after, n)).convert("RGB"), crops)
        if a.scale != 1.0:
            im = im.resize((int(im.width * a.scale), int(im.height * a.scale)), Image.LANCZOS)
        im.save(os.path.join(a.out, n))
        print(f"{n:28s} changed {share * 100:5.1f}%  box {bbox}")


if __name__ == "__main__":
    main()
