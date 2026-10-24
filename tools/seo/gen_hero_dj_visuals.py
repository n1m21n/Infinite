#!/usr/bin/env python3
"""Generates the 1200x675 technical hero diagram for automatic-visuals-for-dj-sets.
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
f_card_title = ImageFont.truetype(font_sans_path, 14)
f_badge = ImageFont.truetype(font_mono_path, 11)
f_code = ImageFont.truetype(font_mono_path, 11)
f_label = ImageFont.truetype(font_mono_path, 11)
f_small = ImageFont.truetype(font_mono_path, 10)

# 1. Background subtle dot grid
for gx in range(24, W, 24):
    for gy in range(24, H, 24):
        draw.point((gx, gy), fill=DOT)

# Outer decorative frame
draw.rounded_rectangle([16, 16, W - 16, H - 16], radius=12, outline=LINE, width=1)

# Top Bar
draw.text((45, 34), "INFINITE", font=f_title, fill=INK)
draw.text((145, 40), "//  AUTONOMOUS AUDIO-REACTIVE DJ VISUAL PIPELINE", font=f_sub, fill=SIGNAL)

badge_text = "BOOTH AUDIO IN -> 8-BAND FFT -> ENVELOPE SMOOTHING -> FIELD PIXEL -> STAGE PROJECTION"
bw = draw.textlength(badge_text, font=f_badge) + 20
draw.rounded_rectangle((W - 45 - bw, 34, W - 45, 60), 6, fill=SURF1, outline=LINE, width=1)
draw.text((W - 45 - bw + 10, 40), badge_text, font=f_badge, fill=INK2)

# Helper function for card
def draw_card(x, y, w, h, title, category, badge=None):
    draw.rounded_rectangle((x, y + 5, x + w, y + h + 5), 10, fill=(4, 5, 14, 160))
    draw.rounded_rectangle((x, y, x + w, y + h), 10, fill=SURF1, outline=LINE, width=1)
    draw.rounded_rectangle((x, y, x + w, y + 38), 10, fill=SURF2)
    draw.rectangle((x, y + 26, x + w, y + 38), fill=SURF2)
    draw.line((x, y + 38, x + w, y + 38), fill=LINE, width=1)
    draw.text((x + 14, y + 10), title, font=f_card_title, fill=INK)
    tw = draw.textlength(title, font=f_card_title)
    draw.text((x + 14 + tw + 10, y + 13), category, font=f_small, fill=INK3)
    if badge:
        bbw = draw.textlength(badge, font=f_small) + 12
        draw.rounded_rectangle((x + w - 14 - bbw, y + 9, x + w - 14, y + 29), 4, fill=SURF3)
        draw.text((x + w - 14 - bbw + 6, y + 12), badge, font=f_small, fill=SIGNAL)

# -------------------------------------------------------------
# Card 1: Audio In & Audio Analyze (Left)
# -------------------------------------------------------------
c1_x, c1_y, c1_w, c1_h = 45, 85, 290, 545
draw_card(c1_x, c1_y, c1_w, c1_h, "Audio Analyze", "DSP // FFT & ONSET", "48 kHz")

# Waveform box
draw.rounded_rectangle((c1_x + 14, c1_y + 50, c1_x + c1_w - 14, c1_y + 115), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c1_x + 22, c1_y + 56), "LINE IN BUFFER (BOOTH OUT)", font=f_small, fill=INK3)
wave_pts = []
for i in range(c1_w - 44):
    wx = c1_x + 22 + i
    wy = c1_y + 86 + math.sin(i * 0.14) * 12 * math.sin(i * 0.02 + 0.8) + math.sin(i * 0.38) * 4
    wave_pts.append((wx, wy))
draw.line(wave_pts, fill=SIGNAL, width=1)

# FFT Spectrum box
draw.rounded_rectangle((c1_x + 14, c1_y + 125, c1_x + c1_w - 14, c1_y + 250), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c1_x + 22, c1_y + 131), "8-BAND SPECTRAL ANALYSIS", font=f_small, fill=INK3)

band_names = ["SUB", "BASS", "LMID", "MID", "HMID", "PRES", "BRIL", "AIR"]
band_heights = [78, 92, 58, 42, 48, 35, 26, 18]
bar_w = 22
bar_gap = 7
bx_start = c1_x + 22
for bi, (bname, bh) in enumerate(zip(band_names, band_heights)):
    bx = bx_start + bi * (bar_w + bar_gap)
    by_bot = c1_y + 230
    by_top = by_bot - bh
    draw.rectangle((bx, by_bot - 82, bx + bar_w, by_bot), fill=SURF1)
    bcolor = EMBER if bi in (0, 1) else (SIGNAL if bi in (2, 3) else INK2)
    draw.rectangle((bx, by_top, bx + bar_w, by_bot), fill=bcolor)
    draw.line((bx, by_top - 2, bx + bar_w, by_top - 2), fill=INK, width=1)
    draw.text((bx + 1, c1_y + 234), bname[:3], font=f_small, fill=INK3)

# Onset Trigger Pill
draw.rounded_rectangle((c1_x + 14, c1_y + 260, c1_x + c1_w - 14, c1_y + 288), 4, fill=DEEP, outline=SURF3, width=1)
draw.ellipse((c1_x + 24, c1_y + 270, c1_x + 32, c1_y + 278), fill=GREEN)
draw.text((c1_x + 38, c1_y + 268), "TRANSIENT ONSET: BEAT LOCKED", font=f_small, fill=INK)

# Output Pins
draw.text((c1_x + 14, c1_y + 302), "SPECTRAL OUTPUT PINS", font=f_small, fill=INK2)
pins_out1 = [
    ("low", "Sub 250 Hz (Kick/Bass)", EMBER, c1_y + 326),
    ("mid", "250 Hz - 4 kHz (Lead/Snare)", SIGNAL, c1_y + 376),
    ("high", "> 4 kHz (Hi-hat/Transients)", SIGNAL, c1_y + 426),
    ("level", "RMS total dynamic energy", INK2, c1_y + 476)
]
c1_out_coords = {}
for pname, pdesc, pcol, py in pins_out1:
    draw.rounded_rectangle((c1_x + 14, py, c1_x + c1_w - 14, py + 38), 6, fill=SURF2, outline=SURF3, width=1)
    draw.text((c1_x + 24, py + 5), pname, font=f_card_title, fill=pcol)
    draw.text((c1_x + 24, py + 21), pdesc, font=f_small, fill=INK3)
    pin_cx = c1_x + c1_w - 14
    pin_cy = py + 19
    draw.ellipse((pin_cx - 5, pin_cy - 5, pin_cx + 5, pin_cy + 5), fill=DEEP, outline=pcol, width=2)
    draw.ellipse((pin_cx - 2, pin_cy - 2, pin_cx + 2, pin_cy + 2), fill=pcol)
    c1_out_coords[pname] = (pin_cx, pin_cy)

# -------------------------------------------------------------
# Card 2: Envelope Follower & Damping (Middle-Left)
# -------------------------------------------------------------
c2_x, c2_y, c2_w, c2_h = 365, 85, 230, 545
draw_card(c2_x, c2_y, c2_w, c2_h, "Envelope Follower", "DYNAMICS", "ASYMMETRIC")

# Envelope Curve Visualizer Box
draw.rounded_rectangle((c2_x + 14, c2_y + 50, c2_x + c2_w - 14, c2_y + 145), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c2_x + 22, c2_y + 56), "ATTACK / RELEASE CURVE", font=f_small, fill=INK3)

# Draw curve: steep attack 2ms, exponential release 120ms
curve_pts = [(c2_x + 24, c2_y + 130), (c2_x + 36, c2_y + 75)]
for cx_step in range(1, c2_w - 60):
    cur_x = c2_x + 36 + cx_step
    # exponential decay
    decay = math.exp(-cx_step / 40.0)
    cur_y = c2_y + 130 - int(55 * decay)
    curve_pts.append((cur_x, cur_y))
draw.line(curve_pts, fill=EMBER, width=2)
draw.text((c2_x + 24, c2_y + 132), "Atk: 2ms", font=f_small, fill=EMBER)
draw.text((c2_x + 130, c2_y + 132), "Rel: 120ms", font=f_small, fill=INK2)

# Smoothed Parameter Sliders
draw.text((c2_x + 14, c2_y + 162), "CONDITIONED BUSES", font=f_small, fill=INK2)

sliders = [
    ("bass_env", 0.85, EMBER, c2_y + 185),
    ("mid_env", 0.52, SIGNAL, c2_y + 245),
    ("treble_env", 0.68, SIGNAL, c2_y + 305),
    ("macro_drift", 0.38, INK, c2_y + 365)
]

c2_in_coords = {}
c2_out_coords = {}

for sname, sval, scol, sy in sliders:
    # Left pin (input)
    lpin_cx = c2_x
    lpin_cy = sy + 20
    draw.ellipse((lpin_cx - 5, lpin_cy - 5, lpin_cx + 5, lpin_cy + 5), fill=DEEP, outline=scol, width=2)
    draw.ellipse((lpin_cx - 2, lpin_cy - 2, lpin_cx + 2, lpin_cy + 2), fill=scol)
    c2_in_coords[sname] = (lpin_cx, lpin_cy)

    # Slider card
    draw.rounded_rectangle((c2_x + 14, sy, c2_x + c2_w - 14, sy + 44), 6, fill=SURF2, outline=SURF3, width=1)
    draw.text((c2_x + 22, sy + 6), sname, font=f_label, fill=scol)
    draw.text((c2_x + c2_w - 55, sy + 6), f"{int(sval*100)}%", font=f_small, fill=INK2)
    
    # Progress track
    bar_x = c2_x + 22
    bar_y = sy + 26
    bar_w_avail = c2_w - 58
    draw.rounded_rectangle((bar_x, bar_y, bar_x + bar_w_avail, bar_y + 6), 3, fill=DEEP)
    draw.rounded_rectangle((bar_x, bar_y, bar_x + int(bar_w_avail * sval), bar_y + 6), 3, fill=scol)

    # Right pin (output)
    rpin_cx = c2_x + c2_w - 14
    rpin_cy = sy + 22
    draw.ellipse((rpin_cx - 5, rpin_cy - 5, rpin_cx + 5, rpin_cy + 5), fill=DEEP, outline=scol, width=2)
    draw.ellipse((rpin_cx - 2, rpin_cy - 2, rpin_cx + 2, rpin_cy + 2), fill=scol)
    c2_out_coords[sname] = (rpin_cx, rpin_cy)

# Anti-flicker badge
draw.rounded_rectangle((c2_x + 14, c2_y + 435, c2_x + c2_w - 14, c2_y + 515), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c2_x + 22, c2_y + 445), "ANTI-STROBE DAMPING", font=f_small, fill=SIGNAL)
draw.text((c2_x + 22, c2_y + 465), "Prevents visual seizure flicker", font=f_small, fill=INK3)
draw.text((c2_x + 22, c2_y + 485), "Exponential decay hold active", font=f_small, fill=INK3)

# -------------------------------------------------------------
# Card 3: Field Pixel Node (Middle-Right)
# -------------------------------------------------------------
c3_x, c3_y, c3_w, c3_h = 625, 85, 300, 545
draw_card(c3_x, c3_y, c3_w, c3_h, "Field Pixel", "GPU // GLSL KERNEL", "GLSL 150")

# Code editor frame
draw.rounded_rectangle((c3_x + 14, c3_y + 50, c3_x + c3_w - 14, c3_y + 450), 6, fill=DEEP, outline=SURF3, width=1)
draw.rectangle((c3_x + 14, c3_y + 50, c3_x + c3_w - 14, c3_y + 75), fill=SURF2)
draw.line((c3_x + 14, c3_y + 75, c3_x + c3_w - 14, c3_y + 75), fill=SURF3, width=1)
draw.text((c3_x + 22, c3_y + 56), "dj_tunnel.field  ->  GPU Kernel", font=f_small, fill=INK2)

code_lines = [
    ("1", "param float bass = 0.0 [0, 1];", EMBER),
    ("2", "param float mid = 0.0 [0, 1];", SIGNAL),
    ("3", "param float treble = 0.0 [0, 1];", SIGNAL),
    ("4", "vec2 p = (uv - 0.5) * vec2(aspect,1);", INK2),
    ("5", "float r = length(p);", INK2),
    ("6", "float a = atan(p.y, p.x);", INK2),
    ("7", "float tun = fract(2.0/(r+0.05*bass)", EMBER),
    ("8", "      + t * 0.4);", INK),
    ("9", "float ridges = sin(a*8.0+tun*12.0);", INK),
    ("10", "ridges = pow(ridges*0.5+0.5, 3.0)", INK),
    ("11", "       * (0.2 + 0.8 * treble);", SIGNAL),
    ("12", "float core = smoothstep(0.3+0.2*mid,", INK),
    ("13", "              0.0, r);", INK),
    ("14", "vec3 bg = vec3(0.08, 0.1, 0.22);", INK2),
    ("15", "vec3 hit = vec3(1.0, 0.42, 0.21);", EMBER),
    ("16", "col = mix(bg, hit, tun*ridges", INK),
    ("17", "          + core * energy);", EMBER),
]

cy = c3_y + 85
for lnum, ltext, lcol in code_lines:
    draw.text((c3_x + 20, cy), lnum, font=f_code, fill=INK3)
    draw.text((c3_x + 44, cy), ltext, font=f_code, fill=lcol)
    cy += 20

# Input pins on left of Card 3
c3_in_coords = {}
fp_in_pins = [
    ("bass", EMBER, c3_y + 88),
    ("mid", SIGNAL, c3_y + 108),
    ("treble", SIGNAL, c3_y + 128)
]
for pname, pcol, py in fp_in_pins:
    pin_cx = c3_x
    pin_cy = py + 4
    draw.ellipse((pin_cx - 5, pin_cy - 5, pin_cx + 5, pin_cy + 5), fill=DEEP, outline=pcol, width=2)
    draw.ellipse((pin_cx - 2, pin_cy - 2, pin_cx + 2, pin_cy + 2), fill=pcol)
    c3_in_coords[pname] = (pin_cx, pin_cy)

# Compile status
draw.rounded_rectangle((c3_x + 14, c3_y + 465, c3_x + c3_w - 14, c3_y + 515), 6, fill=SURF2, outline=SURF3, width=1)
draw.ellipse((c3_x + 24, c3_y + 478, c3_x + 32, c3_y + 486), fill=GREEN)
draw.text((c3_x + 38, c3_y + 476), "GPU Cook: 0.28 ms  ·  60.0 FPS", font=f_small, fill=INK)
draw.text((c3_x + 24, c3_y + 494), "Texture memory: Zero allocation", font=f_small, fill=INK3)

# Output pin
c3_out_cx = c3_x + c3_w - 14
c3_out_cy = c3_y + 300
draw.ellipse((c3_out_cx - 5, c3_out_cy - 5, c3_out_cx + 5, c3_out_cy + 5), fill=DEEP, outline=SIGNAL, width=2)
draw.ellipse((c3_out_cx - 2, c3_out_cy - 2, c3_out_cx + 2, c3_out_cy + 2), fill=SIGNAL)

# -------------------------------------------------------------
# Card 4: Stage Projection & Preview (Right)
# -------------------------------------------------------------
c4_x, c4_y, c4_w, c4_h = 955, 85, 200, 545
draw_card(c4_x, c4_y, c4_w, c4_h, "Projection", "DISPLAY", "1080p60")

# In pin on Viewport
c4_in_cx = c4_x
c4_in_cy = c3_out_cy
draw.ellipse((c4_in_cx - 5, c4_in_cy - 5, c4_in_cx + 5, c4_in_cy + 5), fill=DEEP, outline=SIGNAL, width=2)
draw.ellipse((c4_in_cx - 2, c4_in_cy - 2, c4_in_cx + 2, c4_in_cy + 2), fill=SIGNAL)

# Viewport preview box
vp_box = (c4_x + 14, c4_y + 50, c4_x + c4_w - 14, c4_y + 360)
draw.rounded_rectangle(vp_box, 8, fill=DEEP, outline=SURF3, width=1)

# Render radial tunnel visualizer simulation
cx = (vp_box[0] + vp_box[2]) / 2
cy_vp = (vp_box[1] + vp_box[3]) / 2

# Draw generative radial tunnel rings with Ember and Signal highlights
for ri in range(9, 0, -1):
    rad = ri * 15 + 4
    alpha = int(40 + (ri / 9.0) * 80)
    col = EMBER if ri in (3, 4) else (SIGNAL if ri in (6, 7) else (82, 94, 150))
    width = 3 if ri in (3, 4) else 1
    draw.ellipse((cx - rad, cy_vp - rad, cx + rad, cy_vp + rad), outline=col, width=width)

# Inner bright core
draw.ellipse((cx - 10, cy_vp - 10, cx + 10, cy_vp + 10), fill=EMBER)
draw.ellipse((cx - 4, cy_vp - 4, cx + 4, cy_vp + 4), fill=INK)

# Ray spikes
for angle in range(0, 360, 30):
    rad_ang = math.radians(angle)
    r_len = 55 + (20 if angle % 60 == 0 else 0)
    x1 = cx + math.cos(rad_ang) * 14
    y1 = cy_vp + math.sin(rad_ang) * 14
    x2 = cx + math.cos(rad_ang) * r_len
    y2 = cy_vp + math.sin(rad_ang) * r_len
    draw.line([(x1, y1), (x2, y2)], fill=SIGNAL, width=1)

# Projection info cards below preview
draw.rounded_rectangle((c4_x + 14, c4_y + 375, c4_x + c4_w - 14, c4_y + 435), 6, fill=SURF2, outline=SURF3, width=1)
draw.text((c4_x + 22, c4_y + 383), "HDMI DISPLAY 2", font=f_label, fill=INK)
draw.text((c4_x + 22, c4_y + 401), "1920x1080 @ 60 Hz", font=f_small, fill=INK2)
draw.text((c4_x + 22, c4_y + 417), "Borderless Fullscreen", font=f_small, fill=SIGNAL)

draw.rounded_rectangle((c4_x + 14, c4_y + 445, c4_x + c4_w - 14, c4_y + 515), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c4_x + 22, c4_y + 455), "STAGE PROTOCOLS", font=f_small, fill=INK3)
draw.text((c4_x + 22, c4_y + 473), "• Syphon (macOS)", font=f_small, fill=INK2)
draw.text((c4_x + 22, c4_y + 489), "• Spout (Windows)", font=f_small, fill=INK2)

# -------------------------------------------------------------
# Connecting Cables (Smooth cubic curves)
# -------------------------------------------------------------
def draw_cable(p1, p2, color, width=2):
    x1, y1 = p1
    x2, y2 = p2
    dx = max(30, abs(x2 - x1) * 0.5)
    pts = []
    steps = 40
    for s in range(steps + 1):
        t = s / float(steps)
        # Cubic Bezier interpolation: P0=(x1,y1), P1=(x1+dx,y1), P2=(x2-dx,y2), P3=(x2,y2)
        omt = 1.0 - t
        bx = omt**3 * x1 + 3 * omt**2 * t * (x1 + dx) + 3 * omt * t**2 * (x2 - dx) + t**3 * x2
        by = omt**3 * y1 + 3 * omt**2 * t * y1 + 3 * omt * t**2 * y2 + t**3 * y2
        pts.append((bx, by))
    draw.line(pts, fill=color, width=width)

# Cable 1: low -> bass_env
draw_cable(c1_out_coords["low"], c2_in_coords["bass_env"], EMBER, width=2)
# Cable 2: mid -> mid_env
draw_cable(c1_out_coords["mid"], c2_in_coords["mid_env"], SIGNAL, width=2)
# Cable 3: high -> treble_env
draw_cable(c1_out_coords["high"], c2_in_coords["treble_env"], SIGNAL, width=2)

# Cable 4: bass_env -> Field Pixel bass
draw_cable(c2_out_coords["bass_env"], c3_in_coords["bass"], EMBER, width=2)
# Cable 5: mid_env -> Field Pixel mid
draw_cable(c2_out_coords["mid_env"], c3_in_coords["mid"], SIGNAL, width=2)
# Cable 6: treble_env -> Field Pixel treble
draw_cable(c2_out_coords["treble_env"], c3_in_coords["treble"], SIGNAL, width=2)

# Cable 7: Field Pixel out -> Projection in
draw_cable((c3_out_cx, c3_out_cy), (c4_in_cx, c4_in_cy), SIGNAL, width=3)

# Save image
out_paths = [
    os.path.join(ROOT, "website", "assets", "blog_automatic-visuals-for-dj-sets.png"),
]
# If in worktree or repo, ensure both locations receive the file
alt_root = "/Users/namansoni/Infinite/worktrees/main" if "repo" in ROOT else "/Users/namansoni/Infinite/repo"
out_paths.append(os.path.join(alt_root, "website", "assets", "blog_automatic-visuals-for-dj-sets.png"))

for p in out_paths:
    os.makedirs(os.path.dirname(p), exist_ok=True)
    img.save(p, "PNG")
    print(f"Saved: {p}")
