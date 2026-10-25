#!/usr/bin/env python3
"""Generate high-fidelity preview images for the 10 new creative library items (Pack 12).
Matches brand design: midnight ground #151930, signal #A7B1FF, ember #FF6B35, emerald #4EF2B8, white #EEF1FA.
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

def render_ditherpunk_halftone_grid():
    """1-bit cybernetic ditherpunk lattice with ordered Bayer matrix thresholding."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5
    min_dim = min(W, H)

    # 1. Background scanlines with glitch breaks
    num_scans = 50
    for s in range(num_scans):
        y = (s / num_scans) * H
        glitch = math.sin(s * 13.7) > 0.8
        dx = math.sin(s * 5.2) * 25.0 if glitch else 0.0
        alpha = int(25 + 18 * math.sin(s * 3.4))
        draw.line([(dx, y), (W + dx, y)], fill=(167, 177, 255, alpha), width=1)

    # 2. Bayer 8x8 matrix ordered dither
    bayer8 = [
         0/64, 32/64,  8/64, 40/64,  2/64, 34/64, 10/64, 42/64,
        48/64, 16/64, 56/64, 24/64, 50/64, 18/64, 58/64, 26/64,
        12/64, 44/64,  4/64, 36/64, 14/64, 46/64,  6/64, 38/64,
        60/64, 28/64, 52/64, 20/64, 62/64, 30/64, 54/64, 22/64,
         3/64, 35/64, 11/64, 43/64,  1/64, 33/64,  9/64, 41/64,
        51/64, 19/64, 59/64, 27/64, 49/64, 17/64, 57/64, 25/64,
        15/64, 47/64,  7/64, 39/64, 13/64, 45/64,  5/64, 37/64,
        63/64, 31/64, 55/64, 23/64, 61/64, 29/64, 53/64, 21/64
    ]

    cell = 14.0
    for x in np.arange(0, W, cell):
        for y in np.arange(0, H, cell):
            bx = int(x / cell) % 8
            by = int(y / cell) % 8
            b_val = bayer8[by * 8 + bx]

            dx = x - cx
            dy = y - cy
            r = math.sqrt(dx*dx + dy*dy) / (min_dim * 0.46)
            ang = math.atan2(dy, dx)

            wave = math.sin(r * 16.0 + ang * 6.0) * 0.5 + 0.5
            core = max(0.0, 1.0 - r * 1.1) * (0.6 + 0.4 * wave)
            orbit = math.exp(-((r - 0.72)**2) * 70.0) * (math.sin(ang * 12.0) * 0.5 + 0.5)
            lum = core * 0.75 + orbit * 0.65

            if lum > b_val * 0.55:
                if r < 0.32:
                    col = (255, 107, 53, 240)  # Ember core
                elif r < 0.62:
                    col = (78, 242, 184, 230)  # Emerald
                else:
                    col = (167, 177, 255, 210) # Signal
                draw.rectangle([x, y, x + cell * 0.85, y + cell * 0.85], fill=col)

    # 3. Telemetry crosshairs
    draw.line([(cx - min_dim * 0.1, cy), (cx + min_dim * 0.1, cy)], fill=(245, 248, 255, 220), width=2)
    draw.line([(cx, cy - min_dim * 0.1), (cx, cy + min_dim * 0.1)], fill=(245, 248, 255, 220), width=2)
    draw.ellipse([cx - min_dim * 0.14, cy - min_dim * 0.14, cx + min_dim * 0.14, cy + min_dim * 0.14],
                 outline=(245, 248, 255, 190), width=2)
    draw.ellipse([cx - min_dim * 0.28, cy - min_dim * 0.28, cx + min_dim * 0.28, cy + min_dim * 0.28],
                 outline=(167, 177, 255, 140), width=1)

    im.save(os.path.join(PREVIEWS_DIR, "ditherpunk-halftone-grid.jpg"), quality=94)
    print("ditherpunk-halftone-grid.jpg saved")

