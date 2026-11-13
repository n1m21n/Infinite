#!/usr/bin/env python3
"""Generate the technical hero diagram for the Spectral Field STFT phase vocoder blog post.
Outputs a crisp 1200x675 brand-compliant image to website/assets/blog_spectral-field-real-time-stft-phase-vocoder-kernels.png.
"""
import math
import os
from PIL import Image, ImageDraw, ImageFont

W, H = 1200, 675
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT_PATH = os.path.join(REPO_ROOT, "website", "assets", "blog_spectral-field-real-time-stft-phase-vocoder-kernels.png")
FONT_DIR = os.path.join(REPO_ROOT, "art", "brand", "fonts")

# Brand Midnight tokens (docs/brand/BRAND.md)
DEEP = (13, 16, 32, 255)       # #0D1020
GROUND = (21, 25, 48, 255)     # #151930
SURFACE1 = (32, 38, 63, 255)   # #20263F
SURFACE2 = (42, 48, 82, 255)   # #2A3052
LINE = (52, 59, 94, 255)       # #343B5E
INK = (238, 241, 250, 255)     # #EEF1FA
INK2 = (163, 170, 203, 255)    # #A3AACB
INK3 = (136, 144, 177, 255)    # #8890B1
SIGNAL = (167, 177, 255, 255)  # #A7B1FF
EMBER = (255, 107, 53, 255)    # #FF6B35

img = Image.new("RGBA", (W, H), DEEP)
draw = ImageDraw.Draw(img)

try:
    f_title = ImageFont.truetype(os.path.join(FONT_DIR, "Geist-SemiBold.ttf"), 27)
    f_sub = ImageFont.truetype(os.path.join(FONT_DIR, "Geist-Medium.ttf"), 13)
    f_badge = ImageFont.truetype(os.path.join(FONT_DIR, "GeistMono-Medium.ttf"), 11)
    f_hdr = ImageFont.truetype(os.path.join(FONT_DIR, "Geist-SemiBold.ttf"), 12)
    f_mono = ImageFont.truetype(os.path.join(FONT_DIR, "GeistMono-Medium.ttf"), 11)
    f_mono_sm = ImageFont.truetype(os.path.join(FONT_DIR, "GeistMono-Medium.ttf"), 10)
except Exception:
    f_title = f_sub = f_badge = f_hdr = f_mono = f_mono_sm = ImageFont.load_default()

# Subtle dot grid
for gx in range(24, W - 24, 28):
    for gy in range(24, H - 24, 28):
        draw.ellipse([gx - 1, gy - 1, gx + 1, gy + 1], fill=(35, 42, 74, 180))

# Outer frame
draw.rounded_rectangle([20, 20, W - 20, H - 20], radius=16, outline=SURFACE2, width=1)

# Header badges
draw.rounded_rectangle([45, 38, 375, 64], radius=6, fill=SURFACE1, outline=LINE, width=1)
draw.text((58, 44), "INFINITE  //  SPECTRAL DSP & FIELD ENGINE", font=f_badge, fill=SIGNAL)

draw.rounded_rectangle([W - 355, 38, W - 45, 64], radius=6, fill=SURFACE1, outline=LINE, width=1)
draw.text((W - 340, 44), "TRACK A: REAL-TIME STFT PHASE VOCODER", font=f_badge, fill=INK)

# Title & subtitle
draw.text((45, 78), "Real-Time STFT Phase Vocoder Kernels in Spectral Field", font=f_title, fill=INK)
draw.text((45, 114), "Zero-phase Hann framing (75% overlap), per-bin register kernels, identity phase locking, and dual-mode synthesis", font=f_sub, fill=INK2)
draw.line([(45, 140), (W - 45, 140)], fill=SURFACE2, width=1)

# Left panel: STFT Analysis & Per-Bin Kernel
lx1, ly1, lx2, ly2 = 45, 156, 585, H - 36
draw.rounded_rectangle([lx1, ly1, lx2, ly2], radius=12, fill=GROUND, outline=SURFACE2, width=1)
draw.text((lx1 + 18, ly1 + 14), "1. STFT ANALYSIS & ZERO-PHASE FRAMING", font=f_hdr, fill=EMBER)

