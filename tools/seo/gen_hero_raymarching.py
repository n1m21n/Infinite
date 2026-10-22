#!/usr/bin/env python3
"""Generates the 1200x675 hero technical diagram for the Raymarching blog post.
Renders at 2x (2400x1350) and downsamples with Lanczos for razor-sharp geometry.
Follows Infinite brand guidelines (Midnight ground, Ember accent, Signal lines).
"""
import math, os
from PIL import Image, ImageDraw, ImageFont

W, H = 2400, 1350  # 2x supersampled

# Brand colours
C_GROUND = (21, 25, 48, 255)       # #151930
C_DEEP = (13, 16, 32, 255)         # #0D1020
C_SURFACE1 = (32, 38, 63, 255)     # #20263F
C_SURFACE2 = (42, 48, 82, 255)     # #2A3052
C_LINE = (52, 59, 94, 255)         # #343B5E
C_SIGNAL = (167, 177, 255, 255)    # #A7B1FF
C_SIGNAL_DIM = (74, 84, 214, 180)  # #4A54D6
C_EMBER = (255, 107, 53, 255)      # #FF6B35
C_EMBER_DIM = (217, 67, 14, 200)   # #D9430E
C_INK = (238, 241, 250, 255)       # #EEF1FA
C_INK2 = (163, 170, 203, 255)      # #A3AACB
C_INK3 = (136, 144, 177, 255)      # #8890B1

FONT_DIR = "/Users/namansoni/Infinite/worktrees/main/art/brand/fonts"
f_title = ImageFont.truetype(os.path.join(FONT_DIR, "Geist-SemiBold.ttf"), 68)
f_sub = ImageFont.truetype(os.path.join(FONT_DIR, "Geist-Medium.ttf"), 34)
f_card_title = ImageFont.truetype(os.path.join(FONT_DIR, "Geist-SemiBold.ttf"), 36)
f_card_sub = ImageFont.truetype(os.path.join(FONT_DIR, "Geist-Medium.ttf"), 26)
f_mono = ImageFont.truetype(os.path.join(FONT_DIR, "GeistMono-Medium.ttf"), 24)
f_mono_sm = ImageFont.truetype(os.path.join(FONT_DIR, "GeistMono-Medium.ttf"), 20)
f_mono_lg = ImageFont.truetype(os.path.join(FONT_DIR, "GeistMono-Medium.ttf"), 30)
f_badge = ImageFont.truetype(os.path.join(FONT_DIR, "Geist-SemiBold.ttf"), 22)

im = Image.new("RGBA", (W, H), C_GROUND)
draw = ImageDraw.Draw(im)

# Background subtle grid pattern
grid_step = 80
for x in range(0, W, grid_step):
    draw.line([(x, 0), (x, H)], fill=(28, 33, 58, 80), width=1)
for y in range(0, H, grid_step):
    draw.line([(0, y), (W, y)], fill=(28, 33, 58, 80), width=1)

# Header zone
# Pill badge
badge_text = "REAL-TIME CREATIVE CODING · GLSL 3.2"
bw = 480
draw.rounded_rectangle([(100, 70), (100 + bw, 118)], radius=24, fill=C_DEEP, outline=C_LINE, width=2)
draw.ellipse([(124, 88), (136, 100)], fill=C_EMBER)
draw.text((150, 81), badge_text, font=f_badge, fill=C_SIGNAL)

# Main Title & Subtitle
draw.text((100, 140), "Raymarching Signed Distance Fields in Real-Time GLSL", font=f_title, fill=C_INK)
draw.text((100, 226), "Sphere tracing algorithm, primitive distance functions, polynomial smooth min, and audio-reactive modulation", font=f_sub, fill=C_INK2)

# Divider line
draw.line([(100, 290), (W - 100, 290)], fill=C_LINE, width=2)

# Three Cards Layout
pad_x = 100
gap = 40
card_w = (W - (2 * pad_x) - (2 * gap)) // 3
card_y = 330
card_h = 940

cards = [
    ("01 / SPHERE TRACING", "Hart's unrolled step algorithm"),
    ("02 / SDF PRIMITIVES & SMIN", "Analytic distances and smooth union"),
    ("03 / AUDIO-REACTIVE GRAPH", "Real-time FFT to Field kernel")
]

