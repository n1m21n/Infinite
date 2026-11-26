#!/usr/bin/env python3
"""Generates the 1200x675 hero diagram for chop-and-remix-a-drum-loop-with-slicer.
Adheres strictly to docs/brand/BRAND.md and brand.json (Midnight deep #0D1020 mode).
"""
import math
import os
from PIL import Image, ImageDraw, ImageFont

W, H = 1200, 675
img = Image.new("RGBA", (W, H), (13, 16, 32, 255))
draw = ImageDraw.Draw(img)

# Brand tokens (Midnight mode)
DEEP = (13, 16, 32, 255)          # #0D1020
GROUND = (21, 25, 48, 255)        # #151930
SURF1 = (32, 38, 63, 255)         # #20263F
SURF2 = (42, 48, 82, 255)         # #2A3052
SURF3 = (52, 59, 94, 255)         # #343B5E
DOT = (35, 42, 74, 255)           # #232A4A
INK = (238, 241, 250, 255)        # #EEF1FA
INK2 = (163, 170, 203, 255)       # #A3AACB
INK3 = (136, 144, 177, 255)       # #8890B1
SIGNAL = (167, 177, 255, 255)     # #A7B1FF
EMBER = (255, 107, 53, 255)       # #FF6B35
GREEN = (74, 222, 128, 255)       # #4ADE80
CYAN = (56, 189, 248, 255)        # #38BDF8

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FONT_DIR = os.path.join(REPO_ROOT, "art", "brand", "fonts")
GEIST_SEMIBOLD = os.path.join(FONT_DIR, "Geist-SemiBold.ttf")
GEIST_MEDIUM = os.path.join(FONT_DIR, "Geist-Medium.ttf")
GEIST_MONO = os.path.join(FONT_DIR, "GeistMono-Medium.ttf")

f_title = ImageFont.truetype(GEIST_SEMIBOLD, 26)
f_sub = ImageFont.truetype(GEIST_MEDIUM, 14)
f_badge = ImageFont.truetype(GEIST_MONO, 11)
f_node = ImageFont.truetype(GEIST_SEMIBOLD, 15)
f_label = ImageFont.truetype(GEIST_MEDIUM, 12)
f_mono = ImageFont.truetype(GEIST_MONO, 11)
f_mono_sm = ImageFont.truetype(GEIST_MONO, 10)


def rrect(box, r, fill, outline=None, width=1):
    draw.rounded_rectangle(box, radius=r, fill=fill, outline=outline, width=width)


def cable_h(x1, y1, x2, y2, color, width=3):
    mx = (x1 + x2) // 2
    pts = []
    steps = 24
    for i in range(steps + 1):
        t = i / steps
        mt = 1 - t
        bx = mt**3 * x1 + 3 * mt**2 * t * mx + 3 * mt * t**2 * mx + t**3 * x2
        by = mt**3 * y1 + 3 * mt**2 * t * y1 + 3 * mt * t**2 * y2 + t**3 * y2
        pts.append((bx, by))
    draw.line(pts, fill=color, width=width)
    draw.ellipse((x1 - 4, y1 - 4, x1 + 4, y1 + 4), fill=color)
    draw.ellipse((x2 - 4, y2 - 4, x2 + 4, y2 + 4), fill=color)


def draw_node(x, y, w, h, title, accent, rows, badge=None):
    rrect((x, y, x + w, y + h), 12, GROUND, outline=SURF3, width=2)
    rrect((x, y, x + w, y + 34), 12, SURF1, outline=SURF3, width=1)
    draw.rectangle((x + 1, y + 22, x + w - 1, y + 34), fill=SURF1)
    draw.ellipse((x + 12, y + 12, x + 20, y + 20), fill=accent)
    draw.text((x + 28, y + 8), title, font=f_node, fill=INK)
    if badge:
        draw.text((x + w - 12 - len(badge) * 7, y + 10), badge, font=f_mono_sm, fill=accent)
    ry = y + 46
    for k, v in rows:
        draw.text((x + 14, ry), k, font=f_label, fill=INK3)
        rrect((x + w - 82, ry - 2, x + w - 14, ry + 16), 5, SURF2, outline=SURF3)
        draw.text((x + w - 74, ry + 1), v, font=f_mono_sm, fill=INK)
        ry += 24


# Subtle dot grid background
for gx in range(24, W, 24):
    for gy in range(24, H, 24):
        draw.ellipse((gx - 1, gy - 1, gx + 1, gy + 1), fill=DOT)

# Header
draw.text((48, 34), "Chop and remix a drum loop with Slicer", font=f_title, fill=INK)
draw.text((48, 70), "Split breaks by onset or grid, trigger slices from C1, and shape with filter, Delay, and Reverb", font=f_sub, fill=INK2)

rrect((980, 36, 1152, 64), 14, SURF1, outline=SURF3)
draw.text((998, 44), "CREATIVE · HOW-TO", font=f_badge, fill=SIGNAL)

# Main patch canvas frame
rrect((48, 110, 1152, 560), 16, (17, 20, 38, 255), outline=SURF2, width=1)

