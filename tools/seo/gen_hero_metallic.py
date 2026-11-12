#!/usr/bin/env python3
"""Generate the technical hero diagram for the real-time modal synthesis and metallic percussion blog post.
Outputs a crisp 1200x675 brand-compliant image to website/assets/blog_modal-synthesis-and-physical-modelling-of-metallic-percussion.png.
"""
import math
import os
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
# Top pill badge: INFINITE // PHYSICAL ACOUSTICS & DSP
draw.rounded_rectangle([45, 40, 370, 66], radius=6, fill=(32, 38, 63, 255), outline=(52, 59, 94, 255), width=1)
draw.text((58, 46), "INFINITE  //  PHYSICAL ACOUSTICS & DSP", font=f_badge, fill=(167, 177, 255, 255))

# Right pill: TRACK B: MODAL SYNTHESIS & PERCUSSION
draw.rounded_rectangle([W - 340, 40, W - 45, 66], radius=6, fill=(32, 38, 63, 255), outline=(52, 59, 94, 255), width=1)
draw.text((W - 325, 46), "TRACK B: MODAL SYNTHESIS & METALLIC DSP", font=f_badge, fill=(238, 241, 250, 255))

# Main title and subtitle
draw.text((45, 80), "Modal Synthesis & Metallic Percussion in Real Time", font=f_title, fill=(238, 241, 250, 255))
draw.text((45, 116), "Inharmonic second-order resonator banks, mallet strike exciters, dispersive waveguides, and Chladni plate physics", font=f_sub, fill=(163, 170, 203, 255))

# Horizontal rule below header
draw.line([(45, 142), (W - 45, 142)], fill=(42, 48, 82, 255), width=1)

# 4. Left Pane: Resonator Bank Spectrum & Physical Models (Width ~530)
lx1, ly1, lx2, ly2 = 45, 158, 575, H - 35
draw.rounded_rectangle([lx1, ly1, lx2, ly2], radius=12, fill=(21, 25, 48, 255), outline=(42, 48, 82, 255), width=1)
draw.text((lx1 + 20, ly1 + 14), "12-MODE INHARMONIC RESONATOR BANK & WAVEGUIDE", font=f_card_hdr, fill=(255, 107, 53, 255))

# Schematic Box
sx1, sy1, sx2, sy2 = lx1 + 16, ly1 + 36, lx2 - 16, ly1 + 330
draw.rounded_rectangle([sx1, sy1, sx2, sy2], radius=8, fill=(10, 13, 26, 255), outline=(35, 42, 74, 255), width=1)

# Top section inside Schematic: Exciter pulse + Dispersion loop
exc_box_y = sy1 + 12
draw.text((sx1 + 16, exc_box_y), "EXCITER: MALLET STRIKE PULSE + NOISE BURST", font=f_mono_sm, fill=(255, 107, 53, 255))

# Draw mallet pulse shape (raised cosine / hanning transient)
px1, py1, px2, py2 = sx1 + 16, exc_box_y + 16, sx1 + 230, exc_box_y + 68
draw.rounded_rectangle([px1, py1, px2, py2], radius=4, fill=(18, 23, 44, 255), outline=(42, 50, 84, 255), width=1)
draw.line([(px1 + 8, py2 - 8), (px2 - 8, py2 - 8)], fill=(45, 55, 90, 255), width=1)
pulse_pts = []
for i in range(120):
    t = i / 120.0
    x = px1 + 12 + t * (px2 - px1 - 24)
    # Raised cosine strike impulse with decay tail
    env = 0.0
    if t < 0.25:
        env = 0.5 * (1.0 - math.cos(math.pi * t / 0.25))
    else:
        env = math.exp(-(t - 0.25) * 8.0)
    y = py2 - 8 - env * 34.0
    pulse_pts.append((x, y))
draw.line(pulse_pts, fill=(255, 107, 53, 255), width=2)
draw.text((px1 + 12, py1 + 6), "Strike Transient (1.4 ms)", font=f_mono_sm, fill=(238, 241, 250, 255))
draw.text((px2 - 85, py1 + 6), "p(t) = v * sin(wt)", font=f_mono_sm, fill=(163, 170, 203, 255))

# Waveguide loop block
wx1, wy1, wx2, wy2 = sx1 + 242, exc_box_y + 16, sx2 - 16, exc_box_y + 68
draw.rounded_rectangle([wx1, wy1, wx2, wy2], radius=4, fill=(18, 23, 44, 255), outline=(42, 50, 84, 255), width=1)
draw.text((wx1 + 10, wy1 + 6), "Dispersive Waveguide Loop", font=f_mono_sm, fill=(167, 177, 255, 255))
draw.text((wx1 + 10, wy1 + 22), "Delay z^-M + Allpass A(z)", font=f_mono_sm, fill=(238, 241, 250, 255))
draw.text((wx1 + 10, wy1 + 36), "Phase dispersion tau_g(w)", font=f_mono_sm, fill=(163, 170, 203, 255))

