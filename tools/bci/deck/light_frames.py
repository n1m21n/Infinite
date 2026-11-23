#!/usr/bin/env python3
"""Brain Light clip frames for the deck's animated finding slide.

One HEROIC session (sub014 remote, session 0), eyes open and eyes closed, the same columns of each 10 s
clip, rendered by tools/bci/brain_light.py at 640x400. Every 2nd engine column = 10.7 frames a second.

usage: light_frames.py CSV_DIR OUT_DIR [N_FRAMES]
"""
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import brain_light as bl  # noqa: E402
import direct_pins as dp  # noqa: E402

W, H, STEP, START = 640, 400, 2, 3.0


def set_size(w, h):
    bl.W, bl.H = w, h
    yy, xx = np.mgrid[0:h, 0:w]
    u, v = xx / (w - 1), yy / (h - 1) * (h / w)
    bl.DIST = np.stack([np.hypot(u - p[0], v - p[1] * (h / w)) for p in bl.POS])
    wt = 1.0 / (bl.DIST ** 2 + bl.EPS)
    bl.WT = wt / wt.sum(0)


def main(csv_dir, out, n=40):
    os.makedirs(out, exist_ok=True)
    set_size(W, H)
    for tag, xo, xc in dp.sessions(csv_dir):
        if not tag.startswith("sub014_remote_session0"):
            continue
        for st in ("open", "closed"):
            cal, x, s = bl.clip_state(xo, xc, st)
            nm = dp.Norm(dp.spectro(cal))
            c0 = int(START * bl.COL_RATE)
            for i, f in enumerate(bl.render(x, nm, range(c0, c0 + n * STEP, STEP), s)):
                Image.fromarray(f).save(os.path.join(out, f"{st}_{i:02d}.png"))
        print("frames from", tag, "->", out)
        return
    sys.exit("session sub014_remote_session0 not found")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 40)
