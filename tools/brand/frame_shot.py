"""Frame app screenshots to the brand screenshot standard (BRAND.md "Screenshot standard").

What: takes raw captures (whole window from macOS Cmd+Shift+4 Space, or a node crop from scripts/node_screenshot.py)
and writes each onto a brand ground: the shot keeps its pixels (never rescaled up), gets the window corner radius,
a soft shadow, and an even margin of 6% of the long side. Output sizes snap to a ratio from brand.json channels:
16:9 (default, site and slides), 1:1, 4:5. Checks that the capture is 2x (>= 2400 px wide for a window,
>= 600 px for a node) and warns otherwise.
Why: screenshots were cropped and padded by hand, each with a different margin, ground and shadow.

Usage:
    python3 tools/brand/frame_shot.py SHOT.png [SHOT2.png ...] [--mode midnight|mist|paper] [--ratio 16:9|1:1|4:5|none]
        [--radius 20] [--out docs/brand/img/screenshots]
Exit codes: 0 framed, 1 a file could not be read, 2 usage error. Low-resolution captures are warned, not failed.
"""
import argparse
import json
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
B = json.load(open(os.path.join(ROOT, "docs", "brand", "brand.json")))


def hexrgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def frame(src, mode, ratio, radius):
    shot = Image.open(src).convert("RGBA")
    w, h = shot.size
    m = int(0.06 * max(w, h))
    W, H = w + 2 * m, h + 2 * m
    if ratio != "none":
        rw, rh = map(int, ratio.split(":"))
        if W / H < rw / rh:
            W = round(H * rw / rh)
        else:
            H = round(W * rh / rw)
    k = 2 if w >= 1200 else 1                          # radius and shadow are in points; a 2x capture doubles them
    r = radius * k
    ground = hexrgb(B["colour"]["modes"][mode]["ground"])
    shadow = hexrgb(B["colour"]["modes"][mode]["shadow"])
    out = Image.new("RGBA", (W, H), ground + (255,))
    x, y = (W - w) // 2, (H - h) // 2
    sh = Image.new("RGBA", (W, H), (0, 0, 0, 0))     # two-layer shadow: contact + ambient (shape.shadow in brand.json)
    d = ImageDraw.Draw(sh)
    d.rounded_rectangle((x, y + 12 * k, x + w, y + h + 12 * k), r, fill=shadow + (70 if mode == "midnight" else 40,))
    out.alpha_composite(sh.filter(ImageFilter.GaussianBlur(24 * k)))
    sh = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle((x, y + 1 * k, x + w, y + h + 1 * k), r, fill=shadow + (60,))
    out.alpha_composite(sh.filter(ImageFilter.GaussianBlur(1.5 * k)))
    mask = Image.new("L", (w * 4, h * 4), 0)          # 4x supersampled corner mask
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, w * 4 - 1, h * 4 - 1), r * 4, fill=255)
    mask = mask.resize((w, h), Image.LANCZOS)
    a = shot.split()[3]
    shot.putalpha(ImageChops.darker(a, mask))
    out.alpha_composite(shot, (x, y))
    return out.convert("RGB"), w


def main():
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("shots", nargs="*")
    ap.add_argument("--mode", default="midnight", choices=list(B["colour"]["modes"]))
    ap.add_argument("--ratio", default="16:9", choices=["16:9", "1:1", "4:5", "none"])
    ap.add_argument("--radius", type=int, default=20)
    ap.add_argument("--out", default=os.path.join(ROOT, "docs", "brand", "img", "screenshots"))
    ap.add_argument("-h", "--help", action="store_true")
    a, rest = ap.parse_known_args()
    if a.help or rest or not a.shots:
        print(__doc__)
        return 2
    os.makedirs(a.out, exist_ok=True)
    bad = 0
    for s in a.shots:
        try:
            img, w = frame(s, a.mode, a.ratio, a.radius)
        except OSError as e:
            print(f"BAD {s}: {e}")
            bad += 1
            continue
        name = os.path.splitext(os.path.basename(s))[0]
        dst = os.path.join(a.out, f"{name}.png")
        img.save(dst, optimize=True)
        warn = "  (warning: below 2x, recapture on a Retina screen)" if w < 600 else ""
        print(f"ok  {os.path.relpath(dst, ROOT)}  {img.width}x{img.height}{warn}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