for i, (ctitle, csub) in enumerate(cards):
    cx = pad_x + i * (card_w + gap)
    # Card background
    draw.rounded_rectangle([(cx, card_y), (cx + card_w, card_y + card_h)], radius=32, fill=C_SURFACE1, outline=C_LINE, width=2)
    # Header well inside card
    draw.rounded_rectangle([(cx + 4, card_y + 4), (cx + card_w - 4, card_y + 110)], radius=28, fill=C_DEEP)
    draw.text((cx + 36, card_y + 26), ctitle, font=f_card_title, fill=C_EMBER if i==1 else C_SIGNAL)
    draw.text((cx + 36, card_y + 70), csub, font=f_card_sub, fill=C_INK3)
    draw.line([(cx, card_y + 110), (cx + card_w, card_y + 110)], fill=C_LINE, width=2)

# ----------------- PANEL 1: Sphere Tracing Geometry Diagram -----------------
p1_x = pad_x
p1_cx = p1_x + card_w // 2

# Math box
draw.rounded_rectangle([(p1_x + 30, card_y + 135), (p1_x + card_w - 30, card_y + 250)], radius=16, fill=C_DEEP, outline=C_LINE, width=2)
draw.text((p1_x + 46, card_y + 148), "Ray: R(t) = R_0 + t * dir", font=f_mono_lg, fill=C_INK)
draw.text((p1_x + 46, card_y + 188), "Step: t += f(p),  where p = R(t)", font=f_mono, fill=C_SIGNAL)
draw.text((p1_x + 46, card_y + 218), "Hit:  f(p) < 0.001 (epsilon threshold)", font=f_mono, fill=C_INK2)

# Visual sphere tracing march
diag_y = card_y + 270
diag_h = 420
diag_w = card_w - 60
draw.rounded_rectangle([(p1_x + 30, diag_y), (p1_x + card_w - 30, diag_y + diag_h)], radius=20, fill=C_SURFACE2, outline=C_LINE, width=2)

# Camera icon/eye
eye_x = p1_x + 70
eye_y = diag_y + 280
draw.ellipse([(eye_x - 14, eye_y - 14), (eye_x + 14, eye_y + 14)], fill=C_EMBER)
draw.text((eye_x - 24, eye_y + 24), "Camera R_0", font=f_mono_sm, fill=C_INK)

# Target Surface (SDF geometry in blue)
geom_cx = p1_x + card_w - 140
geom_cy = diag_y + 140
geom_r = 110
draw.ellipse([(geom_cx - geom_r, geom_cy - geom_r), (geom_cx + geom_r, geom_cy + geom_r)], fill=(40, 50, 95, 200), outline=C_SIGNAL, width=4)
draw.text((geom_cx - 45, geom_cy - 12), "Geometry", font=f_card_sub, fill=C_INK)

# Ray vector line passing near geometry
ray_start = (eye_x, eye_y)
ray_end = (geom_cx - 65, geom_cy + 55)
draw.line([ray_start, (eye_x + 520, eye_y - 250)], fill=C_INK3, width=2)

# Sphere march step 1
p1_t1 = (eye_x + 130, eye_y - 62)
r1 = 120
draw.ellipse([(p1_t1[0] - r1, p1_t1[1] - r1), (p1_t1[0] + r1, p1_t1[1] + r1)], outline=(167, 177, 255, 140), width=2)
draw.ellipse([(p1_t1[0] - 6, p1_t1[1] - 6), (p1_t1[0] + 6, p1_t1[1] + 6)], fill=C_SIGNAL)
draw.text((p1_t1[0] - 25, p1_t1[1] + 12), "step 1", font=f_mono_sm, fill=C_SIGNAL)

# Sphere march step 2
p1_t2 = (eye_x + 245, eye_y - 118)
r2 = 72
draw.ellipse([(p1_t2[0] - r2, p1_t2[1] - r2), (p1_t2[0] + r2, p1_t2[1] + r2)], outline=(167, 177, 255, 170), width=2)
draw.ellipse([(p1_t2[0] - 6, p1_t2[1] - 6), (p1_t2[0] + 6, p1_t2[1] + 6)], fill=C_SIGNAL)
draw.text((p1_t2[0] - 25, p1_t2[1] + 10), "step 2", font=f_mono_sm, fill=C_SIGNAL)

