#!/usr/bin/env python3
"""Generates the 1200x675 technical hero diagram for modular-vst3-host-audio-routing-and-modulation.
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

f_title = ImageFont.truetype(font_sans_path, 19)
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
draw.rounded_rectangle([16, 16, W - 16, H - 16], radius=12, outline=LINE, width=1)

# Top Bar
draw.text((42, 34), "INFINITE", font=f_title, fill=INK)
draw.text((142, 39), "//  MODULAR VST3 HOST: PLUGIN ROUTING, PARAMETER AUTOMATION & CRASH ISOLATION", font=f_sub, fill=SIGNAL)

badge_text = "NATIVE VST3 & AU  •  SAMPLE-ACCURATE GRAPH  •  LOCK-FREE AUDIO DISPATCH  •  CRASH GUARD"
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
    draw.text((x + 12, y + 7), title, font=f_card_title, fill=INK)
    tw = draw.textlength(title, font=f_card_title)
    draw.text((x + 12 + tw + 8, y + 9), category, font=f_small, fill=INK3)
    if badge:
        bbw = draw.textlength(badge, font=f_tiny) + 10
        draw.rounded_rectangle((x + w - 10 - bbw, y + 6, x + w - 10, y + 24), 4, fill=SURF3)
        draw.text((x + w - 10 - bbw + 5, y + 8), badge, font=f_tiny, fill=SIGNAL)

# -------------------------------------------------------------
# COLUMN 1: Note Sequencer (x=45, w=240)
# -------------------------------------------------------------
c1_x, c1_w = 45, 240
c1a_y, c1a_h = 76, 260
draw_card(c1_x, c1a_y, c1_w, c1a_h, "Note Sequencer", "NOTE // GENERATOR", "16 STEPS")

# Pitch matrix preview
seq_box_y = c1a_y + 40
draw.rounded_rectangle((c1_x + 10, seq_box_y, c1_x + c1_w - 10, seq_box_y + 110), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c1_x + 16, seq_box_y + 6), "POLYPHONIC NOTE EVENTS (FRAME-OFFSET)", font=f_tiny, fill=INK3)

grid_steps = 16
pitches = [12, 10, 8, 7, 5, 3, 0]
active_cells = {
    (0, 0), (2, 3), (4, 5), (6, 2), (8, 7), (9, 5), (10, 3), (12, 8),
    (13, 7), (14, 5), (15, 10)
}
cell_w = (c1_w - 36) / 16
for s in range(grid_steps):
    for r_idx, p in enumerate(pitches):
        px = c1_x + 18 + s * cell_w
        py = seq_box_y + 24 + r_idx * 11
        if (s, p) in active_cells:
            draw.rectangle((px, py, px + cell_w - 2, py + 8), fill=CYAN)
        else:
            draw.rectangle((px, py, px + cell_w - 2, py + 8), fill=SURF2)

# Parameters block
draw.rounded_rectangle((c1_x + 10, c1a_y + 160, c1_x + c1_w - 10, c1a_y + 246), 6, fill=SURF2, outline=SURF3, width=1)
draw.text((c1_x + 16, c1a_y + 168), "DIVISION // 1/16 NOTES", font=f_small, fill=INK2)
draw.text((c1_x + 16, c1a_y + 186), "GATE     // 78% LEGATO", font=f_small, fill=INK2)
draw.text((c1_x + 16, c1a_y + 204), "HUMANIZE // 4.2 ms JITTER", font=f_small, fill=INK3)
draw.text((c1_x + 16, c1a_y + 222), "MIDI CH  // 1 (OMNI)", font=f_small, fill=CYAN)

c1a_out_note = (c1_x + c1_w - 8, c1a_y + 105)
draw.ellipse((c1a_out_note[0]-4, c1a_out_note[1]-4, c1a_out_note[0]+4, c1a_out_note[1]+4), fill=DEEP, outline=CYAN, width=2)
draw.text((c1_x + c1_w - 46, c1a_y + 100), "notes", font=f_tiny, fill=CYAN)

# -------------------------------------------------------------
# Card 1B: LFO Modulator (x=45, y=354, w=240, h=160)
# -------------------------------------------------------------
c1b_y, c1b_h = 352, 160
draw_card(c1_x, c1b_y, c1_w, c1b_h, "LFO", "MODULATOR // CV", "TEMPO SYNC")

lfo_box_y = c1b_y + 40
draw.rounded_rectangle((c1_x + 10, lfo_box_y, c1_x + c1_w - 10, lfo_box_y + 60), 6, fill=DEEP, outline=SURF3, width=1)
lfo_pts = []
for lx in range(c1_w - 28):
    ang = (lx / (c1_w - 28)) * 4 * math.pi
    ly = (lfo_box_y + 30) - math.sin(ang) * 18
    lfo_pts.append((c1_x + 14 + lx, ly))
draw.line(lfo_pts, fill=EMBER, width=2)

draw.text((c1_x + 16, c1b_y + 110), "RATE     // 1/4 BAR (SYNC)", font=f_small, fill=INK2)
draw.text((c1_x + 16, c1b_y + 128), "SHAPE    // ANALOGUE SINE", font=f_small, fill=EMBER)
draw.text((c1_x + 16, c1b_y + 142), "RANGE    // 0.00 -> 1.00", font=f_small, fill=INK3)

c1b_out_cv = (c1_x + c1_w - 8, c1b_y + 80)
draw.ellipse((c1b_out_cv[0]-4, c1b_out_cv[1]-4, c1b_out_cv[0]+4, c1b_out_cv[1]+4), fill=DEEP, outline=EMBER, width=2)
draw.text((c1_x + c1_w - 32, c1b_y + 75), "cv", font=f_tiny, fill=EMBER)

# -------------------------------------------------------------
# COLUMN 2: Plugin [Instrument] (x=330, w=275)
# -------------------------------------------------------------
c2_x, c2_w = 330, 275
c2_y, c2_h = 76, 436
draw_card(c2_x, c2_y, c2_w, c2_h, "Plugin", "VST3 // INSTRUMENT", "DIVA SYNTH")

# Native plugin info bar
pinfo_y = c2_y + 38
draw.rounded_rectangle((c2_x + 10, pinfo_y, c2_x + c2_w - 10, pinfo_y + 40), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c2_x + 16, pinfo_y + 6), "ID: com.u-he.diva (64-BIT VST3)", font=f_tiny, fill=INK2)
draw.text((c2_x + 16, pinfo_y + 22), "LATENCY: 0 SAMPLES  •  OUT: STEREO", font=f_tiny, fill=GREEN)

# Inputs block
draw.text((c2_x + 16, c2_y + 88), "INPUT SIGNALS", font=f_tiny, fill=INK3)
c2_in_note = (c2_x + 8, c2_y + 105)
draw.ellipse((c2_in_note[0]-4, c2_in_note[1]-4, c2_in_note[0]+4, c2_in_note[1]+4), fill=DEEP, outline=CYAN, width=2)
draw.text((c2_x + 18, c2_y + 101), "notes (MIDI queue)", font=f_tiny, fill=CYAN)

c2_in_sidechain = (c2_x + 8, c2_y + 125)
draw.ellipse((c2_in_sidechain[0]-4, c2_in_sidechain[1]-4, c2_in_sidechain[0]+4, c2_in_sidechain[1]+4), fill=DEEP, outline=INK3, width=2)
draw.text((c2_x + 18, c2_y + 121), "sidechain (stereo)", font=f_tiny, fill=INK3)

# Mapped Parameters Header
draw.line((c2_x + 10, c2_y + 146, c2_x + c2_w - 10, c2_y + 146), fill=SURF3, width=1)
draw.text((c2_x + 16, c2_y + 154), "MAPPED VST3 PARAMETERS (32 POSITIONAL SLOTS)", font=f_tiny, fill=SIGNAL)

# Param 1: Cutoff (with modulation pin!)
p1_y = c2_y + 172
draw.text((c2_x + 22, p1_y), "0: Filter Cutoff", font=f_small, fill=INK)
draw.text((c2_x + c2_w - 56, p1_y), "2.4 kHz", font=f_small, fill=EMBER)
draw.rounded_rectangle((c2_x + 22, p1_y + 14, c2_x + c2_w - 14, p1_y + 22), 3, fill=GROUND)
draw.rounded_rectangle((c2_x + 22, p1_y + 14, c2_x + 22 + int((c2_w - 36) * 0.62), p1_y + 22), 3, fill=EMBER)
c2_mod_cutoff = (c2_x + 8, p1_y + 16)
draw.ellipse((c2_mod_cutoff[0]-4, c2_mod_cutoff[1]-4, c2_mod_cutoff[0]+4, c2_mod_cutoff[1]+4), fill=DEEP, outline=EMBER, width=2)

# Param 2: Resonance
p2_y = c2_y + 214
draw.text((c2_x + 22, p2_y), "1: Resonance", font=f_small, fill=INK)
draw.text((c2_x + c2_w - 50, p2_y), "48%", font=f_small, fill=SIGNAL)
draw.rounded_rectangle((c2_x + 22, p2_y + 14, c2_x + c2_w - 14, p2_y + 22), 3, fill=GROUND)
draw.rounded_rectangle((c2_x + 22, p2_y + 14, c2_x + 22 + int((c2_w - 36) * 0.48), p2_y + 22), 3, fill=SIGNAL)
c2_mod_res = (c2_x + 8, p2_y + 16)
draw.ellipse((c2_mod_res[0]-4, c2_mod_res[1]-4, c2_mod_res[0]+4, c2_mod_res[1]+4), fill=DEEP, outline=SIGNAL, width=2)

# Param 3: Oscillator Detune
p3_y = c2_y + 256
draw.text((c2_x + 22, p3_y), "2: Osc Detune / Spread", font=f_small, fill=INK)
draw.text((c2_x + c2_w - 50, p3_y), "18 ct", font=f_small, fill=INK2)
draw.rounded_rectangle((c2_x + 22, p3_y + 14, c2_x + c2_w - 14, p3_y + 22), 3, fill=GROUND)
draw.rounded_rectangle((c2_x + 22, p3_y + 14, c2_x + 22 + int((c2_w - 36) * 0.32), p3_y + 22), 3, fill=SURF3)
c2_mod_detune = (c2_x + 8, p3_y + 16)
draw.ellipse((c2_mod_detune[0]-4, c2_mod_detune[1]-4, c2_mod_detune[0]+4, c2_mod_detune[1]+4), fill=DEEP, outline=LINE, width=2)

# Param 4: Envelope Decay
p4_y = c2_y + 298
draw.text((c2_x + 22, p4_y), "3: VCF Decay Time", font=f_small, fill=INK)
draw.text((c2_x + c2_w - 56, p4_y), "620 ms", font=f_small, fill=INK2)
draw.rounded_rectangle((c2_x + 22, p4_y + 14, c2_x + c2_w - 14, p4_y + 22), 3, fill=GROUND)
draw.rounded_rectangle((c2_x + 22, p4_y + 14, c2_x + 22 + int((c2_w - 36) * 0.55), p4_y + 22), 3, fill=SURF3)
c2_mod_decay = (c2_x + 8, p4_y + 16)
draw.ellipse((c2_mod_decay[0]-4, c2_mod_decay[1]-4, c2_mod_decay[0]+4, c2_mod_decay[1]+4), fill=DEEP, outline=LINE, width=2)

# Touch-to-learn configure badge
draw.rounded_rectangle((c2_x + 14, c2_y + 348, c2_x + c2_w - 14, c2_y + 382), 6, fill=SURF2, outline=SURF3, width=1)
draw.text((c2_x + 24, c2_y + 356), "[+] TOUCH-TO-LEARN CONFIGURE ON", font=f_tiny, fill=GREEN)
draw.text((c2_x + 24, c2_y + 368), "Touch any plugin knob to map parameter slot", font=f_tiny, fill=INK3)

# Base64 state badge
draw.rounded_rectangle((c2_x + 14, c2_y + 392, c2_x + c2_w - 14, c2_y + 422), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c2_x + 22, c2_y + 398), "STATE: Base64 chunk serialized in .inf", font=f_tiny, fill=INK2)
draw.text((c2_x + 22, c2_y + 409), "Crash status: 0 faults recorded (healthy)", font=f_tiny, fill=GREEN)

# Audio Outputs
c2_out_audio_l = (c2_x + c2_w - 8, c2_y + 200)
draw.ellipse((c2_out_audio_l[0]-4, c2_out_audio_l[1]-4, c2_out_audio_l[0]+4, c2_out_audio_l[1]+4), fill=DEEP, outline=SIGNAL, width=2)
c2_out_audio_r = (c2_x + c2_w - 8, c2_y + 218)
draw.ellipse((c2_out_audio_r[0]-4, c2_out_audio_r[1]-4, c2_out_audio_r[0]+4, c2_out_audio_r[1]+4), fill=DEEP, outline=SIGNAL, width=2)
draw.text((c2_x + c2_w - 48, c2_y + 204), "out L/R", font=f_tiny, fill=SIGNAL)

# -------------------------------------------------------------
# COLUMN 3: Plugin [Effect] (x=645, w=255)
# -------------------------------------------------------------
c3_x, c3_w = 645, 255
c3_y, c3_h = 76, 260
draw_card(c3_x, c3_y, c3_w, c3_h, "Plugin", "VST3 // STEREO EFFECT", "VALHALLA DELAY")

einfo_y = c3_y + 38
draw.rounded_rectangle((c3_x + 10, einfo_y, c3_x + c3_w - 10, einfo_y + 36), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c3_x + 16, einfo_y + 6), "ID: com.valhalla.delay", font=f_tiny, fill=INK2)
draw.text((c3_x + 16, einfo_y + 20), "PDC: 128 SAMPLES LATENCY (COMPENSATED)", font=f_tiny, fill=CYAN)

c3_in_audio_l = (c3_x + 8, c3_y + 98)
draw.ellipse((c3_in_audio_l[0]-4, c3_in_audio_l[1]-4, c3_in_audio_l[0]+4, c3_in_audio_l[1]+4), fill=DEEP, outline=SIGNAL, width=2)
c3_in_audio_r = (c3_x + 8, c3_y + 114)
draw.ellipse((c3_in_audio_r[0]-4, c3_in_audio_r[1]-4, c3_in_audio_r[0]+4, c3_in_audio_r[1]+4), fill=DEEP, outline=SIGNAL, width=2)
draw.text((c3_x + 18, c3_y + 102), "audio in (L/R)", font=f_tiny, fill=SIGNAL)

c3_in_sc = (c3_x + 8, c3_y + 138)
draw.ellipse((c3_in_sc[0]-4, c3_in_sc[1]-4, c3_in_sc[0]+4, c3_in_sc[1]+4), fill=DEEP, outline=INK3, width=2)
draw.text((c3_x + 18, c3_y + 134), "sidechain in", font=f_tiny, fill=INK3)

# Mapped effect parameters
ep1_y = c3_y + 164
draw.text((c3_x + 22, ep1_y), "0: Delay Time", font=f_small, fill=INK)
draw.text((c3_x + c3_w - 52, ep1_y), "3/16 D", font=f_small, fill=CYAN)
draw.rounded_rectangle((c3_x + 22, ep1_y + 14, c3_x + c3_w - 14, ep1_y + 22), 3, fill=GROUND)
draw.rounded_rectangle((c3_x + 22, ep1_y + 14, c3_x + 22 + int((c3_w - 36) * 0.42), ep1_y + 22), 3, fill=CYAN)
c3_mod_time = (c3_x + 8, ep1_y + 16)
draw.ellipse((c3_mod_time[0]-4, c3_mod_time[1]-4, c3_mod_time[0]+4, c3_mod_time[1]+4), fill=DEEP, outline=CYAN, width=2)

ep2_y = c3_y + 206
draw.text((c3_x + 22, ep2_y), "1: Feedback", font=f_small, fill=INK)
draw.text((c3_x + c3_w - 48, ep2_y), "68%", font=f_small, fill=GREEN)
draw.rounded_rectangle((c3_x + 22, ep2_y + 14, c3_x + c3_w - 14, ep2_y + 22), 3, fill=GROUND)
draw.rounded_rectangle((c3_x + 22, ep2_y + 14, c3_x + 22 + int((c3_w - 36) * 0.68), ep2_y + 22), 3, fill=GREEN)
c3_mod_fb = (c3_x + 8, ep2_y + 16)
draw.ellipse((c3_mod_fb[0]-4, c3_mod_fb[1]-4, c3_mod_fb[0]+4, c3_mod_fb[1]+4), fill=DEEP, outline=GREEN, width=2)

c3_out_audio = (c3_x + c3_w - 8, c3_y + 150)
draw.ellipse((c3_out_audio[0]-4, c3_out_audio[1]-4, c3_out_audio[0]+4, c3_out_audio[1]+4), fill=DEEP, outline=SIGNAL, width=2)
draw.text((c3_x + c3_w - 46, c3_y + 145), "audio", font=f_tiny, fill=SIGNAL)

# -------------------------------------------------------------
# COLUMN 4: Audio Analyze & Audio Out (x=940, w=215)
# -------------------------------------------------------------
c4_x, c4_w = 940, 215
c4a_y, c4a_h = 76, 195
draw_card(c4_x, c4a_y, c4_w, c4a_h, "Audio Analyze", "DSP // 8-BAND FFT", "SPECTRAL")

fft_y = c4a_y + 40
draw.rounded_rectangle((c4_x + 10, fft_y, c4_x + c4_w - 10, fft_y + 75), 6, fill=DEEP, outline=SURF3, width=1)
fft_bars = [0.85, 0.72, 0.90, 0.64, 0.48, 0.76, 0.58, 0.35]
bar_w = (c4_w - 36) / 8
for bi, bval in enumerate(fft_bars):
    bx = c4_x + 16 + bi * bar_w
    bh = int(48 * bval)
    draw.rectangle((bx, fft_y + 60 - bh, bx + bar_w - 3, fft_y + 60), fill=GREEN if bi < 3 else SIGNAL)

draw.text((c4_x + 16, c4a_y + 125), "ENVELOPE BANDS (CV OUT)", font=f_tiny, fill=INK3)
draw.text((c4_x + 16, c4a_y + 142), "• Low band  (20-250 Hz)   : 0.82", font=f_tiny, fill=GREEN)
draw.text((c4_x + 16, c4a_y + 158), "• Mid band  (250-4k Hz)   : 0.61", font=f_tiny, fill=SIGNAL)
draw.text((c4_x + 16, c4a_y + 174), "• High band (4k-20k Hz)   : 0.44", font=f_tiny, fill=CYAN)

c4a_in_audio = (c4_x + 8, c4a_y + 80)
draw.ellipse((c4a_in_audio[0]-4, c4a_in_audio[1]-4, c4a_in_audio[0]+4, c4a_in_audio[1]+4), fill=DEEP, outline=SIGNAL, width=2)

c4a_out_high = (c4_x + c4_w - 8, c4a_y + 175)
draw.ellipse((c4a_out_high[0]-4, c4a_out_high[1]-4, c4a_out_high[0]+4, c4a_out_high[1]+4), fill=DEEP, outline=GREEN, width=2)

# Card 4B: Audio Out (Hardware Master)
c4b_y, c4b_h = 285, 227
draw_card(c4_x, c4b_y, c4_w, c4b_h, "Audio Out", "HARDWARE // MASTER", "48 kHz")

vu_box_y = c4b_y + 40
draw.rounded_rectangle((c4_x + 10, vu_box_y, c4_x + c4_w - 10, vu_box_y + 65), 6, fill=DEEP, outline=SURF3, width=1)
draw.text((c4_x + 16, vu_box_y + 8), "L", font=f_tiny, fill=INK2)
draw.rounded_rectangle((c4_x + 28, vu_box_y + 8, c4_x + c4_w - 20, vu_box_y + 18), 3, fill=GROUND)
draw.rounded_rectangle((c4_x + 28, vu_box_y + 8, c4_x + 28 + int((c4_w - 48) * 0.84), vu_box_y + 18), 3, fill=GREEN)

draw.text((c4_x + 16, vu_box_y + 26), "R", font=f_tiny, fill=INK2)
draw.rounded_rectangle((c4_x + 28, vu_box_y + 26, c4_x + c4_w - 20, vu_box_y + 36), 3, fill=GROUND)
draw.rounded_rectangle((c4_x + 28, vu_box_y + 26, c4_x + 28 + int((c4_w - 48) * 0.81), vu_box_y + 36), 3, fill=GREEN)
draw.text((c4_x + 16, vu_box_y + 46), "TRUE PEAK // -1.8 dBFS", font=f_tiny, fill=GREEN)

draw.rounded_rectangle((c4_x + 10, c4b_y + 115, c4_x + c4_w - 10, c4b_y + 215), 6, fill=SURF2, outline=SURF3, width=1)
draw.text((c4_x + 16, c4b_y + 124), "HOST DISPATCH STATUS", font=f_tiny, fill=INK)
draw.text((c4_x + 16, c4b_y + 142), "• Driver: CoreAudio / ASIO", font=f_tiny, fill=INK2)
draw.text((c4_x + 16, c4b_y + 160), "• Buffer: 128 samples (2.6 ms)", font=f_tiny, fill=SIGNAL)
draw.text((c4_x + 16, c4b_y + 178), "• Precision: 32-bit float", font=f_tiny, fill=INK3)
draw.text((c4_x + 16, c4b_y + 196), "• Dropouts / xruns: 0", font=f_tiny, fill=GREEN)

c4b_in_audio = (c4_x + 8, c4b_y + 80)
draw.ellipse((c4b_in_audio[0]-4, c4b_in_audio[1]-4, c4b_in_audio[0]+4, c4b_in_audio[1]+4), fill=DEEP, outline=SIGNAL, width=2)

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

# Cable 1: Note Sequencer -> Plugin [Synth] Notes
draw_cable(c1a_out_note, c2_in_note, CYAN, width=2)

# Cable 2: LFO CV -> Plugin [Synth] Cutoff Modulation
draw_cable(c1b_out_cv, c2_mod_cutoff, EMBER, width=2)

# Cable 3: Plugin [Synth] Audio Out -> Plugin [Effect] Audio In
draw_cable(c2_out_audio_l, c3_in_audio_l, SIGNAL, width=2)
draw_cable(c2_out_audio_r, c3_in_audio_r, SIGNAL, width=2)

# Cable 4: Plugin [Synth] Audio Out -> Audio Analyze Audio In
draw_cable(c2_out_audio_r, c4a_in_audio, SIGNAL, width=2)

# Cable 5: Audio Analyze High band CV -> Plugin [Effect] Feedback Modulation
draw_cable(c4a_out_high, c3_mod_fb, GREEN, width=2)

# Cable 6: Plugin [Effect] Audio Out -> Audio Out Master
draw_cable(c3_out_audio, c4b_in_audio, SIGNAL, width=3)

# -------------------------------------------------------------
# LOWER ARCHITECTURE BANNER (Lock-Free Concurrency & Crash Guard)
# -------------------------------------------------------------
ban_y, ban_h = 530, 115
draw.rounded_rectangle((45, ban_y, W - 45, ban_y + ban_h), 8, fill=GROUND, outline=LINE, width=1)

# Left column: Main Thread
draw.text((65, ban_y + 12), "MAIN THREAD (UI & COORDINATION)", font=f_small, fill=SIGNAL)
draw.text((65, ban_y + 32), "• AudioPluginNode owns Platform::PluginHandle lifecycle", font=f_tiny, fill=INK2)
draw.text((65, ban_y + 48), "• Touch-to-learn parameter inspection via PluginVST3PollLearned", font=f_tiny, fill=INK2)
draw.text((65, ban_y + 64), "• Direct Platform::PluginSetParameter (bypasses 64-slot mailbox limit)", font=f_tiny, fill=INK2)
draw.text((65, ban_y + 80), "• Full state serialization: Base64 chunk embedded directly into .inf patch", font=f_tiny, fill=INK3)

# Center divider
draw.line((W // 2 - 20, ban_y + 14, W // 2 - 20, ban_y + ban_h - 14), fill=LINE, width=1)

# Right column: Audio Thread & Crash Guard
draw.text((W // 2, ban_y + 12), "AUDIO THREAD (REAL-TIME DSP & CRASH JOURNALING)", font=f_small, fill=GREEN)
draw.text((W // 2, ban_y + 32), "• AudioPluginAudioNode reads atomic<PluginHandle*> with acquire semantics", font=f_tiny, fill=INK2)
draw.text((W // 2, ban_y + 48), "• PluginCrashGuard::Scope breadcrumb journaling protects against unhandled faults", font=f_tiny, fill=GREEN)
draw.text((W // 2, ban_y + 64), "• Frame-accurate MIDI note scheduling preserves microsecond event timing", font=f_tiny, fill=CYAN)
draw.text((W // 2, ban_y + 80), "• Automated PDC delay lines align parallel dry/wet paths with zero comb filtering", font=f_tiny, fill=INK3)

# Save to destination
out_path = os.path.join(ROOT, "website/assets/blog_modular-vst3-host-audio-routing-and-modulation.png")
os.makedirs(os.path.dirname(out_path), exist_ok=True)
img.save(out_path, "PNG")
print(f"Hero image written: {out_path} ({W}x{H})")
