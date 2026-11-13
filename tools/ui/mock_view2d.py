#!/usr/bin/env python3
"""Real-metrics mockup of the 2D View tool strip and context bars (Inter + infinite-glyphs.ttf, token sizes).

  python3 tools/ui/mock_view2d.py OUT.png

Draws: the strip at one column (tall panel) and two columns (short panel), with the FG/BG swatches and quick mask
under the tools, and one context bar per tool family. 4 px per pt so 16 px glyphs can be judged one by one.
Rebuild the glyph font first (tools/design/build_glyphs.py) when an SVG changed.
"""
import json, pathlib, sys
from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parents[2]
S = 4
cp = json.loads((ROOT / "art/icons/codepoints.json").read_text())
INTER = str(ROOT / "external/fonts/Inter/Inter-Regular.ttf")
GLYPHS = str(ROOT / "external/icons/Infinite/infinite-glyphs.ttf")
BG, BAR, CHIP, ON, TXT, DIM, LINE, CANVAS = (10, 11, 15), (10, 11, 15), (23, 24, 29), (76, 119, 167), (238, 240, 246), (128, 134, 148), (40, 43, 52), (40, 40, 40)
TILE, ICON, GAP, GROUP = 28, 18, 4, 8

# Photoshop / Photopea / PhotoCraft order, hairline between sections.
STRIP = [["tool-move"], ["marquee", "lasso", "wand", "crop", "eyedropper"], ["brush", "erase", "fill"],
         ["pen-tool", "type", "pointer", "shape"], ["hand", "zoom"]]

W, H = 1500, 760
im = Image.new("RGB", (W * S, H * S), BG)
d = ImageDraw.Draw(im)


def rr(x0, y0, x1, y1, r, fill):
    d.rounded_rectangle([x0 * S, y0 * S, x1 * S, y1 * S], r * S, fill=fill)


def glyph(cx, cy, name, col=TXT, size=ICON):
    f = ImageFont.truetype(GLYPHS, int(size * S))
    d.text((cx * S, cy * S), chr(cp[name]), font=f, fill=col, anchor="mm")


def text(x, cy, s, col=TXT, pt=13, anchor="lm"):
    d.text((x * S, cy * S), s, font=ImageFont.truetype(INTER, int(pt * S)), fill=col, anchor=anchor)


def tw(s, pt=13):
    return d.textlength(s, font=ImageFont.truetype(INTER, int(pt * S))) / S


def chip(x, y, name, on=False, w=TILE):
    rr(x, y, x + w, y + TILE, 6, ON if on else CHIP)
    glyph(x + w / 2, y + TILE / 2, name, (255, 255, 255) if on else TXT)
    return x + w


def tchip(x, y, s, on=False):
    w = tw(s) + 16
    rr(x, y, x + w, y + TILE, 6, ON if on else CHIP)
    text(x + w / 2, y + TILE / 2, s, anchor="mm")
    return x + w


def well(x, y, w, s, label=None):
    if label:
        text(x, y + TILE / 2, label, DIM)
        x += tw(label) + GAP + 2
    rr(x, y, x + w, y + TILE, 4, (30, 32, 40))
    text(x + 8, y + TILE / 2, s)
    return x + w


def dot(x, y, col):
    rr(x + 5, y + 5, x + 23, y + 23, 4, col)
    return x + TILE


