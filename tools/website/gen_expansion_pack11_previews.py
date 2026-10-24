#!/usr/bin/env python3
"""Generate high-fidelity preview images for the 10 new creative library items (Pack 11).
Matches brand design: midnight ground #151930, signal #A7B1FF, ember #FF6B35, emerald #4EF2B8, white.
"""
import os, math, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PREVIEWS_DIR = os.path.join(ROOT, "website", "assets", "library", "previews")
TEMPLATES_DIR = os.path.join(ROOT, "website", "assets", "templates")
os.makedirs(PREVIEWS_DIR, exist_ok=True)
os.makedirs(TEMPLATES_DIR, exist_ok=True)

GROUND = np.array([21, 25, 48], dtype=np.float32)     # #151930
SIGNAL = np.array([167, 177, 255], dtype=np.float32)  # #A7B1FF
EMBER = np.array([255, 107, 53], dtype=np.float32)    # #FF6B35
EMERALD = np.array([78, 242, 184], dtype=np.float32)  # #4EF2B8
WHITE = np.array([245, 248, 255], dtype=np.float32)

W, H = 900, 600

def render_riso_halftone_manga():
    """Risograph manga screen-tone halftone lattice with CMYK spot ink misregistration."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5
    min_dim = min(W, H)

    # Radial manga action speedlines
    num_lines = 48
    for i in range(num_lines):
        angle = (i / num_lines) * 2.0 * math.pi
        l1 = min_dim * (0.26 + 0.12 * math.sin(i * 13.7))
        l2 = min_dim * 0.78
        alpha = int(45 + 35 * math.sin(i * 7.3))
        x1 = cx + math.cos(angle) * l1
        y1 = cy + math.sin(angle) * l1
        x2 = cx + math.cos(angle) * l2
        y2 = cy + math.sin(angle) * l2
        draw.line([(x1, y1), (x2, y2)], fill=(245, 240, 230, alpha), width=1)

    # CMYK Halftone dot screen matrices
    spacing = 18.0
    jitter = 5.0
    for x in np.arange(spacing * 0.5, W, spacing):
        for y in np.arange(spacing * 0.5, H, spacing):
            dx = x - cx + jitter
            dy = y - cy + jitter
            dist = math.sqrt(dx*dx + dy*dy) / (min_dim * 0.45)
            wave = math.sin(dist * 12.0 + math.atan2(dy, dx) * 4.0)
            rad = max(0.0, (1.0 - dist * 0.7) * (0.5 + 0.5 * wave)) * spacing * 0.55
            if rad > 0.8:
                # Fluorescent Pink
                draw.ellipse([x + jitter - rad, y + jitter - rad, x + jitter + rad, y + jitter + rad],
                             fill=(255, 0, 128, 180))

            dx2 = x - cx - jitter * 0.8
            dy2 = y - cy - jitter * 0.8
            dist2 = math.sqrt(dx2*dx2 + dy2*dy2) / (min_dim * 0.45)
            wave2 = math.cos(dist2 * 14.0 + math.atan2(dy2, dx2) * 3.0)
            rad2 = max(0.0, (1.0 - dist2 * 0.75) * (0.5 + 0.5 * wave2)) * spacing * 0.50
            if rad2 > 0.8:
                # Electric Cyan
                draw.ellipse([x - jitter*0.8 - rad2, y - jitter*0.8 - rad2, x - jitter*0.8 + rad2, y - jitter*0.8 + rad2],
                             fill=(0, 215, 242, 160))

    # Center ember starburst badge
    spikes = 16
    star_pts = []
    for i in range(spikes * 2 + 1):
        a = (i * math.pi) / spikes
        r = min_dim * 0.16 if (i % 2 == 0) else min_dim * 0.07
        star_pts.append((cx + math.cos(a) * r, cy + math.sin(a) * r))
    draw.polygon(star_pts, fill=(255, 107, 53, 230))
    draw.ellipse([cx - 18, cy - 18, cx + 18, cy + 18], fill=(245, 240, 230, 255))

    im.save(os.path.join(PREVIEWS_DIR, "riso-halftone-manga.jpg"), quality=94)
    print("riso-halftone-manga.jpg saved")

def render_ukiyoe_wave_strata():
    """Traditional woodblock wave strata with bokashi indigo gradients and curling sea-claws."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5
    min_dim = min(W, H)

    # Bokashi cartographic contour rings
    for c in range(1, 9):
        r = min_dim * (0.12 * c)
        draw.ellipse([cx - r, cy * 0.9 - r * 0.65, cx + r, cy * 0.9 + r * 0.65],
                     outline=(167, 177, 255, int(25 + c * 8)), width=1)

    # 7 tiered wave strata layers
    layers = 7
    for l in range(layers):
        layer_frac = l / layers
        base_y = H * (0.35 + layer_frac * 0.55)
        blue = int(15 + layer_frac * 60)
        green = int(35 + layer_frac * 95)
        indigo = int(100 + layer_frac * 145)

        pts = [(0, H), (0, base_y)]
        steps = 80
        claws = []
        for s in range(steps + 1):
            x = (s / steps) * W
            phase = (s / steps) * 2.0 * math.pi * 2.5 + l * 1.3
            crest = math.pow(math.sin(phase) * 0.5 + 0.5, 1.4)
            wave_h = min_dim * (0.08 + layer_frac * 0.06)
            y = base_y - crest * wave_h + math.sin(x * 0.02) * (min_dim * 0.02)
            pts.append((x, y))
            if crest > 0.55:
                claws.append((x, y, (crest - 0.55) * min_dim * 0.045))
        pts.extend([(W, base_y), (W, H)])

        draw.polygon(pts, fill=(blue, green, indigo, 230))
        # Wave crest outline
        for s in range(len(pts) - 4):
            draw.line([pts[2 + s], pts[3 + s]], fill=(245, 240, 230, 110), width=2)

        # Sea-claw foam nodes
        for cx_claw, cy_claw, c_size in claws:
            draw.ellipse([cx_claw - c_size, cy_claw - c_size * 1.3,
                          cx_claw + c_size, cy_claw + c_size * 1.3],
                         fill=(245, 240, 230, 235))
            draw.ellipse([cx_claw + 4, cy_claw - c_size - 6,
                          cx_claw + 7, cy_claw - c_size - 3],
                         fill=(245, 240, 230, 200))

    # Vermillion woodblock artist seal stamp
    sx, sy = W * 0.84, H * 0.18
    s_r = min_dim * 0.06
    draw.ellipse([sx - s_r, sy - s_r, sx + s_r, sy + s_r], fill=(255, 107, 53, 230))
    draw.rectangle([sx - s_r * 0.6, sy - s_r * 0.6, sx + s_r * 0.6, sy + s_r * 0.6],
                   outline=(245, 240, 230, 220), width=1)

    im.save(os.path.join(PREVIEWS_DIR, "ukiyoe-wave-strata.jpg"), quality=94)
    print("ukiyoe-wave-strata.jpg saved")

