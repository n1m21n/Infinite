#!/usr/bin/env python3
"""Generates the 1200x675 technical hero diagram for image-to-sound-spectrogram-additive-synthesis.
Adheres strictly to docs/brand/BRAND.md and brand.json (Midnight deep #0D1020 mode).
"""
import math
import os
from PIL import Image, ImageDraw, ImageFont

W, H = 1200, 675
img = Image.new("RGBA", (W, H), (13, 16, 32, 255))  # Midnight deep #0D1020
draw = ImageDraw.Draw(img)

# Color tokens from brand.json (Midnight mode)
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

try:
    f_title = ImageFont.truetype(GEIST_SEMIBOLD, 26)
    f_sub = ImageFont.truetype(GEIST_MEDIUM, 13)
    f_badge = ImageFont.truetype(GEIST_MONO, 11)
    f_card_hdr = ImageFont.truetype(GEIST_SEMIBOLD, 12)
    f_node_title = ImageFont.truetype(GEIST_SEMIBOLD, 12)
    f_node_cat = ImageFont.truetype(GEIST_MONO, 10)
    f_body = ImageFont.truetype(GEIST_MEDIUM, 11)
    f_mono = ImageFont.truetype(GEIST_MONO, 11)
    f_mono_sm = ImageFont.truetype(GEIST_MONO, 9)
except Exception:
    f_title = f_sub = f_badge = f_card_hdr = f_node_title = f_node_cat = f_body = f_mono = f_mono_sm = ImageFont.load_default()


def hsv_to_rgb(h, s, v):
    h6 = (h * 6.0) % 6.0
    hi = int(h6)
    f = h6 - hi
    p = v * (1.0 - s)
    q = v * (1.0 - f * s)
    t = v * (1.0 - (1.0 - f) * s)
    if hi == 0:
        r, g, b = v, t, p
    elif hi == 1:
        r, g, b = q, v, p
    elif hi == 2:
        r, g, b = p, v, t
    elif hi == 3:
        r, g, b = p, q, v
    elif hi == 4:
        r, g, b = t, p, v
    else:
        r, g, b = v, p, q
    return int(r * 255), int(g * 255), int(b * 255)


def draw_bezier(d, p0, p1, p2, p3, color, width=2, steps=40):
    pts = []
    for i in range(steps + 1):
        t = i / steps
        mt = 1.0 - t
        x = mt**3 * p0[0] + 3 * mt**2 * t * p1[0] + 3 * mt * t**2 * p2[0] + t**3 * p3[0]
        y = mt**3 * p0[1] + 3 * mt**2 * t * p1[1] + 3 * mt * t**2 * p2[1] + t**3 * p3[1]
        pts.append((x, y))
    d.line(pts, fill=color, width=width)


# 1. Background dot grid
for gx in range(24, W - 24, 28):
    for gy in range(24, H - 24, 28):
        draw.ellipse([gx - 1, gy - 1, gx + 1, gy + 1], fill=DOT)

# 2. Outer frame
draw.rounded_rectangle([20, 20, W - 20, H - 20], radius=16, outline=SURF2, width=1)

# 3. Header badges and title
draw.rounded_rectangle([45, 40, 385, 66], radius=6, fill=SURF1, outline=SURF3, width=1)
draw.text((58, 46), "INFINITE  //  AUDIO-VISUAL SYNTHESIS & DSP", font=f_badge, fill=SIGNAL)

draw.rounded_rectangle([W - 350, 40, W - 45, 66], radius=6, fill=SURF1, outline=SURF3, width=1)
draw.text((W - 336, 46), "TRACK B: CREATIVE CODING & SOUND DESIGN", font=f_badge, fill=INK)

draw.text((45, 80), "Image to Sound: Spectrogram Additive Synthesis", font=f_title, fill=INK)
draw.text((45, 116), "Bilinear 2D texture scanning, five frequency scale laws, and 256-partial stereo resynthesis", font=f_sub, fill=INK2)
draw.line([(45, 142), (W - 45, 142)], fill=SURF2, width=1)

# 4. Left Pane: 2D Spectrogram Matrix (u, v) + Playhead
lx1, ly1, lx2, ly2 = 45, 158, 575, H - 35
draw.rounded_rectangle([lx1, ly1, lx2, ly2], radius=12, fill=GROUND, outline=SURF2, width=1)
draw.text((lx1 + 20, ly1 + 14), "2D SPECTROGRAM MATRIX (u, v) & PLAYHEAD", font=f_card_hdr, fill=SIGNAL)

# Spectrogram image area
sx1, sy1, sx2, sy2 = lx1 + 52, ly1 + 48, lx2 - 24, ly1 + 346
sw, sh = sx2 - sx1, sy2 - sy1
spec_img = Image.new("RGBA", (sw, sh), (10, 13, 26, 255))
spec_px = spec_img.load()

