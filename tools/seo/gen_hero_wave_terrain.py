#!/usr/bin/env python3
"""Generates the 1200x675 technical hero diagram for wave-terrain-synthesis-in-real-time.
Adheres strictly to docs/brand/BRAND.md and brand.json (Midnight deep #0D1020 mode).
"""
import math
import os
import random
from PIL import Image, ImageDraw, ImageFont

W, H = 1200, 675
img = Image.new("RGBA", (W, H), (13, 16, 32, 255))  # Midnight deep #0D1020
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
GREEN = (74, 222, 128, 255)       # #4ADE80
CYAN = (56, 189, 248, 255)        # #38BDF8
GOLD = (251, 191, 36, 255)        # #FBBF24

# Fonts
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
    f_node_title = ImageFont.truetype(GEIST_SEMIBOLD, 13)
    f_node_cat = ImageFont.truetype(GEIST_MONO, 10)
    f_body = ImageFont.truetype(GEIST_MEDIUM, 11)
    f_mono = ImageFont.truetype(GEIST_MONO, 11)
    f_mono_sm = ImageFont.truetype(GEIST_MONO, 9)
    f_formula = ImageFont.truetype(GEIST_MONO, 10)
except Exception:
    f_title = f_sub = f_badge = f_card_hdr = f_node_title = f_node_cat = f_body = f_mono = f_mono_sm = f_formula = ImageFont.load_default()

# 1. Subtle background dot grid
for gx in range(24, W - 24, 28):
    for gy in range(24, H - 24, 28):
        draw.ellipse([gx - 1, gy - 1, gx + 1, gy + 1], fill=DOT)

# 2. Outer decorative frame
draw.rounded_rectangle([20, 20, W - 20, H - 20], radius=16, outline=SURF2, width=1)

# 3. Header section
# Top pill badge: INFINITE // AUDIO-VISUAL SYNTHESIS & DSP
draw.rounded_rectangle([45, 40, 375, 66], radius=6, fill=SURF1, outline=SURF3, width=1)
draw.text((58, 46), "INFINITE  //  AUDIO-VISUAL SYNTHESIS & DSP", font=f_badge, fill=SIGNAL)

# Right pill: TRACK B: CREATIVE CODING & SOUND DESIGN
draw.rounded_rectangle([W - 350, 40, W - 45, 66], radius=6, fill=SURF1, outline=SURF3, width=1)
draw.text((W - 336, 46), "TRACK B: CREATIVE CODING & SOUND DESIGN", font=f_badge, fill=INK)

# Main title and subtitle
draw.text((45, 80), "Wave Terrain Synthesis: Audio from 3D Surfaces", font=f_title, fill=INK)
draw.text((45, 116), "2D orbital trajectories, discrete Fourier transform anti-aliasing, and modular sound design", font=f_sub, fill=INK2)

# Horizontal rule below header
draw.line([(45, 142), (W - 45, 142)], fill=SURF2, width=1)

# 4. Left Pane: 3D Topographic Surface & Orbital Trajectories (Width ~530)
lx1, ly1, lx2, ly2 = 45, 158, 575, H - 35
draw.rounded_rectangle([lx1, ly1, lx2, ly2], radius=12, fill=GROUND, outline=SURF2, width=1)
draw.text((lx1 + 20, ly1 + 14), "3D TOPOGRAPHIC SURFACE & CLOSED ORBITS", font=f_card_hdr, fill=EMBER)

# 3D Viewport Simulation Box
sx1, sy1, sx2, sy2 = lx1 + 16, ly1 + 36, lx2 - 16, ly1 + 320
draw.rounded_rectangle([sx1, sy1, sx2, sy2], radius=8, fill=(10, 13, 26, 255), outline=DOT, width=1)

# Coordinate transform for isometric / perspective projection
# u, v in [-1, 1], height z in [-1, 1]
cx_proj = (sx1 + sx2) / 2.0
cy_proj = (sy1 + sy2) / 2.0 + 35.0

def project(u, v, z):
    # Isometric slant with perspective elevation
    iso_x = (u - v) * 140.0 * 0.866
    iso_y = (u + v) * 75.0 * 0.5 - z * 55.0
    return cx_proj + iso_x, cy_proj + iso_y

# Mathematical terrain: z = T(u, v) = 0.55 * cos(pi * u) * sin(pi * v) + 0.35 * sin(2*pi*(u + v))
def terrain_fn(u, v):
    return 0.55 * math.cos(math.pi * u * 1.8) * math.sin(math.pi * v * 1.8) + 0.30 * math.sin(2.5 * (u * u + v * v))

