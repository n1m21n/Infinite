"""Draws the 1200x630 social preview for the samples and library pages.

What: writes website/assets/og-samples.jpg and og-library.jpg (brand navy, the gradient blob, Geist title).
Why:  both pages shared a nebula photo that looked off in link previews; each page now has its own card.
Usage: python3 tools/website/gen_og_pages.py
"""
import math, os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
FONT = os.path.join(ROOT, "art", "brand", "fonts")
W, H = 1200, 630
STOPS = [(245, 127, 102), (255, 160, 90), (190, 110, 230), (110, 120, 255)]


def blob(cx, cy, r, seed):
    layer = Image.new("RGB", (W, H), (0, 0, 0))
    d = ImageDraw.Draw(layer)
    for i, c in enumerate(STOPS):
        a = seed + i * 1.7
        x, y = cx + math.cos(a) * r * 0.45, cy + math.sin(a * 1.3) * r * 0.4
        d.ellipse([x - r * 0.7, y - r * 0.7, x + r * 0.7, y + r * 0.7], fill=c)
    layer = layer.filter(ImageFilter.GaussianBlur(r * 0.35))
    mask = Image.new("L", (W, H), 0)
    ImageDraw.Draw(mask).ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    return layer, mask.filter(ImageFilter.GaussianBlur(6))


def card(name, title, sub, seed):
    img = Image.new("RGB", (W, H), (13, 17, 36))
    glow = Image.new("RGB", (W, H), (13, 17, 36))
    ImageDraw.Draw(glow).ellipse([640, 40, 1240, 640], fill=(40, 34, 90))
    img = Image.blend(img, glow.filter(ImageFilter.GaussianBlur(120)), 0.9)
    layer, mask = blob(900, 315, 230, seed)
    img.paste(layer, (0, 0), mask)
    d = ImageDraw.Draw(img)
    big = ImageFont.truetype(os.path.join(FONT, "Geist-ExtraBold.ttf"), 78)
    mid = ImageFont.truetype(os.path.join(FONT, "Geist-Medium.ttf"), 32)
    mono = ImageFont.truetype(os.path.join(FONT, "GeistMono-Medium.ttf"), 26)
    d.text((70, 90), "INFINITE", font=mono, fill=(245, 127, 102))
    y = 190
    for line in title:
        d.text((70, y), line, font=big, fill=(255, 255, 255))
        y += 92
    y += 20
    for line in sub:
        d.text((70, y), line, font=mid, fill=(190, 198, 225))
        y += 44
    d.text((70, 560), "infinitedevices.org", font=mono, fill=(140, 150, 190))
    img.save(os.path.join(ROOT, "website", "assets", name), quality=90)


card("og-samples.jpg", ["Free unlimited", "samples"], ["Drums, bass, keys, pads, textures.", "Any key and scale. No sign-up."], 0.6)
card("og-library.jpg", ["The Infinite", "library"], ["Templates, shaders, sounds and skills.", "Open any of it in Infinite."], 2.1)
