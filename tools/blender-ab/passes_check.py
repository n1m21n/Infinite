#!/usr/bin/env python3
"""Row 7.3 check: Render 3D's pass outputs against Cycles' passes on one sweep case.

  python3 tools/blender-ab/passes_check.py [case]      (default cornell; the case must have been swept once)

Blender renders world normal, diffuse colour and world position (blender_scene.py with AB_PASSES=1). Infinite
renders the case's patch three times with `passes` on and its Output cabled to the depth, normal and albedo pins.
Both sides go through the same mapping (depth: (distance - near) / (far - near) with Render 3D's near and far, also
reported in metres; normal: n * 0.5 + 0.5 in Infinite's Y-up frame) and the mean absolute error is printed per
pass over the pixels both sides cover. Gate (design 7.3): MAE under 0.02.
"""
import os
import subprocess
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
BLENDER = "/Applications/Blender.app/Contents/MacOS/Blender"
INFINITE = os.environ.get("INFINITE_BIN", os.path.join(REPO, "build/Infinite.app/Contents/MacOS/Infinite"))
NEAR, FAR = 0.05, 100.0  # Render 3D's nearPlane / farPlane defaults (Geometry3DNodes.h)


def blender_passes(d):
    if not os.path.exists(os.path.join(d, "pass_Position.npy")):
        env = dict(os.environ, AB_PASSES="1")
        r = subprocess.run([BLENDER, "-b", "--factory-startup", "--python", os.path.join(HERE, "blender_scene.py"), "--", d],
                           env=env, capture_output=True, text=True, timeout=600)
        if "PASSESDONE" not in r.stdout:
            sys.exit("blender passes failed:\n" + r.stdout[-2000:] + r.stderr[-2000:])
    return {k: np.load(os.path.join(d, "pass_%s.npy" % k)) for k in ("Normal", "DiffCol", "Position", "cam")}


def infinite_pass(d, pin, w, h):
    src = open(os.path.join(d, "inf.inf")).read().splitlines()
    out, render, output = [], None, None
    for line in src:
        t = line.split()
        if line.startswith("node ") and line.endswith("Render 3D"):
            render = t[1]
        if line.startswith("node ") and line.endswith("Output"):
            output = t[1]
        if line.startswith("cable ") and output and t[1] == output:
            continue
        out.append(line)
        if render and line.startswith("node ") and t[1] == render:
            out.append("  b passes 1")
    out.append("cable %s 0 %s %d" % (output, render, pin))
    patch = os.path.join(d, "pass_%d.inf" % pin)
    open(patch, "w").write("\n".join(out) + "\n")
    fd = os.path.join(d, "pass_%d" % pin)
    os.makedirs(fd, exist_ok=True)
    r = subprocess.run([INFINITE, "--frame", patch, "0", fd + "/", "--size", "%dx%d" % (w, h)],
                       capture_output=True, text=True, timeout=300)
    pngs = sorted(f for f in os.listdir(fd) if f.endswith(".png"))
    if not pngs:
        sys.exit("infinite pass %d failed:\n%s" % (pin, r.stdout[-2000:]))
    return np.asarray(Image.open(os.path.join(fd, pngs[-1])).convert("RGBA"), dtype=np.float32) / 255.0


def main():
    case = sys.argv[1] if len(sys.argv) > 1 else "cornell"
    d = os.path.join(os.path.expanduser("~/.cache/infinite-sweep/ab"), case)
    b = blender_passes(d)
    h, w = b["Normal"].shape[:2]
    pos, cam = b["Position"], b["cam"]
    hit = np.linalg.norm(b["Normal"], axis=2) > 0.5          # Cycles writes 0 where the camera ray met nothing
    dist = np.linalg.norm(pos - cam[None, None, :], axis=2)
    nb = b["Normal"][..., [0, 2, 1]] * np.array([1, 1, -1])   # Blender Z-up -> Infinite Y-up (Blender +Y = -Z)
    want = {
        1: (np.clip((dist - NEAR) / (FAR - NEAR), 0, 1)[..., None], "depth"),
        2: (nb * 0.5 + 0.5, "normal"),
        3: (b["DiffCol"], "albedo"),
    }
    print("%-8s %8s %8s  %s" % ("pass", "MAE", "pixels", "note"))
    worst = 0.0
    for pin, (ref, name) in want.items():
        img = infinite_pass(d, pin, w, h)
        got = img[..., :ref.shape[2]]
        m = hit & (img[..., 3] > 0.5)
        mae = float(np.abs(got - ref)[m].mean())
        note = ""
        if pin == 1:
            note = "in metres %.3f" % float(np.abs(got[..., 0] * (FAR - NEAR) + NEAR - dist)[m].mean())
        print("%-8s %8.4f %8d  %s" % (name, mae, int(m.sum()), note))
        worst = max(worst, mae)
    print("PASSESCHECK %s worst %.4f %s" % (case, worst, "ok" if worst < 0.02 else "over 0.02"))


if __name__ == "__main__":
    main()