def render_technical_blueprint_schematic():
    """Cyanotype drafting schematic with precision architectural grids and compass arcs."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5
    min_dim = min(W, H)

    # 1. Blueprint drafting grid
    spacing = 22.0
    for x in np.arange(0, W, spacing):
        is_major = (int(x / spacing) % 4 == 0)
        draw.line([(x, 0), (x, H)], fill=(167, 177, 255, 80 if is_major else 28), width=1)
    for y in np.arange(0, H, spacing):
        is_major = (int(y / spacing) % 4 == 0)
        draw.line([(0, y), (W, y)], fill=(167, 177, 255, 80 if is_major else 28), width=1)

    # 2. Concentric vernier compass arcs
    rings = 6
    for r in range(1, rings + 1):
        rad = min_dim * (0.09 + (r / rings) * 0.38)
        draw.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], outline=(167, 177, 255, 110), width=1)
        ticks = r * 14
        for k in range(ticks):
            a = (k / ticks) * 2.0 * math.pi
            tlen = min_dim * (0.024 if k % 4 == 0 else 0.01)
            x1 = cx + math.cos(a) * (rad - tlen * 0.5)
            y1 = cy + math.sin(a) * (rad - tlen * 0.5)
            x2 = cx + math.cos(a) * (rad + tlen * 0.5)
            y2 = cy + math.sin(a) * (rad + tlen * 0.5)
            col = (255, 107, 53, 210) if (k % 4 == 0) else (167, 177, 255, 95)
            draw.line([(x1, y1), (x2, y2)], fill=col, width=1)

    # 3. Precision leader lines and callout badges
    callouts = 8
    for i in range(callouts):
        a = (i / callouts) * 2.0 * math.pi + 0.25
        dist_c = min_dim * (0.24 + 0.12 * math.sin(i * 1.8))
        px = cx + math.cos(a) * dist_c
        py = cy + math.sin(a) * dist_c
        draw.line([(cx, cy), (px, py)], fill=(78, 242, 184, 120), width=1)
        draw.ellipse([px - 9, py - 9, px + 9, py + 9], outline=(78, 242, 184, 220), width=2)
        draw.line([(px - 6, py), (px + 6, py)], fill=(255, 107, 53, 240), width=2)
        draw.line([(px, py - 6), (px, py + 6)], fill=(255, 107, 53, 240), width=2)

    # 4. Central drafting rosette
    side = min_dim * 0.14
    draw.rectangle([cx - side*0.5, cy - side*0.5, cx + side*0.5, cy + side*0.5],
                   outline=(245, 248, 255, 220), width=2)
    # 45 deg rotated box
    pts = []
    for k in range(4):
        a = k * math.pi * 0.5 + math.pi * 0.25
        pts.append((cx + math.cos(a) * side * 0.707, cy + math.sin(a) * side * 0.707))
    draw.polygon(pts, outline=(255, 107, 53, 220), width=2)
    draw.ellipse([cx - 5, cy - 5, cx + 5, cy + 5], fill=(245, 248, 255, 240))

    im.save(os.path.join(PREVIEWS_DIR, "technical-blueprint-schematic.jpg"), quality=94)
    print("technical-blueprint-schematic.jpg saved")

def render_liminal_void_corridor():
    """Infinite brutalist corridor with receding concrete portals and lighting troughs."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5

    portals = 16
    for i in range(portals, 0, -1):
        u = i / portals
        z_factor = u ** 2.2
        pw = W * 0.9 * z_factor
        ph = H * 0.85 * z_factor
        thick = 16.0 * z_factor

        # Atmospheric color blend towards midnight ground #151930
        c_val = int(21 + (45 - 21) * (1.0 - u))
        c_frame = (c_val + 10, c_val + 16, c_val + 34, 255)
        c_edge = (c_val + 35, c_val + 45, c_val + 75, 255)

        x1 = cx - pw * 0.5
        y1 = cy - ph * 0.5
        x2 = cx + pw * 0.5
        y2 = cy + ph * 0.5

        # Lintel, floor, and side pillars
        draw.rectangle([x1 - thick, y1 - thick, x2 + thick, y1], fill=c_frame, outline=c_edge)
        draw.rectangle([x1 - thick, y2, x2 + thick, y2 + thick], fill=c_frame, outline=c_edge)
        draw.rectangle([x1 - thick, y1, x1, y2], fill=c_frame, outline=c_edge)
        draw.rectangle([x2, y1, x2 + thick, y2], fill=c_frame, outline=c_edge)

        # Floor lighting runway strip (Ember)
        strip_w = max(2.0, 14.0 * z_factor)
        draw.rectangle([cx - strip_w * 0.5, y2 - 2, cx + strip_w * 0.5, y2 + thick + 2],
                       fill=(255, 107, 53, int(220 * (1.0 - u * 0.7))))

        # Ceiling light strip (Signal)
        draw.rectangle([cx - strip_w * 0.5, y1 - thick - 2, cx + strip_w * 0.5, y1 + 2],
                       fill=(167, 177, 255, int(200 * (1.0 - u * 0.7))))

        # Receding floating plinth
        if i % 4 == 0:
            plinth_w = 40.0 * z_factor
            plinth_h = 75.0 * z_factor
            px = cx + math.sin(i * 1.5) * (pw * 0.25)
            py = cy + math.sin(i * 2.3) * 15.0
            draw.rectangle([px - plinth_w*0.5, py - plinth_h*0.5, px + plinth_w*0.5, py + plinth_h*0.5],
                           fill=(78, 242, 184, 220), outline=(167, 177, 255, 240))

    im.save(os.path.join(PREVIEWS_DIR, "liminal-void-corridor-3d.jpg"), quality=94)
    print("liminal-void-corridor-3d.jpg saved")

