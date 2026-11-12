#!/usr/bin/env python3
"""Library thumbnails: brand-book v2.1 Midnight-mode cards & normalized artwork renders.

What:
  Reads the five library indexes (library, templates, community, skills, extensions) and writes
  website/assets/library/thumbs/<id>.webp (720 x 450, 16:10 card ratio).
Rules:
  - Visual presets (Pixel, Sketch, Sketch 3D, Community, and rich-visual Templates) keep their
    real artwork render, cropped/resized cleanly to 720 x 450 WebP.
  - Audio, Synth, FX, Template (node/blank), AI Skill, and Extension presets are rendered at 2x
    (1440 x 900 -> 720 x 450 LANCZOS) using Midnight mode tokens (#151930 ground, #0D1020 deep,
    #20263F surface-1, #EEF1FA Moon ink, #A7B1FF Signal, one #FF6B35 Ember focal element),
    zero outlines, Geist & Geist Mono typography, and a deterministic preset-specific visual
    (synth waveform + knobs, FX frequency response, or actual node-chain graph).
Run:
  python3 tools/website/gen_library_thumbs.py
  python3 tools/website/gen_library_thumbs.py --check
"""
from __future__ import annotations
import hashlib
import io
import json
import math
import os
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
WEB = os.path.join(ROOT, "website")
SOURCES = [os.path.join(WEB, "assets", d, "index.json") for d in ("library", "templates", "community", "skills", "extensions")]
OUT = os.path.join(WEB, "assets", "library", "thumbs")
FONTS = os.path.join(ROOT, "art", "brand", "fonts")

W, H = 720, 450
S = 2  # 2x supersampling
SW, SH = W * S, H * S

# Brand Book v2.1 Midnight tokens
GROUND = (21, 25, 48)       # #151930
DEEP = (13, 16, 32)         # #0D1020
SURFACE1 = (32, 38, 63)     # #20263F
SURFACE2 = (42, 48, 82)     # #2A3052
LINE = (52, 59, 94)         # #343B5E
MOON = (238, 241, 250)      # #EEF1FA
INK2 = (163, 170, 203)      # #A3AACB
INK3 = (136, 144, 177)      # #8890B1
SIGNAL = (167, 177, 255)    # #A7B1FF
EMBER = (255, 107, 53)      # #FF6B35

ART_CATS = {"Pixel", "Sketch", "Sketch 3D", "Community"}


def load_font(name: str, size: int) -> ImageFont.FreeTypeFont:
    p = os.path.join(FONTS, name)
    if os.path.exists(p):
        return ImageFont.truetype(p, size)
    return ImageFont.load_default()


F_TITLE = load_font("Geist-ExtraBold.ttf", 52)
F_TITLE_SM = load_font("Geist-ExtraBold.ttf", 42)
F_KICKER = load_font("GeistMono-Medium.ttf", 22)
F_META = load_font("GeistMono-Medium.ttf", 22)
F_NODE = load_font("Geist-SemiBold.ttf", 22)
F_MONO_SM = load_font("GeistMono-Medium.ttf", 18)


class Rng:
    """Deterministic PRNG seeded by preset id."""
    def __init__(self, key: str):
        self.s = int.from_bytes(hashlib.sha256(key.encode()).digest()[:8], "big") or 1

    def next(self) -> float:
        self.s ^= (self.s << 13) & 0xFFFFFFFFFFFFFFFF
        self.s ^= self.s >> 7
        self.s ^= (self.s << 17) & 0xFFFFFFFFFFFFFFFF
        return (self.s & 0xFFFFFF) / 0x1000000

    def range(self, a: float, b: float) -> float:
        return a + (b - a) * self.next()


def is_rich_visual_template(item: dict) -> bool:
    """For Templates, keep real rendered visuals and replace blank circles or audio/bare nodegraphs."""
    nodes = item.get("nodes") or []
    if isinstance(nodes, list) and "Audio Out" in nodes and not any(
        k in " ".join(nodes) for k in ("Shader", "Field Pixel", "Render 3D", "Sketch")
    ):
        return False
    src = os.path.join(WEB, item.get("preview", ""))
    if not item.get("preview") or not os.path.exists(src):
        return False
    with Image.open(src) as im:
        arr = np.asarray(im.convert("RGB"))
    black_frac = float(np.mean(arr.max(axis=2) < 10))
    mid_dist = np.abs(arr.astype(int) - np.array(GROUND)).sum(axis=2)
    mid_frac = float(np.mean(mid_dist < 25))
    return black_frac <= 0.70 and mid_frac <= 0.55


