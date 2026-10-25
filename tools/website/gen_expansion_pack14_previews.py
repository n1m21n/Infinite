#!/usr/bin/env python3
"""Generate high-fidelity preview images for the 10 new creative library items (Pack 14).
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
PHOSPHOR = np.array([52, 235, 147], dtype=np.float32) # CRT Phosphor green
CYAN = np.array([56, 189, 248], dtype=np.float32)     # Vector cyan

W, H = 900, 600

def render_analog_crt_vector_oscilloscope():
    """Dual-beam CRT vector oscilloscope with Lissajous harmonic curves and calibration graticule."""
    im = Image.new("RGB", (W, H), (14, 18, 30))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5
    min_dim = min(W, H)

    # 1. Subtle green phosphor CRT glass aura
    draw.ellipse([cx - min_dim * 0.46, cy - min_dim * 0.46, cx + min_dim * 0.46, cy + min_dim * 0.46],
                 fill=(22, 42, 38, 90))

    # 2. Calibration graticule grid
    grid_w = min_dim * 0.78
    grid_h = min_dim * 0.78
    x0, y0 = cx - grid_w * 0.5, cy - grid_h * 0.5
    divs = 8
    draw.rectangle([x0, y0, x0 + grid_w, y0 + grid_h], outline=(52, 235, 147, 75), width=2)

    step_x = grid_w / divs
    step_y = grid_h / divs
    for i in range(1, divs):
        gx = x0 + i * step_x
        gy = y0 + i * step_y
        draw.line([(gx, y0), (gx, y0 + grid_h)], fill=(52, 235, 147, 40), width=1)
        draw.line([(x0, gy), (x0 + grid_w, gy)], fill=(52, 235, 147, 40), width=1)
        # Axis subdivision ticks
        draw.line([(gx, cy - 5), (gx, cy + 5)], fill=(52, 235, 147, 130), width=1)
        draw.line([(cx - 5, gy), (cx + 5, gy)], fill=(52, 235, 147, 130), width=1)

    # Graticule rings
    draw.ellipse([cx - min_dim * 0.22, cy - min_dim * 0.22, cx + min_dim * 0.22, cy + min_dim * 0.22],
                 outline=(52, 235, 147, 50), width=1)
    draw.ellipse([cx - min_dim * 0.38, cy - min_dim * 0.38, cx + min_dim * 0.38, cy + min_dim * 0.38],
                 outline=(52, 235, 147, 50), width=1)

    # 3. Lissajous vector curves (multi-pass glow)
    steps = 480
    rx = min_dim * 0.33
    ry = min_dim * 0.33
    freq_x, freq_y = 3.0, 4.0
    phase = 0.65

    pts = []
    for i in range(steps + 1):
        u = (i / steps) * math.pi * 2.0
        px = cx + math.cos(u * freq_x + phase) * rx
        py = cy + math.sin(u * freq_y) * ry
        pts.append((px, py))

    # Outer diffuse bloom
    draw.line(pts, fill=(52, 235, 147, 55), width=8)
    # Secondary cyan halo
    draw.line(pts, fill=(56, 189, 248, 140), width=4)
    # Intense sharp beam core
    draw.line(pts, fill=(245, 255, 250, 245), width=2)

    # Scanning electron spot
    spot_u = 0.82 * math.pi * 2.0
    sx = cx + math.cos(spot_u * freq_x + phase) * rx
    sy = cy + math.sin(spot_u * freq_y) * ry
    draw.ellipse([sx - 12, sy - 12, sx + 12, sy + 12], fill=(52, 235, 147, 120))
    draw.ellipse([sx - 5, sy - 5, sx + 5, sy + 5], fill=(255, 255, 255, 255))

    im.save(os.path.join(PREVIEWS_DIR, "analog-crt-vector-oscilloscope.jpg"), quality=94)
    print("analog-crt-vector-oscilloscope.jpg saved")

def render_op_art_kinetic_wave_moire():
    """Bridget Riley inspired optical illusion with undulating moire interference wave lines."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5
    min_dim = min(W, H)

    num_lines = 40
    step_y = H / (num_lines + 1)
    pts_per_line = 180
    step_x = W / pts_per_line

    for l in range(num_lines):
        base_norm = (l + 1) / (num_lines + 1)
        base_y = base_norm * H
        waist = math.sin(base_norm * math.pi)
        amp = 34.0 * (0.35 + 0.65 * waist)

        pts1 = []
        pts2 = []
        for i in range(pts_per_line + 1):
            x = i * step_x
            u = x / W
            k = u * 4.0 * math.pi * 2.0
            y1 = base_y + math.sin(k + base_norm * 3.5) * amp + math.sin(k * 2.0) * (amp * 0.25)
            y2 = base_y + math.sin(-k * 1.5 - base_norm * 4.2) * (amp * 0.65)
            pts1.append((x, y1))
            pts2.append((x, y2))

        # Alternating high contrast optical lines
        draw.line(pts1, fill=(245, 248, 255, 235), width=2)
        if l % 2 == 0:
            draw.line(pts2, fill=(167, 177, 255, 160), width=1)

    # Framing focus disc
    draw.ellipse([cx - min_dim * 0.26, cy - min_dim * 0.26, cx + min_dim * 0.26, cy + min_dim * 0.26],
                 outline=(255, 107, 53, 190), width=3)
    draw.ellipse([cx - min_dim * 0.275, cy - min_dim * 0.275, cx + min_dim * 0.275, cy + min_dim * 0.275],
                 outline=(167, 177, 255, 120), width=1)

    im.save(os.path.join(PREVIEWS_DIR, "op-art-kinetic-wave-moire.jpg"), quality=94)
    print("op-art-kinetic-wave-moire.jpg saved")

