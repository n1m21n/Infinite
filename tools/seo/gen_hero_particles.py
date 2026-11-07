#!/usr/bin/env python3
"""Generate the technical hero diagram for the real-time audio-reactive 3D particle systems blog post.
Outputs a crisp 1200x675 brand-compliant image to website/assets/blog_audio-reactive-particle-systems-in-real-time.png.
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
    f_formula = ImageFont.truetype(GEIST_MONO, 11)
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
# Top pill badge: INFINITE // 3D GRAPHICS & SIMULATION
draw.rounded_rectangle([45, 40, 345, 66], radius=6, fill=(32, 38, 63, 255), outline=(52, 59, 94, 255), width=1)
draw.text((58, 46), "INFINITE  //  3D GRAPHICS & SIMULATION", font=f_badge, fill=(167, 177, 255, 255))

# Right pill: TRACK B: REAL-TIME CREATIVE CODING
draw.rounded_rectangle([W - 310, 40, W - 45, 66], radius=6, fill=(32, 38, 63, 255), outline=(52, 59, 94, 255), width=1)
draw.text((W - 296, 46), "TRACK B: REAL-TIME CREATIVE CODING", font=f_badge, fill=(238, 241, 250, 255))

# Main title and subtitle
draw.text((45, 80), "Audio-Reactive 3D Particle Systems in Real Time", font=f_title, fill=(238, 241, 250, 255))
draw.text((45, 116), "Euler numerical integration, 3D curl turbulence force fields, and spectral transient modulators", font=f_sub, fill=(163, 170, 203, 255))

# Horizontal rule below header
draw.line([(45, 142), (W - 45, 142)], fill=(42, 48, 82, 255), width=1)

# 4. Left Pane: Numerical Physics & Force Field Simulation (Width ~530)
lx1, ly1, lx2, ly2 = 45, 158, 565, H - 35
draw.rounded_rectangle([lx1, ly1, lx2, ly2], radius=12, fill=(21, 25, 48, 255), outline=(42, 48, 82, 255), width=1)
draw.text((lx1 + 20, ly1 + 14), "120 HZ NUMERICAL INTEGRATION & TURBULENCE", font=f_card_hdr, fill=(255, 107, 53, 255))

# Simulation View Box
sx1, sy1, sx2, sy2 = lx1 + 16, ly1 + 36, lx2 - 16, ly1 + 330
draw.rounded_rectangle([sx1, sy1, sx2, sy2], radius=8, fill=(10, 13, 26, 255), outline=(35, 42, 74, 255), width=1)

# Grid lines inside simulation box
for gx in range(sx1 + 25, sx2, 45):
    draw.line([(gx, sy1), (gx, sy2)], fill=(20, 26, 50, 120), width=1)
for gy in range(sy1 + 25, sy2, 45):
    draw.line([(sx1, gy), (sx2, gy)], fill=(20, 26, 50, 120), width=1)

# Generate pseudo-particles radiating outwards with curl turbulence
center_x = (sx1 + sx2) / 2.0
center_y = sy2 - 45.0  # Emitter origin near bottom centre

random.seed(42)
num_particles = 320

# Draw subtle velocity vectors and particles
for i in range(num_particles):
    angle = random.uniform(-math.pi * 0.85, -math.pi * 0.15)
    speed = random.uniform(25.0, 220.0)
    life = random.uniform(0.1, 1.0)
    
    # Base velocity
    vx = math.cos(angle) * speed
    vy = math.sin(angle) * speed
    
    # Integrate position across several steps with turbulence
    px = center_x
    py = center_y
    trail = [(px, py)]
    steps = int(life * 18)
    dt = 0.05
    
    for s in range(steps):
        # 3D turbulence approximation (curl-like vortex)
        turb_x = math.sin(py * 0.035 + px * 0.02) * 60.0
        turb_y = math.cos(px * 0.035) * 40.0 - 25.0  # upwards buoyancy / gravity
        
        px += (vx + turb_x) * dt
        py += (vy + turb_y) * dt
        trail.append((px, py))
    
    # Clip and draw particle trail
    valid_trail = [pt for pt in trail if sx1 + 4 <= pt[0] <= sx2 - 4 and sy1 + 4 <= pt[1] <= sy2 - 4]
    if len(valid_trail) >= 2:
        # Colour gradient based on life / velocity
        # High speed: ember (#FF6B35), tail: signal (#A7B1FF), fading out
        if random.random() < 0.25:
            trail_col = (255, 107, 53, int(160 * life))
            head_col = (255, 180, 100, 240)
            sz = 2.0
        elif random.random() < 0.6:
            trail_col = (167, 177, 255, int(140 * life))
            head_col = (210, 220, 255, 230)
            sz = 1.5
        else:
            trail_col = (74, 84, 214, int(110 * life))
            head_col = (130, 145, 255, 200)
            sz = 1.2
            
        draw.line(valid_trail, fill=trail_col, width=1)
        hx, hy = valid_trail[-1]
        draw.ellipse([hx - sz, hy - sz, hx + sz, hy + sz], fill=head_col)

# Emitter origin marker
draw.ellipse([center_x - 6, center_y - 6, center_x + 6, center_y + 6], fill=(255, 107, 53, 200), outline=(255, 255, 255, 240), width=1)
draw.text((center_x + 12, center_y - 8), "EMITTER ORIGIN", font=f_mono_sm, fill=(255, 107, 53, 255))

# Equations below simulation box
eq_y = ly1 + 344
draw.text((lx1 + 20, eq_y), "VELOCITY UPDATE:   v(t+dt) = (v(t) + (g + F_turb)*dt) * (1 - drag*dt)", font=f_formula, fill=(238, 241, 250, 255))
draw.text((lx1 + 20, eq_y + 20), "POSITION UPDATE:   p(t+dt) = p(t) + v(t+dt)*dt", font=f_formula, fill=(163, 170, 203, 255))
draw.text((lx1 + 20, eq_y + 40), "CURL FORCE FIELD:  F_turb  = grad x Psi(p * turb_scale, t) * amplitude", font=f_formula, fill=(167, 177, 255, 255))
draw.text((lx1 + 20, eq_y + 60), "TRANSIENT TRIGGER: rate / sec = base_rate + kick_envelope * burst_scale", font=f_formula, fill=(255, 107, 53, 255))
draw.text((lx1 + 20, eq_y + 82), "FIXED TIMESTEP:    kFixedStep = 1.0 / 120.0s  (deterministic leapfrog)", font=f_mono_sm, fill=(136, 144, 177, 255))

# 5. Right Pane: Modular Patch Architecture & Audiovisual Flow (Width ~570)
rx1, ry1, rx2, ry2 = 585, 158, W - 45, H - 35
draw.rounded_rectangle([rx1, ry1, rx2, ry2], radius=12, fill=(21, 25, 48, 255), outline=(42, 48, 82, 255), width=1)
draw.text((rx1 + 20, ry1 + 14), "MODULAR AUDIOVISUAL EXECUTION GRAPH", font=f_card_hdr, fill=(167, 177, 255, 255))

# Helper to draw a sleek node card
def draw_node_card(x, y, w, h, title, cat, cat_col, params):
    draw.rounded_rectangle([x, y, x + w, y + h], radius=6, fill=(15, 18, 36, 255), outline=(42, 48, 82, 255), width=1)
    # Header bar
    draw.rounded_rectangle([x, y, x + w, y + 24], radius=6, fill=(26, 32, 58, 255))
    draw.text((x + 8, y + 5), title, font=f_node_title, fill=(238, 241, 250, 255))
    draw.text((x + w - 48, y + 6), cat, font=f_node_cat, fill=cat_col)
    
    # Body params
    py = y + 30
    for p_name, p_val, is_mod in params:
        draw.text((x + 8, py), p_name, font=f_mono_sm, fill=(163, 170, 203, 255))
        draw.text((x + w - 75, py), p_val, font=f_mono_sm, fill=(255, 107, 53, 255) if is_mod else (167, 177, 255, 255))
        if is_mod:
            # Modulation socket dot
            draw.ellipse([x + w - 85, py + 2, x + w - 79, py + 8], fill=(255, 107, 53, 255))
        py += 17

# Draw 6 node cards representing the pipeline
# Row 1: Audio In & Audio Analyze
draw_node_card(rx1 + 16, ry1 + 42, 170, 105, "Audio In", "AUDIO", (168, 85, 247, 255), [
    ("device", "System In", False),
    ("channels", "Stereo", False),
    ("sample rate", "48.0 kHz", False),
    ("gain", "0.0 dB", False),
])

draw_node_card(rx1 + 205, ry1 + 42, 180, 105, "Audio Analyze", "AUDIO", (168, 85, 247, 255), [
    ("source", "Audio In", False),
    ("kick envelope", "Out -> Rate", True),
    ("spectral flux", "Out -> Turb", True),
    ("high band", "Out -> Size", True),
])

draw_node_card(rx1 + 405, ry1 + 42, 145, 105, "LFO", "MOD", (255, 107, 53, 255), [
    ("shape", "Sine", False),
    ("frequency", "0.25 Hz", False),
    ("phase", "0.0 deg", False),
    ("out", "-> Gravity Y", True),
])

# Row 2: Particle System & Point Trails
draw_node_card(rx1 + 16, ry1 + 165, 260, 138, "Particle System", "3D", (56, 189, 248, 255), [
    ("max particles", "50,000", False),
    ("rate / sec", "200 + kick*800", True),
    ("turbulence", "2.40 + flux*3.5", True),
    ("turb scale", "1.50", False),
    ("gravity y", "-1.50 + LFO", True),
    ("lifetime", "3.20s", False),
])

draw_node_card(rx1 + 295, ry1 + 165, 255, 138, "Point Trails", "3D", (56, 189, 248, 255), [
    ("geometry in", "Particle System", False),
    ("trail length", "16 points", False),
    ("ribbon width", "0.04", False),
    ("fade alpha", "Exponential", False),
    ("vertex colours", "Velocity Map", False),
    ("geometry out", "-> Render 3D", False),
])

# Row 3: Render 3D & Post FX (Bloom, Color Adjustments, Output)
draw_node_card(rx1 + 16, ry1 + 320, 200, 135, "Render 3D", "3D", (56, 189, 248, 255), [
    ("geometry", "Trails + Pts", False),
    ("shading", "Point Sprites", False),
    ("fov", "45 deg", False),
    ("camera dist", "3.80", False),
    ("bg mode", "Transparent", False),
    ("texture out", "-> Bloom", False),
])

draw_node_card(rx1 + 235, ry1 + 320, 160, 135, "Bloom", "IMAGE", (234, 179, 8, 255), [
    ("texture in", "Render 3D", False),
    ("threshold", "0.65", False),
    ("radius", "1.80", False),
    ("intensity", "1.45", False),
    ("composite", "Screen", False),
    ("texture out", "-> Output", False),
])

draw_node_card(rx1 + 415, ry1 + 320, 135, 135, "Output", "IO", (34, 197, 94, 255), [
    ("video in", "Bloom", False),
    ("resolution", "1920x1080", False),
    ("fps limit", "120 fps", False),
    ("syphon/ndi", "Active", False),
    ("display", "Window / Full", False),
    ("status", "Running", False),
])

# Draw connecting cables between nodes
# Cable 1: Audio In -> Audio Analyze (Audio Purple)
draw.line([(rx1 + 186, ry1 + 80), (rx1 + 205, ry1 + 80)], fill=(168, 85, 247, 220), width=2)
# Cable 2: Audio Analyze -> Particle System (Modulation Ember)
draw.line([(rx1 + 300, ry1 + 147), (rx1 + 300, ry1 + 158), (rx1 + 160, ry1 + 158), (rx1 + 160, ry1 + 165)], fill=(255, 107, 53, 220), width=2)
# Cable 3: LFO -> Particle System (Modulation Ember)
draw.line([(rx1 + 405, ry1 + 120), (rx1 + 276, ry1 + 245)], fill=(255, 107, 53, 180), width=2)
# Cable 4: Particle System -> Point Trails (3D Sky Blue)
draw.line([(rx1 + 276, ry1 + 220), (rx1 + 295, ry1 + 220)], fill=(56, 189, 248, 240), width=2)
# Cable 5: Point Trails -> Render 3D (3D Sky Blue)
draw.line([(rx1 + 420, ry1 + 303), (rx1 + 420, ry1 + 312), (rx1 + 116, ry1 + 312), (rx1 + 116, ry1 + 320)], fill=(56, 189, 248, 240), width=2)
# Cable 6: Render 3D -> Bloom (Image Gold)
draw.line([(rx1 + 216, ry1 + 380), (rx1 + 235, ry1 + 380)], fill=(234, 179, 8, 240), width=2)
# Cable 7: Bloom -> Output (Image Gold)
draw.line([(rx1 + 395, ry1 + 380), (rx1 + 415, ry1 + 380)], fill=(234, 179, 8, 240), width=2)

# Save to destination
out_path = os.path.join(REPO_ROOT, "website", "assets", "blog_audio-reactive-particle-systems-in-real-time.png")
os.makedirs(os.path.dirname(out_path), exist_ok=True)
img.save(out_path, "PNG", optimize=True)
print(f"Generated {out_path} ({W}x{H})")