def render_retro_surreal_monolith():
    """Metaphysical surrealist architectural plinth with checkered floor and floating polyhedral relic."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.54

    # 1. Perspective checkered floor
    grid_cols = 16
    grid_rows = 12
    horizon_y = H * 0.44

    for r in range(grid_rows - 1, -1, -1):
        u1 = r / grid_rows
        u2 = (r + 1) / grid_rows
        y1 = horizon_y + (u1 ** 2.2) * (H - horizon_y)
        y2 = horizon_y + (u2 ** 2.2) * (H - horizon_y)
        scale1 = (u1 ** 1.8) * 1.6
        scale2 = (u2 ** 1.8) * 1.6

        for c in range(grid_cols):
            frac1 = (c - grid_cols * 0.5) / (grid_cols * 0.5)
            frac2 = (c + 1 - grid_cols * 0.5) / (grid_cols * 0.5)
            x1_l = cx + frac1 * (W * 0.5) * scale1
            x1_r = cx + frac2 * (W * 0.5) * scale1
            x2_l = cx + frac1 * (W * 0.5) * scale2
            x2_r = cx + frac2 * (W * 0.5) * scale2

            is_alt = (r + c) % 2 == 0
            col = (30, 36, 62, 255) if is_alt else (167, 177, 255, 210)
            draw.polygon([(x1_l, y1), (x1_r, y1), (x2_r, y2), (x2_l, y2)], fill=col, outline=(45, 55, 90, 180))

    # 2. De Chirico arcade colonnade on left
    arches = 5
    for a in range(arches):
        u = a / arches
        arch_x = W * 0.12 + u * (W * 0.18)
        base_y = horizon_y + (u ** 1.8) * (H - horizon_y) * 0.85
        arch_w = 32.0 * (0.6 + u * 0.8)
        arch_h = 160.0 * (0.6 + u * 0.8)

        draw.rectangle([arch_x, base_y - arch_h, arch_x + arch_w * 0.3, base_y], fill=(55, 65, 98, 255))
        draw.rectangle([arch_x + arch_w * 0.7, base_y - arch_h, arch_x + arch_w, base_y], fill=(55, 65, 98, 255))
        draw.rectangle([arch_x, base_y - arch_h - 18, arch_x + arch_w, base_y - arch_h], fill=(75, 88, 128, 255))

    # 3. Stepped plinth in center
    draw.polygon([(cx - 140, horizon_y + 160), (cx + 140, horizon_y + 160),
                  (cx + 100, horizon_y + 130), (cx - 100, horizon_y + 130)], fill=(45, 55, 88, 255), outline=(120, 135, 180, 255))
    draw.polygon([(cx - 90, horizon_y + 130), (cx + 90, horizon_y + 130),
                  (cx + 65, horizon_y + 105), (cx - 65, horizon_y + 105)], fill=(62, 74, 112, 255), outline=(140, 155, 200, 255))

    # 4. Cast shadow of hovering relic
    draw.ellipse([cx - 55, horizon_y + 110, cx + 75, horizon_y + 128], fill=(12, 14, 28, 230))

    # 5. Hovering multifaceted polyhedral relic (Ember + Emerald)
    poly_y = horizon_y + 10
    rad_poly = 70.0
    pts_ember = [
        (cx, poly_y - rad_poly),
        (cx + rad_poly * 0.866, poly_y + rad_poly * 0.5),
        (cx - rad_poly * 0.866, poly_y + rad_poly * 0.5)
    ]
    draw.polygon(pts_ember, fill=(255, 107, 53, 240), outline=(255, 160, 110, 255))

    pts_emerald = [
        (cx, poly_y + rad_poly),
        (cx + rad_poly * 0.866, poly_y - rad_poly * 0.5),
        (cx - rad_poly * 0.866, poly_y - rad_poly * 0.5)
    ]
    draw.polygon(pts_emerald, fill=(78, 242, 184, 190), outline=(140, 255, 210, 255))

    draw.ellipse([cx - 16, poly_y - 16, cx + 16, poly_y + 16], fill=(245, 248, 255, 240))

    im.save(os.path.join(PREVIEWS_DIR, "retro-surreal-monolith-3d.jpg"), quality=94)
    print("retro-surreal-monolith-3d.jpg saved")

def render_liminal_drone_pad_synth():
    """Hardware synth rack styling for Liminal Drone Pad Synth."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    # Chassis panel
    draw.rounded_rectangle([40, 40, W - 40, H - 40], radius=16, fill=(28, 33, 58, 255), outline=(48, 56, 92, 255), width=2)
    # Oscilloscope display
    draw.rounded_rectangle([60, 60, W - 60, 220], radius=8, fill=(16, 19, 36, 255), outline=(36, 44, 76, 255), width=1)

    # Detuned cluster waveform with slow beating
    wave_pts = []
    for x in range(80, W - 80):
        u = (x - 80) / (W - 160)
        s1 = math.sin(u * 18.0) * 0.4
        s2 = math.sin(u * 18.8) * 0.35
        s3 = math.sin(u * 9.0) * 0.3
        y_val = 140 + (s1 + s2 + s3) * 45.0
        wave_pts.append((x, y_val))

    for i in range(len(wave_pts) - 1):
        draw.line([wave_pts[i], wave_pts[i+1]], fill=(167, 177, 255, 230), width=3)
        draw.line([(wave_pts[i][0], wave_pts[i][1] - 1), (wave_pts[i+1][0], wave_pts[i+1][1] - 1)], fill=(245, 248, 255, 255), width=1)

    # Knobs rack: Cutoff, Detune, Sub Level, Chorus, Drive
    knobs = [("CUTOFF", 0.65, SIGNAL), ("DETUNE", 0.55, EMBER), ("SUB LEVEL", 0.7, EMERALD), ("CHORUS", 0.6, SIGNAL), ("DRIVE", 0.65, EMBER)]
    spacing = (W - 160) / len(knobs)
    for idx, (label, val, col) in enumerate(knobs):
        kx = 80 + idx * spacing + spacing * 0.5
        ky = 370
        kr = 44
        draw.ellipse([kx - kr, ky - kr, kx + kr, ky + kr], fill=(36, 42, 70, 255), outline=(58, 68, 108, 255), width=2)
        # Value arc
        start_a = 135
        span_a = 270 * val
        draw.arc([kx - kr + 6, ky - kr + 6, kx + kr - 6, ky + kr - 6], start=start_a, end=start_a + span_a, fill=tuple(col.astype(int)) + (255,), width=4)
        # Indicator line
        rad_ind = math.radians(start_a + span_a)
        draw.line([(kx, ky), (kx + math.cos(rad_ind) * (kr - 10), ky + math.sin(rad_ind) * (kr - 10))], fill=(245, 248, 255, 255), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "liminal-drone-pad-synth.jpg"), quality=94)
    print("liminal-drone-pad-synth.jpg saved")