def render_analog_crt_wireframe_terrain_3d():
    """Retro vector arcade perspective wireframe terrain with phosphor vectors and horizon beacons."""
    im = Image.new("RGB", (W, H), (14, 18, 30))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.52

    # Horizon line
    draw.line([(0, cy - 40), (W, cy - 40)], fill=(52, 235, 147, 80), width=2)

    # Perspective terrain grid lines
    num_z = 18
    num_x = 22
    for z in range(num_z):
        depth = (z + 1) / num_z
        y = cy - 40 + (depth ** 2.2) * (H - (cy - 40))
        span_x = W * 0.25 + (depth ** 1.8) * (W * 0.75)
        x_left = cx - span_x * 0.5
        x_right = cx + span_x * 0.5

        # Horizontal undulating terrain line
        pts = []
        for x_idx in range(num_x + 1):
            ux = x_idx / num_x
            px = x_left + ux * span_x
            elevation = math.sin(ux * 12.0 + depth * 6.0) * (18.0 * (depth ** 1.5))
            pts.append((px, y - elevation))

        alpha = int(60 + 175 * depth)
        draw.line(pts, fill=(52, 235, 147, alpha), width=2 if depth > 0.6 else 1)

    # Longitudinal converging lines
    for x_idx in range(num_x + 1):
        ux = x_idx / num_x
        pts_long = []
        for z in range(num_z):
            depth = (z + 1) / num_z
            y = cy - 40 + (depth ** 2.2) * (H - (cy - 40))
            span_x = W * 0.25 + (depth ** 1.8) * (W * 0.75)
            x_left = cx - span_x * 0.5
            px = x_left + ux * span_x
            elevation = math.sin(ux * 12.0 + depth * 6.0) * (18.0 * (depth ** 1.5))
            pts_long.append((px, y - elevation))

        draw.line(pts_long, fill=(56, 189, 248, 120), width=1)

    # Horizon radar beacons
    num_beacons = 5
    for b in range(num_beacons):
        bx = cx + (b - (num_beacons - 1) / 2) * 160.0
        by = cy - 40
        draw.line([(bx, by), (bx, by - 65)], fill=(245, 248, 255, 220), width=2)
        draw.ellipse([bx - 6, by - 70, bx + 6, by - 58], fill=(255, 107, 53, 240))
        draw.ellipse([bx - 14, by - 78, bx + 14, by - 50], outline=(255, 107, 53, 110), width=1)

    im.save(os.path.join(PREVIEWS_DIR, "analog-crt-wireframe-terrain-3d.jpg"), quality=94)
    print("analog-crt-wireframe-terrain-3d.jpg saved")

