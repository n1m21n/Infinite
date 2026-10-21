#!/usr/bin/env python3
"""Generates the 1200x675 technical hero diagram for audio-reactive-glsl-shaders.
Adheres strictly to docs/brand/BRAND.md and brand.json (Midnight mode).
"""
import math, os
from PIL import Image, ImageDraw, ImageFont, ImageFilter

W, H = 1200, 675
img = Image.new("RGBA", (W, H), (21, 25, 48, 255)) # Midnight ground #151930
draw = ImageDraw.Draw(img)

# Color tokens
GROUND = (21, 25, 48, 255)       # #151930
DEEP = (13, 16, 32, 255)          # #0D1020
SURF1 = (32, 38, 63, 255)         # #20263F
SURF2 = (42, 48, 82, 255)         # #2A3052
SURF3 = (52, 59, 94, 255)         # #343B5E
LINE = (52, 59, 94, 255)          # #343B5E
DOT = (35, 42, 74, 255)           # #232A4A
INK = (238, 241, 250, 255)        # #EEF1FA
INK2 = (163, 170, 203, 255)       # #A3AACB
INK3 = (136, 144, 177, 255)       # #8890B1
SIGNAL = (167, 177, 255, 255)     # #A7B1FF
EMBER = (255, 107, 53, 255)       # #FF6B35
GREEN = (74, 222, 128, 255)

# Fonts
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
font_mono_path = os.path.join(ROOT, "art/brand/fonts/GeistMono-Medium.ttf")
font_sans_path = os.path.join(ROOT, "art/brand/fonts/Geist-SemiBold.ttf")

if not os.path.exists(font_mono_path):
    font_mono_path = "/System/Library/Fonts/Menlo.ttc"
if not os.path.exists(font_sans_path):
    font_sans_path = "/System/Library/Fonts/Helvetica.ttc"

f_title = ImageFont.truetype(font_sans_path, 20)
f_sub = ImageFont.truetype(font_mono_path, 12)
f_card_title = ImageFont.truetype(font_sans_path, 15)
f_badge = ImageFont.truetype(font_mono_path, 11)
f_code = ImageFont.truetype(font_mono_path, 12)
f_label = ImageFont.truetype(font_mono_path, 11)
f_small = ImageFont.truetype(font_mono_path, 10)

# Draw subtle dot grid
for gx in range(24, W, 24):
    for gy in range(24, H, 24):
        draw.point((gx, gy), fill=DOT)

# Top Bar
draw.text((50, 36), "INFINITE", font=f_title, fill=INK)
draw.text((150, 42), "//  REAL-TIME AUDIO-REACTIVE GLSL SHADER PIPELINE", font=f_sub, fill=SIGNAL)

badge_text = "DSP -> LOCK-FREE FIFO -> GLSL 150 -> GPU KERNEL"
bw = draw.textlength(badge_text, font=f_badge) + 20
draw.rounded_rectangle((W - 50 - bw, 36, W - 50, 62), 6, fill=SURF1, outline=LINE, width=1)
draw.text((W - 50 - bw + 10, 42), badge_text, font=f_badge, fill=INK2)

# Helper function for card
def draw_card(x, y, w, h, title, category, badge=None):
    # Card shadow
    draw.rounded_rectangle((x, y + 6, x + w, y + h + 6), 12, fill=(4, 5, 14, 160))
    # Card surface
    draw.rounded_rectangle((x, y, x + w, y + h), 12, fill=SURF1, outline=LINE, width=1)
    # Header background
    draw.rounded_rectangle((x, y, x + w, y + 42), 12, fill=SURF2)
    draw.rectangle((x, y + 30, x + w, y + 42), fill=SURF2)
    draw.line((x, y + 42, x + w, y + 42), fill=LINE, width=1)
    # Header text
    draw.text((x + 16, y + 12), title, font=f_card_title, fill=INK)
    draw.text((x + 16 + draw.textlength(title, font=f_card_title) + 12, y + 15), category, font=f_small, fill=INK3)
    if badge:
        bbw = draw.textlength(badge, font=f_small) + 12
        draw.rounded_rectangle((x + w - 16 - bbw, y + 11, x + w - 16, y + 31), 4, fill=SURF3)
        draw.text((x + w - 16 - bbw + 6, y + 14), badge, font=f_small, fill=SIGNAL)

