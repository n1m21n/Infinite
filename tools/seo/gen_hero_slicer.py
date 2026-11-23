#!/usr/bin/env python3
"""Generates the 1200x675 technical hero diagram for superflux-transient-slicing-and-beat-chopping-dsp.
Adheres strictly to docs/brand/BRAND.md and brand.json (Midnight deep #0D1020 mode).
"""
import math
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
draw.text((48, 32), "SuperFlux transient slicing & position-domain beat chopping", font=f_title, fill=INK)
draw.text((48, 66), "Centred 1024-pt STFT, 3-bin reference max-filter, Dixon adaptive peak picker, and lock-free slice confinement", font=f_sub, fill=INK2)

# Top-right badge
rrect((958, 34, 1152, 62), 14, SURF1, outline=SURF3)
draw.text((976, 41), "SUPERFLUX · DSP · C++", font=f_badge, fill=SIGNAL)

# ---------------- LEFT PANEL: SlicerDsp::Detect ----------------
L0, L1 = 48, 584
rrect((L0, 106, L1, 586), 16, GROUND, outline=SURF3)
draw.text((L0 + 22, 124), "SLICERDSP::DETECT (WORKER THREAD)", font=f_badge, fill=CYAN)
draw.text((L0 + 22, 144), "Vibrato-tolerant spectral flux onset detection", font=f_card_hdr, fill=INK)

# Stage 1: Centred STFT
rrect((L0 + 22, 180, L1 - 22, 266), 12, SURF1, outline=SURF2)
draw.text((L0 + 36, 192), "1. Centred Hann STFT (PortableFft::RealFft)", font=f_node_title, fill=SIGNAL)
draw.text((L0 + 36, 214), "fftSize = 1024   |   hop = round(256 * sr / 44100) (~172 Hz ODF)", font=f_mono_sm, fill=INK2)
draw.text((L0 + 36, 231), "Frame n centred on n * hop (Portnoff/Dolson): prevents fftSize/2", font=f_mono_sm, fill=INK2)
draw.text((L0 + 36, 246), "early-onset bias from landing in silence ahead of the attack", font=f_mono_sm, fill=INK3)

