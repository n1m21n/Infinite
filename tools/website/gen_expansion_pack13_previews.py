#!/usr/bin/env python3
"""Generate high-fidelity preview images for the 10 new creative library items (Pack 13).
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
MAGENTA = np.array([255, 56, 168], dtype=np.float32)

W, H = 900, 600

def render_y2k_acid_starburst():
    """Y2K Cyber-Acid spiked starburst with iridescent chrome rings and rave telemetry."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5
    min_dim = min(W, H)

    # 1. Concentric holographic orbit rings & HUD ticks
    num_rings = 4
    for r in range(num_rings):
        rad = min_dim * (0.16 + r * 0.09)
        alpha = int(60 + 35 * math.sin(r * 1.5))
        draw.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], outline=(167, 177, 255, alpha), width=1)
        num_ticks = 16 + r * 8
        for k in range(num_ticks):
            a_tick = (k / num_ticks) * math.pi * 2.0 + (0.15 if r % 2 == 0 else -0.15)
            x1 = cx + math.cos(a_tick) * rad
            y1 = cy + math.sin(a_tick) * rad
            tick_len = 6 if k % 4 == 0 else 3
            x2 = cx + math.cos(a_tick) * (rad + tick_len)
            y2 = cy + math.sin(a_tick) * (rad + tick_len)
            draw.line([(x1, y1), (x2, y2)], fill=(245, 248, 255, alpha + 40), width=1)

    # 2. Spiked starburst shells
    num_stars = 7
    spikes = 6
    for s in range(num_stars - 1, -1, -1):
        rad_outer = min_dim * (0.13 + s * 0.042)
        rad_inner = rad_outer * 0.32
        rot = s * 0.38 * (1 if s % 2 == 0 else -1)

        pts = []
        count = spikes * 2
        for i in range(count):
            a = (i * math.pi * 2.0) / count + rot
            r_dist = rad_outer if i % 2 == 0 else rad_inner
            pts.append((cx + math.cos(a) * r_dist, cy + math.sin(a) * r_dist))

        if s % 3 == 0:
            stroke_col = (255, 56, 168, 235)  # Acid magenta
            fill_col = (255, 107, 53, 35)      # Ember wash
        elif s % 3 == 1:
            stroke_col = (78, 242, 184, 230)  # Emerald
            fill_col = (78, 242, 184, 30)
        else:
            stroke_col = (167, 177, 255, 240) # Signal
            fill_col = (167, 177, 255, 25)

        draw.polygon(pts, fill=fill_col, outline=stroke_col)

        # Star tip node pearls
        if s == num_stars - 1 or s == 3:
            for i in range(spikes):
                a = (i * 2 * math.pi * 2.0) / count + rot
                px = cx + math.cos(a) * rad_outer
                py = cy + math.sin(a) * rad_outer
                draw.ellipse([px - 3, py - 3, px + 3, py + 3], fill=(245, 248, 255, 240))

    # 3. Glowing chrome nucleus
    draw.ellipse([cx - 24, cy - 24, cx + 24, cy + 24], fill=(78, 242, 184, 110))
    draw.ellipse([cx - 12, cy - 12, cx + 12, cy + 12], fill=(245, 248, 255, 250))

    im.save(os.path.join(PREVIEWS_DIR, "y2k-acid-starburst.jpg"), quality=94)
    print("y2k-acid-starburst.jpg saved")

