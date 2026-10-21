#!/usr/bin/env python3
"""Generates the 1200x675 technical hero diagram for paulstretch-extreme-spectral-time-stretching.
Adheres strictly to docs/brand/BRAND.md and brand.json (Midnight mode).
"""
import math, os
from PIL import Image, ImageDraw, ImageFont

W, H = 1200, 675
img = Image.new("RGBA", (W, H), (21, 25, 48, 255)) # Midnight ground #151930
draw = ImageDraw.Draw(img)

# Color tokens from brand.json
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

try:
    f_title = ImageFont.truetype(font_sans_path, 20)
    f_sub = ImageFont.truetype(font_mono_path, 12)
    f_card_title = ImageFont.truetype(font_sans_path, 15)
    f_badge = ImageFont.truetype(font_mono_path, 11)
    f_code = ImageFont.truetype(font_mono_path, 12)
    f_label = ImageFont.truetype(font_mono_path, 11)
    f_small = ImageFont.truetype(font_mono_path, 10)
except Exception:
    f_title = f_sub = f_card_title = f_badge = f_code = f_label = f_small = ImageFont.load_default()

# 1. Subtle dot grid
for gx in range(24, W, 24):
    for gy in range(24, H, 24):
        draw.point((gx, gy), fill=DOT)

# 2. Top Bar
draw.text((50, 36), "INFINITE", font=f_title, fill=INK)
draw.text((150, 42), "//  REAL-TIME SPECTRAL TIME-STRETCHING ENGINE", font=f_sub, fill=SIGNAL)

badge_text = "STFT -> PHASE RANDOMISATION -> vDSP IFFT -> OVERLAP-ADD"
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

# -------------------------------------------------------------
# Card 1: Analysis & Windowing (Left)
# -------------------------------------------------------------
card1_x, card1_y, card1_w, card1_h = 50, 95, 300, 520
draw_card(card1_x, card1_y, card1_w, card1_h, "STFT Analysis", "DSP // WINDOWING", "44.1 kHz")

# Source sample slice box
draw.rounded_rectangle((card1_x + 16, card1_y + 58, card1_x + card1_w - 16, card1_y + 148), 8, fill=DEEP, outline=SURF3, width=1)
draw.text((card1_x + 26, card1_y + 64), "INPUT GRAIN x(n)", font=f_small, fill=INK3)

# Waveform points
wave_w = card1_w - 52
wave_pts = []
for i in range(wave_w):
    wx = card1_x + 26 + i
    wy = card1_y + 106 + math.sin(i * 0.18) * 16 * math.sin(i * 0.04) + math.sin(i * 0.45) * 6
    wave_pts.append((wx, wy))
draw.line(wave_pts, fill=SIGNAL, width=1)

# Hann window curve overlay
hann_pts = []
for i in range(wave_w):
    hx = card1_x + 26 + i
    hw = 0.5 * (1.0 - math.cos(2.0 * math.pi * i / (wave_w - 1)))
    hy = card1_y + 140 - hw * 32
    hann_pts.append((hx, hy))
draw.line(hann_pts, fill=EMBER, width=2)
draw.text((card1_x + 26, card1_y + 132), "Hann Window w(n)", font=f_small, fill=EMBER)

# Forward FFT Box
draw.rounded_rectangle((card1_x + 16, card1_y + 160, card1_x + card1_w - 16, card1_y + 280), 8, fill=DEEP, outline=SURF3, width=1)
draw.text((card1_x + 26, card1_y + 166), "FORWARD RADIX-2 FFT", font=f_small, fill=INK3)
draw.text((card1_x + 26, card1_y + 182), "X[k] = FFT(x(n) · w(n))", font=f_code, fill=INK)

# Spectral magnitude bars
fft_bars = [18, 42, 68, 55, 38, 48, 62, 75, 40, 28, 22, 14, 9, 6]
bar_w = 14
bar_gap = 5
bx_start = card1_x + 26
for bi, bh in enumerate(fft_bars):
    bx = bx_start + bi * (bar_w + bar_gap)
    by_bot = card1_y + 268
    by_top = by_bot - bh
    draw.rectangle((bx, by_bot - 80, bx + bar_w, by_bot), fill=SURF1)
    draw.rectangle((bx, by_top, bx + bar_w, by_bot), fill=SIGNAL)
    draw.line((bx, by_top - 2, bx + bar_w, by_top - 2), fill=INK, width=1)

# Formula explanation
draw.rounded_rectangle((card1_x + 16, card1_y + 292, card1_x + card1_w - 16, card1_y + 368), 6, fill=SURF2, outline=SURF3, width=1)
draw.text((card1_x + 26, card1_y + 300), "MAGNITUDE EXTRACTION", font=f_small, fill=SIGNAL)
draw.text((card1_x + 26, card1_y + 318), "M[k] = sqrt(Re(X)^2 + Im(X)^2)", font=f_code, fill=INK)
draw.text((card1_x + 26, card1_y + 338), "Phase arg(X) discarded", font=f_small, fill=INK3)

