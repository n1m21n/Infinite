#!/usr/bin/env python3
"""Generate the technical hero diagram for the reaction-diffusion simulation post.
Outputs a crisp 1200x675 brand-compliant image to website/assets/blog_reaction_diffusion_simulation_in_real_time_glsl.png.
"""
import math
import os
import random
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
    f_title = ImageFont.truetype(GEIST_SEMIBOLD, 26)
    f_sub = ImageFont.truetype(GEIST_SEMIBOLD, 13)
    f_badge = ImageFont.truetype(GEIST_MONO, 11)
    f_card_title = ImageFont.truetype(GEIST_SEMIBOLD, 14)
    f_body = ImageFont.truetype(GEIST_SEMIBOLD, 12)
    f_mono = ImageFont.truetype(GEIST_MONO, 12)
    f_mono_sm = ImageFont.truetype(GEIST_MONO, 10)
    f_formula = ImageFont.truetype(GEIST_MONO, 12)
except Exception:
    f_title = f_sub = f_badge = f_card_title = f_body = f_mono = f_mono_sm = f_formula = ImageFont.load_default()

# 1. Subtle background dot grid
dot_color = (35, 42, 74, 180)  # #232A4A
for gx in range(24, W - 24, 28):
    for gy in range(24, H - 24, 28):
        draw.ellipse([gx - 1, gy - 1, gx + 1, gy + 1], fill=dot_color)

# 2. Outer decorative frame
draw.rounded_rectangle([20, 20, W - 20, H - 20], radius=16, outline=(42, 48, 82, 255), width=1)

# 3. Header section
# Top pill badge: INFINITE // REACTION-DIFFUSION & TURING MORPHOGENESIS
pill_box = [45, 40, 420, 66]
draw.rounded_rectangle(pill_box, radius=6, fill=(32, 38, 63, 255), outline=(52, 59, 94, 255), width=1)
draw.text((58, 46), "INFINITE  //  REACTION-DIFFUSION & TURING PATTERNS", font=f_badge, fill=(167, 177, 255, 255))

# Right pill: TRACK B: CREATIVE CODING
draw.rounded_rectangle([W - 245, 40, W - 45, 66], radius=6, fill=(32, 38, 63, 255), outline=(52, 59, 94, 255), width=1)
draw.text((W - 233, 46), "TRACK B: CREATIVE CODING", font=f_badge, fill=(238, 241, 250, 255))

# Main title and subtitle
draw.text((45, 82), "Reaction-Diffusion Simulation in Real-Time GLSL", font=f_title, fill=(238, 241, 250, 255))
draw.text((45, 118), "Gray-Scott chemical morphogenesis, 9-point Laplacian stencils, and audio-reactive rate modulation", font=f_sub, fill=(163, 170, 203, 255))

# Horizontal rule below header
draw.line([(45, 144), (W - 45, 144)], fill=(42, 48, 82, 255), width=1)

# Card styling helper
def draw_card_shell(x1, y1, x2, y2, title, kicker=None):
    draw.rounded_rectangle([x1, y1, x2, y2], radius=12, fill=(21, 25, 48, 255), outline=(42, 48, 82, 255), width=1)
    draw.rounded_rectangle([x1, y1, x2, y1 + 38], radius=12, fill=(32, 38, 63, 255))
    draw.rectangle([x1, y1 + 26, x2, y1 + 38], fill=(32, 38, 63, 255))
    draw.line([(x1, y1 + 38), (x2, y1 + 38)], fill=(52, 59, 94, 255), width=1)
    draw.text((x1 + 16, y1 + 11), title, font=f_card_title, fill=(238, 241, 250, 255))
    if kicker:
        kw = draw.textlength(kicker, font=f_mono_sm) + 12
        draw.rounded_rectangle([x2 - 16 - kw, y1 + 9, x2 - 16, y1 + 29], radius=4, fill=(52, 59, 94, 255))
        draw.text((x2 - 16 - kw + 6, y1 + 13), kicker, font=f_mono_sm, fill=(167, 177, 255, 255))

# Pane layout: 3 columns
col1_x1, col1_x2 = 45, 400
col2_x1, col2_x2 = 416, 784
col3_x1, col3_x2 = 800, W - 45
card_y1, card_y2 = 160, H - 42

# --- COLUMN 1: Mathematical Formulation & Stencil ---
draw_card_shell(col1_x1, card_y1, col1_x2, card_y2, "Gray-Scott System", "PDE & STENCIL")