# Draw wireframe terrain grid lines
grid_steps = 18
pts_grid = []
for i in range(grid_steps + 1):
    u = -1.0 + 2.0 * (i / grid_steps)
    row = []
    for j in range(grid_steps + 1):
        v = -1.0 + 2.0 * (j / grid_steps)
        z = terrain_fn(u, v)
        px, py = project(u, v, z)
        row.append((px, py, z))
    pts_grid.append(row)

# Draw terrain lines in v direction
for i in range(grid_steps + 1):
    for j in range(grid_steps):
        p1 = (pts_grid[i][j][0], pts_grid[i][j][1])
        p2 = (pts_grid[i][j+1][0], pts_grid[i][j+1][1])
        draw.line([p1, p2], fill=(40, 52, 90, 130), width=1)

# Draw terrain lines in u direction
for j in range(grid_steps + 1):
    for i in range(grid_steps):
        p1 = (pts_grid[i][j][0], pts_grid[i][j][1])
        p2 = (pts_grid[i+1][j][0], pts_grid[i+1][j][1])
        draw.line([p1, p2], fill=(45, 60, 105, 140), width=1)

# Superimpose Closed Trajectory 1: Lissajous 3:2 Orbit in Signal Blue
liss_pts = []
for s in range(160):
    t = s / 159.0 * 2.0 * math.pi
    u = 0.65 * math.sin(3.0 * t + 0.4)
    v = 0.65 * math.sin(2.0 * t)
    z = terrain_fn(u, v)
    px, py = project(u, v, z)
    liss_pts.append((px, py))

for s in range(len(liss_pts) - 1):
    draw.line([liss_pts[s], liss_pts[s+1]], fill=(167, 177, 255, 230), width=2)

# Superimpose Closed Trajectory 2: Lemniscate of Bernoulli (Infinity) in Ember
lemn_pts = []
for s in range(160):
    t = s / 159.0 * 2.0 * math.pi
    denom = 1.0 + math.sin(t) ** 2
    u = 0.75 * math.cos(t) / denom
    v = 0.75 * math.sin(t) * math.cos(t) / denom
    z = terrain_fn(u, v)
    px, py = project(u, v, z)
    lemn_pts.append((px, py))

for s in range(len(lemn_pts) - 1):
    draw.line([lemn_pts[s], lemn_pts[s+1]], fill=(255, 107, 53, 240), width=2)

# Highlight active phase sampling point along Lemniscate orbit
sample_idx = 48
spx, spy = lemn_pts[sample_idx]
draw.ellipse([spx - 6, spy - 6, spx + 6, spy + 6], fill=(255, 107, 53, 255), outline=INK, width=2)
# Draw height projection indicator
base_px, base_py = project(0.75 * math.cos(sample_idx / 159.0 * 2.0 * math.pi) / (1.0 + math.sin(sample_idx / 159.0 * 2.0 * math.pi) ** 2),
                           0.75 * math.sin(sample_idx / 159.0 * 2.0 * math.pi) * math.cos(sample_idx / 159.0 * 2.0 * math.pi) / (1.0 + math.sin(sample_idx / 159.0 * 2.0 * math.pi) ** 2),
                           -1.0)
draw.line([(spx, spy), (base_px, base_py)], fill=(255, 107, 53, 140), width=1)

# Legend inside 3D viewport
draw.rounded_rectangle([sx1 + 12, sy1 + 10, sx1 + 220, sy1 + 54], radius=4, fill=(18, 22, 42, 220), outline=SURF2, width=1)
draw.line([(sx1 + 20, sy1 + 22), (sx1 + 38, sy1 + 22)], fill=EMBER, width=2)
draw.text((sx1 + 46, sy1 + 17), "Lemniscate (kOrbitLemniscate)", font=f_mono_sm, fill=INK)
draw.line([(sx1 + 20, sy1 + 38), (sx1 + 38, sy1 + 38)], fill=SIGNAL, width=2)
draw.text((sx1 + 46, sy1 + 33), "Lissajous 3:2 (kOrbitLissajous)", font=f_mono_sm, fill=INK2)

