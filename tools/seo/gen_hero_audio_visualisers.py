#!/usr/bin/env python3
"""Generates the 1200x675 technical hero diagram for lock-free-audio-visualisers-textures-ribbons-3d-displacement.
Adheres strictly to docs/brand/BRAND.md and brand.json (Midnight deep #0D1020 mode).
"""
import math
import os
from PIL import Image, ImageDraw, ImageFont

W, H = 1200, 675
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT_PATH = os.path.join(
    REPO_ROOT,
    "website",
    "assets",
    "blog_lock-free-audio-visualisers-textures-ribbons-3d-displacement.png",
)
FONT_DIR = os.path.join(REPO_ROOT, "art", "brand", "fonts")

# Brand Midnight tokens
DEEP = (13, 16, 32, 255)        # #0D1020
GROUND = (21, 25, 48, 255)      # #151930
SURF1 = (32, 38, 63, 255)       # #20263F
SURF2 = (42, 48, 82, 255)       # #2A3052
SURF3 = (52, 59, 94, 255)       # #343B5E
DOT = (35, 42, 74, 255)         # #232A4A
INK = (238, 241, 250, 255)      # #EEF1FA
INK2 = (163, 170, 203, 255)     # #A3AACB
INK3 = (136, 144, 177, 255)     # #8890B1
SIGNAL = (167, 177, 255, 255)   # #A7B1FF
EMBER = (255, 107, 53, 255)     # #FF6B35
GREEN = (74, 222, 128, 255)     # #4ADE80
CYAN = (56, 189, 248, 255)      # #38BDF8

f_title = ImageFont.truetype(os.path.join(FONT_DIR, "Geist-SemiBold.ttf"), 25)
f_sub = ImageFont.truetype(os.path.join(FONT_DIR, "Geist-Medium.ttf"), 13)
f_badge = ImageFont.truetype(os.path.join(FONT_DIR, "GeistMono-Medium.ttf"), 11)
f_card_hdr = ImageFont.truetype(os.path.join(FONT_DIR, "Geist-SemiBold.ttf"), 13)
f_node_title = ImageFont.truetype(os.path.join(FONT_DIR, "Geist-SemiBold.ttf"), 12)
f_body = ImageFont.truetype(os.path.join(FONT_DIR, "Geist-Medium.ttf"), 11)
f_mono = ImageFont.truetype(os.path.join(FONT_DIR, "GeistMono-Medium.ttf"), 11)
f_mono_sm = ImageFont.truetype(os.path.join(FONT_DIR, "GeistMono-Medium.ttf"), 10)

img = Image.new("RGBA", (W, H), DEEP)
draw = ImageDraw.Draw(img)


def rrect(box, r, fill, outline=None, width=1):
    draw.rounded_rectangle(box, radius=r, fill=fill, outline=outline, width=width)