# Window & zero-phase box
wx1, wy1, wx2, wy2 = lx1 + 16, ly1 + 38, lx2 - 16, ly1 + 145
draw.rounded_rectangle([wx1, wy1, wx2, wy2], radius=8, fill=DEEP, outline=LINE, width=1)
draw.text((wx1 + 12, wy1 + 8), "HANN WINDOW (HOP = SIZE / 4)  ->  ROTATE BY SIZE / 2", font=f_mono_sm, fill=SIGNAL)

# Draw Hann curve (left half) and zero-phase rotated curve (right half)
mid_x = (wx1 + wx2) // 2
# Left: standard Hann
pts_hann = []
for i in range(100):
    t = i / 99.0
    x = wx1 + 16 + t * (mid_x - wx1 - 36)
    h_val = 0.5 * (1.0 - math.cos(2.0 * math.pi * t))
    y = wy2 - 16 - h_val * 54.0
    pts_hann.append((x, y))
draw.line(pts_hann, fill=INK2, width=2)
draw.text((wx1 + 16, wy2 - 28), "w[n] Hann", font=f_mono_sm, fill=INK3)

# Arrow between left and right
draw.line([(mid_x - 12, wy1 + 56), (mid_x + 12, wy1 + 56)], fill=EMBER, width=2)
draw.polygon([(mid_x + 12, wy1 + 52), (mid_x + 20, wy1 + 56), (mid_x + 12, wy1 + 60)], fill=EMBER)

# Right: zero-phase rotated (peaks at edges/0)
pts_zp = []
for i in range(100):
    t = i / 99.0
    x = mid_x + 28 + t * (wx2 - mid_x - 44)
    t_rot = (t + 0.5) % 1.0
    h_val = 0.5 * (1.0 - math.cos(2.0 * math.pi * t_rot))
    y = wy2 - 16 - h_val * 54.0
    pts_zp.append((x, y))
draw.line(pts_zp, fill=EMBER, width=2)
draw.text((mid_x + 28, wy2 - 28), "Zero-phase (index 0 peak)", font=f_mono_sm, fill=INK)

# Per-bin spectrum & register kernel box
kx1, ky1, kx2, ky2 = lx1 + 16, ly1 + 158, lx2 - 16, ly2 - 16
draw.rounded_rectangle([kx1, ky1, kx2, ky2], radius=8, fill=DEEP, outline=LINE, width=1)
draw.text((kx1 + 12, ky1 + 10), "2. PER-BIN FIELD KERNEL  (k = 0 .. Size/2)", font=f_hdr, fill=SIGNAL)

# Spectrum bars
bar_y0 = ky1 + 130
num_bars = 36
bar_w = (kx2 - kx1 - 36) / num_bars
peaks = [7, 16, 25]
for b in range(num_bars):
    mag = 0.12
    for pk in peaks:
        dist = abs(b - pk)
        mag += 0.82 * math.exp(-(dist * dist) / 3.5)
    mag = min(1.0, mag)
    bh = int(mag * 82)
    bx = kx1 + 18 + b * bar_w
    col = EMBER if b in peaks else SIGNAL
    draw.rectangle([bx + 1, bar_y0 - bh, bx + bar_w - 2, bar_y0], fill=col)

draw.line([(kx1 + 14, bar_y0 + 1), (kx2 - 14, bar_y0 + 1)], fill=LINE, width=1)
draw.text((kx1 + 14, bar_y0 + 6), "mag[k]   phase[k]   freq[k] (Hz)   binhz = sr / Size", font=f_mono_sm, fill=INK2)

# Code snippet box inside kernel panel
cx1, cy1, cx2, cy2 = kx1 + 14, bar_y0 + 28, kx2 - 14, ky2 - 14
draw.rounded_rectangle([cx1, cy1, cx2, cy2], radius=6, fill=SURFACE1, outline=LINE, width=1)
code_lines = [
    ("param float shift = 200 [-2000, 2000]", INK2),
    ("state float m = 0", SIGNAL),
    ("k = bin - shift / binhz", INK),
    ("mag = mag.at(k)       // fractional bin lookup", EMBER),
    ("freq = freq.at(k) + shift", INK),
]
for idx, (txt, col) in enumerate(code_lines):
    draw.text((cx1 + 12, cy1 + 10 + idx * 19), txt, font=f_mono, fill=col)

# Right panel: Dual Synthesis & Identity Phase Locking
rx1, ry1, rx2, ry2 = 605, 156, W - 45, H - 36
draw.rounded_rectangle([rx1, ry1, rx2, ry2], radius=12, fill=GROUND, outline=SURFACE2, width=1)
draw.text((rx1 + 18, ry1 + 14), "3. DUAL SYNTHESIS & IDENTITY PHASE LOCKING", font=f_hdr, fill=EMBER)

