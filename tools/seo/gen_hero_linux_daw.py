#!/usr/bin/env python3
"""Generates the 1200x675 technical hero diagram for linux-daw-modular-audio-and-generative-visuals.
Adheres strictly to docs/brand/BRAND.md and brand.json (Midnight deep #0D1020 mode).
"""
import math, os
from PIL import Image, ImageDraw, ImageFont, ImageFilter

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

f_title = ImageFont.truetype(font_sans_path, 18)
f_sub = ImageFont.truetype(font_mono_path, 11)
f_card_title = ImageFont.truetype(font_sans_path, 13)
f_badge = ImageFont.truetype(font_mono_path, 10)
f_code = ImageFont.truetype(font_mono_path, 10)
f_label = ImageFont.truetype(font_mono_path, 10)
f_small = ImageFont.truetype(font_mono_path, 9)
f_tiny = ImageFont.truetype(font_mono_path, 8)

# 1. Background subtle dot grid
for gx in range(24, W, 24):
    for gy in range(24, H, 24):
        draw.point((gx, gy), fill=DOT)

# Outer decorative frame
draw.rounded_rectangle([16, 16, W - 16, H - 16], radius=10, outline=LINE, width=1)

# Top Bar
draw.text((40, 32), "INFINITE", font=f_title, fill=INK)
draw.text((135, 36), "//  LINUX DAW: MODULAR AUDIO, VST3 HOSTING & LIVE SHADERS", font=f_sub, fill=SIGNAL)

badge_text = "PIPEWIRE & ALSA  •  NATIVE LINUX VST3  •  SAMPLE-ACCURATE GRAPH  •  GLSL SHADERS"
bw = draw.textlength(badge_text, font=f_badge) + 20
draw.rounded_rectangle((W - 40 - bw, 28, W - 40, 52), 5, fill=SURF1, outline=LINE, width=1)
draw.text((W - 40 - bw + 10, 34), badge_text, font=f_badge, fill=INK2)

# Helper function for card
def draw_card(x, y, w, h, title, category, badge=None):
    draw.rounded_rectangle((x, y + 4, x + w, y + h + 4), 8, fill=(4, 5, 14, 160))
    draw.rounded_rectangle((x, y, x + w, y + h), 8, fill=SURF1, outline=LINE, width=1)
    draw.rounded_rectangle((x, y, x + w, y + 32), 8, fill=SURF2)
    draw.rectangle((x, y + 20, x + w, y + 32), fill=SURF2)
    draw.line((x, y + 32, x + w, y + 32), fill=LINE, width=1)
    draw.text((x + 12, y + 8), title, font=f_card_title, fill=INK)
    tw = draw.textlength(title, font=f_card_title)
    draw.text((x + 12 + tw + 8, y + 10), category, font=f_small, fill=INK3)
    if badge:
        bbw = draw.textlength(badge, font=f_tiny) + 10
        draw.rounded_rectangle((x + w - 10 - bbw, y + 7, x + w - 10, y + 25), 4, fill=SURF3)
        draw.text((x + w - 10 - bbw + 5, y + 9), badge, font=f_tiny, fill=SIGNAL)

# -------------------------------------------------------------
# COLUMN 1: Linux Audio Backend & Generative Sequencer (x=40, w=250)
# -------------------------------------------------------------
c1_x, c1_w = 40, 250

# Card 1A: PipeWire / ALSA Engine
c1a_y, c1a_h = 75, 260
draw_card(c1_x, c1a_y, c1_w, c1a_h, "PipeWire / ALSA", "AUDIO // BACKEND", "48 kHz")

draw.rounded_rectangle((c1_x + 10, c1a_y + 42, c1_x + c1_w - 10, c1a_y + 112), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c1_x + 16, c1a_y + 48), "BUFFER TIMING & SCHED_FIFO", font=f_tiny, fill=INK3)

# Buffer chunk visualization
buf_w = 42
for bi in range(4):
    bx = c1_x + 18 + bi * (buf_w + 12)
    by = c1a_y + 68
    draw.rounded_rectangle((bx, by, bx + buf_w, by + 32), 4, fill=SURF2, outline=CYAN if bi == 3 else LINE, width=1)
    draw.text((bx + 6, by + 6), f"B{bi}", font=f_tiny, fill=CYAN if bi == 3 else INK2)
    draw.text((bx + 6, by + 18), "128spl", font=f_tiny, fill=INK3)