def render_op_art_hyperbolic_cage_3d():
    """Ruled hyperbolic optical illusion wireframe cage with moire interference."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5
    min_dim = min(W, H)

    r_rim = min_dim * 0.36
    h_cage = min_dim * 0.68
    y_top = cy - h_cage * 0.5
    y_bot = cy + h_cage * 0.5

    # Collar rims
    draw.ellipse([cx - r_rim, y_top - r_rim * 0.22, cx + r_rim, y_top + r_rim * 0.22],
                 outline=(167, 177, 255, 210), width=3)
    draw.ellipse([cx - r_rim, y_bot - r_rim * 0.22, cx + r_rim, y_bot + r_rim * 0.22],
                 outline=(167, 177, 255, 210), width=3)

    # Ruled hyperbolic straight struts
    num_struts = 36
    twist = 2.05
    for i in range(num_struts):
        a_top = (i / num_struts) * math.pi * 2.0
        a_bot = a_top + twist

        x1 = cx + math.cos(a_top) * r_rim
        y1 = y_top + math.sin(a_top) * (r_rim * 0.22)

        x2 = cx + math.cos(a_bot) * r_rim
        y2 = y_bot + math.sin(a_bot) * (r_rim * 0.22)

        is_light = (i % 2 == 0)
        col = (245, 248, 255, 240) if is_light else (167, 177, 255, 210)
        draw.line([(x1, y1), (x2, y2)], fill=col, width=2)

        # Node beads
        draw.ellipse([x1 - 3, y1 - 3, x1 + 3, y1 + 3], fill=(255, 107, 53, 240))

        # Counter inner cage
        r_in = r_rim * 0.55
        xi1 = cx + math.cos(a_top) * r_in
        yi1 = cy - h_cage * 0.35 + math.sin(a_top) * (r_in * 0.2)
        xi2 = cx + math.cos(a_top - twist * 0.7) * r_in
        yi2 = cy + h_cage * 0.35 + math.sin(a_top - twist * 0.7) * (r_in * 0.2)
        draw.line([(xi1, yi1), (xi2, yi2)], fill=(78, 242, 184, 160), width=1)

    im.save(os.path.join(PREVIEWS_DIR, "op-art-hyperbolic-cage-3d.jpg"), quality=94)
    print("op-art-hyperbolic-cage-3d.jpg saved")

def render_analog_crt_phosphor_beam_synth():
    """Oscilloscope vector beam oscillator with resonant beam focusing filter and meters."""
    im = Image.new("RGB", (W, H), (14, 18, 30))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.48

    # Chassis bezel
    draw.rectangle([W * 0.08, H * 0.1, W * 0.92, H * 0.88], outline=(52, 62, 95, 220), width=2)
    # Cathode screen window
    draw.rectangle([W * 0.12, H * 0.15, W * 0.88, H * 0.78], fill=(18, 28, 42, 255), outline=(52, 235, 147, 90), width=2)

    # Waveform display trace (oscilloscope saw-pulse morph)
    pts = []
    num_pts = 240
    screen_w = W * 0.74
    screen_h = H * 0.58
    x_start = W * 0.13
    y_center = H * 0.465

    for i in range(num_pts + 1):
        u = i / num_pts
        px = x_start + u * screen_w
        phase = (u * 6.0) % 1.0
        saw = (phase * 2.0 - 1.0)
        pulse = 0.8 if phase > 0.45 else -0.8
        wave = saw * 0.6 + pulse * 0.4
        py = y_center - wave * (screen_h * 0.32)
        pts.append((px, py))

    draw.line(pts, fill=(52, 235, 147, 60), width=8)
    draw.line(pts, fill=(56, 189, 248, 140), width=4)
    draw.line(pts, fill=(245, 255, 250, 250), width=2)

    # Control knobs and indicators along bottom panel
    num_knobs = 5
    knob_labels = ["CUTOFF", "RESONANCE", "SUB LEVEL", "WARMTH", "DECAY"]
    for k in range(num_knobs):
        kx = W * 0.18 + k * (W * 0.64 / (num_knobs - 1))
        ky = H * 0.83
        draw.ellipse([kx - 18, ky - 18, kx + 18, ky + 18], fill=(32, 42, 68, 255), outline=(167, 177, 255, 200), width=2)
        angle = -2.2 + k * 1.0
        draw.line([(kx, ky), (kx + math.cos(angle) * 14, ky + math.sin(angle) * 14)], fill=(255, 107, 53, 255), width=2)

    im.save(os.path.join(PREVIEWS_DIR, "analog-crt-phosphor-beam-synth.jpg"), quality=94)
    print("analog-crt-phosphor-beam-synth.jpg saved")

def render_op_art_spectral_chime_synth():
    """Spectral additive chime synthesizer with harmonic partial bars and moire beating."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5

    # Harmonic spectral bars
    num_partials = 28
    bar_w = 16.0
    start_x = W * 0.12
    step_x = (W * 0.76) / num_partials

    for p in range(num_partials):
        u = p / num_partials
        bx = start_x + p * step_x
        # Inharmonic modal height distribution
        h_norm = math.exp(-u * 2.8) * (math.sin(u * 14.0) * 0.35 + 0.65)
        bar_h = h_norm * (H * 0.62)
        by = H * 0.78 - bar_h

        # Spectral gradient
        col = (int(167 + 78 * u), int(177 - 70 * u), 255, 230)
        draw.rectangle([bx, by, bx + bar_w, H * 0.78], fill=col)

        # Resonant peak highlight
        draw.rectangle([bx, by, bx + bar_w, by + 4], fill=(245, 248, 255, 250))

        # Kinetic beating envelope curve
        if p < num_partials - 1:
            next_u = (p + 1) / num_partials
            next_h = math.exp(-next_u * 2.8) * (math.sin(next_u * 14.0) * 0.35 + 0.65) * (H * 0.62)
            next_by = H * 0.78 - next_h
            draw.line([(bx + bar_w * 0.5, by), (bx + step_x + bar_w * 0.5, next_by)],
                      fill=(78, 242, 184, 180), width=2)

    im.save(os.path.join(PREVIEWS_DIR, "op-art-spectral-chime-synth.jpg"), quality=94)
    print("op-art-spectral-chime-synth.jpg saved")

