#!/usr/bin/env python3
"""Generate the technical hero diagram for the real-time fluid simulation in GLSL post.
Outputs a crisp 1200x675 brand-compliant image to website/assets/blog_real_time_fluid_simulation_in_glsl.png.
"""
import math
import os
from PIL import Image, ImageDraw, ImageFont

W, H = 1200, 675
img = Image.new("RGBA", (W, H), (13, 16, 32, 255))  # Midnight deep #0D1020
draw = ImageDraw.Draw(img)

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FONT_DIR = os.path.join(REPO_ROOT, "art", "brand", "fonts")
GEIST_SEMIBOLD = os.path.join(FONT_DIR, "Geist-SemiBold.ttf")
GEIST_MEDIUM = os.path.join(FONT_DIR, "Geist-Medium.ttf")
GEIST_MONO = os.path.join(FONT_DIR, "GeistMono-Medium.ttf")

try:
    f_title = ImageFont.truetype(GEIST_SEMIBOLD, 26)
    f_sub = ImageFont.truetype(GEIST_MEDIUM, 13)
    f_badge = ImageFont.truetype(GEIST_MONO, 11)
    f_card_title = ImageFont.truetype(GEIST_SEMIBOLD, 14)
    f_body = ImageFont.truetype(GEIST_MEDIUM, 11)
    f_mono = ImageFont.truetype(GEIST_MONO, 11)
    f_mono_sm = ImageFont.truetype(GEIST_MONO, 9)
    f_formula = ImageFont.truetype(GEIST_MONO, 11)
except Exception:
    f_title = f_sub = f_badge = f_card_title = f_body = f_mono = f_mono_sm = f_formula = ImageFont.load_default()

# 1. Background dot grid
dot_color = (35, 42, 74, 180)
for gx in range(24, W - 24, 28):
    for gy in range(24, H - 24, 28):
        draw.ellipse([gx - 1, gy - 1, gx + 1, gy + 1], fill=dot_color)

# 2. Outer frame
draw.rounded_rectangle([20, 20, W - 20, H - 20], radius=16, outline=(42, 48, 82, 255), width=1)

# 3. Header section
pill_box = [45, 40, 420, 66]
draw.rounded_rectangle(pill_box, radius=6, fill=(32, 38, 63, 255), outline=(52, 59, 94, 255), width=1)
draw.ellipse([58, 50, 64, 56], fill=(255, 107, 53, 255))
draw.text((72, 46), "INFINITE // EULERIAN NAVIER-STOKES FLUIDS", font=f_badge, fill=(167, 177, 255, 255))

draw.rounded_rectangle([W - 245, 40, W - 45, 66], radius=6, fill=(32, 38, 63, 255), outline=(52, 59, 94, 255), width=1)
draw.text((W - 233, 46), "TRACK B: CREATIVE CODING", font=f_badge, fill=(238, 241, 250, 255))

draw.text((45, 82), "Real-Time Fluid Simulation in GLSL: Navier-Stokes & Advection", font=f_title, fill=(238, 241, 250, 255))
draw.text((45, 118), "Stam stable fluids, MAC staggered grids, multigrid Poisson pressure solver, and optical flow force injection", font=f_sub, fill=(163, 170, 203, 255))

draw.line([(45, 144), (W - 45, 144)], fill=(42, 48, 82, 255), width=1)

# Card helper
def draw_card_shell(x1, y1, x2, y2, title, kicker=None):
    draw.rounded_rectangle([x1, y1, x2, y2], radius=12, fill=(21, 25, 48, 255), outline=(42, 48, 82, 255), width=1)
    draw.rounded_rectangle([x1, y1, x2, y1 + 38], radius=12, fill=(32, 38, 63, 255))
    draw.rectangle([x1, y1 + 26, x2, y1 + 38], fill=(32, 38, 63, 255))
    draw.line([(x1, y1 + 38), (x2, y1 + 38)], fill=(52, 59, 94, 255), width=1)
    draw.text((x1 + 16, y1 + 11), title, font=f_card_title, fill=(238, 241, 250, 255))
    if kicker:
        kw = draw.textlength(kicker, font=f_mono_sm) + 12
        draw.rounded_rectangle([x2 - 16 - kw, y1 + 9, x2 - 16, y1 + 29], radius=4, fill=(52, 59, 94, 255))
        draw.text((x2 - 16 - kw + 6, y1 + 14), kicker, font=f_mono_sm, fill=(167, 177, 255, 255))

