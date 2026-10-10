"""Build every logo file from the lemniscate formula in docs/brand/brand.json.

What: mark, tile and lockup SVGs (gradient, ink-on-light, Moon-on-dark), favicon SVG + PNGs, the app-icon PNG
ladder, a macOS .iconset/.icns, a Windows .ico, Linux PNGs and a press-ready PNG set.
Why: the mark was only ever stored as a bitmap iconset; this draws it from the curve so every size and channel
matches, and measures the shipped icon against the formula instead of eyeballing it.

Usage:
    python3 tools/brand/build_logo.py              # build into art/brand/logo/
    python3 tools/brand/build_logo.py --check      # also compare with assets/Infinite.iconset, exit 1 on drift
    python3 tools/brand/build_logo.py --install    # also copy favicons into website/assets/ (never touches assets/)

Exit codes: 0 ok, 1 drift found by --check, 2 usage error.
"""
import json
import math
import os
import shutil
import subprocess
import sys

from PIL import Image, ImageDraw

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
B = json.load(open(os.path.join(ROOT, "docs", "brand", "brand.json")))
OUT = os.path.join(ROOT, "art", "brand", "logo")
GRAD = B["colour"]["gradient"]
MODES = B["colour"]["modes"]
S = B["logo"]["stroke"]                  # stroke / A
TILE = B["logo"]["tile"]
TILE_BG = TILE["ground"]
TILE_CORNER = TILE["corner"]             # corner radius / tile side
TILE_MARK = TILE["mark_width"]           # mark width (with stroke) / tile side
SS = 4                                   # supersampling for PNGs


def lem_pts(A, cx, cy, n=720):
    out = []
    for i in range(n):
        p = 2 * math.pi * i / n
        d = 1 + math.cos(p) ** 2
        out.append((cx + A * math.sin(p) / d, cy - A * math.sin(p) * math.cos(p) / d))
    return out


def size_for(A):
    s = S * A
    return 2 * A + s, A / math.sqrt(2) + s


# ------------------------------------------------------------------ SVG
def svg_path(A, cx, cy):
    return "M" + " L".join(f"{x:.2f},{y:.2f}" for x, y in lem_pts(A, cx, cy, 360)) + "Z"


def grad(gid, x0, x1):
    st = "".join(f'<stop offset="{p}" stop-color="{h}"/>' for h, p in zip(GRAD["stops"], GRAD["positions"]))
    return f'<linearGradient id="{gid}" gradientUnits="userSpaceOnUse" x1="{x0:.2f}" y1="0" x2="{x1:.2f}" y2="0">{st}</linearGradient>'


def svg_mark(paint, clear=False):
    """paint: 'gradient' or a hex. clear=True adds the 2s clear space as padding."""
    A = 100
    s = S * A
    w, h = size_for(A)
    pad = 2 * s if clear else 0
    W, H = w + 2 * pad, h + 2 * pad
    cx, cy = W / 2, H / 2
    defs = f"<defs>{grad('g', cx - w / 2, cx + w / 2)}</defs>" if paint == "gradient" else ""
    stroke = "url(#g)" if paint == "gradient" else paint
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W:.2f} {H:.2f}" role="img" aria-label="Infinite">{defs}'
            f'<path d="{svg_path(A, cx, cy)}" fill="none" stroke="{stroke}" stroke-width="{s:.2f}" stroke-linejoin="round"/></svg>\n')


def svg_tile(side=1024):
    w = TILE_MARK * side
    A = w / (2 + S)
    s = S * A
    c = side / 2
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {side} {side}" role="img" aria-label="Infinite">'
            f'<defs>{grad("g", c - w / 2, c + w / 2)}</defs>'
            f'<rect width="{side}" height="{side}" rx="{TILE_CORNER * side:.1f}" fill="{TILE_BG}"/>'
            f'<path d="{svg_path(A, c, c)}" fill="none" stroke="url(#g)" stroke-width="{s:.2f}" stroke-linejoin="round"/></svg>\n')


