#!/usr/bin/env python3
"""Generates the 1200x675 technical hero diagram for binaural-hrtf-spatial-audio-mixer-dsp.
Adheres strictly to docs/brand/BRAND.md and brand.json (Midnight deep #0D1020 mode).
"""
import os
from PIL import Image, ImageDraw, ImageFont

W, H = 1200, 675
img = Image.new("RGBA", (W, H), (13, 16, 32, 255))  # Midnight deep #0D1020
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

f_title = ImageFont.truetype(GEIST_SEMIBOLD, 25)
f_sub = ImageFont.truetype(GEIST_MEDIUM, 13)
f_badge = ImageFont.truetype(GEIST_MONO, 11)
f_card_hdr = ImageFont.truetype(GEIST_SEMIBOLD, 13)
f_node_title = ImageFont.truetype(GEIST_SEMIBOLD, 12)
f_body = ImageFont.truetype(GEIST_MEDIUM, 11)
f_mono = ImageFont.truetype(GEIST_MONO, 11)
f_mono_sm = ImageFont.truetype(GEIST_MONO, 10)


def rrect(box, r, fill, outline=None, width=1):
    draw.rounded_rectangle(box, radius=r, fill=fill, outline=outline, width=width)


def arrow_h(x1, x2, y, color, label=None, label_dy=-15, width=2):
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

# Header
draw.text((48, 34), "Binaural HRTF spatial audio & head tracking", font=f_title, fill=INK)
draw.text((48, 68), "12 spatial objects routed through MIT KEMAR convolution or Woodworth ITD, Schroeder room, and BS.1770-4 metering", font=f_sub, fill=INK2)

# Top-right badge
rrect((966, 36, 1152, 64), 14, SURF1, outline=SURF3)
draw.text((984, 43), "DSP · HRTF · BS.1770-4", font=f_badge, fill=SIGNAL)

# 1. Left column: 3D Object Inputs & Head Tracking
L_X0, L_X1 = 48, 356
rrect((L_X0, 112, L_X1, 578), 16, GROUND, outline=SURF3)
draw.text((L_X0 + 20, 130), "SPATIAL OBJECTS (1..12)", font=f_badge, fill=INK3)
draw.text((L_X0 + 20, 148), "Auto-growing audio inputs", font=f_card_hdr, fill=INK)

# Polar coordinate card
rrect((L_X0 + 18, 182, L_X1 - 18, 316), 12, SURF1, outline=SURF2)
draw.text((L_X0 + 32, 196), "Per-object coordinates", font=f_node_title, fill=EMBER)
rows_l = [
    ("azimuth", "0 front, +90 right, ±180 back"),
    ("elevation", "-40° to +90°"),
    ("distance", "0.2 m to 20 m (1/r + air)"),
    ("width", "0..1 (stereo ±30° · w)"),
]
ry = 220
for k, v in rows_l:
    draw.text((L_X0 + 32, ry), k, font=f_mono_sm, fill=SIGNAL)
    draw.text((L_X0 + 106, ry), v, font=f_mono_sm, fill=INK2)
    ry += 21

# Head tracking card
rrect((L_X0 + 18, 332, L_X1 - 18, 456), 12, SURF1, outline=SURF2)
draw.text((L_X0 + 32, 346), "Head Tracking & Anchor", font=f_node_title, fill=CYAN)
draw.text((L_X0 + 32, 368), "Off · AirPods motion · Webcam", font=f_body, fill=INK2)
draw.text((L_X0 + 32, 388), "Room object: az - yaw", font=f_mono_sm, fill=INK2)
draw.text((L_X0 + 32, 408), "headLocked: stays at front", font=f_mono_sm, fill=GREEN)
draw.text((L_X0 + 32, 428), "File capture: always faces front", font=f_mono_sm, fill=INK3)

# Render modes card
rrect((L_X0 + 18, 472, L_X1 - 18, 560), 12, SURF1, outline=SURF2)
draw.text((L_X0 + 32, 486), "Render Mode Switch", font=f_node_title, fill=SIGNAL)
draw.text((L_X0 + 32, 508), "0: Binaural (headphones)", font=f_mono_sm, fill=INK2)
draw.text((L_X0 + 32, 528), "1: Stereo (equal-power pan)", font=f_mono_sm, fill=INK3)

# 2. Centre column: Dual Binaural Renderer
C_X0, C_X1 = 414, 786
rrect((C_X0, 112, C_X1, 578), 16, SURF1, outline=SIGNAL, width=1)
draw.text((C_X0 + 20, 130), "DUAL BINAURAL RENDERER", font=f_badge, fill=SIGNAL)
draw.text((C_X0 + 20, 148), "Zero-allocation per-sample voices", font=f_card_hdr, fill=INK)

# KEMAR HRTF card
rrect((C_X0 + 18, 182, C_X1 - 18, 370), 12, DEEP, outline=SURF3)
draw.text((C_X0 + 32, 196), "Hrtf::Voice  (MIT KEMAR)", font=f_mono, fill=EMBER)
kemar_rows = [
    ("Grid", "14 el (-40..90°) × 72 az (5° step)", SIGNAL),
    ("FIR", "128 taps @ 44.1 kHz (resampled)", INK2),
    ("ITD", "Split onset delay (no phase smear)", GREEN),
    ("Interp", "4-cell bilinear + block crossfade", INK2),
    ("Glide", "8 ms 1-pole on gain, air & delay", INK3),
]
ry = 224
for k, desc, col in kemar_rows:
    rrect((C_X0 + 30, ry, C_X1 - 30, ry + 24), 6, SURF2)
    draw.text((C_X0 + 38, ry + 5), k, font=f_mono_sm, fill=col)
    draw.text((C_X0 + 96, ry + 5), desc, font=f_mono_sm, fill=INK2)
    ry += 28