def render_neo_brutalist_grid_matrix():
    """Swiss neo-brutalist architectural poster layout with asymmetric modular grids and hazard hatchings."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    margin = 44.0
    inner_w = W - margin * 2
    inner_h = H - margin * 2
    cols, rows = 6, 4
    cell_w = inner_w / cols
    cell_h = inner_h / rows

    # 1. Framing border with corner brackets
    draw.rectangle([margin, margin, W - margin, H - margin], outline=(245, 248, 255, 220), width=3)
    c_len = 28.0
    draw.line([(margin - 8, margin), (margin + c_len, margin)], fill=(255, 107, 53, 255), width=3)
    draw.line([(margin, margin - 8), (margin, margin + c_len)], fill=(255, 107, 53, 255), width=3)
    draw.line([(W - margin + 8, margin), (W - margin - c_len, margin)], fill=(255, 107, 53, 255), width=3)
    draw.line([(W - margin, margin - 8), (W - margin, margin + c_len)], fill=(255, 107, 53, 255), width=3)
    draw.line([(margin - 8, H - margin), (margin + c_len, H - margin)], fill=(255, 107, 53, 255), width=3)
    draw.line([(margin, H - margin + 8), (margin, H - margin - c_len)], fill=(255, 107, 53, 255), width=3)
    draw.line([(W - margin + 8, H - margin), (W - margin - c_len, H - margin)], fill=(255, 107, 53, 255), width=3)
    draw.line([(W - margin, H - margin + 8), (W - margin, H - margin - c_len)], fill=(255, 107, 53, 255), width=3)

    # 2. Grid cells
    for c in range(cols):
        for r in range(rows):
            x = margin + c * cell_w
            y = margin + r * cell_h
            draw.rectangle([x, y, x + cell_w, y + cell_h], outline=(167, 177, 255, 60), width=1)

            # Hero block at c=3, r=1
            if c == 3 and r == 1:
                draw.rectangle([x + 4, y + 4, x + cell_w - 4, y + cell_h - 4], fill=(255, 107, 53, 240))
                draw.line([(x + cell_w * 0.5, y + 10), (x + cell_w * 0.5, y + cell_h - 10)], fill=(21, 25, 48, 255), width=3)
                draw.line([(x + 10, y + cell_h * 0.5), (x + cell_w - 10, y + cell_h * 0.5)], fill=(21, 25, 48, 255), width=3)
            # Diagonal hazard hatch block
            elif (c == 1 and r == 2) or (c == 4 and r == 0):
                step = 10.0
                for h in np.arange(-cell_h, cell_w + cell_h, step):
                    x1 = max(x, x + h)
                    y1 = max(y, y - h)
                    x2 = min(x + cell_w, x + h + cell_h)
                    y2 = min(y + cell_h, y + h + cell_h)
                    draw.line([(x1, y1), (x2, y2)], fill=(245, 248, 255, 180), width=2)
            # Concrete tint block with drafting target
            elif (c == 0 and r == 0) or (c == 5 and r == 2) or (c == 2 and r == 3):
                draw.rectangle([x + 3, y + 3, x + cell_w - 3, y + cell_h - 3], fill=(36, 44, 76, 200))
                cx_c = x + cell_w * 0.5
                cy_c = y + cell_h * 0.5
                rad_t = min(cell_w, cell_h) * 0.28
                draw.ellipse([cx_c - rad_t, cy_c - rad_t, cx_c + rad_t, cy_c + rad_t], outline=(78, 242, 184, 220), width=2)
                draw.ellipse([cx_c - 4, cy_c - 4, cx_c + 4, cy_c + 4], fill=(78, 242, 184, 240))

    # 3. Typography registration marks
    draw.rectangle([margin + 16, H - margin + 12, margin + 120, H - margin + 16], fill=(245, 248, 255, 220))
    draw.rectangle([margin + 140, H - margin + 12, margin + 180, H - margin + 16], fill=(245, 248, 255, 220))
    draw.rectangle([margin + 195, H - margin + 12, margin + 215, H - margin + 16], fill=(245, 248, 255, 220))

    im.save(os.path.join(PREVIEWS_DIR, "neo-brutalist-grid-matrix.jpg"), quality=94)
    print("neo-brutalist-grid-matrix.jpg saved")

def render_y2k_chrome_torus_orbit_3d():
    """Interlocking liquid-chrome ribbed tori with orbiting spiked satellites."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5
    min_dim = min(W, H)

    # 1. Background radial chrome illumination aura
    num_auras = 10
    for a in range(num_auras, 0, -1):
        rad_a = min_dim * (0.2 + a * 0.035)
        alpha = int(12 + (10 - a) * 3)
        draw.ellipse([cx - rad_a, cy - rad_a, cx + rad_a, cy + rad_a], fill=(167, 177, 255, alpha))

    # 2. Outer chrome torus ribbon
    R = min_dim * 0.36
    r = min_dim * 0.08
    num_pts = 36
    for i in range(num_pts):
        a1 = (i / num_pts) * math.pi * 2.0
        a2 = ((i + 1) / num_pts) * math.pi * 2.0

        x1 = cx + math.cos(a1) * R
        y1 = cy + math.sin(a1) * (R * 0.45)
        x2 = cx + math.cos(a2) * R
        y2 = cy + math.sin(a2) * (R * 0.45)

        sheen = math.sin(a1 * 3.0) * 0.5 + 0.5
        col = (int(167 + 80 * sheen), int(177 + 75 * sheen), 255, 240)
        draw.line([(x1, y1), (x2, y2)], fill=col, width=12)

        # Metallic ribbed beads
        bead_r = 8.0
        draw.ellipse([x1 - bead_r, y1 - bead_r, x1 + bead_r, y1 + bead_r], fill=(245, 248, 255, 250))

    # 3. Inner counter-tilted acid rim ring
    R_in = R * 0.62
    for i in range(num_pts):
        a1 = (i / num_pts) * math.pi * 2.0
        a2 = ((i + 1) / num_pts) * math.pi * 2.0

        x1 = cx + math.cos(a1) * (R_in * 0.5) - math.sin(a1) * (R_in * 0.8)
        y1 = cy + math.cos(a1) * (R_in * 0.8) + math.sin(a1) * (R_in * 0.5) * 0.35
        x2 = cx + math.cos(a2) * (R_in * 0.5) - math.sin(a2) * (R_in * 0.8)
        y2 = cy + math.cos(a2) * (R_in * 0.8) + math.sin(a2) * (R_in * 0.5) * 0.35

        col_in = (255, 107, 53, 230) if i % 2 == 0 else (78, 242, 184, 230)
        draw.line([(x1, y1), (x2, y2)], fill=col_in, width=7)

    # 4. Spiked star satellite orbits
    num_sats = 8
    for k in range(num_sats):
        a_sat = (k / num_sats) * math.pi * 2.0 + 0.4
        orb_r = R * 1.3
        sx = cx + math.cos(a_sat) * orb_r
        sy = cy + math.sin(a_sat) * (orb_r * 0.4)

        # Crossed 3D diamond star
        star_w = 18.0
        draw.line([(sx - star_w, sy), (sx + star_w, sy)], fill=(245, 248, 255, 240), width=3)
        draw.line([(sx, sy - star_w), (sx, sy + star_w)], fill=(245, 248, 255, 240), width=3)
        draw.line([(sx - star_w * 0.7, sy - star_w * 0.7), (sx + star_w * 0.7, sy + star_w * 0.7)], fill=(167, 177, 255, 200), width=2)
        draw.line([(sx - star_w * 0.7, sy + star_w * 0.7), (sx + star_w * 0.7, sy - star_w * 0.7)], fill=(167, 177, 255, 200), width=2)

    # 5. Central liquid chrome core
    draw.ellipse([cx - 40, cy - 40, cx + 40, cy + 40], fill=(55, 65, 110, 255), outline=(167, 177, 255, 255), width=3)
    draw.ellipse([cx - 28, cy - 28, cx + 28, cy + 28], fill=(167, 177, 255, 240))
    draw.ellipse([cx - 14, cy - 20, cx, cy - 6], fill=(245, 248, 255, 255))

    im.save(os.path.join(PREVIEWS_DIR, "y2k-chrome-torus-orbit-3d.jpg"), quality=94)
    print("y2k-chrome-torus-orbit-3d.jpg saved")