def render_swiss_brutalist_monolith():
    """Modular constructivist concrete monolith with cantilevered slabs."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.52

    # Draw isometric constructivist architectural monolith
    rot_y = 0.55
    rot_x = 0.42

    def project_pt(x, y, z):
        x1 = x * math.cos(rot_y) + z * math.sin(rot_y)
        z1 = -x * math.sin(rot_y) + z * math.cos(rot_y)
        y2 = y * math.cos(rot_x) - z1 * math.sin(rot_x)
        z2 = y * math.sin(rot_x) + z1 * math.cos(rot_x)
        fov = 3.2
        dist = 4.2 - z2
        px = cx + (x1 / dist) * fov * 240
        py = cy - (y2 / dist) * fov * 240
        return px, py

    def draw_box(x, y, z, sx, sy, sz, color_fill, color_edge):
        h_sx, h_sy, h_sz = sx * 0.5, sy * 0.5, sz * 0.5
        v = [
            project_pt(x - h_sx, y - h_sy, z - h_sz),
            project_pt(x + h_sx, y - h_sy, z - h_sz),
            project_pt(x + h_sx, y + h_sy, z - h_sz),
            project_pt(x - h_sx, y + h_sy, z - h_sz),
            project_pt(x - h_sx, y - h_sy, z + h_sz),
            project_pt(x + h_sx, y - h_sy, z + h_sz),
            project_pt(x + h_sx, y + h_sy, z + h_sz),
            project_pt(x - h_sx, y + h_sy, z + h_sz),
        ]
        faces = [
            ([v[4], v[5], v[6], v[7]], color_fill),
            ([v[1], v[2], v[6], v[5]], (int(color_fill[0]*0.8), int(color_fill[1]*0.8), int(color_fill[2]*0.8), color_fill[3])),
            ([v[2], v[3], v[7], v[6]], (int(color_fill[0]*1.1), int(color_fill[1]*1.1), int(color_fill[2]*1.1), color_fill[3])),
        ]
        for f, col in faces:
            draw.polygon(f, fill=col, outline=color_edge)

    # Base plinth
    draw_box(0, -1.2, 0, 2.2, 0.2, 2.2, (32, 38, 62, 255), (45, 55, 90, 255))

    # Concrete tower core
    draw_box(0, 0, 0, 0.55, 2.2, 0.55, (45, 52, 78, 255), (65, 75, 110, 255))

    # Cantilevered slabs
    floors = 9
    for i in range(floors):
        u = i / floors
        y = (u - 0.5) * 2.1
        angle = i * 1.570796
        shift = math.sin(i * 1.8) * 0.55
        sx = math.cos(angle) * shift
        sz = math.sin(angle) * shift
        w_slab = 0.95 + 0.3 * math.sin(i * 2.3)
        d_slab = 0.95 + 0.3 * math.cos(i * 2.3)
        c_fill = (167, 177, 255, 230) if (i % 2 == 0) else (68, 78, 115, 230)
        draw_box(sx, y, sz, w_slab, 0.16, d_slab, c_fill, (120, 130, 180, 255))

        if i % 3 == 0:
            # Ember accent strut
            draw_box(sx + w_slab * 0.45, y, sz, 0.08, 0.22, 0.08, (255, 107, 53, 240), (255, 140, 90, 255))

    # Tension cables
    for k in range(4):
        a = k * math.pi * 0.5 + 0.35
        p1 = project_pt(math.cos(a) * 0.9, -1.1, math.sin(a) * 0.9)
        p2 = project_pt(math.cos(a) * 0.45, 1.1, math.sin(a) * 0.45)
        draw.line([p1, p2], fill=(78, 242, 184, 180), width=2)

    im.save(os.path.join(PREVIEWS_DIR, "swiss-brutalist-monolith-3d.jpg"), quality=94)
    print("swiss-brutalist-monolith-3d.jpg saved")

def render_kinetic_moire_sculpture():
    """Concentric nested hyperboloid cages with optical moire interference."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5

    rot_y = 0.65
    rot_x = 0.45

    def project_pt(x, y, z):
        x1 = x * math.cos(rot_y) + z * math.sin(rot_y)
        z1 = -x * math.sin(rot_y) + z * math.cos(rot_y)
        y2 = y * math.cos(rot_x) - z1 * math.sin(rot_x)
        z2 = y * math.sin(rot_x) + z1 * math.cos(rot_x)
        fov = 3.2
        dist = 4.2 - z2
        px = cx + (x1 / dist) * fov * 250
        py = cy - (y2 / dist) * fov * 250
        return px, py

    cages = 4
    ribs = 36
    half_h = 1.1

    for c in range(cages):
        c_frac = c / cages
        base_r = 0.5 + c_frac * 0.65
        twist = 1.6 * (1.0 if c % 2 == 0 else -1.0)
        c_spin = c * 0.4

        col = [(167, 177, 255, 180), (78, 242, 184, 180), (255, 107, 53, 190)][c % 3]

        for i in range(ribs):
            u = i / ribs
            a_top = u * 2.0 * math.pi + c_spin + twist
            a_bot = u * 2.0 * math.pi + c_spin - twist

            p_top = project_pt(math.cos(a_top) * base_r, half_h, math.sin(a_top) * base_r)
            p_bot = project_pt(math.cos(a_bot) * base_r, -half_h, math.sin(a_bot) * base_r)
            draw.line([p_top, p_bot], fill=col, width=1)

        # Collar rings
        pts_top = [project_pt(math.cos(u * 2.0 * math.pi + c_spin + twist) * base_r, half_h,
                              math.sin(u * 2.0 * math.pi + c_spin + twist) * base_r) for u in np.linspace(0, 1, 40)]
        pts_bot = [project_pt(math.cos(u * 2.0 * math.pi + c_spin - twist) * base_r, -half_h,
                              math.sin(u * 2.0 * math.pi + c_spin - twist) * base_r) for u in np.linspace(0, 1, 40)]
        for s in range(len(pts_top) - 1):
            draw.line([pts_top[s], pts_top[s+1]], fill=(245, 240, 230, 120), width=1)
            draw.line([pts_bot[s], pts_bot[s+1]], fill=(245, 240, 230, 120), width=1)

    im.save(os.path.join(PREVIEWS_DIR, "kinetic-moire-sculpture-3d.jpg"), quality=94)
    print("kinetic-moire-sculpture-3d.jpg saved")