for py in range(sh):
    ny = 1.0 - (py / (sh - 1))  # 0 = bass (bottom), 1 = treble (top)
    for px in range(sw):
        nx = px / (sw - 1)       # 0 = left, 1 = right
        intensity = 0.0
        hue = 0.62 + 0.15 * nx   # cool Signal/cyan base

        # Harmonic horizontal bars
        for h in range(1, 6):
            bar_y = 0.09 * h
            dy = abs(ny - bar_y)
            if dy < 0.015:
                env = 1.0 - dy / 0.015
                pulse = 0.45 + 0.55 * math.cos(nx * math.pi * 8.0)
                intensity = max(intensity, env * 0.72 * pulse)

        # Rising exponential chirp
        chirp_y = 0.06 + 0.84 * (nx ** 1.75)
        dc = abs(ny - chirp_y)
        if dc < 0.018:
            intensity = max(intensity, (1.0 - dc / 0.018) * 0.95)
            hue = 0.52 + 0.22 * nx

        # Spiral / Lissajous glyph in upper middle
        dx = (nx - 0.52) * 1.45
        dy = ny - 0.56
        rad = math.sqrt(dx * dx + dy * dy)
        ang = math.atan2(dy, dx)
        spiral_r = 0.17 + 0.045 * math.sin(ang * 5.0 + nx * 6.28318)
        ds = abs(rad - spiral_r)
        if ds < 0.022:
            intensity = max(intensity, (1.0 - ds / 0.022) * 0.90)
            hue = 0.08 + 0.55 * (0.5 + 0.5 * math.sin(ang * 2.0))

        # Subtle background grid lines inside spectrogram
        grid = 0.04 if (px % 48 == 0 or py % 40 == 0) else 0.0
        if intensity > 0.01:
            r, g, b = hsv_to_rgb(hue % 1.0, 0.72, min(1.0, intensity))
            spec_px[px, py] = (max(10, r), max(13, g), max(26, b), 255)
        elif grid > 0:
            spec_px[px, py] = (32, 38, 63, 255)

img.paste(spec_img, (sx1, sy1))
draw.rounded_rectangle([sx1, sy1, sx2, sy2], radius=4, outline=SURF3, width=1)

# Frequency axis labels (left of spectrogram)
for label, frac in [("16 kHz", 0.04), ("4 kHz", 0.28), ("1 kHz", 0.52), ("250 Hz", 0.76), ("40 Hz", 0.96)]:
    yy = int(sy1 + frac * sh)
    draw.text((lx1 + 10, yy - 5), label, font=f_mono_sm, fill=INK3)

# Single Ember focal element: Playhead scanline u(t)
play_u = 0.58
play_x = int(sx1 + play_u * sw)
draw.line([(play_x, sy1 - 4), (play_x, sy2 + 4)], fill=EMBER, width=2)
draw.polygon([(play_x - 6, sy1 - 10), (play_x + 6, sy1 - 10), (play_x, sy1 - 2)], fill=EMBER)
draw.text((play_x + 8, sy1 - 12), "u(t) = 0.58", font=f_mono_sm, fill=EMBER)

# Sampled partial dots along the scanline
for k in range(12):
    ky = sy2 - int((k + 0.5) / 12.0 * sh)
    draw.ellipse([play_x - 3, ky - 3, play_x + 3, ky + 3], fill=INK, outline=EMBER, width=1)

# Axis caption & bottom info card in left pane
draw.text((sx1, sy2 + 10), "u: Time Axis (0.0 -> 1.0)   |   v: 64 / 128 / 256 Sine Partials", font=f_mono_sm, fill=INK2)

draw.rounded_rectangle([lx1 + 16, sy2 + 32, lx2 - 16, ly2 - 14], radius=8, fill=SURF1, outline=SURF2, width=1)
draw.text((lx1 + 28, sy2 + 44), "AUDIO THREAD PIPELINE (LOCK-FREE SWAP)", font=f_badge, fill=SIGNAL)
draw.text((lx1 + 28, sy2 + 66), "1. SampleBilinear(u, normY) -> RGBA texel interpolation", font=f_mono_sm, fill=INK)
draw.text((lx1 + 28, sy2 + 84), "2. EvaluatePartialColor() -> gate, gamma, stereo pan", font=f_mono_sm, fill=INK2)
draw.text((lx1 + 28, sy2 + 102), "3. FastSineTable[4096] * de-zippered L/R partial gains", font=f_mono_sm, fill=INK2)

# 5. Right Pane Top: Five Frequency Scale Laws
rx1, ry1, rx2, ry2 = 595, 158, W - 45, 412
draw.rounded_rectangle([rx1, ry1, rx2, ry2], radius=12, fill=GROUND, outline=SURF2, width=1)
draw.text((rx1 + 20, ry1 + 14), "FIVE FREQUENCY SCALE LAWS  f(k)", font=f_card_hdr, fill=SIGNAL)

# Plot area for scale curves
px1, py1, px2, py2 = rx1 + 24, ry1 + 42, rx1 + 290, ry2 - 28
draw.rounded_rectangle([px1, py1, px2, py2], radius=8, fill=DEEP, outline=SURF2, width=1)

pw, ph = px2 - px1 - 24, py2 - py1 - 24
ox, oy = px1 + 12, py2 - 12

