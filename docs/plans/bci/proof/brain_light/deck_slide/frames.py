import sys, os, numpy as np
sys.path.insert(0, "tools/bci")
import brain_light as bl, direct_pins as dp
from PIL import Image
W, H = 800, 500
bl.W, bl.H = W, H
yy, xx = np.mgrid[0:H, 0:W]
u, v = xx/(W-1), yy/(H-1)*(H/W)
bl.DIST = np.stack([np.hypot(u-p[0], v-p[1]*(H/W)) for p in bl.POS])
wt = 1/(bl.DIST**2+bl.EPS); bl.WT = wt/wt.sum(0)
out = sys.argv[2]
for tag, xo, xc in dp.sessions(sys.argv[1]):
    if not tag.startswith(("sub014_remote_session0", "sub006_remote_session1", "sub001_in_lab")): continue
    for st in ("open", "closed"):
        cal, x, s = bl.clip_state(xo, xc, st)
        nm = dp.Norm(dp.spectro(cal))
        for t in (4.0, 7.0):
            f = bl.render(x, nm, [int(t*bl.COL_RATE)], s)[0]
            Image.fromarray(f).save(f"{out}/{tag[:14]}_{st}_{int(t)}.png")
    print(tag)