def art_webp(item: dict) -> bytes | None:
    """Crop/resize a real visual preview to 720 x 450 WebP."""
    src = os.path.join(WEB, item.get("preview", ""))
    if not item.get("preview") or not os.path.exists(src):
        return None
    with Image.open(src) as im:
        im = im.convert("RGB")
        w, h = im.size
        target_ratio = W / H
        cur_ratio = w / h
        if cur_ratio > target_ratio:
            nw = int(h * target_ratio)
            left = (w - nw) // 2
            im = im.crop((left, 0, left + nw, h))
        elif cur_ratio < target_ratio:
            nh = int(w / target_ratio)
            top = (h - nh) // 2
            im = im.crop((0, top, w, top + nh))
        im = im.resize((W, H), Image.LANCZOS)
        b = io.BytesIO()
        im.save(b, "WEBP", quality=85, method=6)
        return b.getvalue()


def wrap_text(draw: ImageDraw.ImageDraw, text: str, font: ImageFont.FreeTypeFont, max_w: int, max_lines: int = 3) -> list[str]:
    words = text.split()
    lines: list[str] = []
    cur = ""
    for w in words:
        cand = f"{cur} {w}".strip()
        if draw.textlength(cand, font=font) <= max_w:
            cur = cand
        else:
            if cur:
                lines.append(cur)
            cur = w
            if len(lines) == max_lines - 1:
                break
    if cur and len(lines) < max_lines:
        # Append remaining words if any were skipped
        idx = sum(len(l.split()) for l in lines)
        rem = " ".join(words[idx:])
        while draw.textlength(rem, font=font) > max_w and len(rem) > 4:
            rem = rem[:-2].rstrip() + "…"
        lines.append(rem)
    return lines


def draw_bezier(draw: ImageDraw.ImageDraw, p0, p1, p2, p3, fill, width: int) -> None:
    pts = []
    steps = 32
    for i in range(steps + 1):
        t = i / steps
        mt = 1 - t
        x = mt**3 * p0[0] + 3 * mt**2 * t * p1[0] + 3 * mt * t**2 * p2[0] + t**3 * p3[0]
        y = mt**3 * p0[1] + 3 * mt**2 * t * p1[1] + 3 * mt * t**2 * p2[1] + t**3 * p3[1]
        pts.append((x, y))
    draw.line(pts, fill=fill, width=width, joint="curve")


def draw_knob(draw: ImageDraw.ImageDraw, cx: int, cy: int, r: int, val: float, ember: bool) -> None:
    # Track arc (270 deg from 135 deg)
    bbox = [cx - r - 14, cy - r - 14, cx + r + 14, cy + r + 14]
    draw.arc(bbox, start=135, end=405, fill=SURFACE2, width=8)
    end_ang = 135 + int(270 * val)
    draw.arc(bbox, start=135, end=end_ang, fill=EMBER if ember else SIGNAL, width=8)
    # Knob body (no outline)
    draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=SURFACE1)
    draw.ellipse([cx - int(r * 0.78), cy - int(r * 0.78), cx + int(r * 0.78), cy + int(r * 0.78)], fill=SURFACE2)
    # Pointer
    rad = math.radians(end_ang)
    px0, py0 = cx + math.cos(rad) * (r * 0.25), cy + math.sin(rad) * (r * 0.25)
    px1, py1 = cx + math.cos(rad) * (r * 0.72), cy + math.sin(rad) * (r * 0.72)
    draw.line([(px0, py0), (px1, py1)], fill=MOON, width=5)