def render_ditherpunk_chiptune_lead_synth():
    """8-bit tracker synthesizer styling for Ditherpunk Chiptune Lead Synth."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    draw.rounded_rectangle([40, 40, W - 40, H - 40], radius=16, fill=(28, 33, 58, 255), outline=(48, 56, 92, 255), width=2)
    draw.rounded_rectangle([60, 60, W - 60, 220], radius=8, fill=(16, 19, 36, 255), outline=(36, 44, 76, 255), width=1)

    # 8-bit quantized staircase pulse waveform
    wave_pts = []
    for x in range(80, W - 80):
        u = (x - 80) / (W - 160)
        p = (u * 6.0) % 1.0
        raw = 0.8 if p > 0.35 else -0.8
        levels = 4.0
        crushed = math.floor(raw * levels + 0.5) / levels
        y_val = 140 + crushed * 50.0
        wave_pts.append((x, y_val))

    for i in range(len(wave_pts) - 1):
        draw.line([wave_pts[i], wave_pts[i+1]], fill=(78, 242, 184, 240), width=3)
        draw.line([(wave_pts[i][0], wave_pts[i][1] - 1), (wave_pts[i+1][0], wave_pts[i+1][1] - 1)], fill=(245, 248, 255, 255), width=1)

    knobs = [("PULSE WIDTH", 0.45, EMERALD), ("CRUSH BITS", 0.75, EMBER), ("GLIDE", 0.4, SIGNAL), ("DECAY", 0.65, EMERALD), ("ACCENT", 0.7, EMBER)]
    spacing = (W - 160) / len(knobs)
    for idx, (label, val, col) in enumerate(knobs):
        kx = 80 + idx * spacing + spacing * 0.5
        ky = 370
        kr = 44
        draw.ellipse([kx - kr, ky - kr, kx + kr, ky + kr], fill=(36, 42, 70, 255), outline=(58, 68, 108, 255), width=2)
        start_a = 135
        span_a = 270 * val
        draw.arc([kx - kr + 6, ky - kr + 6, kx + kr - 6, ky + kr - 6], start=start_a, end=start_a + span_a, fill=tuple(col.astype(int)) + (255,), width=4)
        rad_ind = math.radians(start_a + span_a)
        draw.line([(kx, ky), (kx + math.cos(rad_ind) * (kr - 10), ky + math.sin(rad_ind) * (kr - 10))], fill=(245, 248, 255, 255), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "ditherpunk-chiptune-lead-synth.jpg"), quality=94)
    print("ditherpunk-chiptune-lead-synth.jpg saved")

def render_chavetta_tape_saturator_fx():
    """Vintage analog reel-to-reel tape deck styling for Chavetta Tape Saturator FX."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    draw.rounded_rectangle([40, 40, W - 40, H - 40], radius=16, fill=(28, 33, 58, 255), outline=(48, 56, 92, 255), width=2)

    # Dual spinning tape reels
    for rx in (W * 0.32, W * 0.68):
        ry = 150
        rr = 75
        draw.ellipse([rx - rr, ry - rr, rx + rr, ry + rr], fill=(18, 22, 40, 255), outline=(58, 68, 108, 255), width=3)
        draw.ellipse([rx - 22, ry - 22, rx + 22, ry + 22], fill=(36, 42, 72, 255), outline=(167, 177, 255, 255), width=2)
        # 3 tape hub cutouts
        for k in range(3):
            a = k * (2.0 * math.pi / 3.0) + 0.4
            cx_hub = rx + math.cos(a) * 45
            cy_hub = ry + math.sin(a) * 45
            draw.ellipse([cx_hub - 12, cy_hub - 12, cx_hub + 12, cy_hub + 12], fill=(28, 33, 58, 255))

    # Tape path line
    draw.line([(W * 0.32, 225), (W * 0.5, 240), (W * 0.68, 225)], fill=(255, 107, 53, 220), width=4)

    knobs = [("DRIVE", 0.72, EMBER), ("HYSTERESIS", 0.6, EMBER), ("FLUTTER", 0.5, SIGNAL), ("TONE", 0.65, EMERALD), ("MIX", 0.85, SIGNAL)]
    spacing = (W - 160) / len(knobs)
    for idx, (label, val, col) in enumerate(knobs):
        kx = 80 + idx * spacing + spacing * 0.5
        ky = 380
        kr = 44
        draw.ellipse([kx - kr, ky - kr, kx + kr, ky + kr], fill=(36, 42, 70, 255), outline=(58, 68, 108, 255), width=2)
        start_a = 135
        span_a = 270 * val
        draw.arc([kx - kr + 6, ky - kr + 6, kx + kr - 6, ky + kr - 6], start=start_a, end=start_a + span_a, fill=tuple(col.astype(int)) + (255,), width=4)
        rad_ind = math.radians(start_a + span_a)
        draw.line([(kx, ky), (kx + math.cos(rad_ind) * (kr - 10), ky + math.sin(rad_ind) * (kr - 10))], fill=(245, 248, 255, 255), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "chavetta-tape-saturator-fx.jpg"), quality=94)
    print("chavetta-tape-saturator-fx.jpg saved")

