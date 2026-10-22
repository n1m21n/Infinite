#!/usr/bin/env python3
"""Generate high-fidelity preview images for the 10 new creative library items.
Matches brand design: midnight ground #151930, signal #A7B1FF, ember #FF6B35, emerald #4EF2B8.
"""
import os, math
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PREVIEWS_DIR = os.path.join(ROOT, "website", "assets", "library", "previews")
os.makedirs(PREVIEWS_DIR, exist_ok=True)

GROUND = np.array([21, 25, 48], dtype=np.float32)     # #151930
SIGNAL = np.array([167, 177, 255], dtype=np.float32)  # #A7B1FF
EMBER = np.array([255, 107, 53], dtype=np.float32)    # #FF6B35
EMERALD = np.array([78, 242, 184], dtype=np.float32)  # #4EF2B8
WHITE = np.array([245, 248, 255], dtype=np.float32)

W, H = 900, 600

def render_aizawa():
    """Aizawa attractor chaotic orbital helix."""
    acc = np.zeros((H, W), dtype=np.float32)
    cx, cy = W / 2, H / 2
    k = min(W, H) * 0.38
    a, b, c, d, e, f = 0.95, 0.7, 0.6, 3.5, 0.25, 0.1
    x, y, z = 0.1, 0.0, 0.0
    dt = 0.006

    for i in range(1500000):
        x2, y2, z2 = x * x, y * y, z * z
        dx = (z - b) * x - d * y
        dy = d * x + (z - b) * y
        dz = c + a * z - (z * z2) / 3.0 - (x2 + y2) * (1.0 + e * z) + f * z * (x * x2)
        x += dx * dt
        y += dy * dt
        z += dz * dt

        if i > 2000:
            rot = 0.85
            rx = x * math.cos(rot) - y * math.sin(rot)
            ry = (x * math.sin(rot) + y * math.cos(rot)) * 0.45 + z * 0.85
            px = int(cx + rx * k)
            py = int(cy - (ry - 0.4) * k)
            if 0 <= px < W and 0 <= py < H:
                acc[py, px] += 1.0

    acc = np.log1p(acc * 0.9)
    norm = np.clip(acc / np.percentile(acc[acc > 0], 99.6), 0.0, 1.0)
    out = np.zeros((H, W, 3), dtype=np.float32)
    for c_idx in range(3):
        out[:, :, c_idx] = GROUND[c_idx]

    for y_idx in range(H):
        for x_idx in range(W):
            v = norm[y_idx, x_idx]
            if v > 0:
                frac = v**0.8
                mix_col = SIGNAL * (1.0 - frac) + EMBER * frac
                out[y_idx, x_idx] = np.clip(GROUND * (1.0 - v) + mix_col * v * 1.8 + WHITE * (v**3 * 1.4), 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "aizawa-attractor.jpg"), quality=94)
    print("aizawa-attractor.jpg saved")

def render_thomas():
    """Thomas cyclically symmetric chaotic attractor."""
    acc = np.zeros((H, W), dtype=np.float32)
    cx, cy = W / 2, H / 2
    k = min(W, H) * 0.13
    b = 0.208186
    x, y, z = 0.1, 0.0, 0.0
    dt = 0.03

    for i in range(1200000):
        dx = math.sin(y) - b * x
        dy = math.sin(z) - b * y
        dz = math.sin(x) - b * z
        x += dx * dt
        y += dy * dt
        z += dz * dt

        if i > 1000:
            rotY = 0.65
            rotX = 0.5
            x1 = x * math.cos(rotY) - z * math.sin(rotY)
            z1 = x * math.sin(rotY) + z * math.cos(rotY)
            y1 = y * math.cos(rotX) - z1 * math.sin(rotX)

            px = int(cx + x1 * k)
            py = int(cy - y1 * k)
            if 0 <= px < W and 0 <= py < H:
                acc[py, px] += 1.0

    acc = np.log1p(acc * 0.8)
    norm = np.clip(acc / np.percentile(acc[acc > 0], 99.5), 0.0, 1.0)
    out = np.zeros((H, W, 3), dtype=np.float32)
    for c_idx in range(3):
        out[:, :, c_idx] = GROUND[c_idx]

    for y_idx in range(H):
        for x_idx in range(W):
            v = norm[y_idx, x_idx]
            if v > 0:
                frac = v**0.85
                mix_col = EMBER * (1.0 - frac) + SIGNAL * frac
                out[y_idx, x_idx] = np.clip(GROUND * (1.0 - v) + mix_col * v * 1.8 + WHITE * (v**3 * 1.3), 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "thomas-attractor.jpg"), quality=94)
    print("thomas-attractor.jpg saved")

