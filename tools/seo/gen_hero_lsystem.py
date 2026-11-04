#!/usr/bin/env python3
"""Generate the technical hero diagram for the L-system procedural 3D branching post.
Outputs a crisp 1200x675 brand-compliant image to website/assets/blog_l_system_procedural_3d_branching_fractals.png.
"""
from __future__ import annotations
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
# Top pill badge: INFINITE // PROCEDURAL 3D & FRACTAL MORPHOLOGY
pill_box = [45, 40, 440, 66]
draw.rounded_rectangle(pill_box, radius=6, fill=(32, 38, 63, 255), outline=(52, 59, 94, 255), width=1)
draw.text((58, 46), "INFINITE  //  PROCEDURAL 3D & FRACTAL MORPHOLOGY", font=f_badge, fill=(167, 177, 255, 255))

# Right pill: TRACK B: CREATIVE CODING
draw.rounded_rectangle([W - 245, 40, W - 45, 66], radius=6, fill=(32, 38, 63, 255), outline=(52, 59, 94, 255), width=1)
draw.text((W - 233, 46), "TRACK B: CREATIVE CODING", font=f_badge, fill=(238, 241, 250, 255))

# Main title and subtitle
draw.text((45, 82), "L-System Procedural 3D Branching and Fractals", font=f_title, fill=(238, 241, 250, 255))
draw.text((45, 118), "Lindenmayer grammars, 3D turtle state matrices, branch tube extrusion, and audio-reactive kinetics", font=f_sub, fill=(163, 170, 203, 255))

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

# --- COLUMN 1: Lindenmayer Grammar & Turtle Triad ---
draw_card_shell(col1_x1, card_y1, col1_x2, card_y2, "Lindenmayer Grammar", "REWRITE & TURTLE")

# 1. Grammar box
gm_box = [col1_x1 + 14, card_y1 + 48, col1_x2 - 14, card_y1 + 140]
draw.rounded_rectangle(gm_box, radius=8, fill=(13, 16, 32, 255), outline=(35, 42, 74, 255), width=1)
draw.text((col1_x1 + 22, card_y1 + 54), "DETERMINISTIC CONTEXT-FREE GRAMMAR", font=f_mono_sm, fill=(136, 144, 177, 255))
draw.text((col1_x1 + 22, card_y1 + 72), "Axiom:  X", font=f_formula, fill=(255, 107, 53, 255))
draw.text((col1_x1 + 22, card_y1 + 92), "Rule 1: X = F+[[X]-X]-F[-FX]+X", font=f_formula, fill=(238, 241, 250, 255))
draw.text((col1_x1 + 22, card_y1 + 114), "Rule 2: F = FF   (Growth step)", font=f_formula, fill=(0, 245, 212, 255))

# 2. 3D Turtle Orthonormal Frame
tf_y1 = card_y1 + 150
tf_y2 = tf_y1 + 160
draw.rounded_rectangle([col1_x1 + 14, tf_y1, col1_x2 - 14, tf_y2], radius=8, fill=(13, 16, 32, 255), outline=(35, 42, 74, 255), width=1)
draw.text((col1_x1 + 22, tf_y1 + 8), "3D TURTLE ORTHONORMAL BASIS TRIAD", font=f_mono_sm, fill=(136, 144, 177, 255))

turtle_ops = [
    ("Heading H (0, 1, 0)", "F / f : Step forward", (255, 107, 53, 255)),
    ("Left L   (-1, 0, 0)", "+ / - : Yaw (H, L plane)", (0, 245, 212, 255)),
    ("Up U     (0, 0, 1)", "& / ^ : Pitch (H, U plane)", (167, 177, 255, 255)),
    ("Roll Rotations", "\\ / / : Roll (L, U plane)", (238, 241, 250, 255)),
    ("Reverse Vector", "|     : Reverse 180 deg", (251, 191, 36, 255)),
]
for idx, (basis, op, col) in enumerate(turtle_ops):
    oy = tf_y1 + 28 + idx * 25
    draw.ellipse([col1_x1 + 22, oy + 4, col1_x1 + 28, oy + 10], fill=col)
    draw.text((col1_x1 + 34, oy), basis, font=f_mono_sm, fill=(238, 241, 250, 255))
    draw.text((col1_x2 - 180, oy), op, font=f_mono_sm, fill=(163, 170, 203, 255))

