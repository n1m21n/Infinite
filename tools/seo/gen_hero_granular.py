#!/usr/bin/env python3
"""Generates the 1200x675 technical hero diagram for granular-synthesis-modular-audio-sound-design.
Adheres strictly to docs/brand/BRAND.md and brand.json (Midnight deep #0D1020 mode).
"""
import math, os
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
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
font_mono_path = os.path.join(ROOT, "art/brand/fonts/GeistMono-Medium.ttf")
font_sans_path = os.path.join(ROOT, "art/brand/fonts/Geist-SemiBold.ttf")

if not os.path.exists(font_mono_path):
    font_mono_path = "/System/Library/Fonts/Menlo.ttc"
if not os.path.exists(font_sans_path):
    font_sans_path = "/System/Library/Fonts/Helvetica.ttc"

try:
    f_title = ImageFont.truetype(font_sans_path, 19)
    f_sub = ImageFont.truetype(font_mono_path, 11)
    f_card_title = ImageFont.truetype(font_sans_path, 13)
    f_badge = ImageFont.truetype(font_mono_path, 10)
    f_code = ImageFont.truetype(font_mono_path, 10)
    f_label = ImageFont.truetype(font_mono_path, 10)
    f_small = ImageFont.truetype(font_mono_path, 9)
    f_tiny = ImageFont.truetype(font_mono_path, 8)
except Exception:
    f_title = f_sub = f_card_title = f_badge = f_code = f_label = f_small = f_tiny = ImageFont.load_default()

# 1. Background subtle dot grid
for gx in range(24, W, 24):
    for gy in range(24, H, 24):
        draw.point((gx, gy), fill=DOT)

# Outer decorative border
draw.rounded_rectangle([16, 16, W - 16, H - 16], radius=12, outline=LINE, width=1)

# Top Bar
draw.text((42, 34), "INFINITE", font=f_title, fill=INK)
draw.text((142, 39), "//  GRANULAR SYNTHESIS ENGINE: MICRO-SOUND CLOUDS & TIME DISSECTION", font=f_sub, fill=SIGNAL)

badge_text = "REAL-TIME DSP  •  HERMITE 4-POINT CUBIC  •  STOCHASTIC SPRAY  •  METRIC SORTING"
bw = draw.textlength(badge_text, font=f_badge) + 20
draw.rounded_rectangle((W - 42 - bw, 32, W - 42, 58), 6, fill=SURF1, outline=LINE, width=1)
draw.text((W - 42 - bw + 10, 38), badge_text, font=f_badge, fill=INK2)

# Helper function for card
def draw_card(x, y, w, h, title, category, badge=None):
    draw.rounded_rectangle((x, y + 4, x + w, y + h + 4), 8, fill=(4, 5, 14, 160))
    draw.rounded_rectangle((x, y, x + w, y + h), 8, fill=SURF1, outline=LINE, width=1)
    draw.rounded_rectangle((x, y, x + w, y + 30), 8, fill=SURF2)
    draw.rectangle((x, y + 18, x + w, y + 30), fill=SURF2)
    draw.line((x, y + 30, x + w, y + 30), fill=LINE, width=1)
    draw.text((x + 12, y + 8), title, font=f_card_title, fill=INK)
    tw = draw.textlength(title, font=f_card_title)
    draw.text((x + 12 + tw + 10, y + 10), category, font=f_tiny, fill=INK3)
    if badge:
        bbw = draw.textlength(badge, font=f_tiny) + 10
        draw.rounded_rectangle((x + w - 12 - bbw, y + 7, x + w - 12, y + 23), 4, fill=SURF3)
        draw.text((x + w - 12 - bbw + 5, y + 9), badge, font=f_tiny, fill=SIGNAL)