# 1. Left Card: Audio Analyze Node
card1_x, card1_y, card1_w, card1_h = 50, 95, 310, 520
draw_card(card1_x, card1_y, card1_w, card1_h, "Audio Analyze", "DSP // ANALYSIS", "48 kHz")

# Waveform box
draw.rounded_rectangle((card1_x + 16, card1_y + 58, card1_x + card1_w - 16, card1_y + 130), 8, fill=DEEP, outline=SURF3, width=1)
draw.text((card1_x + 26, card1_y + 64), "AUDIO INPUT BUFFER", font=f_small, fill=INK3)
# Draw audio waveform
wave_pts = []
for i in range(card1_w - 52):
    wx = card1_x + 26 + i
    wy = card1_y + 98 + math.sin(i * 0.12) * 14 * math.sin(i * 0.03 + 1.2) + math.sin(i * 0.35) * 5
    wave_pts.append((wx, wy))
draw.line(wave_pts, fill=SIGNAL, width=1)

# FFT Spectrum box
draw.rounded_rectangle((card1_x + 16, card1_y + 142, card1_x + card1_w - 16, card1_y + 276), 8, fill=DEEP, outline=SURF3, width=1)
draw.text((card1_x + 26, card1_y + 148), "8-BAND FFT SPECTRUM", font=f_small, fill=INK3)

band_names = ["SUB", "BASS", "L-MID", "MID", "H-MID", "PRES", "BRILL", "AIR"]
band_heights = [74, 98, 62, 45, 52, 38, 28, 18]
bar_w = 24
bar_gap = 8
bx_start = card1_x + 26
for bi, (bname, bh) in enumerate(zip(band_names, band_heights)):
    bx = bx_start + bi * (bar_w + bar_gap)
    by_bot = card1_y + 252
    by_top = by_bot - bh
    # Bar background
    draw.rectangle((bx, by_bot - 88, bx + bar_w, by_bot), fill=SURF1)
    # Active bar (Ember for bass, Signal for rest)
    bcolor = EMBER if bi == 1 else (SIGNAL if bi in (0, 2, 3) else INK2)
    draw.rectangle((bx, by_top, bx + bar_w, by_bot), fill=bcolor)
    # Peak line
    draw.line((bx, by_top - 3, bx + bar_w, by_top - 3), fill=INK, width=1)
    draw.text((bx + 1, card1_y + 256), bname[:3], font=f_small, fill=INK3)

# Output Pins List
draw.text((card1_x + 16, card1_y + 292), "OUTPUT SIGNALS", font=f_small, fill=INK2)
pins_out = [
    ("low", "Sub-bass & Kick envelope", EMBER, card1_y + 320),
    ("mid", "Vocal & Harmonic band", SIGNAL, card1_y + 370),
    ("high", "Hi-hat & Transient onset", SIGNAL, card1_y + 420),
    ("level", "RMS overall energy", INK2, card1_y + 470)
]

out_pin_coords = {}
for pname, pdesc, pcol, py in pins_out:
    draw.rounded_rectangle((card1_x + 16, py, card1_x + card1_w - 16, py + 38), 6, fill=SURF2, outline=SURF3, width=1)
    draw.text((card1_x + 28, py + 6), pname, font=f_card_title, fill=pcol)
    draw.text((card1_x + 28, py + 22), pdesc, font=f_small, fill=INK3)
    # Pin connector circle on right
    pin_cx = card1_x + card1_w - 16
    pin_cy = py + 19
    draw.ellipse((pin_cx - 6, pin_cy - 6, pin_cx + 6, pin_cy + 6), fill=DEEP, outline=pcol, width=2)
    draw.ellipse((pin_cx - 2, pin_cy - 2, pin_cx + 2, pin_cy + 2), fill=pcol)
    out_pin_coords[pname] = (pin_cx, pin_cy)


# 2. Middle Card: Field Pixel Node
card2_x, card2_y, card2_w, card2_h = 420, 95, 450, 520
draw_card(card2_x, card2_y, card2_w, card2_h, "Field Pixel", "GPU // GLSL KERNEL", "GLSL 150")