draw.rounded_rectangle((c1_x + 10, c1a_y + 122, c1_x + c1_w - 10, c1a_y + 248), 6, fill=SURF2, outline=SURF3, width=1)
draw.text((c1_x + 16, c1a_y + 130), "LATENCY // 2.6 ms (128 spl @ 48k)", font=f_tiny, fill=GREEN)
draw.text((c1_x + 16, c1a_y + 148), "BACKEND // ALSA DIRECT / PIPEWIRE", font=f_tiny, fill=INK)
draw.text((c1_x + 16, c1a_y + 166), "THREAD // SCHED_FIFO REAL-TIME", font=f_tiny, fill=SIGNAL)
draw.text((c1_x + 16, c1a_y + 184), "XRUNS // 0 OCCURRED IN 60 MIN", font=f_tiny, fill=GREEN)
draw.text((c1_x + 16, c1a_y + 202), "SYNC // ZERO PHASE DRIFT", font=f_tiny, fill=INK2)
draw.text((c1_x + 16, c1a_y + 220), "CHANNELS // 2 IN / 2 OUT STEREO", font=f_tiny, fill=INK3)

c1a_out_audio = (c1_x + c1_w - 8, c1a_y + 100)
draw.ellipse((c1a_out_audio[0]-4, c1a_out_audio[1]-4, c1a_out_audio[0]+4, c1a_out_audio[1]+4), fill=DEEP, outline=CYAN, width=2)

# Card 1B: Euclidean Sequencer
c1b_y, c1b_h = 350, 280
draw_card(c1_x, c1b_y, c1_w, c1b_h, "Euclidean Seq", "NOTE // GENERATIVE", "E(7,16)")

draw.rounded_rectangle((c1_x + 10, c1b_y + 42, c1_x + c1_w - 10, c1b_y + 168), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c1_x + 16, c1b_y + 48), "BJORKLUND DISTRIBUTION (16 STEPS)", font=f_tiny, fill=INK3)

# Radial / circle Euclidean ring
cx, cy, radius = c1_x + c1_w // 2, c1b_y + 108, 42
draw.ellipse((cx - radius, cy - radius, cx + radius, cy + radius), outline=LINE, width=1)
hits = {0, 2, 5, 7, 9, 12, 14}  # 7 pulses in 16 steps
for step in range(16):
    angle = (step / 16.0) * 2 * math.pi - math.pi / 2
    px = cx + math.cos(angle) * radius
    py = cy + math.sin(angle) * radius
    is_hit = step in hits
    dot_r = 5 if is_hit else 3
    col = GOLD if is_hit else SURF3
    draw.ellipse((px - dot_r, py - dot_r, px + dot_r, py + dot_r), fill=col)

draw.rounded_rectangle((c1_x + 10, c1b_y + 178, c1_x + c1_w - 10, c1b_y + 268), 6, fill=SURF2, outline=SURF3, width=1)
draw.text((c1_x + 16, c1b_y + 188), "SCALE // D MINOR PENTATONIC", font=f_tiny, fill=INK2)
draw.text((c1_x + 16, c1b_y + 208), "PULSES // 7 HITS / 16 STEPS", font=f_tiny, fill=GOLD)
draw.text((c1_x + 16, c1b_y + 228), "CLOCK // 1/16 SWING 52%", font=f_tiny, fill=SIGNAL)
draw.text((c1_x + 16, c1b_y + 248), "OCTAVE // C2 TO C4 DYNAMIC", font=f_tiny, fill=INK3)

c1b_out_note = (c1_x + c1_w - 8, c1b_y + 80)
draw.ellipse((c1b_out_note[0]-4, c1b_out_note[1]-4, c1b_out_note[0]+4, c1b_out_note[1]+4), fill=DEEP, outline=GOLD, width=2)

# -------------------------------------------------------------
# COLUMN 2: Synthesiser & Linux VST3 Host (x=325, w=265)
# -------------------------------------------------------------
c2_x, c2_w = 325, 265