def render_cyber_acid_303():
    """Cyber Acid 303 Synth UI & waveform visualization."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    # Dark chassis panel with brushed metallic styling
    draw.rounded_rectangle([40, 40, W - 40, H - 40], radius=16, fill=(28, 33, 58, 255), outline=(48, 56, 92, 255), width=2)
    draw.rounded_rectangle([60, 60, W - 60, 160], radius=8, fill=(16, 19, 36, 255), outline=(36, 44, 76, 255), width=1)

    # Glowing CRT oscilloscope showing saturated resonant 303 acid squelch waveform
    wave_pts = []
    for x in range(80, W - 80):
        u = (x - 80) / (W - 160)
        p = (u * 4.0) % 1.0
        saw = p * 2.0 - 1.0
        # Resonant ladder ripple
        res = math.sin(u * 40.0) * math.exp(-((p - 0.2) ** 2) * 20.0) * 0.4
        y_val = 110 + (saw * 0.6 + res) * 35.0
        wave_pts.append((x, y_val))

    for i in range(len(wave_pts) - 1):
        draw.line([wave_pts[i], wave_pts[i+1]], fill=(255, 107, 53, 240), width=3)
        draw.line([(wave_pts[i][0], wave_pts[i][1] - 1), (wave_pts[i+1][0], wave_pts[i+1][1] - 1)], fill=(255, 240, 220, 255), width=1)

    # Knobs rack: Cutoff, Resonance, Env Mod, Accent, Drive
    knobs = [("CUTOFF", 0.75, EMBER), ("RESONANCE", 0.88, EMBER), ("ENV MOD", 0.7, SIGNAL), ("ACCENT", 0.65, EMERALD), ("DRIVE", 0.6, SIGNAL)]
    spacing = (W - 160) / len(knobs)
    for idx, (label, val, col) in enumerate(knobs):
        kx = 80 + idx * spacing + spacing * 0.5
        ky = 320
        kr = 44
        draw.ellipse([kx - kr, ky - kr, kx + kr, ky + kr], fill=(36, 42, 70, 255), outline=(58, 68, 108, 255), width=2)
        # Value arc
        a_start = math.radians(135)
        a_sweep = math.radians(270 * val)
        pts_arc = []
        for step in range(30):
            cur_a = a_start + (step / 29) * a_sweep
            pts_arc.append((kx + math.cos(cur_a) * (kr - 6), ky + math.sin(cur_a) * (kr - 6)))
        for s in range(len(pts_arc) - 1):
            draw.line([pts_arc[s], pts_arc[s+1]], fill=(int(col[0]), int(col[1]), int(col[2]), 240), width=4)
        # Pointer
        end_a = a_start + a_sweep
        draw.line([(kx, ky), (kx + math.cos(end_a) * (kr - 10), ky + math.sin(end_a) * (kr - 10))], fill=(245, 248, 255, 255), width=3)
        # Center cap
        draw.ellipse([kx - 16, ky - 16, kx + 16, ky + 16], fill=(21, 25, 48, 255))

    # Pattern step buttons along bottom
    for step in range(16):
        bx = 80 + step * ((W - 160) / 16.0) + 4
        by = 460
        bw = ((W - 160) / 16.0) - 8
        bh = 32
        active = (step in (0, 3, 6, 8, 10, 12, 14))
        btn_col = (255, 107, 53, 230) if active else (40, 48, 80, 200)
        draw.rounded_rectangle([bx, by, bx + bw, by + bh], radius=4, fill=btn_col)

    im.save(os.path.join(PREVIEWS_DIR, "cyber-acid-303-synth.jpg"), quality=94)
    im_webp = im.resize((960, 600), Image.Resampling.LANCZOS)
    im_webp.save(os.path.join(TEMPLATES_DIR, "cyber-acid-303-synth.webp"), "WEBP", quality=90)
    print("cyber-acid-303-synth.jpg and webp saved")

def render_spectral_resonator_bell():
    """Spectral Resonator Bell Synth modal dispersion and acoustic spectrum."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    draw.rounded_rectangle([40, 40, W - 40, H - 40], radius=16, fill=(28, 33, 58, 255), outline=(48, 56, 92, 255), width=2)
    draw.rounded_rectangle([60, 60, W - 60, 220], radius=8, fill=(16, 19, 36, 255), outline=(36, 44, 76, 255), width=1)

    # Inharmonic modal spectrum bars & Chladni nodal bell curves
    modes = [(1.0, 1.0, EMBER), (2.42, 0.7, SIGNAL), (3.86, 0.5, EMERALD), (5.14, 0.35, WHITE), (6.92, 0.22, SIGNAL)]
    for freq_mult, amp, col in modes:
        bx = 100 + (freq_mult / 8.0) * (W - 220)
        bh = amp * 120.0
        draw.line([(bx, 200), (bx, 200 - bh)], fill=(int(col[0]), int(col[1]), int(col[2]), 230), width=6)
        draw.ellipse([bx - 6, 200 - bh - 6, bx + 6, 200 - bh + 6], fill=(255, 248, 240, 255))

    # Acoustic decaying sinusoidal modal wave
    wave_pts = []
    for x in range(80, W - 80):
        u = (x - 80) / (W - 160)
        decay = math.exp(-u * 2.8)
        w1 = math.sin(u * 30.0) * 0.5
        w2 = math.sin(u * 72.0) * 0.25
        w3 = math.sin(u * 115.0) * 0.12
        y = 140 + (w1 + w2 + w3) * decay * 55.0
        wave_pts.append((x, y))

    for i in range(len(wave_pts) - 1):
        draw.line([wave_pts[i], wave_pts[i+1]], fill=(167, 177, 255, 200), width=2)

    # Knobs: Strike, Brightness, Damping, Inharmonicity, Decay
    knobs = [("STRIKE", 0.85, EMBER), ("BRIGHT", 0.72, SIGNAL), ("DAMP", 0.45, EMERALD), ("INHARMONIC", 0.68, EMBER), ("DECAY", 0.6, SIGNAL)]
    spacing = (W - 160) / len(knobs)
    for idx, (label, val, col) in enumerate(knobs):
        kx = 80 + idx * spacing + spacing * 0.5
        ky = 370
        kr = 44
        draw.ellipse([kx - kr, ky - kr, kx + kr, ky + kr], fill=(36, 42, 70, 255), outline=(58, 68, 108, 255), width=2)
        a_start = math.radians(135)
        a_sweep = math.radians(270 * val)
        pts_arc = []
        for step in range(30):
            cur_a = a_start + (step / 29) * a_sweep
            pts_arc.append((kx + math.cos(cur_a) * (kr - 6), ky + math.sin(cur_a) * (kr - 6)))
        for s in range(len(pts_arc) - 1):
            draw.line([pts_arc[s], pts_arc[s+1]], fill=(int(col[0]), int(col[1]), int(col[2]), 240), width=4)
        end_a = a_start + a_sweep
        draw.line([(kx, ky), (kx + math.cos(end_a) * (kr - 10), ky + math.sin(end_a) * (kr - 10))], fill=(245, 248, 255, 255), width=3)
        draw.ellipse([kx - 16, ky - 16, kx + 16, ky + 16], fill=(21, 25, 48, 255))

    im.save(os.path.join(PREVIEWS_DIR, "spectral-resonator-bell-synth.jpg"), quality=94)
    im_webp = im.resize((960, 600), Image.Resampling.LANCZOS)
    im_webp.save(os.path.join(TEMPLATES_DIR, "spectral-resonator-bell-synth.webp"), "WEBP", quality=90)
    print("spectral-resonator-bell-synth.jpg and webp saved")

