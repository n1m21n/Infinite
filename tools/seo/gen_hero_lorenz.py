#!/usr/bin/env python3
"""Generate the technical hero diagram for the Lorenz attractor interactive simulation post.
Outputs a crisp 1200x675 brand-compliant image to website/assets/blog_lorenz_attractor_interactive_simulation.png.
"""
import math
import os
from PIL import Image, ImageDraw, ImageFont

W, H = 1200, 675
img = Image.new("RGBA", (W, H), (13, 16, 32, 255))  # Midnight deep #0D1020
draw = ImageDraw.Draw(img)

# Fonts
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FONT_DIR = os.path.join(REPO_ROOT, "art", "brand", "fonts")
GEIST_SEMIBOLD = os.path.join(FONT_DIR, "Geist-SemiBold.ttf")
GEIST_MONO = os.path.join(FONT_DIR, "GeistMono-Medium.ttf")

try:
    f_title = ImageFont.truetype(GEIST_SEMIBOLD, 28)
    f_sub = ImageFont.truetype(GEIST_SEMIBOLD, 14)
    f_badge = ImageFont.truetype(GEIST_MONO, 11)
    f_body = ImageFont.truetype(GEIST_SEMIBOLD, 12)
    f_mono = ImageFont.truetype(GEIST_MONO, 12)
    f_mono_sm = ImageFont.truetype(GEIST_MONO, 11)
    f_formula = ImageFont.truetype(GEIST_MONO, 13)
except Exception:
    f_title = f_sub = f_badge = f_body = f_mono = f_mono_sm = f_formula = ImageFont.load_default()

# 1. Subtle background dot grid
dot_color = (35, 42, 74, 180)  # #232A4A
for gx in range(24, W - 24, 28):
    for gy in range(24, H - 24, 28):
        draw.ellipse([gx - 1, gy - 1, gx + 1, gy + 1], fill=dot_color)

# 2. Outer decorative frame
draw.rounded_rectangle([20, 20, W - 20, H - 20], radius=16, outline=(42, 48, 82, 255), width=1)

# 3. Header section
# Top pill badge: INFINITE // CHAOS DYNAMICS & SIMULATION
pill_box = [45, 42, 335, 68]
draw.rounded_rectangle(pill_box, radius=6, fill=(32, 38, 63, 255), outline=(52, 59, 94, 255), width=1)
draw.text((58, 48), "INFINITE  //  CHAOS DYNAMICS & SIMULATION", font=f_badge, fill=(167, 177, 255, 255))

# Right pill: TRACK B: CREATIVE CODING
draw.rounded_rectangle([W - 250, 42, W - 45, 68], radius=6, fill=(32, 38, 63, 255), outline=(52, 59, 94, 255), width=1)
draw.text((W - 238, 48), "TRACK B: CREATIVE CODING", font=f_badge, fill=(238, 241, 250, 255))

# Main title and subtitle
draw.text((45, 84), "Lorenz Attractor Interactive Simulation", font=f_title, fill=(238, 241, 250, 255))
draw.text((45, 122), "Differential equations, phase space integration, and real-time audio-reactive chaos modulation", font=f_sub, fill=(163, 170, 203, 255))

# Horizontal rule below header
draw.line([(45, 148), (W - 45, 148)], fill=(42, 48, 82, 255), width=1)

# 4. Left Pane: Numerical Integration Phase Space Projection
# Plot bounds for attractor: x in [-22, 22], z in [5, 48]
lx1, ly1, lx2, ly2 = 45, 168, 535, H - 42
draw.rounded_rectangle([lx1, ly1, lx2, ly2], radius=12, fill=(21, 25, 48, 255), outline=(42, 48, 82, 255), width=1)

# Inner graph box
gx1, gy1, gx2, gy2 = lx1 + 20, ly1 + 38, lx2 - 20, ly2 - 20
draw.rounded_rectangle([gx1, gy1, gx2, gy2], radius=8, fill=(13, 16, 32, 255), outline=(35, 42, 74, 255), width=1)

# Title above inner graph box
draw.text((lx1 + 20, ly1 + 16), "PHASE SPACE TRAJECTORY  (X-Z PROJECTION)", font=f_badge, fill=(255, 107, 53, 255))