# Input Pins on left of card 2
in_pins = [
    ("bass", EMBER, card2_y + 80),
    ("mid", SIGNAL, card2_y + 130),
    ("treble", SIGNAL, card2_y + 180)
]
in_pin_coords = {}
for pname, pcol, py in in_pins:
    pin_cx = card2_x
    pin_cy = py
    draw.ellipse((pin_cx - 6, pin_cy - 6, pin_cx + 6, pin_cy + 6), fill=DEEP, outline=pcol, width=2)
    draw.ellipse((pin_cx - 2, pin_cy - 2, pin_cx + 2, pin_cy + 2), fill=pcol)
    draw.text((card2_x + 14, py - 6), pname, font=f_label, fill=pcol)
    in_pin_coords[pname] = (pin_cx, pin_cy)

# Code editor frame
draw.rounded_rectangle((card2_x + 16, card2_y + 215, card2_x + card2_w - 16, card2_y + 465), 8, fill=DEEP, outline=SURF3, width=1)
# Header of editor
draw.rectangle((card2_x + 16, card2_y + 215, card2_x + card2_w - 16, card2_y + 242), fill=SURF2)
draw.line((card2_x + 16, card2_y + 242, card2_x + card2_w - 16, card2_y + 242), fill=SURF3, width=1)
draw.text((card2_x + 28, card2_y + 222), "kernel.field  ->  GPU fragment program", font=f_small, fill=INK2)

code_lines = [
    ("1", "param float bass = 0.0 [0.0, 1.0];", EMBER),
    ("2", "param float treble = 0.0 [0.0, 1.0];", SIGNAL),
    ("3", "vec2 p = (uv - 0.5) * vec2(aspect, 1.0);", INK2),
    ("4", "float r = length(p);", INK2),
    ("5", "float ripple = sin(r * 28.0 - t * 4.0);", INK),
    ("6", "float ring = smoothstep(0.015, 0.0,", INK),
    ("7", "    abs(r - 0.28 - bass * 0.18) - ripple * 0.02);", EMBER),
    ("8", "vec3 c = mix(vec3(0.08,0.1,0.2), vec3(1,0.42,0.2), ring);", INK2),
    ("9", "col = c + vec3(0.65,0.7,1.0) * treble * (1.0 - r);", SIGNAL),
]

cy = card2_y + 252
for lnum, ltext, lcol in code_lines:
    draw.text((card2_x + 26, cy), lnum, font=f_code, fill=INK3)
    draw.text((card2_x + 50, cy), ltext, font=f_code, fill=lcol)
    cy += 21

# Footer status of Field Pixel
draw.rounded_rectangle((card2_x + 16, card2_y + 478, card2_x + card2_w - 16, card2_y + 506), 6, fill=SURF2)
draw.ellipse((card2_x + 28, card2_y + 488, card2_x + 36, card2_y + 496), fill=GREEN)
draw.text((card2_x + 44, card2_y + 486), "Shader compile: OK  ·  Cook time: 0.32 ms  ·  60.0 FPS", font=f_small, fill=INK)

# Output pin of Field Pixel
fp_out_x = card2_x + card2_w - 16
fp_out_y = card2_y + 340
draw.ellipse((fp_out_x - 6, fp_out_y - 6, fp_out_x + 6, fp_out_y + 6), fill=DEEP, outline=SIGNAL, width=2)
draw.ellipse((fp_out_x - 2, fp_out_y - 2, fp_out_x + 2, fp_out_y + 2), fill=SIGNAL)


# 3. Right Card: Viewport Output
card3_x, card3_y, card3_w, card3_h = 930, 95, 220, 520
draw_card(card3_x, card3_y, card3_w, card3_h, "Output Viewport", "WINDOW // SINK", "1080p")

# In pin on Viewport
vp_in_x = card3_x
vp_in_y = card3_y + 340
draw.ellipse((vp_in_x - 6, vp_in_y - 6, vp_in_x + 6, vp_in_y + 6), fill=DEEP, outline=SIGNAL, width=2)
draw.ellipse((vp_in_x - 2, vp_in_y - 2, vp_in_x + 2, vp_in_y + 2), fill=SIGNAL)

