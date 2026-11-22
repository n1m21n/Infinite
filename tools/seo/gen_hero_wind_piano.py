#!/usr/bin/env python3
"""Generates the 1200x675 technical hero diagram for digital-waveguide-wind-and-piano-physical-modelling.
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

f_title = ImageFont.truetype(GEIST_SEMIBOLD, 25)
f_sub = ImageFont.truetype(GEIST_MEDIUM, 13)
f_badge = ImageFont.truetype(GEIST_MONO, 11)
f_card_hdr = ImageFont.truetype(GEIST_SEMIBOLD, 14)
f_node_title = ImageFont.truetype(GEIST_SEMIBOLD, 12)
f_body = ImageFont.truetype(GEIST_MEDIUM, 11)
f_mono = ImageFont.truetype(GEIST_MONO, 11)
f_mono_sm = ImageFont.truetype(GEIST_MONO, 10)


def rrect(box, r, fill, outline=None, width=1):
    draw.rounded_rectangle(box, radius=r, fill=fill, outline=outline, width=width)


def arrow_v(x, y1, y2, color, label=None, width=2):
    draw.line([(x, y1), (x, y2)], fill=color, width=width)
    d = 1 if y2 > y1 else -1
    draw.polygon([(x, y2), (x - 5, y2 - d * 8), (x + 5, y2 - d * 8)], fill=color)
    if label:
        draw.text((x + 10, (y1 + y2) // 2 - 7), label, font=f_mono_sm, fill=color)


# Subtle dot grid background
for gx in range(24, W, 24):
    for gy in range(24, H, 24):
        draw.ellipse((gx - 1, gy - 1, gx + 1, gy + 1), fill=DOT)

# Header
draw.text((48, 32), "Digital waveguide physical modelling: Wind & Piano", font=f_title, fill=INK)
draw.text((48, 66), "Single-sample acoustic bore and stiff-string feedback loops with lock-free ParamMailbox and 32-sample chunked retuning", font=f_sub, fill=INK2)

# Top-right badge
rrect((952, 34, 1152, 62), 14, SURF1, outline=SURF3)
draw.text((970, 41), "WAVEGUIDE · MODAL · C++", font=f_badge, fill=SIGNAL)

# ---------------- LEFT PANEL: WindNode ----------------
L0, L1 = 48, 584
rrect((L0, 106, L1, 586), 16, GROUND, outline=SURF3)
draw.text((L0 + 22, 124), "WINDNODE (8 VOICES · MONO)", font=f_badge, fill=CYAN)
draw.text((L0 + 22, 144), "Nonlinear reed, jet, and lip bore waveguides", font=f_card_hdr, fill=INK)

# Stage 1: Breath + Tongue
rrect((L0 + 22, 180, L1 - 22, 248), 12, SURF1, outline=SURF2)
draw.text((L0 + 36, 192), "Breath envelope & tongue overshoot", font=f_node_title, fill=EMBER)
draw.text((L0 + 36, 214), "attack (30 ms) + 40 ms tongue transient (kTongueAmount = 0.4)", font=f_mono_sm, fill=INK2)
draw.text((L0 + 36, 229), "5.5 Hz breath vibrato + white noise stream -> pressure p", font=f_mono_sm, fill=INK3)

arrow_v((L0 + L1) // 2, 248, 274, SIGNAL, "p(n)")

# Stage 2: 4 Excitation Tables
rrect((L0 + 22, 274, L1 - 22, 412), 12, SURF1, outline=SURF2)
draw.text((L0 + 36, 286), "Excitation table + bore geometry", font=f_node_title, fill=SIGNAL)

modes = [
    ("Clarinet", "0.5T cyl", "Smith reed table, odd harmonics", CYAN),
    ("Sax", "1.0T cone", "2-part bore (0.4), Scavone 1997", GREEN),
    ("Flute", "1.9965T", "Cubic air jet (0.32) + 20 Hz DC", EMBER),
    ("Brass", "2.0T lip", "2-pole lip resonator (Q=25, 0.975)", SIGNAL),
]
my = 310
for name, loop, desc, col in modes:
    rrect((L0 + 36, my, L0 + 114, my + 20), 6, SURF2, outline=SURF3)
    draw.text((L0 + 44, my + 4), name, font=f_mono_sm, fill=col)
    draw.text((L0 + 124, my + 4), loop, font=f_mono_sm, fill=INK)
    draw.text((L0 + 206, my + 4), desc, font=f_mono_sm, fill=INK2)
    my += 24

arrow_v((L0 + L1) // 2, 412, 438, CYAN, "1-sample loop")

# Stage 3: Bore Delay + Bell Loss
rrect((L0 + 22, 438, L1 - 22, 564), 12, SURF1, outline=SURF2)
draw.text((L0 + 36, 450), "Fractional bore delay & phase-compensated bell", font=f_node_title, fill=GREEN)
draw.text((L0 + 36, 472), "bore[32768] + aux[32768] (linear interp, up to 192 kHz)", font=f_mono_sm, fill=INK2)
draw.text((L0 + 36, 490), "One-pole bell loss (tone) + OnePoleDelay() phase subtraction", font=f_mono_sm, fill=INK2)
draw.text((L0 + 36, 508), "Retune every kChunk = 32 samples (0.7 ms at 48 kHz)", font=f_mono_sm, fill=INK3)
draw.text((L0 + 36, 528), "Output: 60 Hz HPF -> DC block -> kHeadroom (0.35)", font=f_mono_sm, fill=EMBER)

# ---------------- RIGHT PANEL: PianoNode ----------------
R0, R1 = 616, 1152
rrect((R0, 106, R1, 586), 16, GROUND, outline=SURF3)
draw.text((R0 + 22, 124), "PIANONODE (16 VOICES · STEREO)", font=f_badge, fill=EMBER)
draw.text((R0 + 22, 144), "Chaigne felt hammer, stiff strings & 224-mode soundboard", font=f_card_hdr, fill=INK)

# Stage 1: Felt Hammer
rrect((R0 + 22, 180, R1 - 22, 248), 12, SURF1, outline=SURF2)
draw.text((R0 + 36, 192), "Chaigne & Askenfelt (1994) nonlinear felt hammer", font=f_node_title, fill=EMBER)
draw.text((R0 + 36, 214), "K = 8e9, p = 2.5 (+0.012/key), m = 5g at C4, pos = 1/8", font=f_mono_sm, fill=INK2)
draw.text((R0 + 36, 229), "1024-sample force reflection buffer before rebound", font=f_mono_sm, fill=INK3)

arrow_v((R0 + R1) // 2, 248, 274, EMBER, "F_hammer")

# Stage 2: 4 Coupled String Waveguides
rrect((R0 + 22, 274, R1 - 22, 434), 12, SURF1, outline=SURF2)
draw.text((R0 + 36, 286), "4 coupled string waveguides per note (kStrBuf = 8192)", font=f_node_title, fill=SIGNAL)

stages_p = [
    ("Unisons", "1-3 detuned strings + slower 2nd polarisation", CYAN),
    ("LossFilter", "Closed-form 2-point one-pole (25 s T60 at A0)", GREEN),
    ("Dispersion", "4x 1st-order allpass via 24-step golden-section", EMBER),
    ("Inharmonic", "Partials stretched to k*f0*sqrt(1 + B*k^2)", SIGNAL),
    ("Fractional", "Thiran allpass (eta) + undamped treble >= F6", CYAN),
]
py = 310
for tag, desc, col in stages_p:
    rrect((R0 + 36, py, R0 + 124, py + 20), 6, SURF2, outline=SURF3)
    draw.text((R0 + 44, py + 4), tag, font=f_mono_sm, fill=col)
    draw.text((R0 + 134, py + 4), desc, font=f_mono_sm, fill=INK2)
    py += 24

arrow_v((R0 + R1) // 2, 434, 460, GREEN, "string sum")

# Stage 3: DenseBody Soundboard + Electric Models
rrect((R0 + 22, 460, R1 - 22, 564), 12, SURF1, outline=SURF2)
draw.text((R0 + 36, 472), "DenseBody 224-mode soundboard & electric models", font=f_node_title, fill=GREEN)
draw.text((R0 + 36, 494), "224 modes (45 Hz - 6 kHz), eta = 0.02 (T60 ~ 1/f), width 0.7", font=f_mono_sm, fill=INK2)
draw.text((R0 + 36, 512), "Direct admittance share (0.65) + modal resonance (0.45)", font=f_mono_sm, fill=INK2)
draw.text((R0 + 36, 532), "Models: Grand | Rhodes tine | Wurlitzer reed (5.6 Hz tremolo)", font=f_mono_sm, fill=EMBER)

# Footer bar
rrect((48, 604, 1152, 646), 12, GROUND, outline=SURF2)
draw.text((70, 619), "REAL-TIME CONSTRAINTS", font=f_badge, fill=INK3)
draw.text((235, 619), "Zero audio-thread allocations  ·  kChunk = 32 retune  ·  44.1-192 kHz  ·  infinitedevices.org", font=f_mono, fill=INK2)

out_path = os.path.join(REPO_ROOT, "website", "assets", "blog_digital-waveguide-wind-and-piano-physical-modelling.png")
img.convert("RGB").save(out_path, "PNG", optimize=True)
print("Wrote", out_path)