# Equation Box
eq_box = [col1_x1 + 14, card_y1 + 48, col1_x2 - 14, card_y1 + 126]
draw.rounded_rectangle(eq_box, radius=8, fill=(13, 16, 32, 255), outline=(35, 42, 74, 255), width=1)
draw.text((col1_x1 + 22, card_y1 + 54), "CONTINUOUS PARTIAL DERIVATIVES", font=f_mono_sm, fill=(136, 144, 177, 255))
draw.text((col1_x1 + 22, card_y1 + 74), "dA/dt = D_a*∇²A - A*B² + F*(1 - A)", font=f_formula, fill=(238, 241, 250, 255))
draw.text((col1_x1 + 22, card_y1 + 98), "dB/dt = D_b*∇²B + A*B² - (F + k)*B", font=f_formula, fill=(255, 107, 53, 255))

# Stencil Box
st_y1 = card_y1 + 136
st_y2 = st_y1 + 152
draw.rounded_rectangle([col1_x1 + 14, st_y1, col1_x2 - 14, st_y2], radius=8, fill=(13, 16, 32, 255), outline=(35, 42, 74, 255), width=1)
draw.text((col1_x1 + 22, st_y1 + 8), "9-POINT LAPLACIAN CONVOLUTION STENCIL", font=f_mono_sm, fill=(136, 144, 177, 255))

grid_cx = col1_x1 + 80
grid_cy = st_y1 + 84
cell_s = 32
stencil_vals = [
    ["+0.05", "+0.20", "+0.05"],
    ["+0.20", "-1.00", "+0.20"],
    ["+0.05", "+0.20", "+0.05"],
]
for r in range(3):
    for c in range(3):
        cx = grid_cx + (c - 1) * cell_s
        cy = grid_cy + (r - 1) * cell_s
        val = stencil_vals[r][c]
        is_ctr = (r == 1 and c == 1)
        bg = (52, 59, 94, 255) if is_ctr else (21, 25, 48, 255)
        fg = (255, 107, 53, 255) if is_ctr else (167, 177, 255, 255)
        draw.rectangle([cx - 15, cy - 15, cx + 15, cy + 15], fill=bg, outline=(42, 48, 82, 255))
        draw.text((cx - 13, cy - 6), val, font=f_mono_sm, fill=fg)

# Stencil explanation beside grid
draw.text((grid_cx + 58, st_y1 + 42), "Δx = 1.0 (texel)", font=f_mono_sm, fill=(163, 170, 203, 255))
draw.text((grid_cx + 58, st_y1 + 62), "Direct: weight 0.20", font=f_mono_sm, fill=(163, 170, 203, 255))
draw.text((grid_cx + 58, st_y1 + 82), "Diagonal: weight 0.05", font=f_mono_sm, fill=(163, 170, 203, 255))
draw.text((grid_cx + 58, st_y1 + 102), "Center: -1.00", font=f_mono_sm, fill=(255, 107, 53, 255))
draw.text((grid_cx + 58, st_y1 + 122), "Sum of weights = 0.0", font=f_mono_sm, fill=(74, 222, 128, 255))

# Parameter Regimes Table
reg_y1 = st_y2 + 10
reg_y2 = card_y2 - 14
draw.rounded_rectangle([col1_x1 + 14, reg_y1, col1_x2 - 14, reg_y2], radius=8, fill=(13, 16, 32, 255), outline=(35, 42, 74, 255), width=1)
draw.text((col1_x1 + 22, reg_y1 + 8), "CANONICAL PARAMETER REGIMES (F, k)", font=f_mono_sm, fill=(136, 144, 177, 255))

regimes = [
    ("Coral Growth", "F = 0.054", "k = 0.062", (167, 177, 255, 255)),
    ("Mitosis / Cells", "F = 0.036", "k = 0.065", (74, 222, 128, 255)),
    ("Soliton Waves", "F = 0.030", "k = 0.062", (255, 107, 53, 255)),
    ("Labyrinth Maze", "F = 0.029", "k = 0.057", (238, 241, 250, 255)),
]
for idx, (name, f_val, k_val, col) in enumerate(regimes):
    ry = reg_y1 + 30 + idx * 29
    draw.ellipse([col1_x1 + 24, ry + 4, col1_x1 + 30, ry + 10], fill=col)
    draw.text((col1_x1 + 38, ry), name, font=f_body, fill=(238, 241, 250, 255))
    draw.text((col1_x2 - 140, ry + 1), f_val, font=f_mono_sm, fill=col)
    draw.text((col1_x2 - 76, ry + 1), k_val, font=f_mono_sm, fill=(163, 170, 203, 255))