# Sphere march step 3 (close)
p1_t3 = (eye_x + 325, eye_y - 156)
r3 = 34
draw.ellipse([(p1_t3[0] - r3, p1_t3[1] - r3), (p1_t3[0] + r3, p1_t3[1] + r3)], outline=C_EMBER, width=2)
draw.ellipse([(p1_t3[0] - 6, p1_t3[1] - 6), (p1_t3[0] + 6, p1_t3[1] + 6)], fill=C_EMBER)
draw.text((p1_t3[0] - 25, p1_t3[1] + 10), "step 3", font=f_mono_sm, fill=C_EMBER)

# Hit Point & Normal Vector
hit_pt = (eye_x + 360, eye_y - 172)
draw.ellipse([(hit_pt[0] - 9, hit_pt[1] - 9), (hit_pt[0] + 9, hit_pt[1] + 9)], fill=C_EMBER)
draw.line([hit_pt, (hit_pt[0] - 45, hit_pt[1] - 55)], fill=C_EMBER, width=4)
draw.text((hit_pt[0] - 90, hit_pt[1] - 80), "Normal n", font=f_mono, fill=C_EMBER)

# Code snippet for loop in Panel 1
draw.rounded_rectangle([(p1_x + 30, card_y + 710), (p1_x + card_w - 30, card_y + card_h - 25)], radius=16, fill=C_DEEP, outline=C_LINE, width=2)
c1_lines = [
    "float d = 0.0;",
    "for (int i = 0; i < 128; i++) {",
    "    vec3 p = ro + rd * d;",
    "    float h = map(p);",
    "    if (h < 0.001) return d; // hit",
    "    d += h;",
    "    if (d > 50.0) break;",
    "}"
]
for idx, cl in enumerate(c1_lines):
    col = C_EMBER if "return d" in cl else (C_SIGNAL if "map(p)" in cl else C_INK2)
    draw.text((p1_x + 46, card_y + 725 + idx * 24), cl, font=f_mono_sm, fill=col)

# ----------------- PANEL 2: SDF Primitives & Smooth Min -----------------
p2_x = pad_x + card_w + gap

# Mathematical Primitives formulas
draw.rounded_rectangle([(p2_x + 30, card_y + 135), (p2_x + card_w - 30, card_y + 400)], radius=16, fill=C_DEEP, outline=C_LINE, width=2)
p2_code = [
    "// 1. Sphere",
    "float sdSphere(vec3 p, float r) {",
    "    return length(p) - r;",
    "}",
    "// 2. Torus",
    "float sdTorus(vec3 p, vec2 t) {",
    "    vec2 q = vec2(length(p.xz) - t.x, p.y);",
    "    return length(q) - t.y;",
    "}"
]
for idx, cl in enumerate(p2_code):
    col = C_INK3 if cl.startswith("//") else (C_SIGNAL if "return" in cl else C_INK)
    draw.text((p2_x + 46, card_y + 148 + idx * 28), cl, font=f_mono, fill=col)

# Diagram of Smooth Minimum smin (Organic Blending)
smin_y = card_y + 420
smin_h = 270
draw.rounded_rectangle([(p2_x + 30, smin_y), (p2_x + card_w - 30, smin_y + smin_h)], radius=20, fill=C_SURFACE2, outline=C_LINE, width=2)

draw.text((p2_x + 50, smin_y + 20), "Polynomial Smooth Min (k = 0.35)", font=f_card_sub, fill=C_INK)
# Left circle
c_left = (p2_x + 220, smin_y + 145)
# Right circle
c_right = (p2_x + 460, smin_y + 145)
draw.ellipse([(c_left[0] - 70, c_left[1] - 70), (c_left[0] + 70, c_left[1] + 70)], fill=(35, 42, 75, 255), outline=C_SIGNAL, width=3)
draw.ellipse([(c_right[0] - 70, c_right[1] - 70), (c_right[0] + 70, c_right[1] + 70)], fill=(35, 42, 75, 255), outline=C_SIGNAL, width=3)