# Mathematical formula card under the 3D box
fx1, fy1, fx2, fy2 = lx1 + 16, ly1 + 332, lx2 - 16, ly2 - 16
draw.rounded_rectangle([fx1, fy1, fx2, fy2], radius=6, fill=SURF1, outline=SURF3, width=1)
draw.text((fx1 + 14, fy1 + 10), "PHYSICAL EQUATIONS & KINEMATIC SCANNING", font=f_badge, fill=CYAN)
draw.text((fx1 + 14, fy1 + 30), "Continuous signal:  s(t) = T(u(t), v(t))  where  z = T(u, v) ∈ [-1, +1]", font=f_formula, fill=INK)
draw.text((fx1 + 14, fy1 + 48), "Lemniscate orbit:   u(t) = cx + rx·cos(ωt) / (1 + sin²(ωt))", font=f_formula, fill=INK2)
draw.text((fx1 + 14, fy1 + 66), "                    v(t) = cy + ry·sin(ωt)·cos(ωt) / (1 + sin²(ωt))", font=f_formula, fill=INK2)
draw.text((fx1 + 14, fy1 + 86), "Bilinear raster:    T(u, v) = (1-tx)(1-ty)c00 + tx(1-ty)c10 + ...", font=f_formula, fill=INK3)
draw.text((fx1 + 14, fy1 + 106), "Frequency scaling:  ω = 2π · f_note · (1 + detune_unison)", font=f_formula, fill=SIGNAL)


# 5. Right Pane: Modular Signal Flow & DSP Pipeline (Width ~530)
rx1, ry1, rx2, ry2 = 600, 158, W - 45, H - 35
draw.rounded_rectangle([rx1, ry1, rx2, ry2], radius=12, fill=GROUND, outline=SURF2, width=1)
draw.text((rx1 + 20, ry1 + 14), "MODULAR SIGNAL FLOW & RADIX-2 FFT ANTI-ALIASING", font=f_card_hdr, fill=SIGNAL)

# Node 1: Texture Input Node (e.g. Reaction-Diffusion / Noise)
n1_x1, n1_y1, n1_x2, n1_y2 = rx1 + 16, ry1 + 36, rx1 + 240, ry1 + 130
draw.rounded_rectangle([n1_x1, n1_y1, n1_x2, n1_y2], radius=8, fill=SURF1, outline=SURF3, width=1)
draw.text((n1_x1 + 12, n1_y1 + 8), "Source / Picture", font=f_node_cat, fill=INK3)
draw.text((n1_x1 + 12, n1_y1 + 24), "Reaction-Diffusion", font=f_node_title, fill=INK)
draw.text((n1_x1 + 12, n1_y1 + 46), "Feed: 0.054  Kill: 0.062", font=f_mono_sm, fill=INK2)
draw.text((n1_x1 + 12, n1_y1 + 62), "Morph rate: 0.25 Hz", font=f_mono_sm, fill=INK2)
draw.text((n1_x1 + 12, n1_y1 + 78), "Texture: 1024x1024 RGBA", font=f_mono_sm, fill=CYAN)
# Pin out
draw.rectangle([n1_x2 - 10, n1_y1 + 46, n1_x2, n1_y1 + 56], fill=CYAN)

# Node 2: Modulator (LFO / Orbit Drift)
n2_x1, n2_y1, n2_x2, n2_y2 = rx1 + 16, ry1 + 146, rx1 + 240, ry1 + 236
draw.rounded_rectangle([n2_x1, n2_y1, n2_x2, n2_y2], radius=8, fill=SURF1, outline=SURF3, width=1)
draw.text((n2_x1 + 12, n2_y1 + 8), "Modulator", font=f_node_cat, fill=INK3)
draw.text((n2_x1 + 12, n2_y1 + 24), "LFO (Multi-wave)", font=f_node_title, fill=INK)
draw.text((n2_x1 + 12, n2_y1 + 46), "Shape: Sine  Rate: 0.12 Hz", font=f_mono_sm, fill=INK2)
draw.text((n2_x1 + 12, n2_y1 + 62), "Target: wtOrbit / rotation", font=f_mono_sm, fill=GOLD)
# Pin out
draw.rectangle([n2_x2 - 10, n2_y1 + 46, n2_x2, n2_y2 - 34], fill=GOLD)

# Node 3: Wave Terrain Node (Central Synthesis Engine)
n3_x1, n3_y1, n3_x2, n3_y2 = rx1 + 276, ry1 + 36, rx2 - 16, ry1 + 250
draw.rounded_rectangle([n3_x1, n3_y1, n3_x2, n3_y2], radius=8, fill=SURF1, outline=(105, 146, 246, 255), width=2)
# Category header badge
draw.rounded_rectangle([n3_x1 + 12, n3_y1 + 8, n3_x1 + 80, n3_y1 + 22], radius=3, fill=(105, 146, 246, 60))
draw.text((n3_x1 + 16, n3_y1 + 9), "SYNTHS", font=f_mono_sm, fill=SIGNAL)
draw.text((n3_x1 + 92, n3_y1 + 8), "Wave Terrain", font=f_node_title, fill=INK)