col1_x1, col1_x2 = 45, 400
col2_x1, col2_x2 = 416, 784
col3_x1, col3_x2 = 800, W - 45
card_y1, card_y2 = 160, H - 42

# --- COLUMN 1: Mathematical Formulation & Staggered Grid ---
draw_card_shell(col1_x1, card_y1, col1_x2, card_y2, "Navier-Stokes Pipeline", "PDE & MAC GRID")

# Equations Box
eq_box = [col1_x1 + 14, card_y1 + 48, col1_x2 - 14, card_y1 + 136]
draw.rounded_rectangle(eq_box, radius=8, fill=(13, 16, 32, 255), outline=(35, 42, 74, 255), width=1)
draw.text((col1_x1 + 22, card_y1 + 54), "INCOMPRESSIBLE NAVIER-STOKES (STAM 1999)", font=f_mono_sm, fill=(136, 144, 177, 255))
draw.text((col1_x1 + 22, card_y1 + 72), "∂u/∂t = -(u·∇)u - (1/ρ)∇p + ν∇²u + f", font=f_formula, fill=(238, 241, 250, 255))
draw.text((col1_x1 + 22, card_y1 + 94), "∇·u = 0  (incompressibility constraint)", font=f_formula, fill=(255, 107, 53, 255))
draw.text((col1_x1 + 22, card_y1 + 114), "∂c/∂t = -(u·∇)c  (passive dye advection)", font=f_formula, fill=(167, 177, 255, 255))

# MAC Staggered Grid Diagram Box
grid_box = [col1_x1 + 14, card_y1 + 146, col1_x2 - 14, card_y1 + 330]
draw.rounded_rectangle(grid_box, radius=8, fill=(13, 16, 32, 255), outline=(35, 42, 74, 255), width=1)
draw.text((col1_x1 + 22, card_y1 + 152), "STAGGERED MAC GRID (FACE VELOCITIES)", font=f_mono_sm, fill=(136, 144, 177, 255))