def svg_lockup(ink, paint="gradient"):
    """Mark + wordmark. Wordmark cap height = mark height without stroke; gap = 1.5 s. Geist 700, tracking -0.03 em."""
    A = 100
    s = S * A
    w, h = size_for(A)
    fs = h * 1.02
    gap = 1.5 * s
    tw = fs * 3.55                          # Geist 700 "Infinite" advance at -0.03 em, measured
    W, H = w + gap + tw, h
    defs = f"<defs>{grad('g', 0, w)}</defs>" if paint == "gradient" else ""
    stroke = "url(#g)" if paint == "gradient" else paint
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W:.1f} {H:.1f}" role="img" aria-label="Infinite">{defs}'
            f'<path d="{svg_path(A, w / 2, h / 2)}" fill="none" stroke="{stroke}" stroke-width="{s:.2f}" stroke-linejoin="round"/>'
            f'<text x="{w + gap:.1f}" y="{h / 2 + fs * 0.36:.1f}" font-family="Geist, Inter, system-ui, sans-serif" font-weight="700" '
            f'font-size="{fs:.1f}" letter-spacing="{-0.03 * fs:.2f}" fill="{ink}">Infinite</text></svg>\n')


# ------------------------------------------------------------------ raster
def gradient_img(W, H, x0, x1):
    stops = [(p, tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))) for h, p in zip(GRAD["stops"], GRAD["positions"])]
    row = Image.new("RGB", (W, 1))
    px = row.load()
    for x in range(W):
        t = min(1, max(0, (x - x0) / max(1, x1 - x0)))
        for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
            if p0 <= t <= p1:
                k = (t - p0) / (p1 - p0)
                px[x, 0] = tuple(round(a + (b - a) * k) for a, b in zip(c0, c1))
                break
    return row.resize((W, H))


def stroke_mask(W, H, A, cx, cy):
    m = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(m)
    pts = lem_pts(A, cx, cy)
    s = S * A
    d.line(pts + [pts[0]], fill=255, width=max(1, round(s)), joint="curve")
    r = s / 2
    for x, y in pts[::6]:
        d.ellipse((x - r, y - r, x + r, y + r), fill=255)
    return m


def png_tile(side):
    W = side * SS
    w = TILE_MARK * W
    A = w / (2 + S)
    c = W / 2
    tile = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    shape = Image.new("L", (W, W), 0)
    ImageDraw.Draw(shape).rounded_rectangle((0, 0, W - 1, W - 1), radius=TILE_CORNER * W, fill=255)
    bg = Image.new("RGBA", (W, W), TILE_BG)
    tile.paste(bg, (0, 0), shape)
    g = gradient_img(W, W, c - w / 2, c + w / 2).convert("RGBA")
    tile.paste(g, (0, 0), stroke_mask(W, W, A, c, c))
    return tile.resize((side, side), Image.LANCZOS)


def png_mark(width, paint):
    A = width * SS / (2 + S)
    w, h = size_for(A)
    W, H = round(w), round(h)
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    fill = gradient_img(W, H, 0, W).convert("RGBA") if paint == "gradient" else Image.new("RGBA", (W, H), paint)
    img.paste(fill, (0, 0), stroke_mask(W, H, A, W / 2, H / 2))
    return img.resize((max(1, round(W / SS)), max(1, round(H / SS))), Image.LANCZOS)


# ------------------------------------------------------------------ check
def check():
    """Compare the shipped 1024 icon with the formula: tile colour, corner radius, and the mark's warm-end bounds."""
    path = os.path.join(ROOT, "assets", "Infinite.iconset", "icon_512x512@2x.png")
    ship = Image.open(path).convert("RGBA")
    ref = png_tile(1024)
    probs = []
    sc, rc = ship.getpixel((100, 512)), ref.getpixel((100, 512))
    dc = max(abs(a - b) for a, b in zip(sc[:3], rc[:3]))
    corner = next(i for i in range(512) if ship.getpixel((i, i))[3] > 128)
    rcorner = next(i for i in range(512) if ref.getpixel((i, i))[3] > 128)
    def alpha_iou(a, b):
        A = [p > 128 for p in a.split()[3].getdata()]
        Bv = [p > 128 for p in b.split()[3].getdata()]
        inter = sum(x and y for x, y in zip(A, Bv))
        return inter / max(1, sum(x or y for x, y in zip(A, Bv)))
    def mark_box(im):
        px = im.load()
        xs, ys = [], []
        for y in range(0, 1024, 2):
            for x in range(0, 1024, 2):
                r, g, b, a = px[x, y]
                if a > 128 and (r + g + b) > 330:
                    xs.append(x); ys.append(y)
        return (min(xs), min(ys), max(xs), max(ys)) if xs else None
    sb, rb = mark_box(ship), mark_box(ref)
    rows = [("tile colour", f"#{sc[0]:02X}{sc[1]:02X}{sc[2]:02X}", f"#{rc[0]:02X}{rc[1]:02X}{rc[2]:02X}", dc <= 6),
            ("corner (diagonal px)", corner, rcorner, abs(corner - rcorner) <= 8),
            ("silhouette IoU", f"{alpha_iou(ship, ref):.3f}", "1.000", alpha_iou(ship, ref) > 0.98),
            ("mark bounds", sb, rb, sb is not None and all(abs(a - b) <= 24 for a, b in zip(sb, rb)))]
    print(f"{'check':22} {'shipped':>22} {'formula':>22}  ok")
    for name, a, b, ok in rows:
        print(f"{name:22} {str(a):>22} {str(b):>22}  {'yes' if ok else 'NO'}")
        if not ok:
            probs.append(name)
    return probs