# 3. Stack semantics
st_y1 = tf_y2 + 10
st_y2 = card_y2 - 14
draw.rounded_rectangle([col1_x1 + 14, st_y1, col1_x2 - 14, st_y2], radius=8, fill=(13, 16, 32, 255), outline=(35, 42, 74, 255), width=1)
draw.text((col1_x1 + 22, st_y1 + 8), "PUSHDOWN STACK & TAPER MULTIPLIERS", font=f_mono_sm, fill=(136, 144, 177, 255))
draw.text((col1_x1 + 22, st_y1 + 28), "[  Push: save state; rad*=0.80, step*=0.75", font=f_mono_sm, fill=(167, 177, 255, 255))
draw.text((col1_x1 + 22, st_y1 + 48), "]  Pop:  restore state; return to fork point", font=f_mono_sm, fill=(74, 222, 128, 255))
draw.text((col1_x1 + 22, st_y1 + 68), "Limits: kMaxSymbols=200k, kMaxStrands=6k", font=f_mono_sm, fill=(136, 144, 177, 255))


# --- COLUMN 2: Procedural 3D Canopy Simulation ---
draw_card_shell(col2_x1, card_y1, col2_x2, card_y2, "Procedural Canopy", "TREE MORPHOLOGY")

prev_box_x = col2_x1 + 14
prev_box_y = card_y1 + 48
disp_w = col2_x2 - col2_x1 - 28
disp_h = 300

# Inner canvas for tree
tree_img = Image.new("RGBA", (disp_w, disp_h), (13, 16, 32, 255))
tree_draw = ImageDraw.Draw(tree_img)

# Recursive tree generator
def draw_tree(x0, y0, length, angle_deg, depth, max_depth):
    if depth > max_depth or length < 2.0:
        return
    rad = math.radians(angle_deg)
    x1 = x0 + length * math.sin(rad)
    y1 = y0 - length * math.cos(rad)
    
    # Color interpolation: trunk (Ember #FF6B35) -> mid (Iris #A7B1FF) -> outer (Cyan #00F5D4)
    t = depth / max_depth
    if t < 0.4:
        # Ember to Iris
        f = t / 0.4
        c = (
            int(255 * (1 - f) + 167 * f),
            int(107 * (1 - f) + 177 * f),
            int(53 * (1 - f) + 255 * f),
            255
        )
    else:
        # Iris to Cyan
        f = (t - 0.4) / 0.6
        c = (
            int(167 * (1 - f) + 0 * f),
            int(177 * (1 - f) + 245 * f),
            int(255 * (1 - f) + 212 * f),
            255
        )
        
    line_w = max(1, int(6.5 * (1.0 - t * 0.85)))
    tree_draw.line([(x0, y0), (x1, y1)], fill=c, width=line_w)
    
    # Branching
    sub_len = length * 0.74
    draw_tree(x1, y1, sub_len, angle_deg - 26.0, depth + 1, max_depth)
    draw_tree(x1, y1, sub_len, angle_deg + 22.0, depth + 1, max_depth)
    if depth >= 2:
        draw_tree(x1, y1, sub_len * 0.82, angle_deg + 3.0, depth + 1, max_depth)

# Generate tree centered in preview
tree_base_x = disp_w // 2
tree_base_y = disp_h - 15
draw_tree(tree_base_x, tree_base_y, 76.0, 0.0, 0, 5)

# Paste tree into main image
img.paste(tree_img, (prev_box_x, prev_box_y))
draw.rounded_rectangle([prev_box_x, prev_box_y, prev_box_x + disp_w, prev_box_y + disp_h], radius=8, outline=(42, 48, 82, 255), width=1)

