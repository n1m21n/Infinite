#!/usr/bin/env python3
"""Writes docs/screenshots/v1/gallery-<name>.png for the README: a row of the manuals' own retina node shots
(docs/shots/raw/<Category>__<Node>__o.png, gitignored, so pass --shots when the checkout has none), each trimmed to
the node, tall ones cut to their top section, on the canvas colour.
    python3 tools/readme/node_gallery.py [--shots path/to/docs/shots/raw]
Needs PIL."""
import os, sys
from PIL import Image, ImageChops
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "docs", "screenshots", "v1")
BG = (21, 25, 48)
GALLERY = {
    "sound": ["Synths__Piano", "Synths__Analog", "Synths__Granular", "Synths__Generate_Samples"],
    "control": ["Modulators__LFO", "Modulators__Audio_Analyze", "Notes__Arpeggiator", "Notes__Note_Sequencer"],
}
H, GAP, PAD = 400, 24, 24

def trim(im):
    bg = im.getpixel((2, 2))
    box = ImageChops.difference(im, Image.new("RGB", im.size, bg)).point(lambda v: 255 if v > 14 else 0).getbbox()
    return im.crop(box) if box else im

shots = sys.argv[sys.argv.index("--shots") + 1] if "--shots" in sys.argv else os.path.join(ROOT, "docs", "shots", "raw")
for name, nodes in GALLERY.items():
    tiles = []
    for n in nodes:
        im = trim(Image.open(os.path.join(shots, n + "__o.png")).convert("RGB"))
        if im.height > 1.3 * im.width:
            im = im.crop((0, 0, im.width, round(1.3 * im.width)))
        k = H / im.height
        tiles.append(im.resize((max(1, round(im.width * k)), H), Image.LANCZOS))
    W = sum(t.width for t in tiles) + GAP * (len(tiles) - 1) + 2 * PAD
    sheet = Image.new("RGB", (W, H + 2 * PAD), BG)
    x = PAD
    for t in tiles:
        sheet.paste(t, (x, PAD)); x += t.width + GAP
    sheet.save(os.path.join(OUT, "gallery-%s.png" % name), optimize=True)
    print(name, sheet.size)
