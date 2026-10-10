#!/usr/bin/env python3
"""Render the component gallery (INFINITE_UIGALLERY) in light and dark and diff against goldens.
usage: golden.py [--update]      goldens: tests/ui-golden/<platform>/gallery-{light,dark}.png
Text rasterization differs per OS, so each platform keeps its own goldens. Exit 1 on any difference
above the tolerance (top 64 px, the live menu bar, is ignored) (mean abs channel diff 0.05, or more than 0.02 % of pixels changed by > 24)."""
import os, subprocess, sys, tempfile, platform
if platform.system() != "Darwin":   # goldens exist for macOS only, and the app path below is the macOS bundle
    print("gallery golden: macOS only, skipped on " + platform.system()); sys.exit(0)
from PIL import Image, ImageChops
root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
plat = {"Darwin": "macos", "Windows": "windows", "Linux": "linux"}[platform.system()]
gdir = os.path.join(root, "tests", "ui-golden", plat)
exe = os.path.join(root, "build/Infinite.app/Contents/MacOS/Infinite")
update = "--update" in sys.argv
os.makedirs(gdir, exist_ok=True)
bad = 0
for mode in ("light", "dark"):
    out = os.path.join(tempfile.mkdtemp(), f"gallery-{mode}.png")
    env = dict(os.environ, INFINITE_UIGALLERY=mode, INFINITE_NO_UPDATE_CHECK="1",
               INFINITE_SCREENSHOT_FRAME="20", IMAGERESYNTH_SCREENSHOT=out)
    subprocess.run([exe], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    if not os.path.exists(out):
        print(f"{mode}: render failed"); bad += 1; continue
    gold = os.path.join(gdir, f"gallery-{mode}.png")
    if update or not os.path.exists(gold):
        Image.open(out).convert("RGB").save(gold, optimize=True); print(f"{mode}: golden written"); continue
    a, b = Image.open(out).convert("RGB"), Image.open(gold).convert("RGB")
    if a.size != b.size:
        print(f"{mode}: size {a.size} != golden {b.size}"); bad += 1; continue
    d = ImageChops.difference(a, b).convert("L")
    d.paste(0, (0, 0, d.size[0], 64))   # the menu bar shows live values (fps, beat); the gallery sits below it
    hist = d.histogram(); n = a.size[0] * a.size[1]
    mean = sum(i * c for i, c in enumerate(hist)) / n
    big = sum(hist[25:]) / n
    ok = mean <= 0.05 and big <= 0.0002
    print(f"{mode}: mean diff {mean:.4f}, pixels >24: {big*100:.4f}%  {'ok' if ok else 'DIFFERS'}")
    if not ok:
        d.point(lambda v: 255 if v > 24 else 0).save(os.path.join(os.path.dirname(out), f"diff-{mode}.png"))
        print("  diff image:", os.path.join(os.path.dirname(out), f"diff-{mode}.png")); bad += 1
sys.exit(1 if bad else 0)