def render_ukiyoe_cloud_pagoda_3d():
    """Multi-tiered Japanese architectural pagoda with floating woodblock cloud ribbons."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5 + 40
    min_dim = min(W, H)

    # 1. Foundation plinth
    plinth_w = 260.0
    draw.polygon([(cx - plinth_w * 0.5, cy + 130), (cx + plinth_w * 0.5, cy + 130),
                  (cx + plinth_w * 0.42, cy + 155), (cx - plinth_w * 0.42, cy + 155)], fill=(32, 38, 65, 255), outline=(58, 68, 108, 255))

    # 2. Pagoda tiers (5 tiers)
    tiers = 5
    for t in range(tiers):
        u = t / tiers
        tier_y = cy + 110 - t * 58
        scale = 1.0 - u * 0.38

        # Chamber box
        ch_w = 70.0 * scale
        ch_h = 32.0
        draw.rectangle([cx - ch_w * 0.5, tier_y - ch_h, cx + ch_w * 0.5, tier_y], fill=(42, 32, 40, 255), outline=(78, 56, 70, 255))

        # Vermilion bracket rafter layer
        br_w = 95.0 * scale
        draw.rectangle([cx - br_w * 0.5, tier_y - ch_h - 6, cx + br_w * 0.5, tier_y - ch_h], fill=(255, 107, 53, 240))

        # Flared curved eave roof
        rf_w = 175.0 * scale
        rf_pts = [
            (cx - rf_w * 0.5, tier_y - ch_h - 10),
            (cx - rf_w * 0.44, tier_y - ch_h - 22),
            (cx + rf_w * 0.44, tier_y - ch_h - 22),
            (cx + rf_w * 0.5, tier_y - ch_h - 10),
            (cx, tier_y - ch_h - 26)
        ]
        draw.polygon(rf_pts, fill=(35, 48, 88, 255), outline=(167, 177, 255, 230))

        # Ceramic ridge caps
        draw.line([(cx - rf_w * 0.5, tier_y - ch_h - 10), (cx, tier_y - ch_h - 26)], fill=(167, 177, 255, 240), width=2)
        draw.line([(cx + rf_w * 0.5, tier_y - ch_h - 10), (cx, tier_y - ch_h - 26)], fill=(167, 177, 255, 240), width=2)

    # 3. Gilded Sorin Spire Mast
    top_y = cy + 110 - tiers * 58 - 20
    draw.line([(cx, top_y), (cx, top_y - 70)], fill=(245, 248, 255, 255), width=3)
    for r in range(7):
        ry = top_y - 12 - r * 7
        rw = 14 - r * 1.2
        draw.ellipse([cx - rw, ry - 3, cx + rw, ry + 3], fill=(255, 107, 53, 240))

    # 4. Floating Edo woodblock cloud ribbons (Kumogata)
    clouds = [
        (cx - 180, cy - 40, 65),
        (cx + 170, cy + 20, 75),
        (cx - 140, cy + 70, 55),
        (cx + 150, cy - 100, 60)
    ]
    for cl_x, cl_y, cl_r in clouds:
        draw.ellipse([cl_x - cl_r, cl_y - cl_r * 0.4, cl_x + cl_r, cl_y + cl_r * 0.4], fill=(78, 242, 184, 140))
        draw.ellipse([cl_x - cl_r * 0.5, cl_y - cl_r * 0.55, cl_x + cl_r * 0.5, cl_y + cl_r * 0.25], fill=(245, 248, 255, 180))
        draw.ellipse([cl_x + cl_r * 0.2, cl_y - cl_r * 0.4, cl_x + cl_r * 0.8, cl_y + cl_r * 0.3], fill=(167, 177, 255, 160))

    im.save(os.path.join(PREVIEWS_DIR, "ukiyoe-cloud-pagoda-3d.jpg"), quality=94)
    print("ukiyoe-cloud-pagoda-3d.jpg saved")

def render_acid_bass_303_lead_synth():
    """Hardware rack synth styling for Acid Bass 303 Lead Synth."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    draw.rounded_rectangle([40, 40, W - 40, H - 40], radius=16, fill=(28, 33, 58, 255), outline=(48, 56, 92, 255), width=2)
    draw.rounded_rectangle([60, 60, W - 60, 220], radius=8, fill=(16, 19, 36, 255), outline=(36, 44, 76, 255), width=1)

    # 303 Resonant filter squelch sweep display
    wave_pts = []
    for x in range(80, W - 80):
        u = (x - 80) / (W - 160)
        p = (u * 4.0) % 1.0
        saw = (p * 2.0 - 1.0)
        env = math.exp(-p * 3.5)
        res_osc = math.sin(p * 45.0) * env * 0.7
        y_val = 140 + (saw * 0.35 + res_osc) * 55.0
        wave_pts.append((x, y_val))

    for i in range(len(wave_pts) - 1):
        draw.line([wave_pts[i], wave_pts[i+1]], fill=(255, 56, 168, 240), width=3)
        draw.line([(wave_pts[i][0], wave_pts[i][1] - 1), (wave_pts[i+1][0], wave_pts[i+1][1] - 1)], fill=(245, 248, 255, 255), width=1)

    # Knobs: Cutoff, Resonance, Env Mod, Decay, Accent
    knobs = [("CUTOFF", 0.65, MAGENTA), ("RESONANCE", 0.82, EMBER), ("ENV MOD", 0.75, SIGNAL), ("DECAY", 0.42, EMERALD), ("ACCENT", 0.7, EMBER)]
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

    im.save(os.path.join(PREVIEWS_DIR, "acid-bass-303-lead-synth.jpg"), quality=94)
    print("acid-bass-303-lead-synth.jpg saved")

