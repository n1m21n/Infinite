#!/usr/bin/env python3
"""Generates the 1200x675 technical hero diagram for generative-visuals-live-performance.
Adheres strictly to docs/brand/BRAND.md and brand.json (Midnight deep #0D1020 mode).
"""
import math, os
from PIL import Image, ImageDraw, ImageFont

W, H = 1200, 675
img = Image.new("RGBA", (W, H), (13, 16, 32, 255))  # Midnight deep #0D1020
draw = ImageDraw.Draw(img)

# Brand colour tokens
DEEP = (13, 16, 32, 255)       # #0D1020
GROUND = (21, 25, 48, 255)     # #151930
SURF1 = (32, 38, 63, 255)      # #20263F
SURF2 = (42, 48, 82, 255)      # #2A3052
SURF3 = (52, 59, 94, 255)      # #343B5E
LINE = (52, 59, 94, 255)       # #343B5E
DOT = (35, 42, 74, 255)        # #232A4A
INK = (238, 241, 250, 255)     # #EEF1FA
INK2 = (163, 170, 203, 255)    # #A3AACB
INK3 = (136, 144, 177, 255)    # #8890B1
SIGNAL = (167, 177, 255, 255)  # #A7B1FF
EMBER = (255, 107, 53, 255)    # #FF6B35
GREEN = (46, 170, 74, 255)     # #2EAA4A

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

# 1. Subtle dot grid background
for gx in range(20, W, 20):
    for gy in range(20, H, 20):
        draw.point((gx, gy), fill=DOT)

# Outer decorative border
draw.rounded_rectangle([14, 14, W - 14, H - 14], radius=10, outline=LINE, width=1)

# Header banner
draw.text((35, 30), "INFINITE", font=f_title, fill=INK)
draw.text((125, 34), "//  GENERATIVE VISUALS FOR LIVE PERFORMANCE", font=f_sub, fill=SIGNAL)

badge_text = "DSP ENVELOPES -> GPU FRAGMENT SHADERS -> PROCEDURAL 3D -> STAGE PROJECTION"
bw = draw.textlength(badge_text, font=f_badge) + 18
draw.rounded_rectangle((W - 35 - bw, 27, W - 35, 51), 5, fill=SURF1, outline=LINE, width=1)
draw.text((W - 35 - bw + 9, 32), badge_text, font=f_badge, fill=INK2)

# Card helper
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
# Card 1: Audio Analysis & Envelope Tracking (x=35, w=260)
# -------------------------------------------------------------
c1_x, c1_y, c1_w, c1_h = 35, 75, 260, 535
draw_card(c1_x, c1_y, c1_w, c1_h, "Audio Analyze", "DSP // FFT SINK", "48 kHz")

# Waveform box
draw.rounded_rectangle((c1_x + 12, c1_y + 45, c1_x + c1_w - 12, c1_y + 110), 5, fill=DEEP, outline=SURF3, width=1)
draw.text((c1_x + 18, c1_y + 50), "STAGE AUDIO INPUT", font=f_small, fill=INK3)
wave_pts = []
for i in range(c1_w - 36):
    wx = c1_x + 18 + i
    wy = c1_y + 82 + math.sin(i * 0.14) * 12 * math.cos(i * 0.03 + 0.3) + math.sin(i * 0.38) * 4
    wave_pts.append((wx, wy))
draw.line(wave_pts, fill=SIGNAL, width=1)

# Spectral Filterbank
draw.rounded_rectangle((c1_x + 12, c1_y + 120, c1_x + c1_w - 12, c1_y + 245), 5, fill=DEEP, outline=SURF3, width=1)
draw.text((c1_x + 18, c1_y + 126), "8-BAND SPECTRAL ANALYSIS", font=f_small, fill=INK3)

band_names = ["SUB", "BASS", "LMID", "MID", "HMID", "PRES", "BRIL", "AIR"]
band_heights = [74, 88, 54, 40, 48, 32, 24, 18]
bar_w = 20
bar_gap = 7
bx_start = c1_x + 18
for bi, (bname, bh) in enumerate(zip(band_names, band_heights)):
    bx = bx_start + bi * (bar_w + bar_gap)
    by_bot = c1_y + 225
    draw.rectangle((bx, by_bot - 78, bx + bar_w, by_bot), fill=SURF1)
    bcolor = EMBER if bi in (0, 1) else (SIGNAL if bi in (2, 3, 4) else INK2)
    draw.rectangle((bx, by_bot - bh, bx + bar_w, by_bot), fill=bcolor)
    draw.text((bx + 1, by_bot + 4), bname, font=f_small, fill=INK3)

