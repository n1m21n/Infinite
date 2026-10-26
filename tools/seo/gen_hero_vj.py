#!/usr/bin/env python3
"""Generates the 1200x675 technical hero diagram for open-source-vj-software.
Adheres strictly to docs/brand/BRAND.md and brand.json (Midnight deep #0D1020 mode).
"""
import math, os
from PIL import Image, ImageDraw, ImageFont, ImageFilter

W, H = 1200, 675
img = Image.new("RGBA", (W, H), (13, 16, 32, 255)) # Midnight deep #0D1020
draw = ImageDraw.Draw(img)

# Color tokens from brand.json
DEEP = (13, 16, 32, 255)          # #0D1020
GROUND = (21, 25, 48, 255)        # #151930
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
GREEN = (46, 170, 74, 255)        # #2EAA4A (Go / Active)

# Fonts
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
font_mono_path = os.path.join(ROOT, "art/brand/fonts/GeistMono-Medium.ttf")
font_sans_path = os.path.join(ROOT, "art/brand/fonts/Geist-SemiBold.ttf")

if not os.path.exists(font_mono_path):
    font_mono_path = "/System/Library/Fonts/Menlo.ttc"
if not os.path.exists(font_sans_path):
    font_sans_path = "/System/Library/Fonts/Helvetica.ttc"

f_title = ImageFont.truetype(font_sans_path, 18)
f_sub = ImageFont.truetype(font_mono_path, 11)
f_card_title = ImageFont.truetype(font_sans_path, 13)
f_badge = ImageFont.truetype(font_mono_path, 10)
f_code = ImageFont.truetype(font_mono_path, 10)
f_label = ImageFont.truetype(font_mono_path, 10)
f_small = ImageFont.truetype(font_mono_path, 9)

# 1. Background subtle dot grid
for gx in range(20, W, 20):
    for gy in range(20, H, 20):
        draw.point((gx, gy), fill=DOT)

# Outer decorative frame
draw.rounded_rectangle([14, 14, W - 14, H - 14], radius=10, outline=LINE, width=1)

# Top Bar
draw.text((35, 30), "INFINITE", font=f_title, fill=INK)
draw.text((125, 34), "//  OPEN-SOURCE VJ SOFTWARE & REAL-TIME VISUAL PIPELINE", font=f_sub, fill=SIGNAL)

badge_text = "BOOTH AUDIO -> 8-BAND FFT -> FIELD PIXEL GLSL -> BLEND & FEEDBACK -> PROJECTION MAPPING"
bw = draw.textlength(badge_text, font=f_badge) + 18
draw.rounded_rectangle((W - 35 - bw, 27, W - 35, 51), 5, fill=SURF1, outline=LINE, width=1)
draw.text((W - 35 - bw + 9, 32), badge_text, font=f_badge, fill=INK2)

# Helper function for card
def draw_card(x, y, w, h, title, category, badge=None):
    draw.rounded_rectangle((x, y + 4, x + w, y + h + 4), 8, fill=(4, 5, 14, 160))
    draw.rounded_rectangle((x, y, x + w, y + h), 8, fill=SURF1, outline=LINE, width=1)
    draw.rounded_rectangle((x, y, x + w, y + 34), 8, fill=SURF2)
    draw.rectangle((x, y + 24, x + w, y + 34), fill=SURF2)
    draw.line((x, y + 34, x + w, y + 34), fill=LINE, width=1)
    draw.text((x + 12, y + 8), title, font=f_card_title, fill=INK)
    tw = draw.textlength(title, font=f_card_title)
    draw.text((x + 12 + tw + 8, y + 11), category, font=f_small, fill=INK3)
    if badge:
        bbw = draw.textlength(badge, font=f_small) + 10
        draw.rounded_rectangle((x + w - 12 - bbw, y + 7, x + w - 12, y + 25), 3, fill=SURF3)
        draw.text((x + w - 12 - bbw + 5, y + 10), badge, font=f_small, fill=SIGNAL)