def render_brutalist_sub_bass_rumble_synth():
    """Monolithic sub-bass styling for Brutalist Sub-Bass Rumble Synth."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    draw.rounded_rectangle([40, 40, W - 40, H - 40], radius=16, fill=(28, 33, 58, 255), outline=(48, 56, 92, 255), width=2)
    draw.rounded_rectangle([60, 60, W - 60, 220], radius=8, fill=(16, 19, 36, 255), outline=(36, 44, 76, 255), width=1)

    # Low frequency seismic wavefolded waveform
    wave_pts = []
    for x in range(80, W - 80):
        u = (x - 80) / (W - 160)
        sin_core = math.sin(u * 12.0)
        sub1 = math.sin(u * 6.0) * 0.7
        sub2 = 0.5 if (u * 3.0) % 1.0 > 0.5 else -0.5
        folded = math.sin((sin_core + sub1 + sub2) * 2.2)
        y_val = 140 + folded * 50.0
        wave_pts.append((x, y_val))

    for i in range(len(wave_pts) - 1):
        draw.line([wave_pts[i], wave_pts[i+1]], fill=(167, 177, 255, 240), width=3)
        draw.line([(wave_pts[i][0], wave_pts[i][1] - 1), (wave_pts[i+1][0], wave_pts[i+1][1] - 1)], fill=(245, 248, 255, 255), width=1)

    knobs = [("SUB MIX", 0.65, EMBER), ("FOLD DRIVE", 0.75, SIGNAL), ("GLIDE", 0.35, EMERALD), ("DECAY", 0.8, SIGNAL), ("BODY TONE", 0.45, EMBER)]
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

    im.save(os.path.join(PREVIEWS_DIR, "brutalist-sub-bass-rumble-synth.jpg"), quality=94)
    print("brutalist-sub-bass-rumble-synth.jpg saved")

def render_glitch_bitcrush_buffer_stutter_fx():
    """Studio rack styling for Glitch Bitcrush Buffer Stutter FX."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    draw.rounded_rectangle([40, 40, W - 40, H - 40], radius=16, fill=(28, 33, 58, 255), outline=(48, 56, 92, 255), width=2)
    draw.rounded_rectangle([60, 60, W - 60, 220], radius=8, fill=(16, 19, 36, 255), outline=(36, 44, 76, 255), width=1)

    # Bitcrushed stutter waveform
    wave_pts = []
    for x in range(80, W - 80):
        u = (x - 80) / (W - 160)
        p = (u * 14.0) % 1.0
        sin_val = math.sin(u * 16.0)
        if (int(u * 6.0) % 2 == 1):
            # Glitch repeat block
            crush_levels = 4.0
            val = math.floor(sin_val * crush_levels) / crush_levels
        else:
            val = sin_val
        y_val = 140 + val * 52.0
        wave_pts.append((x, y_val))

    for i in range(len(wave_pts) - 1):
        draw.line([wave_pts[i], wave_pts[i+1]], fill=(78, 242, 184, 240), width=3)
        draw.line([(wave_pts[i][0], wave_pts[i][1] - 1), (wave_pts[i+1][0], wave_pts[i+1][1] - 1)], fill=(245, 248, 255, 255), width=1)

    knobs = [("BITS", 0.6, EMERALD), ("DOWNSAMPLE", 0.65, EMBER), ("STUTTER", 0.7, SIGNAL), ("JITTER", 0.45, EMERALD), ("MIX", 0.85, EMBER)]
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

    im.save(os.path.join(PREVIEWS_DIR, "glitch-bitcrush-buffer-stutter-fx.jpg"), quality=94)
    print("glitch-bitcrush-buffer-stutter-fx.jpg saved")

