#!/usr/bin/env python3
"""Score a path-traced image against a Blender A/B case's Cycles truth.

Generates (or reuses when scene.hash matches) the case's Cycles truth via
blender_scene.py, computes FLIP, brightness ratio and mean linear luma error
using harness.py's definitions, and writes a 3-panel comparison board PNG.
When Blender or flip_evaluator is missing, prints "skipped (blender not found)"
and exits cleanly so the render self-test still passes on machines without
Blender installed.
"""
import argparse
import hashlib
import json
import os
import shutil
import sys
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from cases import CASES  # noqa: E402
from harness import BASELINE, load, luma, run  # noqa: E402


def find_blender():
    env_b = os.environ.get("BLENDER")
    if env_b is not None:
        if not env_b:
            return None
        w = shutil.which(env_b)
        if w:
            return w
        return env_b if (os.path.isfile(env_b) and os.access(env_b, os.X_OK)) else None
    w = shutil.which("blender") or shutil.which("Blender")
    if w:
        return w
    path_env = os.environ.get("PATH", "")
    mac_blender = "/Applications/Blender.app/Contents/MacOS/Blender"
    if ("/opt/homebrew" in path_env or "/usr/local" in path_env) and os.path.isfile(mac_blender) and os.access(mac_blender, os.X_OK):
        return mac_blender
    return None


def write_skip(metrics_path):
    print("skipped (blender not found)", flush=True)
    if metrics_path:
        with open(metrics_path, "w") as f:
            f.write("SKIP\n")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, help="Output directory holding <case>/truth.png and scene.hash")
    ap.add_argument("--case", default="cornell", help="Case name in cases.py")
    ap.add_argument("--image", required=True, help="Path to rendered PNG to score")
    ap.add_argument("--board", required=True, help="Path to write comparison board PNG")
    ap.add_argument("--metrics", required=True, help="Path to write machine-readable metrics")
    args = ap.parse_args()

    blender_bin = find_blender()
    if blender_bin is None:
        return write_skip(args.metrics)

    try:
        import flip_evaluator as flip
    except ImportError:
        return write_skip(args.metrics)

    if args.case not in CASES:
        sys.exit("unknown case: %s" % args.case)

    scene = dict(CASES[args.case])
    case_dir = os.path.join(os.path.abspath(args.out), args.case)
    os.makedirs(case_dir, exist_ok=True)
    text = json.dumps(scene, sort_keys=True, indent=1)
    h = hashlib.sha1(text.encode()).hexdigest()
    hp = os.path.join(case_dir, "scene.hash")
    truth_p = os.path.join(case_dir, "truth.png")

    if not (os.path.exists(hp) and open(hp).read() == h and os.path.exists(truth_p)):
        with open(os.path.join(case_dir, "scene.json"), "w") as f:
            f.write(text)
        _, log = run([blender_bin, "-b", "--factory-startup", "--python", os.path.join(HERE, "blender_scene.py"), "--", case_dir])
        if "SCENEDONE" not in log:
            sys.exit("blender failed on %s:\n%s" % (args.case, log[-3000:]))
        with open(hp, "w") as f:
            f.write(h)

    ref = load(truth_p)
    test = load(args.image)
    if ref.shape != test.shape:
        sys.exit("size mismatch: truth %s vs image %s" % (ref.shape, test.shape))

    err_map, mean_flip, _ = flip.evaluate(ref, test, "LDR", applyMagma=True)
    lt, li = luma(ref), luma(test)
    luma_err = float(np.abs(li - lt).mean())
    ratio = float(li.mean() / max(float(lt.mean()), 1e-6))

    base = json.load(open(BASELINE)) if os.path.exists(BASELINE) else {}
    raster_flip = float(base.get(args.case, 0.583))

    h_px, w_px, _ = ref.shape
    heat_im = Image.fromarray((np.clip(err_map, 0.0, 1.0) * 255.0).astype(np.uint8))
    board = Image.new("RGB", (w_px * 3, h_px + 32), (18, 18, 20))
    board.paste(Image.open(truth_p).convert("RGB"), (0, 32))
    board.paste(Image.open(args.image).convert("RGB"), (w_px, 32))
    board.paste(heat_im, (w_px * 2, 32))
    draw = ImageDraw.Draw(board)
    draw.text((10, 8), "Cycles Truth (64 spp)", fill=(230, 230, 230))
    draw.text((w_px + 10, 8), "Infinite Path Trace (256 spp)  ratio=%.2fx" % ratio, fill=(230, 230, 230))
    draw.text((w_px * 2 + 10, 8), "FLIP=%.3f (raster=%.3f)" % (mean_flip, raster_flip), fill=(230, 230, 230))
    os.makedirs(os.path.dirname(os.path.abspath(args.board)), exist_ok=True)
    board.save(args.board)

    with open(args.metrics, "w") as f:
        f.write("OK %.6f %.6f %.6f %.6f\n" % (mean_flip, ratio, luma_err, raster_flip))
    return 0


if __name__ == "__main__":
    sys.exit(main())
