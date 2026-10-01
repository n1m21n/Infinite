#!/usr/bin/env python3
"""patch-layout-preview.py in.inf out.png - draw the boxes patch-layout.py placed (blue nodes, yellow comments),
so overlap/spacing can be checked without opening the GUI."""
import re, sys
from PIL import Image, ImageDraw
ns = {}
exec(open(__file__.replace("patch-layout-preview", "patch-layout")).read().split("def main")[0], ns)
SZ, CD = ns["SIZES"], ns["CAT_DEFAULT"]
items, cur = [], None
for l in open(sys.argv[1]).read().split("\n"):
    m = re.match(r"node (\S+) (\S+) (.*)", l)
    if m: cur = dict(cat=m[2], typ=m[3], id=m[1], x=0, y=0)
    if cur:
        for pat, f in ((r"\s+id (\w+)", lambda g: cur.update(id=g[1])), (r"\s+pos (\S+) (\S+)", lambda g: cur.update(x=float(g[1]), y=float(g[2]))),
                       (r"\s+f (width|height) ([\d.]+)", lambda g: cur.update({g[1][0]: float(g[2])}))):
            g = re.match(pat, l)
            if g: f(g)
        if l == "end":
            cm = cur["typ"] == "Comment"
            w, h = (cur.get("w", 260), cur.get("h", 140)) if cm else SZ.get(cur["typ"], CD.get(cur["cat"], (300, 300)))
            items.append((cur["x"], cur["y"], w, h, cur["id"], cm)); cur = None
S = 0.12
X0 = min(i[0] for i in items); Y0 = min(i[1] for i in items)
W = int((max(i[0] + i[2] for i in items) - X0) * S) + 40; H = int((max(i[1] + i[3] for i in items) - Y0) * S) + 40
im = Image.new("RGB", (W, H), "white"); d = ImageDraw.Draw(im)
for x, y, w, h, n, c in items:
    a = ((x - X0) * S + 20, (y - Y0) * S + 20, (x - X0 + w) * S + 20, (y - Y0 + h) * S + 20)
    d.rectangle(a, fill="#ffe08a" if c else "#9bb8f0", outline="black"); d.text((a[0] + 3, a[1] + 3), n, fill="black")
im.save(sys.argv[2]); print(W, H)