# --- COLUMN 2: Morphogenesis Simulation Preview ---
draw_card_shell(col2_x1, card_y1, col2_x2, card_y2, "Morphogenesis Render", "GRAY-SCOTT")

# We will generate a real reaction-diffusion pattern using a fast numerical grid
# Grid size: 140 x 140
GW, GH = 140, 140
random.seed(42)
u = [[1.0 for _ in range(GW)] for _ in range(GH)]
v = [[0.0 for _ in range(GW)] for _ in range(GH)]

# Seed initial spot perturbations
for cy, cx, rad in [(45, 45, 14), (95, 95, 16), (45, 95, 12), (95, 45, 12), (70, 70, 18), (30, 70, 8), (110, 70, 8)]:
    for y in range(max(0, cy - rad), min(GH, cy + rad)):
        for x in range(max(0, cx - rad), min(GW, cx + rad)):
            if (x - cx)**2 + (y - cy)**2 <= rad**2:
                u[y][x] = 0.5 + random.uniform(-0.02, 0.02)
                v[y][x] = 0.25 + random.uniform(-0.02, 0.02)

# Run 160 simulation steps (Labyrinth / Coral mixture)
Da, Db = 1.0, 0.5
F, k = 0.037, 0.060
dt = 1.0

for step in range(140):
    new_u = [[1.0 for _ in range(GW)] for _ in range(GH)]
    new_v = [[0.0 for _ in range(GW)] for _ in range(GH)]
    for y in range(GH):
        ym1 = (y - 1) % GH
        yp1 = (y + 1) % GH
        for x in range(GW):
            xm1 = (x - 1) % GW
            xp1 = (x + 1) % GW
            
            # Laplacian
            lap_u = (
                0.20 * (u[y][xm1] + u[y][xp1] + u[ym1][x] + u[yp1][x]) +
                0.05 * (u[ym1][xm1] + u[ym1][xp1] + u[yp1][xm1] + u[yp1][xp1]) -
                1.00 * u[y][x]
            )
            lap_v = (
                0.20 * (v[y][xm1] + v[y][xp1] + v[ym1][x] + v[yp1][x]) +
                0.05 * (v[ym1][xm1] + v[ym1][xp1] + v[yp1][xm1] + v[yp1][xp1]) -
                1.00 * v[y][x]
            )
            
            uvv = u[y][x] * v[y][x] * v[y][x]
            new_u[y][x] = max(0.0, min(1.0, u[y][x] + (Da * lap_u - uvv + F * (1.0 - u[y][x])) * dt))
            new_v[y][x] = max(0.0, min(1.0, v[y][x] + (Db * lap_v + uvv - (F + k) * v[y][x]) * dt))
    u, v = new_u, new_v

# Render simulation grid to image with brand color mapping
sim_img = Image.new("RGBA", (GW, GH))
for y in range(GH):
    for x in range(GW):
        val = max(0.0, min(1.0, v[y][x] * 3.2))
        # Brand ramp: Midnight Deep -> Signal Violet -> Ember Orange -> Cream White
        if val < 0.25:
            t = val / 0.25
            r = int(13 + (32 - 13) * t)
            g = int(16 + (38 - 16) * t)
            b = int(32 + (63 - 32) * t)
        elif val < 0.6:
            t = (val - 0.25) / 0.35
            r = int(32 + (129 - 32) * t)
            g = int(38 + (140 - 38) * t)
            b = int(63 + (248 - 63) * t)
        elif val < 0.85:
            t = (val - 0.6) / 0.25
            r = int(129 + (255 - 129) * t)
            g = int(140 + (107 - 140) * t)
            b = int(248 + (53 - 248) * t)
        else:
            t = (val - 0.85) / 0.15
            r = int(255 + (238 - 255) * t)
            g = int(107 + (241 - 107) * t)
            b = int(53 + (250 - 53) * t)
        sim_img.putpixel((x, y), (r, g, b, 255))