# Card 2A: Analog Synth
c2a_y, c2a_h = 75, 260
draw_card(c2_x, c2a_y, c2_w, c2a_h, "Analog Synth", "SYNTH // SUBTRACTIVE", "POLY 8")

draw.rounded_rectangle((c2_x + 10, c2a_y + 42, c2_x + c2_w - 10, c2a_y + 112), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c2_x + 16, c2a_y + 48), "VCO SAW/PULSE MORPH + LADDER", font=f_tiny, fill=INK3)

# Waveform line
vco_pts = []
for i in range(c2_w - 32):
    vx = c2_x + 16 + i
    t = (i % 32) / 32.0
    saw = (t * 2.0 - 1.0) * 10
    pulse = (10 if t < 0.4 else -10) * 0.5
    vy = c2a_y + 82 + (saw + pulse) * 0.8
    vco_pts.append((vx, vy))
draw.line(vco_pts, fill=EMBER, width=2)

draw.rounded_rectangle((c2_x + 10, c2a_y + 122, c2_x + c2_w - 10, c2a_y + 248), 6, fill=SURF2, outline=SURF3, width=1)
draw.text((c2_x + 16, c2a_y + 132), "CUTOFF // 960 Hz RESONANT", font=f_tiny, fill=INK)

# Cutoff slider
draw.rounded_rectangle((c2_x + 16, c2a_y + 150, c2_x + c2_w - 24, c2a_y + 156), 3, fill=DEEP)
draw.rounded_rectangle((c2_x + 16, c2a_y + 150, c2_x + 150, c2a_y + 156), 3, fill=EMBER)
draw.ellipse((c2_x + 146, c2a_y + 148, c2_x + 154, c2a_y + 158), fill=INK)

draw.text((c2_x + 16, c2a_y + 172), "RESONANCE // 3.8 Q PEAK", font=f_tiny, fill=INK2)
draw.text((c2_x + 16, c2a_y + 192), "DRIVE // +3.5 dB ASYMMETRIC", font=f_tiny, fill=INK3)
draw.text((c2_x + 16, c2a_y + 212), "SUB OSC // -1 OCT SINE 40%", font=f_tiny, fill=SIGNAL)
draw.text((c2_x + 16, c2a_y + 230), "ENVELOPE // 2ms A, 280ms D", font=f_tiny, fill=INK3)

c2a_in_note = (c2_x + 8, c2a_y + 70)
draw.ellipse((c2a_in_note[0]-4, c2a_in_note[1]-4, c2a_in_note[0]+4, c2a_in_note[1]+4), fill=DEEP, outline=GOLD, width=2)
c2a_in_mod_cutoff = (c2_x + 8, c2a_y + 153)
draw.ellipse((c2a_in_mod_cutoff[0]-4, c2a_in_mod_cutoff[1]-4, c2a_in_mod_cutoff[0]+4, c2a_in_mod_cutoff[1]+4), fill=DEEP, outline=SIGNAL, width=2)

c2a_out_audio = (c2_x + c2_w - 8, c2a_y + 100)
draw.ellipse((c2a_out_audio[0]-4, c2a_out_audio[1]-4, c2a_out_audio[0]+4, c2a_out_audio[1]+4), fill=DEEP, outline=EMBER, width=2)

# Card 2B: Linux VST3 Host (Plugin node)
c2b_y, c2b_h = 350, 280
draw_card(c2_x, c2b_y, c2_w, c2b_h, "Plugin (VST3)", "HOST // LINUX 64-BIT", "SURGE XT")

draw.rounded_rectangle((c2_x + 10, c2b_y + 42, c2_x + c2_w - 10, c2b_y + 138), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c2_x + 16, c2b_y + 48), "NATIVE LINUX VST3 BUNDLE", font=f_tiny, fill=SIGNAL)
draw.text((c2_x + 16, c2b_y + 68), "PATH: ~/.vst3/Surge XT.vst3", font=f_tiny, fill=INK2)
draw.text((c2_x + 16, c2b_y + 86), "BIN: Contents/x86_64-linux/so", font=f_tiny, fill=INK3)
draw.text((c2_x + 16, c2b_y + 106), "STATUS: SCAN VALIDATED (OK)", font=f_tiny, fill=GREEN)