# Draw arrow from pulse into waveguide
draw.line([(px2, py1 + 26), (wx1, py1 + 26)], fill=(255, 107, 53, 220), width=2)
draw.polygon([(wx1, py1 + 26), (wx1 - 5, py1 + 23), (wx1 - 5, py1 + 29)], fill=(255, 107, 53, 255))

# Divider inside Schematic
body_box_y = exc_box_y + 80
draw.line([(sx1 + 14, body_box_y), (sx2 - 14, body_box_y)], fill=(35, 42, 74, 255), width=1)
draw.text((sx1 + 16, body_box_y + 8), "MODAL RESONATOR SPECTRUM (STEEL BAR & BELL BRONZE RATIOS)", font=f_mono_sm, fill=(167, 177, 255, 255))

# Spectrum axes
ax_x1, ax_y1, ax_x2, ax_y2 = sx1 + 24, body_box_y + 26, sx2 - 24, sy2 - 14
draw.line([(ax_x1, ax_y2), (ax_x2, ax_y2)], fill=(52, 59, 94, 255), width=1)
draw.line([(ax_x1, ax_y1), (ax_x1, ax_y2)], fill=(52, 59, 94, 255), width=1)

# Inharmonic modal peaks for Metallic (Steel / Bell Bronze hybrid)
# Mode ratios: 1.000, 2.756, 5.404, 8.933, 13.344, 18.640, 24.815...
modes = [
    (220.0 * 1.000, 1.00, "f0 (220 Hz)"),
    (220.0 * 2.756, 0.78, "2.76x (mode 2)"),
    (220.0 * 5.404, 0.56, "5.40x (mode 3)"),
    (220.0 * 8.933, 0.40, "8.93x (mode 4)"),
    (220.0 * 13.34, 0.28, "13.34x (mode 5)"),
    (220.0 * 18.64, 0.19, "18.64x (mode 6)"),
    (220.0 * 24.81, 0.13, "24.81x (mode 7)"),
    (220.0 * 31.87, 0.08, "31.87x (mode 8)"),
]

# Continuous resonance curve
curve_pts = []
for k in range(ax_x2 - ax_x1):
    freq = 180.0 * math.pow(10.0, (k / (ax_x2 - ax_x1)) * 1.6)
    resp = 0.04
    for mf, ma, _ in modes:
        q = 36.0
        bw = mf / q
        d = (freq - mf) / bw
        resp += ma / (1.0 + d * d)
    resp_clamped = min(1.0, resp)
    cx = ax_x1 + k
    cy = ax_y2 - resp_clamped * (ax_y2 - ax_y1 - 10)
    curve_pts.append((cx, cy))

fill_poly = [(ax_x1, ax_y2)] + curve_pts + [(ax_x2, ax_y2)]
draw.polygon(fill_poly, fill=(26, 36, 70, 120))
draw.line(curve_pts, fill=(167, 177, 255, 240), width=2)

# Mark key peaks
for mf, ma, label in modes[:4]:
    k_pos = int((math.log10(mf / 180.0) / 1.6) * (ax_x2 - ax_x1))
    if 0 <= k_pos <= (ax_x2 - ax_x1):
        cx = ax_x1 + k_pos
        cy = ax_y2 - ma * (ax_y2 - ax_y1 - 10)
        draw.ellipse([cx - 3, cy - 3, cx + 3, cy + 3], fill=(255, 107, 53, 255))
        draw.text((cx - 24, cy - 14), label, font=f_mono_sm, fill=(238, 241, 250, 255))

# Equations below simulation box
eq_y = ly1 + 342
draw.text((lx1 + 20, eq_y), "BEAM THEORY:      f_n = (pi / 2 L^2) * sqrt(E * I / (rho * A)) * (n + 0.5)^2", font=f_formula, fill=(238, 241, 250, 255))
draw.text((lx1 + 20, eq_y + 19), "BANDPASS MODE:    H_m(z) = g_m * (1 - r^2) / (1 - 2 r cos(w_m) z^-1 + r^2 z^-2)", font=f_formula, fill=(167, 177, 255, 255))
draw.text((lx1 + 20, eq_y + 38), "ALLPASS DISP:     A(z) = (a + z^-1) / (1 + a z^-1),  tau_g(w) = (1 - a^2) / |1 + a e^-jw|^2", font=f_formula, fill=(255, 107, 53, 255))
draw.text((lx1 + 20, eq_y + 57), "CHLADNI PLATES:   J_m(k r) - (I_m(k r) * J_m(k a) / I_m(k a)) = 0  [2D Bessel nodal rings]", font=f_formula, fill=(163, 170, 203, 255))
draw.text((lx1 + 20, eq_y + 76), "REAL-TIME SOLVE:  12 parallel second-order IIR filters evaluated per audio sample (48 kHz)", font=f_mono_sm, fill=(136, 144, 177, 255))

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
        draw.text((x + w - 85, py), p_val, font=f_mono_sm, fill=(255, 107, 53, 255) if is_mod else (167, 177, 255, 255))
        py += 16

