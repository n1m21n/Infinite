"""Row 5.6, case lights_64: FLIP of R and L against T from the engine self-test's PPMs.
   INFINITE_ENGINETEST=1 INFINITE_ENGINETEST_LIGHTS64=<dir> Infinite ; python3 lights64.py <dir> [sheet.jpg]"""
import sys
import numpy as np
from PIL import Image
import flip_evaluator as flip

d = sys.argv[1]
rows = []
for t in ("64", "8"):
    im = {k: np.asarray(Image.open(f"{d}/{k}{t}.ppm").convert("RGB"), dtype=np.float32) / 255 for k in "RLT"}
    rows.append(np.concatenate([im[k] for k in "RLT"], 1))
    for k in "RL":
        _, m, _ = flip.evaluate(im["T"], im[k], "LDR")
        print("%s lights  %s  FLIP vs T %.4f  brightness %.3f" % (t, k, m, im[k].mean() / im["T"].mean()))
if len(sys.argv) > 2:
    Image.fromarray((np.concatenate(rows, 0) * 255).astype(np.uint8)).save(sys.argv[2], quality=90)
