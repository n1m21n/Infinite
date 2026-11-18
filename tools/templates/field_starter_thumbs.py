#!/usr/bin/env python3
"""Writes assets/templates/thumbs/fieldstarter-N.png for every Field patcher starter: a node card with the
starter's name, its kind chip and the first lines of its code, on the template canvas colour (320 x 320).
    python3 tools/templates/field_starter_thumbs.py"""
import os, re
from PIL import Image, ImageDraw, ImageFont
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = open(os.path.join(ROOT, "src/core/field/FieldPatcherStarters.h")).read()
OUT = os.path.join(ROOT, "assets/templates/thumbs")
BG = (21, 25, 48)
KIND = {"element": ((120, 200, 255), "POINTS"), "notes": ((255, 170, 90), "NOTES"),
        "pixel": ((255, 110, 130), "PIXELS"), "sample": ((120, 220, 150), "SOUND")}
sans = ImageFont.truetype(os.path.join(ROOT, "art/brand/fonts/Geist-Medium.ttf"), 22)
chip = ImageFont.truetype(os.path.join(ROOT, "art/brand/fonts/GeistMono-Medium.ttf"), 12)
mono = ImageFont.truetype(os.path.join(ROOT, "external/fonts/GeistMono/GeistMono-Regular.ttf"), 12)
items = []
for m in re.finditer(r'\{ "([^"]+)", "(\w+)", (true|false),\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\}', SRC):
    code = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(4))).encode().decode("unicode_escape")
    items.append((m.group(1), m.group(2), m.group(3) == "true", code))
for k, (name, dom, val, code) in enumerate(items):
    col, label = KIND[dom]
    if dom == "element" and val:
        label = "MODULATOR"
    im = Image.new("RGB", (320, 320), BG); d = ImageDraw.Draw(im)
    x0, y0, x1, y1 = 24, 40, 296, 280
    d.rounded_rectangle((x0, y0, x1, y1), 14, fill=(34, 40, 72), outline=col, width=2)
    d.rounded_rectangle((x0, y0, x1, y0 + 44), 14, fill=col)
    d.rectangle((x0, y0 + 30, x1, y0 + 44), fill=col)
    d.text((x0 + 14, y0 + 9), name, font=sans, fill=BG)
    d.text((x0 + 14, y0 + 56), label, font=chip, fill=col)
    for i, line in enumerate([l for l in code.split("\n") if l][:9]):
        d.text((x0 + 14, y0 + 80 + i * 17), line[:30] + ("…" if len(line) > 30 else ""), font=mono, fill=(200, 208, 235))
    d.ellipse((x0 - 6, 154, x0 + 6, 166), fill=col); d.ellipse((x1 - 6, 154, x1 + 6, 166), fill=col)
    im.save(os.path.join(OUT, f"fieldstarter-{k}.png"))
print(len(items), "thumbnails")