# Output Pins
draw.rounded_rectangle((c1_x + 12, c1_y + 258, c1_x + c1_w - 12, c1_y + 515), 5, fill=DEEP, outline=SURF3, width=1)
draw.text((c1_x + 18, c1_y + 266), "MODULATION OUTPUT PINS", font=f_small, fill=INK3)

c1_pins = [
    ("low", "0..1 (Sub-bass transient)", EMBER, c1_y + 295),
    ("mid", "0..1 (Vocal / Snare punch)", SIGNAL, c1_y + 340),
    ("high", "0..1 (Hi-hat / Cymbals)", INK, c1_y + 385),
    ("onset", "Trigger impulse (Clock)", GREEN, c1_y + 430),
    ("level", "RMS envelope amplitude", INK2, c1_y + 475)
]

c1_out_coords = {}
for pname, pdesc, pcol, py in c1_pins:
    draw.rounded_rectangle((c1_x + 18, py, c1_x + c1_w - 28, py + 34), 4, fill=SURF1, outline=SURF3, width=1)
    draw.text((c1_x + 26, py + 5), pname, font=f_card_title, fill=pcol)
    draw.text((c1_x + 26, py + 19), pdesc, font=f_small, fill=INK3)
    # Output jack on right edge
    jx = c1_x + c1_w - 12
    jy = py + 17
    draw.ellipse((jx - 4, jy - 4, jx + 4, jy + 4), fill=DEEP, outline=pcol, width=2)
    draw.ellipse((jx - 2, jy - 2, jx + 2, jy + 2), fill=pcol)
    c1_out_coords[pname] = (jx, jy)

# -------------------------------------------------------------
# Card 2: Field Pixel // GPU Shader Synthesis (x=330, w=285)
# -------------------------------------------------------------
c2_x, c2_y, c2_w, c2_h = 330, 75, 285, 535
draw_card(c2_x, c2_y, c2_w, c2_h, "Field Pixel", "GPU SHADER", "GLSL 60 FPS")

# Ingest pins on left edge
c2_in_pins = [
    ("bass_drive", c2_y + 295),
    ("warp_rate", c2_y + 340)
]
c2_in_coords = {}
for pname, py in c2_in_pins:
    jx = c2_x + 12
    jy = py + 17
    draw.ellipse((jx - 4, jy - 4, jx + 4, jy + 4), fill=DEEP, outline=SIGNAL, width=2)
    draw.ellipse((jx - 2, jy - 2, jx + 2, jy + 2), fill=SIGNAL)
    c2_in_coords[pname] = (jx, jy)

# Live Generative Canvas Preview (Raymarched Gyroid / Domain Warping)
canvas_box = (c2_x + 12, c2_y + 45, c2_x + c2_w - 12, c2_y + 245)
draw.rounded_rectangle(canvas_box, 5, fill=DEEP, outline=SURF3, width=1)
draw.text((c2_x + 18, c2_y + 50), "DOMAIN-WARP GPU BUFFER", font=f_small, fill=INK3)

# Draw simulated generative shader pattern
cx = c2_x + c2_w // 2
cy = c2_y + 148
for ring in range(7, 0, -1):
    rad = ring * 13
    r_pts = []
    for deg in range(0, 360, 6):
        rad_angle = math.radians(deg)
        distort = math.sin(deg * 0.08) * 8 * (ring / 7.0) + math.cos(deg * 0.12) * 4
        px = cx + (rad + distort) * math.cos(rad_angle)
        py = cy + (rad + distort) * math.sin(rad_angle) * 0.72
        r_pts.append((px, py))
    r_col = EMBER if ring in (2, 3) else (SIGNAL if ring in (4, 5) else SURF3)
    draw.polygon(r_pts, outline=r_col, width=1)

draw.text((c2_x + 18, c2_y + 225), "WARP: 3.42 · HARMONICS: 8", font=f_small, fill=SIGNAL)

# Code snippet box
draw.rounded_rectangle((c2_x + 12, c2_y + 258, c2_x + c2_w - 12, c2_y + 440), 5, fill=DEEP, outline=SURF3, width=1)
draw.text((c2_x + 18, c2_y + 266), "FIELD KERNEL DECLARATION", font=f_small, fill=INK3)

