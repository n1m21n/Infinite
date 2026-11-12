"""Row 4.4 gate: per head, the pixel correlation between Cycles' wear truth and each Infinite engine.
wear_masks puts one monkey per image half on black; a pixel counts when either picture is lit there."""
import os
import sys

import numpy as np
from PIL import Image

D = os.path.expanduser("~/.cache/infinite-sweep/ab/wear_masks")


def load(n):
    p = os.path.join(D, n)
    return np.asarray(Image.open(p).convert("L"), dtype=np.float64) / 255.0 if os.path.exists(p) else None


t = load("truth.png")
for name in sys.argv[1:] or ["inf.png", "inf_live.png", "inf_settle.png"]:
    a = load(name)
    if a is None or t is None:
        print(name, "missing"); continue
    h, w = t.shape
    out = []
    for side, sl in (("left", slice(0, w // 2)), ("right", slice(w // 2, w))):
        x, y = t[:, sl], a[:, sl]
        m = (x > 0.01) | (y > 0.01)
        r = np.corrcoef(x[m], y[m])[0, 1] if m.sum() > 10 else float("nan")
        out.append("%s %.3f (%d px, mean %.2f vs %.2f)" % (side, r, m.sum(), x[m].mean(), y[m].mean()))
    print("%-16s %s" % (name, " | ".join(out)))
