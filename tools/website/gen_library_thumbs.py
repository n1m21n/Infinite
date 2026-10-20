#!/usr/bin/env python3
"""Library thumbnails: knobs and cables, generated, one SVG per preset.

Why   : a preset's own render is a different style every time. These are one system: a handful of knobs laid out at
        random, joined by cables, in the colour of the preset's category. Quiet, on-brand, about 3 KB each, sharp at any size.
What  : reads the five library indexes (library, templates, community, skills, extensions) and writes website/assets/library/thumbs/<id>.svg (720 x 450, the card ratio).
Rules : the knob count is the preset's own param count, or its node count for patches and templates (3 to 8, so a bigger preset looks busier); the layout, the knob
        positions and the cables come from a hash of the preset id, so a preset always gets the same picture and re-running
        changes nothing. Cables chain the knobs nearest-first and add one or two cross links, each a sagging bezier with a plug
        at both ends. Colours: brand only: the category's brand colour (ember, mint, pink or signal) for cables and value arcs, ink for pointers, ground #151930 to deep #0D1020 with the brand dot grid. Knobs and cables are shaded as 3D (bezel, domed cap, tube highlights, blurred shadows).
Run   : python3 tools/website/gen_library_thumbs.py          (write)
        python3 tools/website/gen_library_thumbs.py --check  (exit 1 if a file would change)
Art   : presets of the Pixel, Sketch and Sketch 3D categories are pictures, so their card shows their own render instead: the
        preview jpg is cropped to the card ratio and written as thumbs/<id>.webp (about 20 KB). Everything else gets the knobs and cables.
Used  : website/library.js shows thumbs/<id>.svg on every deck card; the preset page keeps its real render.
"""
import hashlib, json, math, os, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "website")
SOURCES = [os.path.join(ROOT, "assets", d, "index.json") for d in ("library", "templates", "community", "skills", "extensions")]   # every card in the library deck
OUT = os.path.join(ROOT, "assets", "library", "thumbs")
W, H = 720, 450
COLOUR = {"Pixel": "#FF9878", "Audio FX": "#8FE3D0", "Audio Effects": "#8FE3D0", "Synths": "#8795F4", "Notes": "#F6B4C8", "Sketch": "#A7B1FF", "Sketch 3D": "#8795F4", "Templates": "#FF9878", "Community": "#8FE3D0", "AI Skills": "#F6B4C8", "Extensions": "#A7B1FF"}   # brand: ember-300, iridescence mint and pink, signal


BG = "plain"   # "plain": flat brand ground with a soft glow. "textured": ground with a vignette and the brand dot grid. Change this one word and rerun.


ART = {"Pixel", "Sketch", "Sketch 3D"}   # categories whose own render is the thumbnail


def art_webp(item):
    """The preset's own preview, cropped to 720 x 450 and compressed. Returns bytes, or None if it has no preview file."""
    from PIL import Image
    import io
    src = os.path.join(ROOT, item.get("preview", ""))
    if not item.get("preview") or not os.path.exists(src): return None
    im = Image.open(src).convert("RGB"); w, h = im.size; th = int(w * H / W)
    top = (h - th) // 2 if th < h else 0
    im = im.crop((0, top, w, top + min(th, h))).resize((W, H), Image.LANCZOS)
    b = io.BytesIO(); im.save(b, "WEBP", quality=80, method=6)
    return b.getvalue()


class Rng:
    """Small deterministic generator seeded by the preset id (no global state, same on every machine)."""
    def __init__(self, key):
        self.s = int.from_bytes(hashlib.sha256(key.encode()).digest()[:8], "big") or 1
    def next(self):
        self.s ^= (self.s << 13) & 0xFFFFFFFFFFFFFFFF; self.s ^= self.s >> 7; self.s ^= (self.s << 17) & 0xFFFFFFFFFFFFFFFF
        return (self.s & 0xFFFFFF) / 0x1000000
    def range(self, a, b): return a + (b - a) * self.next()


def place(rng, n):
    """n knobs at random, kept apart (dart throwing with a shrinking gap) and away from the edge."""
    gap = 150
    while True:
        pts = []
        for _ in range(4000):
            r = rng.range(26, 40) if rng.next() < 0.7 else rng.range(44, 56)
            x, y = rng.range(70, W - 70), rng.range(66, H - 66)
            if all(math.hypot(x - px, y - py) > gap * 0.5 + (r + pr) * 0.6 for px, py, pr in pts):
                pts.append((x, y, r))
            if len(pts) == n: return pts
        gap *= 0.85