def render_brand_card(item: dict) -> bytes:
    rng = Rng(item["id"])
    cat = item.get("category", "Library")
    cat_norm = "Audio Effects" if cat.lower() == "audio effects" else cat

    im = Image.new("RGB", (SW, SH), GROUND)
    draw = ImageDraw.Draw(im)

    # Programmed dot field (~5% Signal over Midnight ground)
    dot_col = (30, 35, 62)
    for gx in range(32, SW - 20, 48):
        for gy in range(32, SH - 20, 48):
            draw.ellipse([gx - 2, gy - 2, gx + 2, gy + 2], fill=dot_col)

    # Right stage well (no outline, rounded 40px at 2x = 20px at 1x)
    sx1, sy1, sx2, sy2 = 660, 118, SW - 64, SH - 138
    draw.rounded_rectangle([sx1, sy1, sx2, sy2], radius=40, fill=DEEP)

    # Subtle graticule inside stage
    grat_col = (26, 30, 52)
    for gx in range(sx1 + 64, sx2, 64):
        draw.line([(gx, sy1 + 16), (gx, sy2 - 16)], fill=grat_col, width=2)
    for gy in range(sy1 + 64, sy2, 64):
        draw.line([(sx1 + 16, gy), (sx2 - 16, gy)], fill=grat_col, width=2)

    # Left column: category badge, title, metadata (safe from top chips and bottom title bar)
    lx = 68
    badge_txt = cat_norm.upper()
    bw = int(draw.textlength(badge_txt, font=F_KICKER)) + 36
    draw.rounded_rectangle([lx, 172, lx + bw, 214], radius=21, fill=SURFACE1)
    draw.text((lx + 18, 180), badge_txt, font=F_KICKER, fill=SIGNAL)

    name = item.get("name", item["id"])
    font_t = F_TITLE if len(name) <= 24 else F_TITLE_SM
    lines = wrap_text(draw, name, font_t, max_w=550, max_lines=3)
    ty = 248
    line_h = 60 if font_t == F_TITLE else 50
    for ln in lines:
        draw.text((lx, ty), ln, font=font_t, fill=MOON)
        ty += line_h

    # Metadata line
    tags = item.get("tags") or []
    nodes = item.get("nodes") or []
    params = item.get("params")
    if isinstance(nodes, list) and nodes:
        meta = f"{len(nodes)} nodes · " + " · ".join(nodes[:2])
    elif params:
        meta = f"{params} params" + (f" · {' · '.join(tags[:2])}" if tags else "")
    elif tags:
        meta = " · ".join(tags[:3])
    else:
        meta = item.get("kind", "Patch")
    while draw.textlength(meta, font=F_META) > 550 and len(meta) > 6:
        meta = meta[:-2].rstrip() + "…"
    draw.text((lx, ty + 18), meta, font=F_META, fill=INK2)

    # Pre-blended header tint colours over SURFACE1
    hdr_signal = (51, 58, 90)
    hdr_ember = (63, 48, 61)

    # Right stage bespoke visual by category
    if cat_norm == "Templates" and isinstance(nodes, list) and len(nodes) >= 2:
        show_nodes = nodes[:3]
        ny0 = sy1 + 54
        nh = 104
        gap = 54
        prev_out = None
        for idx, nname in enumerate(show_nodes):
            nx1 = sx1 + 60 + idx * 70
            nx2 = min(sx2 - 44, nx1 + 380)
            ny = ny0 + idx * (nh + gap)
            if ny + nh > sy2 - 30:
                break
            draw.rounded_rectangle([nx1, ny, nx2, ny + nh], radius=24, fill=SURFACE1)
            is_last = idx == len(show_nodes) - 1
            hdr_col = hdr_ember if is_last else hdr_signal
            draw.rounded_rectangle([nx1, ny, nx2, ny + 44], radius=24, fill=hdr_col)
            draw.rectangle([nx1, ny + 22, nx2, ny + 44], fill=hdr_col)
            nlabel = nname if len(nname) <= 22 else nname[:20] + "…"
            draw.text((nx1 + 22, ny + 10), nlabel, font=F_NODE, fill=MOON)
            draw.text((nx1 + 22, ny + 60), f"NODE 0{idx + 1}", font=F_MONO_SM, fill=INK3)
            if prev_out is not None:
                p0 = prev_out
                p3 = (nx1, ny + 68)
                dx = max(60, abs(p3[0] - p0[0]) * 0.5)
                c_col = EMBER if is_last else SIGNAL
                draw_bezier(draw, p0, (p0[0] + dx, p0[1]), (p3[0] - dx, p3[1]), p3, fill=c_col, width=6)
                draw.ellipse([p0[0] - 8, p0[1] - 8, p0[0] + 8, p0[1] + 8], fill=c_col)
                draw.ellipse([p3[0] - 8, p3[1] - 8, p3[0] + 8, p3[1] + 8], fill=c_col)
            prev_out = (nx2, ny + 68)

    elif cat_norm == "Synths":
        wave_y = sy1 + 190
        pts = []
        f1 = rng.range(2.0, 5.0)
        f2 = rng.range(5.0, 11.0)
        mod = rng.range(0.4, 1.4)
        for x in range(sx1 + 48, sx2 - 48, 3):
            t = (x - (sx1 + 48)) / max(1, (sx2 - sx1 - 96))
            env = math.sin(t * math.pi)
            y = wave_y - env * 115 * math.sin(t * math.pi * f1 + mod * math.sin(t * math.pi * f2))
            pts.append((x, y))
        draw.line(pts, fill=SIGNAL, width=6, joint="curve")

        ky = sy2 - 120
        for i in range(3):
            kx = sx1 + 135 + i * 215
            draw_knob(draw, kx, ky, 46, rng.range(0.2, 0.9), ember=(i == 1))

    elif cat_norm == "Audio Effects":
        base_y = sy1 + 310
        peak_pos = rng.range(0.28, 0.72)
        q = rng.range(16.0, 34.0)
        pts = []
        for x in range(sx1 + 48, sx2 - 48, 3):
            t = (x - (sx1 + 48)) / max(1, (sx2 - sx1 - 96))
            lp = 1.0 / (1.0 + math.exp((t - peak_pos) * 9.0))
            res = 0.55 * math.exp(-((t - peak_pos) ** 2) * q)
            y = base_y - (lp * 130 + res * 170)
            pts.append((x, y))
        poly = [(sx1 + 48, base_y)] + pts + [(sx2 - 48, base_y)]
        draw.polygon(poly, fill=(36, 40, 68))
        draw.line(pts, fill=SIGNAL, width=6, joint="curve")
        pk_x = int(sx1 + 48 + peak_pos * (sx2 - sx1 - 96))
        pk_y = int(base_y - (0.5 * 130 + 0.55 * 170))
        draw.ellipse([pk_x - 11, pk_y - 11, pk_x + 11, pk_y + 11], fill=EMBER)

        ky = sy2 - 110
        for i in range(3):
            kx = sx1 + 135 + i * 215
            draw_knob(draw, kx, ky, 44, rng.range(0.2, 0.9), ember=(i == 0))

    else:
        mx1, my1, mx2, my2 = sx1 + 72, sy1 + 110, sx2 - 72, sy2 - 110
        draw.rounded_rectangle([mx1, my1, mx2, my2], radius=28, fill=SURFACE1)
        draw.rounded_rectangle([mx1, my1, mx2, my1 + 60], radius=28, fill=hdr_signal)
        draw.rectangle([mx1, my1 + 30, mx2, my1 + 60], fill=hdr_signal)
        draw.text((mx1 + 28, my1 + 16), cat_norm.upper(), font=F_NODE, fill=MOON)
        for r in range(3):
            py = my1 + 115 + r * 68
            is_e = (r == 1)
            draw.ellipse([mx1 - 10, py - 10, mx1 + 10, py + 10], fill=EMBER if is_e else SIGNAL)
            draw.rounded_rectangle([mx1 + 32, py - 14, mx2 - 48 - r * 36, py + 14], radius=14,
                                   fill=EMBER if is_e else SURFACE2)

    out = im.resize((W, H), Image.LANCZOS)
    b = io.BytesIO()
    out.save(b, "WEBP", quality=85, method=6)
    return b.getvalue()


def main() -> int:
    items = [it for f in SOURCES if os.path.exists(f) for it in json.load(open(f, encoding="utf-8"))["items"]]
    items = list({it["id"]: it for it in reversed(items)}.values())[::-1]
    os.makedirs(OUT, exist_ok=True)
    check = "--check" in sys.argv
    changed = 0

    for it in items:
        cat = it.get("category", "")
        use_art = (cat in ART_CATS) or (cat == "Templates" and is_rich_visual_template(it))
        data = art_webp(it) if use_art else None
        if data is None:
            data = render_brand_card(it)
        p = os.path.join(OUT, f"{it['id']}.webp")
        if not os.path.exists(p) or open(p, "rb").read() != data:
            changed += 1
            if not check:
                open(p, "wb").write(data)
        old_svg = os.path.join(OUT, f"{it['id']}.svg")
        if os.path.exists(old_svg) and not check:
            os.remove(old_svg)

    print(f"{'differ' if check else 'written'}: {changed} / {len(items)} library thumbnails")
    return 1 if (check and changed) else 0


if __name__ == "__main__":
    sys.exit(main())