def render_op_art_phaser_matrix_fx():
    """Kinetic optical phaser rack unit styling for Op Art Phaser Matrix FX."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    draw.rounded_rectangle([40, 40, W - 40, H - 40], radius=16, fill=(28, 33, 58, 255), outline=(48, 56, 92, 255), width=2)
    draw.rounded_rectangle([60, 60, W - 60, 220], radius=8, fill=(16, 19, 36, 255), outline=(36, 44, 76, 255), width=1)

    # 4-stage optical phase Lissajous curve
    cx_sc, cy_sc = W * 0.5, 140
    liss_pts = []
    steps = 240
    for s in range(steps + 1):
        t_phase = (s / steps) * 2.0 * math.pi
        lx = cx_sc + math.sin(t_phase * 3.0) * 160.0
        ly = cy_sc + math.sin(t_phase * 2.0 + 1.2) * 55.0
        liss_pts.append((lx, ly))

    for i in range(len(liss_pts) - 1):
        draw.line([liss_pts[i], liss_pts[i+1]], fill=(167, 177, 255, 220), width=2)

    knobs = [("RATE", 0.55, SIGNAL), ("DEPTH", 0.8, EMBER), ("FEEDBACK", 0.65, EMERALD), ("CENTER FREQ", 0.7, SIGNAL), ("MIX", 0.85, EMBER)]
    spacing = (W - 160) / len(knobs)
    for idx, (label, val, col) in enumerate(knobs):
        kx = 80 + idx * spacing + spacing * 0.5
        ky = 370
        kr = 44
        draw.ellipse([kx - kr, ky - kr, kx + kr, ky + kr], fill=(36, 42, 70, 255), outline=(58, 68, 108, 255), width=2)
        start_a = 135
        span_a = 270 * val
        draw.arc([kx - kr + 6, ky - kr + 6, kx + kr - 6, ky + kr - 6], start=start_a, end=start_a + span_a, fill=tuple(col.astype(int)) + (255,), width=4)
        rad_ind = math.radians(start_a + span_a)
        draw.line([(kx, ky), (kx + math.cos(rad_ind) * (kr - 10), ky + math.sin(rad_ind) * (kr - 10))], fill=(245, 248, 255, 255), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "op-art-phaser-matrix-fx.jpg"), quality=94)
    print("op-art-phaser-matrix-fx.jpg saved")

def render_ditherpunk_terminal_matrix():
    """1-bit Bayer dithered cybernetic terminal CRT raster preview."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    aspect = W / H

    y_indices, x_indices = np.indices((H, W))
    p0_x = ((x_indices / W) - 0.5) * aspect
    p0_y = ((y_indices / H) - 0.5)
    r2 = p0_x**2 + p0_y**2

    p_x = p0_x * (1.0 + 0.18 * r2)
    p_y = p0_y * (1.0 + 0.18 * r2)

    # Horizontal glitch shift
    bar_y = 0.48
    d_bar = np.abs(p_y - (bar_y - 0.5))
    shift_x = np.where(d_bar < 0.05, np.sin(p_y * 140.0) * 0.025, 0.0)
    p_x += shift_x

    r = np.sqrt(p_x**2 + p_y**2)
    ang = np.arctan2(p_y, p_x)

    sig1 = np.sin(r * 24.0 + ang * 6.0) * 0.5 + 0.5
    sig2 = np.exp(-((r - 0.35)**2) * 40.0) * (np.sin(ang * 8.0) * 0.5 + 0.5)
    lum = sig1 * 0.6 + sig2 * 0.5 + np.maximum(0.0, 0.35 - r) * 1.6

    bayer_scale = 48.0
    bx = (np.floor(((x_indices / W) * aspect + shift_x) * bayer_scale) % 4.0)
    by = (np.floor((y_indices / H) * bayer_scale) % 4.0)
    b_mat = ((bx * 4.0 + by * 9.0 + (bx % 2.0) * 6.0) % 16.0) / 16.0

    dith = np.where(lum > b_mat * 0.52, 1.0, 0.0)
    scan = np.sin(p_y * 280.0 * 2.0 * math.pi) * 0.2 + 0.8

    vignette = np.clip(1.0 - r2 * 1.6, 0.0, 1.0)

    for y in range(H):
        for x in range(W):
            d = dith[y, x]
            s = scan[y, x]
            s2 = sig2[y, x]
            c_green = EMERALD * d * 1.35 * s
            c_amber = EMBER * d * 1.35 * s
            t_col = c_green * (1.0 - s2) + c_amber * s2
            v = vignette[y, x]
            col = GROUND * (1.0 - v) + t_col * v
            out[y, x] = np.clip(col, 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "ditherpunk-terminal-matrix.jpg"), quality=94)
    print("ditherpunk-terminal-matrix.jpg saved")

