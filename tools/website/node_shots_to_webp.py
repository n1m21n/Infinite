#!/usr/bin/env python3
"""Compress node screenshots into the website's node-deck pictures.

Input : PNGs from scripts/node_screenshot.py (zoomed captures of a node in the running Infinite app), one folder or several.
Output: website/assets/nodes/hd/<slug>.webp, 720 px tall at most, quality 82 (about 15 KB each).
Use   : python3 tools/website/node_shots_to_webp.py DIR [DIR ...]
The file name "Field Modifier.png" becomes "field-modifier.webp". tools/website/gen_audiences.py lists which ones each page shows (NODE_DECK).
"""
import glob, os, re, sys
from PIL import Image

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "website", "assets", "nodes", "hd")


def main(dirs):
    os.makedirs(OUT, exist_ok=True)
    n = 0
    for d in dirs:
        for p in sorted(glob.glob(os.path.join(d, "*.png"))):
            slug = re.sub(r"[^a-z0-9]+", "-", os.path.basename(p)[:-4].lower()).strip("-")
            im = Image.open(p).convert("RGBA")
            if im.size[1] > 720:
                im = im.resize((round(im.size[0] * 720 / im.size[1]), 720), Image.LANCZOS)
            im.save(os.path.join(OUT, slug + ".webp"), "WEBP", quality=82, method=6)
            n += 1
    print("wrote %d pictures to %s" % (n, os.path.normpath(OUT)))


if __name__ == "__main__":
    main(sys.argv[1:] or [])