# Left column: Note Sequencer and MIDI Input -> Slicer
draw_node(78, 150, 210, 134, "Note Sequencer", SIGNAL,
          [("steps", "16"), ("base", "C1"), ("gate", "75%")])

draw_node(78, 326, 210, 110, "MIDI Input", SIGNAL,
          [("channel", "All"), ("note", "C1..G2")])

# Centre: Featured Slicer node with waveform and slice markers
SX, SY, SW, SH = 340, 145, 310, 330
rrect((SX, SY, SX + SW, SY + SH), 12, GROUND, outline=CYAN, width=2)
rrect((SX, SY, SX + SW, SY + 34), 12, SURF1, outline=SURF3, width=1)
draw.rectangle((SX + 1, SY + 22, SX + SW - 1, SY + 34), fill=SURF1)
draw.ellipse((SX + 12, SY + 12, SX + 20, SY + 20), fill=CYAN)
draw.text((SX + 28, SY + 8), "Slicer", font=f_node, fill=INK)
draw.text((SX + SW - 74, SY + 10), "8 SLICES", font=f_mono_sm, fill=CYAN)

# Waveform box inside Slicer
WX0, WY0, WX1, WY1 = SX + 16, SY + 48, SX + SW - 16, SY + 162
rrect((WX0, WY0, WX1, WY1), 8, DEEP, outline=SURF3, width=1)
mid_y = (WY0 + WY1) // 2
slice_xs = [WX0 + 6, WX0 + 42, WX0 + 76, WX0 + 114, WX0 + 148, WX0 + 186, WX0 + 222, WX0 + 250]
for i in range(WX0 + 8, WX1 - 8, 4):
    # Pseudo-transient drum envelope
    dist = min((i - sx for sx in slice_xs if i >= sx), default=99)
    amp = max(6, int(44 * math.exp(-dist / 13.0)))
    col = EMBER if dist < 6 else SIGNAL
    draw.line([(i, mid_y - amp), (i, mid_y + amp)], fill=col, width=2)

for idx, sx in enumerate(slice_xs):
    draw.line([(sx, WY0 + 4), (sx, WY1 - 4)], fill=EMBER if idx in (0, 3) else SURF3, width=1)
    draw.text((sx + 3, WY0 + 5), f"{idx + 1}", font=f_mono_sm, fill=INK2)

# Slicer params
s_rows = [
    ("slice by", "onsets"),
    ("sensitivity", "64%"),
    ("pitch / speed", "-2 st / 1.00x"),
    ("decay", "hold"),
    ("crossthrough", "off"),
]
ry = WY1 + 18
for k, v in s_rows:
    draw.text((SX + 16, ry), k, font=f_label, fill=INK3)
    rrect((SX + SW - 122, ry - 2, SX + SW - 16, ry + 16), 5, SURF2, outline=SURF3)
    draw.text((SX + SW - 114, ry + 1), v, font=f_mono_sm, fill=INK)
    ry += 26

# Right side: Audio Filter -> Mixer -> Delay / Reverb -> Audio Out
draw_node(705, 150, 185, 114, "Audio Filter", GREEN,
          [("mode", "Lowpass"), ("cutoff", "3.2 kHz")])

draw_node(705, 308, 185, 108, "LFO", EMBER,
          [("rate", "4 bars"), ("shape", "Sine")], badge="MOD")

draw_node(945, 150, 175, 114, "Mixer", GREEN,
          [("ch 1", "0.0 dB"), ("ch 2", "-4.0 dB")])

draw_node(945, 300, 175, 114, "Delay & Reverb", GREEN,
          [("rate", "1/8 dot"), ("mix", "22%")])

draw_node(945, 444, 175, 84, "Audio Out", EMBER,
          [("output", "Stereo 1-2")])

# Cables
cable_h(288, 210, 340, 210, SIGNAL)
cable_h(288, 376, 340, 236, SIGNAL)
cable_h(650, 210, 705, 200, GREEN)
cable_h(798, 308, 798, 264, EMBER)
cable_h(890, 200, 945, 200, GREEN)
cable_h(1032, 264, 1032, 300, GREEN)
cable_h(1032, 414, 1032, 444, EMBER)

# Legend footer
draw.ellipse((48, 594, 58, 604), fill=SIGNAL)
draw.text((66, 592), "Note / MIDI trigger (C1+)", font=f_label, fill=INK2)

draw.ellipse((270, 594, 280, 604), fill=CYAN)
draw.text((288, 592), "Slicer instrument", font=f_label, fill=INK2)

draw.ellipse((450, 594, 460, 604), fill=GREEN)
draw.text((468, 592), "Audio & effects", font=f_label, fill=INK2)

draw.ellipse((620, 594, 630, 604), fill=EMBER)
draw.text((638, 592), "Modulation & Audio Out", font=f_label, fill=INK2)

draw.text((940, 592), "infinitedevices.org", font=f_mono, fill=INK3)

out_path = os.path.join(REPO_ROOT, "website", "assets", "blog_chop-and-remix-a-drum-loop-with-slicer.png")
os.makedirs(os.path.dirname(out_path), exist_ok=True)
img.save(out_path, "PNG")
print("Wrote", out_path)