def render_roman_surface():
    """Steiner Roman Surface 3D."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2
    radius = 180.0
    uSteps, vSteps = 48, 28

    for j in range(vSteps + 1):
        v = (j / vSteps) * math.pi - math.pi * 0.5
        pts = []
        for i in range(uSteps + 1):
            u = (i / uSteps) * math.pi
            x = radius * math.sin(2.0 * u) * math.cos(v) * math.cos(v)
            y = radius * math.sin(u) * math.sin(2.0 * v)
            z = radius * math.cos(u) * math.sin(2.0 * v)

            rotY = 0.75
            rotX = 0.45
            x1 = x * math.cos(rotY) + z * math.sin(rotY)
            z1 = -x * math.sin(rotY) + z * math.cos(rotY)
            y2 = y * math.cos(rotX) - z1 * math.sin(rotX)
            pts.append((cx + x1, cy + y2))

        frac = j / vSteps
        cr = int(SIGNAL[0] * (1 - frac) + EMBER[0] * frac)
        cg = int(SIGNAL[1] * (1 - frac) + EMBER[1] * frac)
        cb = int(SIGNAL[2] * (1 - frac) + EMBER[2] * frac)

        for i in range(len(pts) - 1):
            draw.line([pts[i], pts[i+1]], fill=(cr, cg, cb, 220), width=2)

    im.save(os.path.join(PREVIEWS_DIR, "roman-surface-3d.jpg"), quality=94)
    print("roman-surface-3d.jpg saved")

def render_clifford_torus():
    """Clifford Torus 3D stereographic projection."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2
    circles = 32
    segs = 64
    radius = 160.0
    angle4D = 0.65
    c4, s4 = math.cos(angle4D), math.sin(angle4D)

    for c in range(circles):
        u = (c / circles) * math.pi * 2.0
        pts = []
        for i in range(segs + 1):
            v = (i / segs) * math.pi * 2.0
            x0 = math.cos(u) * 0.7071
            y0 = math.sin(u) * 0.7071
            z0 = math.cos(v) * 0.7071
            w0 = math.sin(v) * 0.7071

            x4 = x0 * c4 - w0 * s4
            w4 = x0 * s4 + w0 * c4
            y4 = y0 * c4 - z0 * s4
            z4 = y0 * s4 + z0 * c4

            denom = 1.6 - w4
            px = (x4 / denom) * radius
            py = (y4 / denom) * radius
            pz = (z4 / denom) * radius

            rotY = 0.55
            rotX = 0.4
            x1 = px * math.cos(rotY) + pz * math.sin(rotY)
            z1 = -px * math.sin(rotY) + pz * math.cos(rotY)
            y2 = py * math.cos(rotX) - z1 * math.sin(rotX)
            pts.append((cx + x1, cy + y2))

        frac = c / circles
        cr = int(EMBER[0] * (1 - frac) + SIGNAL[0] * frac)
        cg = int(EMBER[1] * (1 - frac) + SIGNAL[1] * frac)
        cb = int(EMBER[2] * (1 - frac) + SIGNAL[2] * frac)

        for i in range(len(pts) - 1):
            draw.line([pts[i], pts[i+1]], fill=(cr, cg, cb, 210), width=2)

    im.save(os.path.join(PREVIEWS_DIR, "clifford-torus-3d.jpg"), quality=94)
    print("clifford-torus-3d.jpg saved")

