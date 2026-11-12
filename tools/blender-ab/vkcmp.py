import sys, os, numpy as np
from PIL import Image
import flip_evaluator as flip
a, b = [os.path.expanduser("~/.cache/infinite-sweep/img/" + t) for t in sys.argv[1:3]]
lim = float(sys.argv[3]) if len(sys.argv) > 3 else 0.01
ok = n = 0; worst = []
for f in sorted(set(os.listdir(a)) & set(os.listdir(b))):
    if not f.endswith(".png"): continue
    x = np.asarray(Image.open(os.path.join(a, f)).convert("RGB"), dtype=np.float32) / 255
    y = np.asarray(Image.open(os.path.join(b, f)).convert("RGB"), dtype=np.float32) / 255
    v = 1.0 if x.shape != y.shape else float(flip.evaluate(x, y, "LDR")[1])
    n += 1; ok += v <= lim; worst.append((v, f[:-4]))
worst.sort(reverse=True)
print("%d of %d within %.3f" % (ok, n, lim))
for v, f in worst[:12]: print("%-24s %.4f" % (f, v))