draw.rounded_rectangle((c2_x + 10, c2b_y + 148, c2_x + c2_w - 10, c2b_y + 268), 6, fill=SURF2, outline=SURF3, width=1)
draw.text((c2_x + 16, c2b_y + 156), "EXPOSED MODULATION INPUTS", font=f_tiny, fill=INK)

# Plugin params
draw.text((c2_x + 28, c2b_y + 176), "Param 01: Morph Position", font=f_tiny, fill=SIGNAL)
draw.text((c2_x + 28, c2b_y + 196), "Param 02: Filter Cutoff", font=f_tiny, fill=INK2)
draw.text((c2_x + 28, c2b_y + 216), "Param 03: Resonance Q", font=f_tiny, fill=INK3)
draw.text((c2_x + 28, c2b_y + 236), "Param 04: Distortion Drive", font=f_tiny, fill=EMBER)
draw.text((c2_x + 28, c2b_y + 252), "OUTPUT: 32-BIT FLOAT STEREO", font=f_tiny, fill=CYAN)

c2b_in_note = (c2_x + 8, c2b_y + 70)
draw.ellipse((c2b_in_note[0]-4, c2b_in_note[1]-4, c2b_in_note[0]+4, c2b_in_note[1]+4), fill=DEEP, outline=GOLD, width=2)
c2b_in_param_morph = (c2_x + 8, c2b_y + 180)
draw.ellipse((c2b_in_param_morph[0]-4, c2b_in_param_morph[1]-4, c2b_in_param_morph[0]+4, c2b_in_param_morph[1]+4), fill=DEEP, outline=SIGNAL, width=2)

c2b_out_audio = (c2_x + c2_w - 8, c2b_y + 100)
draw.ellipse((c2b_out_audio[0]-4, c2b_out_audio[1]-4, c2b_out_audio[0]+4, c2b_out_audio[1]+4), fill=DEEP, outline=CYAN, width=2)

# -------------------------------------------------------------
# COLUMN 3: Audio DSP & FFT Analysis (x=625, w=255)
# -------------------------------------------------------------
c3_x, c3_w = 625, 255

# Card 3A: Audio Analyze (8-band FFT)
c3a_y, c3a_h = 75, 260
draw_card(c3_x, c3a_y, c3_w, c3a_h, "Audio Analyze", "DSP // FFT & ONSET", "8-BAND")

draw.rounded_rectangle((c3_x + 10, c3a_y + 42, c3_x + c3_w - 10, c3a_y + 155), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c3_x + 16, c3a_y + 48), "SPECTRAL ENERGY BANDS", font=f_tiny, fill=INK3)

# 8 FFT Bars
bands = ["SUB", "BASS", "LMID", "MID", "HMID", "PRES", "BRIL", "AIR"]
heights = [62, 78, 54, 42, 48, 32, 24, 16]
bar_w = 18
bar_gap = 9
for bi, (bname, bh) in enumerate(zip(bands, heights)):
    bx = c3_x + 18 + bi * (bar_w + bar_gap)
    by_base = c3a_y + 144
    draw.rectangle((bx, by_base - bh, bx + bar_w, by_base), fill=CYAN if bi < 2 else SIGNAL)
    draw.text((bx + 1, by_base + 3), bname[:2], font=f_tiny, fill=INK3)

draw.rounded_rectangle((c3_x + 10, c3a_y + 168, c3_x + c3_w - 10, c3a_y + 248), 6, fill=SURF2, outline=SURF3, width=1)
draw.text((c3_x + 16, c3a_y + 176), "ONSET DETECTOR // TRANSIENT HIT", font=f_tiny, fill=GREEN)
draw.text((c3_x + 16, c3a_y + 196), "SUB ENERGY (40-80Hz) // 0.82", font=f_tiny, fill=CYAN)
draw.text((c3_x + 16, c3a_y + 214), "MID ENERGY (500-2kHz) // 0.45", font=f_tiny, fill=SIGNAL)
draw.text((c3_x + 16, c3a_y + 232), "HIGH ENERGY (4k-16kHz) // 0.28", font=f_tiny, fill=INK2)