# Vocoder mode box
vx1, vy1, vx2, vy2 = rx1 + 16, ry1 + 38, rx2 - 16, ry1 + 265
draw.rounded_rectangle([vx1, vy1, vx2, vy2], radius=8, fill=DEEP, outline=LINE, width=1)
draw.text((vx1 + 12, vy1 + 10), "VOCODER MODE  (kernel leaves phase untouched)", font=f_hdr, fill=SIGNAL)
draw.text((vx1 + 12, vy1 + 30), "Unchanged freq: keeps analysis phase (exact reconstruction)", font=f_mono_sm, fill=INK2)
draw.text((vx1 + 12, vy1 + 46), "Moved bins:     synthPhase += 2 * pi * outFreq * hop / sr", font=f_mono_sm, fill=INK)

# Identity Phase Locking diagram
ipl_y0 = vy2 - 28
draw.text((vx1 + 12, vy1 + 70), "IDENTITY PHASE LOCKING (LAROCHE & DOLSON 1999)", font=f_mono_sm, fill=EMBER)
# Draw continuous spectral envelope with 2 peaks and valley boundary
env_pts = []
p1, p2 = 0.30, 0.70
for i in range(160):
    t = i / 159.0
    x = vx1 + 18 + t * (vx2 - vx1 - 36)
    v = 0.85 * math.exp(-((t - p1) ** 2) / 0.012) + 0.72 * math.exp(-((t - p2) ** 2) / 0.014) + 0.08
    y = ipl_y0 - v * 92.0
    env_pts.append((x, y))
draw.line(env_pts, fill=SIGNAL, width=2)

# Valley line
vx_val = vx1 + 18 + 0.49 * (vx2 - vx1 - 36)
draw.line([(vx_val, vy1 + 92), (vx_val, ipl_y0)], fill=LINE, width=2)
draw.text((vx_val - 20, ipl_y0 + 6), "valley", font=f_mono_sm, fill=INK3)

# Peak markers
for pt, lbl in [(p1, "Peak 1 -> Region 1"), (p2, "Peak 2 -> Region 2")]:
    px = vx1 + 18 + pt * (vx2 - vx1 - 36)
    py = ipl_y0 - (0.85 if pt == p1 else 0.72) * 92.0 - 8
    draw.ellipse([px - 4, py - 4, px + 4, py + 4], fill=EMBER)
    draw.text((px - 52, py - 18), lbl, font=f_mono_sm, fill=INK)

# Raw phase mode box
px1, py1, px2, py2 = rx1 + 16, ry1 + 278, rx2 - 16, ry1 + 365
draw.rounded_rectangle([px1, py1, px2, py2], radius=8, fill=DEEP, outline=LINE, width=1)
draw.text((px1 + 12, py1 + 10), "RAW PHASE MODE  (kernel assigns phase)", font=f_hdr, fill=SIGNAL)
draw.text((px1 + 12, py1 + 32), "phase = 0                // metallic robot buzz", font=f_mono_sm, fill=INK2)
draw.text((px1 + 12, py1 + 48), "phase = rand() * 2 * pi  // whispered noise", font=f_mono_sm, fill=INK2)
draw.text((px1 + 12, py1 + 64), "phase = B.phase          // cross-synthesis from input B", font=f_mono_sm, fill=EMBER)

# Bottom OLA & Latency box
ox1, oy1, ox2, oy2 = rx1 + 16, ry1 + 378, rx2 - 16, ly2 - 16
draw.rounded_rectangle([ox1, oy1, ox2, oy2], radius=8, fill=SURFACE1, outline=LINE, width=1)
draw.text((ox1 + 14, oy1 + 12), "4. INVERSE FFT + OVERLAP-ADD + DRY DELAY RING", font=f_hdr, fill=INK)
draw.text((ox1 + 14, oy1 + 34), "Hann^2 75% OLA norm: 1 / (Size * 1.5)   |   Size: 512 .. 8192", font=f_mono_sm, fill=SIGNAL)
draw.text((ox1 + 14, oy1 + 52), "Audio-thread safe: zero allocs, lock-free ParamMailbox & mDry ring", font=f_mono_sm, fill=INK2)

os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
img.save(OUT_PATH, "PNG")
print("Wrote", OUT_PATH)