# Output Pins on Card 1
out1_py = card1_y + 420
draw.rounded_rectangle((card1_x + 16, out1_py, card1_x + card1_w - 16, out1_py + 44), 6, fill=SURF2, outline=SURF3, width=1)
draw.text((card1_x + 28, out1_py + 8), "spec_mag", font=f_card_title, fill=SIGNAL)
draw.text((card1_x + 28, out1_py + 24), "Spectral envelope stream", font=f_small, fill=INK3)

pin1_cx = card1_x + card1_w - 16
pin1_cy = out1_py + 22
draw.ellipse((pin1_cx - 6, pin1_cy - 6, pin1_cx + 6, pin1_cy + 6), fill=DEEP, outline=SIGNAL, width=2)
draw.ellipse((pin1_cx - 2, pin1_cy - 2, pin1_cx + 2, pin1_cy + 2), fill=SIGNAL)

# -------------------------------------------------------------
# Card 2: Paul Stretch Node Engine (Middle)
# -------------------------------------------------------------
card2_x, card2_y, card2_w, card2_h = 390, 95, 470, 520
draw_card(card2_x, card2_y, card2_w, card2_h, "Paul Stretch", "SYNTHS // SPECTRAL", "vDSP 64-bit")

# Input Pins on Card 2 (Left edge)
in_pins2 = [
    ("in_audio", SIGNAL, card2_y + 80, "Record in / sample"),
    ("pos", EMBER, card2_y + 130, "Playhead position [0..1]"),
    ("stretch", SIGNAL, card2_y + 180, "Stretch ratio [1..1000x]"),
]
in2_coords = {}
for pname, pcol, py, pdesc in in_pins2:
    pin_cx = card2_x
    pin_cy = py
    draw.ellipse((pin_cx - 6, pin_cy - 6, pin_cx + 6, pin_cy + 6), fill=DEEP, outline=pcol, width=2)
    draw.ellipse((pin_cx - 2, pin_cy - 2, pin_cx + 2, pin_cy + 2), fill=pcol)
    draw.text((card2_x + 14, py - 6), pname, font=f_label, fill=pcol)
    in2_coords[pname] = (pin_cx, pin_cy)

# Node Parameters Panel
param_y = card2_y + 68
param_rows = [
    ("Stretch Factor", "32.0x", 0.35, EMBER),
    ("Window Size", "32768 (743ms)", 0.70, SIGNAL),
    ("Phase Randomisation", "1.00 (100%)", 1.0, SIGNAL),
    ("Pitch / Fine Shift", "+0.0 st  ·  +0 c", 0.5, INK2),
    ("Unison Detune", "4 voices  ·  12 c", 0.6, INK),
]

for ptitle, pval, pfill, pcol in param_rows:
    draw.text((card2_x + 120, param_y), ptitle, font=f_small, fill=INK2)
    draw.text((card2_x + card2_w - 24 - draw.textlength(pval, font=f_badge), param_y), pval, font=f_badge, fill=pcol)
    # Slider track
    draw.rounded_rectangle((card2_x + 120, param_y + 15, card2_x + card2_w - 24, param_y + 20), 2, fill=DEEP)
    draw.rounded_rectangle((card2_x + 120, param_y + 15, card2_x + 120 + int((card2_w - 144) * pfill), param_y + 20), 2, fill=pcol)
    param_y += 28

# Phase Randomisation Visualisation Box
rand_box_y = card2_y + 218
draw.rounded_rectangle((card2_x + 20, rand_box_y, card2_x + card2_w - 20, rand_box_y + 248), 8, fill=DEEP, outline=SURF3, width=1)
draw.text((card2_x + 32, rand_box_y + 10), "PHASE RANDOMISATION & RECONSTRUCTION", font=f_small, fill=SIGNAL)

# Left sub-box: Correlated Phase (Metallic Flutter)
draw.text((card2_x + 32, rand_box_y + 30), "Correlated Phase (Flutter)", font=f_small, fill=INK3)
center1_x = card2_x + 105
center1_y = rand_box_y + 95
draw.ellipse((center1_x - 45, center1_y - 45, center1_x + 45, center1_y + 45), outline=SURF2, width=1)
for k in range(12):
    ang = 0.4 + 0.08 * k
    lx = center1_x + math.cos(ang) * 40
    ly = center1_y + math.sin(ang) * 40
    draw.line((center1_x, center1_y, lx, ly), fill=INK3, width=1)
    draw.ellipse((lx - 2, ly - 2, lx + 2, ly + 2), fill=INK3)