c3a_in_audio = (c3_x + 8, c3a_y + 70)
draw.ellipse((c3a_in_audio[0]-4, c3a_in_audio[1]-4, c3a_in_audio[0]+4, c3a_in_audio[1]+4), fill=DEEP, outline=EMBER, width=2)

c3a_out_sub = (c3_x + c3_w - 8, c3a_y + 80)
draw.ellipse((c3a_out_sub[0]-4, c3a_out_sub[1]-4, c3a_out_sub[0]+4, c3a_out_sub[1]+4), fill=DEEP, outline=SIGNAL, width=2)
c3a_out_onset = (c3_x + c3_w - 8, c3a_y + 180)
draw.ellipse((c3a_out_onset[0]-4, c3a_out_onset[1]-4, c3a_out_onset[0]+4, c3a_out_onset[1]+4), fill=DEEP, outline=GREEN, width=2)

# Card 3B: Audio FX Chain
c3b_y, c3b_h = 350, 280
draw_card(c3_x, c3b_y, c3_w, c3b_h, "Audio Effects", "DSP // DELAY & REVERB", "STEREO")

draw.rounded_rectangle((c3_x + 10, c3b_y + 42, c3_x + c3_w - 10, c3b_y + 138), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c3_x + 16, c3b_y + 48), "PING-PONG DELAY // 3/16 DOTTED", font=f_tiny, fill=CYAN)

# Delay repeats visualization
for ri in range(5):
    rx = c3_x + 22 + ri * 42
    ry = c3b_y + 75
    decay = 1.0 - (ri * 0.18)
    h_bar = int(32 * decay)
    draw.rectangle((rx, ry + (32 - h_bar), rx + 16, ry + 32), fill=CYAN if ri % 2 == 0 else EMBER)

draw.rounded_rectangle((c3_x + 10, c3b_y + 148, c3_x + c3_w - 10, c3b_y + 268), 6, fill=SURF2, outline=SURF3, width=1)
draw.text((c3_x + 16, c3b_y + 158), "STEREO SPREAD // 100% WIDE", font=f_tiny, fill=INK)
draw.text((c3_x + 16, c3b_y + 178), "DIFFUSION REVERB // 2.4s TAIL", font=f_tiny, fill=INK2)
draw.text((c3_x + 16, c3b_y + 198), "WARM TAPE SATURATION // 18%", font=f_tiny, fill=EMBER)
draw.text((c3_x + 16, c3b_y + 218), "HIGHPASS CLEANUP // 35 Hz", font=f_tiny, fill=INK3)
draw.text((c3_x + 16, c3b_y + 238), "MIX RATIO // DRY 65% / WET 35%", font=f_tiny, fill=SIGNAL)
draw.text((c3_x + 16, c3b_y + 254), "DYNAMIC PDC // 0 SAMPLES", font=f_tiny, fill=GREEN)

c3b_in_audio = (c3_x + 8, c3b_y + 80)
draw.ellipse((c3b_in_audio[0]-4, c3b_in_audio[1]-4, c3b_in_audio[0]+4, c3b_in_audio[1]+4), fill=DEEP, outline=CYAN, width=2)

c3b_out_audio = (c3_x + c3_w - 8, c3b_y + 100)
draw.ellipse((c3b_out_audio[0]-4, c3b_out_audio[1]-4, c3b_out_audio[0]+4, c3b_out_audio[1]+4), fill=DEEP, outline=EMBER, width=2)

# -------------------------------------------------------------
# COLUMN 4: Real-time GPU Shaders & Output (x=915, w=245)
# -------------------------------------------------------------
c4_x, c4_w = 915, 245

# Card 4A: Field Pixel (GLSL Shader)
c4a_y, c4a_h = 75, 260
draw_card(c4_x, c4a_y, c4_w, c4a_h, "Field Pixel", "GPU // GLSL SHADER", "60 FPS")

draw.rounded_rectangle((c4_x + 10, c4a_y + 42, c4_x + c4_w - 10, c4a_y + 155), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c4_x + 16, c4a_y + 48), "AUDIO-REACTIVE FRAGMENT GLSL", font=f_tiny, fill=GREEN)

