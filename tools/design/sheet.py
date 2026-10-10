#!/usr/bin/env python3
"""Contact sheet of every glyph from the built TTF: 16/20/24 px, light and dark. Usage: sheet.py out.png (needs pillow)."""
import json, pathlib, sys
from PIL import Image, ImageDraw, ImageFont
ROOT = pathlib.Path(__file__).resolve().parents[2]
reg = json.loads((ROOT / "art/icons/codepoints.json").read_text())
ttf = str(ROOT / "external/icons/Infinite/infinite-glyphs.ttf")
names = list(reg)
S = 3  # supersample so the sheet shows shape, not hinting
cw, rh = 150, 72
W, H = cw * 6, rh * ((len(names) + 5) // 6) * 2 + 20
img = Image.new("RGB", (W, H), "white"); d = ImageDraw.Draw(img)
half = H // 2
d.rectangle([0, half, W, H], fill=(24, 26, 34))
lab = ImageFont.load_default()
for i, n in enumerate(names):
    cx, cy = (i % 6) * cw, (i // 6) * rh
    for theme, (bg, fg, y0) in enumerate((("white", (20, 22, 30), 0), ((24, 26, 34), (235, 238, 245), half))):
        x = cx + 8
        for px in (16, 20, 24):
            f = ImageFont.truetype(ttf, px)
            d.text((x, y0 + cy + 8 + (24 - px) // 2), chr(reg[n]), font=f, fill=fg)
            x += px + 14
        d.text((cx + 8, y0 + cy + 44), n, font=lab, fill=fg)
out = sys.argv[1] if len(sys.argv) > 1 else "glyph-sheet.png"
img.save(out); print("wrote", out)