def swatches(x, y):
    """FG over BG, Photoshop corner layout, in a 28x28 cell plus swap/reset."""
    rr(x + 10, y + 10, x + 26, y + 26, 2, (255, 255, 255))
    rr(x + 2, y + 2, x + 18, y + 18, 2, (0, 0, 0))
    d.rectangle([(x + 2) * S, (y + 2) * S, (x + 18) * S, (y + 18) * S], outline=(160, 160, 170), width=S // 2)


def strip(x, y, cols):
    cy = y
    if cols > 1:
        flat = [n for g in STRIP for n in g]
        for i, n in enumerate(flat):
            chip(x + (i % cols) * (TILE + GAP), cy, n, on=(n == "tool-move"))
            if i % cols == cols - 1 or i == len(flat) - 1:
                cy += TILE + GAP
        cy += GAP
        swatches(x, cy)
        chip(x + TILE + GAP, cy, "quick-mask")
        return cy + TILE
    for gi, group in enumerate(STRIP):
        if gi:
            d.line([(x * S, (cy + 2) * S), ((x + cols * TILE + (cols - 1) * GAP) * S, (cy + 2) * S)], fill=LINE, width=S)
            cy += GAP + 1
        for i, n in enumerate(group):
            cx = x + (i % cols) * (TILE + GAP)
            chip(cx, cy, n, on=(n == "tool-move"))
            if i % cols == cols - 1 or i == len(group) - 1:
                cy += TILE + GAP
    cy += GAP
    swatches(x, cy)
    if cols == 2:
        chip(x + TILE + GAP, cy, "quick-mask")
        cy += TILE
    else:
        cy += TILE + GAP
        chip(x, cy, "quick-mask")
        cy += TILE
    return cy


def divider(x, y):
    d.line([((x + GROUP / 2) * S, (y + 6) * S), ((x + GROUP / 2) * S, (y + TILE - 6) * S)], fill=LINE, width=S)
    return x + GROUP


text(16, 14, "1 column (tall panel)", DIM, 11)
end1 = strip(16, 28, 1)
text(70, 14, "2 col", DIM, 11)
end2 = strip(70, 28, 2)
text(140, 14, "3 col", DIM, 11)
end3 = strip(140, 28, 3)
text(16, end1 + 16, f"strip h {end1 - 28:.0f} / {end2 - 28:.0f} pt", DIM, 11)

bx, by = 260, 28
rows = []


def bar(title, items):
    global by
    text(bx, by - 8, title, DIM, 11)
    x = bx
    for it in items:
        kind = it[0]
        if kind == "c":
            x = chip(x, by, it[1], it[2] if len(it) > 2 else False) + GAP
        elif kind == "t":
            x = tchip(x, by, it[1], it[2] if len(it) > 2 else False) + GAP
        elif kind == "w":
            x = well(x, by, it[1], it[2], it[3] if len(it) > 3 else None) + GAP
        elif kind == "d":
            x = dot(x, by, it[1]) + GAP
        elif kind == "|":
            x = divider(x - GAP, by) + GAP / 2
        elif kind == "l":
            text(x, by + TILE / 2, it[1], DIM)
            x += tw(it[1]) + GAP + 2
    rows.append((title, x - bx))
    by += TILE + 30


bar("Move", [("t", "Auto-select", True), ("|",), ("c", "marquee"), ("|",)] + [("c", "tool-move")] * 0)
bar("Marquee", [("t", "Rectangle", True), ("t", "Ellipse"), ("|",), ("c", "sel-new", True), ("c", "sel-add"),
                ("c", "sel-subtract"), ("c", "sel-intersect"), ("|",), ("w", 56, "0 px", "Feather")])
bar("Wand", [("c", "sel-new", True), ("c", "sel-add"), ("c", "sel-subtract"), ("c", "sel-intersect"), ("|",),
             ("w", 48, "32", "Tolerance"), ("t", "Contiguous", True), ("t", "All layers")])
bar("Brush", [("w", 110, "Soft round"), ("|",), ("w", 64, "24 px", "Size"), ("w", 56, "0%", "Hardness"),
              ("w", 56, "100%", "Opacity"), ("w", 56, "100%", "Flow"), ("|",), ("t", "Pressure", True)])
bar("Fill", [("t", "Bucket", True), ("t", "Linear"), ("t", "Radial"), ("|",), ("w", 48, "32", "Tolerance"),
             ("t", "Contiguous", True), ("t", "All layers")])
bar("Shape", [("w", 92, "Rectangle"), ("|",), ("l", "Fill"), ("d", (230, 80, 80)), ("l", "Stroke"), ("d", (255, 255, 255)),
              ("w", 56, "2 px"), ("c", "stroke-style"), ("|",), ("c", "sel-add", True), ("c", "sel-subtract"),
              ("c", "sel-intersect"), ("c", "sel-exclude"), ("|",), ("w", 56, "0", "Radius")])
bar("Text", [("w", 140, "Helvetica"), ("w", 64, "90 px"), ("|",), ("c", "text-left", True), ("c", "text-center"),
             ("c", "text-right"), ("|",), ("d", (0, 0, 0)), ("|",), ("w", 56, "1.20", "Leading"), ("w", 48, "0", "Tracking")])
bar("Status row", [("c", "zoom-out"), ("w", 58, "29%"), ("c", "zoom"), ("t", "Fit"), ("t", "1:1"), ("l", "1920 x 1080   5.72 ms")])

# Glyph check: every tool glyph at 16 / 18 / 24 pt, side by side, so look-alikes show up.
gx, gy = 260, by + 6
text(gx, gy - 8, "glyph check (16 / 18 / 24 pt)", DIM, 11)
names = [n for g in STRIP for n in g] + ["zoom-out", "sel-new", "sel-add", "sel-subtract", "sel-intersect", "sel-exclude",
                                          "text-left", "text-center", "text-right", "quick-mask", "stroke-style"]
for i, n in enumerate(names):
    x = gx + (i % 13) * 92
    y = gy + (i // 13) * 56
    for j, sz in enumerate((16, 18, 24)):
        glyph(x + 10 + j * 26, y + 14, n, TXT, sz)
    text(x, y + 38, n, DIM, 9)

im = im.resize((W * 2, H * 2), Image.LANCZOS)
im.save(sys.argv[1] if len(sys.argv) > 1 else "mock_view2d.png")
for t, w in rows:
    print(f"{t:12s} {w:6.0f} pt")
print("strip heights", end1 - 28, end2 - 28, end3 - 28)