code_lines = [
    ("param bass_drive = 0.5", EMBER),
    ("param warp_rate  = 1.2", SIGNAL),
    ("param col_phase  = 0.8", INK2),
    ("", INK3),
    ("vec2 p = (fragCoord - 0.5 * R) / R.y;", INK),
    ("p += sin(p.yx * 4.0 + time) * 0.2;", INK2),
    ("float d = length(p) - bass_drive;", SIGNAL),
    ("float pattern = sin(d * 24.0 + time);", INK),
    ("fragColor = vec4(pal(pattern), 1.0);", EMBER)
]
for li, (line, lcol) in enumerate(code_lines):
    draw.text((c2_x + 18, c2_y + 284 + li * 15), line, font=f_code, fill=lcol)

# Parameter controls
draw.rounded_rectangle((c2_x + 12, c2_y + 450, c2_x + c2_w - 12, c2_y + 515), 5, fill=DEEP, outline=SURF3, width=1)
draw.text((c2_x + 18, c2_y + 456), "REAL-TIME PARAMETERS", font=f_small, fill=INK3)

draw.text((c2_x + 18, c2_y + 474), "Bass Drive", font=f_small, fill=INK2)
draw.rounded_rectangle((c2_x + 85, c2_y + 475, c2_x + c2_w - 20, c2_y + 482), 3, fill=SURF1)
draw.rounded_rectangle((c2_x + 85, c2_y + 475, c2_x + 185, c2_y + 482), 3, fill=EMBER)

draw.text((c2_x + 18, c2_y + 494), "Warp Rate", font=f_small, fill=INK2)
draw.rounded_rectangle((c2_x + 85, c2_y + 495, c2_x + c2_w - 20, c2_y + 502), 3, fill=SURF1)
draw.rounded_rectangle((c2_x + 85, c2_y + 495, c2_x + 215, c2_y + 502), 3, fill=SIGNAL)

# Output jack on Card 2
c2_out_cx = c2_x + c2_w - 12
c2_out_cy = c2_y + 360
draw.ellipse((c2_out_cx - 4, c2_out_cy - 4, c2_out_cx + 4, c2_out_cy + 4), fill=DEEP, outline=EMBER, width=2)
draw.ellipse((c2_out_cx - 2, c2_out_cy - 2, c2_out_cx + 2, c2_out_cy + 2), fill=EMBER)

# -------------------------------------------------------------
# Card 3: Feedback & Procedural 3D (x=645, w=265)
# -------------------------------------------------------------
c3_x, c3_y, c3_w, c3_h = 645, 75, 265, 535
draw_card(c3_x, c3_y, c3_w, c3_h, "Feedback & 3D", "COMPOSITOR", "PASS 2")

# Input pin on Card 3
c3_in_cx = c3_x + 12
c3_in_cy = c2_out_cy
draw.ellipse((c3_in_cx - 4, c3_in_cy - 4, c3_in_cx + 4, c3_in_cy + 4), fill=DEEP, outline=EMBER, width=2)
draw.ellipse((c3_in_cx - 2, c3_in_cy - 2, c3_in_cx + 2, c3_in_cy + 2), fill=EMBER)

# Procedural Geometry Mesh Wireframe
draw.rounded_rectangle((c3_x + 12, c3_y + 45, c3_x + c3_w - 12, c3_y + 245), 5, fill=DEEP, outline=SURF3, width=1)
draw.text((c3_x + 18, c3_y + 50), "3D INSTANCING & DISPLACEMENT", font=f_small, fill=INK3)

# Wireframe torus / lattice
m_cx = c3_x + c3_w // 2
m_cy = c3_y + 148
for ring in range(5):
    rad_x = 24 + ring * 16
    rad_y = 12 + ring * 8
    draw.ellipse((m_cx - rad_x, m_cy - rad_y, m_cx + rad_x, m_cy + rad_y), outline=SURF3, width=1)
for i in range(12):
    ang = i * (math.pi / 6.0)
    x1 = m_cx + 24 * math.cos(ang)
    y1 = m_cy + 12 * math.sin(ang)
    x2 = m_cx + 88 * math.cos(ang)
    y2 = m_cy + 44 * math.sin(ang)
    draw.line((x1, y1, x2, y2), fill=SIGNAL if i % 3 == 0 else SURF2, width=1)
