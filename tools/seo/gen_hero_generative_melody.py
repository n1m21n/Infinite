#!/usr/bin/env python3
"""Generates the 1200x675 hero diagram for generative-melody-and-chords-in-infinite.
Adheres strictly to docs/brand/BRAND.md and brand.json (Midnight deep #0D1020 mode).
"""
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
draw.text((48, 34), "Generative melody & chord progression patch", font=f_title, fill=INK)
draw.text((48, 70), "Patch Chorder, Arpeggiator, and Random Note Generator into Analog, Delay, and Reverb", font=f_sub, fill=INK2)

rrect((980, 36, 1152, 64), 14, SURF1, outline=SURF3)
draw.text((998, 44), "CREATIVE · HOW-TO", font=f_badge, fill=SIGNAL)

# Main patch canvas frame
rrect((48, 110, 1152, 560), 16, (17, 20, 38, 255), outline=SURF2, width=1)

# Top row (Chords & Arp): Chorder -> Arpeggiator -> Analog (Chords)
draw_node(78, 145, 195, 134, "Chorder", SIGNAL,
          [("scale", "Minor"), ("chords", "random"), ("notes / strum", "4 / 18ms")])

draw_node(328, 145, 195, 134, "Arpeggiator", SIGNAL,
          [("mode", "Up/Down"), ("octaves", "2"), ("gate", "68%")])

draw_node(578, 145, 185, 134, "Analog (Pad)", CYAN,
          [("freq", "1.4 kHz"), ("spread", "55%"), ("analog", "on")])

# Bottom row (Melody & LFO): Random Note Generator -> Analog (Lead), LFO -> Analog (Pad)
draw_node(78, 365, 225, 134, "Random Note Gen", SIGNAL,
          [("scale / root", "Minor / A"), ("wander", "4"), ("groove", "32%")])

draw_node(365, 365, 185, 134, "Analog (Lead)", CYAN,
          [("freq", "2.8 kHz"), ("detune", "14%"), ("voices", "3")])

draw_node(595, 395, 168, 104, "LFO", EMBER,
          [("rate", "4 bars"), ("shape", "Sine")], badge="MOD")

# Right column: Mixer -> Delay -> Reverb -> Audio Out
draw_node(805, 200, 158, 116, "Mixer", GREEN,
          [("ch 1", "0.0 dB"), ("ch 2", "-3.5 dB")])

draw_node(805, 360, 158, 116, "Delay", GREEN,
          [("rate", "1/8 dot"), ("bounce", "on")])

draw_node(995, 200, 132, 116, "Reverb", GREEN,
          [("size", "72%"), ("mix", "28%")])

draw_node(995, 360, 132, 92, "Audio Out", INK,
          [("output", "Stereo")])

# Cables (Note = SIGNAL, Audio = GREEN, Mod = EMBER)
cable_h(273, 212, 328, 212, SIGNAL)      # Chorder -> Arp
cable_h(523, 212, 578, 212, SIGNAL)      # Arp -> Analog (Pad)
cable_h(303, 432, 365, 432, SIGNAL)      # Random Note Gen -> Analog (Lead)

cable_h(763, 212, 805, 240, GREEN)       # Analog (Pad) -> Mixer
cable_h(550, 385, 805, 280, GREEN)       # Analog (Lead) -> Mixer (clears LFO)
draw.line([(884, 316), (884, 360)], fill=GREEN, width=3)  # Mixer -> Delay
draw.ellipse((880, 312, 888, 320), fill=GREEN)
draw.ellipse((880, 356, 888, 364), fill=GREEN)
cable_h(963, 418, 995, 258, GREEN)       # Delay -> Reverb
draw.line([(1061, 316), (1061, 360)], fill=GREEN, width=3)  # Reverb -> Audio Out
draw.ellipse((1057, 312, 1065, 320), fill=GREEN)
draw.ellipse((1057, 356, 1065, 364), fill=GREEN)

# Modulation cable from LFO to Analog (Pad) freq
draw.line([(679, 395), (679, 279)], fill=EMBER, width=2)
draw.ellipse((675, 391, 683, 399), fill=EMBER)
draw.ellipse((675, 275, 683, 283), fill=EMBER)
draw.ellipse((742, 189, 750, 197), fill=EMBER)  # orange mod dot on freq

# Legend bar at bottom
ly = 592
rrect((48, ly, 1152, ly + 46), 10, GROUND, outline=SURF2)
items = [
    (SIGNAL, "Note cables (scale-locked chords & melody)"),
    (GREEN, "Audio cables (synth -> mixer -> delay -> reverb -> speakers)"),
    (EMBER, "Modulation (slow LFO opens filter over 4 bars)"),
]
lx = 74
for col, text in items:
    draw.ellipse((lx, ly + 18, lx + 10, ly + 28), fill=col)
    draw.text((lx + 20, ly + 15), text, font=f_label, fill=INK2)
    lx += 365

out_path = os.path.join(REPO_ROOT, "website", "assets", "blog_generative-melody-and-chords-in-infinite.png")
img.save(out_path, "PNG")
print("Wrote", out_path)