# -----------------------------------------------------------------------------
# Card 1: Sample Buffer & Scan Playhead (Left)
# -----------------------------------------------------------------------------
c1_x, c1_y, c1_w, c1_h = 42, 78, 350, 560
draw_card(c1_x, c1_y, c1_w, c1_h, "Audio Buffer & Read Head", "DSP // MEMORY", "LOCK-FREE")

# Waveform view container
w_box_y = c1_y + 44
w_box_h = 130
draw.rounded_rectangle((c1_x + 12, w_box_y, c1_x + c1_w - 12, w_box_y + w_box_h), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c1_x + 20, w_box_y + 8), "SAMPLE BUFFER // 44.1 kHz STEREO", font=f_tiny, fill=INK3)

# Waveform drawing
wave_w = c1_w - 44
mid_y = w_box_y + 68
wave_pts = []
for i in range(wave_w):
    frac = i / wave_w
    amp = (math.sin(frac * 32.0) * 0.4 + math.sin(frac * 88.0) * 0.25 + math.sin(frac * 7.0) * 0.35)
    amp *= math.sin(frac * math.pi) ** 0.8
    wy = mid_y + amp * 38
    wave_pts.append((c1_x + 22 + i, wy))
draw.line(wave_pts, fill=SURF3, width=1)

# Active trim region
trim_start_x = c1_x + 22 + int(wave_w * 0.15)
trim_end_x = c1_x + 22 + int(wave_w * 0.88)
draw.line((trim_start_x, w_box_y + 24, trim_start_x, w_box_y + w_box_h - 10), fill=CYAN, width=2)
draw.line((trim_end_x, w_box_y + 24, trim_end_x, w_box_y + w_box_h - 10), fill=CYAN, width=2)
draw.text((trim_start_x - 14, w_box_y + w_box_h - 22), "START", font=f_tiny, fill=CYAN)
draw.text((trim_end_x - 8, w_box_y + w_box_h - 22), "END", font=f_tiny, fill=CYAN)

# Playhead line
scan_x = c1_x + 22 + int(wave_w * 0.46)
draw.line((scan_x, w_box_y + 22, scan_x, w_box_y + w_box_h - 8), fill=EMBER, width=2)
draw.polygon([(scan_x - 5, w_box_y + 20), (scan_x + 5, w_box_y + 20), (scan_x, w_box_y + 28)], fill=EMBER)
draw.text((scan_x + 6, w_box_y + 24), "SCAN POS", font=f_tiny, fill=EMBER)

# Spawn spray indicator
spray_l = scan_x - 28
spray_r = scan_x + 28
draw.line((spray_l, mid_y + 28, spray_r, mid_y + 28), fill=GOLD, width=1)
draw.line((spray_l, mid_y + 24, spray_l, mid_y + 32), fill=GOLD, width=1)
draw.line((spray_r, mid_y + 24, spray_r, mid_y + 32), fill=GOLD, width=1)
draw.text((scan_x - 22, mid_y + 34), "RANDOM POS SPRAY", font=f_tiny, fill=GOLD)

# Grain spawn markers along waveform
for gx_off, gy_off, gcol in [(-20, -15, SIGNAL), (-8, 12, GREEN), (10, -8, CYAN), (22, 18, GOLD)]:
    px = scan_x + gx_off
    py = mid_y + gy_off
    draw.ellipse((px - 3, py - 3, px + 3, py + 3), fill=gcol)

# Parameter details
py = w_box_y + w_box_h + 16
draw.text((c1_x + 14, py), "CORE CONTROL PARAMETERS", font=f_small, fill=SIGNAL)
py += 18

params = [
    ("Position", "0.460", "Base buffer scrub offset [0..1]"),
    ("Scan Speed", "+0.25x", "Continuous bi-directional read"),
    ("Grain Length", "65.0 ms", "Window duration [5..1000 ms]"),
    ("Grain Density", "28.0 Hz", "Grains spawned per second"),
    ("Freeze Mode", "ENABLED", "Halts buffer write / scan loop"),
    ("Position Spray", "14.2 %", "Gaussian jitter around playhead"),
    ("Pitch Spray", "+-7 semi", "Per-grain random transposition"),
    ("Stereo Spread", "85 %", "Equal-power stereo distribution"),
    ("Reverse Chance", "20 %", "Probability of backward grain"),
    ("Interpolation", "Hermite", "4-point cubic continuous"),
]