draw.ellipse((m_cx - 6, m_cy - 6, m_cx + 6, m_cy + 6), fill=EMBER)

draw.text((c3_x + 18, c3_y + 225), "VERTICES: 16,384 · DRAW CALLS: 1", font=f_small, fill=INK2)

# Feedback Loop State Box
draw.rounded_rectangle((c3_x + 12, c3_y + 258, c3_x + c3_w - 12, c3_y + 400), 5, fill=DEEP, outline=SURF3, width=1)
draw.text((c3_x + 18, c3_y + 266), "FRAME PERSISTENCE LOOP", font=f_small, fill=INK3)

fb_params = [
    ("Feedback Decay", "0.94 (Stable attenuation)", c3_y + 295),
    ("Radial Zoom", "+1.02x (Continuous bloom)", c3_y + 335),
    ("Hue Rotation", "+2.5 deg (Harmonic drift)", c3_y + 375)
]
for fp_name, fp_val, fpy in fb_params:
    draw.text((c3_x + 18, fpy), fp_name, font=f_card_title, fill=INK)
    draw.text((c3_x + 18, fpy + 14), fp_val, font=f_small, fill=SIGNAL)

# Performance matrix
draw.rounded_rectangle((c3_x + 12, c3_y + 412, c3_x + c3_w - 12, c3_y + 515), 5, fill=DEEP, outline=SURF3, width=1)
draw.text((c3_x + 18, c3_y + 418), "FRAME TIMING & STABILITY", font=f_small, fill=INK3)

draw.text((c3_x + 18, c3_y + 440), "GPU Cook Time", font=f_small, fill=INK2)
draw.text((c3_x + c3_w - 70, c3_y + 440), "2.1 ms", font=f_label, fill=GREEN)

draw.text((c3_x + 18, c3_y + 465), "Display Pacing", font=f_small, fill=INK2)
draw.text((c3_x + c3_w - 85, c3_y + 465), "60.00 FPS", font=f_label, fill=GREEN)

draw.text((c3_x + 18, c3_y + 490), "VRAM Footprint", font=f_small, fill=INK2)
draw.text((c3_x + c3_w - 75, c3_y + 490), "142 MB", font=f_label, fill=SIGNAL)

# Output jack on Card 3
c3_out_cx = c3_x + c3_w - 12
c3_out_cy = c3_y + 465
draw.ellipse((c3_out_cx - 4, c3_out_cy - 4, c3_out_cx + 4, c3_out_cy + 4), fill=DEEP, outline=SIGNAL, width=2)
draw.ellipse((c3_out_cx - 2, c3_out_cy - 2, c3_out_cx + 2, c3_out_cy + 2), fill=SIGNAL)

# -------------------------------------------------------------
# Card 4: Stage Control & Multi-Screen Matrix (x=940, w=225)
# -------------------------------------------------------------
c4_x, c4_y, c4_w, c4_h = 940, 75, 225, 535
draw_card(c4_x, c4_y, c4_w, c4_h, "Stage Matrix", "PROJECTION", "OUTPUT")

# Input pin on Card 4
c4_in_cx = c4_x + 12
c4_in_cy = c3_out_cy
draw.ellipse((c4_in_cx - 4, c4_in_cy - 4, c4_in_cx + 4, c4_in_cy + 4), fill=DEEP, outline=SIGNAL, width=2)
draw.ellipse((c4_in_cx - 2, c4_in_cy - 2, c4_in_cx + 2, c4_in_cy + 2), fill=SIGNAL)

# Perspective Keystone Box
draw.rounded_rectangle((c4_x + 12, c4_y + 45, c4_x + c4_w - 12, c4_y + 190), 5, fill=DEEP, outline=SURF3, width=1)
draw.text((c4_x + 18, c4_y + 50), "SURFACE KEYSTONE WARP", font=f_small, fill=INK3)

warp_poly = [
    (c4_x + 35, c4_y + 80),
    (c4_x + c4_w - 28, c4_y + 72),
    (c4_x + c4_w - 42, c4_y + 165),
    (c4_x + 24, c4_y + 155)
]
draw.polygon(warp_poly, fill=SURF1, outline=SIGNAL)
for ptx, pty in warp_poly:
    draw.ellipse((ptx - 3, pty - 3, ptx + 3, pty + 3), fill=EMBER, outline=INK)
draw.text((c4_x + 40, c4_y + 115), "CALIBRATION OK", font=f_small, fill=INK2)