# Smooth blending bridge in Ember
bridge_points = [
    (c_left[0] + 25, c_left[1] - 65),
    (c_left[0] + 120, c_left[1] - 25),
    (c_right[0] - 25, c_right[1] - 65),
    (c_right[0] - 25, c_right[1] + 65),
    (c_left[0] + 120, c_left[1] + 25),
    (c_left[0] + 25, c_left[1] + 65),
]
draw.polygon(bridge_points, fill=(255, 107, 53, 90))
draw.line([(c_left[0] + 25, c_left[1] - 65), (c_left[0] + 120, c_left[1] - 25), (c_right[0] - 25, c_right[1] - 65)], fill=C_EMBER, width=3)
draw.line([(c_left[0] + 25, c_left[1] + 65), (c_left[0] + 120, c_left[1] + 25), (c_right[0] - 25, c_right[1] + 65)], fill=C_EMBER, width=3)

draw.text((c_left[0] - 40, c_left[1] - 12), "Body A", font=f_mono, fill=C_INK)
draw.text((c_right[0] - 40, c_right[1] - 12), "Body B", font=f_mono, fill=C_INK)
draw.text((p2_x + 305, smin_y + 133), "smin()", font=f_mono_lg, fill=C_EMBER)

# smin code box
draw.rounded_rectangle([(p2_x + 30, card_y + 710), (p2_x + card_w - 30, card_y + card_h - 25)], radius=16, fill=C_DEEP, outline=C_LINE, width=2)
smin_code = [
    "float smin(float a, float b, float k) {",
    "    float h = clamp(0.5 + 0.5 * (b - a) / k,",
    "                    0.0, 1.0);",
    "    return mix(b, a, h) - k * h * (1.0 - h);",
    "}",
    "// Finite differences normal",
    "vec3 calcNormal(vec3 p) {",
    "    vec2 e = vec2(0.001, 0.0);",
    "    return normalize(vec3(",
    "        map(p+e.xyy) - map(p-e.xyy),",
    "        map(p+e.yxy) - map(p-e.yxy),",
    "        map(p+e.yyx) - map(p-e.yyx)));",
    "}"
]
for idx, cl in enumerate(smin_code):
    col = C_EMBER if "smin" in cl or "mix" in cl else (C_SIGNAL if "calcNormal" in cl else C_INK2)
    draw.text((p2_x + 46, card_y + 725 + idx * 24), cl, font=f_mono_sm, fill=col)

# ----------------- PANEL 3: Infinite Audio-Reactive Node Graph -----------------
p3_x = pad_x + 2 * (card_w + gap)

def draw_node(x, y, w, h, title, cat, pins_in=None, pins_out=None, border=C_LINE):
    draw.rounded_rectangle([(x, y), (x + w, y + h)], radius=18, fill=C_DEEP, outline=border, width=2)
    # Header bar
    draw.rounded_rectangle([(x + 2, y + 2), (x + w - 2, y + 54)], radius=16, fill=C_SURFACE2)
    draw.text((x + 24, y + 14), title, font=f_card_title, fill=C_INK)
    draw.text((x + w - 140, y + 20), cat, font=f_badge, fill=C_SIGNAL)
    draw.line([(x, y + 54), (x + w, y + 54)], fill=C_LINE, width=2)
    # Pins
    if pins_in:
        for py, ptxt, pcol in pins_in:
            draw.ellipse([(x - 8, py - 8), (x + 8, py + 8)], fill=pcol, outline=C_INK, width=2)
            draw.text((x + 18, py - 10), ptxt, font=f_mono_sm, fill=C_INK2)
    if pins_out:
        for py, ptxt, pcol in pins_out:
            draw.ellipse([(x + w - 8, py - 8), (x + w + 8, py + 8)], fill=pcol, outline=C_INK, width=2)
            draw.text((x + w - 18 - len(ptxt)*12, py - 10), ptxt, font=f_mono_sm, fill=C_INK2)

# Node 1: Audio Analyze
na_x = p3_x + 40
na_y = card_y + 140
draw_node(na_x, na_y, 270, 190, "Audio Analyze", "DSP",
          pins_in=[(na_y + 90, "in", C_SIGNAL), (na_y + 140, "gain", C_INK3)],
          pins_out=[(na_y + 80, "low", C_EMBER), (na_y + 115, "mid", C_EMBER), (na_y + 150, "high", C_SIGNAL)],
          border=C_LINE)

# Node 2: LFO Modulator
nl_x = p3_x + 360
nl_y = card_y + 140
draw_node(nl_x, nl_y, 270, 190, "LFO", "MOD",
          pins_in=[(nl_y + 90, "sync", C_SIGNAL), (nl_y + 140, "rate", C_INK3)],
          pins_out=[(nl_y + 85, "out", C_SIGNAL), (nl_y + 135, "phase", C_SIGNAL)],
          border=C_LINE)