for label, val, desc in params:
    draw.rounded_rectangle((c1_x + 12, py, c1_x + c1_w - 12, py + 26), 4, fill=SURF2, outline=SURF3, width=1)
    draw.text((c1_x + 20, py + 6), label, font=f_label, fill=INK)
    draw.text((c1_x + 120, py + 6), val, font=f_code, fill=CYAN if label == "Position" else (EMBER if "Freeze" in label else GREEN))
    draw.text((c1_x + 185, py + 7), desc, font=f_tiny, fill=INK3)
    py += 31

# -----------------------------------------------------------------------------
# Card 2: Grain Windows & Overlap-Add Engine (Centre)
# -----------------------------------------------------------------------------
c2_x, c2_y, c2_w, c2_h = 410, 78, 380, 560
draw_card(c2_x, c2_y, c2_w, c2_h, "Window Envelopes & Overlap", "DSP // KERNEL", "HERMITE CUBIC")

# Subcard 2A: Window Function Shapes
draw.text((c2_x + 14, c2_y + 44), "WINDOW SHAPE SELECTION", font=f_small, fill=SIGNAL)

win_box_y = c2_y + 64
win_box_w = (c2_w - 36) // 3
win_box_h = 75

windows = [
    ("HANN", "0.5*(1-cos(2pi*t))", lambda t: 0.5 * (1.0 - math.cos(2.0 * math.pi * t)), CYAN),
    ("GAUSSIAN", "exp(-8*(2t-1)^2)", lambda t: math.exp(-8.0 * ((2.0 * t - 1.0) ** 2)), GREEN),
    ("EXP DECAY", "sin(pi*t)*e^(-3t)", lambda t: math.sin(math.pi * t) * math.exp(-3.0 * t) * 2.3, EMBER),
    ("TRIANGLE", "1 - 2*|t-0.5|", lambda t: 1.0 - 2.0 * abs(t - 0.5), GOLD),
    ("TRAPEZOID", "clamp(min ramps)", lambda t: max(0.0, min(1.0, min(t / 0.12, (1.0 - t) / 0.12))), SIGNAL),
    ("SINE", "sin(pi*t)", lambda t: math.sin(math.pi * t), INK),
]

for idx, (wname, wform, wfunc, wcol) in enumerate(windows):
    col_idx = idx % 3
    row_idx = idx // 3
    bx = c2_x + 12 + col_idx * (win_box_w + 6)
    by = win_box_y + row_idx * (win_box_h + 8)

    draw.rounded_rectangle((bx, by, bx + win_box_w, by + win_box_h), 4, fill=DEEP, outline=SURF3, width=1)
    draw.text((bx + 8, by + 6), wname, font=f_tiny, fill=wcol)

    # Plot curve
    pts = []
    plot_w = win_box_w - 16
    for i in range(plot_w):
        t = i / max(1, plot_w - 1)
        val = max(0.0, min(1.0, wfunc(t)))
        py = by + win_box_h - 10 - int(val * (win_box_h - 32))
        pts.append((bx + 8 + i, py))
    draw.line(pts, fill=wcol, width=1)

# Subcard 2B: Overlapping Grain Cloud Stream
gy = win_box_y + 2 * (win_box_h + 8) + 12
draw.text((c2_x + 14, gy), "OVERLAPPING GRAIN CLOUD (28 GRAINS / SEC)", font=f_small, fill=SIGNAL)
gy += 20

cloud_h = 180
draw.rounded_rectangle((c2_x + 12, gy, c2_x + c2_w - 12, gy + cloud_h), 6, fill=DEEP, outline=SURF3, width=1)