arrow_v((L0 + L1) // 2, 266, 294, SIGNAL, "|X(n, k)|")

# Stage 2: SuperFlux 3-bin max-filter
rrect((L0 + 22, 294, L1 - 22, 404), 12, SURF1, outline=SURF2)
draw.text((L0 + 36, 306), "2. Log compression & SuperFlux 3-bin reference filter", font=f_node_title, fill=CYAN)
draw.text((L0 + 36, 328), "Y(n, k) = log(1 + 1000 * |X(n, k)|)   (Dixon, DAFx-06)", font=f_mono_sm, fill=INK2)
draw.text((L0 + 36, 345), "M(n-1, k) = max(Y(n-1, k-1), Y(n-1, k), Y(n-1, k+1))", font=f_mono_sm, fill=INK)
draw.text((L0 + 36, 362), "ODF(n) = sum_k max(0, Y(n, k) - M(n-1, k))   (Boeck, DAFx-13)", font=f_mono_sm, fill=INK2)
draw.text((L0 + 36, 381), "Absorbs vibrato and pitch glides while preserving sharp attacks", font=f_mono_sm, fill=INK3)

arrow_v((L0 + L1) // 2, 404, 432, CYAN, "ODF(n)")

# Stage 3: Adaptive peak picker & pruning
rrect((L0 + 22, 432, L1 - 22, 564), 12, SURF1, outline=SURF2)
draw.text((L0 + 36, 444), "3. Adaptive peak picker & top-N main-thread prune", font=f_node_title, fill=GREEN)

rules = [
    ("Peak window", "kPeakW = 3 (~17 ms), kPeakMultiplier = 3 mean", SIGNAL),
    ("Gates", "silenceGateDb = -70 dB, minIoiSeconds = 28 ms", INK2),
    ("Frame 0 anchor", "kForcedOnsetStrength = 1e9f (never pruned)", EMBER),
    ("Grid bypass", "sliceBy = grid derives from Transport tempo", GREEN),
]
ry = 468
for tag, desc, col in rules:
    rrect((L0 + 36, ry, L0 + 148, ry + 20), 6, SURF2, outline=SURF3)
    draw.text((L0 + 44, ry + 4), tag, font=f_mono_sm, fill=col)
    draw.text((L0 + 158, ry + 4), desc, font=f_mono_sm, fill=INK2)
    ry += 23

# ---------------- RIGHT PANEL: SlicerNode & AudioSlicerNode ----------------
R0, R1 = 616, 1152
rrect((R0, 106, R1, 586), 16, GROUND, outline=SURF3)
draw.text((R0 + 22, 124), "AUDIOSLICERNODE (64 SLICES · C1 UPWARDS)", font=f_badge, fill=EMBER)
draw.text((R0 + 22, 144), "Position-domain slice confinement & triple-buffered UI", font=f_card_hdr, fill=INK)

# Waveform + Slice markers visualization
wf_x0, wf_y0, wf_x1, wf_y1 = R0 + 22, 180, R1 - 22, 312
rrect((wf_x0, wf_y0, wf_x1, wf_y1), 12, DEEP, outline=SURF2)
draw.text((wf_x0 + 14, wf_y0 + 10), "Waveform cache (256 buckets) & slice markers (MIDI 36..99)", font=f_mono_sm, fill=INK3)

# Draw synthetic transient waveform inside wf box
mid_y = (wf_y0 + wf_y1) // 2 + 8
wf_w = wf_x1 - wf_x0 - 28
slice_fracs = [0.0, 0.16, 0.34, 0.50, 0.72, 0.86]
for px in range(wf_w):
    t = px / wf_w
    # envelope bursts at each slice start
    env = 0.08
    for sf in slice_fracs:
        if t >= sf:
            dt = t - sf
            env = max(env, 0.92 * math.exp(-dt * 14.0))
    amp = env * (0.55 + 0.45 * abs(math.sin(px * 0.47) * math.cos(px * 0.19)))
    h = max(2, int(amp * 36))
    x = wf_x0 + 14 + px
    draw.line([(x, mid_y - h), (x, mid_y + h)], fill=SURF3 if t > 0.50 and t < 0.72 else SIGNAL, width=1)

# Draw slice markers and labels
for idx, sf in enumerate(slice_fracs):
    mx = wf_x0 + 14 + int(sf * wf_w)
    col = EMBER if idx == 2 else CYAN
    draw.line([(mx, wf_y0 + 30), (mx, wf_y1 - 10)], fill=col, width=2 if idx == 2 else 1)
    note_num = 36 + idx
    draw.text((mx + 4, wf_y0 + 30), f"S{idx} ({note_num})", font=f_mono_sm, fill=col)

# Active playhead inside Slice 2
ph_x = wf_x0 + 14 + int(0.455 * wf_w)
draw.line([(ph_x, wf_y0 + 30), (ph_x, wf_y1 - 10)], fill=EMBER, width=2)
draw.polygon([(ph_x, wf_y0 + 28), (ph_x - 4, wf_y0 + 21), (ph_x + 4, wf_y0 + 21)], fill=EMBER)

# Stage 2: Orthogonal Boundary vs Envelope
rrect((R0 + 22, 330, R1 - 22, 464), 12, SURF1, outline=SURF2)
draw.text((R0 + 36, 342), "Orthogonal boundary vs envelope (no conflation)", font=f_node_title, fill=SIGNAL)
draw.text((R0 + 36, 365), "crossthrough = off: stops at next onset with 3 ms raised-cosine", font=f_mono_sm, fill=INK)
draw.text((R0 + 36, 381), "crossthrough = on:  runs past boundary through rest of sample", font=f_mono_sm, fill=INK2)
draw.text((R0 + 36, 402), "decay (ms / hold):  shapes amplitude only, independent of stop", font=f_mono_sm, fill=INK)
draw.text((R0 + 36, 422), "Position-domain confinement: read-head checks next onset in", font=f_mono_sm, fill=EMBER)
draw.text((R0 + 36, 438), "samples, so speed (0.25x..4.0x) never bleeds into slice n+1", font=f_mono_sm, fill=INK2)

# Stage 3: Lock-free Triple Buffer & Graph
rrect((R0 + 22, 480, R1 - 22, 564), 12, SURF1, outline=SURF2)
draw.text((R0 + 36, 492), "Lock-free UI feedback & audiovisual patching", font=f_node_title, fill=GREEN)
draw.text((R0 + 36, 514), "SlicerVoiceSnapshot: 8 visual playheads via triple buffer", font=f_mono_sm, fill=INK2)
draw.text((R0 + 36, 531), "Patchable with Euclidean, Arpeggiator, Spatial Mixer, Audio Texture", font=f_mono_sm, fill=INK2)

# Bottom summary bar
rrect((48, 604, 1152, 646), 12, GROUND, outline=SURF3)
draw.text((70, 619), "INFINITE AUDIO DSP", font=f_badge, fill=EMBER)
draw.text((224, 619), "SlicerNode (src/nodes/SlicerNode.h) + SlicerDsp (src/audio/dsp/SlicerDsp.h)   |   MIT Open Source", font=f_mono, fill=INK2)

out_path = os.path.join(REPO_ROOT, "website", "assets", "blog_superflux-transient-slicing-and-beat-chopping-dsp.png")
img.convert("RGB").save(out_path, "PNG", optimize=True)
print("Wrote:", out_path)
