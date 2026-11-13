#!/usr/bin/env python3
"""Generate the technical hero diagram for the real-time Delaunay triangulation and Voronoi geometry blog post.
Outputs a crisp 1200x675 brand-compliant image to website/assets/blog_delaunay-triangulation-and-voronoi-geometry-in-real-time.png.
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

# Palette tokens
MIDNIGHT = (13, 16, 32, 255)
SURF1 = (21, 25, 48, 255)      # Card ground #151930
SURF2 = (26, 32, 58, 255)      # Inner card ground
SURF3 = (42, 48, 82, 255)      # Card borders
LINE = (52, 59, 94, 255)       # Grid / dividers
INK = (238, 241, 250, 255)     # Primary text
INK2 = (163, 170, 203, 255)    # Secondary text
INK3 = (105, 114, 150, 255)    # Muted text
SIGNAL = (167, 177, 255, 255)  # Structural blue/purple
EMBER = (255, 107, 53, 255)    # Warm accent
CYAN = (56, 189, 248, 255)     # Voronoi dual lines
GREEN = (74, 222, 128, 255)    # Pass / verified

# 1. Background dot grid
dot_color = (35, 42, 74, 180)
for gx in range(24, W - 24, 28):
    for gy in range(24, H - 24, 28):
        draw.ellipse([gx - 1, gy - 1, gx + 1, gy + 1], fill=dot_color)

# 2. Outer decorative frame
draw.rounded_rectangle([20, 20, W - 20, H - 20], radius=16, outline=SURF3, width=1)

# 3. Header section
draw.rounded_rectangle([45, 40, 380, 66], radius=6, fill=SURF2, outline=LINE, width=1)
draw.text((58, 46), "INFINITE  //  COMPUTATIONAL GEOMETRY", font=f_badge, fill=SIGNAL)

draw.rounded_rectangle([W - 355, 40, W - 45, 66], radius=6, fill=SURF2, outline=LINE, width=1)
draw.text((W - 340, 46), "TRACK B: DELAUNAY TRIANGULATION & VORONOI", font=f_badge, fill=INK)

draw.text((45, 80), "Delaunay Triangulation & Voronoi in Real Time", font=f_title, fill=INK)
draw.text((45, 116), "Empty circumcircle criterion, Bowyer-Watson cavity insertion, dual cell insetting, and 3D heightfields", font=f_sub, fill=INK2)

draw.line([(45, 142), (W - 45, 142)], fill=SURF3, width=1)

# 4. Left Pane: Bowyer-Watson Delaunay Triangulation & Circumcircle Test
lx1, ly1, lx2, ly2 = 45, 158, 585, H - 35
draw.rounded_rectangle([lx1, ly1, lx2, ly2], radius=12, fill=SURF1, outline=SURF3, width=1)
draw.text((lx1 + 20, ly1 + 14), "BOWYER-WATSON DELAUNAY & EMPTY CIRCUMCIRCLE", font=f_card_hdr, fill=EMBER)

# Schematic canvas inside left card
sx1, sy1, sx2, sy2 = lx1 + 16, ly1 + 36, lx2 - 16, ly1 + 315
draw.rounded_rectangle([sx1, sy1, sx2, sy2], radius=8, fill=(10, 13, 26, 255), outline=(35, 42, 74, 255), width=1)

# Define a fixed, attractive set of 2D sites for illustration
sites = [
    (sx1 + 65, sy1 + 55),
    (sx1 + 175, sy1 + 45),
    (sx1 + 310, sy1 + 60),
    (sx1 + 440, sy1 + 75),
    (sx1 + 110, sy1 + 130),
    (sx1 + 245, sy1 + 140), # Target new insertion point
    (sx1 + 375, sy1 + 145),
    (sx1 + 75, sy1 + 225),
    (sx1 + 195, sy1 + 235),
    (sx1 + 325, sy1 + 220),
    (sx1 + 455, sy1 + 215),
]

# Triangles among sites (excluding new point at index 5 to show cavity insertion)
triangles = [
    (0, 1, 4),
    (1, 2, 6),
    (2, 3, 6),
    (3, 6, 10),
    (0, 4, 7),
    (4, 7, 8),
    (6, 9, 10),
    (7, 8, 9),
    (8, 9, 10),
]

# Circumcircles for key triangles
def get_circumcircle(p1, p2, p3):
    ax, ay = p1
    bx, by = p2[0] - ax, p2[1] - ay
    cx, cy = p3[0] - ax, p3[1] - ay
    d = 2.0 * (bx * cy - by * cx)
    if abs(d) < 1e-9:
        return ax, ay, 0
    b2 = bx * bx + by * by
    c2 = cx * cx + cy * cy
    ux = (cy * b2 - by * c2) / d
    uy = (bx * c2 - cx * b2) / d
    return ax + ux, ay + uy, math.sqrt(ux * ux + uy * uy)

# Draw circumcircles in soft translucent lines
circ_triangles = [(0, 1, 4), (1, 2, 6), (6, 9, 10), (4, 7, 8)]
for t in circ_triangles:
    ccx, ccy, cr = get_circumcircle(sites[t[0]], sites[t[1]], sites[t[2]])
    if cr > 0:
        draw.ellipse([ccx - cr, ccy - cr, ccx + cr, ccy + cr], outline=(70, 85, 140, 90), width=1)
        draw.ellipse([ccx - 2, ccy - 2, ccx + 2, ccy + 2], fill=(167, 177, 255, 140))

# Draw existing Delaunay edges
for t in triangles:
    p1, p2, p3 = sites[t[0]], sites[t[1]], sites[t[2]]
    draw.line([p1, p2], fill=(85, 98, 150, 180), width=1)
    draw.line([p2, p3], fill=(85, 98, 150, 180), width=1)
    draw.line([p3, p1], fill=(85, 98, 150, 180), width=1)

# Highlight bad triangle cavity around new point sites[5]
new_p = sites[5]
cavity_edges = [
    (sites[1], sites[2]),
    (sites[2], sites[6]),
    (sites[6], sites[9]),
    (sites[9], sites[8]),
    (sites[8], sites[4]),
    (sites[4], sites[1]),
]

# Draw cavity star polygon
cavity_poly = [sites[1], sites[2], sites[6], sites[9], sites[8], sites[4]]
draw.polygon(cavity_poly, fill=(255, 107, 53, 35))

# Draw cavity boundary edges in Ember
for i in range(len(cavity_poly)):
    p_a = cavity_poly[i]
    p_b = cavity_poly[(i + 1) % len(cavity_poly)]
    draw.line([p_a, p_b], fill=EMBER, width=2)

# Draw new triangle connections to the inserted site
for p_c in cavity_poly:
    draw.line([p_c, new_p], fill=(255, 180, 120, 220), width=1)

# Draw sites as points
for idx, s in enumerate(sites):
    if idx == 5:
        # Inserted site: glowing Ember dot
        draw.ellipse([s[0] - 7, s[1] - 7, s[0] + 7, s[1] + 7], fill=(255, 107, 53, 60))
        draw.ellipse([s[0] - 4, s[1] - 4, s[0] + 4, s[1] + 4], fill=EMBER)
        draw.text((s[0] + 8, s[1] - 14), "Inserted site P(u, v)", font=f_mono, fill=EMBER)
    else:
        draw.ellipse([s[0] - 4, s[1] - 4, s[0] + 4, s[1] + 4], fill=SIGNAL)

# Annotations in left card
draw.text((sx1 + 14, sy1 + 10), "CIRCUMCIRCLE CAVITY DELETION", font=f_badge, fill=EMBER)
draw.text((sx1 + 14, sy2 - 24), "Empty circumcircle criterion: no site lies inside circumcircle C(T)", font=f_mono_sm, fill=INK2)

# Lower spec cards in Left Pane
info_y = sy2 + 14
box_w = (lx2 - lx1 - 42) // 2

# Box 1: Bowyer-Watson step
draw.rounded_rectangle([lx1 + 16, info_y, lx1 + 16 + box_w, ly2 - 14], radius=6, fill=SURF2, outline=SURF3, width=1)
draw.text((lx1 + 26, info_y + 8), "ALGORITHMIC STEP", font=f_badge, fill=SIGNAL)
b1_lines = [
    "1. Append super-triangle vertices",
    "2. Grid-snake spatial sorting",
    "3. Identify bad circumcircles",
    "4. Retriangulate cavity boundary",
    "5. Remove super-triangle indices"
]
for li, line_txt in enumerate(b1_lines):
    draw.text((lx1 + 26, info_y + 26 + li * 15), line_txt, font=f_mono_sm, fill=INK)

# Box 2: Determinant Formula
draw.rounded_rectangle([lx1 + 26 + box_w, info_y, lx2 - 16, ly2 - 14], radius=6, fill=SURF2, outline=SURF3, width=1)
draw.text((lx1 + 36 + box_w, info_y + 8), "CIRCUMCENTRE DET FORMULA", font=f_badge, fill=CYAN)
f_lines = [
    "D = 2.0 * (bx * cy - by * cx)",
    "ux = (cy * b^2 - by * c^2) / D",
    "uy = (bx * c^2 - cx * b^2) / D",
    "r^2 = ux^2 + uy^2",
    "dx^2 + dy^2 < r^2 * (1 - 1e-12)"
]
for li, line_txt in enumerate(f_lines):
    draw.text((lx1 + 36 + box_w, info_y + 26 + li * 15), line_txt, font=f_formula, fill=(180, 220, 255, 255))

# 5. Right Pane: Voronoi Dual Cells & 3D Audio-Reactive Mesh (Width ~570)
rx1, ry1, rx2, ry2 = 615, 158, W - 45, H - 35
draw.rounded_rectangle([rx1, ry1, rx2, ry2], radius=12, fill=SURF1, outline=SURF3, width=1)
draw.text((rx1 + 20, ry1 + 14), "DUAL VORONOI CELLS & 3D HEIGHTFIELD LIFTING", font=f_card_hdr, fill=CYAN)

# Right top canvas: Voronoi cell insetting visualization
vx1, vy1, vx2, vy2 = rx1 + 16, ry1 + 36, rx2 - 16, ry1 + 240
draw.rounded_rectangle([vx1, vy1, vx2, vy2], radius=8, fill=(10, 13, 26, 255), outline=(35, 42, 74, 255), width=1)

draw.text((vx1 + 14, vy1 + 10), "VORONOI CELLS (INSET = 0.22)", font=f_badge, fill=CYAN)
draw.text((vx2 - 190, vy1 + 10), "POLYGONAL INSET CHANNELS", font=f_badge, fill=GREEN)

# Illustrate polygonal Voronoi cells with insets
v_cells = [
    # (site_x, site_y, [vertices])
    (vx1 + 90, vy1 + 90, [
        (vx1 + 40, vy1 + 45), (vx1 + 145, vy1 + 40), (vx1 + 160, vy1 + 115),
        (vx1 + 110, vy1 + 155), (vx1 + 45, vy1 + 130)
    ]),
    (vx1 + 230, vy1 + 80, [
        (vx1 + 145, vy1 + 40), (vx1 + 285, vy1 + 35), (vx1 + 320, vy1 + 105),
        (vx1 + 270, vy1 + 150), (vx1 + 160, vy1 + 115)
    ]),
    (vx1 + 380, vy1 + 95, [
        (vx1 + 285, vy1 + 35), (vx1 + 450, vy1 + 45), (vx1 + 475, vy1 + 125),
        (vx1 + 410, vy1 + 165), (vx1 + 320, vy1 + 105)
    ]),
    (vx1 + 150, vy1 + 175, [
        (vx1 + 110, vy1 + 155), (vx1 + 160, vy1 + 115), (vx1 + 270, vy1 + 150),
        (vx1 + 260, vy1 + 220), (vx1 + 95, vy1 + 215)
    ]),
    (vx1 + 330, vy1 + 180, [
        (vx1 + 270, vy1 + 150), (vx1 + 320, vy1 + 105), (vx1 + 410, vy1 + 165),
        (vx1 + 420, vy1 + 225), (vx1 + 260, vy1 + 220)
    ])
]

# Draw cells with inset pull
inset_factor = 0.22
for sx, sy, verts in v_cells:
    # Outer cell ghost border (cyan faint)
    draw.polygon(verts, outline=(56, 189, 248, 60), fill=(20, 35, 65, 40))
    # Inset cell vertices
    inset_verts = []
    for vx_i, vy_i in verts:
        ix = sx + (vx_i - sx) * (1.0 - inset_factor)
        iy = sy + (vy_i - sy) * (1.0 - inset_factor)
        inset_verts.append((ix, iy))
    # Inset cell polygon fill and stroke
    draw.polygon(inset_verts, fill=(35, 65, 115, 120), outline=CYAN, width=1)
    # Connecting spokes from centre to inset vertices
    for ix, iy in inset_verts:
        draw.line([(sx, sy), (ix, iy)], fill=(56, 189, 248, 80), width=1)
    # Site centre dot
    draw.ellipse([sx - 3, sy - 3, sx + 3, sy + 3], fill=EMBER)

draw.text((vx1 + 14, vy2 - 22), "Circumcentre radial sorting // Convex hull boundary pruning // Spoke fan triangulation", font=f_mono_sm, fill=INK2)

# Right lower section: Node Graph Flow Card (Width ~538)
nx1, ny1, nx2, ny2 = rx1 + 16, ry1 + 254, rx2 - 16, ly2 - 14
draw.rounded_rectangle([nx1, ny1, nx2, ny2], radius=8, fill=SURF2, outline=SURF3, width=1)
draw.text((nx1 + 16, ny1 + 10), "INFINITE NODE PIPELINE: AUDIO-REACTIVE 3D MESH", font=f_badge, fill=GREEN)

# Draw native node badges in the chain
node_chain = [
    ("Audio Analyze", "AUDIO", (255, 107, 53, 255)),
    ("Particle System", "SIMULATION", (167, 177, 255, 255)),
    ("Audio Displacement", "GEOMETRY OP", (251, 191, 36, 255)),
    ("Delaunay Mesh", "GEOMETRY 3D", (56, 189, 248, 255)),
    ("Render 3D", "RENDER", (74, 222, 128, 255))
]

nb_w = 96
nb_h = 58
nb_spacing = 10
nb_start_x = nx1 + 12
nb_y = ny1 + 34

for ni, (n_name, n_cat, n_color) in enumerate(node_chain):
    nb_x = nb_start_x + ni * (nb_w + nb_spacing)
    # Node card
    draw.rounded_rectangle([nb_x, nb_y, nb_x + nb_w, nb_y + nb_h], radius=6, fill=SURF1, outline=LINE, width=1)
    # Header bar
    draw.rounded_rectangle([nb_x, nb_y, nb_x + nb_w, nb_y + 16], radius=4, fill=(35, 42, 74, 255))
    draw.text((nb_x + 6, nb_y + 3), n_cat[:11], font=f_mono_sm, fill=n_color)
    # Node name
    draw.text((nb_x + 6, nb_y + 22), n_name.split()[0], font=f_mono_sm, fill=INK)
    if len(n_name.split()) > 1:
        draw.text((nb_x + 6, nb_y + 36), " ".join(n_name.split()[1:]), font=f_mono_sm, fill=INK2)
    
    # Connecting wire to next node
    if ni < len(node_chain) - 1:
        wire_x1 = nb_x + nb_w
        wire_x2 = wire_x1 + nb_spacing
        wire_y = nb_y + nb_h // 2
        draw.line([(wire_x1, wire_y), (wire_x2, wire_y)], fill=n_color, width=2)
        # Arrowhead or connector dot
        draw.ellipse([wire_x2 - 3, wire_y - 2, wire_x2 + 1, wire_y + 2], fill=n_color)

# Lower telemetry banner in node pipeline
tb_y = ny1 + 104
draw.line([(nx1 + 12, tb_y), (nx2 - 12, tb_y)], fill=LINE, width=1)
draw.text((nx1 + 14, tb_y + 12), "60 FPS Cook Time: 1.4 ms", font=f_mono_sm, fill=GREEN)
draw.text((nx1 + 175, tb_y + 12), "Plane: XY / XZ / YZ", font=f_mono_sm, fill=SIGNAL)
draw.text((nx1 + 330, tb_y + 12), "Max Points: 2,000", font=f_mono_sm, fill=EMBER)
draw.text((nx1 + 450, tb_y + 12), "Alive Only: True", font=f_mono_sm, fill=INK2)

# Save image
out_png = os.path.join(REPO_ROOT, "website", "assets", "blog_delaunay-triangulation-and-voronoi-geometry-in-real-time.png")
out_webp = os.path.join(REPO_ROOT, "website", "assets", "blog_delaunay-triangulation-and-voronoi-geometry-in-real-time-720.webp")
os.makedirs(os.path.dirname(out_png), exist_ok=True)
img.save(out_png, "PNG", optimize=True)
print(f"Saved hero diagram: {out_png} ({os.path.getsize(out_png)} bytes)")

# WebP version at 720px width
w720 = 720
h720 = round(H * w720 / W)
img_720 = img.convert("RGB").resize((w720, h720), Image.Resampling.LANCZOS)
img_720.save(out_webp, "WEBP", quality=85, method=6)
print(f"Saved webp thumbnail: {out_webp} ({os.path.getsize(out_webp)} bytes)")
