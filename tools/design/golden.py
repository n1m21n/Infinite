#!/usr/bin/env python3
"""Render the component gallery (INFINITE_UIGALLERY) in light and dark and diff against goldens.
usage:
  golden.py [--update] [--binary PATH] [--render-out DIR]
  golden.py --compare CAND_DIR GOLD_DIR

Goldens live at tests/ui-golden/<platform>/gallery-{light,dark}.png (macos, windows, linux).
Text rasterization differs per OS, so each platform keeps its own goldens. Exit 1 on any difference
above the tolerance (top 64 px, the live menu bar, is ignored) (mean abs channel diff 0.05, or more
than 0.02 % of pixels changed by > 24).
"""
import argparse, os, platform, subprocess, sys, tempfile
from PIL import Image, ImageChops

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PLAT = {"Darwin": "macos", "Windows": "windows", "Linux": "linux"}.get(platform.system(), platform.system().lower())


def default_binary():
    if os.environ.get("INFINITE_BIN"):
        return os.environ["INFINITE_BIN"]
    if platform.system() == "Darwin":
        return os.path.join(ROOT, "build/Infinite.app/Contents/MacOS/Infinite")
    if platform.system() == "Windows":
        return os.path.join(ROOT, "build/Release/Infinite.exe")
    for cand in ("build-linux/Infinite", "build/Infinite"):
        p = os.path.join(ROOT, cand)
        if os.path.exists(p):
            return p
    return os.path.join(ROOT, "build-linux/Infinite")


def diff_images(cand_path, gold_path, diff_out):
    a, b = Image.open(cand_path).convert("RGB"), Image.open(gold_path).convert("RGB")
    if a.size != b.size:
        return False, f"size {a.size} != golden {b.size}"
    d = ImageChops.difference(a, b).convert("L")
    d.paste(0, (0, 0, d.size[0], 64))   # the menu bar shows live values (fps, beat); the gallery sits below it
    hist = d.histogram()
    n = a.size[0] * a.size[1]
    mean = sum(i * c for i, c in enumerate(hist)) / n
    big = sum(hist[25:]) / n
    ok = mean <= 0.05 and big <= 0.0002
    if not ok and diff_out:
        d.point(lambda v: 255 if v > 24 else 0).save(diff_out)
    return ok, f"mean diff {mean:.4f}, pixels >24: {big*100:.4f}%  {'ok' if ok else 'DIFFERS'}"


def compare_dirs(cand_dir, gold_dir):
    bad = 0
    for mode in ("light", "dark"):
        cand = os.path.join(cand_dir, f"gallery-{mode}.png")
        gold = os.path.join(gold_dir, f"gallery-{mode}.png")
        if not os.path.exists(cand):
            print(f"{mode}: missing candidate {cand}"); bad += 1; continue
        if not os.path.exists(gold):
            print(f"{mode}: missing golden {gold}"); bad += 1; continue
        ok, msg = diff_images(cand, gold, os.path.join(cand_dir, f"diff-{mode}.png"))
        print(f"{mode}: {msg}")
        if not ok:
            bad += 1
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("--update", action="store_true", help="Write/overwrite goldens for this platform")
    ap.add_argument("--binary", default=None, help="Path to Infinite binary")
    ap.add_argument("--render-out", default=None, help="Also save rendered PNGs into this directory (CI artifacts)")
    ap.add_argument("--compare", nargs=2, metavar=("CAND_DIR", "GOLD_DIR"), help="Compare two directories without rendering")
    args = ap.parse_args()

    if args.compare:
        return compare_dirs(args.compare[0], args.compare[1])

    gdir = os.path.join(ROOT, "tests", "ui-golden", PLAT)
    exe = args.binary or default_binary()
    if args.update:
        os.makedirs(gdir, exist_ok=True)
    if args.render_out:
        os.makedirs(args.render_out, exist_ok=True)

    bad = 0
    for mode in ("light", "dark"):
        tmp_dir = tempfile.mkdtemp()
        out = os.path.join(args.render_out, f"gallery-{mode}.png") if args.render_out else os.path.join(tmp_dir, f"gallery-{mode}.png")
        env = dict(os.environ, INFINITE_UIGALLERY=mode, INFINITE_NO_UPDATE_CHECK="1",
                   INFINITE_SCREENSHOT_FRAME="20", IMAGERESYNTH_SCREENSHOT=out)
        subprocess.run([exe], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        if not os.path.exists(out):
            print(f"{mode}: render failed"); bad += 1; continue
        gold = os.path.join(gdir, f"gallery-{mode}.png")
        if args.update:
            Image.open(out).convert("RGB").save(gold, optimize=True)
            print(f"{mode}: golden written ({gold})")
            continue
        if not os.path.exists(gold):
            print(f"{mode}: no committed golden at {gold}; rendered to {out}")
            continue
        ok, msg = diff_images(out, gold, os.path.join(os.path.dirname(out), f"diff-{mode}.png"))
        print(f"{mode}: {msg}")
        if not ok:
            print("  diff image:", os.path.join(os.path.dirname(out), f"diff-{mode}.png"))
            bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