# Viewport preview screen
vp_box = (card3_x + 16, card3_y + 60, card3_x + card3_w - 16, card3_y + 460)
draw.rounded_rectangle(vp_box, 8, fill=DEEP, outline=SURF3, width=1)

# Render generative shader visual inside Viewport preview
# Center of preview
cx = (vp_box[0] + vp_box[2]) / 2
cy = (vp_box[1] + vp_box[3]) / 2

# Draw glowing concentric rings with ember/signal gradient
num_rings = 7
for ri in range(num_rings, 0, -1):
    r_radius = ri * 24 + 18
    # draw glow
    alpha = int(40 + (ri / num_rings) * 80)
    col = (255, 107, 53, alpha) if ri in (3, 4) else (167, 177, 255, alpha)
    draw.ellipse((cx - r_radius, cy - r_radius, cx + r_radius, cy + r_radius), outline=col[:3], width=2 if ri not in (3, 4) else 4)

# Center core orb
draw.ellipse((cx - 18, cy - 18, cx + 18, cy + 18), fill=EMBER)
draw.ellipse((cx - 8, cy - 8, cx + 8, cy + 8), fill=INK)

# Wave interference lines
for a in range(0, 360, 20):
    rad = math.radians(a)
    dist1 = 40 + math.sin(a * 4) * 8
    dist2 = 95 + math.cos(a * 3) * 12
    x1 = cx + math.cos(rad) * dist1
    y1 = cy + math.sin(rad) * dist1
    x2 = cx + math.cos(rad) * dist2
    y2 = cy + math.sin(rad) * dist2
    draw.line((x1, y1, x2, y2), fill=SIGNAL, width=1)

draw.text((card3_x + 22, card3_y + 476), "1920x1080  ·  0.3 ms  ·  vsync", font=f_small, fill=INK3)


# 4. Connecting Cables (Cubic Bézier)
cable_layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
cdraw = ImageDraw.Draw(cable_layer)

def draw_cable(p1, p2, color, thickness=3, glow=True):
    x1, y1 = p1
    x2, y2 = p2
    dx = abs(x2 - x1) * 0.55
    # Bézier points
    pts = []
    for step in range(60):
        t = step / 59.0
        # cubic bezier
        cx1, cy1 = x1 + dx, y1
        cx2, cy2 = x2 - dx, y2
        bx = (1-t)**3 * x1 + 3*(1-t)**2 * t * cx1 + 3*(1-t) * t**2 * cx2 + t**3 * x2
        by = (1-t)**3 * y1 + 3*(1-t)**2 * t * cy1 + 3*(1-t) * t**2 * cy2 + t**3 * y2
        pts.append((bx, by))
    if glow:
        glow_col = (color[0], color[1], color[2], 50)
        cdraw.line(pts, fill=glow_col, width=thickness + 6)
    cdraw.line(pts, fill=color, width=thickness)

# Low -> Bass (Ember cable)
draw_cable(out_pin_coords["low"], in_pin_coords["bass"], EMBER, thickness=4, glow=True)

# Mid -> Mid (Signal cable)
draw_cable(out_pin_coords["mid"], in_pin_coords["mid"], SIGNAL, thickness=3, glow=True)

# High -> Treble (Signal cable)
draw_cable(out_pin_coords["high"], in_pin_coords["treble"], (136, 144, 255, 255), thickness=3, glow=True)

# Field Pixel out -> Viewport in (Signal cable)
draw_cable((fp_out_x, fp_out_y), (vp_in_x, vp_in_y), SIGNAL, thickness=3, glow=True)

# Composite cables onto main image
img = Image.alpha_composite(img, cable_layer)

# Save result
out_dir = os.path.join(ROOT, "website", "assets")
os.makedirs(out_dir, exist_ok=True)
out_path = os.path.join(out_dir, "blog_audio-reactive-glsl-shaders.png")
img.convert("RGB").save(out_path, "PNG", quality=95)
print(f"Generated hero graphic at {out_path} ({W}x{H})")