# -------------------------------------------------------------
# Card 1: Audio Analyze (Left, x=35, w=260)
# -------------------------------------------------------------
c1_x, c1_y, c1_w, c1_h = 35, 75, 260, 535
draw_card(c1_x, c1_y, c1_w, c1_h, "Audio Analyze", "DSP // FFT SINK", "48 kHz")

# Waveform box
draw.rounded_rectangle((c1_x + 12, c1_y + 45, c1_x + c1_w - 12, c1_y + 105), 5, fill=DEEP, outline=SURF3, width=1)
draw.text((c1_x + 18, c1_y + 50), "LIVE BOOTH AUDIO IN", font=f_small, fill=INK3)
wave_pts = []
for i in range(c1_w - 36):
    wx = c1_x + 18 + i
    wy = c1_y + 78 + math.sin(i * 0.16) * 11 * math.sin(i * 0.025 + 0.5) + math.sin(i * 0.42) * 3
    wave_pts.append((wx, wy))
draw.line(wave_pts, fill=SIGNAL, width=1)

# FFT Spectrum box
draw.rounded_rectangle((c1_x + 12, c1_y + 115, c1_x + c1_w - 12, c1_y + 230), 5, fill=DEEP, outline=SURF3, width=1)
draw.text((c1_x + 18, c1_y + 120), "8-BAND SPECTRAL ANALYSIS", font=f_small, fill=INK3)

band_names = ["SUB", "BASS", "LMID", "MID", "HMID", "PRES", "BRIL", "AIR"]
band_heights = [70, 84, 52, 38, 44, 30, 22, 16]
bar_w = 20
bar_gap = 7
bx_start = c1_x + 18
for bi, (bname, bh) in enumerate(zip(band_names, band_heights)):
    bx = bx_start + bi * (bar_w + bar_gap)
    by_bot = c1_y + 212
    by_top = by_bot - bh
    draw.rectangle((bx, by_bot - 74, bx + bar_w, by_bot), fill=SURF1)
    bcolor = EMBER if bi in (0, 1) else (SIGNAL if bi in (2, 3) else INK2)
    draw.rectangle((bx, by_top, bx + bar_w, by_bot), fill=bcolor)
    draw.line((bx, by_top - 1, bx + bar_w, by_top - 1), fill=INK, width=1)
    draw.text((bx + 1, c1_y + 216), bname[:3], font=f_small, fill=INK3)

# Onset Trigger Pill
draw.rounded_rectangle((c1_x + 12, c1_y + 240, c1_x + c1_w - 12, c1_y + 266), 4, fill=DEEP, outline=SURF3, width=1)
draw.ellipse((c1_x + 20, c1_y + 249, c1_x + 28, c1_y + 257), fill=GREEN)
draw.text((c1_x + 34, c1_y + 248), "ONSET DETECTOR: LOCKED", font=f_small, fill=INK)

# Output Pins
draw.text((c1_x + 12, c1_y + 280), "MODULATION OUTPUT PINS", font=f_small, fill=INK2)
pins_out1 = [
    ("low", "Sub 250 Hz (Kick/Bass)", EMBER, c1_y + 302),
    ("mid", "250 Hz - 4 kHz (Snare/Lead)", SIGNAL, c1_y + 352),
    ("high", "> 4 kHz (Hi-hat/Sizzle)", SIGNAL, c1_y + 402),
    ("onset", "Transient beat spike", GREEN, c1_y + 452)
]
c1_out_coords = {}
for pname, pdesc, pcol, py in pins_out1:
    draw.rounded_rectangle((c1_x + 12, py, c1_x + c1_w - 12, py + 38), 5, fill=SURF2, outline=SURF3, width=1)
    draw.text((c1_x + 20, py + 5), pname, font=f_card_title, fill=pcol)
    draw.text((c1_x + 20, py + 21), pdesc, font=f_small, fill=INK3)
    pin_cx = c1_x + c1_w - 12
    pin_cy = py + 19
    draw.ellipse((pin_cx - 4, pin_cy - 4, pin_cx + 4, pin_cy + 4), fill=DEEP, outline=pcol, width=2)
    draw.ellipse((pin_cx - 2, pin_cy - 2, pin_cx + 2, pin_cy + 2), fill=pcol)
    c1_out_coords[pname] = (pin_cx, pin_cy)

