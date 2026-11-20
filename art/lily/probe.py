# One cheap settle probe of the tile scene: python3 art/lily/probe.py NAME VIEW [WxH] [SPP]
import os, shutil, subprocess, sys, json
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__)); REPO = os.path.dirname(os.path.dirname(HERE))
W = os.path.expanduser("~/Infinite/media/films/flower-film/work")
name, view = sys.argv[1], sys.argv[2]
size = sys.argv[3] if len(sys.argv) > 3 else "480x360"; spp = sys.argv[4] if len(sys.argv) > 4 else "48"
inf, out = os.path.join(W, name + ".inf"), os.path.join(W, name)
subprocess.run([sys.executable, os.path.join(HERE, "gen_tile.py"), "--probe", "--size", size, "--spp", spp, "--view", view, "--out", inf], check=True, capture_output=True)
shutil.rmtree(out, ignore_errors=True)
r = subprocess.run([os.path.join(REPO, "build/Infinite.app/Contents/MacOS/Infinite"), "--frame", inf, "0", out + "/", "--timeout", "300"],
                   env=dict(os.environ, INFINITE_LIVE_ENGINE="15"), capture_output=True, text=True)
d = json.loads(r.stdout.strip().splitlines()[-1])
a = np.asarray(Image.open(os.path.join(out, "frame_00000.png")).convert("RGB")).astype(float) / 255
hsv = np.asarray(Image.open(os.path.join(out, "frame_00000.png")).convert("HSV")).astype(float) / 255
print(name, "%.0fs" % (d["elapsed_ms"] / 1000), "luma p1/50/99", np.percentile(a.mean(2), [1, 50, 99]).round(2), "sat", hsv[..., 1].mean().round(3))