# Hardware Control Box
draw.rounded_rectangle((c4_x + 12, c4_y + 200, c4_x + c4_w - 12, c4_y + 335), 5, fill=DEEP, outline=SURF3, width=1)
draw.text((c4_x + 18, c4_y + 206), "HARDWARE BINDINGS", font=f_small, fill=INK3)

ctrls = [
    ("MIDI CC #16", "Feedback master decay", c4_y + 230),
    ("MIDI CC #17", "Warp complexity", c4_y + 265),
    ("OSC /cue/drop", "Preset trigger scene", c4_y + 300)
]
for c_tag, c_desc, c_py in ctrls:
    draw.text((c4_x + 18, c_py), c_tag, font=f_code, fill=EMBER)
    draw.text((c4_x + 18, c_py + 12), c_desc, font=f_small, fill=INK3)

# Stage routing channels
draw.rounded_rectangle((c4_x + 12, c4_y + 348, c4_x + c4_w - 12, c4_y + 515), 5, fill=DEEP, outline=SURF3, width=1)
draw.text((c4_x + 18, c4_y + 354), "TARGET DESTINATIONS", font=f_small, fill=INK3)

destinations = [
    ("Main LED Wall", "Display 1 (4K HDR)", EMBER, c4_y + 380),
    ("Stage Projector", "Keystone Warped", SIGNAL, c4_y + 425),
    ("Syphon / Spout", "Zero-Copy GPU Bus", INK, c4_y + 470)
]
for dname, dinfo, dcol, dy in destinations:
    draw.rounded_rectangle((c4_x + 18, dy, c4_x + c4_w - 18, dy + 38), 4, fill=SURF2, outline=SURF3, width=1)
    draw.text((c4_x + 24, dy + 6), dname, font=f_card_title, fill=dcol)
    draw.text((c4_x + 24, dy + 21), dinfo, font=f_small, fill=INK3)

# -------------------------------------------------------------
# Inter-Card Bezier Cables
# -------------------------------------------------------------
def draw_spline_cable(p1, p2, col, width=2):
    x1, y1 = p1
    x2, y2 = p2
    dx = abs(x2 - x1) * 0.55
    pts = []
    steps = 40
    for s in range(steps + 1):
        t = s / steps
        bx = (1 - t)**3 * x1 + 3 * (1 - t)**2 * t * (x1 + dx) + 3 * (1 - t) * t**2 * (x2 - dx) + t**3 * x2
        by = (1 - t)**3 * y1 + 3 * (1 - t)**2 * t * y1 + 3 * (1 - t) * t**2 * y2 + t**3 * y2
        pts.append((bx, by))
    draw.line(pts, fill=col, width=width)

# Cable 1: Audio Analyze (low) -> Field Pixel (bass_drive)
draw_spline_cable(c1_out_coords["low"], c2_in_coords["bass_drive"], EMBER, 2)

# Cable 2: Audio Analyze (mid) -> Field Pixel (warp_rate)
draw_spline_cable(c1_out_coords["mid"], c2_in_coords["warp_rate"], SIGNAL, 2)

# Cable 3: Field Pixel (out) -> Feedback (in)
draw_spline_cable((c2_out_cx, c2_out_cy), (c3_in_cx, c3_in_cy), EMBER, 2)

# Cable 4: Feedback (out) -> Stage Matrix (in)
draw_spline_cable((c3_out_cx, c3_out_cy), (c4_in_cx, c4_in_cy), SIGNAL, 2)

# -------------------------------------------------------------
# Bottom Status Bar
# -------------------------------------------------------------
draw.line((35, H - 42, W - 35, H - 42), fill=SURF3, width=1)
draw.text((35, H - 32), "AUDIO MODULATION: LOCK-FREE RING BUFFER // SHADER PIPELINE: SINGLE-PASS GLSL // ZERO ALLOCATIONS", font=f_small, fill=INK3)
draw.text((W - 355, H - 32), "MIT LICENSED // MACOS · WINDOWS · LINUX // INFINITEDEVICES.ORG", font=f_small, fill=SIGNAL)

# Save
out_dir = os.path.join(ROOT, "website", "assets")
os.makedirs(out_dir, exist_ok=True)
out_path = os.path.join(out_dir, "blog_generative-visuals-live-performance.png")
img.save(out_path, optimize=True)
print(f"ok  Saved 1200x675 hero diagram to: {out_path}")