# Compute Lorenz Attractor points
sigma = 10.0
rho = 28.0
beta = 8.0 / 3.0
dt = 0.007

x, y, z = 0.1, 0.0, 0.0
raw_pts = []
for _ in range(2600):
    dx = sigma * (y - x)
    dy = x * (rho - z) - y
    dz = x * y - beta * z
    x += dx * dt
    y += dy * dt
    z += dz * dt
    raw_pts.append((x, y, z))

# Scale points into gx1..gx2, gy1..gy2
# x_range: [-22, 22], z_range: [2, 48]
xmin, xmax = -22.0, 22.0
zmin, zmax = 2.0, 48.0

def proj(px, pz):
    sx = gx1 + 20 + (px - xmin) / (xmax - xmin) * (gx2 - gx1 - 40)
    sy = gy2 - 20 - (pz - zmin) / (zmax - zmin) * (gy2 - gy1 - 40)
    return sx, sy

# Draw subtle grid lines inside graph
for gz in [10, 20, 30, 40]:
    _, py = proj(0, gz)
    draw.line([(gx1 + 12, py), (gx2 - 12, py)], fill=(32, 38, 63, 140), width=1)
    draw.text((gx1 + 15, py - 12), f"z={gz}", font=f_mono_sm, fill=(98, 107, 144, 200))

for gx in [-15, 0, 15]:
    px, _ = proj(gx, 0)
    draw.line([(px, gy1 + 12), (px, gy2 - 12)], fill=(32, 38, 63, 140), width=1)
    draw.text((px - 10, gy2 - 18), f"x={gx}", font=f_mono_sm, fill=(98, 107, 144, 200))

# Fixed equilibrium points C+ and C-
# C+ = (+sqrt(beta*(rho-1)), +sqrt(beta*(rho-1)), rho-1)
c_val = math.sqrt(beta * (rho - 1.0))
cp_x, cp_z = c_val, rho - 1.0
cm_x, cm_z = -c_val, rho - 1.0

# Render glowing fixed points
for fx, fz, label in [(cp_x, cp_z, "C+"), (cm_x, cm_z, "C-")]:
    cx_p, cy_p = proj(fx, fz)
    draw.ellipse([cx_p - 10, cy_p - 10, cx_p + 10, cy_p + 10], fill=(255, 107, 53, 40))
    draw.ellipse([cx_p - 5, cy_p - 5, cx_p + 5, cy_p + 5], fill=(255, 107, 53, 200), outline=(238, 241, 250, 255), width=1)
    draw.line([(cx_p - 8, cy_p), (cx_p + 8, cy_p)], fill=(255, 158, 121, 255), width=1)
    draw.line([(cx_p, cy_p - 8), (cx_p, cy_p + 8)], fill=(255, 158, 121, 255), width=1)
    draw.text((cx_p + 8, cy_p - 16), label, font=f_badge, fill=(255, 158, 121, 255))

# Draw the continuous attractor trajectory with a vibrant colour ramp
n_pts = len(raw_pts)
trail_start = 200
for i in range(trail_start + 1, n_pts):
    p0 = raw_pts[i - 1]
    p1 = raw_pts[i]
    x0, y0 = proj(p0[0], p0[2])
    x1, y1 = proj(p1[0], p1[2])
    
    t_ratio = (i - trail_start) / float(n_pts - trail_start)
    
    # Colour gradient: dark blue/indigo -> signal purple -> cyan -> bright orange
    if t_ratio < 0.35:
        sub = t_ratio / 0.35
        r = int(52 + (122 - 52) * sub)
        g = int(59 + (82 - 59) * sub)
        b = int(140 + (255 - 140) * sub)
        alpha = int(100 + 80 * sub)
        w_line = 1
    elif t_ratio < 0.75:
        sub = (t_ratio - 0.35) / 0.40
        r = int(122 + (56 - 122) * sub)
        g = int(82 + (189 - 82) * sub)
        b = int(255 + (248 - 255) * sub)
        alpha = int(180 + 50 * sub)
        w_line = 2
    else:
        sub = (t_ratio - 0.75) / 0.25
        r = int(56 + (255 - 56) * sub)
        g = int(189 + (107 - 189) * sub)
        b = int(248 + (53 - 248) * sub)
        alpha = 255
        w_line = 2

    draw.line([(x0, y0), (x1, y1)], fill=(r, g, b, alpha), width=w_line)