def render_liminal_void_haze():
    """Volumetric brutalist liminal corridor haze preview."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    aspect = W / H

    y_indices, x_indices = np.indices((H, W))
    p_x = ((x_indices / W) - 0.5) * aspect
    p_y = ((y_indices / H) - 0.5)

    r = np.sqrt(p_x**2 + p_y**2)
    z_depth = 1.0 / np.maximum(0.02, r)

    box_dist = np.maximum(np.abs(p_x * z_depth) - 1.2, np.abs(p_y * z_depth) - 0.9)
    edge_glow = np.exp(-np.abs(box_dist) * 10.0)

    floor_y = -0.32
    floor_strip = np.exp(-((p_y - floor_y)**2) * 80.0) * np.exp(-np.abs(p_x) * 4.0)

    ceil_y = 0.35
    ceil_strip = np.exp(-((p_y - ceil_y)**2) * 80.0) * np.exp(-np.abs(p_x) * 4.0)

    fog = np.clip((z_depth - 1.0) * 0.12 * 1.25, 0.0, 1.0)
    caustic = np.sin(p_x * 14.0) * np.sin(p_y * 14.0) * 0.5 + 0.5

    for y in range(H):
        for x in range(W):
            fg = fog[y, x]
            cst = caustic[y, x]
            eg = edge_glow[y, x]
            fs = floor_strip[y, x]
            cs = ceil_strip[y, x]

            haze = GROUND * (1.0 - fg) + EMERALD * (0.8 + cst * 0.4) * fg
            illum = SIGNAL * (eg * 1.8 + cs * 1.6) + EMBER * (fs * 2.2)
            col = haze + illum
            out[y, x] = np.clip(col, 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "liminal-void-haze.jpg"), quality=94)
    print("liminal-void-haze.jpg saved")

def main():
    render_ditherpunk_halftone_grid()
    render_technical_blueprint_schematic()
    render_liminal_void_corridor()
    render_retro_surreal_monolith()
    render_liminal_drone_pad_synth()
    render_ditherpunk_chiptune_lead_synth()
    render_chavetta_tape_saturator_fx()
    render_op_art_phaser_matrix_fx()
    render_ditherpunk_terminal_matrix()
    render_liminal_void_haze()

    # Also render template webp previews using template_preview.py
    sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
    import template_preview
    for t_slug in ("liminal-drone-pad-synth", "ditherpunk-chiptune-lead-synth", "chavetta-tape-saturator-fx", "op-art-phaser-matrix-fx"):
        inf_path = os.path.join(TEMPLATES_DIR, t_slug + ".inf")
        webp_path = os.path.join(TEMPLATES_DIR, t_slug + ".webp")
        template_preview.render(inf_path, webp_path)
        print(f"{t_slug}.webp saved")

    print("All Pack 12 previews successfully generated!")

if __name__ == "__main__":
    main()
