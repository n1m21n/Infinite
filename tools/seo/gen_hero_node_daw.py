#!/usr/bin/env python3
"""Generates the 1200x675 technical hero diagram for node-based-daw-modular-audio-routing.
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
CYAN = (56, 189, 248, 255)

# Fonts
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
font_mono_path = os.path.join(ROOT, "art/brand/fonts/GeistMono-Medium.ttf")
font_sans_path = os.path.join(ROOT, "art/brand/fonts/Geist-SemiBold.ttf")

if not os.path.exists(font_mono_path):
    font_mono_path = "/System/Library/Fonts/Menlo.ttc"
if not os.path.exists(font_sans_path):
    font_sans_path = "/System/Library/Fonts/Helvetica.ttc"

f_title = ImageFont.truetype(font_sans_path, 20)
f_sub = ImageFont.truetype(font_mono_path, 12)
f_card_title = ImageFont.truetype(font_sans_path, 13)
f_badge = ImageFont.truetype(font_mono_path, 11)
f_code = ImageFont.truetype(font_mono_path, 10)
f_label = ImageFont.truetype(font_mono_path, 11)
f_small = ImageFont.truetype(font_mono_path, 10)
f_tiny = ImageFont.truetype(font_mono_path, 9)

# 1. Background subtle dot grid
for gx in range(24, W, 24):
    for gy in range(24, H, 24):
        draw.point((gx, gy), fill=DOT)

# Outer decorative frame
draw.rounded_rectangle([16, 16, W - 16, H - 16], radius=12, outline=LINE, width=1)

# Top Bar
draw.text((45, 34), "INFINITE", font=f_title, fill=INK)
draw.text((145, 40), "//  NODE-BASED DAW: MODULAR AUDIO, MIDI & VISUAL ROUTING", font=f_sub, fill=SIGNAL)

badge_text = "SAMPLE-ACCURATE GRAPH  •  CROSS-MODULATION  •  ZERO AUX BUSES  •  INTEGRATED SHADERS"
bw = draw.textlength(badge_text, font=f_badge) + 20
draw.rounded_rectangle((W - 45 - bw, 34, W - 45, 60), 6, fill=SURF1, outline=LINE, width=1)
draw.text((W - 45 - bw + 10, 40), badge_text, font=f_badge, fill=INK2)

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
# COLUMN 1: Note Sequencer & Generative Arpeggiator (x=45, w=245)
# -------------------------------------------------------------
c1_x, c1_w = 45, 245

# Card 1A: Note Sequencer
c1a_y, c1a_h = 80, 260
draw_card(c1_x, c1a_y, c1_w, c1a_h, "Note Sequencer", "NOTE // GENERATOR", "16 STEPS")

# Step sequencer matrix
seq_box_y = c1a_y + 44
draw.rounded_rectangle((c1_x + 10, seq_box_y, c1_x + c1_w - 10, seq_box_y + 115), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c1_x + 16, seq_box_y + 6), "POLYPHONIC PITCH GRID (D MINOR)", font=f_tiny, fill=INK3)

grid_steps = 16
pitches = [12, 10, 8, 7, 5, 3, 0]  # 7 rows
active_cells = {
    (0, 0), (2, 3), (3, 5), (4, 2), (6, 7), (7, 5), (8, 3), (10, 8),
    (11, 7), (12, 5), (14, 10), (15, 12)
}
cell_w = (c1_w - 36) / 16
for s in range(grid_steps):
    for r in range(5):
        cell_x1 = c1_x + 16 + s * cell_w
        cell_y1 = seq_box_y + 24 + r * 16
        cell_x2 = cell_x1 + cell_w - 2
        cell_y2 = cell_y1 + 13
        is_hit = (s, r) in [(0, 4), (2, 2), (4, 3), (6, 1), (8, 4), (10, 0), (12, 2), (14, 1)]
        fill_col = SIGNAL if is_hit else (22, 28, 52)
        draw.rectangle((cell_x1, cell_y1, cell_x2, cell_y2), fill=fill_col)

# Gate division info
draw.rounded_rectangle((c1_x + 10, c1a_y + 170, c1_x + c1_w - 10, c1a_y + 245), 6, fill=SURF2, outline=SURF3, width=1)
draw.text((c1_x + 16, c1a_y + 178), "CLOCK DIV // 1/16 SWING 54%", font=f_tiny, fill=INK2)
draw.text((c1_x + 16, c1a_y + 196), "GATE LENGTH // 85% TIE=ON", font=f_tiny, fill=INK3)
draw.text((c1_x + 16, c1a_y + 214), "VELOCITY // PROBABILISTIC 90%", font=f_tiny, fill=GREEN)

# Output pin 1A
c1a_out_note = (c1_x + c1_w - 8, c1a_y + 75)
draw.ellipse((c1a_out_note[0]-4, c1a_out_note[1]-4, c1a_out_note[0]+4, c1a_out_note[1]+4), fill=DEEP, outline=SIGNAL, width=2)
c1a_out_gate = (c1_x + c1_w - 8, c1a_y + 115)
draw.ellipse((c1a_out_gate[0]-4, c1a_out_gate[1]-4, c1a_out_gate[0]+4, c1a_out_gate[1]+4), fill=DEEP, outline=GREEN, width=2)

# Card 1B: Random Note Generator
c1b_y, c1b_h = 355, 275
draw_card(c1_x, c1b_y, c1_w, c1b_h, "Random Note", "NOTE // GENERATIVE", "MARKOV")

draw.rounded_rectangle((c1_x + 10, c1b_y + 44, c1_x + c1_w - 10, c1b_y + 160), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c1_x + 16, c1b_y + 52), "MARKOV DRIFT PROBABILITY", font=f_tiny, fill=INK3)

# Markov transition tree nodes
for mi in range(4):
    mx = c1_x + 30 + mi * 48
    my = c1b_y + 105 + (18 if mi % 2 == 1 else -18)
    draw.ellipse((mx - 10, my - 10, mx + 10, my + 10), fill=SURF2, outline=SIGNAL, width=1)
    draw.text((mx - 5, my - 6), str(mi + 1), font=f_tiny, fill=INK)
    if mi > 0:
        prev_mx = c1_x + 30 + (mi - 1) * 48
        prev_my = c1b_y + 105 + (18 if (mi - 1) % 2 == 1 else -18)
        draw.line((prev_mx + 10, prev_my, mx - 10, my), fill=LINE, width=1)

draw.rounded_rectangle((c1_x + 10, c1b_y + 175, c1_x + c1_w - 10, c1b_y + 260), 6, fill=SURF2, outline=SURF3, width=1)
draw.text((c1_x + 16, c1b_y + 185), "SCALE // DORIAN ROOT: D3", font=f_tiny, fill=INK2)
draw.text((c1_x + 16, c1b_y + 205), "CHANCE // 72% DENSITY", font=f_tiny, fill=INK3)
draw.text((c1_x + 16, c1b_y + 225), "OCTAVE SPREAD // +/- 2 OCT", font=f_tiny, fill=SIGNAL)

c1b_out_note = (c1_x + c1_w - 8, c1b_y + 80)
draw.ellipse((c1b_out_note[0]-4, c1b_out_note[1]-4, c1b_out_note[0]+4, c1b_out_note[1]+4), fill=DEEP, outline=SIGNAL, width=2)

# -------------------------------------------------------------
# COLUMN 2: Synthesisers & Sound Generators (x=325, w=275)
# -------------------------------------------------------------
c2_x, c2_w = 325, 275

# Card 2A: Analog Synth Node
c2a_y, c2a_h = 80, 260
draw_card(c2_x, c2a_y, c2_w, c2a_h, "Analog", "SYNTH // DUAL VCO", "POLY 8")

# Oscilloscope waveform display
draw.rounded_rectangle((c2_x + 10, c2a_y + 44, c2_x + c2_w - 10, c2a_y + 115), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c2_x + 16, c2a_y + 50), "VCO1 (SAW) + VCO2 (PULSE 40%)", font=f_tiny, fill=INK3)

vco_pts = []
for i in range(c2_w - 32):
    vx = c2_x + 16 + i
    t = (i % 38) / 38.0
    saw = (t * 2.0 - 1.0) * 12
    pulse = (12 if t < 0.4 else -12) * 0.6
    vy = c2a_y + 85 + (saw + pulse) * 0.7
    vco_pts.append((vx, vy))
draw.line(vco_pts, fill=EMBER, width=2)

# Filter & envelope knobs / sliders
draw.rounded_rectangle((c2_x + 10, c2a_y + 125, c2_x + c2_w - 10, c2a_y + 245), 6, fill=SURF2, outline=SURF3, width=1)
draw.text((c2_x + 16, c2a_y + 135), "4-POLE LADDER FILTER (24 dB/oct)", font=f_tiny, fill=INK)

# Cutoff slider
draw.text((c2_x + 16, c2a_y + 155), "CUTOFF // 840 Hz", font=f_tiny, fill=INK2)
draw.rounded_rectangle((c2_x + 16, c2a_y + 170, c2_x + c2_w - 30, c2a_y + 176), 3, fill=DEEP)
draw.rounded_rectangle((c2_x + 16, c2a_y + 170, c2_x + 140, c2a_y + 176), 3, fill=EMBER)
draw.ellipse((c2_x + 136, c2a_y + 168, c2_x + 144, c2a_y + 178), fill=INK)

# Resonance & Drive
draw.text((c2_x + 16, c2a_y + 190), "RESONANCE // 3.4 Q", font=f_tiny, fill=INK3)
draw.text((c2_x + 16, c2a_y + 210), "DRIVE // +4.2 dB ASYMMETRIC", font=f_tiny, fill=INK3)
draw.text((c2_x + 16, c2a_y + 230), "PORTAMENTO // 35 ms GLIDE", font=f_tiny, fill=INK3)

# Inputs and outputs 2A
c2a_in_note = (c2_x + 8, c2a_y + 70)
draw.ellipse((c2a_in_note[0]-4, c2a_in_note[1]-4, c2a_in_note[0]+4, c2a_in_note[1]+4), fill=DEEP, outline=SIGNAL, width=2)
c2a_in_mod_cutoff = (c2_x + 8, c2a_y + 173)
draw.ellipse((c2a_in_mod_cutoff[0]-4, c2a_in_mod_cutoff[1]-4, c2a_in_mod_cutoff[0]+4, c2a_in_mod_cutoff[1]+4), fill=DEEP, outline=EMBER, width=2)

c2a_out_audio = (c2_x + c2_w - 8, c2a_y + 100)
draw.ellipse((c2a_out_audio[0]-4, c2a_out_audio[1]-4, c2a_out_audio[0]+4, c2a_out_audio[1]+4), fill=DEEP, outline=EMBER, width=2)

# Card 2B: Plugin (VST3 / AU Host)
c2b_y, c2b_h = 355, 275
draw_card(c2_x, c2b_y, c2_w, c2b_h, "Plugin", "AUDIO // VST3 HOST", "SURGE XT")

draw.rounded_rectangle((c2_x + 10, c2b_y + 44, c2_x + c2_w - 10, c2b_y + 140), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c2_x + 16, c2b_y + 52), "HOSTED VST3 INSTRUMENT", font=f_tiny, fill=SIGNAL)
draw.text((c2_x + 16, c2b_y + 70), "Plugin: Surge XT (Polyphonic)", font=f_tiny, fill=INK)
draw.text((c2_x + 16, c2b_y + 88), "Latency: 0 samples (zero PDC)", font=f_tiny, fill=INK3)
draw.text((c2_x + 16, c2b_y + 106), "Exposed Automatable Params: 48", font=f_tiny, fill=GREEN)
draw.text((c2_x + 16, c2b_y + 124), "Format: VST3 64-bit Native C++", font=f_tiny, fill=INK2)

# VST3 Modulatable Pins
draw.rounded_rectangle((c2_x + 10, c2b_y + 152, c2_x + c2_w - 10, c2b_y + 260), 6, fill=SURF2, outline=SURF3, width=1)
draw.text((c2_x + 16, c2b_y + 162), "MODULATABLE HOST PARAMETERS", font=f_tiny, fill=INK)
draw.text((c2_x + 16, c2b_y + 184), "• param: Filter Cutoff [0-1]", font=f_tiny, fill=EMBER)
draw.text((c2_x + 16, c2b_y + 204), "• param: Morph Wavetable [0-1]", font=f_tiny, fill=SIGNAL)
draw.text((c2_x + 16, c2b_y + 224), "• param: Reverb Feedback [0-1]", font=f_tiny, fill=INK2)
draw.text((c2_x + 16, c2b_y + 244), "• param: Chorus Depth [0-1]", font=f_tiny, fill=INK3)

c2b_in_note = (c2_x + 8, c2b_y + 70)
draw.ellipse((c2b_in_note[0]-4, c2b_in_note[1]-4, c2b_in_note[0]+4, c2b_in_note[1]+4), fill=DEEP, outline=SIGNAL, width=2)
c2b_in_morph = (c2_x + 8, c2b_y + 204)
draw.ellipse((c2b_in_morph[0]-4, c2b_in_morph[1]-4, c2b_in_morph[0]+4, c2b_in_morph[1]+4), fill=DEEP, outline=SIGNAL, width=2)

c2b_out_audio = (c2_x + c2_w - 8, c2b_y + 80)
draw.ellipse((c2b_out_audio[0]-4, c2b_out_audio[1]-4, c2b_out_audio[0]+4, c2b_out_audio[1]+4), fill=DEEP, outline=EMBER, width=2)

# -------------------------------------------------------------
# COLUMN 3: Modulators & Audio Effects (x=635, w=250)
# -------------------------------------------------------------
c3_x, c3_w = 635, 250

# Card 3A: LFO & Multi-Segment Envelope
c3a_y, c3a_h = 80, 260
draw_card(c3_x, c3a_y, c3_w, c3a_h, "LFO & MSEG", "MODULATOR // TEMPO", "1/4 SYNC")

# LFO waveform display
draw.rounded_rectangle((c3_x + 10, c3a_y + 44, c3_x + c3_w - 10, c3a_y + 115), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c3_x + 16, c3a_y + 50), "LFO 1 // SINE + TRIANGLE 0.25 Hz", font=f_tiny, fill=INK3)

lfo_pts = []
for i in range(c3_w - 32):
    lx = c3_x + 16 + i
    ly = c3a_y + 85 + math.sin(i * 0.12) * 16
    lfo_pts.append((lx, ly))
draw.line(lfo_pts, fill=SIGNAL, width=2)

# MSEG curve display
draw.rounded_rectangle((c3_x + 10, c3a_y + 125, c3_x + c3_w - 10, c3a_y + 195), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c3_x + 16, c3a_y + 133), "MSEG // 6-NODE BEZIER ENVELOPE", font=f_tiny, fill=INK3)

mseg_pts = [
    (c3_x + 16, c3a_y + 185),
    (c3_x + 40, c3a_y + 150),
    (c3_x + 80, c3a_y + 165),
    (c3_x + 120, c3a_y + 145),
    (c3_x + 180, c3a_y + 180),
    (c3_x + c3_w - 16, c3a_y + 188)
]
draw.line(mseg_pts, fill=EMBER, width=2)
for mx, my in mseg_pts:
    draw.ellipse((mx - 3, my - 3, mx + 3, my + 3), fill=INK)

# Mod outputs info
draw.rounded_rectangle((c3_x + 10, c3a_y + 205, c3_x + c3_w - 10, c3a_y + 245), 6, fill=SURF2, outline=SURF3, width=1)
draw.text((c3_x + 16, c3a_y + 215), "CROSS-MOD RATE // POLAR SPREAD", font=f_tiny, fill=INK2)

c3a_out_lfo = (c3_x + c3_w - 8, c3a_y + 85)
draw.ellipse((c3a_out_lfo[0]-4, c3a_out_lfo[1]-4, c3a_out_lfo[0]+4, c3a_out_lfo[1]+4), fill=DEEP, outline=SIGNAL, width=2)
c3a_out_mseg = (c3_x + c3_w - 8, c3a_y + 165)
draw.ellipse((c3a_out_mseg[0]-4, c3a_out_mseg[1]-4, c3a_out_mseg[0]+4, c3a_out_mseg[1]+4), fill=DEEP, outline=EMBER, width=2)

# Card 3B: Field Effect (DSP Delay & Saturation)
c3b_y, c3b_h = 355, 275
draw_card(c3_x, c3b_y, c3_w, c3b_h, "Field Effect", "DSP // AUDIO KERNEL", "C++ JIT")

draw.rounded_rectangle((c3_x + 10, c3b_y + 44, c3_x + c3_w - 10, c3b_y + 180), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c3_x + 16, c3b_y + 52), "delay_kernel.field  ->  Sample DSP", font=f_tiny, fill=INK2)

dsp_code = [
    ("1", "param float time = 0.35 [0, 1];", SIGNAL),
    ("2", "param float feedback = 0.65;", EMBER),
    ("3", "state float buf[48000];", INK3),
    ("4", "float delayed = buf[t_tap];", INK),
    ("5", "float sat = tanh(in + delayed * feedback);", EMBER),
    ("6", "buf[write_ptr] = sat;", INK2),
    ("7", "out = mix(in, sat, 0.45);", GREEN)
]
cy = c3b_y + 70
for num, line, col in dsp_code:
    draw.text((c3_x + 16, cy), num, font=f_code, fill=INK3)
    draw.text((c3_x + 32, cy), line, font=f_code, fill=col)
    cy += 15

draw.rounded_rectangle((c3_x + 10, c3b_y + 190, c3_x + c3_w - 10, c3b_y + 260), 6, fill=SURF2, outline=SURF3, width=1)
draw.text((c3_x + 16, c3b_y + 200), "BUFFER // 48,000 SAMPLES (1.0 SEC)", font=f_tiny, fill=INK)
draw.text((c3_x + 16, c3b_y + 218), "CPU LOAD // 0.04% PER INSTANCE", font=f_tiny, fill=GREEN)
draw.text((c3_x + 16, c3b_y + 236), "MODULATABLE TAPS // PING-PONG", font=f_tiny, fill=SIGNAL)

c3b_in_audio = (c3_x + 8, c3b_y + 80)
draw.ellipse((c3b_in_audio[0]-4, c3b_in_audio[1]-4, c3b_in_audio[0]+4, c3b_in_audio[1]+4), fill=DEEP, outline=EMBER, width=2)
c3b_in_mod_fb = (c3_x + 8, c3b_y + 140)
draw.ellipse((c3b_in_mod_fb[0]-4, c3b_in_mod_fb[1]-4, c3b_in_mod_fb[0]+4, c3b_in_mod_fb[1]+4), fill=DEEP, outline=SIGNAL, width=2)

c3b_out_audio = (c3_x + c3_w - 8, c3b_y + 90)
draw.ellipse((c3b_out_audio[0]-4, c3b_out_audio[1]-4, c3b_out_audio[0]+4, c3b_out_audio[1]+4), fill=DEEP, outline=EMBER, width=2)

# -------------------------------------------------------------
# COLUMN 4: Visual Canvas & Audio Master Output (x=920, w=235)
# -------------------------------------------------------------
c4_x, c4_w = 920, 235

# Card 4A: FieldPixel Node (Direct Audio-to-Visual)
c4a_y, c4a_h = 80, 260
draw_card(c4_x, c4a_y, c4_w, c4a_h, "FieldPixel", "GPU // REAL-TIME GLSL", "60 FPS")

# Shader preview viewport
vp_x, vp_y = c4_x + 10, c4a_y + 44
vp_w, vp_h = c4_w - 20, 115
draw.rounded_rectangle((vp_x, vp_y, vp_x + vp_w, vp_y + vp_h), 6, fill=DEEP, outline=SURF3, width=1)

# Render generative rings & rays
cx, cy_vp = vp_x + vp_w // 2, vp_y + vp_h // 2
for rad in [14, 26, 38, 50]:
    draw.ellipse((cx - rad, cy_vp - rad, cx + rad, cy_vp + rad), outline=SIGNAL, width=1)
draw.ellipse((cx - 8, cy_vp - 8, cx + 8, cy_vp + 8), fill=EMBER)

for angle in range(0, 360, 45):
    rad_ang = math.radians(angle)
    x1 = cx + math.cos(rad_ang) * 10
    y1 = cy_vp + math.sin(rad_ang) * 10
    x2 = cx + math.cos(rad_ang) * 48
    y2 = cy_vp + math.sin(rad_ang) * 48
    draw.line([(x1, y1), (x2, y2)], fill=SIGNAL, width=1)

draw.rounded_rectangle((c4_x + 10, c4a_y + 170, c4_x + c4_w - 10, c4a_y + 245), 6, fill=SURF2, outline=SURF3, width=1)
draw.text((c4_x + 16, c4a_y + 180), "LIVE AUDIO PARAMETER LINK", font=f_tiny, fill=EMBER)
draw.text((c4_x + 16, c4a_y + 198), "• synth_env -> tunnel_depth", font=f_tiny, fill=INK2)
draw.text((c4_x + 16, c4a_y + 216), "• lfo_sine   -> ring_rotation", font=f_tiny, fill=SIGNAL)
draw.text((c4_x + 16, c4a_y + 232), "• master_rms -> bloom_glow", font=f_tiny, fill=GREEN)

c4a_in_mod1 = (c4_x + 8, c4a_y + 198)
draw.ellipse((c4a_in_mod1[0]-4, c4a_in_mod1[1]-4, c4a_in_mod1[0]+4, c4a_in_mod1[1]+4), fill=DEEP, outline=EMBER, width=2)
c4a_in_mod2 = (c4_x + 8, c4a_y + 216)
draw.ellipse((c4a_in_mod2[0]-4, c4a_in_mod2[1]-4, c4a_in_mod2[0]+4, c4a_in_mod2[1]+4), fill=DEEP, outline=SIGNAL, width=2)

# Card 4B: Audio Out (DAC Master)
c4b_y, c4b_h = 355, 275
draw_card(c4_x, c4b_y, c4_w, c4b_h, "Audio Out", "HARDWARE // MASTER", "48 kHz")

draw.rounded_rectangle((c4_x + 10, c4b_y + 44, c4_x + c4_w - 10, c4b_y + 140), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c4_x + 16, c4b_y + 52), "STEREO MASTER VU METERS", font=f_tiny, fill=INK3)

# VU meter bars
vu_w = c4_w - 44
draw.text((c4_x + 16, c4b_y + 70), "L", font=f_tiny, fill=INK2)
draw.rounded_rectangle((c4_x + 30, c4b_y + 72, c4_x + 30 + vu_w, c4b_y + 82), 3, fill=GROUND)
draw.rounded_rectangle((c4_x + 30, c4b_y + 72, c4_x + 30 + int(vu_w * 0.82), c4b_y + 82), 3, fill=GREEN)

draw.text((c4_x + 16, c4b_y + 92), "R", font=f_tiny, fill=INK2)
draw.rounded_rectangle((c4_x + 30, c4b_y + 94, c4_x + 30 + vu_w, c4b_y + 104), 3, fill=GROUND)
draw.rounded_rectangle((c4_x + 30, c4b_y + 94, c4_x + 30 + int(vu_w * 0.79), c4b_y + 104), 3, fill=GREEN)

draw.text((c4_x + 16, c4b_y + 118), "PEAK // -2.4 dBFS (TRUE PEAK)", font=f_tiny, fill=GREEN)

draw.rounded_rectangle((c4_x + 10, c4b_y + 152, c4_x + c4_w - 10, c4b_y + 260), 6, fill=SURF2, outline=SURF3, width=1)
draw.text((c4_x + 16, c4b_y + 162), "AUDIO ENGINE TELEMETRY", font=f_tiny, fill=INK)
draw.text((c4_x + 16, c4b_y + 184), "• Driver: CoreAudio / ASIO / JACK", font=f_tiny, fill=INK2)
draw.text((c4_x + 16, c4b_y + 204), "• Buffer: 128 samples (2.6 ms)", font=f_tiny, fill=SIGNAL)
draw.text((c4_x + 16, c4b_y + 224), "• Precision: 32-bit float audio", font=f_tiny, fill=INK3)
draw.text((c4_x + 16, c4b_y + 244), "• Dropouts / xruns: 0", font=f_tiny, fill=GREEN)

c4b_in_audio = (c4_x + 8, c4b_y + 78)
draw.ellipse((c4b_in_audio[0]-4, c4b_in_audio[1]-4, c4b_in_audio[0]+4, c4b_in_audio[1]+4), fill=DEEP, outline=EMBER, width=2)

# -------------------------------------------------------------
# Smooth Bezier Cables
# -------------------------------------------------------------
def draw_cable(p1, p2, color, width=2):
    x1, y1 = p1
    x2, y2 = p2
    dx = max(40, abs(x2 - x1) * 0.5)
    pts = []
    steps = 40
    for s in range(steps + 1):
        t = s / float(steps)
        omt = 1.0 - t
        bx = omt**3 * x1 + 3 * omt**2 * t * (x1 + dx) + 3 * omt * t**2 * (x2 - dx) + t**3 * x2
        by = omt**3 * y1 + 3 * omt**2 * t * y1 + 3 * omt * t**2 * y2 + t**3 * y2
        pts.append((bx, by))
    draw.line(pts, fill=color, width=width)

# Cable 1: Sequencer MIDI note -> Analog Synth note in
draw_cable(c1a_out_note, c2a_in_note, SIGNAL, width=2)

# Cable 2: Random Note Generator -> Plugin VST3 note in
draw_cable(c1b_out_note, c2b_in_note, SIGNAL, width=2)

# Cable 3: LFO output -> Analog cutoff modulation
draw_cable(c3a_out_lfo, c2a_in_mod_cutoff, EMBER, width=2)

# Cable 4: MSEG envelope output -> Plugin morph wavetable modulation
draw_cable(c3a_out_mseg, c2b_in_morph, SIGNAL, width=2)

# Cable 5: Analog Audio Out -> Field Effect Audio In
draw_cable(c2a_out_audio, c3b_in_audio, EMBER, width=3)

# Cable 6: Plugin Audio Out -> Field Effect Audio In (summing node)
draw_cable(c2b_out_audio, (c3b_in_audio[0], c3b_in_audio[1] + 15), EMBER, width=3)

# Cable 7: Field Effect Audio Out -> Audio Out (Master DAC)
draw_cable(c3b_out_audio, c4b_in_audio, EMBER, width=3)

# Cable 8: Cross-modulation: MSEG envelope -> FieldPixel shader depth modulation
draw_cable(c3a_out_mseg, c4a_in_mod1, EMBER, width=2)

# Cable 9: LFO sine -> FieldPixel rotation modulation
draw_cable(c3a_out_lfo, c4a_in_mod2, SIGNAL, width=2)

# Save to destination
out_filename = "blog_node-based-daw-modular-audio-routing.png"
out_paths = [
    os.path.join(ROOT, "website", "assets", out_filename),
]
alt_root = "/Users/namansoni/Infinite/worktrees/main" if "repo" in ROOT else "/Users/namansoni/Infinite/repo"
out_paths.append(os.path.join(alt_root, "website", "assets", out_filename))

for p in out_paths:
    os.makedirs(os.path.dirname(p), exist_ok=True)
    img.save(p, "PNG")
    print(f"Saved: {p}")