# Trajectory Head Indicator
head_x, head_y = proj(raw_pts[-1][0], raw_pts[-1][2])
draw.ellipse([head_x - 8, head_y - 8, head_x + 8, head_y + 8], fill=(255, 107, 53, 90))
draw.ellipse([head_x - 4, head_y - 4, head_x + 4, head_y + 4], fill=(255, 107, 53, 255), outline=(238, 241, 250, 255), width=1)
draw.text((head_x + 10, head_y - 8), "t = 18.2s (head)", font=f_mono_sm, fill=(255, 158, 121, 255))

# Labels for lobes
draw.text((gx1 + 35, gy1 + 30), "LOBE A (x < 0)", font=f_badge, fill=(167, 177, 255, 220))
draw.text((gx2 - 110, gy1 + 30), "LOBE B (x > 0)", font=f_badge, fill=(167, 177, 255, 220))

# Parameter watermark pill inside the graph
p_pill = [gx1 + 14, gy2 - 38, gx1 + 245, gy2 - 14]
draw.rounded_rectangle(p_pill, radius=4, fill=(21, 25, 48, 230), outline=(42, 48, 82, 255), width=1)
draw.text((gx1 + 22, gy2 - 33), "sigma=10.0  rho=28.0  beta=8/3", font=f_mono_sm, fill=(238, 241, 250, 255))


# 5. Right Pane: Technical Cards (x=555 to W-45)
rx1, ry1, rx2, ry2 = 555, 168, W - 45, H - 42
draw.rounded_rectangle([rx1, ry1, rx2, ry2], radius=12, fill=(21, 25, 48, 255), outline=(42, 48, 82, 255), width=1)

# Card Section 1: Non-Linear Differential Equations
draw.text((rx1 + 20, ry1 + 16), "LORENZ SYSTEM OF EQUATIONS", font=f_badge, fill=(255, 107, 53, 255))

fbox = [rx1 + 20, ry1 + 36, rx2 - 20, ry1 + 92]
draw.rounded_rectangle(fbox, radius=6, fill=(13, 16, 32, 255), outline=(35, 42, 74, 255), width=1)
draw.text((rx1 + 30, ry1 + 44), "dx/dt = sigma * (y - x)", font=f_formula, fill=(238, 241, 250, 255))
draw.text((rx1 + 30, ry1 + 65), "dy/dt = x * (rho - z) - y", font=f_formula, fill=(238, 241, 250, 255))
draw.text((rx1 + 320, ry1 + 44), "dz/dt = x * y - beta * z", font=f_formula, fill=(238, 241, 250, 255))
draw.text((rx1 + 320, ry1 + 65), "fixed pts: C+, C- (bifurcation at rho=1)", font=f_mono_sm, fill=(163, 170, 203, 255))

# Card Section 2: Real-Time Sketch Kernel
draw.text((rx1 + 20, ry1 + 106), "REAL-TIME SKETCH INTEGRATOR", font=f_badge, fill=(167, 177, 255, 255))

cbox = [rx1 + 20, ry1 + 126, rx2 - 20, ry1 + 276]
draw.rounded_rectangle(cbox, radius=6, fill=(13, 16, 32, 255), outline=(35, 42, 74, 255), width=1)

code_lines = [
    ("// Lorenz attractor live integration in an Infinite Sketch node", (98, 107, 144, 255)),
    ("param(\"rho\", 28, 10, 40);  param(\"trail\", 1400, 200, 4000);", (167, 177, 255, 255)),
    ("const s = 10, b = 8 / 3, dt = 0.006;", (238, 241, 250, 255)),
    ("for (let i = 0; i < steps; i++) {", (238, 241, 250, 255)),
    ("  const dx = s * (y - x), dy = x * (rho - z) - y, dz = x * y - b * z;", (255, 107, 53, 255)),
    ("  x += dx * dt; y += dy * dt; z += dz * dt;", (238, 241, 250, 255)),
    ("  pts.push([x, y, z]);", (163, 170, 203, 255)),
    ("}", (238, 241, 250, 255)),
    ("stroke(hsl(0.58 - 0.5 * f, 0.8, 0.35 + 0.35 * f)); line(...);", (56, 189, 248, 255)),
]