# Scale simulation to fit preview box: 336 x 336
sim_disp_w = col2_x2 - col2_x1 - 28
sim_disp_h = sim_disp_w
sim_scaled = sim_img.resize((sim_disp_w, sim_disp_h), Image.Resampling.BILINEAR)

prev_box_x = col2_x1 + 14
prev_box_y = card_y1 + 48
img.paste(sim_scaled, (prev_box_x, prev_box_y))
draw.rounded_rectangle([prev_box_x, prev_box_y, prev_box_x + sim_disp_w, prev_box_y + sim_disp_h], radius=8, outline=(42, 48, 82, 255), width=1)

# Overlay info badges inside preview
draw.rounded_rectangle([prev_box_x + 10, prev_box_y + 10, prev_box_x + 184, prev_box_y + 32], radius=4, fill=(13, 16, 32, 220), outline=(52, 59, 94, 255))
draw.text((prev_box_x + 16, prev_box_y + 14), "CONCENTRATION B [0.0 -> 1.0]", font=f_mono_sm, fill=(255, 107, 53, 255))

draw.rounded_rectangle([prev_box_x + sim_disp_w - 92, prev_box_y + 10, prev_box_x + sim_disp_w - 10, prev_box_y + 32], radius=4, fill=(13, 16, 32, 220), outline=(52, 59, 94, 255))
draw.text((prev_box_x + sim_disp_w - 84, prev_box_y + 14), "140 STEPS", font=f_mono_sm, fill=(74, 222, 128, 255))

# Below preview: status indicators
inf_y = prev_box_y + sim_disp_h + 12
draw.rounded_rectangle([prev_box_x, inf_y, prev_box_x + sim_disp_w, card_y2 - 14], radius=8, fill=(13, 16, 32, 255), outline=(35, 42, 74, 255), width=1)
draw.text((prev_box_x + 12, inf_y + 8), "GPU BUFFER ALLOCATION", font=f_mono_sm, fill=(136, 144, 177, 255))
draw.text((prev_box_x + 12, inf_y + 26), "Ping-pong FBO: 512x512 RG32F (Texture A / B)", font=f_mono_sm, fill=(238, 241, 250, 255))
draw.text((prev_box_x + 12, inf_y + 44), "Computation: 8 substeps/frame (480 updates/s)", font=f_mono_sm, fill=(167, 177, 255, 255))
draw.text((prev_box_x + 12, inf_y + 62), "Precision: 32-bit floating point, zero drift", font=f_mono_sm, fill=(74, 222, 128, 255))


# --- COLUMN 3: Modular Node Graph Pipeline ---
draw_card_shell(col3_x1, card_y1, col3_x2, card_y2, "Node Canvas Pipeline", "MODULAR ROUTING")

def draw_node_badge(nx, ny, nw, nh, name, category, cat_color, in_pins, out_pins):
    # Node container
    draw.rounded_rectangle([nx, ny, nx + nw, ny + nh], radius=8, fill=(13, 16, 32, 255), outline=(42, 48, 82, 255), width=1)
    # Header strip
    draw.rounded_rectangle([nx, ny, nx + nw, ny + 24], radius=8, fill=(32, 38, 63, 255))
    draw.rectangle([nx, ny + 14, nx + nw, ny + 24], fill=(32, 38, 63, 255))
    draw.line([(nx, ny + 24), (nx + nw, ny + 24)], fill=cat_color, width=2)
    # Header label
    draw.text((nx + 10, ny + 5), name, font=f_body, fill=(238, 241, 250, 255))
    cw = draw.textlength(category, font=f_mono_sm)
    draw.text((nx + nw - 10 - cw, ny + 6), category, font=f_mono_sm, fill=(163, 170, 203, 255))
    
    # Input pins (left)
    for idx, pin in enumerate(in_pins):
        py = ny + 36 + idx * 18
        draw.ellipse([nx - 5, py - 4, nx + 3, py + 4], fill=cat_color, outline=(13, 16, 32, 255), width=1)
        draw.text((nx + 8, py - 6), pin, font=f_mono_sm, fill=(163, 170, 203, 255))
        
    # Output pins (right)
    for idx, pin in enumerate(out_pins):
        py = ny + 36 + idx * 18
        draw.ellipse([nx + nw - 3, py - 4, nx + nw + 5, py + 4], fill=cat_color, outline=(13, 16, 32, 255), width=1)
        pw = draw.textlength(pin, font=f_mono_sm)
        draw.text((nx + nw - 8 - pw, py - 6), pin, font=f_mono_sm, fill=(163, 170, 203, 255))