# ------------------------------------------------------------------ main
def main():
    args = set(sys.argv[1:])
    if args - {"--check", "--install"}:
        print(__doc__)
        return 2
    d = lambda *p: os.path.join(OUT, *p)
    for sub in ("svg", "png", "favicon", "macos/Infinite.iconset", "windows", "linux"):
        os.makedirs(d(sub), exist_ok=True)
    mid, paper = MODES["midnight"], MODES["paper"]
    svgs = {
        "mark.svg": svg_mark("gradient"),
        "mark-clearspace.svg": svg_mark("gradient", clear=True),
        "mark-ink.svg": svg_mark(paper["ink"]),
        "mark-moon.svg": svg_mark(mid["ink"]),
        "tile.svg": svg_tile(),
        "lockup-on-dark.svg": svg_lockup(mid["ink"]),
        "lockup-on-light.svg": svg_lockup(paper["ink"]),
        "lockup-ink.svg": svg_lockup(paper["ink"], paper["ink"]),
    }
    for n, s in svgs.items():
        open(d("svg", n), "w").write(s)
    shutil.copy(d("svg", "tile.svg"), d("favicon", "favicon.svg"))
    for side, n in ((32, "favicon-32.png"), (64, "icon-64.png"), (96, "icon-96.png"), (180, "apple-touch-icon.png"), (512, "icon-512.png")):
        png_tile(side).save(d("favicon", n))
    for w in (256, 512, 1024, 2048):
        png_mark(w, "gradient").save(d("png", f"mark-{w}.png"))
        png_mark(w, paper["ink"]).save(d("png", f"mark-ink-{w}.png"))
        png_mark(w, mid["ink"]).save(d("png", f"mark-moon-{w}.png"))
    png_tile(1024).save(d("png", "tile-1024.png"))
    for base in (16, 32, 128, 256, 512):
        png_tile(base).save(d("macos/Infinite.iconset", f"icon_{base}x{base}.png"))
        png_tile(base * 2).save(d("macos/Infinite.iconset", f"icon_{base}x{base}@2x.png"))
    if shutil.which("iconutil"):
        subprocess.run(["iconutil", "-c", "icns", d("macos/Infinite.iconset"), "-o", d("macos", "Infinite.icns")], check=True)
    png_tile(256).save(d("windows", "Infinite.ico"), sizes=[(s, s) for s in (16, 24, 32, 48, 64, 256)])
    for side in (256, 512):
        png_tile(side).save(d("linux", f"infinite-{side}.png"))
    shutil.copy(d("svg", "tile.svg"), d("linux", "infinite.svg"))
    n = sum(len(f) for _, _, f in os.walk(OUT))
    print(f"logo: {n} files in {os.path.relpath(OUT, ROOT)}")
    if "--install" in args:
        site = os.path.join(ROOT, "website", "assets")
        for n in ("favicon.svg", "favicon-32.png", "icon-64.png", "icon-96.png", "apple-touch-icon.png"):
            shutil.copy(d("favicon", n), os.path.join(site, n))
        print("installed favicons into website/assets")
    if "--check" in args:
        probs = check()
        if probs:
            print("drift: " + ", ".join(probs))
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