# Arrow pointing right
draw.line((card2_x + 175, rand_box_y + 95, card2_x + 225, rand_box_y + 95), fill=EMBER, width=2)
draw.polygon([(card2_x + 230, rand_box_y + 95), (card2_x + 222, rand_box_y + 90), (card2_x + 222, rand_box_y + 100)], fill=EMBER)
draw.text((card2_x + 180, rand_box_y + 75), "XorShift32", font=f_small, fill=EMBER)

# Right sub-box: Isotropic Random Phase (Ambient Cloud)
draw.text((card2_x + 250, rand_box_y + 30), "Random Phase theta in [-pi, pi]", font=f_small, fill=EMBER)
center2_x = card2_x + 355
center2_y = rand_box_y + 95
draw.ellipse((center2_x - 45, center2_y - 45, center2_x + 45, center2_y + 45), outline=SURF2, width=1)
phase_angles = [0.12, 1.45, 2.78, 4.02, 5.21, 0.89, 2.14, 3.42, 4.67, 5.92, 1.15, 3.82]
for idx, ang in enumerate(phase_angles):
    lx = center2_x + math.cos(ang) * 40
    ly = center2_y + math.sin(ang) * 40
    col = EMBER if idx % 2 == 0 else SIGNAL
    draw.line((center2_x, center2_y, lx, ly), fill=col, width=1)
    draw.ellipse((lx - 2, ly - 2, lx + 2, ly + 2), fill=col)

# Complex frequency synthesis formula
draw.text((card2_x + 32, rand_box_y + 155), "Y[k] = M[k] · (cos(theta[k]) + i·sin(theta[k]))", font=f_code, fill=INK)
draw.text((card2_x + 32, rand_box_y + 176), "y(n) = IFFT(Y[k])  ·  Overlap hop = (N / 2) / stretch", font=f_code, fill=SIGNAL)
draw.text((card2_x + 32, rand_box_y + 196), "Unison: 4 detuned complex spectra summed before IFFT", font=f_small, fill=INK2)

# Footer Status Bar
draw.rounded_rectangle((card2_x + 20, card2_y + 478, card2_x + card2_w - 20, card2_y + 506), 6, fill=SURF2)
draw.ellipse((card2_x + 32, card2_y + 488, card2_x + 40, card2_y + 496), fill=GREEN)
draw.text((card2_x + 48, card2_y + 486), "DSP Engine: Active  ·  Zero-alloc thread  ·  vDSP FFT OK  ·  PDC: 0", font=f_small, fill=INK)

# Output Pins on Card 2 (Right edge)
out_pins2 = [
    ("out_L", SIGNAL, card2_y + 320),
    ("out_R", SIGNAL, card2_y + 380),
]
out2_coords = {}
for pname, pcol, py in out_pins2:
    pin_cx = card2_x + card2_w
    pin_cy = py
    draw.ellipse((pin_cx - 6, pin_cy - 6, pin_cx + 6, pin_cy + 6), fill=DEEP, outline=pcol, width=2)
    draw.ellipse((pin_cx - 2, pin_cy - 2, pin_cx + 2, pin_cy + 2), fill=pcol)
    draw.text((card2_x + card2_w - draw.textlength(pname, font=f_label) - 14, py - 6), pname, font=f_label, fill=pcol)
    out2_coords[pname] = (pin_cx, pin_cy)

# -------------------------------------------------------------
# Card 3: Overlap-Add & Output (Right)
# -------------------------------------------------------------
card3_x, card3_y, card3_w, card3_h = 890, 95, 260, 520
draw_card(card3_x, card3_y, card3_w, card3_h, "Audio Out", "OUTPUT // OVERLAP-ADD", "Stereo")

# In pins on Card 3
in_pins3 = [
    ("in_L", SIGNAL, card3_y + 320),
    ("in_R", SIGNAL, card3_y + 380),
]
in3_coords = {}
for pname, pcol, py in in_pins3:
    pin_cx = card3_x
    pin_cy = py
    draw.ellipse((pin_cx - 6, pin_cy - 6, pin_cx + 6, pin_cy + 6), fill=DEEP, outline=pcol, width=2)
    draw.ellipse((pin_cx - 2, pin_cy - 2, pin_cx + 2, pin_cy + 2), fill=pcol)
    in3_coords[pname] = (pin_cx, pin_cy)

# Overlap-add ring buffer diagram
draw.rounded_rectangle((card3_x + 16, card3_y + 60, card3_x + card3_w - 16, card3_y + 220), 8, fill=DEEP, outline=SURF3, width=1)
draw.text((card3_x + 24, card3_y + 68), "OVERLAP-ADD ACCUMULATOR", font=f_small, fill=INK3)