# -------------------------------------------------------------
# Card 2: Field Pixel (GPU Shader, x=325, w=275)
# -------------------------------------------------------------
c2_x, c2_y, c2_w, c2_h = 325, 75, 275, 535
draw_card(c2_x, c2_y, c2_w, c2_h, "Field Pixel", "GLSL 150 SHADER", "GPU")

# Code preview box
draw.rounded_rectangle((c2_x + 12, c2_y + 45, c2_x + c2_w - 12, c2_y + 245), 5, fill=DEEP, outline=SURF3, width=1)
draw.text((c2_x + 18, c2_y + 50), "KERNEL DECLARATION", font=f_small, fill=INK3)

code_lines = [
    ("param float bass = 0.0;", EMBER),
    ("param float mid = 0.0;", SIGNAL),
    ("param float speed = 1.0;", INK2),
    ("", INK3),
    ("vec2 p = (uv - 0.5) * vec2(aspect, 1.0);", INK),
    ("float r = length(p);", INK),
    ("float ring = sin(r*28.0 - t*speed*4.0);", SIGNAL),
    ("float pulse = smoothstep(0.03, 0.0,", INK),
    ("  abs(ring) - 0.15 - bass * 0.4);", EMBER),
    ("col = mix(c_deep, c_accent, pulse);", INK),
    ("col += c_signal * mid * (1.0 - r);", SIGNAL),
]
for li, (line, lcol) in enumerate(code_lines):
    draw.text((c2_x + 18, c2_y + 68 + li * 15), line, font=f_code, fill=lcol)

# Visual Graphic Box
draw.rounded_rectangle((c2_x + 12, c2_y + 255, c2_x + c2_w - 12, c2_y + 440), 5, fill=DEEP, outline=SURF3, width=1)
draw.text((c2_x + 18, c2_y + 260), "GENERATIVE SHADER OUTPUT", font=f_small, fill=INK3)

# Draw generative radial concentric rings inside box
rcx, rcy = c2_x + c2_w // 2, c2_y + 350
for rad, rcol, rwidth in [
    (14, EMBER, 3),
    (28, SIGNAL, 2),
    (42, EMBER, 2),
    (58, SIGNAL, 1),
    (74, INK2, 1)
]:
    draw.ellipse((rcx - rad, rcy - rad, rcx + rad, rcy + rad), outline=rcol, width=rwidth)
# Glowing center dot
draw.ellipse((rcx - 5, rcy - 5, rcx + 5, rcy + 5), fill=EMBER)

# Input pins on left of Card 2
c2_in_coords = {}
pins_in2 = [
    ("bass", c2_y + 80, EMBER),
    ("mid", c2_y + 115, SIGNAL)
]
for pin_name, py, pcol in pins_in2:
    pin_cx = c2_x + 12
    draw.ellipse((pin_cx - 4, py - 4, pin_cx + 4, py + 4), fill=DEEP, outline=pcol, width=2)
    draw.ellipse((pin_cx - 2, py - 2, pin_cx + 2, py + 2), fill=pcol)
    c2_in_coords[pin_name] = (pin_cx, py)

# Output pin on right of Card 2
c2_out_cx = c2_x + c2_w - 12
c2_out_cy = c2_y + 480
draw.rounded_rectangle((c2_x + 12, c2_y + 460, c2_x + c2_w - 12, c2_y + 500), 5, fill=SURF2, outline=SURF3, width=1)
draw.text((c2_x + 20, c2_y + 468), "col (GPU Texture)", font=f_card_title, fill=EMBER)
draw.text((c2_x + 20, c2_y + 484), "GL_TEXTURE_2D output", font=f_small, fill=INK3)
draw.ellipse((c2_out_cx - 4, c2_out_cy - 4, c2_out_cx + 4, c2_out_cy + 4), fill=DEEP, outline=EMBER, width=2)
draw.ellipse((c2_out_cx - 2, c2_out_cy - 2, c2_out_cx + 2, c2_out_cy + 2), fill=EMBER)

