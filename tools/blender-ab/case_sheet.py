"""Owner-review sheet: one row per case, columns truth (Cycles), R (GL raster), L (Metal live), T (settle),
from the last sweep's images in ~/.cache/infinite-sweep/ab/<case>/.

    python3 case_sheet.py out.jpg wear_groove [more cases]     (--no-truth drops the Cycles column)
"""
import os, sys
from PIL import Image, ImageDraw

AB = os.path.expanduser("~/.cache/infinite-sweep/ab")
COLS = (("Cycles", "truth.png"), ("R raster", "inf.png"), ("L live", "inf_live.png"), ("T settle", "inf_settle.png"))


def main(out, cases):
    global COLS
    if "--no-truth" in cases:
        cases = [c for c in cases if c != "--no-truth"]
        COLS = COLS[1:]
    tiles = [[Image.open(os.path.join(AB, c, f)).convert("RGB") if os.path.exists(os.path.join(AB, c, f)) else None
              for _, f in COLS] for c in cases]
    w, h = next(t for row in tiles for t in row if t).size
    top, left = 28, 150
    sheet = Image.new("RGB", (left + w * len(COLS), top + h * len(cases)), (24, 24, 24))
    d = ImageDraw.Draw(sheet)
    for i, (name, _) in enumerate(COLS):
        d.text((left + i * w + 8, 8), name, fill=(230, 230, 230))
    for r, (c, row) in enumerate(zip(cases, tiles)):
        d.text((8, top + r * h + h // 2), c, fill=(230, 230, 230))
        for i, t in enumerate(row):
            if t:
                sheet.paste(t.resize((w, h)), (left + i * w, top + r * h))
    sheet.save(out, quality=88)
    print(out, sheet.size)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2:])