# Draw 4 Node Boxes
nw = col3_x2 - col3_x1 - 28
nx = col3_x1 + 14

# 1. Audio Analyze Node
n1_y = card_y1 + 48
n1_h = 76
draw_node_badge(nx, n1_y, nw, n1_h, "Audio Analyze", "DSP", (255, 107, 53, 255), ["audio in"], ["sub-bass", "hi-mid", "level"])

# 2. Reaction Diffusion Node
n2_y = n1_y + 96
n2_h = 100
draw_node_badge(nx, n2_y, nw, n2_h, "Reaction Diffusion", "COMPOSITING", (129, 140, 248, 255), ["feed (mod)", "kill (mod)", "seed in"], ["picture out", "chemical B"])

# 3. Color Ramp Node
n3_y = n2_y + 118
n3_h = 66
draw_node_badge(nx, n3_y, nw, n3_h, "Color Ramp", "COMPOSITING", (129, 140, 248, 255), ["input"], ["picture out"])

# 4. Output / 3D Material
n4_y = n3_y + 84
n4_h = 66
draw_node_badge(nx, n4_y, nw, n4_h, "Output & 3D Material", "UTILITY", (74, 222, 128, 255), ["picture in", "displacement"], ["display / GL"])

# Cables between nodes
def draw_curved_cable(x1, y1, x2, y2, color, width=2):
    mid_x = (x1 + x2) // 2
    pts = []
    for step in range(30):
        t = step / 29.0
        # Cubic Bezier: (x1, y1), (mid_x, y1), (mid_x, y2), (x2, y2)
        cx = (1 - t)**3 * x1 + 3 * (1 - t)**2 * t * mid_x + 3 * (1 - t) * t**2 * mid_x + t**3 * x2
        cy = (1 - t)**3 * y1 + 3 * (1 - t)**2 * t * y1 + 3 * (1 - t) * t**2 * y2 + t**3 * y2
        pts.append((cx, cy))
    draw.line(pts, fill=color, width=width)

# Cable 1: sub-bass -> feed (mod)
c1_start = (nx + nw, n1_y + 36)
c1_end = (nx, n2_y + 36)
# Cable loops outside right and comes into left
pts_c1 = [
    c1_start,
    (nx + nw + 12, n1_y + 36),
    (nx + nw + 12, n1_y + 88),
    (nx - 12, n1_y + 88),
    (nx - 12, n2_y + 36),
    c1_end
]
draw.line(pts_c1, fill=(255, 107, 53, 200), width=2)

# Cable 2: hi-mid -> kill (mod)
c2_start = (nx + nw, n1_y + 54)
c2_end = (nx, n2_y + 54)
pts_c2 = [
    c2_start,
    (nx + nw + 18, n1_y + 54),
    (nx + nw + 18, n1_y + 92),
    (nx - 18, n1_y + 92),
    (nx - 18, n2_y + 54),
    c2_end
]
draw.line(pts_c2, fill=(255, 170, 53, 200), width=2)

# Cable 3: Reaction Diffusion picture out -> Color Ramp input
c3_start = (nx + nw, n2_y + 36)
c3_end = (nx, n3_y + 36)
pts_c3 = [
    c3_start,
    (nx + nw + 14, n2_y + 36),
    (nx + nw + 14, n2_y + 110),
    (nx - 14, n2_y + 110),
    (nx - 14, n3_y + 36),
    c3_end
]
draw.line(pts_c3, fill=(129, 140, 248, 220), width=2)

# Cable 4: Color Ramp picture out -> Output picture in
c4_start = (nx + nw, n3_y + 36)
c4_end = (nx, n4_y + 36)
pts_c4 = [
    c4_start,
    (nx + nw + 14, n3_y + 36),
    (nx + nw + 14, n3_y + 76),
    (nx - 14, n3_y + 76),
    (nx - 14, n4_y + 36),
    c4_end
]
draw.line(pts_c4, fill=(74, 222, 128, 220), width=2)

# Save output
out_dir = os.path.join(REPO_ROOT, "website", "assets")
os.makedirs(out_dir, exist_ok=True)
out_path = os.path.join(out_dir, "blog_reaction_diffusion_simulation_in_real_time_glsl.png")
img.save(out_path, "PNG")
print(f"Generated hero diagram: {out_path} ({W}x{H})")