# -------------------------------------------------------------
# Card 3: Blend & Feedback (Compositing, x=630, w=265)
# -------------------------------------------------------------
c3_x, c3_y, c3_w, c3_h = 630, 75, 265, 535
draw_card(c3_x, c3_y, c3_w, c3_h, "Blend & Feedback", "COMPOSITING & FX", "30 MODES")

# Multi-layer blend selector
draw.rounded_rectangle((c3_x + 12, c3_y + 45, c3_x + c3_w - 12, c3_y + 115), 5, fill=DEEP, outline=SURF3, width=1)
draw.text((c3_x + 18, c3_y + 50), "ACTIVE BLEND MODE", font=f_small, fill=INK3)
draw.rounded_rectangle((c3_x + 18, c3_y + 68, c3_x + c3_w - 18, c3_y + 102), 4, fill=SURF2, outline=LINE, width=1)
draw.text((c3_x + 26, c3_y + 77), "Linear Screen + Add", font=f_card_title, fill=INK)
draw.text((c3_x + c3_w - 75, c3_y + 80), "32-bit", font=f_small, fill=SIGNAL)

# Feedback loop module
draw.rounded_rectangle((c3_x + 12, c3_y + 125, c3_x + c3_w - 12, c3_y + 245), 5, fill=DEEP, outline=SURF3, width=1)
draw.text((c3_x + 18, c3_y + 130), "TEMPORAL FEEDBACK & TRAILS", font=f_small, fill=INK3)

# Mini feedback schematic
f_bx, f_by, f_bw, f_bh = c3_x + 24, c3_y + 155, 140, 70
draw.rounded_rectangle((f_bx, f_by, f_bx + f_bw, f_by + f_bh), 4, fill=SURF1, outline=SURF3, width=1)
draw.text((f_bx + 10, f_by + 8), "Feedback Node", font=f_small, fill=INK)
draw.text((f_bx + 10, f_by + 24), "1-Frame Buffer Delay", font=f_small, fill=SIGNAL)
draw.text((f_bx + 10, f_by + 40), "Decay: 0.88 // Phosphor", font=f_small, fill=INK3)

# Feedback loop arrow graphic
f_loop_pts = [
    (f_bx + f_bw, f_by + 35),
    (f_bx + f_bw + 35, f_by + 35),
    (f_bx + f_bw + 35, f_by - 10),
    (f_bx - 12, f_by - 10),
    (f_bx - 12, f_by + 35),
    (f_bx, f_by + 35)
]
draw.line(f_loop_pts, fill=EMBER, width=1)

# Crossfader Box
draw.rounded_rectangle((c3_x + 12, c3_y + 255, c3_x + c3_w - 12, c3_y + 375), 5, fill=DEEP, outline=SURF3, width=1)
draw.text((c3_x + 18, c3_y + 260), "HARDWARE MIDI CROSSFADER", font=f_small, fill=INK3)

# Crossfader visual slider
sl_x, sl_y, sl_w = c3_x + 24, c3_y + 300, c3_w - 48
draw.line((sl_x, sl_y, sl_x + sl_w, sl_y), fill=SURF3, width=4)
# Knob position (at 65%)
kpos = sl_x + int(sl_w * 0.65)
draw.rounded_rectangle((kpos - 10, sl_y - 14, kpos + 10, sl_y + 14), 4, fill=EMBER, outline=INK, width=1)
draw.text((sl_x, sl_y + 20), "LAYER A (LIVE CAM)", font=f_small, fill=INK3)
draw.text((sl_x + sl_w - 95, sl_y + 20), "LAYER B (FIELD)", font=f_small, fill=SIGNAL)
draw.text((c3_x + 18, c3_y + 348), "MIDI CC #1: CH1 FADER -> MIX", font=f_small, fill=GREEN)