# Nodes layout in execution flow
# Node 1: Note Sequencer
draw_node_card(rx1 + 16, ry1 + 38, 240, 115, "Note Sequencer", "NOTES", (120, 220, 140, 255), [
    ("mode", "Polyphonic", False),
    ("length", "16 steps", False),
    ("division", "1/16 sync", False),
    ("swing", "18%", False),
    ("velocity", "dynamic", True),
])

# Node 2: Random (Modulator)
draw_node_card(rx1 + 16, ry1 + 166, 240, 95, "Random", "MOD", (255, 107, 53, 255), [
    ("distribution", "Gaussian", False),
    ("rate", "1/8 bar", False),
    ("smooth", "45 ms", False),
    ("output", "mallet snap", True),
])

# Node 3: Metallic (Physical Modelling Synthesiser)
draw_node_card(rx1 + 285, ry1 + 38, 255, 140, "Metallic", "SYNTH", (255, 107, 53, 255), [
    ("material", "Bell Bronze", False),
    ("modes", "12 inharmonic", False),
    ("decay", "3.50 s", True),
    ("stiffness", "0.75", False),
    ("transient", "1.40 mallet", True),
    ("frequency", "MIDI tracking", False),
])

# Node 4: Gain (Utility)
draw_node_card(rx1 + 285, ry1 + 192, 255, 95, "Gain", "UTILITY", (167, 177, 255, 255), [
    ("gain", "-3.5 dB", False),
    ("pan", "stereo spread", True),
    ("softLimit", "enabled", False),
    ("meter", "peak / RMS", False),
])

# Node 5: Audio Analyze
draw_node_card(rx1 + 16, ry1 + 300, 240, 110, "Audio Analyze", "MOD", (167, 177, 255, 255), [
    ("source", "Metallic audio", False),
    ("envelope", "fast peak", True),
    ("spectralRms", "0.58", True),
    ("pitchCv", "MIDI 60 (C4)", True),
    ("modes", "inharmonic", False),
])

# Node 6: Audio Displacement -> Render 3D
draw_node_card(rx1 + 285, ry1 + 300, 255, 110, "Audio Displacement", "3D", (200, 140, 255, 255), [
    ("mesh", "Chladni Plate", False),
    ("displace", "modal normals", True),
    ("amplitude", "0.35 m", True),
    ("damping", "material loss", True),
    ("output", "Render 3D", False),
])

# Connecting cables between nodes
# Cable 1: Note Sequencer -> Metallic (Note Cable: Green)
draw.line([(rx1 + 256, ry1 + 65), (rx1 + 285, ry1 + 65)], fill=(120, 220, 140, 255), width=2)
draw.ellipse([rx1 + 283, ry1 + 63, rx1 + 287, ry1 + 67], fill=(120, 220, 140, 255))

# Cable 2: Random -> Metallic (Modulation Cable: Orange)
c2_pts = [(rx1 + 256, ry1 + 210), (rx1 + 270, ry1 + 210), (rx1 + 270, ry1 + 115), (rx1 + 285, ry1 + 115)]
draw.line(c2_pts, fill=(255, 107, 53, 255), width=2)
draw.ellipse([rx1 + 283, ry1 + 113, rx1 + 287, ry1 + 117], fill=(255, 107, 53, 255))

# Cable 3: Metallic -> Gain (Audio Cable: Blue)
draw.line([(rx1 + 412, ry1 + 178), (rx1 + 412, ry1 + 192)], fill=(167, 177, 255, 255), width=2)

# Cable 4: Gain -> Audio Analyze (Audio Cable: Blue)
c4_pts = [(rx1 + 285, ry1 + 240), (rx1 + 270, ry1 + 240), (rx1 + 270, ry1 + 340), (rx1 + 256, ry1 + 340)]
draw.line(c4_pts, fill=(167, 177, 255, 255), width=2)

# Cable 5: Audio Analyze -> Audio Displacement (Modulation Cable: Orange)
draw.line([(rx1 + 256, ry1 + 370), (rx1 + 285, ry1 + 370)], fill=(255, 107, 53, 255), width=2)

# Flow banner at bottom
draw.rounded_rectangle([rx1 + 16, ry2 - 32, rx2 - 16, ry2 - 10], radius=4, fill=(15, 18, 36, 255), outline=(42, 48, 82, 255), width=1)
draw.text((rx1 + 26, ry2 - 26), "SIGNAL CHAIN: NoteSeq -> Metallic Synth -> Gain -> Audio Analyze -> Audio Displacement -> 3D Mesh", font=f_mono_sm, fill=(238, 241, 250, 255))

# Save image
out_path = os.path.join(REPO_ROOT, "website", "assets", "blog_modal-synthesis-and-physical-modelling-of-metallic-percussion.png")
os.makedirs(os.path.dirname(out_path), exist_ok=True)
img.save(out_path, format="PNG")
print(f"Generated hero diagram: {out_path} ({W}x{H})")