def render_analog_crt_tape_flutter_fx():
    """Cathode tape flutter delay visualization with magnetic tape reels and saturation waveform."""
    im = Image.new("RGB", (W, H), (14, 18, 30))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5

    # Dual tape reels
    reel_r = 110.0
    r1_x, r1_y = W * 0.3, H * 0.42
    r2_x, r2_y = W * 0.7, H * 0.42

    for rx, ry in [(r1_x, r1_y), (r2_x, r2_y)]:
        draw.ellipse([rx - reel_r, ry - reel_r, rx + reel_r, ry + reel_r], outline=(52, 62, 95, 240), width=4)
        draw.ellipse([rx - reel_r * 0.35, ry - reel_r * 0.35, rx + reel_r * 0.35, ry + reel_r * 0.35],
                     fill=(32, 42, 68, 255), outline=(167, 177, 255, 220), width=2)
        # 3 tape spokes
        for s in range(3):
            a = s * (math.pi * 2.0 / 3.0) + 0.4
            sx = rx + math.cos(a) * reel_r
            sy = ry + math.sin(a) * reel_r
            draw.line([(rx, ry), (sx, sy)], fill=(167, 177, 255, 180), width=3)

    # Tape path linking reels
    draw.line([(r1_x, r1_y + reel_r), (r2_x, r2_y + reel_r)], fill=(255, 107, 53, 230), width=5)

    # Flutter delay ripple waveform along bottom
    pts = []
    num_pts = 160
    for i in range(num_pts + 1):
        u = i / num_pts
        px = W * 0.15 + u * (W * 0.7)
        py = H * 0.78 + math.sin(u * 24.0) * 16.0 * math.sin(u * 4.0)
        pts.append((px, py))
    draw.line(pts, fill=(52, 235, 147, 230), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "analog-crt-tape-flutter-fx.jpg"), quality=94)
    print("analog-crt-tape-flutter-fx.jpg saved")