# Raymarching / visual rings simulation
scx, scy = c4_x + c4_w // 2, c4a_y + 105
for r_step in range(4):
    sr = 12 + r_step * 10
    draw.ellipse((scx - sr, scy - sr, scx + sr, scy + sr), outline=GREEN if r_step == 2 else SIGNAL, width=1)

draw.rounded_rectangle((c4_x + 10, c4a_y + 168, c4_x + c4_w - 10, c4a_y + 248), 6, fill=SURF2, outline=SURF3, width=1)
draw.text((c4_x + 16, c4a_y + 176), "param float u_bass // 0.82", font=f_tiny, fill=SIGNAL)
draw.text((c4_x + 16, c4a_y + 196), "param float u_onset // 1.0", font=f_tiny, fill=GREEN)
draw.text((c4_x + 16, c4a_y + 214), "RESOLUTION // 1920x1080 GPU", font=f_tiny, fill=INK)
draw.text((c4_x + 16, c4a_y + 232), "SYNC // FRAME-ACCURATE WITH DSP", font=f_tiny, fill=CYAN)

c4a_in_bass = (c4_x + 8, c4a_y + 179)
draw.ellipse((c4a_in_bass[0]-4, c4a_in_bass[1]-4, c4a_in_bass[0]+4, c4a_in_bass[1]+4), fill=DEEP, outline=SIGNAL, width=2)
c4a_in_onset = (c4_x + 8, c4a_y + 199)
draw.ellipse((c4a_in_onset[0]-4, c4a_in_onset[1]-4, c4a_in_onset[0]+4, c4a_in_onset[1]+4), fill=DEEP, outline=GREEN, width=2)

c4a_out_visual = (c4_x + c4_w - 8, c4a_y + 90)
draw.ellipse((c4a_out_visual[0]-4, c4a_out_visual[1]-4, c4a_out_visual[0]+4, c4a_out_visual[1]+4), fill=DEEP, outline=GREEN, width=2)

# Card 4B: Output & Headless CLI
c4b_y, c4b_h = 350, 280
draw_card(c4_x, c4b_y, c4_w, c4b_h, "Output & CLI", "MASTER // HEADLESS", "DESKTOP")

draw.rounded_rectangle((c4_x + 10, c4b_y + 42, c4_x + c4_w - 10, c4b_y + 128), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c4_x + 16, c4b_y + 48), "STEREO MASTER METERS (dBFS)", font=f_tiny, fill=INK3)

# Master level meters L & R
draw.text((c4_x + 16, c4b_y + 68), "L", font=f_tiny, fill=INK2)
draw.rounded_rectangle((c4_x + 30, c4b_y + 68, c4_x + c4_w - 20, c4b_y + 76), 3, fill=SURF2)
draw.rounded_rectangle((c4_x + 30, c4b_y + 68, c4_x + 180, c4b_y + 76), 3, fill=GREEN)

draw.text((c4_x + 16, c4b_y + 88), "R", font=f_tiny, fill=INK2)
draw.rounded_rectangle((c4_x + 30, c4b_y + 88, c4_x + c4_w - 20, c4b_y + 96), 3, fill=SURF2)
draw.rounded_rectangle((c4_x + 30, c4b_y + 88, c4_x + 172, c4b_y + 96), 3, fill=GREEN)

draw.text((c4_x + 16, c4b_y + 108), "PEAK: -3.2 dB  //  RMS: -14.1 dB", font=f_tiny, fill=INK)

draw.rounded_rectangle((c4_x + 10, c4b_y + 138, c4_x + c4_w - 10, c4b_y + 268), 6, fill=SURF2, outline=SURF3, width=1)
draw.text((c4_x + 16, c4b_y + 146), "HEADLESS LINUX RENDER CLI", font=f_tiny, fill=SIGNAL)
draw.text((c4_x + 16, c4b_y + 166), "$ xvfb-run -a infinite \\", font=f_tiny, fill=GREEN)
draw.text((c4_x + 24, c4b_y + 184), "--render --patch live.inf \\", font=f_tiny, fill=INK2)
draw.text((c4_x + 24, c4b_y + 202), "--frames 1800 --out vid.mp4", font=f_tiny, fill=INK2)
draw.text((c4_x + 16, c4b_y + 224), "OFFLINE AUDIO + 60FPS VIDEO", font=f_tiny, fill=CYAN)
draw.text((c4_x + 16, c4b_y + 246), "SERVER / CI / CRON AUTOMATION", font=f_tiny, fill=INK3)

