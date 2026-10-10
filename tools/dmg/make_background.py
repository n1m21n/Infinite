#!/usr/bin/env python3
"""Draw the DMG window background (600x400 pt, written at 2x): an arrow from the app to Applications.
Colours and type follow docs/brand (Paper mode, one Ember arrow, Geist)."""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

W, H, S = 600, 400, 2
out = sys.argv[1] if len(sys.argv) > 1 else "background.png"
img = Image.new("RGB", (W * S, H * S), (0xFB, 0xFA, 0xF6))  # Paper ground
d = ImageDraw.Draw(img)

FONTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "art", "brand", "fonts")


def font(size, weight="SemiBold"):
    for p in (os.path.join(FONTS, f"Geist-{weight}.ttf"), "/System/Library/Fonts/SFNS.ttf", "/System/Library/Fonts/Helvetica.ttc"):
        try:
            return ImageFont.truetype(p, size * S)
        except OSError:
            pass
    return ImageFont.load_default()

ink, soft, ember = (0x12, 0x15, 0x2A), (0x5A, 0x61, 0x80), (0xD9, 0x43, 0x0E)  # ink, ink-3, Ember 500 (marks)
text = "Drag Infinite to Applications"
f = font(18)
w = d.textlength(text, font=f)
d.text(((W * S - w) / 2, 52 * S), text, fill=ink, font=f)

# arrow between the two icons (icons sit at x=150 and x=450, y=190)
y = 190 * S
x0, x1 = 232 * S, 368 * S
d.line([(x0, y), (x1 - 10 * S, y)], fill=ember, width=3 * S)
d.polygon([(x1, y), (x1 - 16 * S, y - 10 * S), (x1 - 16 * S, y + 10 * S)], fill=ember)

f2 = font(12, "Medium")
note = "If macOS blocks the first launch, double-click “Fix & Open Infinite”."
w2 = d.textlength(note, font=f2)
d.text(((W * S - w2) / 2, 352 * S), note, fill=soft, font=f2)
img.save(out, dpi=(72 * S, 72 * S))