def cable(a, b, rng, col, w):
    """A 3D tube: a soft shadow, a dark rim, the body, then a thin highlight along the top. A metal jack at each end."""
    (x1, y1, _), (x2, y2, _) = a, b
    d = math.hypot(x2 - x1, y2 - y1)
    sag = min(90, d * 0.35) * (1 if rng.next() < 0.75 else -0.5)
    mx, my = (x1 + x2) / 2, (y1 + y2) / 2 + sag
    c1x, c1y = x1 + (mx - x1) * 0.9, y1 + (my - y1) * 0.9 + sag * 0.15
    c2x, c2y = x2 + (mx - x2) * 0.9, y2 + (my - y2) * 0.9 + sag * 0.15
    path = "M%.1f %.1fC%.1f %.1f %.1f %.1f %.1f %.1f" % (x1, y1, c1x, c1y, c2x, c2y, x2, y2)
    st = lambda wd, c, extra="": '<path d="%s" fill="none" stroke="%s" stroke-width="%.1f" stroke-linecap="round" %s/>' % (path, c, wd, extra)
    jacks = "".join('<circle cx="%.1f" cy="%.1f" r="9" fill="url(#jack)"/><circle cx="%.1f" cy="%.1f" r="4.5" fill="%s"/><circle cx="%.1f" cy="%.1f" r="4.5" fill="none" stroke="#04050E" stroke-opacity=".5"/>' % (x, y, x, y, col, x, y) for x, y in ((x1, y1), (x2, y2)))
    return (st(w + 6, "#04050E", 'stroke-opacity=".4" filter="url(#blur)" transform="translate(0 9)"') + st(w + 3, "#0D1020") + st(w, col) +
            st(w * 0.35, "#FFFFFF", 'stroke-opacity=".5" transform="translate(0 -%.1f)"' % (w * 0.22)) + jacks)


def knob(i, k, rng, col):
    """A 3D knob: a blurred drop shadow, a bevelled bezel, a domed cap lit from the top left, a value arc and a glowing pointer."""
    x, y, r = k
    val = rng.range(0.15, 0.95)
    a0, sweep = math.radians(135), math.radians(270)
    def pt(t, rr): return x + rr * math.cos(a0 + sweep * t), y + rr * math.sin(a0 + sweep * t)
    ring = r + 11
    s, e0, e1 = pt(0, ring), pt(1, ring), pt(val, ring)
    track = "M%.1f %.1fA%.1f %.1f 0 1 1 %.1f %.1f" % (s[0], s[1], ring, ring, e0[0], e0[1])
    arc = "M%.1f %.1fA%.1f %.1f 0 %d 1 %.1f %.1f" % (s[0], s[1], ring, ring, 1 if val > 0.5 else 0, e1[0], e1[1])
    p0, p1 = pt(val, r * 0.3), pt(val, r * 0.82)
    return ('<g><ellipse cx="%.1f" cy="%.1f" rx="%.1f" ry="%.1f" fill="#04050E" fill-opacity=".6" filter="url(#blur)"/>'
            '<path d="%s" fill="none" stroke="#04050E" stroke-opacity=".55" stroke-width="6" stroke-linecap="round"/>'
            '<path d="%s" fill="none" stroke="%s" stroke-width="4" stroke-linecap="round" filter="url(#glow)"/>'
            '<circle cx="%.1f" cy="%.1f" r="%.1f" fill="url(#bezel)"/>'
            '<circle cx="%.1f" cy="%.1f" r="%.1f" fill="url(#cap)"/>'
            '<circle cx="%.1f" cy="%.1f" r="%.1f" fill="none" stroke="#FFFFFF" stroke-opacity=".22" stroke-width="1.2"/>'
            '<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="4" stroke-linecap="round"/></g>') % (
        x + 3, y + r * 0.85, r * 1.05, r * 0.5, track, arc, col, x, y, r, x, y, r * 0.8, x - r * 0.1, y - r * 0.12, r * 0.62, p0[0], p0[1], p1[0], p1[1], "#EEF1FA")