def render_liquid_chorus_flanger_matrix_fx():
    """Liquid multi-phase delay matrix styling for Liquid Chorus Flanger Matrix FX."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    draw.rounded_rectangle([40, 40, W - 40, H - 40], radius=16, fill=(28, 33, 58, 255), outline=(48, 56, 92, 255), width=2)
    draw.rounded_rectangle([60, 60, W - 60, 220], radius=8, fill=(16, 19, 36, 255), outline=(36, 44, 76, 255), width=1)

    # Dual-phase sinusoidal lissajous sweep
    wave1, wave2 = [], []
    for x in range(80, W - 80):
        u = (x - 80) / (W - 160)
        s1 = math.sin(u * 22.0) * math.cos(u * 4.0) * 45.0
        s2 = math.cos(u * 22.0 + 1.2) * math.sin(u * 4.0) * 45.0
        wave1.append((x, 140 + s1))
        wave2.append((x, 140 + s2))

    for i in range(len(wave1) - 1):
        draw.line([wave1[i], wave1[i+1]], fill=(167, 177, 255, 220), width=3)
        draw.line([wave2[i], wave2[i+1]], fill=(255, 107, 53, 220), width=3)

    knobs = [("RATE", 0.55, SIGNAL), ("DEPTH", 0.65, EMERALD), ("FEEDBACK", 0.58, EMBER), ("DISPERSION", 0.4, SIGNAL), ("MIX", 0.85, EMERALD)]
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

    im.save(os.path.join(PREVIEWS_DIR, "liquid-chorus-flanger-matrix-fx.jpg"), quality=94)
    print("liquid-chorus-flanger-matrix-fx.jpg saved")

def render_y2k_liquid_chrome_rave():
    """Iridescent liquid chrome metaballs with spiked star flares preview."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    aspect = W / H

    y_indices, x_indices = np.indices((H, W))
    p_x = ((x_indices / W) - 0.5) * aspect
    p_y = ((y_indices / H) - 0.5)

    r = np.sqrt(p_x**2 + p_y**2)
    ang = np.arctan2(p_y, p_x)

    # Ripple wave
    wave = np.sin(r * 28.0 - 1.2) * 0.02
    p_x2 = p_x + p_x * wave
    p_y2 = p_y + p_y * wave

    # Metaballs
    d1 = np.sqrt((p_x2 - 0.16)**2 + (p_y2 - 0.1)**2) - 0.28
    d2 = np.sqrt((p_x2 + 0.18)**2 + (p_y2 - 0.14)**2) - 0.22
    d3 = np.sqrt((p_x2 - 0.05)**2 + (p_y2 + 0.18)**2) - 0.19

    h1 = np.clip(0.5 + 0.5 * (d2 - d1) / 0.18, 0.0, 1.0)
    mb1 = d2 * (1.0 - h1) + d1 * h1 - 0.18 * h1 * (1.0 - h1)

    h2 = np.clip(0.5 + 0.5 * (d3 - mb1) / 0.18, 0.0, 1.0)
    mb = d3 * (1.0 - h2) + mb1 * h2 - 0.18 * h2 * (1.0 - h2)

    surface = np.clip(1.0 - mb * 8.0, 0.0, 1.0)
    edge_glow = np.clip(1.0 - np.abs(mb) * 12.0, 0.0, 1.0) ** 3.0

    star = (np.abs(np.cos(ang * 6.0)) ** 16.0) * np.exp(-r * 4.0)

    fresnel = (surface ** 2.5) * 1.8
    vignette = np.clip(1.0 - (p_x**2 + p_y**2) * 1.4, 0.0, 1.0)

    for y in range(H):
        for x in range(W):
            sf = surface[y, x]
            eg = edge_glow[y, x]
            fr = fresnel[y, x]
            st = star[y, x]
            vg = vignette[y, x]

            c_chrome = SIGNAL * (fr + eg * 1.4)
            c_neon = MAGENTA * (eg * 1.5) + EMERALD * (st * 2.0)
            col = GROUND * (1.0 - sf - st * 0.8) + (c_chrome + c_neon) * (sf + st * 0.8)
            out[y, x] = np.clip(col * vg, 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "y2k-liquid-chrome-rave.jpg"), quality=94)
    print("y2k-liquid-chrome-rave.jpg saved")