def render_wave_terrain():
    """Wave Terrain Synth 2D scanning trajectory over 3D height field."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2

    # Background terrain grid
    grid_lines = 24
    for gy in range(-grid_lines, grid_lines + 1, 2):
        pts = []
        for gx in range(-grid_lines, grid_lines + 1):
            x = gx * 12.0
            y = gy * 10.0
            z = math.sin(gx * 0.35) * math.cos(gy * 0.3) * 45.0
            iso_x = cx + (x - y) * 1.1
            iso_y = cy + (x + y) * 0.55 - z
            pts.append((iso_x, iso_y))
        for i in range(len(pts) - 1):
            draw.line([pts[i], pts[i+1]], fill=(42, 48, 82, 160), width=1)

    # Scanning orbit trajectory
    orbit_pts = []
    for step in range(400):
        t = (step / 400.0) * math.pi * 8.0
        ox = math.sin(t) * 150.0
        oy = math.cos(t * 1.5) * 120.0
        oz = math.sin(ox * 0.02) * math.cos(oy * 0.02) * 55.0
        px = cx + (ox - oy) * 0.95
        py = cy + (ox + oy) * 0.48 - oz
        orbit_pts.append((px, py))

    for i in range(len(orbit_pts) - 1):
        frac = i / len(orbit_pts)
        cr = int(EMBER[0] * (1 - frac) + SIGNAL[0] * frac)
        cg = int(EMBER[1] * (1 - frac) + SIGNAL[1] * frac)
        cb = int(EMBER[2] * (1 - frac) + SIGNAL[2] * frac)
        draw.line([orbit_pts[i], orbit_pts[i+1]], fill=(cr, cg, cb, 240), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "wave-terrain-synth.jpg"), quality=94)
    print("wave-terrain-synth.jpg saved")

def render_formant_vocal():
    """Formant Vocal Synth phonetic spectral formant peaks."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2

    # Formant resonance curve
    formants = [(600, 60, EMBER), (1400, 90, SIGNAL), (2600, 140, EMERALD)]
    pts = []
    for x in range(W):
        freq = x / W * 3600.0
        resp = 0.0
        for f0, bw, _ in formants:
            resp += 1.0 / (1.0 + ((freq - f0) / bw)**2)
        y = cy + 110 - resp * 160.0
        pts.append((x, y))

    # Glow fill under envelope
    for i in range(0, len(pts), 4):
        draw.line([pts[i], (pts[i][0], cy + 110)], fill=(int(SIGNAL[0]), int(SIGNAL[1]), int(SIGNAL[2]), 40), width=2)

    for i in range(len(pts) - 1):
        frac = i / len(pts)
        cr = int(SIGNAL[0] * (1 - frac) + EMBER[0] * frac)
        cg = int(SIGNAL[1] * (1 - frac) + EMBER[1] * frac)
        cb = int(SIGNAL[2] * (1 - frac) + EMBER[2] * frac)
        draw.line([pts[i], pts[i+1]], fill=(cr, cg, cb, 255), width=3)

    # Glottal pulse train
    pulse_pts = []
    for x in range(W):
        t = (x - cx) / W * 24.0
        val = math.sin(t * 12.0) * math.exp(-((x % 90) / 30.0))
        pulse_pts.append((x, cy - 120 + val * 35.0))

    for i in range(len(pulse_pts) - 1):
        draw.line([pulse_pts[i], pulse_pts[i+1]], fill=(int(WHITE[0]), int(WHITE[1]), int(WHITE[2]), 200), width=2)

    im.save(os.path.join(PREVIEWS_DIR, "formant-vocal-synth.jpg"), quality=94)
    print("formant-vocal-synth.jpg saved")

def render_pitch_shifter_delay():
    """Pitch Shifter Delay FX dual rotating delay heads and tape shimmer."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2

    # Rotating delay tap rings
    for r in range(40, 260, 20):
        frac = r / 260.0
        alpha = int(255 * math.sin(frac * math.pi) * 0.7)
        cr = int(SIGNAL[0] * (1 - frac) + EMBER[0] * frac)
        cg = int(SIGNAL[1] * (1 - frac) + EMBER[1] * frac)
        cb = int(SIGNAL[2] * (1 - frac) + EMBER[2] * frac)
        draw.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(cr, cg, cb, alpha), width=2)

    # Crossfading pitch shift wave
    for wave_idx, col in enumerate([EMBER, SIGNAL]):
        shift_pts = []
        phase_off = wave_idx * math.pi
        for x in range(W):
            t = (x - cx) / W * 16.0
            flutter = math.sin(t * 2.2) * 0.15
            env = math.exp(-((x - cx) / 280.0)**2)
            y = cy + math.sin(t * (8.0 + wave_idx * 4.0) + phase_off + flutter) * 80.0 * env
            shift_pts.append((x, y))

        for i in range(len(shift_pts) - 1):
            draw.line([shift_pts[i], shift_pts[i+1]], fill=(int(col[0]), int(col[1]), int(col[2]), 230), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "pitch-shifter-delay-fx.jpg"), quality=94)
    print("pitch-shifter-delay-fx.jpg saved")

def render_envelope_follower_wah():
    """Envelope Follower Wah FX dynamic optical response and resonant peak."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2

    # Swept resonant wah curves
    for sweep_idx, (f_center, col) in enumerate([(cx - 160, EMBER), (cx, SIGNAL), (cx + 160, EMERALD)]):
        pts = []
        for x in range(W):
            dist = (x - f_center) / 55.0
            peak = 1.0 / (1.0 + dist * dist)
            y = cy + 100 - peak * 170.0
            pts.append((x, y))

        for i in range(len(pts) - 1):
            draw.line([pts[i], pts[i+1]], fill=(int(col[0]), int(col[1]), int(col[2]), 220), width=3)

    # Optical attack / release curve on top
    opt_pts = []
    for x in range(W):
        t = x / W
        if t < 0.2:
            val = (t / 0.2)**0.5
        else:
            val = math.exp(-(t - 0.2) * 3.5)
        opt_pts.append((x, cy - 110 - val * 65.0))

    for i in range(len(opt_pts) - 1):
        draw.line([opt_pts[i], opt_pts[i+1]], fill=(int(WHITE[0]), int(WHITE[1]), int(WHITE[2]), 240), width=2)

    im.save(os.path.join(PREVIEWS_DIR, "envelope-follower-wah-fx.jpg"), quality=94)
    print("envelope-follower-wah-fx.jpg saved")