def thumb(item):
    rng = Rng(item["id"])
    col = COLOUR.get(item.get("category"), "#A7B1FF")
    raw = item.get("params", item.get("nodes", 4))
    raw = len(raw) if isinstance(raw, (list, dict)) else raw
    n = max(3, min(8, int(raw or 4)))
    ks = place(rng, n)
    order, left = [ks[0]], ks[1:]                      # chain nearest-first so the cables read as one patch
    while left:
        last = order[-1]; nx = min(left, key=lambda k: math.hypot(k[0] - last[0], k[1] - last[1])); left.remove(nx); order.append(nx)
    links = list(zip(order, order[1:]))
    for _ in range(1 + (n > 5)):
        a, b = rng.next(), rng.next(); ia, ib = int(a * n), int(b * n)
        if abs(ia - ib) > 1: links.append((order[ia], order[ib]))
    gx, gy = rng.range(0.2, 0.8) * W, rng.range(0.15, 0.6) * H
    parts = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d">' % (W, H, W, H),
             '<defs><radialGradient id="bg" cx="%.0f" cy="%.0f" r="480" gradientUnits="userSpaceOnUse"><stop offset="0" stop-color="%s" stop-opacity=".16"/><stop offset="1" stop-color="%s" stop-opacity="0"/></radialGradient>' % (gx, gy, col, col),
             '<radialGradient id="vig" cx=".5" cy=".42" r=".8"><stop offset="0" stop-color="#20263F"/><stop offset="1" stop-color="#0D1020"/></radialGradient>',
             '<pattern id="dots" width="24" height="24" patternUnits="userSpaceOnUse"><circle cx="2" cy="2" r="1" fill="#2A3052"/></pattern>',
             '<radialGradient id="cap" cx=".34" cy=".26" r=".95"><stop offset="0" stop-color="#6A73A6"/><stop offset=".45" stop-color="#394070"/><stop offset="1" stop-color="#1C2140"/></radialGradient>',
             '<linearGradient id="bezel" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#5A6396"/><stop offset=".5" stop-color="#262C52"/><stop offset="1" stop-color="#0D1020"/></linearGradient>',
             '<radialGradient id="jack" cx=".35" cy=".3" r=".9"><stop offset="0" stop-color="#D5DAF0"/><stop offset=".6" stop-color="#6F77A3"/><stop offset="1" stop-color="#2A3052"/></radialGradient>',
             '<filter id="blur" x="-30%" y="-30%" width="160%" height="160%"><feGaussianBlur stdDeviation="7"/></filter>',
             '<filter id="glow" x="-30%" y="-30%" width="160%" height="160%"><feGaussianBlur stdDeviation="2.2" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter></defs>',
             ('<rect width="%d" height="%d" fill="url(#vig)"/><rect width="%d" height="%d" fill="url(#dots)" opacity=".55"/><rect width="%d" height="%d" fill="url(#bg)"/>' % (W, H, W, H, W, H)) if BG == "textured" else
             ('<rect width="%d" height="%d" fill="#151930"/><rect width="%d" height="%d" fill="url(#bg)"/>' % (W, H, W, H))]
    parts += [cable(a, b, rng, col, 7) for a, b in links]
    parts += [knob(i, k, rng, col) for i, k in enumerate(ks)]
    parts.append("</svg>")
    return "".join(parts) + "\n"


def main():
    items = [it for f in SOURCES if os.path.exists(f) for it in json.load(open(f, encoding="utf-8"))["items"]]
    items = list({it["id"]: it for it in reversed(items)}.values())[::-1]   # a preset listed in two indexes (the synth templates) gets one picture
    os.makedirs(OUT, exist_ok=True)
    check = "--check" in sys.argv; diff = []
    for it in items:
        if it.get("category") in ART:
            data = art_webp(it)
            if data is not None:
                p = os.path.join(OUT, it["id"] + ".webp")
                if not os.path.exists(p) or open(p, "rb").read() != data:
                    diff.append(it["id"])
                    if not check: open(p, "wb").write(data)
                old = os.path.join(OUT, it["id"] + ".svg")
                if os.path.exists(old) and not check: os.remove(old)
                continue
        p = os.path.join(OUT, it["id"] + ".svg"); new = thumb(it)
        if not os.path.exists(p) or open(p, encoding="utf-8").read() != new:
            diff.append(it["id"])
            if not check: open(p, "w", encoding="utf-8").write(new)
    print(("differ: " if check else "written: ") + (", ".join(diff) or "none"))
    return 1 if (check and diff) else 0


if __name__ == "__main__":
    sys.exit(main())
