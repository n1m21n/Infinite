#!/usr/bin/env python3
"""Generate the technical hero diagram for the real-time physical modelling string synthesis blog post.
Outputs a crisp 1200x675 brand-compliant image to website/assets/blog_physical-modelling-string-synthesis-in-real-time.png.
"""
import math
import os
import random
from PIL import Image, ImageDraw, ImageFont

W, H = 1200, 675
# Midnight deep #0D1020
img = Image.new("RGBA", (W, H), (13, 16, 32, 255))
draw = ImageDraw.Draw(img)

# Fonts
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FONT_DIR = os.path.join(REPO_ROOT, "art", "brand", "fonts")
GEIST_SEMIBOLD = os.path.join(FONT_DIR, "Geist-SemiBold.ttf")
GEIST_MEDIUM = os.path.join(FONT_DIR, "Geist-Medium.ttf")
GEIST_MONO = os.path.join(FONT_DIR, "GeistMono-Medium.ttf")

try:
    f_title = ImageFont.truetype(GEIST_SEMIBOLD, 27)
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
dot_color = (35, 42, 74, 180)  # #232A4A
for gx in range(24, W - 24, 28):
    for gy in range(24, H - 24, 28):
        draw.ellipse([gx - 1, gy - 1, gx + 1, gy + 1], fill=dot_color)

# 2. Outer decorative frame
draw.rounded_rectangle([20, 20, W - 20, H - 20], radius=16, outline=(42, 48, 82, 255), width=1)

# 3. Header section
# Top pill badge: INFINITE // PHYSICAL MODELLING SYNTHESIS
draw.rounded_rectangle([45, 40, 360, 66], radius=6, fill=(32, 38, 63, 255), outline=(52, 59, 94, 255), width=1)
draw.text((58, 46), "INFINITE  //  PHYSICAL MODELLING SYNTHESIS", font=f_badge, fill=(167, 177, 255, 255))

# Right pill: TRACK B: DIGITAL WAVEGUIDES & MODAL DSP
draw.rounded_rectangle([W - 325, 40, W - 45, 66], radius=6, fill=(32, 38, 63, 255), outline=(52, 59, 94, 255), width=1)
draw.text((W - 310, 46), "TRACK B: DIGITAL WAVEGUIDES & MODAL DSP", font=f_badge, fill=(238, 241, 250, 255))

# Main title and subtitle
draw.text((45, 80), "Physical Modelling String Synthesis in Real Time", font=f_title, fill=(238, 241, 250, 255))
draw.text((45, 116), "Digital waveguides, allpass dispersion filters, nonlinear bow friction, and modal acoustic bodies", font=f_sub, fill=(163, 170, 203, 255))

# Horizontal rule below header
draw.line([(45, 142), (W - 45, 142)], fill=(42, 48, 82, 255), width=1)

# 4. Left Pane: Waveguide Delay Loop & Modal Physics (Width ~530)
lx1, ly1, lx2, ly2 = 45, 158, 575, H - 35
draw.rounded_rectangle([lx1, ly1, lx2, ly2], radius=12, fill=(21, 25, 48, 255), outline=(42, 48, 82, 255), width=1)
draw.text((lx1 + 20, ly1 + 14), "BIDIRECTIONAL DIGITAL WAVEGUIDE & MODAL BODY", font=f_card_hdr, fill=(255, 107, 53, 255))

# Schematic Box
sx1, sy1, sx2, sy2 = lx1 + 16, ly1 + 36, lx2 - 16, ly1 + 330
draw.rounded_rectangle([sx1, sy1, sx2, sy2], radius=8, fill=(10, 13, 26, 255), outline=(35, 42, 74, 255), width=1)

# Inside Schematic: Top & Bottom Delay Rails (Waveguide)
wg_top_y = sy1 + 52
wg_bot_y = sy1 + 120
rail_x1 = sx1 + 65
rail_x2 = sx2 - 65

# Background guide rails
draw.line([(rail_x1, wg_top_y), (rail_x2, wg_top_y)], fill=(52, 59, 94, 200), width=2)
draw.line([(rail_x1, wg_bot_y), (rail_x2, wg_bot_y)], fill=(52, 59, 94, 200), width=2)

# Waveforms on delay lines
# Top rail wave (right-going wave)
pts_top = []
for i in range(160):
    t = i / 160.0
    x = rail_x1 + t * (rail_x2 - rail_x1)
    y = wg_top_y - math.sin(t * math.pi * 3.5) * 16.0 * (1.0 - t * 0.35)
    pts_top.append((x, y))
draw.line(pts_top, fill=(167, 177, 255, 230), width=2)