# Pins in
draw.rectangle([n3_x1, n3_y1 + 46, n3_x1 + 10, n3_y1 + 56], fill=CYAN)
draw.text((n3_x1 + 14, n3_y1 + 46), "texture", font=f_mono_sm, fill=CYAN)
draw.rectangle([n3_x1, n3_y1 + 72, n3_x1 + 10, n3_y1 + 82], fill=GREEN)
draw.text((n3_x1 + 14, n3_y1 + 72), "note in", font=f_mono_sm, fill=GREEN)
draw.rectangle([n3_x1, n3_y1 + 98, n3_x1 + 10, n3_y1 + 108], fill=GOLD)
draw.text((n3_x1 + 14, n3_y1 + 98), "rot mod", font=f_mono_sm, fill=GOLD)

# Inner specs list
draw.text((n3_x1 + 14, n3_y1 + 120), "• wtOrbit: Lemniscate", font=f_mono_sm, fill=INK2)
draw.text((n3_x1 + 14, n3_y1 + 136), "• wtChan: Luminance (0.299R+0.587G+0.114B)", font=f_mono_sm, fill=INK2)
draw.text((n3_x1 + 14, n3_y1 + 152), "• 1024-Point Radix-2 FFT Engine", font=f_mono_sm, fill=CYAN)
draw.text((n3_x1 + 14, n3_y1 + 168), "• 10 Mip Levels (Nyquist Brickwall)", font=f_mono_sm, fill=GREEN)
draw.text((n3_x1 + 14, n3_y1 + 184), "• Polyphony: 8 Voices, 8x Unison Detune", font=f_mono_sm, fill=INK2)
draw.text((n3_x1 + 14, n3_y1 + 200), "• ADSR Amp Envelope + 24dB LP Filter", font=f_mono_sm, fill=INK2)

# Pin out
draw.rectangle([n3_x2 - 10, n3_y1 + 100, n3_x2, n3_y1 + 110], fill=EMBER)
draw.text((n3_x2 - 38, n3_y1 + 100), "audio", font=f_mono_sm, fill=EMBER)

# Cables between nodes
# Cable 1: Texture -> Wave Terrain
draw.line([(n1_x2, n1_y1 + 51), (n3_x1, n3_y1 + 51)], fill=(56, 189, 248, 200), width=2)
# Cable 2: Modulator -> Wave Terrain rot mod
draw.line([(n2_x2, n2_y1 + 51), (n2_x2 + 18, n2_y1 + 51), (n2_x2 + 18, n3_y1 + 103), (n3_x1, n3_y1 + 103)], fill=(251, 191, 36, 200), width=2)

# FFT Mip Pyramid & Anti-Aliasing Spectrum Card
fft_x1, fft_y1, fft_x2, fft_y2 = rx1 + 16, ry1 + 264, rx2 - 16, ly2 - 16
draw.rounded_rectangle([fft_x1, fft_y1, fft_x2, fft_y2], radius=8, fill=SURF1, outline=SURF3, width=1)
draw.text((fft_x1 + 14, fft_y1 + 10), "10-LEVEL DISCRETE FOURIER TRANSFORM MIP PYRAMID", font=f_badge, fill=GREEN)
draw.text((fft_x1 + 14, fft_y1 + 28), "Cooley-Tukey Radix-2 FFT band-limits harmonics per octave to prevent Nyquist foldover distortion.", font=f_mono_sm, fill=INK2)

# Draw Mip pyramid bars
bar_x = fft_x1 + 14
bar_y = fft_y1 + 52
mip_widths = [190, 160, 135, 110, 85, 65, 45, 30, 18, 10]
for idx, mw in enumerate(mip_widths):
    col = (74, 222, 128, 255 - idx * 20)
    draw.rectangle([bar_x, bar_y + idx * 7, bar_x + mw, bar_y + idx * 7 + 4], fill=col)

draw.text((bar_x + 205, bar_y + 4), "Mip 0: Full 512 Harmonics (Low Bass)", font=f_mono_sm, fill=INK)
draw.text((bar_x + 205, bar_y + 24), "Mip 4: 32 Harmonics (Mid Register)", font=f_mono_sm, fill=INK2)
draw.text((bar_x + 205, bar_y + 44), "Mip 9: 1 Harmonic / Pure Sine (Top Octave)", font=f_mono_sm, fill=CYAN)
draw.text((bar_x + 205, bar_y + 64), "Zero Nyquist Aliasing: < -80 dB Attenuation", font=f_mono_sm, fill=GREEN)

# Save to destination
out_path = os.path.join(REPO_ROOT, "website", "assets", "blog_wave-terrain-synthesis-in-real-time.png")
os.makedirs(os.path.dirname(out_path), exist_ok=True)
img.save(out_path, "PNG", optimize=True)
print(f"Saved hero diagram: {out_path} ({os.path.getsize(out_path)} bytes)")