c4b_in_audio = (c4_x + 8, c4b_y + 72)
draw.ellipse((c4b_in_audio[0]-4, c4b_in_audio[1]-4, c4b_in_audio[0]+4, c4b_in_audio[1]+4), fill=DEEP, outline=EMBER, width=2)
c4b_in_visual = (c4_x + 8, c4b_y + 92)
draw.ellipse((c4b_in_visual[0]-4, c4b_in_visual[1]-4, c4b_in_visual[0]+4, c4b_in_visual[1]+4), fill=DEEP, outline=GREEN, width=2)

# -------------------------------------------------------------
# PATCH CABLES (Curved bezier splines)
# -------------------------------------------------------------
def draw_cable(p1, p2, col, width=2, curvature=0.5):
    x1, y1 = p1
    x2, y2 = p2
    dx = abs(x2 - x1) * curvature
    pts = []
    steps = 40
    for s in range(steps + 1):
        t = s / steps
        # Cubic Bezier
        bx = (1-t)**3 * x1 + 3*(1-t)**2*t*(x1 + dx) + 3*(1-t)*t**2*(x2 - dx) + t**3 * x2
        by = (1-t)**3 * y1 + 3*(1-t)**2*t*y1 + 3*(1-t)*t**2*y2 + t**3 * y2
        pts.append((bx, by))
    draw.line(pts, fill=col, width=width)

# Cable 1: Euclidean Note Out -> Analog Note In (Gold)
draw_cable(c1b_out_note, c2a_in_note, GOLD, width=2, curvature=0.45)

# Cable 2: Euclidean Note Out -> VST3 Note In (Gold)
draw_cable(c1b_out_note, c2b_in_note, GOLD, width=2, curvature=0.35)

# Cable 3: Analog Audio Out -> Audio FX In (Ember)
draw_cable(c2a_out_audio, c3b_in_audio, EMBER, width=2, curvature=0.4)

# Cable 4: VST3 Audio Out -> Audio Analyze In (Cyan)
draw_cable(c2b_out_audio, c3a_in_audio, CYAN, width=2, curvature=0.4)

# Cable 5: Audio Analyze Sub FFT Out -> Field Pixel Bass Mod In (Signal)
draw_cable(c3a_out_sub, c4a_in_bass, SIGNAL, width=2, curvature=0.45)

# Cable 6: Audio Analyze Onset Out -> Field Pixel Onset Mod In (Green)
draw_cable(c3a_out_onset, c4a_in_onset, GREEN, width=2, curvature=0.4)

# Cable 7: Audio FX Out -> Master Audio In (Ember)
draw_cable(c3b_out_audio, c4b_in_audio, EMBER, width=2, curvature=0.4)

# Cable 8: Field Pixel Visual Out -> Master Video In (Green)
draw_cable(c4a_out_visual, c4b_in_visual, GREEN, width=2, curvature=0.4)

# Save image
out_filename = "blog_linux-daw-modular-audio-and-generative-visuals.png"
out_dir = os.path.join(ROOT, "website", "assets")
os.makedirs(out_dir, exist_ok=True)
out_path = os.path.join(out_dir, out_filename)
img.save(out_path, "PNG")
print(f"Generated {out_path} ({W}x{H})")

# Also copy to worktrees/main or infinte-base if present
alt_dirs = [
    os.path.join(ROOT, "..", "worktrees", "main", "website", "assets"),
    os.path.join(ROOT, "..", "infinte-base", "website", "assets"),
]
for ad in alt_dirs:
    if os.path.exists(ad):
        alt_path = os.path.join(ad, out_filename)
        img.save(alt_path, "PNG")
        print(f"Also saved {alt_path}")