# Bottom rail wave (left-going wave)
pts_bot = []
for i in range(160):
    t = i / 160.0
    x = rail_x1 + t * (rail_x2 - rail_x1)
    y = wg_bot_y + math.sin((1.0 - t) * math.pi * 3.5) * 16.0 * (0.65 + t * 0.35)
    pts_bot.append((x, y))
draw.line(pts_bot, fill=(255, 107, 53, 230), width=2)

# Nut (Left Termination: inverting reflection R0 = -1)
draw.rounded_rectangle([sx1 + 14, wg_top_y - 20, rail_x1, wg_bot_y + 20], radius=4, fill=(26, 32, 58, 255), outline=(62, 72, 115, 255), width=1)
draw.text((sx1 + 22, wg_top_y + 16), "NUT", font=f_mono_sm, fill=(238, 241, 250, 255))
draw.text((sx1 + 18, wg_top_y + 30), "R0 = -1", font=f_mono_sm, fill=(163, 170, 203, 255))
# Connection curve at nut
draw.arc([rail_x1 - 18, wg_top_y, rail_x1 + 18, wg_bot_y], start=90, end=270, fill=(255, 107, 53, 255), width=2)

# Bridge & Filters (Right Termination: loop filter + dispersion + tuning)
bx1, by1, bx2, by2 = rail_x2, wg_top_y - 20, sx2 - 14, wg_bot_y + 20
draw.rounded_rectangle([bx1, by1, bx2, by2], radius=4, fill=(26, 32, 58, 255), outline=(255, 107, 53, 255), width=1)
draw.text((bx1 + 8, by1 + 6), "BRIDGE", font=f_mono_sm, fill=(255, 107, 53, 255))
draw.text((bx1 + 8, by1 + 20), "Loop filter", font=f_mono_sm, fill=(238, 241, 250, 255))
draw.text((bx1 + 8, by1 + 34), "4x allpass", font=f_mono_sm, fill=(167, 177, 255, 255))
draw.text((bx1 + 8, by1 + 48), "Fractional L", font=f_mono_sm, fill=(163, 170, 203, 255))
# Connection curve at bridge
draw.arc([rail_x2 - 18, wg_top_y, rail_x2 + 18, wg_bot_y], start=-90, end=90, fill=(167, 177, 255, 255), width=2)

# Delay direction labels
draw.text((rail_x1 + 30, wg_top_y - 22), "Delay Line -> Right-going displacement y+(n)", font=f_mono_sm, fill=(167, 177, 255, 255))
draw.text((rail_x1 + 30, wg_bot_y + 18), "<- Delay Line Left-going displacement y-(n)", font=f_mono_sm, fill=(255, 107, 53, 255))

# Exciter Injection Node in centre
exc_x = rail_x1 + int((rail_x2 - rail_x1) * 0.32)
draw.line([(exc_x, wg_top_y - 30), (exc_x, wg_bot_y + 10)], fill=(255, 255, 255, 180), width=1)
draw.rounded_rectangle([exc_x - 38, sy1 + 10, exc_x + 38, sy1 + 32], radius=4, fill=(35, 45, 80, 255), outline=(255, 107, 53, 255), width=1)
draw.text((exc_x - 30, sy1 + 15), "EXCITER", font=f_mono_sm, fill=(255, 107, 53, 255))
draw.text((exc_x + 8, sy1 + 15), "pos=0.32", font=f_mono_sm, fill=(238, 241, 250, 255))

# Lower section inside Schematic: Modal Body Resonator Curve
body_box_y = sy1 + 160
draw.line([(sx1 + 14, body_box_y), (sx2 - 14, body_box_y)], fill=(35, 42, 74, 255), width=1)
draw.text((sx1 + 16, body_box_y + 8), "MODAL RESONANCE FREQUENCY RESPONSE (GUITAR / VIOLIN CORPUS)", font=f_mono_sm, fill=(167, 177, 255, 255))

# Spectrum axes
ax_x1, ax_y1, ax_x2, ax_y2 = sx1 + 24, body_box_y + 26, sx2 - 24, sy2 - 12
draw.line([(ax_x1, ax_y2), (ax_x2, ax_y2)], fill=(52, 59, 94, 255), width=1)
draw.line([(ax_x1, ax_y1), (ax_x1, ax_y2)], fill=(52, 59, 94, 255), width=1)