def arrow_h(x1, x2, y, color, label=None, label_dy=-16, width=2):
    draw.line([(x1, y), (x2, y)], fill=color, width=width)
    d = 1 if x2 > x1 else -1
    draw.polygon([(x2, y), (x2 - d * 8, y - 5), (x2 - d * 8, y + 5)], fill=color)
    if label:
        bbox = draw.textbbox((0, 0), label, font=f_mono_sm)
        lw = bbox[2] - bbox[0]
        draw.text(((x1 + x2 - lw) // 2, y + label_dy), label, font=f_mono_sm, fill=color)


# Subtle dot grid background
for gx in range(24, W, 24):
    for gy in range(24, H, 24):
        draw.ellipse((gx - 1, gy - 1, gx + 1, gy + 1), fill=DOT)

# Outer frame
rrect((20, 20, W - 20, H - 20), 16, DEEP, outline=SURF2, width=1)

# Header
draw.text((48, 36), "Lock-free audio visualisers: textures, ribbons & 3D meshes", font=f_title, fill=INK)
draw.text((48, 70), "Zero-allocation MeterRing (4096 floats) bridging real-time audio blocks to GL_RGBA8 textures and deformed meshes", font=f_sub, fill=INK2)

# Top-right badge
rrect((922, 38, 1152, 66), 14, SURF1, outline=SURF3)
draw.text((938, 45), "AUDIO -> TEXTURE -> 3D", font=f_badge, fill=SIGNAL)

draw.line([(48, 102), (W - 48, 102)], fill=SURF2, width=1)

# 1. Left column: Audio Thread & MeterRing
L_X0, L_X1 = 48, 368
rrect((L_X0, 120, L_X1, 594), 16, GROUND, outline=SURF3)
draw.text((L_X0 + 20, 138), "AUDIO THREAD (LOCK-FREE)", font=f_badge, fill=EMBER)
draw.text((L_X0 + 20, 156), "AudioNode::ProcessBlock()", font=f_card_hdr, fill=INK)

# Stereo to Mono card
rrect((L_X0 + 18, 190, L_X1 - 18, 304), 12, SURF1, outline=SURF2)
draw.text((L_X0 + 32, 204), "Passthrough + Mono Tap", font=f_node_title, fill=CYAN)
draw.text((L_X0 + 32, 226), "mono[i] = (inL[i] + inR[i]) * 0.5", font=f_mono_sm, fill=SIGNAL)
draw.text((L_X0 + 32, 246), "Dry stereo passes through untouched", font=f_body, fill=INK2)
draw.text((L_X0 + 32, 266), "No heap allocs, no mutexes", font=f_body, fill=GREEN)

# MeterRing visualization
rrect((L_X0 + 18, 320, L_X1 - 18, 488), 12, DEEP, outline=SURF3)
draw.text((L_X0 + 32, 334), "MeterRing (SPSC Atomic)", font=f_node_title, fill=EMBER)
draw.text((L_X0 + 32, 354), "kCapacity = 4096 floats", font=f_mono_sm, fill=INK2)

# Draw circular ring buffer graphic
cx, cy, rad = (L_X0 + L_X1) // 2, 424, 42
draw.ellipse((cx - rad, cy - rad, cx + rad, cy + rad), outline=SURF3, width=6)
draw.arc((cx - rad, cy - rad, cx + rad, cy + rad), start=210, end=350, fill=EMBER, width=6)
draw.arc((cx - rad, cy - rad, cx + rad, cy + rad), start=20, end=160, fill=SIGNAL, width=6)
draw.text((cx - 24, cy - 12), "mTail", font=f_mono_sm, fill=EMBER)
draw.text((cx - 24, cy + 2), "mHead", font=f_mono_sm, fill=SIGNAL)

# Rolling window card
rrect((L_X0 + 18, 504, L_X1 - 18, 576), 12, SURF1, outline=SURF2)
draw.text((L_X0 + 32, 518), "Fixed Rolling History", font=f_node_title, fill=SIGNAL)
draw.text((L_X0 + 32, 540), "512 / 1024 / 2048 / 4096 samples", font=f_mono_sm, fill=INK2)

# Arrows from Left to Center/Right
arrow_h(L_X1 + 6, 424, 248, SIGNAL, "PCM", label_dy=-15)
arrow_h(L_X1 + 6, 424, 454, EMBER, "FFT", label_dy=-15)

# 2. Center column: 2D GPU Textures (Audio Texture & Audio Color Ramp)
C_X0, C_X1 = 428, 780
rrect((C_X0, 120, C_X1, 594), 16, GROUND, outline=SURF3)
draw.text((C_X0 + 20, 138), "2D GPU TEXTURE NODES", font=f_badge, fill=SIGNAL)
draw.text((C_X0 + 20, 156), "OpenGL GL_RGBA8 & LUTs", font=f_card_hdr, fill=INK)

# Audio Texture card
rrect((C_X0 + 18, 190, C_X1 - 18, 376), 12, SURF1, outline=SURF2)
draw.text((C_X0 + 32, 204), "Audio Texture", font=f_node_title, fill=CYAN)
draw.text((C_X0 + 32, 224), "Waveform (W x H)  |  Spectrum (512x512)", font=f_mono_sm, fill=INK2)

# Waveform + Spectrum mini plot
px0, py0, px1, py1 = C_X0 + 32, 248, C_X1 - 32, 316
rrect((px0, py0, px1, py1), 8, DEEP, outline=SURF3)
pts = []
for i in range(60):
    t = i / 59.0
    x = px0 + 10 + t * (px1 - px0 - 20)
    val = 0.55 * math.sin(t * 14.0) * math.exp(-abs(t - 0.5) * 2.2) + 0.25 * math.cos(t * 31.0)
    y = (py0 + py1) / 2 - val * 22.0
    pts.append((x, y))
draw.line(pts, fill=CYAN, width=2)
draw.text((C_X0 + 32, 326), "1024-pt Hann + Radix2FFT (-60..0 dB)", font=f_mono_sm, fill=SIGNAL)
draw.text((C_X0 + 32, 346), "Log frequency axis: 20 Hz - 20 kHz", font=f_mono_sm, fill=INK3)

# Audio Color Ramp card
rrect((C_X0 + 18, 392, C_X1 - 18, 576), 12, SURF1, outline=SURF2)
draw.text((C_X0 + 32, 406), "Audio Color Ramp", font=f_node_title, fill=EMBER)
draw.text((C_X0 + 32, 426), "Up to 8 log bands -> 256x1 RGB LUT", font=f_mono_sm, fill=INK2)

# 8-band VIBGYOR bar + LUT gradient
band_cols = [
    (242, 38, 38, 255),
    (255, 140, 26, 255),
    (255, 230, 51, 255),
    (51, 242, 102, 255),
    (26, 217, 217, 255),
    (51, 115, 255, 255),
    (115, 51, 242, 255),
    (191, 51, 255, 255),
]
bx0, by0, bx1, by1 = C_X0 + 32, 452, C_X1 - 32, 516
rrect((bx0, by0, bx1, by1), 8, DEEP, outline=SURF3)
bw = (bx1 - bx0 - 20) / 8.0
heights = [0.85, 0.72, 0.92, 0.65, 0.50, 0.42, 0.30, 0.24]
for idx, col in enumerate(band_cols):
    x_a = bx0 + 10 + idx * bw + 2
    x_b = bx0 + 10 + (idx + 1) * bw - 2
    bh = int(heights[idx] * 46)
    draw.rectangle((x_a, by1 - 8 - bh, x_b, by1 - 8), fill=col)

draw.text((C_X0 + 32, 526), "15 ms attack / 120 ms decay ballistics", font=f_mono_sm, fill=SIGNAL)
draw.text((C_X0 + 32, 546), "Standalone gradient or luma-grade shader", font=f_mono_sm, fill=INK3)

# Arrows to Right column
arrow_h(C_X1 + 6, 836, 248, CYAN, "IMG", label_dy=-15)
arrow_h(C_X1 + 6, 836, 454, GREEN, "MESH", label_dy=-15)

# 3. Right column: 3D Geometry Nodes
R_X0, R_X1 = 840, 1152
rrect((R_X0, 120, R_X1, 594), 16, GROUND, outline=SURF3)
draw.text((R_X0 + 20, 138), "3D GEOMETRY NODES", font=f_badge, fill=GREEN)
draw.text((R_X0 + 20, 156), "Render 3D Mesh Sources", font=f_card_hdr, fill=INK)

# Audio Ribbon card
rrect((R_X0 + 18, 190, R_X1 - 18, 336), 12, SURF1, outline=SURF2)
draw.text((R_X0 + 32, 204), "Audio Ribbon", font=f_node_title, fill=GREEN)
draw.text((R_X0 + 32, 226), "4..2048 segment 3D triangle strip", font=f_mono_sm, fill=INK2)
draw.text((R_X0 + 32, 246), "Analytical XY tangents + normals", font=f_mono_sm, fill=INK2)
draw.text((R_X0 + 32, 266), "Full TRS + PBR material outputs", font=f_mono_sm, fill=SIGNAL)
draw.text((R_X0 + 32, 286), "Smoothed waveform heightScale", font=f_mono_sm, fill=INK3)
draw.text((R_X0 + 32, 306), "Input: audio  ->  Output: geo", font=f_mono_sm, fill=CYAN)

# Audio Displacement card
rrect((R_X0 + 18, 352, R_X1 - 18, 576), 12, SURF1, outline=SURF2)
draw.text((R_X0 + 32, 366), "Audio Displacement", font=f_node_title, fill=EMBER)
draw.text((R_X0 + 32, 388), "5 deformation modes on any mesh:", font=f_body, fill=INK2)
modes = [
    ("0", "Normal Waveform"),
    ("1", "Spectral FFT Bands (32 bands)"),
    ("2", "Acoustic Ripples (radial decay)"),
    ("3", "Cymatics (Chladni m, n modes)"),
    ("4", "Directional Axis (X / Y / Z)"),
]
my = 412
for num, name in modes:
    draw.text((R_X0 + 32, my), num, font=f_mono_sm, fill=EMBER)
    draw.text((R_X0 + 50, my), name, font=f_mono_sm, fill=INK)
    my += 21

draw.text((R_X0 + 32, 524), "Subdivide 0..4 + real/offline dt", font=f_mono_sm, fill=SIGNAL)
draw.text((R_X0 + 32, 544), "Inputs: geo + audio -> geo", font=f_mono_sm, fill=GREEN)

# Footer bar
draw.text((48, 616), "INFINITE  //  infinitedevices.org", font=f_mono, fill=INK3)
draw.text((792, 616), "Source: src/audio/MeterRing.h & src/nodes/Audio*.cpp", font=f_mono_sm, fill=INK3)

os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
img.save(OUT_PATH, "PNG")
print("Wrote", OUT_PATH)
