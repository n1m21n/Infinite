#!/usr/bin/env python3
"""Generates the 1200x675 technical hero diagram for drum-and-bass-reese-bass-sound-design.
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
    f_title = ImageFont.truetype(font_sans_path, 18)
    f_sub = ImageFont.truetype(font_mono_path, 11)
    f_card_title = ImageFont.truetype(font_sans_path, 12)
    f_badge = ImageFont.truetype(font_mono_path, 10)
    f_code = ImageFont.truetype(font_mono_path, 10)
    f_label = ImageFont.truetype(font_mono_path, 9)
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
draw.text((142, 39), "//  DRUM & BASS REESE BASS: MULTI-BAND DSP ARCHITECTURE", font=f_sub, fill=SIGNAL)

badge_text = "MULTI-BAND SPLIT  •  DETUNED SAWS  •  ASYMMETRIC SATURATION  •  AUDIO-REACTIVE GLSL"
bw = draw.textlength(badge_text, font=f_badge) + 20
draw.rounded_rectangle((W - 42 - bw, 32, W - 42, 58), 6, fill=SURF1, outline=LINE, width=1)
draw.text((W - 42 - bw + 10, 38), badge_text, font=f_badge, fill=INK2)

# Helper function for card
def draw_card(x, y, w, h, title, category, badge=None):
    draw.rounded_rectangle((x, y + 4, x + w, y + h + 4), 8, fill=(4, 5, 14, 160))
    draw.rounded_rectangle((x, y, x + w, y + h), 8, fill=SURF1, outline=LINE, width=1)
    draw.rounded_rectangle((x, y, x + w, y + 28), 8, fill=SURF2)
    draw.rectangle((x, y + 16, x + w, y + 28), fill=SURF2)
    draw.line((x, y + 28, x + w, y + 28), fill=LINE, width=1)
    draw.text((x + 10, y + 6), title, font=f_card_title, fill=INK)
    tw = draw.textlength(title, font=f_card_title)
    draw.text((x + 10 + tw + 8, y + 8), category, font=f_small, fill=INK3)
    if badge:
        bbw = draw.textlength(badge, font=f_tiny) + 8
        draw.rounded_rectangle((x + w - 8 - bbw, y + 5, x + w - 8, y + 22), 4, fill=SURF3)
        draw.text((x + w - 8 - bbw + 4, y + 7), badge, font=f_tiny, fill=SIGNAL)

# -------------------------------------------------------------
# COLUMN 1: Sources (Analog + Modulator)
# -------------------------------------------------------------
c1_x, c1_w = 40, 255

# 1A: Analog Node (Detuned saw oscillators)
c1a_y, c1a_h = 74, 275
draw_card(c1_x, c1a_y, c1_w, c1a_h, "Analog", "AUDIO // SYNTH", "DUAL SAW CORE")

box1_y = c1a_y + 36
draw.rounded_rectangle((c1_x + 8, box1_y, c1_x + c1_w - 8, box1_y + 115), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c1_x + 14, box1_y + 6), "DETUNED INTERFERENCE (BEATING)", font=f_tiny, fill=INK3)

# Draw detuned beating waveform
wave_w = c1_w - 28
wave_pts = []
for i in range(wave_w):
    t = (i / wave_w) * 6.0 * math.pi
    # Superposition of two detuned saws
    saw1 = 2.0 * (t / (2.0 * math.pi) - math.floor(0.5 + t / (2.0 * math.pi)))
    t2 = t * 1.08  # ~8 cents detune
    saw2 = 2.0 * (t2 / (2.0 * math.pi) - math.floor(0.5 + t2 / (2.0 * math.pi)))
    sub = math.sin(t * 0.5) * 0.7  # sub octave sine
    val = (saw1 + saw2) * 0.45
    wy = (box1_y + 68) - val * 26
    wave_pts.append((c1_x + 14 + i, wy))
draw.line(wave_pts, fill=CYAN, width=2)

draw.text((c1_x + 14, c1a_y + 160), "OSC 1   // SAWTOOTH (ROOT)", font=f_small, fill=INK2)
draw.text((c1_x + 14, c1a_y + 178), "OSC 2   // SAWTOOTH (+9 CENTS)", font=f_small, fill=CYAN)
draw.text((c1_x + 14, c1a_y + 196), "OSC 3   // CLEAN SINE (-1 OCT)", font=f_small, fill=GOLD)
draw.text((c1_x + 14, c1a_y + 214), "PHASE   // FREE-RUNNING", font=f_small, fill=INK3)
draw.text((c1_x + 14, c1a_y + 232), "BEAT f  // |f1 - f2| = 1.84 Hz", font=f_small, fill=EMBER)
draw.text((c1_x + 14, c1a_y + 250), "POLY    // MONO LEGATO (GLIDE 45ms)", font=f_small, fill=INK2)

# Ports
p_analog_out = (c1_x + c1_w - 6, c1a_y + 105)
draw.ellipse((p_analog_out[0]-4, p_analog_out[1]-4, p_analog_out[0]+4, p_analog_out[1]+4), fill=DEEP, outline=CYAN, width=2)
draw.text((c1_x + c1_w - 40, c1a_y + 100), "audio", font=f_tiny, fill=CYAN)

# 1B: Modulator Node (LFO CV)
c1b_y, c1b_h = 365, 275
draw_card(c1_x, c1b_y, c1_w, c1b_h, "Modulator", "MODULATOR // CV", "COMPLEX LFO")

box2_y = c1b_y + 36
draw.rounded_rectangle((c1_x + 8, box2_y, c1_x + c1_w - 8, box2_y + 115), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c1_x + 14, box2_y + 6), "MULTI-STAGE NOTCH SWEEP CV", font=f_tiny, fill=INK3)

# Draw complex LFO curve
lfo_pts = []
for i in range(wave_w):
    t = (i / wave_w) * 4.0 * math.pi
    y_val = math.sin(t) * 0.7 + math.sin(t * 2.3 + 0.4) * 0.3
    ly = (box2_y + 68) - y_val * 32
    lfo_pts.append((c1_x + 14 + i, ly))
draw.line(lfo_pts, fill=EMBER, width=2)

draw.text((c1_x + 14, c1b_y + 160), "RATE     // 1/2 BAR (SYNCED)", font=f_small, fill=INK2)
draw.text((c1_x + 14, c1b_y + 178), "SHAPE    // MORPHED TRI / SINE", font=f_small, fill=EMBER)
draw.text((c1_x + 14, c1b_y + 196), "SLEW     // 12.5 ms FILTERED", font=f_small, fill=INK3)
draw.text((c1_x + 14, c1b_y + 214), "DEST 1   // NOTCH CUTOFF [0.65]", font=f_small, fill=SIGNAL)
draw.text((c1_x + 14, c1b_y + 232), "DEST 2   // DRIVE LEVEL  [0.30]", font=f_small, fill=EMBER)
draw.text((c1_x + 14, c1b_y + 250), "DEST 3   // VISUAL DISP  [0.85]", font=f_small, fill=CYAN)

p_mod_out = (c1_x + c1_w - 6, c1b_y + 105)
draw.ellipse((p_mod_out[0]-4, p_mod_out[1]-4, p_mod_out[0]+4, p_mod_out[1]+4), fill=DEEP, outline=EMBER, width=2)
draw.text((c1_x + c1_w - 28, c1b_y + 100), "cv", font=f_tiny, fill=EMBER)

# -------------------------------------------------------------
# COLUMN 2: Frequency Crossover & Distortion
# -------------------------------------------------------------
c2_x, c2_w = 325, 265

# 2A: Filter (Crossover Split)
c2a_y, c2a_h = 74, 275
draw_card(c2_x, c2a_y, c2_w, c2a_h, "Filter", "AUDIO // CROSSOVER", "110 Hz SPLIT")

box3_y = c2a_y + 36
draw.rounded_rectangle((c2_x + 8, box3_y, c2_x + c2_w - 8, box3_y + 115), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c2_x + 14, box3_y + 6), "BUTTERWORTH 24dB CROSSOVER", font=f_tiny, fill=INK3)

# Draw crossover curve (LP and HP overlapping at 110Hz)
cw = c2_w - 28
lp_pts, hp_pts = [], []
for i in range(cw):
    xf = i / cw  # 0 to 1 across log freq
    # Lowpass curve
    lp_gain = 1.0 / math.sqrt(1.0 + math.pow(xf / 0.35, 8))
    # Highpass curve
    hp_gain = 1.0 / math.sqrt(1.0 + math.pow(0.35 / (xf + 0.001), 8))
    
    lpy = (box3_y + 102) - lp_gain * 65
    hpy = (box3_y + 102) - hp_gain * 65
    lp_pts.append((c2_x + 14 + i, lpy))
    hp_pts.append((c2_x + 14 + i, hpy))

draw.line(lp_pts, fill=GOLD, width=2)
draw.line(hp_pts, fill=CYAN, width=2)
draw.text((c2_x + 14 + int(cw * 0.12), box3_y + 35), "SUB (MONO)", font=f_tiny, fill=GOLD)
draw.text((c2_x + 14 + int(cw * 0.58), box3_y + 35), "GRIT (STEREO)", font=f_tiny, fill=CYAN)
draw.line([(c2_x + 14 + int(cw * 0.35), box3_y + 20), (c2_x + 14 + int(cw * 0.35), box3_y + 105)], fill=LINE, width=1)
draw.text((c2_x + 14 + int(cw * 0.35) - 20, box3_y + 104), "110 Hz", font=f_tiny, fill=INK3)

draw.text((c2_x + 14, c2a_y + 160), "TYPE A  // 4-POLE LOWPASS (110 Hz)", font=f_small, fill=GOLD)
draw.text((c2_x + 14, c2a_y + 178), "TYPE B  // 4-POLE HIGHPASS (110 Hz)", font=f_small, fill=CYAN)
draw.text((c2_x + 14, c2a_y + 196), "SLOPE   // 24 dB/OCTAVE LINKWITZ", font=f_small, fill=INK2)
draw.text((c2_x + 14, c2a_y + 214), "PHASE   // ZERO SUM CANCELLATION", font=f_small, fill=GREEN)
draw.text((c2_x + 14, c2a_y + 232), "SUB OUT // CLEAN MONO ANCHOR", font=f_small, fill=GOLD)
draw.text((c2_x + 14, c2a_y + 250), "MID OUT // ROUTED TO SATURATION", font=f_small, fill=CYAN)

p_crossover_in = (c2_x + 6, c2a_y + 105)
draw.ellipse((p_crossover_in[0]-4, p_crossover_in[1]-4, p_crossover_in[0]+4, p_crossover_in[1]+4), fill=DEEP, outline=CYAN, width=2)

p_sub_out = (c2_x + c2_w - 6, c2a_y + 75)
draw.ellipse((p_sub_out[0]-4, p_sub_out[1]-4, p_sub_out[0]+4, p_sub_out[1]+4), fill=DEEP, outline=GOLD, width=2)
draw.text((c2_x + c2_w - 32, c2a_y + 70), "sub", font=f_tiny, fill=GOLD)

p_grit_out = (c2_x + c2_w - 6, c2a_y + 135)
draw.ellipse((p_grit_out[0]-4, p_grit_out[1]-4, p_grit_out[0]+4, p_grit_out[1]+4), fill=DEEP, outline=CYAN, width=2)
draw.text((c2_x + c2_w - 34, c2a_y + 130), "grit", font=f_tiny, fill=CYAN)

# 2B: Distortion Node (Grit processing)
c2b_y, c2b_h = 365, 275
draw_card(c2_x, c2b_y, c2_w, c2b_h, "Distortion", "AUDIO // WAVESHAPER", "ASYMMETRIC")

box4_y = c2b_y + 36
draw.rounded_rectangle((c2_x + 8, box4_y, c2_x + c2_w - 8, box4_y + 115), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c2_x + 14, box4_y + 6), "POLYNOMIAL TANH SATURATION", font=f_tiny, fill=INK3)

# Draw waveshaping transfer curve
dist_pts = []
for i in range(cw):
    x_in = (i / cw) * 2.0 - 1.0
    # Asymmetric saturation
    if x_in > 0:
        y_out = math.tanh(x_in * 2.8) * 0.95
    else:
        y_out = (math.tanh(x_in * 1.8) - 0.15 * (x_in * x_in)) * 0.9
    dy = (box4_y + 68) - y_out * 38
    dist_pts.append((c2_x + 14 + i, dy))
draw.line([(c2_x + 14, box4_y + 68), (c2_x + 14 + cw, box4_y + 68)], fill=SURF3, width=1)
draw.line([(c2_x + 14 + cw // 2, box4_y + 20), (c2_x + 14 + cw // 2, box4_y + 110)], fill=SURF3, width=1)
draw.line(dist_pts, fill=EMBER, width=2)

draw.text((c2_x + 14, c2b_y + 160), "DRIVE    // +16.5 dB (CV MODULATED)", font=f_small, fill=EMBER)
draw.text((c2_x + 14, c2b_y + 178), "MODE     // ASYMMETRIC TUBE FOLD", font=f_small, fill=INK2)
draw.text((c2_x + 14, c2b_y + 196), "BIAS     // +0.22 (EVEN HARMONICS)", font=f_small, fill=CYAN)
draw.text((c2_x + 14, c2b_y + 214), "OVERSAMP // 4X POLYPHASE IIR", font=f_small, fill=GREEN)
draw.text((c2_x + 14, c2b_y + 232), "CEILING  // 0.0 dBFS HARD ROOF", font=f_small, fill=INK3)
draw.text((c2_x + 14, c2b_y + 250), "TONE     // +2.4 dB HIGH SHELF", font=f_small, fill=INK2)

p_dist_in = (c2_x + 6, c2b_y + 105)
draw.ellipse((p_dist_in[0]-4, p_dist_in[1]-4, p_dist_in[0]+4, p_dist_in[1]+4), fill=DEEP, outline=CYAN, width=2)

p_dist_cv = (c2_x + 6, c2b_y + 165)
draw.ellipse((p_dist_cv[0]-4, p_dist_cv[1]-4, p_dist_cv[0]+4, p_dist_cv[1]+4), fill=DEEP, outline=EMBER, width=2)

p_dist_out = (c2_x + c2_w - 6, c2b_y + 105)
draw.ellipse((p_dist_out[0]-4, p_dist_out[1]-4, p_dist_out[0]+4, p_dist_out[1]+4), fill=DEEP, outline=EMBER, width=2)
draw.text((c2_x + c2_w - 40, c2b_y + 100), "audio", font=f_tiny, fill=EMBER)

# -------------------------------------------------------------
# COLUMN 3: Movement & Stereo Space (Filter Notch + Chorus)
# -------------------------------------------------------------
c3_x, c3_w = 615, 265

# 3A: Filter (Notch / Comb Movement)
c3a_y, c3a_h = 74, 275
draw_card(c3_x, c3a_y, c3_w, c3a_h, "Filter", "AUDIO // FORMANT", "TWIN NOTCH")

box5_y = c3a_y + 36
draw.rounded_rectangle((c3_x + 8, box5_y, c3_x + c3_w - 8, box5_y + 115), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c3_x + 14, box5_y + 6), "DYNAMIC SPECTRAL VOWEL SWEEP", font=f_tiny, fill=INK3)

# Draw twin notch response
nw = c3_w - 28
notch_pts = []
for i in range(nw):
    xf = i / nw
    # two notches at x=0.35 and x=0.68
    d1 = abs(xf - 0.38)
    d2 = abs(xf - 0.70)
    att1 = 1.0 - 0.85 / (1.0 + math.pow(d1 * 28.0, 2))
    att2 = 1.0 - 0.75 / (1.0 + math.pow(d2 * 32.0, 2))
    tot = min(att1, att2)
    ny = (box5_y + 102) - tot * 65
    notch_pts.append((c3_x + 14 + i, ny))
draw.line(notch_pts, fill=SIGNAL, width=2)
draw.text((c3_x + 14 + int(nw * 0.32), box5_y + 30), "NOTCH 1", font=f_tiny, fill=SIGNAL)
draw.text((c3_x + 14 + int(nw * 0.64), box5_y + 30), "NOTCH 2", font=f_tiny, fill=SIGNAL)

draw.text((c3_x + 14, c3a_y + 160), "TOPOLOGY // TWIN REJECT NOTCH", font=f_small, fill=SIGNAL)
draw.text((c3_x + 14, c3a_y + 178), "SWEEP f1 // 420 Hz -> 1.8 kHz", font=f_small, fill=CYAN)
draw.text((c3_x + 14, c3a_y + 196), "SWEEP f2 // 1.2 kHz -> 3.4 kHz", font=f_small, fill=SIGNAL)
draw.text((c3_x + 14, c3a_y + 214), "Q FACTOR // 4.8 (SHARP VOWEL FORMANT)", font=f_small, fill=INK2)
draw.text((c3_x + 14, c3a_y + 232), "CV IN    // MODULATOR LFO BOUND", font=f_small, fill=EMBER)
draw.text((c3_x + 14, c3a_y + 250), "DRIVE    // SOFT COMPENSATE GAIN", font=f_small, fill=INK3)

p_notch_in = (c3_x + 6, c3a_y + 105)
draw.ellipse((p_notch_in[0]-4, p_notch_in[1]-4, p_notch_in[0]+4, p_notch_in[1]+4), fill=DEEP, outline=EMBER, width=2)

p_notch_cv = (c3_x + 6, c3a_y + 235)
draw.ellipse((p_notch_cv[0]-4, p_notch_cv[1]-4, p_notch_cv[0]+4, p_notch_cv[1]+4), fill=DEEP, outline=EMBER, width=2)

p_notch_out = (c3_x + c3_w - 6, c3a_y + 105)
draw.ellipse((p_notch_out[0]-4, p_notch_out[1]-4, p_notch_out[0]+4, p_notch_out[1]+4), fill=DEEP, outline=SIGNAL, width=2)
draw.text((c3_x + c3_w - 40, c3a_y + 100), "audio", font=f_tiny, fill=SIGNAL)

# 3B: Chorus Node (High-band stereo width)
c3b_y, c3b_h = 365, 275
draw_card(c3_x, c3b_y, c3_w, c3b_h, "Chorus", "AUDIO // SPATIAL", "HAAS DELAY")

box6_y = c3b_y + 36
draw.rounded_rectangle((c3_x + 8, box6_y, c3_x + c3_w - 8, box6_y + 115), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c3_x + 14, box6_y + 6), "STEREO PHASE EXPANSION (>110 Hz)", font=f_tiny, fill=INK3)

# Draw stereo Haas delay lines
draw.line([(c3_x + 20, box6_y + 40), (c3_x + c3_w - 20, box6_y + 40)], fill=CYAN, width=2)
draw.line([(c3_x + 36, box6_y + 65), (c3_x + c3_w - 20, box6_y + 65)], fill=SIGNAL, width=2)
draw.line([(c3_x + 20, box6_y + 32), (c3_x + 20, box6_y + 48)], fill=CYAN, width=2)
draw.line([(c3_x + 36, box6_y + 57), (c3_x + 36, box6_y + 73)], fill=SIGNAL, width=2)
draw.text((c3_x + 24, box6_y + 46), "LEFT DELAY: 0.0 ms", font=f_tiny, fill=CYAN)
draw.text((c3_x + 40, box6_y + 72), "RIGHT DELAY: +14.2 ms (HAAS)", font=f_tiny, fill=SIGNAL)
draw.text((c3_x + 14, box6_y + 98), "SUB 100 Hz PHASE: 100% CORRELATED (MONO)", font=f_tiny, fill=GREEN)

draw.text((c3_x + 14, c3b_y + 160), "STEREO W // 145% WIDTH ON GRIT", font=f_small, fill=CYAN)
draw.text((c3_x + 14, c3b_y + 178), "MONO BAS // STRICT < 110 Hz MONO", font=f_small, fill=GREEN)
draw.text((c3_x + 14, c3b_y + 196), "FEEDBACK // 18% POLISHED RING", font=f_small, fill=INK2)
draw.text((c3_x + 14, c3b_y + 214), "RATE     // 0.28 Hz QUADRATURE", font=f_small, fill=INK3)
draw.text((c3_x + 14, c3b_y + 232), "DRY/WET  // 65% WET PARALLEL", font=f_small, fill=SIGNAL)
draw.text((c3_x + 14, c3b_y + 250), "CLUB SAFE// ZERO MONO CANCELLATION", font=f_small, fill=GREEN)

p_chorus_in = (c3_x + 6, c3b_y + 105)
draw.ellipse((p_chorus_in[0]-4, p_chorus_in[1]-4, p_chorus_in[0]+4, p_chorus_in[1]+4), fill=DEEP, outline=SIGNAL, width=2)

p_grit_final = (c3_x + c3_w - 6, c3b_y + 105)
draw.ellipse((p_grit_final[0]-4, p_grit_final[1]-4, p_grit_final[0]+4, p_grit_final[1]+4), fill=DEEP, outline=CYAN, width=2)
draw.text((c3_x + c3_w - 42, c3b_y + 100), "stereo", font=f_tiny, fill=CYAN)

# -------------------------------------------------------------
# COLUMN 4: Visuals & Metering (Audio Meter + Audio Texture)
# -------------------------------------------------------------
c4_x, c4_w = 905, 255

# 4A: Audio Meter (Phase correlation & vectorscope)
c4a_y, c4a_h = 74, 275
draw_card(c4_x, c4a_y, c4_w, c4a_h, "Audio Meter", "UTILITY // ANALYSIS", "VECTORSCOPE")

box7_y = c4a_y + 36
draw.rounded_rectangle((c4_x + 8, box7_y, c4_x + c4_w - 8, box7_y + 115), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c4_x + 14, box7_y + 6), "GONISCOPE / PHASE CORRELATION", font=f_tiny, fill=INK3)

# Draw vectorscope lissajous ellipse
cx = c4_x + 8 + (c4_w - 16) // 2
cy = box7_y + 68
draw.line([(cx - 45, cy), (cx + 45, cy)], fill=SURF3, width=1)
draw.line([(cx, cy - 42), (cx, cy + 42)], fill=SURF3, width=1)

# Lissajous trace
liss_pts = []
for i in range(120):
    th = (i / 120) * 2.0 * math.pi
    lx = cx + math.sin(th) * 32 + math.cos(th * 2.0) * 8
    ly = cy - math.cos(th) * 38 + math.sin(th * 3.0) * 6
    liss_pts.append((lx, ly))
draw.line(liss_pts, fill=GREEN, width=2)
draw.text((c4_x + 14, box7_y + 102), "PHASE: +0.94 (MONO SAFE)", font=f_tiny, fill=GREEN)

draw.text((c4_x + 14, c4a_y + 160), "RMS LEV  // -12.4 dBFS NOMINAL", font=f_small, fill=INK2)
draw.text((c4_x + 14, c4a_y + 178), "PEAK SUB // -2.8 dBFS HEADROOM", font=f_small, fill=GOLD)
draw.text((c4_x + 14, c4a_y + 196), "CREST    // 9.6 dB DYNAMIC FACTOR", font=f_small, fill=INK3)
draw.text((c4_x + 14, c4a_y + 214), "OCTAVES  // 30 Hz -> 8.5 kHz", font=f_small, fill=CYAN)
draw.text((c4_x + 14, c4a_y + 232), "CORREL   // +0.94 IN CLUBS", font=f_small, fill=GREEN)
draw.text((c4_x + 14, c4a_y + 250), "BALLIST  // ITU-R BS.1770 TRUE PEAK", font=f_small, fill=INK2)

p_meter_in = (c4_x + 6, c4a_y + 105)
draw.ellipse((p_meter_in[0]-4, p_meter_in[1]-4, p_meter_in[0]+4, p_meter_in[1]+4), fill=DEEP, outline=GREEN, width=2)

# 4B: Audio Texture / Render 3D (Audio-reactive deformation)
c4b_y, c4b_h = 365, 275
draw_card(c4_x, c4b_y, c4_w, c4b_h, "Audio Texture", "2D // SHADER UNIFORM", "REACTIVE 3D")

box8_y = c4b_y + 36
draw.rounded_rectangle((c4_x + 8, box8_y, c4_x + c4_w - 8, box8_y + 115), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c4_x + 14, box8_y + 6), "AUDIO BEAT DRIVING GLSL UNIFORMS", font=f_tiny, fill=INK3)

# Draw simulated 3D audio-reactive mesh deformation
mw = c4_w - 28
mesh_y = box8_y + 68
for row in range(-2, 3):
    m_pts = []
    for col in range(mw):
        u = (col / mw) * 4.0 * math.pi
        disp = math.sin(u + row * 0.8) * math.cos(u * 0.5) * 14.0 * (1.0 - abs(row) * 0.25)
        m_pts.append((c4_x + 14 + col, mesh_y + row * 10 - disp))
    draw.line(m_pts, fill=EMBER if row == 0 else SIGNAL, width=2 if row == 0 else 1)

draw.text((c4_x + 14, c4b_y + 160), "DEST     // Render 3D [VERTEX DISP]", font=f_small, fill=CYAN)
draw.text((c4_x + 14, c4b_y + 178), "SOURCE   // REESE BEATING SPECTRUM", font=f_small, fill=EMBER)
draw.text((c4_x + 14, c4b_y + 196), "TEXTURE  // 512x1 FLOAT16 BUFFER", font=f_small, fill=INK2)
draw.text((c4_x + 14, c4b_y + 214), "FPS LOCK // 60 FPS ZERO-COPY GPU", font=f_small, fill=GREEN)
draw.text((c4_x + 14, c4b_y + 232), "EFFECT   // GEOMETRY PULSES WITH BEAT", font=f_small, fill=SIGNAL)
draw.text((c4_x + 14, c4b_y + 250), "LATENCY  // 1.3 ms DISPATCH DELAY", font=f_small, fill=INK3)

p_tex_in = (c4_x + 6, c4b_y + 105)
draw.ellipse((p_tex_in[0]-4, p_tex_in[1]-4, p_tex_in[0]+4, p_tex_in[1]+4), fill=DEEP, outline=EMBER, width=2)

# -------------------------------------------------------------
# CABLES: Connecting nodes across the canvas
# -------------------------------------------------------------
def draw_cable(p1, p2, col, width=2):
    # Bezier-like curve via cubic points
    x1, y1 = p1
    x2, y2 = p2
    dx = (x2 - x1) * 0.5
    pts = []
    for step in range(32):
        t = step / 31.0
        # Hermite spline
        cx1 = x1 + dx
        cx2 = x2 - dx
        bx = (1-t)**3 * x1 + 3*(1-t)**2 * t * cx1 + 3*(1-t)*t**2 * cx2 + t**3 * x2
        by = (1-t)**3 * y1 + 3*(1-t)**2 * t * y1 + 3*(1-t)*t**2 * y2 + t**3 * y2
        pts.append((bx, by))
    draw.line(pts, fill=col, width=width)

# Cable 1: Analog out -> Filter Crossover in
draw_cable(p_analog_out, p_crossover_in, CYAN, 2)

# Cable 2: Filter Grit out -> Distortion in
draw_cable(p_grit_out, p_dist_in, CYAN, 2)

# Cable 3: Modulator out -> Distortion CV in
draw_cable(p_mod_out, p_dist_cv, EMBER, 2)

# Cable 4: Modulator out -> Filter Notch CV in
draw_cable(p_mod_out, p_notch_cv, EMBER, 2)

# Cable 5: Distortion out -> Filter Notch in
draw_cable(p_dist_out, p_notch_in, EMBER, 2)

# Cable 6: Filter Notch out -> Chorus in
draw_cable(p_notch_out, p_chorus_in, SIGNAL, 2)

# Cable 7: Chorus out -> Audio Meter in
draw_cable(p_grit_final, p_meter_in, CYAN, 2)

# Cable 8: Modulator CV -> Audio Texture in
draw_cable(p_mod_out, p_tex_in, EMBER, 2)

# Cable 9: Sub Out -> Audio Meter in (summed mono)
draw_cable(p_sub_out, (c4_x + 6, c4a_y + 85), GOLD, 2)

# Save to target file
out_filename = "blog_drum-and-bass-reese-bass-sound-design.png"
out_paths = [
    os.path.join(ROOT, "website", "assets", out_filename),
    os.path.join("/Users/namansoni/Infinite/repo/website/assets", out_filename)
]

for p in out_paths:
    os.makedirs(os.path.dirname(p), exist_ok=True)
    img.save(p, "PNG")
    print(f"Generated hero diagram: {p} ({W}x{H})")