# Draw fitted body mode peaks (Air mode A0, Top plate T(1,1), Corpus modes)
modes = [
    (99.0, 1.00, "A0 air 99Hz"),
    (199.0, 0.75, "T(1,1) plate 199Hz"),
    (392.0, 0.45, "body 392Hz"),
    (772.0, 0.28, "high 772Hz"),
    (1544.0, 0.18, "air mode"),
    (3368.0, 0.22, "radiation"),
]
# Draw continuous resonant curve
curve_pts = []
for k in range(ax_x2 - ax_x1):
    freq = 60.0 * math.pow(10.0, (k / (ax_x2 - ax_x1)) * 1.9)
    resp = 0.05
    for mf, ma, _ in modes:
        q = 14.0
        bw = mf / q
        d = (freq - mf) / bw
        resp += ma / (1.0 + d * d)
    resp_clamped = min(1.0, resp)
    px = ax_x1 + k
    py = ax_y2 - resp_clamped * (ax_y2 - ax_y1 - 10)
    curve_pts.append((px, py))

# Fill under curve
fill_poly = [(ax_x1, ax_y2)] + curve_pts + [(ax_x2, ax_y2)]
draw.polygon(fill_poly, fill=(26, 36, 70, 120))
draw.line(curve_pts, fill=(167, 177, 255, 240), width=2)

# Mark key peaks
for mf, ma, label in modes[:3]:
    k_pos = int((math.log10(mf / 60.0) / 1.9) * (ax_x2 - ax_x1))
    px = ax_x1 + k_pos
    py = ax_y2 - ma * (ax_y2 - ax_y1 - 10)
    draw.ellipse([px - 3, py - 3, px + 3, py + 3], fill=(255, 107, 53, 255))
    draw.text((px - 20, py - 14), label, font=f_mono_sm, fill=(238, 241, 250, 255))

# Equations below simulation box
eq_y = ly1 + 342
draw.text((lx1 + 20, eq_y), "LOOP TUNING:       P_total = f_s / f_0 = M + P_allpass(w_0) + P_filter(w_0)", font=f_formula, fill=(238, 241, 250, 255))
draw.text((lx1 + 20, eq_y + 19), "STIFFNESS PASS:    H_disp(z) = prod_{k=1}^4 (a_k + z^-1) / (1 + a_k z^-1)  [inharmonicity]", font=f_formula, fill=(167, 177, 255, 255))
draw.text((lx1 + 20, eq_y + 38), "NONLINEAR BOW:     F_friction(v_rel) = sign(v_rel) * (mu_d + (mu_s - mu_d) * e^(-c|v_rel|))", font=f_formula, fill=(255, 107, 53, 255))
draw.text((lx1 + 20, eq_y + 57), "MODAL COUPLING:    y_body(t) = sum_{m=1}^{10} A_m * e^(-alpha_m t) * sin(w_m t + phi_m)", font=f_formula, fill=(163, 170, 203, 255))
draw.text((lx1 + 20, eq_y + 76), "REAL-TIME SOLVE:   Allpass phase delay inverted numerically in 40 iterations per chunk", font=f_mono_sm, fill=(136, 144, 177, 255))

# 5. Right Pane: Modular Execution Graph (Width ~575)
rx1, ry1, rx2, ry2 = 595, 158, W - 45, H - 35
draw.rounded_rectangle([rx1, ry1, rx2, ry2], radius=12, fill=(21, 25, 48, 255), outline=(42, 48, 82, 255), width=1)
draw.text((rx1 + 20, ry1 + 14), "MODULAR AUDIOVISUAL EXECUTION GRAPH", font=f_card_hdr, fill=(167, 177, 255, 255))

# Helper to draw node cards
def draw_node_card(x, y, w, h, title, cat, cat_col, params):
    draw.rounded_rectangle([x, y, x + w, y + h], radius=6, fill=(15, 18, 36, 255), outline=(42, 48, 82, 255), width=1)
    # Header bar
    draw.rounded_rectangle([x, y, x + w, y + 24], radius=6, fill=(26, 32, 58, 255))
    draw.text((x + 8, y + 5), title, font=f_node_title, fill=(238, 241, 250, 255))
    draw.text((x + w - 50, y + 6), cat, font=f_node_cat, fill=cat_col)
    
    # Body params
    py = y + 30
    for p_name, p_val, is_mod in params:
        draw.text((x + 8, py), p_name, font=f_mono_sm, fill=(163, 170, 203, 255))
        draw.text((x + w - 75, py), p_val, font=f_mono_sm, fill=(255, 107, 53, 255) if is_mod else (167, 177, 255, 255))
        py += 16

# Nodes layout in execution flow
# Node 1: Note Sequencer
draw_node_card(rx1 + 16, ry1 + 38, 240, 115, "Note Sequencer", "NOTES", (120, 220, 140, 255), [
    ("mode", "Polyphonic", False),
    ("length", "16 steps", False),
    ("division", "1/16 sync", False),
    ("swing", "24%", False),
    ("velocity", "dynamic", True),
])

