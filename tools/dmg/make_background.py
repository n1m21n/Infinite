#!/usr/bin/env python3
"""Draw the DMG window background (600x400 pt, written at 2x): an arrow from the app to Applications."""
import sys
from PIL import Image, ImageDraw, ImageFont

W, H, S = 600, 400, 2
out = sys.argv[1] if len(sys.argv) > 1 else "background.png"
img = Image.new("RGB", (W * S, H * S), (244, 245, 247))
d = ImageDraw.Draw(img)

def font(size):
    for p in ("/System/Library/Fonts/SFNS.ttf", "/System/Library/Fonts/Helvetica.ttc"):
        try:
            return ImageFont.truetype(p, size * S)
        except OSError:
            pass
    return ImageFont.load_default()

ink, soft = (40, 44, 58), (130, 136, 150)
text = "Drag Infinite to Applications"
f = font(18)
w = d.textlength(text, font=f)
d.text(((W * S - w) / 2, 52 * S), text, fill=ink, font=f)

# arrow between the two icons (icons sit at x=150 and x=450, y=190)
y = 190 * S
x0, x1 = 232 * S, 368 * S
d.line([(x0, y), (x1 - 10 * S, y)], fill=soft, width=3 * S)
d.polygon([(x1, y), (x1 - 16 * S, y - 10 * S), (x1 - 16 * S, y + 10 * S)], fill=soft)

f2 = font(12)
note = "If macOS blocks the first launch, double-click “Fix & Open Infinite”."
w2 = d.textlength(note, font=f2)
d.text(((W * S - w2) / 2, 352 * S), note, fill=soft, font=f2)
img.save(out, dpi=(72 * S, 72 * S))