# Input pins on left of Card 3
c3_in_coords = {}
c3_in_coords["inA"] = (c3_x + 12, c3_y + 80)
c3_in_coords["inB"] = (c3_x + 12, c3_y + 480)

for p_name, (px, py) in c3_in_coords.items():
    draw.ellipse((px - 4, py - 4, px + 4, py + 4), fill=DEEP, outline=EMBER, width=2)
    draw.ellipse((px - 2, py - 2, px + 2, py + 2), fill=EMBER)

# Output pin on right of Card 3
c3_out_cx = c3_x + c3_w - 12
c3_out_cy = c3_y + 440
draw.rounded_rectangle((c3_x + 12, c3_y + 415, c3_x + c3_w - 12, c3_y + 465), 5, fill=SURF2, outline=SURF3, width=1)
draw.text((c3_x + 20, c3_y + 425), "out (Composited)", font=f_card_title, fill=SIGNAL)
draw.text((c3_x + 20, c3_y + 445), "Master visual bus", font=f_small, fill=INK3)
draw.ellipse((c3_out_cx - 4, c3_out_cy - 4, c3_out_cx + 4, c3_out_cy + 4), fill=DEEP, outline=SIGNAL, width=2)
draw.ellipse((c3_out_cx - 2, c3_out_cy - 2, c3_out_cx + 2, c3_out_cy + 2), fill=SIGNAL)

# -------------------------------------------------------------
# Card 4: Projection & Syphon (Output, x=925, w=240)
# -------------------------------------------------------------
c4_x, c4_y, c4_w, c4_h = 925, 75, 240, 535
draw_card(c4_x, c4_y, c4_w, c4_h, "Projection & Syphon", "STAGE ROUTING", "1080p60")

# Input pin on left of Card 4
c4_in_cx = c4_x + 12
c4_in_cy = c4_y + 440
draw.ellipse((c4_in_cx - 4, c4_in_cy - 4, c4_in_cx + 4, c4_in_cy + 4), fill=DEEP, outline=SIGNAL, width=2)
draw.ellipse((c4_in_cx - 2, c4_in_cy - 2, c4_in_cx + 2, c4_in_cy + 2), fill=SIGNAL)

# Keystone / Corner Pin Warp Display
draw.rounded_rectangle((c4_x + 12, c4_y + 45, c4_x + c4_w - 12, c4_y + 185), 5, fill=DEEP, outline=SURF3, width=1)
draw.text((c4_x + 18, c4_y + 50), "4-CORNER PERSPECTIVE WARP", font=f_small, fill=INK3)

# Draw warped quadrilateral representing projection keystone
w_poly = [
    (c4_x + 40, c4_y + 80),
    (c4_x + c4_w - 30, c4_y + 70),
    (c4_x + c4_w - 45, c4_y + 165),
    (c4_x + 25, c4_y + 155)
]
draw.polygon(w_poly, fill=SURF1, outline=SIGNAL)
# Draw corner pin handles
for ptx, pty in w_poly:
    draw.ellipse((ptx - 3, pty - 3, ptx + 3, pty + 3), fill=EMBER, outline=INK)
draw.text((c4_x + 45, c4_y + 115), "HOMOGRAPHY OK", font=f_small, fill=INK2)

# Mesh Grid Warping Box
draw.rounded_rectangle((c4_x + 12, c4_y + 195, c4_x + c4_w - 12, c4_y + 325), 5, fill=DEEP, outline=SURF3, width=1)
draw.text((c4_x + 18, c4_y + 200), "MESH GRID CALIBRATION", font=f_small, fill=INK3)

