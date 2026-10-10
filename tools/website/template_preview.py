"""Brand-styled patch diagram for a starter template (used by gen_library_templates.py).

What: reads an assets/templates/*.inf, drops comments and the shared scaffold (the little picture that follows the
      sound), lays the remaining nodes left to right by wiring depth and draws them as Midnight cards with a category
      stripe, cables in the source node's category colour, and one Ember port on the last node. 960 x 600 webp.
Why:  the app's own template thumbnails are a single orange dot for every audio template; this shows what the patch is.
Colours: Midnight tokens from docs/brand/brand.json, category colours from the app's default theme preset.
"""
import json, os, re
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
B = json.load(open(os.path.join(ROOT, "docs", "brand", "brand.json")))
M = B["colour"]["modes"]["midnight"]
FONTS = os.path.join(ROOT, "art", "brand", "fonts")
CAT = {"Source": "#4ADE80", "3D": "#38BDF8", "Compositing": "#818CF8", "Effects": "#FACC15", "Modulators": "#A3E635",
       "Prediction": "#22D388", "Macros": "#FC963C", "Utility": "#A3A9BA", "Notes": "#4CD964", "Synths": "#6992F6",
       "AudioEffects": "#53C7E3"}
LABEL = {"AudioEffects": "AUDIO EFFECTS"}
SCAFFOLD = {"Audio Analyze", "Shape", "Output", "Comment"}
S = 2                       # supersample
W, H = 960, 600


def parse(path):
    nodes, order, edges, cur = {}, [], [], None
    for raw in open(path):
        l = raw.rstrip("\n")
        m = re.match(r"node (\d+) (\S+) (.+)", l)
        if m:
            cur = {"cat": m[2], "type": m[3], "id": m[1]}
            continue
        if cur is not None:
            g = re.match(r"\s+id (\S+)", l)
            if g:
                cur["id"] = g[1]
            if l == "end":
                nodes[cur["id"]] = cur
                order.append(cur["id"])
                cur = None
            continue
        t = l.split()
        if len(t) >= 4 and t[0] in ("note", "aud", "cable", "geo", "mod"):
            edges.append((t[3], t[1]) if t[0] != "mod" else (t[3], t[1]))
    keep = [i for i in order if nodes[i]["type"] not in SCAFFOLD]
    edges = [(a, b) for a, b in edges if a in keep and b in keep and a != b]
    return nodes, keep, edges


def rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def font(name, size):
    return ImageFont.truetype(os.path.join(FONTS, name), int(size * S))


def bez(p0, p3, n=40):
    dx = max(40, abs(p3[0] - p0[0]) * 0.5)
    p1, p2 = (p0[0] + dx, p0[1]), (p3[0] - dx, p3[1])
    out = []
    for i in range(n + 1):
        t = i / n
        u = 1 - t
        out.append((u**3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t**3 * p3[0],
                    u**3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t**3 * p3[1]))
    return out


def signal_path(keep, edges, limit=5):
    """The main chain of a big patch: from the sink with the most ancestors back along the busiest input."""
    preds = {i: [a for a, b in edges if b == i] for i in keep}
    def anc(i, seen=None):
        seen = set() if seen is None else seen
        for q in preds[i]:
            if q not in seen:
                seen.add(q); anc(q, seen)
        return seen
    size = {i: len(anc(i)) for i in keep}
    sinks = [i for i in keep if not any(a == i for a, _ in edges)]
    cur = max(sinks or keep, key=lambda i: size[i])
    chain = [cur]
    while preds[cur] and len(chain) < limit:
        cur = max((q for q in preds[cur] if q not in chain), key=lambda q: size[q], default=None)
        if cur is None:
            break
        chain.append(cur)
    chain.reverse()
    return chain


