#!/usr/bin/env python3
"""Draw the installer art from docs/brand: Midnight ground, the gradient mark, Geist.

usage: make_art.py <out-dir>
  mac/background.png, mac/background-dark.png   Installer.app window art (bottom-left mark, 2x)
  windows/wizard.bmp, windows/wizard-small.bmp  Inno Setup side panel (164x314 at 2x) and header mark
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
FONTS = os.path.join(ROOT, "art", "brand", "fonts")
MARK = os.path.join(ROOT, "art", "brand", "logo", "png", "mark-512.png")

MIDNIGHT = (0x15, 0x19, 0x30)   # ground (dark)
DEEP = (0x0D, 0x10, 0x20)       # wells
PAPER = (0xFB, 0xFA, 0xF6)      # ground (paper)
MOON = (0xEE, 0xF1, 0xFA)       # ink on dark
INK3_DARK = (0x88, 0x90, 0xB1)  # ink-3 on dark
INK = (0x12, 0x15, 0x2A)        # ink on paper
INK3 = (0x5A, 0x61, 0x80)       # ink-3 on paper


def font(size, weight="SemiBold"):
    for p in (os.path.join(FONTS, f"Geist-{weight}.ttf"), "/System/Library/Fonts/SFNS.ttf"):
        try:
            return ImageFont.truetype(p, size)
        except OSError:
            pass
    return ImageFont.load_default()


def mark(width):
    # The mark is wide (512x219): scale by width, keep its shape.
    m = Image.open(MARK).convert("RGBA")
    return m.resize((width, round(width * m.height / m.width)), Image.LANCZOS)


def mac(out, dark):
    # Installer.app draws the art under its whole content pane; the mark sits bottom-left, away from text.
    S = 2
    W, H = 620, 418
    img = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    m = mark(96 * S)
    img.alpha_composite(m, (28 * S, (H - 28) * S - m.height))
    img.save(out, dpi=(72 * S, 72 * S))


def wizard(out):
    # Inno Setup's big side image (Welcome and Finished pages): 164x314 at 100%, drawn at 2x.
    S = 2
    W, H = 164 * S, 314 * S
    img = Image.new("RGB", (W, H), MIDNIGHT)
    d = ImageDraw.Draw(img)
    d.rectangle([0, H - 64 * S, W, H], fill=DEEP)
    m = mark(112 * S)
    img.paste(m, ((W - m.width) // 2, 72 * S + (88 * S - m.height) // 2), m)
    f = font(19 * S, "SemiBold")
    t = "Infinite"
    d.text(((W - d.textlength(t, font=f)) / 2, 178 * S), t, fill=MOON, font=f)
    f2 = font(11 * S, "Medium")
    t2 = "Instrument  ·  FX  ·  App"
    d.text(((W - d.textlength(t2, font=f2)) / 2, 208 * S), t2, fill=INK3_DARK, font=f2)
    img.save(out)


def wizard_small(out):
    # Header mark on the inner pages: 55x55 at 100%, drawn at 2x on the header's white.
    S = 2
    img = Image.new("RGB", (55 * S, 55 * S), (0xFF, 0xFF, 0xFF))
    m = mark(46 * S)
    img.paste(m, ((img.width - m.width) // 2, (img.height - m.height) // 2), m)
    img.save(out)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(os.path.join(out, "mac"), exist_ok=True)
    os.makedirs(os.path.join(out, "windows"), exist_ok=True)
    mac(os.path.join(out, "mac", "background.png"), False)
    mac(os.path.join(out, "mac", "background-dark.png"), True)
    wizard(os.path.join(out, "windows", "wizard.bmp"))
    wizard_small(os.path.join(out, "windows", "wizard-small.bmp"))


if __name__ == "__main__":
    main()