# Overlay info badges inside preview
draw.rounded_rectangle([prev_box_x + 10, prev_box_y + 10, prev_box_x + 185, prev_box_y + 32], radius=4, fill=(13, 16, 32, 220), outline=(52, 59, 94, 255))
draw.text((prev_box_x + 16, prev_box_y + 14), "CANOPY ANGLE: 25.0° (AUDIO MOD)", font=f_mono_sm, fill=(0, 245, 212, 255))

draw.rounded_rectangle([prev_box_x + disp_w - 96, prev_box_y + 10, prev_box_x + disp_w - 10, prev_box_y + 32], radius=4, fill=(13, 16, 32, 220), outline=(52, 59, 94, 255))
draw.text((prev_box_x + disp_w - 88, prev_box_y + 14), "5 GENERATIONS", font=f_mono_sm, fill=(255, 107, 53, 255))

# Below preview: mesh extrusion indicators
inf_y = prev_box_y + disp_h + 12
draw.rounded_rectangle([prev_box_x, inf_y, prev_box_x + disp_w, card_y2 - 14], radius=8, fill=(13, 16, 32, 255), outline=(35, 42, 74, 255), width=1)
draw.text((prev_box_x + 12, inf_y + 8), "WATERTIGHT MESH SYNTHESIS", font=f_mono_sm, fill=(136, 144, 177, 255))
draw.text((prev_box_x + 12, inf_y + 26), "Mesh: MeshOps::TubeAlong (6 sides, taper=0.30)", font=f_mono_sm, fill=(238, 241, 250, 255))
draw.text((prev_box_x + 12, inf_y + 44), "Topology: 1,842 segments, 14,736 triangles", font=f_mono_sm, fill=(167, 177, 255, 255))
draw.text((prev_box_x + 12, inf_y + 62), "Curve Output: Continuous polyline for Path camera", font=f_mono_sm, fill=(74, 222, 128, 255))


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
draw_node_badge(nx, n1_y, nw, n1_h, "Audio Analyze", "DSP", (255, 107, 53, 255), ["audio in"], ["low (bass)", "mid", "onset"])

# 2. Math Node
n2_y = n1_y + 92
n2_h = 66
draw_node_badge(nx, n2_y, nw, n2_h, "Math / Range Map", "UTILITY", (167, 177, 255, 255), ["signal in"], ["angle [18°-38°]"])

# 3. L-System Node
n3_y = n2_y + 82
n3_h = 100
draw_node_badge(nx, n3_y, nw, n3_h, "L-System", "3D GEOMETRY", (0, 245, 212, 255), ["angle (mod)", "length (mod)"], ["mesh out", "curve out"])

# 4. Material & Render 3D Node
n4_y = n3_y + 116
n4_h = 76
draw_node_badge(nx, n4_y, nw, n4_h, "Material & Render 3D", "RENDER", (74, 222, 128, 255), ["mesh in", "light / cam"], ["display / GL"])

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
    for i in range(len(pts) - 1):
        draw.line([pts[i], pts[i + 1]], fill=color, width=width)

# Cable 1: Audio Analyze (low) -> Math (signal in)
draw_curved_cable(nx + nw, n1_y + 36, nx, n2_y + 36, (255, 107, 53, 255), width=2)

# Cable 2: Math (angle) -> L-System (angle mod)
draw_curved_cable(nx + nw, n2_y + 36, nx, n3_y + 36, (167, 177, 255, 255), width=2)

# Cable 3: L-System (mesh out) -> Material (mesh in)
draw_curved_cable(nx + nw, n3_y + 36, nx, n4_y + 36, (0, 245, 212, 255), width=2)

out_path = os.path.join(REPO_ROOT, "website", "assets", "blog_l_system_procedural_3d_branching_fractals.png")
os.makedirs(os.path.dirname(out_path), exist_ok=True)
img.save(out_path, "PNG")
print(f"Generated hero diagram: {out_path} ({W}x{H})")
