#!/usr/bin/env python3
"""Writes assets/templates/thumbs/<slug>.png for the Starters: the manuals' own retina screenshot of one relevant
node (docs/shots/raw/<Category>__<Node>__o.png, written by tools/docs-shots; gitignored, so pass --shots when the
checkout has none) trimmed to the node and centred on the canvas colour in a 320 x 320 tile (owner's call, 2026-10-31).
    python3 tools/templates/node_thumbs.py [--shots path/to/docs/shots/raw]
The Showcase tile is a screenshot of the patch itself (shoot_thumbs.py). Needs PIL."""
import os, sys
from PIL import Image, ImageChops
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SHOTS = os.path.join(ROOT, "docs", "shots", "raw")
THUMBS = os.path.join(ROOT, "assets", "templates", "thumbs")
BG = (21, 25, 48)
NODE = {   # starter slug -> the node that shows what the starter is about
    "first-sound": "Synths__Analog", "first-image": "Source__Shape", "first-music-image": "Modulators__Audio_Analyze",
    "source": "Source__Noise", "compositing": "Compositing__Trails", "effects": "Effects__kaleidoscope",
    "3d": "3D__Render_3D", "modulators": "Modulators__LFO", "macros": "Macros__Macro_Knob",
    "notes": "Notes__Note_Sequencer", "synths": "Synths__FM", "audio-effects": "AudioEffects__Reverb",
    "prediction": "Prediction__Predictive_Notes", "utility": "Utility__Mixer", "field": "Source__FieldPixel",
}

def trim(im):
    """Cut the canvas away from the node: everything that differs from the corner colour."""
    bg = im.getpixel((2, 2))
    box = ImageChops.difference(im, Image.new("RGB", im.size, bg)).point(lambda v: 255 if v > 14 else 0).getbbox()
    return im.crop(box) if box else im

shots = SHOTS
if "--shots" in sys.argv:
    shots = sys.argv[sys.argv.index("--shots") + 1]
for slug, node in NODE.items():
    im = trim(Image.open(os.path.join(shots, node + "__o.png")).convert("RGB"))
    if im.height > 1.2 * im.width:      # a tall node: its top section (header, preview, first controls) reads better than all of it
        im = im.crop((0, 0, im.width, round(1.2 * im.width)))
    k = min(272 / im.width, 272 / im.height)
    im = im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS)
    tile = Image.new("RGB", (320, 320), BG)
    tile.paste(im, ((320 - im.width) // 2, (320 - im.height) // 2))
    tile.save(os.path.join(THUMBS, slug + ".png"))
    print(slug, "<-", node)