# Parametric model card
rrect((C_X0 + 18, 386, C_X1 - 18, 560), 12, DEEP, outline=SURF3)
draw.text((C_X0 + 32, 400), "Binaural::Voice  (Spherical Head)", font=f_mono, fill=CYAN)
param_rows = [
    ("Woodworth ITD", "(a/c)(lat + sin lat), a=8.75 cm", SIGNAL),
    ("Head shadow", "1-pole LP (~1.6 kHz @ 90°)", INK2),
    ("Pinna rear", "High-cut for back sources", INK2),
    ("Elevation", "Mild lift cue + 1/r + air LP", INK3),
    ("Wrap-safe", "8 ms glide in derived space", GREEN),
]
ry = 428
for k, desc, col in param_rows:
    rrect((C_X0 + 30, ry, C_X1 - 30, ry + 22), 6, SURF2)
    draw.text((C_X0 + 38, ry + 4), k, font=f_mono_sm, fill=col)
    draw.text((C_X0 + 138, ry + 4), desc, font=f_mono_sm, fill=INK2)
    ry += 25

# 3. Right column: SpatialMaster & Output
R_X0, R_X1 = 844, 1152
rrect((R_X0, 112, R_X1, 578), 16, GROUND, outline=SURF3)
draw.text((R_X0 + 20, 130), "MASTER BUS & EXPORT", font=f_badge, fill=INK3)
draw.text((R_X0 + 20, 148), "SpatialMaster & CaptureRing", font=f_card_hdr, fill=INK)

# Room card
rrect((R_X0 + 18, 182, R_X1 - 18, 286), 12, SURF1, outline=SURF2)
draw.text((R_X0 + 32, 196), "Schroeder / Moorer Room", font=f_node_title, fill=SIGNAL)
draw.text((R_X0 + 32, 218), "6 ER taps (7..41 ms, L/R alt)", font=f_mono_sm, fill=INK2)
draw.text((R_X0 + 32, 238), "4 damped combs/ear + 2 allpass", font=f_mono_sm, fill=INK2)
draw.text((R_X0 + 32, 258), "Constant send -> distance cues", font=f_mono_sm, fill=INK3)

# Bass mono + Limiter card
rrect((R_X0 + 18, 302, R_X1 - 18, 412), 12, SURF1, outline=SURF2)
draw.text((R_X0 + 32, 316), "Bass Mono & True-Peak Limiter", font=f_node_title, fill=EMBER)
draw.text((R_X0 + 32, 338), "bassHz: mono below cutoff", font=f_mono_sm, fill=INK2)
draw.text((R_X0 + 32, 358), "Linked ceiling: -1.0 dBTP", font=f_mono_sm, fill=INK2)
draw.text((R_X0 + 32, 378), "63-sample lookahead (PDC)", font=f_mono_sm, fill=GREEN)

# BS.1770 + Export card
rrect((R_X0 + 18, 428, R_X1 - 18, 560), 12, SURF1, outline=SURF2)
draw.text((R_X0 + 32, 442), "BS.1770-4 & Binaural Export", font=f_node_title, fill=CYAN)
draw.text((R_X0 + 32, 464), "K-weighted 100 ms blocks", font=f_mono_sm, fill=INK2)
draw.text((R_X0 + 32, 484), "400 ms momentary · 3 s short", font=f_mono_sm, fill=INK2)
draw.text((R_X0 + 32, 504), "WAV / FLAC (16 or 24-bit)", font=f_mono_sm, fill=INK2)
draw.text((R_X0 + 32, 526), "Lock-free AudioCaptureRing", font=f_mono_sm, fill=GREEN)

# Arrows connecting columns
arrow_h(L_X1 + 2, C_X0 - 4, 276, SIGNAL, "objects", label_dy=-16)
arrow_h(L_X1 + 2, C_X0 - 4, 468, CYAN, "az - yaw", label_dy=-16)
arrow_h(C_X1 + 2, R_X0 - 4, 276, EMBER, "binaural L/R", label_dy=-16)
arrow_h(C_X1 + 2, R_X0 - 4, 468, GREEN, "room send", label_dy=-16)

# Footer bar
rrect((48, 602, 1152, 642), 10, SURF1, outline=SURF2)
draw.text((68, 615), "INFINITE  ·  SpatialMixerNode (Utility)  ·  AudioSpatialMixerNode  ·  0 mallocs / 0 locks on audio thread", font=f_mono_sm, fill=INK2)
draw.text((972, 615), "infinitedevices.org", font=f_mono_sm, fill=SIGNAL)

out_path = os.path.join(REPO_ROOT, "website", "assets", "blog_binaural-hrtf-spatial-audio-mixer-dsp.png")
os.makedirs(os.path.dirname(out_path), exist_ok=True)
img.convert("RGB").save(out_path, "PNG", optimize=True)
print("Saved:", out_path)