def render_analog_tape_warble():
    """Analog Tape Warble FX reel-to-reel visualization and flutter waveform."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx = W * 0.5

    draw.rounded_rectangle([40, 40, W - 40, H - 40], radius=16, fill=(28, 33, 58, 255), outline=(48, 56, 92, 255), width=2)

    # Dual tape reels
    for r_idx, rx in enumerate([240, W - 240]):
        ry = 180
        rr = 85
        draw.ellipse([rx - rr, ry - rr, rx + rr, ry + rr], fill=(18, 22, 38, 255), outline=(60, 70, 110, 255), width=4)
        # Reel spokes
        for spk in range(3):
            sa = (spk * 2.0 * math.pi / 3.0) + (0.5 if r_idx == 0 else -0.5)
            draw.line([(rx, ry), (rx + math.cos(sa) * (rr - 8), ry + math.sin(sa) * (rr - 8))], fill=(167, 177, 255, 180), width=5)
        draw.ellipse([rx - 28, ry - 28, rx + 28, ry + 28], fill=(36, 42, 70, 255), outline=(255, 107, 53, 230), width=2)

    # Tape path line
    draw.line([(240, 265), (cx, 280), (W - 240, 265)], fill=(255, 107, 53, 220), width=3)
    # Tape head block
    draw.rounded_rectangle([cx - 40, 265, cx + 40, 295], radius=4, fill=(45, 55, 90, 255), outline=(78, 242, 184, 255), width=2)

    # Knobs: Drive, Wow, Flutter, Tape Loss, Mix
    knobs = [("DRIVE", 0.65, EMBER), ("WOW", 0.45, SIGNAL), ("FLUTTER", 0.35, EMERALD), ("TAPE LOSS", 0.55, EMBER), ("MIX", 0.95, SIGNAL)]
    spacing = (W - 160) / len(knobs)
    for idx, (label, val, col) in enumerate(knobs):
        kx = 80 + idx * spacing + spacing * 0.5
        ky = 410
        kr = 44
        draw.ellipse([kx - kr, ky - kr, kx + kr, ky + kr], fill=(36, 42, 70, 255), outline=(58, 68, 108, 255), width=2)
        a_start = math.radians(135)
        a_sweep = math.radians(270 * val)
        pts_arc = []
        for step in range(30):
            cur_a = a_start + (step / 29) * a_sweep
            pts_arc.append((kx + math.cos(cur_a) * (kr - 6), ky + math.sin(cur_a) * (kr - 6)))
        for s in range(len(pts_arc) - 1):
            draw.line([pts_arc[s], pts_arc[s+1]], fill=(int(col[0]), int(col[1]), int(col[2]), 240), width=4)
        end_a = a_start + a_sweep
        draw.line([(kx, ky), (kx + math.cos(end_a) * (kr - 10), ky + math.sin(end_a) * (kr - 10))], fill=(245, 248, 255, 255), width=3)
        draw.ellipse([kx - 16, ky - 16, kx + 16, ky + 16], fill=(21, 25, 48, 255))

    im.save(os.path.join(PREVIEWS_DIR, "analog-tape-warble-fx.jpg"), quality=94)
    im_webp = im.resize((960, 600), Image.Resampling.LANCZOS)
    im_webp.save(os.path.join(TEMPLATES_DIR, "analog-tape-warble-fx.webp"), "WEBP", quality=90)
    print("analog-tape-warble-fx.jpg and webp saved")

def render_spectral_freeze_reverb():
    """Spectral Freeze Reverb FX infinite hold FDN visualization."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    draw.rounded_rectangle([40, 40, W - 40, H - 40], radius=16, fill=(28, 33, 58, 255), outline=(48, 56, 92, 255), width=2)
    draw.rounded_rectangle([60, 60, W - 60, 220], radius=8, fill=(16, 19, 36, 255), outline=(36, 44, 76, 255), width=1)

    # Shimmer reverb FDN particle dispersion cloud
    np.random.seed(42)
    for _ in range(350):
        px = np.random.uniform(80, W - 80)
        py = np.random.uniform(80, 200)
        rad = np.random.uniform(1.5, 4.5)
        c_pick = [SIGNAL, EMERALD, EMBER, WHITE][np.random.randint(4)]
        draw.ellipse([px - rad, py - rad, px + rad, py + rad],
                     fill=(int(c_pick[0]), int(c_pick[1]), int(c_pick[2]), int(np.random.uniform(80, 220))))

    # Infinite freeze hold horizon line
    draw.line([(80, 140), (W - 80, 140)], fill=(78, 242, 184, 200), width=2)

    # Knobs: Decay, Damping, Freeze, Shimmer, Mix
    knobs = [("DECAY", 0.88, EMBER), ("DAMPING", 0.38, SIGNAL), ("FREEZE", 0.75, EMERALD), ("SHIMMER", 0.45, EMBER), ("MIX", 0.5, SIGNAL)]
    spacing = (W - 160) / len(knobs)
    for idx, (label, val, col) in enumerate(knobs):
        kx = 80 + idx * spacing + spacing * 0.5
        ky = 370
        kr = 44
        draw.ellipse([kx - kr, ky - kr, kx + kr, ky + kr], fill=(36, 42, 70, 255), outline=(58, 68, 108, 255), width=2)
        a_start = math.radians(135)
        a_sweep = math.radians(270 * val)
        pts_arc = []
        for step in range(30):
            cur_a = a_start + (step / 29) * a_sweep
            pts_arc.append((kx + math.cos(cur_a) * (kr - 6), ky + math.sin(cur_a) * (kr - 6)))
        for s in range(len(pts_arc) - 1):
            draw.line([pts_arc[s], pts_arc[s+1]], fill=(int(col[0]), int(col[1]), int(col[2]), 240), width=4)
        end_a = a_start + a_sweep
        draw.line([(kx, ky), (kx + math.cos(end_a) * (kr - 10), ky + math.sin(end_a) * (kr - 10))], fill=(245, 248, 255, 255), width=3)
        draw.ellipse([kx - 16, ky - 16, kx + 16, ky + 16], fill=(21, 25, 48, 255))

    im.save(os.path.join(PREVIEWS_DIR, "spectral-freeze-reverb-fx.jpg"), quality=94)
    im_webp = im.resize((960, 600), Image.Resampling.LANCZOS)
    im_webp.save(os.path.join(TEMPLATES_DIR, "spectral-freeze-reverb-fx.webp"), "WEBP", quality=90)
    print("spectral-freeze-reverb-fx.jpg and webp saved")