# Draw calibration grid
gx_start, gy_start = c4_x + 35, c4_y + 225
for r in range(4):
    for c in range(5):
        cx = gx_start + c * 35
        cy = gy_start + r * 22
        draw.point((cx, cy), fill=SIGNAL)
        if c < 4:
            draw.line((cx, cy, cx + 35, cy), fill=SURF3, width=1)
        if r < 3:
            draw.line((cx, cy, cx, cy + 22), fill=SURF3, width=1)
draw.text((c4_x + 18, c4_y + 305), "TEST PATTERN: ALIGNMENT CROSS", font=f_small, fill=GREEN)

# Output Channels Box
draw.rounded_rectangle((c4_x + 12, c4_y + 335, c4_x + c4_w - 12, c4_y + 515), 5, fill=DEEP, outline=SURF3, width=1)
draw.text((c4_x + 18, c4_y + 342), "DESTINATION TARGETS", font=f_small, fill=INK3)

targets = [
    ("Syphon Out", "macOS Zero-Copy GPU bus", EMBER, c4_y + 365),
    ("Spout Out", "Windows DirectX/GL Interop", SIGNAL, c4_y + 415),
    ("Projector A", "Display 2 (HDMI 1080p60)", INK, c4_y + 465)
]
for tname, tdesc, tcol, ty in targets:
    draw.rounded_rectangle((c4_x + 18, ty, c4_x + c4_w - 18, ty + 40), 4, fill=SURF2, outline=SURF3, width=1)
    draw.text((c4_x + 26, ty + 6), tname, font=f_card_title, fill=tcol)
    draw.text((c4_x + 26, ty + 22), tdesc, font=f_small, fill=INK3)

# -------------------------------------------------------------
# Signal Routing Cables between Cards
# -------------------------------------------------------------
def draw_spline_cable(p1, p2, col, width=2):
    x1, y1 = p1
    x2, y2 = p2
    dx = abs(x2 - x1) * 0.55
    pts = []
    steps = 40
    for s in range(steps + 1):
        t = s / steps
        # Cubic Bezier
        bx = (1 - t)**3 * x1 + 3 * (1 - t)**2 * t * (x1 + dx) + 3 * (1 - t) * t**2 * (x2 - dx) + t**3 * x2
        by = (1 - t)**3 * y1 + 3 * (1 - t)**2 * t * y1 + 3 * (1 - t) * t**2 * y2 + t**3 * y2
        pts.append((bx, by))
    draw.line(pts, fill=col, width=width)

# Cable 1: Audio Analyze (low) -> Field Pixel (bass)
draw_spline_cable(c1_out_coords["low"], c2_in_coords["bass"], EMBER, 2)

# Cable 2: Audio Analyze (mid) -> Field Pixel (mid)
draw_spline_cable(c1_out_coords["mid"], c2_in_coords["mid"], SIGNAL, 2)

# Cable 3: Field Pixel (col) -> Blend (inB)
draw_spline_cable((c2_out_cx, c2_out_cy), c3_in_coords["inB"], EMBER, 2)

# Cable 4: Blend (out) -> Projection (in)
draw_spline_cable((c3_out_cx, c3_out_cy), (c4_in_cx, c4_in_cy), SIGNAL, 2)

# -------------------------------------------------------------
# Bottom Status Line
# -------------------------------------------------------------
draw.line((35, H - 42, W - 35, H - 42), fill=SURF3, width=1)
draw.text((35, H - 32), "MODULATION: LOCK-FREE AUDIO SINK // RENDER: SINGLE-PASS OPENGL // ZERO FRAME DROP", font=f_small, fill=INK3)
draw.text((W - 355, H - 32), "MIT LICENSED // MACOS · WINDOWS · LINUX // INFINITEDEVICES.ORG", font=f_small, fill=SIGNAL)

# Save image
out_dir = os.path.join(ROOT, "website", "assets")
os.makedirs(out_dir, exist_ok=True)
out_path = os.path.join(out_dir, "blog_open-source-vj-software.png")
img.save(out_path, optimize=True)
print(f"ok  Saved 1200x675 hero diagram to: {out_path}")