# Draw 2x2 grid cells
gx0, gy0 = col1_x1 + 55, card_y1 + 175
cw = 110
ch = 64
for row in range(2):
    for col in range(2):
        cx, cy = gx0 + col * cw, gy0 + row * ch
        draw.rectangle([cx, cy, cx + cw, cy + ch], outline=(52, 59, 94, 255), fill=(21, 25, 48, 200), width=1)
        # Pressure / cell centre marker
        draw.ellipse([cx + cw//2 - 4, cy + ch//2 - 4, cx + cw//2 + 4, cy + ch//2 + 4], fill=(167, 177, 255, 255))
        draw.text((cx + cw//2 + 7, cy + ch//2 - 7), f"p({col},{row})", font=f_mono_sm, fill=(167, 177, 255, 255))
        # Left face horizontal velocity u
        draw.polygon([(cx + 2, cy + ch//2 - 4), (cx + 10, cy + ch//2), (cx + 2, cy + ch//2 + 4)], fill=(255, 107, 53, 255))
        draw.line([(cx - 8, cy + ch//2), (cx + 8, cy + ch//2)], fill=(255, 107, 53, 255), width=2)
        draw.text((cx - 18, cy + ch//2 - 14), f"u", font=f_mono_sm, fill=(255, 107, 53, 255))
        # Bottom face vertical velocity v
        draw.polygon([(cx + cw//2 - 4, cy + ch - 2), (cx + cw//2, cy + ch - 10), (cx + cw//2 + 4, cy + ch - 2)], fill=(120, 220, 160, 255))
        draw.line([(cx + cw//2, cy + ch + 8), (cx + cw//2, cy + ch - 8)], fill=(120, 220, 160, 255), width=2)
        draw.text((cx + cw//2 + 6, cy + ch - 4), f"v", font=f_mono_sm, fill=(120, 220, 160, 255))

draw.text((col1_x1 + 22, card_y1 + 310), "Face u,v eliminates odd-even checkerboarding", font=f_mono_sm, fill=(163, 170, 203, 255))

# Text description box
desc_box = [col1_x1 + 14, card_y1 + 340, col1_x2 - 14, card_y2 - 14]
draw.rounded_rectangle(desc_box, radius=8, fill=(13, 16, 32, 255), outline=(35, 42, 74, 255), width=1)
draw.text((col1_x1 + 22, card_y1 + 348), "FOUR SPLIT OPERATORS PER FRAME", font=f_mono_sm, fill=(136, 144, 177, 255))
steps = [
    ("1. Advect", "Semi-Lagrangian backtrack (u·∇)u"),
    ("2. Force", "Optical Flow + stirrer injection"),
    ("3. Vorticity", "Re-inject curl: ε * (N x ω)"),
    ("4. Project", "Multigrid Poisson solve: ∇²p = ∇·u")
]
for idx, (sname, sdesc) in enumerate(steps):
    sy = card_y1 + 368 + idx * 23
    draw.text((col1_x1 + 22, sy), sname, font=f_mono, fill=(255, 107, 53, 255))
    draw.text((col1_x1 + 95, sy), sdesc, font=f_body, fill=(163, 170, 203, 255))


# --- COLUMN 2: Multigrid Pressure Solver & Vorticity ---
draw_card_shell(col2_x1, card_y1, col2_x2, card_y2, "Multigrid V-Cycle", "POISSON SOLVER")

mg_box = [col2_x1 + 14, card_y1 + 48, col2_x2 - 14, card_y1 + 265]
draw.rounded_rectangle(mg_box, radius=8, fill=(13, 16, 32, 255), outline=(35, 42, 74, 255), width=1)
draw.text((col2_x1 + 22, card_y1 + 54), "5-LEVEL GEOMETRIC MULTIGRID V-CYCLE", font=f_mono_sm, fill=(136, 144, 177, 255))

# V-cycle diagram
levels = [
    ("Lv 0 (1920x1080)", 60, 80),
    ("Lv 1 (960x540)", 100, 115),
    ("Lv 2 (480x270)", 140, 150),
    ("Lv 3 (240x135)", 180, 185),
    ("Lv 4 (120x68)", 220, 220),
]
cx_center = (col2_x1 + col2_x2) // 2

# Draw V path
v_pts_down = [
    (col2_x1 + 60, card_y1 + 80),
    (col2_x1 + 100, card_y1 + 115),
    (col2_x1 + 140, card_y1 + 150),
    (col2_x1 + 180, card_y1 + 185),
    (cx_center, card_y1 + 220)
]
v_pts_up = [
    (cx_center, card_y1 + 220),
    (col2_x2 - 180, card_y1 + 185),
    (col2_x2 - 140, card_y1 + 150),
    (col2_x2 - 100, card_y1 + 115),
    (col2_x2 - 60, card_y1 + 80)
]
draw.line(v_pts_down, fill=(255, 107, 53, 255), width=3)
draw.line(v_pts_up, fill=(167, 177, 255, 255), width=3)

# Draw level nodes
for pt in v_pts_down:
    draw.ellipse([pt[0] - 5, pt[1] - 5, pt[0] + 5, pt[1] + 5], fill=(255, 107, 53, 255), outline=(238, 241, 250, 255))
for pt in v_pts_up[1:]:
    draw.ellipse([pt[0] - 5, pt[1] - 5, pt[0] + 5, pt[1] + 5], fill=(167, 177, 255, 255), outline=(238, 241, 250, 255))

draw.text((col2_x1 + 25, card_y1 + 140), "Restrict (↓)", font=f_mono_sm, fill=(255, 107, 53, 255))
draw.text((col2_x2 - 85, card_y1 + 140), "Prolong (↑)", font=f_mono_sm, fill=(167, 177, 255, 255))
draw.text((cx_center - 45, card_y1 + 235), "Coarse solve", font=f_mono_sm, fill=(238, 241, 250, 255))

# Poisson and Vorticity box
vort_box = [col2_x1 + 14, card_y1 + 275, col2_x2 - 14, card_y2 - 14]
draw.rounded_rectangle(vort_box, radius=8, fill=(13, 16, 32, 255), outline=(35, 42, 74, 255), width=1)
draw.text((col2_x1 + 22, card_y1 + 283), "VORTICITY CONFINEMENT DYNAMICS", font=f_mono_sm, fill=(136, 144, 177, 255))
draw.text((col2_x1 + 22, card_y1 + 303), "ω = ∇ × u = (∂v/∂x - ∂u/∂y)", font=f_formula, fill=(238, 241, 250, 255))
draw.text((col2_x1 + 22, card_y1 + 323), "η = ∇|ω| / |∇|ω||  (gradient unit vector)", font=f_formula, fill=(163, 170, 203, 255))
draw.text((col2_x1 + 22, card_y1 + 343), "f_vort = ε * (η × ω) * Δx", font=f_formula, fill=(255, 107, 53, 255))

vort_notes = [
    "• Recovers turbulent eddies lost to bilinear damping",
    "• Incompressible projection: u_clean = u - ∇p",
    "• Multigrid converges in 20 iters vs 120 Jacobi iters",
    "• Preserves zero divergence across solid obstacle walls"
]
for idx, note in enumerate(vort_notes):
    draw.text((col2_x1 + 22, card_y1 + 372 + idx * 19), note, font=f_body, fill=(163, 170, 203, 255))


# --- COLUMN 3: Audiovisual Architecture in Infinite ---
draw_card_shell(col3_x1, card_y1, col3_x2, card_y2, "Audiovisual Patching", "LIVE MODULATION")

graph_box = [col3_x1 + 14, card_y1 + 48, col3_x2 - 14, card_y1 + 275]
draw.rounded_rectangle(graph_box, radius=8, fill=(13, 16, 32, 255), outline=(35, 42, 74, 255), width=1)
draw.text((col3_x1 + 22, card_y1 + 54), "AUDIO-REACTIVE FLUID NODE GRAPH", font=f_mono_sm, fill=(136, 144, 177, 255))

# Draw node boxes
def draw_node_badge(x, y, w, h, name, kind, color):
    draw.rounded_rectangle([x, y, x + w, y + h], radius=6, fill=(21, 25, 48, 255), outline=color, width=1)
    draw.text((x + 8, y + 4), name, font=f_card_title, fill=(238, 241, 250, 255))
    draw.text((x + 8, y + 22), kind, font=f_mono_sm, fill=color)

ny = card_y1 + 75
draw_node_badge(col3_x1 + 24, ny, 140, 42, "Audio In", "Utility (Microphone)", (167, 177, 255, 255))
draw_node_badge(col3_x1 + 185, ny, 140, 42, "Audio Analyze", "8-Band FFT Analysis", (255, 107, 53, 255))

draw.line([(col3_x1 + 164, ny + 21), (col3_x1 + 185, ny + 21)], fill=(167, 177, 255, 255), width=2)

ny2 = card_y1 + 130
draw_node_badge(col3_x1 + 24, ny2, 140, 42, "Video / Camera", "Source (Dye Media)", (120, 220, 160, 255))
draw_node_badge(col3_x1 + 185, ny2, 140, 42, "Optical Flow", "Lucas-Kanade Vectors", (255, 107, 53, 255))

draw.line([(col3_x1 + 164, ny2 + 21), (col3_x1 + 185, ny2 + 21)], fill=(120, 220, 160, 255), width=2)

ny3 = card_y1 + 195
draw_node_badge(col3_x1 + 70, ny3, 210, 60, "Fluid", "Navier-Stokes Simulation", (255, 107, 53, 255))

# Connect to Fluid node
draw.line([(col3_x1 + 255, ny + 42), (col3_x1 + 230, ny3)], fill=(255, 107, 53, 255), width=2) # FFT -> Stirrer
draw.line([(col3_x1 + 94, ny2 + 42), (col3_x1 + 120, ny3)], fill=(120, 220, 160, 255), width=2) # Video -> Image (dye)
draw.line([(col3_x1 + 255, ny2 + 42), (col3_x1 + 200, ny3)], fill=(255, 107, 53, 255), width=2) # Flow -> Force

# Parameter specs
spec_box = [col3_x1 + 14, card_y1 + 285, col3_x2 - 14, card_y2 - 14]
draw.rounded_rectangle(spec_box, radius=8, fill=(13, 16, 32, 255), outline=(35, 42, 74, 255), width=1)
draw.text((col3_x1 + 22, card_y1 + 293), "CRITICAL SIMULATION PARAMETERS", font=f_mono_sm, fill=(136, 144, 177, 255))

params = [
    ("resolution", "0: full, 1: half (1080p -> 540p sim)"),
    ("pressureIters", "20 multigrid iters (V-cycle)"),
    ("vorticity", "0.25 - 0.45 (curls & turbulence)"),
    ("forceGain", "Cabled to bass FFT band (low punch)"),
    ("stirrer", "Spin / Orbit modulated by tempo"),
    ("dyeFade", "0.005 (decay rate for clean trails)")
]
for idx, (pname, pval) in enumerate(params):
    py = card_y1 + 315 + idx * 22
    draw.text((col3_x1 + 22, py), pname, font=f_mono, fill=(167, 177, 255, 255))
    draw.text((col3_x1 + 115, py), pval, font=f_body, fill=(163, 170, 203, 255))

out_path = os.path.join(REPO_ROOT, "website", "assets", "blog_real_time_fluid_simulation_in_glsl.png")
os.makedirs(os.path.dirname(out_path), exist_ok=True)
img.save(out_path, "PNG", optimize=True)
print(f"Saved {W}x{H} hero diagram to {out_path}")