def render_op_art_frequency_shifter_fx():
    """Quadrature phase shifter with barber-pole Shepard tone phase spiral."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5
    min_dim = min(W, H)

    # Concentric quadrature phase circles
    num_circles = 8
    for c in range(num_circles):
        rad = min_dim * (0.1 + c * 0.042)
        draw.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], outline=(167, 177, 255, 70), width=1)

    # Barber-pole phase spiral arms
    num_arms = 6
    for a in range(num_arms):
        arm_phase = a * (math.pi * 2.0 / num_arms)
        pts = []
        for r_step in range(120):
            r = (r_step / 120.0) * (min_dim * 0.42)
            theta = arm_phase + r * 0.045
            px = cx + math.cos(theta) * r
            py = cy + math.sin(theta) * r
            pts.append((px, py))
        draw.line(pts, fill=(245, 248, 255, 230) if a % 2 == 0 else (78, 242, 184, 210), width=3)

    # Central quadrature nucleus
    draw.ellipse([cx - 24, cy - 24, cx + 24, cy + 24], fill=(255, 107, 53, 240))
    draw.ellipse([cx - 10, cy - 10, cx + 10, cy + 10], fill=(245, 248, 255, 255))

    im.save(os.path.join(PREVIEWS_DIR, "op-art-frequency-shifter-fx.jpg"), quality=94)
    print("op-art-frequency-shifter-fx.jpg saved")

def render_analog_crt_phosphor_bloom():
    """Cathode ray oscilloscope vector bloom preview with curved barrel distortion and scanlines."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    aspect = W / H

    y_indices, x_indices = np.indices((H, W))
    p0_x = ((x_indices / W) - 0.5) * aspect
    p0_y = ((y_indices / H) - 0.5)

    r2 = p0_x**2 + p0_y**2
    barrel = 0.22
    p_x = p0_x * (1.0 + r2 * barrel)
    p_y = p0_y * (1.0 + r2 * barrel)

    r = np.sqrt(p_x**2 + p_y**2)
    ang = np.arctan2(p_y, p_x)

    curve1 = np.abs(p_y - np.sin(p_x * 6.28 * 2.0 + 1.2) * 0.22)
    curve2 = np.abs(p_x - np.cos(p_y * 6.28 * 3.0 - 0.9) * 0.28)
    beam = np.exp(-curve1 * 35.0) + np.exp(-curve2 * 35.0)

    ring = np.abs(r - 0.32 - np.sin(ang * 4.0) * 0.04)
    halo = np.exp(-ring * 24.0) * 0.6

    scan = 0.85 + 0.15 * np.sin((y_indices / H) * 480.0 * np.pi)
    vignette = np.clip(1.0 - r2 * 1.6, 0.0, 1.0)

    c_phosphor = np.array([52, 235, 147], dtype=np.float32)
    c_core = np.array([245, 255, 250], dtype=np.float32)
    c_ground = np.array([12, 16, 26], dtype=np.float32)

    for y in range(H):
        for x in range(W):
            b = beam[y, x]
            h = halo[y, x]
            s = scan[y, x]
            vg = vignette[y, x]

            col_glow = c_phosphor * (b * 1.6 + h * 1.2)
            col_core = c_core * (np.clip(b, 0.0, 1.0) ** 3.0)
            col_sig = col_glow + col_core

            total_intensity = np.clip(b + h, 0.0, 1.0)
            col = (c_ground * (1.0 - total_intensity) + col_sig * total_intensity) * s * vg
            out[y, x] = np.clip(col, 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "analog-crt-phosphor-bloom.jpg"), quality=94)
    print("analog-crt-phosphor-bloom.jpg saved")

def render_op_art_hypnotic_vortex():
    """Hypnotic high-contrast optical art spiral vortex with moire bands preview."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    aspect = W / H

    y_indices, x_indices = np.indices((H, W))
    p_x = ((x_indices / W) - 0.5) * aspect
    p_y = ((y_indices / H) - 0.5)

    r = np.sqrt(p_x**2 + p_y**2)
    ang = np.arctan2(p_y, p_x)

    log_r = np.log(r + 0.001)
    spiral = ang * 16.0 + log_r * 3.5 * 12.0
    concentric = np.sin(r * 24.0 * 6.28)
    moire = np.sin(spiral) * concentric

    edge = np.clip((moire + 0.2) / 0.4, 0.0, 1.0)
    vignette = np.clip(1.0 - r * 1.4, 0.0, 1.0)

    c_light = np.array([245, 248, 255], dtype=np.float32)
    c_signal = np.array([167, 177, 255], dtype=np.float32)
    c_dark = np.array([21, 25, 48], dtype=np.float32)

    for y in range(H):
        for x in range(W):
            e = edge[y, x]
            vg = vignette[y, x]
            a = ang[y, x]

            c_fg = c_light * 0.75 + c_signal * (math.sin(a * 4.0) * 0.25 + 0.25)
            col = (c_dark * (1.0 - e) + c_fg * e) * vg
            out[y, x] = np.clip(col, 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "op-art-hypnotic-vortex.jpg"), quality=94)
    print("op-art-hypnotic-vortex.jpg saved")

def main():
    render_analog_crt_vector_oscilloscope()
    render_op_art_kinetic_wave_moire()
    render_analog_crt_wireframe_terrain_3d()
    render_op_art_hyperbolic_cage_3d()
    render_analog_crt_phosphor_beam_synth()
    render_op_art_spectral_chime_synth()
    render_analog_crt_tape_flutter_fx()
    render_op_art_frequency_shifter_fx()
    render_analog_crt_phosphor_bloom()
    render_op_art_hypnotic_vortex()

    # Also render template webp previews using template_preview.py
    sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
    import template_preview
    for t_slug in ("analog-crt-phosphor-beam-synth", "op-art-spectral-chime-synth", "analog-crt-tape-flutter-fx", "op-art-frequency-shifter-fx"):
        inf_path = os.path.join(TEMPLATES_DIR, t_slug + ".inf")
        webp_path = os.path.join(TEMPLATES_DIR, t_slug + ".webp")
        template_preview.render(inf_path, webp_path)
        print(f"{t_slug}.webp saved")

    print("All Pack 14 previews successfully generated!")

if __name__ == "__main__":
    main()