def render_riso_pop_screen():
    """Risograph screen print halftone matrix preview."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    aspect = W / H

    y_indices, x_indices = np.indices((H, W))
    p_x = ((x_indices / W) - 0.5) * aspect
    p_y = ((y_indices / H) - 0.5)

    r = np.sqrt(p_x**2 + p_y**2)
    ang = np.arctan2(p_y, p_x)

    motif = np.sin(r * 18.0 + ang * 5.0) * 0.5 + 0.5
    core = np.exp(-r * 3.5)
    val = np.clip((motif * 0.7 + core * 0.5) * 1.6, 0.0, 1.0)

    dot_scale = 36.0
    ink_bleed = 1.2
    misreg = 0.025

    rotC = 0.26
    pC_x = p_x * math.cos(rotC) - p_y * math.sin(rotC) + misreg
    pC_y = p_x * math.sin(rotC) + p_y * math.cos(rotC)
    dotC = np.sqrt(((pC_x * dot_scale + 0.5) % 1.0 - 0.5)**2 + ((pC_y * dot_scale + 0.5) % 1.0 - 0.5)**2) * 2.0
    inkC = np.clip((val * ink_bleed + 0.25 - dotC) / 0.25, 0.0, 1.0)

    rotM = 1.31
    pM_x = p_x * math.cos(rotM) - p_y * math.sin(rotM) - misreg
    pM_y = p_x * math.sin(rotM) + p_y * math.cos(rotM) - misreg * 0.5
    dotM = np.sqrt(((pM_x * dot_scale + 0.5) % 1.0 - 0.5)**2 + ((pM_y * dot_scale + 0.5) % 1.0 - 0.5)**2) * 2.0
    inkM = np.clip(((1.0 - val) * ink_bleed + 0.25 - dotM) / 0.25, 0.0, 1.0)

    cPaper = np.array([245, 240, 232], dtype=np.float32)
    cPink = np.array([255, 0, 128], dtype=np.float32)
    cCyan = np.array([0, 215, 242], dtype=np.float32)

    fade = np.clip((2.2 - r * 2.0), 0.0, 1.0)

    for y in range(H):
        for x in range(W):
            iM = inkM[y, x]
            iC = inkC[y, x]
            col = cPaper * (1.0 - (1.0 - cPink / 255.0) * iM) * (1.0 - (1.0 - cCyan / 255.0) * iC)
            col = GROUND * (1.0 - fade[y, x]) + col * fade[y, x]
            out[y, x] = np.clip(col, 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "riso-pop-screen.jpg"), quality=94)
    print("riso-pop-screen.jpg saved")

def render_analog_crt_vhs_glitch():
    """Analog CRT raster & VHS tracking glitch preview."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    aspect = W / H

    y_indices, x_indices = np.indices((H, W))
    p0_x = ((x_indices / W) - 0.5) * aspect
    p0_y = ((y_indices / H) - 0.5)
    r2 = p0_x**2 + p0_y**2

    p_x = p0_x * (1.0 + 0.28 * r2)
    p_y = p0_y * (1.0 + 0.28 * r2)

    # VHS tracking bar glitch
    bar_y = 0.42
    dist_bar = np.abs(p_y - (bar_y - 0.5))
    jitter = np.where(dist_bar < 0.08, np.sin(p_y * 120.0) * 0.035, 0.0)
    p_x += jitter

    r = np.sqrt(p_x**2 + p_y**2)
    ang = np.arctan2(p_y, p_x)
    ring = np.sin(r * 22.0 + ang * 4.0) * 0.5 + 0.5

    ca = 0.035
    pRed_r = np.sqrt((p_x * (1.0 + ca))**2 + (p_y * (1.0 + ca))**2)
    pBlue_r = np.sqrt((p_x * (1.0 - ca))**2 + (p_y * (1.0 - ca))**2)

    valR = np.sin(pRed_r * 22.0) * 0.5 + 0.5
    valG = ring
    valB = np.sin(pBlue_r * 22.0) * 0.5 + 0.5

    scan = np.sin(p_y * 240.0 * 2.0 * math.pi) * 0.25 + 0.75
    vignette = np.clip(1.0 - r2 * 1.8, 0.0, 1.0)

    for y in range(H):
        for x in range(W):
            cPhosphor = np.array([valR[y, x] * 1.1 * 255, valG[y, x] * 0.95 * 255, valB[y, x] * 1.3 * 255]) * 1.4 * scan[y, x]
            col = GROUND * (1.0 - vignette[y, x]) + cPhosphor * vignette[y, x]
            out[y, x] = np.clip(col, 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "analog-crt-vhs-glitch.jpg"), quality=94)
    print("analog-crt-vhs-glitch.jpg saved")

def main():
    render_riso_halftone_manga()
    render_ukiyoe_wave_strata()
    render_swiss_brutalist_monolith()
    render_kinetic_moire_sculpture()
    render_cyber_acid_303()
    render_spectral_resonator_bell()
    render_analog_tape_warble()
    render_spectral_freeze_reverb()
    render_riso_pop_screen()
    render_analog_crt_vhs_glitch()
    print("All Pack 11 previews successfully generated!")

if __name__ == "__main__":
    main()