def render_bioluminescent_abyss():
    """Bioluminescent Abyss deep oceanic medusa and caustic spores."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    cx, cy = W / 2, H / 2
    for c_idx in range(3):
        out[:, :, c_idx] = GROUND[c_idx]

    y_grid, x_grid = np.ogrid[:H, :W]
    dx = (x_grid - cx) / (min(W, H) * 0.45)
    dy = (y_grid - cy) / (min(W, H) * 0.45)
    r = np.sqrt(dx**2 + dy**2)
    a = np.arctan2(dy, dx)

    bell = np.clip((0.55 - r) / 0.35, 0, 1)**2.0
    ribs = np.cos(a * 8.0 + np.sin(r * 12.0) * 1.6)
    filaments = np.clip((ribs - 0.2) / 0.7, 0, 1) * np.clip((0.75 - r) / 0.4, 0, 1)

    c1 = np.sin(dx * 12.0 + np.sin(dy * 9.0))
    c2 = np.sin(dy * 12.0 + np.cos(dx * 9.0))
    spores = np.clip((c1 * c2 - 0.6) / 0.38, 0, 1)**2.0

    cyan = np.array([30, 225, 242], dtype=np.float32)
    violet = np.array([167, 89, 250], dtype=np.float32)

    for y_idx in range(H):
        for x_idx in range(W):
            rad = r[y_idx, x_idx]
            b_val = bell[y_idx, x_idx]
            f_val = filaments[y_idx, x_idx]
            s_val = spores[y_idx, x_idx]

            mix_v = min(1.0, rad * 1.6)
            glow_c = cyan * (1.0 - mix_v) + violet * mix_v
            medusa = glow_c * (b_val * 1.5 + f_val * 1.8)
            sparkles = EMBER * s_val * 1.2 + SIGNAL * (b_val * 0.4)

            col = GROUND * (1.0 - min(1.0, b_val + f_val)) + medusa + sparkles
            vig = np.clip((1.6 - rad) / 0.7, 0, 1)
            out[y_idx, x_idx] = np.clip(col * vig + GROUND * (1.0 - vig), 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "bioluminescent-abyss.jpg"), quality=94)
    print("bioluminescent-abyss.jpg saved")

def render_quantum_field_lattice():
    """Quantum Field Lattice relativistic spacetime grid."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    cx, cy = W / 2, H / 2
    for c_idx in range(3):
        out[:, :, c_idx] = GROUND[c_idx]

    y_grid, x_grid = np.ogrid[:H, :W]
    dx = (x_grid - cx) / (min(W, H) * 0.45)
    dy = (y_grid - cy) / (min(W, H) * 0.45)
    r = np.sqrt(dx**2 + dy**2)

    warp_factor = 1.0 + r * r * 1.8
    wx = (dx / warp_factor) * 15.0
    wy = (dy / warp_factor) * 15.0

    q1 = np.sin(wx + 0.8) * np.cos(wy * 2.4)
    q2 = np.cos(wy - 0.7) * np.sin(wx * 2.4)
    field = q1 + q2

    rings = np.abs(np.mod(r * 8.0, 1.0) - 0.5)
    phase = np.clip((np.maximum(np.abs(field) * 0.38, rings) - 0.40) / 0.09, 0, 1)

    for y_idx in range(H):
        for x_idx in range(W):
            rad = r[y_idx, x_idx]
            f_val = field[y_idx, x_idx]
            p_val = phase[y_idx, x_idx]

            field_norm = math.sin(f_val * 3.14159) * 0.5 + 0.5
            lattice = (SIGNAL * (1.0 - field_norm) + EMERALD * field_norm) * (p_val * 1.7)
            core = EMBER * np.clip((0.36 - rad) / 0.36, 0, 1)**1.8 * 1.5

            col = GROUND * (1.0 - p_val * 0.7) + lattice + core
            vig = np.clip((1.7 - rad) / 0.7, 0, 1)
            out[y_idx, x_idx] = np.clip(col * vig + GROUND * (1.0 - vig), 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "quantum-field-lattice.jpg"), quality=94)
    print("quantum-field-lattice.jpg saved")

if __name__ == "__main__":
    render_aizawa()
    render_thomas()
    render_roman_surface()
    render_clifford_torus()
    render_wave_terrain()
    render_formant_vocal()
    render_pitch_shifter_delay()
    render_envelope_follower_wah()
    render_bioluminescent_abyss()
    render_quantum_field_lattice()
    print("All 10 preview images successfully generated!")