line_y = ry1 + 134
for text, col in code_lines:
    draw.text((rx1 + 30, line_y), text, font=f_mono_sm, fill=col)
    line_y += 15

# Card Section 3: Audio-Reactive Signal Flow
draw.text((rx1 + 20, ry1 + 290), "AUDIO-REACTIVE MODULAR GRAPH", font=f_badge, fill=(238, 241, 250, 255))

gbox = [rx1 + 20, ry1 + 310, rx2 - 20, ry2 - 18]
draw.rounded_rectangle(gbox, radius=6, fill=(13, 16, 32, 255), outline=(35, 42, 74, 255), width=1)

def draw_node(bx, by, bw, bh, title, sub, stroke_col):
    draw.rounded_rectangle([bx, by, bx + bw, by + bh], radius=6, fill=(32, 38, 63, 255), outline=stroke_col, width=1)
    draw.text((bx + 10, by + 7), title, font=f_body, fill=(238, 241, 250, 255))
    draw.text((bx + 10, by + 23), sub, font=f_mono_sm, fill=(163, 170, 203, 255))

# Row 1: Audio Modulation into Sketch
draw_node(rx1 + 32, ry1 + 324, 115, 42, "Audio Analyze", "8-BAND FFT", (74, 84, 214, 255))
draw_node(rx1 + 190, ry1 + 324, 110, 42, "Math", "SCALE (10..40)", (255, 107, 53, 255))
draw_node(rx1 + 345, ry1 + 324, 125, 42, "Sketch", "LORENZ CHAOS", (56, 189, 248, 255))

# Wire 1: Audio Analyze -> Math
draw.line([(rx1 + 147, ry1 + 345), (rx1 + 190, ry1 + 345)], fill=(255, 107, 53, 255), width=2)
draw.ellipse([rx1 + 145, ry1 + 343, rx1 + 149, ry1 + 347], fill=(255, 107, 53, 255))
draw.ellipse([rx1 + 188, ry1 + 343, rx1 + 192, ry1 + 347], fill=(255, 107, 53, 255))

# Wire 2: Math -> Sketch (rho param)
draw.line([(rx1 + 300, ry1 + 345), (rx1 + 345, ry1 + 345)], fill=(255, 107, 53, 255), width=2)
draw.ellipse([rx1 + 298, ry1 + 343, rx1 + 302, ry1 + 347], fill=(255, 107, 53, 255))
draw.ellipse([rx1 + 343, ry1 + 343, rx1 + 347, ry1 + 347], fill=(255, 107, 53, 255))

# Row 2: Sonification & 3D Render
draw_node(rx1 + 190, ry1 + 384, 110, 42, "Oscillator", "FM SONIFY (X)", (167, 177, 255, 255))
draw_node(rx1 + 345, ry1 + 384, 125, 42, "Render 3D", "RIBBON MESH", (74, 84, 214, 255))

# Wire 3: Sketch branch down to Oscillator and Render 3D
draw.line([(rx1 + 405, ry1 + 366), (rx1 + 405, ry1 + 384)], fill=(56, 189, 248, 255), width=2)
draw.line([(rx1 + 345, ry1 + 355), (rx1 + 320, ry1 + 355), (rx1 + 320, ry1 + 405), (rx1 + 300, ry1 + 405)], fill=(167, 177, 255, 200), width=2)

# Save image
out_path = os.path.join(REPO_ROOT, "website", "assets", "blog_lorenz_attractor_interactive_simulation.png")
os.makedirs(os.path.dirname(out_path), exist_ok=True)
img.convert("RGB").save(out_path, "PNG", optimize=True)
print(f"Generated {out_path} ({W}x{H})")