# Plot Log, Linear, Mel curves
log_pts, lin_pts, mel_pts = [], [], []
for i in range(41):
    t = i / 40.0
    x = ox + t * pw
    # Normalized frequency [0..1]
    y_lin = t
    f_log = 40.0 * ((16000.0 / 40.0) ** t)
    y_log = (f_log - 40.0) / (16000.0 - 40.0)
    mel_min = 2595.0 * math.log10(1.0 + 40.0 / 700.0)
    mel_max = 2595.0 * math.log10(1.0 + 16000.0 / 700.0)
    f_mel = 700.0 * (10.0 ** ((mel_min + t * (mel_max - mel_min)) / 2595.0) - 1.0)
    y_mel = (f_mel - 40.0) / (16000.0 - 40.0)

    lin_pts.append((x, oy - y_lin * ph))
    log_pts.append((x, oy - y_log * ph))
    mel_pts.append((x, oy - y_mel * ph))

draw.line(lin_pts, fill=INK3, width=2)
draw.line(mel_pts, fill=CYAN, width=2)
draw.line(log_pts, fill=SIGNAL, width=2)
draw.text((px1 + 10, py1 + 8), "f(y) / 16 kHz", font=f_mono_sm, fill=INK3)
draw.text((px2 - 68, py2 - 18), "normK ->", font=f_mono_sm, fill=INK3)

# Legend and Color-to-Pan summary on right half of top card
lx_col = rx1 + 308
scales = [
    ("Logarithmic", "min * (max/min)^y", SIGNAL),
    ("Linear (Hz)", "min + y * (max - min)", INK3),
    ("Mel Scale", "2595 * log10(1 + f/700)", CYAN),
    ("Harmonic", "rootFreq * (k + 1)", GREEN),
    ("Chromatic", "12-TET semitone grid", INK2),
]
for idx, (name, desc, col) in enumerate(scales):
    yy = ry1 + 46 + idx * 38
    draw.rounded_rectangle([lx_col, yy, rx2 - 20, yy + 30], radius=6, fill=SURF1, outline=SURF2, width=1)
    draw.ellipse([lx_col + 10, yy + 10, lx_col + 20, yy + 20], fill=col)
    draw.text((lx_col + 28, yy + 4), name, font=f_node_title, fill=INK)
    draw.text((lx_col + 28, yy + 17), desc, font=f_mono_sm, fill=INK2)

# 6. Right Pane Bottom: Modular Graph Patch Topology
gx1, gy1, gx2, gy2 = 595, 428, W - 45, H - 35
draw.rounded_rectangle([gx1, gy1, gx2, gy2], radius=12, fill=GROUND, outline=SURF2, width=1)
draw.text((gx1 + 20, gy1 + 14), "MODULAR AUDIOVISUAL PATCH GRAPH", font=f_card_hdr, fill=SIGNAL)


def draw_node(x, y, w, h, title, cat, accent):
    draw.rounded_rectangle([x, y, x + w, y + h], radius=8, fill=SURF1, outline=SURF3, width=1)
    draw.rounded_rectangle([x, y, x + w, y + 24], radius=8, fill=SURF2)
    draw.rectangle([x, y + 16, x + w, y + 24], fill=SURF2)
    draw.ellipse([x + 10, y + 7, x + 18, y + 15], fill=accent)
    draw.text((x + 24, y + 5), title, font=f_node_title, fill=INK)
    draw.text((x + 10, y + 32), cat, font=f_mono_sm, fill=INK2)


# Nodes
draw_node(gx1 + 20, gy1 + 44, 145, 58, "Sketch / Noise", "image out", CYAN)
draw_node(gx1 + 20, gy1 + 124, 145, 58, "Note Sequencer", "note out (MIDI)", GREEN)
draw_node(gx1 + 205, gy1 + 78, 165, 74, "Spectral Synth", "64..256 partials\n8x poly + unison", SIGNAL)
draw_node(gx1 + 410, gy1 + 44, 130, 58, "Audio Out", "stereo L/R", EMBER)
draw_node(gx1 + 410, gy1 + 124, 130, 58, "Audio Ribbon", "3D visualiser", CYAN)

# Cables
draw_bezier(draw, (gx1 + 165, gy1 + 73), (gx1 + 185, gy1 + 73), (gx1 + 185, gy1 + 102), (gx1 + 205, gy1 + 102), CYAN, 2)
draw_bezier(draw, (gx1 + 165, gy1 + 153), (gx1 + 185, gy1 + 153), (gx1 + 185, gy1 + 132), (gx1 + 205, gy1 + 132), GREEN, 2)
draw_bezier(draw, (gx1 + 370, gy1 + 110), (gx1 + 390, gy1 + 110), (gx1 + 390, gy1 + 73), (gx1 + 410, gy1 + 73), SIGNAL, 2)
draw_bezier(draw, (gx1 + 370, gy1 + 122), (gx1 + 390, gy1 + 122), (gx1 + 390, gy1 + 153), (gx1 + 410, gy1 + 153), SIGNAL, 2)

out_path = os.path.join(REPO_ROOT, "website", "assets", "blog_image-to-sound-spectrogram-additive-synthesis.png")
img.save(out_path, "PNG")
print("Wrote", out_path)
