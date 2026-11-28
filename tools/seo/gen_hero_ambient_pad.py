#!/usr/bin/env python3
"""Generates the 1200x675 hero diagram for stretch-a-sample-into-an-ambient-pad.
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
f_title = ImageFont.truetype(os.path.join(FONT_DIR, "Geist-SemiBold.ttf"), 26)
f_sub = ImageFont.truetype(os.path.join(FONT_DIR, "Geist-Medium.ttf"), 14)
f_badge = ImageFont.truetype(os.path.join(FONT_DIR, "GeistMono-Medium.ttf"), 11)
f_node = ImageFont.truetype(os.path.join(FONT_DIR, "Geist-SemiBold.ttf"), 15)
f_label = ImageFont.truetype(os.path.join(FONT_DIR, "Geist-Medium.ttf"), 12)
f_mono = ImageFont.truetype(os.path.join(FONT_DIR, "GeistMono-Medium.ttf"), 11)
f_mono_sm = ImageFont.truetype(os.path.join(FONT_DIR, "GeistMono-Medium.ttf"), 10)


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


def cable_v(x1, y1, x2, y2, color, width=2):
    my = (y1 + y2) // 2
    pts = []
    steps = 24
    for i in range(steps + 1):
        t = i / steps
        mt = 1 - t
        bx = mt**3 * x1 + 3 * mt**2 * t * x1 + 3 * mt * t**2 * x2 + t**3 * x2
        by = mt**3 * y1 + 3 * mt**2 * t * my + 3 * mt * t**2 * my + t**3 * y2
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

# Header badge + title
rrect((48, 36, 224, 62), 13, SURF1, outline=SURF3)
draw.text((62, 43), "AMBIENT SOUND DESIGN", font=f_badge, fill=SIGNAL)
draw.text((48, 76), "Stretch any sample into an ambient pad", font=f_title, fill=INK)
draw.text((48, 112), "Paul Stretch  ->  Audio Filter (LFO)  ->  Chorus  ->  Reverb  ->  Audio Out", font=f_sub, fill=INK2)

# LFO node above Audio Filter
lfo_x, lfo_y, lfo_w, lfo_h = 370, 166, 195, 112
draw_node(lfo_x, lfo_y, lfo_w, lfo_h, "LFO", CYAN, [
    ("shape", "sine"),
    ("rate", "0.08 Hz"),
], badge="MOD")

# Paul Stretch node (tall, with waveform)
ps_x, ps_y, ps_w, ps_h = 48, 210, 260, 290
draw_node(ps_x, ps_y, ps_w, ps_h, "Paul Stretch", EMBER, [], badge="SAMPLE")
# Waveform box inside Paul Stretch
wf_x, wf_y, wf_w, wf_h = ps_x + 14, ps_y + 46, ps_w - 28, 74
rrect((wf_x, wf_y, wf_x + wf_w, wf_y + wf_h), 8, DEEP, outline=SURF3)
for i in range(44):
    bx = wf_x + 10 + i * 5
    amp = int(8 + 20 * abs(math.sin(i * 0.27)) * (0.5 + 0.5 * math.cos(i * 0.11)))
    cy = wf_y + wf_h // 2
    draw.line((bx, cy - amp, bx, cy + amp), fill=EMBER if 6 <= i <= 36 else SURF3, width=2)
# Loop bounds markers
draw.line((wf_x + 38, wf_y + 6, wf_x + 38, wf_y + wf_h - 6), fill=GREEN, width=2)
draw.line((wf_x + 190, wf_y + 6, wf_x + 190, wf_y + wf_h - 6), fill=GREEN, width=2)

# Paul Stretch parameters
ry = wf_y + wf_h + 16
for k, v in [
    ("stretch", "48.0x"),
    ("phase rand", "1.00"),
    ("unison", "4 v"),
    ("detune", "14.0 c"),
    ("pitch", "-12.0 st"),
    ("loop", "on"),
]:
    draw.text((ps_x + 14, ry), k, font=f_label, fill=INK3)
    rrect((ps_x + ps_w - 86, ry - 2, ps_x + ps_w - 14, ry + 16), 5, SURF2, outline=SURF3)
    draw.text((ps_x + ps_w - 78, ry + 1), v, font=f_mono_sm, fill=INK)
    ry += 25

# Audio Filter
af_x, af_y, af_w, af_h = 370, 326, 195, 160
draw_node(af_x, af_y, af_w, af_h, "Audio Filter", SIGNAL, [
    ("type", "lowpass"),
    ("freq", "1.4 kHz"),
    ("Q", "0.70"),
    ("mix", "1.00"),
], badge="FX")

# Chorus
ch_x, ch_y, ch_w, ch_h = 620, 326, 185, 160
draw_node(ch_x, ch_y, ch_w, ch_h, "Chorus", SIGNAL, [
    ("depth", "4.2 ms"),
    ("spread", "0.75"),
    ("feedback", "0.20"),
    ("mix", "0.45"),
], badge="FX")

# Reverb
rv_x, rv_y, rv_w, rv_h = 860, 326, 185, 160
draw_node(rv_x, rv_y, rv_w, rv_h, "Reverb", SIGNAL, [
    ("size", "0.88"),
    ("decay", "6.40 s"),
    ("damping", "0.45"),
    ("mix", "0.55"),
], badge="FX")

# Audio Out
ao_x, ao_y, ao_w, ao_h = 1085, 360, 82, 92
rrect((ao_x, ao_y, ao_x + ao_w, ao_y + ao_h), 12, GROUND, outline=GREEN, width=2)
rrect((ao_x, ao_y, ao_x + ao_w, ao_y + 30), 12, SURF1, outline=SURF3, width=1)
draw.rectangle((ao_x + 1, ao_y + 18, ao_x + ao_w - 1, ao_y + 30), fill=SURF1)
draw.ellipse((ao_x + 10, ao_y + 10, ao_x + 18, ao_y + 18), fill=GREEN)
draw.text((ao_x + 24, ao_y + 7), "Out", font=f_node, fill=INK)
draw.text((ao_x + 14, ao_y + 46), "Stereo", font=f_label, fill=INK2)
draw.text((ao_x + 14, ao_y + 64), "-3.2 dB", font=f_mono_sm, fill=GREEN)

# Audio signal cables
cable_h(ps_x + ps_w, ps_y + 190, af_x, af_y + 72, SIGNAL)
cable_h(af_x + af_w, af_y + 72, ch_x, ch_y + 72, SIGNAL)
cable_h(ch_x + ch_w, ch_y + 72, rv_x, rv_y + 72, SIGNAL)
cable_h(rv_x + rv_w, rv_y + 72, ao_x, ao_y + 52, GREEN)

# Modulation cable from LFO -> Audio Filter freq
cable_v(lfo_x + lfo_w // 2, lfo_y + lfo_h, af_x + af_w - 28, af_y + 70, CYAN)

# Bottom tip bar
rrect((48, 558, W - 48, 634), 12, SURF1, outline=SURF3)
draw.text((70, 576), "BEGINNER WORKFLOW", font=f_badge, fill=EMBER)
draw.text((70, 598), "Load any short chord or field recording into Paul Stretch, raise stretch to 20x+, and sweep Audio Filter with an LFO.", font=f_sub, fill=INK2)

out_path = os.path.join(REPO_ROOT, "website", "assets", "blog_stretch-a-sample-into-an-ambient-pad.png")
img.convert("RGB").save(out_path, "PNG", optimize=True)
print(f"Saved {out_path} ({os.path.getsize(out_path)} bytes)")