# Time axis
draw.line((c2_x + 24, gy + cloud_h - 20, c2_x + c2_w - 24, gy + cloud_h - 20), fill=SURF3, width=1)
draw.text((c2_x + 24, gy + cloud_h - 16), "0.0s", font=f_tiny, fill=INK3)
draw.text((c2_x + (c2_w // 2) - 10, gy + cloud_h - 16), "TIME (t)", font=f_tiny, fill=INK3)
draw.text((c2_x + c2_w - 48, gy + cloud_h - 16), "0.5s", font=f_tiny, fill=INK3)

# Draw simulated active grains with staggered start times and durations
grain_specs = [
    (20, 85, 0.9, CYAN, -3),
    (45, 110, 0.75, GREEN, +2),
    (75, 95, 0.85, GOLD, -5),
    (110, 120, 1.0, EMBER, 0),
    (150, 80, 0.7, SIGNAL, +7),
    (185, 105, 0.95, CYAN, +4),
    (220, 90, 0.8, GREEN, -2),
    (260, 75, 0.65, GOLD, +12),
]

for g_start, g_dur, g_amp, g_col, g_pitch in grain_specs:
    start_px = c2_x + 24 + g_start
    g_pts = []
    for step in range(g_dur):
        if start_px + step > c2_x + c2_w - 24:
            break
        norm_t = step / g_dur
        win_v = 0.5 * (1.0 - math.cos(2.0 * math.pi * norm_t)) * g_amp
        y_pos = gy + cloud_h - 24 - int(win_v * (cloud_h - 55))
        g_pts.append((start_px + step, y_pos))
    if len(g_pts) > 1:
        draw.line(g_pts, fill=g_col, width=1)
        # Small grain label
        mid_pt = g_pts[len(g_pts) // 2]
        pitch_str = f"{g_pitch:+d}st" if g_pitch != 0 else "0st"
        draw.text((mid_pt[0] - 8, mid_pt[1] - 12), pitch_str, font=f_tiny, fill=g_col)

# Gain normalisation math card
norm_y = gy + cloud_h + 14
draw.rounded_rectangle((c2_x + 12, norm_y, c2_x + c2_w - 12, c2_y + c2_h - 14), 6, fill=SURF2, outline=SURF3, width=1)
draw.text((c2_x + 20, norm_y + 10), "ENERGY NORMALISATION (POWER CONSERVING)", font=f_small, fill=INK)
draw.text((c2_x + 20, norm_y + 28), "overlapFactor = max(1.0, totalFrames / spawnInterval)", font=f_code, fill=CYAN)
draw.text((c2_x + 20, norm_y + 44), "gain = 1.0 / sqrt(overlapFactor)   // prevents clipping", font=f_code, fill=GREEN)
draw.text((c2_x + 20, norm_y + 60), "stereoPan = cos(theta)*L + sin(theta)*R  [constant power]", font=f_code, fill=GOLD)

# -----------------------------------------------------------------------------
# Card 3: Grain Molder & Modular Matrix (Right)
# -----------------------------------------------------------------------------
c3_x, c3_y, c3_w, c3_h = 808, 78, 350, 560
draw_card(c3_x, c3_y, c3_w, c3_h, "Grain Molder & Signal Flow", "DSP // GRAPH", "CANVAS")

# Feature sorting visual
draw.text((c3_x + 14, c3_y + 44), "METRIC FEATURE SORTING (GRAIN MOLDER)", font=f_small, fill=SIGNAL)

molder_y = c3_y + 64
draw.rounded_rectangle((c3_x + 12, molder_y, c3_x + c3_w - 12, molder_y + 130), 6, fill=DEEP, outline=SURF3, width=1)

draw.text((c3_x + 20, molder_y + 8), "GRAIN RE-ORDERING // BLEND 0% -> 100%", font=f_tiny, fill=INK3)

# Visual bars representing sorted grains
bar_w = 14
bar_gap = 5
bars_start_x = c3_x + 22
num_bars = 14

# Original vs Sorted comparison
draw.text((bars_start_x, molder_y + 26), "ORIGINAL ORDER (CHRONOLOGICAL)", font=f_tiny, fill=INK2)
for b in range(num_bars):
    bh = 12 + int((math.sin(b * 1.2) * 0.5 + 0.5) * 26)
    bx = bars_start_x + b * (bar_w + bar_gap)
    draw.rounded_rectangle((bx, molder_y + 68 - bh, bx + bar_w, molder_y + 68), 2, fill=SURF3)

draw.text((bars_start_x, molder_y + 76), "SORTED BY LEVEL / RMS ENERGY (RISER)", font=f_tiny, fill=EMBER)
for b in range(num_bars):
    bh = 6 + int((b / (num_bars - 1)) * 34)
    bx = bars_start_x + b * (bar_w + bar_gap)
    draw.rounded_rectangle((bx, molder_y + 122 - bh, bx + bar_w, molder_y + 122), 2, fill=EMBER)

# Sorting modes pills
pill_y = molder_y + 140
modes = [("LEVEL", EMBER), ("BRIGHTNESS", CYAN), ("RANDOM", GOLD)]
px_cur = c3_x + 12
for m_name, m_col in modes:
    pw = draw.textlength(m_name, font=f_tiny) + 16
    draw.rounded_rectangle((px_cur, pill_y, px_cur + pw, pill_y + 20), 4, fill=SURF2, outline=m_col, width=1)
    draw.text((px_cur + 8, pill_y + 5), m_name, font=f_tiny, fill=m_col)
    px_cur += pw + 8

# Modular canvas patch routing flow
flow_y = pill_y + 36
draw.text((c3_x + 14, flow_y), "MODULAR PATCH ROUTING", font=f_small, fill=SIGNAL)
flow_y += 18

node_chain = [
    ("Audio In / Sampler", "Source live buffer or .wav", CYAN, "AudioCable"),
    ("Granular", "Micro-sound cloud engine", EMBER, "Stereo Float"),
    ("Grain Molder", "Energy / brightness sort", GOLD, "Scramble"),
    ("Stereo Delay & Reverb", "Spatial diffuse tail", GREEN, "Wet/Dry"),
    ("AudioTexture / Mesh", "Audio-reactive GPU visual", SIGNAL, "3D Viewport"),
]

for n_title, n_sub, n_col, n_tag in node_chain:
    draw.rounded_rectangle((c3_x + 12, flow_y, c3_x + c3_w - 12, flow_y + 36), 6, fill=SURF2, outline=SURF3, width=1)
    # Status pip
    draw.ellipse((c3_x + 22, flow_y + 14, c3_x + 30, flow_y + 22), fill=n_col)
    draw.text((c3_x + 38, flow_y + 6), n_title, font=f_label, fill=INK)
    draw.text((c3_x + 38, flow_y + 20), n_sub, font=f_tiny, fill=INK3)

    tbw = draw.textlength(n_tag, font=f_tiny) + 10
    draw.rounded_rectangle((c3_x + c3_w - 20 - tbw, flow_y + 10, c3_x + c3_w - 20, flow_y + 26), 4, fill=SURF1)
    draw.text((c3_x + c3_w - 15 - tbw, flow_y + 13), n_tag, font=f_tiny, fill=n_col)

    flow_y += 42
    if n_title != "AudioTexture / Mesh":
        # Cable connector line
        draw.line((c3_x + 26, flow_y - 6, c3_x + 26, flow_y), fill=n_col, width=2)

# Save image
out_path = os.path.join(ROOT, "website", "assets", "blog_granular-synthesis-modular-audio-sound-design.png")
os.makedirs(os.path.dirname(out_path), exist_ok=True)
img.save(out_path, "PNG")
print(f"Hero diagram generated successfully at: {out_path}")
