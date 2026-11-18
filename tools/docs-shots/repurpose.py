#!/usr/bin/env python3
"""One master capture per panel, repurposed for every channel.

Masters live in docs/screenshots/v2/<name>.png (3200x2000, from docs/showcase/shoot.sh). This script derives:
  README / GitHub profile   the master itself (already referenced from docs/screenshots/v2)
  website showcase          website/assets/showcase/<name>.webp, 1800x1125
  manuals (Interface)       docs/ui_screenshots/<manual id>.png, a crop of the master (box in a 1000-wide preview)
Run after any re-shoot:  python3 tools/docs-shots/repurpose.py [--check]
--check exits 1 when a derived file is older than its master (the CI-style drift test)."""
import os
import sys

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
V2 = os.path.join(ROOT, "docs", "screenshots", "v2")
WEB = os.path.join(ROOT, "website", "assets", "showcase")
MAN = os.path.join(ROOT, "docs", "ui_screenshots")

WEBSITE = ["2d-view", "3d-view", "drums", "field-patcher", "hero-panels", "mixer", "mod-matrix", "node-graph",
           "performance", "piano-roll", "sample-studio", "session-clips", "terminal", "timeline"]
# manual id -> (master, crop box in a 1000-wide preview of the master)
MANUAL = {
    "panel_mixer": ("mixer", (0, 435, 1000, 620)),
    "panel_drums": ("drums", (0, 38, 1000, 275)),
    "panel_fieldpatcher": ("field-patcher", (0, 35, 1000, 330)),
    "panel_view2d": ("2d-view", (0, 35, 1000, 312)),
    "panel_curve": ("curve-editor", (0, 35, 1000, 245)),
}


def main():
    check = "--check" in sys.argv
    stale = []
    for n in WEBSITE:
        src, dst = os.path.join(V2, n + ".png"), os.path.join(WEB, n + ".webp")
        if not os.path.exists(src):
            continue
        if check:
            if not os.path.exists(dst) or os.path.getmtime(dst) < os.path.getmtime(src) - 1:
                stale.append(dst)
            continue
        Image.open(src).convert("RGB").resize((1800, 1125), Image.LANCZOS).save(dst, "WEBP", quality=84, method=6)
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
    print("repurposed", len(WEBSITE), "website images and", len(MANUAL), "manual crops")
    return 0


if __name__ == "__main__":
    sys.exit(main())