# Node 2: LFO (Modulator)
draw_node_card(rx1 + 16, ry1 + 166, 240, 95, "LFO", "MOD", (255, 107, 53, 255), [
    ("shape", "Triangle", False),
    ("rate", "1/4 bar", False),
    ("output", "bow pressure", True),
    ("depth", "0.72", True),
])

# Node 3: String (Physical Waveguide Synthesiser)
draw_node_card(rx1 + 285, ry1 + 38, 255, 140, "String", "SYNTH", (255, 107, 53, 255), [
    ("exciter", "Bow / Pluck", False),
    ("position", "0.32", True),
    ("tone", "0.78", True),
    ("decay", "2.40 s", False),
    ("stiffness", "0.35", False),
    ("bodyType", "Cello / Guitar", False),
])

# Node 4: Resonator Bank
draw_node_card(rx1 + 285, ry1 + 192, 255, 95, "Resonator Bank", "AUDIO", (167, 177, 255, 255), [
    ("modes", "12 bandpass", False),
    ("decay", "1.80 s", False),
    ("feedback", "0.45", False),
    ("brightness", "0.68", False),
])

# Node 5: Audio Analyze
draw_node_card(rx1 + 16, ry1 + 300, 240, 110, "Audio Analyze", "AUDIO", (167, 177, 255, 255), [
    ("source", "String audio", False),
    ("envelope", "fast peak", True),
    ("spectralRms", "0.64", True),
    ("pitchCv", "MIDI 48 (C3)", True),
    ("harmonics", "odd / even", False),
])

# Node 6: Audio Displacement -> Render 3D
draw_node_card(rx1 + 285, ry1 + 300, 255, 110, "Audio Displacement", "3D", (200, 140, 255, 255), [
    ("mesh", "Cylinder wire", False),
    ("displace", "waveguide Y", True),
    ("amplitude", "0.42 m", True),
    ("vibration", "traveling", True),
    ("output", "Render 3D", False),
])

# Connecting cables between nodes
# Cable 1: Note Sequencer -> String (Note Cable)
draw.line([(rx1 + 256, ry1 + 65), (rx1 + 285, ry1 + 65)], fill=(120, 220, 140, 255), width=2)
draw.ellipse([rx1 + 283, ry1 + 63, rx1 + 287, ry1 + 67], fill=(120, 220, 140, 255))

# Cable 2: LFO -> String (Modulation Cable)
c2_pts = [(rx1 + 256, ry1 + 210), (rx1 + 270, ry1 + 210), (rx1 + 270, ry1 + 115), (rx1 + 285, ry1 + 115)]
draw.line(c2_pts, fill=(255, 107, 53, 255), width=2)
draw.ellipse([rx1 + 283, ry1 + 113, rx1 + 287, ry1 + 117], fill=(255, 107, 53, 255))

# Cable 3: String -> Resonator Bank (Audio Cable)
draw.line([(rx1 + 412, ry1 + 178), (rx1 + 412, ry1 + 192)], fill=(167, 177, 255, 255), width=2)

# Cable 4: Resonator Bank -> Audio Analyze
c4_pts = [(rx1 + 285, ry1 + 240), (rx1 + 270, ry1 + 240), (rx1 + 270, ry1 + 340), (rx1 + 256, ry1 + 340)]
draw.line(c4_pts, fill=(167, 177, 255, 255), width=2)

# Cable 5: Audio Analyze -> Audio Displacement (Modulation Cable)
draw.line([(rx1 + 256, ry1 + 370), (rx1 + 285, ry1 + 370)], fill=(255, 107, 53, 255), width=2)

# Flow banner at bottom
draw.rounded_rectangle([rx1 + 16, ry2 - 32, rx2 - 16, ry2 - 10], radius=4, fill=(15, 18, 36, 255), outline=(42, 48, 82, 255), width=1)
draw.text((rx1 + 26, ry2 - 26), "SIGNAL CHAIN: NoteSeq -> String Waveguide -> Resonator -> AudioAnalyze -> 3D Mesh Deform", font=f_mono_sm, fill=(238, 241, 250, 255))

# Save image
out_path = os.path.join(REPO_ROOT, "website", "assets", "blog_physical-modelling-string-synthesis-in-real-time.png")
os.makedirs(os.path.dirname(out_path), exist_ok=True)
img.save(out_path, format="PNG")
print(f"Generated hero diagram: {out_path} ({W}x{H})")