def render_neo_brutalist_concrete_monolith():
    """Stark Swiss architectural concrete facade preview."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    aspect = W / H

    y_indices, x_indices = np.indices((H, W))
    p_x = ((x_indices / W) - 0.5) * aspect
    p_y = ((y_indices / H) - 0.5)

    slab_scale = 18.0
    grid_x = np.floor(p_x * slab_scale)
    grid_y = np.floor(p_y * slab_scale)

    elevation = np.sin(grid_x * 2.3 + grid_y * 4.7) * 0.5 + 0.5
    fract_x = (p_x * slab_scale) % 1.0 - 0.5
    fract_y = (p_y * slab_scale) % 1.0 - 0.5

    dist_border = np.maximum(np.abs(fract_x), np.abs(fract_y))
    groove = np.clip((dist_border - 0.42) / 0.06, 0.0, 1.0)

    shadow = np.clip((fract_x * 0.7071 + fract_y * 0.7071) * 1.4 * 1.6 + 0.5, 0.0, 1.0)

    slot_active = np.where((np.abs(grid_x) < 3.0) & ((grid_y % 3.0) < 1.0), 1.0, 0.0)
    slot_glow = np.exp(-np.abs(fract_y) * 12.0) * slot_active * 1.2

    vignette = np.clip(1.0 - (p_x**2 + p_y**2) * 1.2, 0.0, 1.0)

    c_concrete = np.array([61, 71, 107], dtype=np.float32)

    for y in range(H):
        for x in range(W):
            el = elevation[y, x]
            sh = shadow[y, x]
            gr = groove[y, x]
            sg = slot_glow[y, x]
            vg = vignette[y, x]
            gx = grid_x[y, x]

            base_conc = c_concrete * (0.8 + el * 0.4) * sh
            slab_base = base_conc * (1.0 - gr) + GROUND * gr
            c_sig = SIGNAL * (sg * 1.5)
            c_emb = EMBER * (sg * 0.8 if gx == 0.0 else 0.0)

            col = (slab_base + c_sig + c_emb) * vg
            out[y, x] = np.clip(col, 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "neo-brutalist-concrete-monolith.jpg"), quality=94)
    print("neo-brutalist-concrete-monolith.jpg saved")

def main():
    render_y2k_acid_starburst()
    render_neo_brutalist_grid_matrix()
    render_y2k_chrome_torus_orbit_3d()
    render_ukiyoe_cloud_pagoda_3d()
    render_acid_bass_303_lead_synth()
    render_brutalist_sub-bass-rumble-synth() if False else None  # syntax safety
    render_brutalist_sub_bass_rumble_synth()
    render_glitch_bitcrush_buffer_stutter_fx()
    render_liquid_chorus_flanger_matrix_fx()
    render_y2k_liquid_chrome_rave()
    render_neo_brutalist_concrete_monolith()

    # Also render template webp previews using template_preview.py
    sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
    import template_preview
    for t_slug in ("acid-bass-303-lead-synth", "brutalist-sub-bass-rumble-synth", "glitch-bitcrush-buffer-stutter-fx", "liquid-chorus-flanger-matrix-fx"):
        inf_path = os.path.join(TEMPLATES_DIR, t_slug + ".inf")
        webp_path = os.path.join(TEMPLATES_DIR, t_slug + ".webp")
        template_preview.render(inf_path, webp_path)
        print(f"{t_slug}.webp saved")

    print("All Pack 13 previews successfully generated!")

if __name__ == "__main__":
    main()
