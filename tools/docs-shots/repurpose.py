#!/usr/bin/env python3
"""One master capture per panel, repurposed for every channel.

Masters live in docs/screenshots/v2/<name>.png (3200x2000, from docs/showcase/shoot.sh). This script derives:
  README / GitHub profile   the master itself (already referenced from docs/screenshots/v2)
  README and website crops  docs/screenshots/v2/crop/<name>.png and website/assets/showcase/<name>.webp
  manuals (Interface)       docs/ui_screenshots/<manual id>.png, a crop of the master (box in a 1000-wide preview)
Crops (README tiles and website cards) come from the CROP table below. Boxes are in pixels of the 3200-wide
master (x0, y0, x1, y1) and must be 16:10 (grid tiles) or 16:9 (hero). Every crop is resized by the same factor
(SCALE = 0.5, so one zoom across the set) and is never upscaled.
Run after any re-shoot:  python3 tools/docs-shots/repurpose.py [--check]   then   python3 tools/docs-shots/check_shots.py
--check exits 1 when a derived file is older than its master (the CI-style drift test)."""
import os
import sys

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
V2 = os.path.join(ROOT, "docs", "screenshots", "v2")
WEB = os.path.join(ROOT, "website", "assets", "showcase")
MAN = os.path.join(ROOT, "docs", "ui_screenshots")

SCALE = 0.5  # one zoom for every derived image: master px x 0.5
# name -> (master, box in 3200-wide master px, aspect). Filled from the reshot masters.
CROP = {}

# manual id -> (master, crop box in a 1000-wide preview of the master)
MANUAL = {
    "panel_mixer": ("mixer", (0, 435, 1000, 620)),
    "panel_drums": ("drums", (0, 38, 1000, 275)),
    "panel_fieldpatcher": ("field-patcher", (0, 35, 1000, 330)),
    "panel_view2d": ("2d-view", (0, 35, 1000, 312)),
    "panel_curve": ("curve-editor", (0, 35, 1000, 245)),
}


def crops(check, stale):
    for name, (master, box, aspect) in CROP.items():
        src = os.path.join(V2, master + ".png")
        if not os.path.exists(src):
            continue
        dsts = [os.path.join(V2, "crop", name + ".png"), os.path.join(WEB, name + ".webp")]
        if check:
            stale += [d for d in dsts if not os.path.exists(d) or os.path.getmtime(d) < os.path.getmtime(src) - 1]
            continue
        im = Image.open(src).convert("RGB")
        k = im.size[0] / 3200
        x0, y0, x1, y1 = (int(v * k) for v in box)
        w, h = x1 - x0, y1 - y0
        if abs(w / h - aspect) > 0.01:
            raise SystemExit(f"{name}: box is {w}x{h}, not {aspect:.3f}")
        tile = im.crop((x0, y0, x1, y1)).resize((round(w * SCALE), round(h * SCALE)), Image.LANCZOS)
        os.makedirs(os.path.join(V2, "crop"), exist_ok=True)
        tile.save(dsts[0], optimize=True)
        tile.save(dsts[1], "WEBP", quality=84, method=6)


def main():
    check = "--check" in sys.argv
    stale = []
    crops(check, stale)
    for mid, (n, box) in MANUAL.items():
        src, dst = os.path.join(V2, n + ".png"), os.path.join(MAN, mid + ".png")
        if not os.path.exists(src):
            continue
        if check:
            if not os.path.exists(dst) or os.path.getmtime(dst) < os.path.getmtime(src) - 1:
                stale.append(dst)
            continue
        im = Image.open(src)
        k = im.size[0] / 1000
        im.crop(tuple(int(v * k) for v in box)).save(dst, optimize=True)
    if check:
        print("\n".join("stale: " + os.path.relpath(s, ROOT) for s in stale) or "repurpose: in sync")
        return 1 if stale else 0
    print("repurposed", len(CROP), "crops (README and website) and", len(MANUAL), "manual crops")
    return 0


if __name__ == "__main__":
    sys.exit(main())