# 3 overlapping grain lobes
lobe_w = 120
for li, (lx_off, ly_off, lcol) in enumerate([(30, 110, SURF3), (70, 110, INK2), (110, 110, EMBER)]):
    lobe_pts = []
    for step in range(lobe_w):
        lx = card3_x + lx_off + step
        if lx > card3_x + card3_w - 24: break
        lw = 0.5 * (1.0 - math.cos(2.0 * math.pi * step / (lobe_w - 1)))
        ly = ly_off - lw * 28
        lobe_pts.append((lx, ly))
    draw.line(lobe_pts, fill=lcol, width=1 if li < 2 else 2)

# Summed continuous result curve
sum_pts = []
for step in range(card3_w - 48):
    sx = card3_x + 24 + step
    # flat smooth top with gentle undulating harmonic ripple
    sy = card3_y + 175 + math.sin(step * 0.12) * 5 + math.cos(step * 0.04) * 3
    sum_pts.append((sx, sy))
draw.line(sum_pts, fill=SIGNAL, width=2)
draw.text((card3_x + 24, card3_y + 196), "Continuous Ambient Output", font=f_small, fill=SIGNAL)

# Output Stereo Meters
draw.rounded_rectangle((card3_x + 16, card3_y + 236, card3_x + card3_w - 16, card3_y + 300), 8, fill=DEEP, outline=SURF3, width=1)
draw.text((card3_x + 24, card3_y + 244), "STEREO OUTPUT LEVEL", font=f_small, fill=INK3)

# L meter
draw.text((card3_x + 24, card3_y + 262), "L", font=f_badge, fill=INK2)
draw.rounded_rectangle((card3_x + 40, card3_y + 264, card3_x + card3_w - 24, card3_y + 270), 2, fill=SURF1)
draw.rounded_rectangle((card3_x + 40, card3_y + 264, card3_x + 40 + int((card3_w - 64) * 0.74), card3_y + 270), 2, fill=SIGNAL)

# R meter
draw.text((card3_x + 24, card3_y + 278), "R", font=f_badge, fill=INK2)
draw.rounded_rectangle((card3_x + 40, card3_y + 280, card3_x + card3_w - 24, card3_y + 286), 2, fill=SURF1)
draw.rounded_rectangle((card3_x + 40, card3_y + 280, card3_x + 40 + int((card3_w - 64) * 0.78), card3_y + 286), 2, fill=EMBER)

# Master level badge
draw.rounded_rectangle((card3_x + 16, card3_y + 420, card3_x + card3_w - 16, card3_y + 465), 6, fill=SURF2, outline=SURF3, width=1)
draw.text((card3_x + 24, card3_y + 428), "PEAK LEVEL: -3.2 dB", font=f_badge, fill=INK)
draw.text((card3_x + 24, card3_y + 444), "LUFS: -16.4  ·  True 32-bit", font=f_small, fill=GREEN)

draw.text((card3_x + 24, card3_y + 486), "To DAC / Master Out", font=f_small, fill=INK3)

# -------------------------------------------------------------
# 4. Connecting Cables (Cubic Bézier)
# -------------------------------------------------------------
cable_layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
cdraw = ImageDraw.Draw(cable_layer)

def draw_cable(p1, p2, color, thickness=3, glow=True):
    x1, y1 = p1
    x2, y2 = p2
    dx = abs(x2 - x1) * 0.55
    pts = []
    for step in range(60):
        t = step / 59.0
        cx1, cy1 = x1 + dx, y1
        cx2, cy2 = x2 - dx, y2
        bx = (1-t)**3 * x1 + 3*(1-t)**2 * t * cx1 + 3*(1-t) * t**2 * cx2 + t**3 * x2
        by = (1-t)**3 * y1 + 3*(1-t)**2 * t * cy1 + 3*(1-t) * t**2 * cy2 + t**3 * y2
        pts.append((bx, by))
    if glow:
        glow_col = (color[0], color[1], color[2], 50)
        cdraw.line(pts, fill=glow_col, width=thickness + 6)
    cdraw.line(pts, fill=color, width=thickness)

# Card 1 -> Card 2 cables
draw_cable((pin1_cx, pin1_cy), in2_coords["in_audio"], SIGNAL, thickness=3, glow=True)

# Card 2 -> Card 3 cables (Stereo L and R)
draw_cable(out2_coords["out_L"], in3_coords["in_L"], SIGNAL, thickness=3, glow=True)
draw_cable(out2_coords["out_R"], in3_coords["in_R"], EMBER, thickness=3, glow=True)

# Composite cables
img = Image.alpha_composite(img, cable_layer)

# Save result
out_dir = os.path.join(ROOT, "website", "assets")
os.makedirs(out_dir, exist_ok=True)
out_path = os.path.join(out_dir, "blog_paulstretch-extreme-spectral-time-stretching.png")
img.convert("RGB").save(out_path, "PNG", quality=95)
print(f"Generated hero graphic at {out_path} ({W}x{H})")