# Node 3: Field Pixel (Raymarching Kernel)
nf_x = p3_x + 60
nf_y = card_y + 400
draw_node(nf_x, nf_y, 550, 270, "Field Pixel (Raymarcher)", "GPU GLSL",
          pins_in=[
              (nf_y + 85, "param bass", C_EMBER),
              (nf_y + 125, "param morph", C_EMBER),
              (nf_y + 165, "param twist", C_SIGNAL),
              (nf_y + 205, "param orbit", C_SIGNAL)
          ],
          pins_out=[(nf_y + 140, "image", C_SIGNAL)],
          border=C_EMBER)

# Cable connections:
# Audio Analyze 'low' -> Field Pixel 'param bass'
c1_start = (na_x + 270, na_y + 80)
c1_end = (nf_x, nf_y + 85)
draw.line([c1_start, (c1_start[0] + 50, c1_start[1]), (c1_end[0] - 50, c1_end[1]), c1_end], fill=C_EMBER, width=4)

# Audio Analyze 'mid' -> Field Pixel 'param morph'
c2_start = (na_x + 270, na_y + 115)
c2_end = (nf_x, nf_y + 125)
draw.line([c2_start, (c2_start[0] + 30, c2_start[1]), (c2_end[0] - 40, c2_end[1]), c2_end], fill=C_EMBER, width=4)

# LFO 'out' -> Field Pixel 'param twist'
c3_start = (nl_x + 270, nl_y + 85)
c3_end = (nf_x, nf_y + 165)
# Draw bezier-like route
draw.line([c3_start, (c3_start[0] + 30, c3_start[1]), (c3_start[0] + 30, c3_start[1] + 180), (nf_x - 30, nf_y + 165), c3_end], fill=C_SIGNAL, width=3)

# Node 4: Bloom
nb_x = p3_x + 80
nb_y = card_y + 730
draw_node(nb_x, nb_y, 240, 160, "Bloom", "2D FX",
          pins_in=[(nb_y + 90, "image", C_SIGNAL)],
          pins_out=[(nb_y + 90, "image", C_SIGNAL)],
          border=C_LINE)

# Field Pixel 'image' -> Bloom 'image'
draw.line([(nf_x + 550, nf_y + 140), (nf_x + 580, nf_y + 140), (nf_x + 580, nb_y + 50), (nb_x + 120, nb_y + 50), (nb_x + 120, nb_y + 90), (nb_x, nb_y + 90)], fill=C_SIGNAL, width=4)

# Node 5: Output
no_x = p3_x + 390
no_y = card_y + 730
draw_node(no_x, no_y, 240, 160, "Output", "SINK",
          pins_in=[(no_y + 90, "video", C_SIGNAL)],
          pins_out=None,
          border=C_LINE)

# Bloom 'image' -> Output 'video'
draw.line([(nb_x + 240, nb_y + 90), (no_x, no_y + 90)], fill=C_SIGNAL, width=4)

# Status Pill at bottom right
draw.rounded_rectangle([(p3_x + 40, card_y + card_h - 60), (p3_x + card_w - 40, card_y + card_h - 15)], radius=14, fill=C_DEEP, outline=C_LINE, width=1)
draw.ellipse([(p3_x + 65, card_y + card_h - 43), (p3_x + 77, card_y + card_h - 31)], fill=(46, 213, 115))
draw.text((p3_x + 95, card_y + card_h - 48), "60 FPS · Zero audio-thread hitches · GPU GLSL", font=f_mono_sm, fill=C_INK2)

# Downsample to target 1200x675
target_w, target_h = 1200, 675
final_im = im.resize((target_w, target_h), Image.LANCZOS)

out_paths = [
    "/Users/namansoni/Infinite/worktrees/main/website/assets/blog_raymarching_signed_distance_fields_in_real_time_glsl.png",
    "/Users/namansoni/Infinite/repo/website/assets/blog_raymarching_signed_distance_fields_in_real_time_glsl.png"
]

for p in out_paths:
    os.makedirs(os.path.dirname(p), exist_ok=True)
    final_im.save(p, "PNG")
    print("Saved:", p)
