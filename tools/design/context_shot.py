#!/usr/bin/env python3
"""Render the real app headless and crop design surfaces for in-context review.
usage: context_shot.py <outdir> [panels]   (panels: arrange,modmatrix,perf,viewport)
Crops are 2x pixels; each is upscaled 2x more so small glyphs are legible."""
import os, subprocess, sys
from PIL import Image
out = sys.argv[1]; panels = sys.argv[2] if len(sys.argv) > 2 else "arrange"
os.makedirs(out, exist_ok=True)
shot = os.path.join(out, "full.png")
env = dict(os.environ, INFINITE_OPENPANELS=panels, INFINITE_NO_UPDATE_CHECK="1",
           IMAGERESYNTH_SCREENSHOT=shot, INFINITE_SHOWCASE="1")
subprocess.run(["build/Infinite.app/Contents/MacOS/Infinite"], env=env,
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
im = Image.open(shot); W, H = im.size
crops = {  # name: (x0,y0,x1,y1) in 3200x2000 px
  "topbar_left":  (0, 0, 1700, 60),
  "topbar_right": (2000, 0, 3200, 60),
  "arrange_header": (0, 1530, 1100, 1630),
  "node_toggles": (900, 660, 1900, 800),
}
for n, b in crops.items():
    c = im.crop(b); c = c.resize((c.width * 2, c.height * 2), Image.LANCZOS)
    c.save(os.path.join(out, n + ".png"))
print("wrote", out)