def render(inf, out):
    nodes, keep, edges = parse(inf)
    if len(keep) > 6:
        keep = signal_path(keep, edges)
        edges = [(a, b) for a, b in edges if a in keep and b in keep]
    depth = {i: 0 for i in keep}
    for _ in range(len(keep)):
        for a, b in edges:
            depth[b] = max(depth[b], depth[a] + 1)
    cols = {}
    for i in keep:
        cols.setdefault(depth[i], []).append(i)
    ncol = len(cols)
    cw0, ch0, gx0, gy0 = 196, 92, 58, 34
    tw0 = ncol * cw0 + (ncol - 1) * gx0
    th0 = max(len(v) for v in cols.values()) * (ch0 + gy0) - gy0
    sc = min((W - 110) / tw0, (H - 150) / th0, 1.55)
    cw, ch, gapx, gapy = cw0 * sc, ch0 * sc, gx0 * sc, gy0 * sc
    x0 = (W - (ncol * cw + (ncol - 1) * gapx)) / 2
    pos = {}
    for c, ids in sorted(cols.items()):
        colh = len(ids) * ch + (len(ids) - 1) * gapy
        for r, i in enumerate(ids):
            pos[i] = (x0 + c * (cw + gapx), (H - colh) / 2 + r * (ch + gapy))
    img = Image.new("RGB", (W * S, H * S), rgb(M["deep"]))
    d = ImageDraw.Draw(img, "RGBA")
    for gx in range(0, W + 1, 32):
        for gy in range(0, H + 1, 32):
            d.ellipse([(gx - 1.4) * S, (gy - 1.4) * S, (gx + 1.4) * S, (gy + 1.4) * S], fill=rgb(M["dot"]))
    for a, b in edges:
        p0 = (pos[a][0] + cw, pos[a][1] + ch / 2)
        p3 = (pos[b][0], pos[b][1] + ch / 2)
        col = rgb(CAT.get(nodes[a]["cat"], "#A3A9BA"))
        d.line([(x * S, y * S) for x, y in bez(p0, p3)], fill=col + (235,), width=int(3.5 * sc * S), joint="curve")
    last = max(keep, key=lambda i: (depth[i], pos[i][1])) if keep else None
    ft, fm = font("Geist-SemiBold.ttf", 21 * sc), font("GeistMono-Medium.ttf", 11.5 * sc)
    for i in keep:
        x, y = pos[i]
        n = nodes[i]
        col = rgb(CAT.get(n["cat"], "#A3A9BA"))
        d.rounded_rectangle([(x + 2) * S, (y + 10) * S, (x + cw + 2) * S, (y + ch + 10) * S], 16 * sc * S, fill=rgb(M["shadow"]) + (150,))
        d.rounded_rectangle([x * S, y * S, (x + cw) * S, (y + ch) * S], 16 * sc * S, fill=rgb(M["surface-2"]))
        d.rounded_rectangle([x * S, (y + 18 * sc) * S, (x + 6 * sc) * S, (y + ch - 18 * sc) * S], 3 * sc * S, fill=col)
        title = {"Random Note Generator":"Random Notes"}.get(n["type"], n["type"])
        ftt = ft
        k = 21 * sc
        while d.textlength(title, font=ftt) > (cw - 40 * sc) * S and k > 12 * sc:
            k -= 0.5 * sc
            ftt = font("Geist-SemiBold.ttf", k)
        d.text(((x + 24 * sc) * S, (y + 24 * sc) * S), title, font=ftt, fill=rgb(M["ink"]))
        d.text(((x + 24 * sc) * S, (y + 58 * sc) * S), LABEL.get(n["cat"], n["cat"].upper()), font=fm, fill=rgb(M["ink-3"]))
        ins = any(b == i for _, b in edges)
        outs = any(a == i for a, _ in edges)
        for on, px in ((ins, x), (outs, x + cw)):
            if on:
                c = rgb(M["accent"]) if (i == last and px == x + cw) or (i == last and not outs) else rgb(M["ink"])
                d.ellipse([(px - 7 * sc) * S, (y + ch / 2 - 7 * sc) * S, (px + 7 * sc) * S, (y + ch / 2 + 7 * sc) * S], fill=rgb(M["surface-2"]), outline=c, width=int(3 * sc * S))
    if last and not any(a == last for a, _ in edges):
        px, py = pos[last][0] + cw, pos[last][1] + ch / 2
        d.ellipse([(px - 7 * sc) * S, (py - 7 * sc) * S, (px + 7 * sc) * S, (py + 7 * sc) * S], fill=rgb(M["accent"]))
    img = img.resize((W, H), Image.LANCZOS)
    img.save(out, "WEBP", quality=90, method=6)
