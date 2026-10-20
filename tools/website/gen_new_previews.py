#!/usr/bin/env python3
import os
import math
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

W_ROOT = "/Users/namansoni/Infinite/worktrees/main/website"
PREVIEWS_DIR = os.path.join(W_ROOT, "assets/library/previews")
TEMPLATES_DIR = os.path.join(W_ROOT, "assets/templates")

GROUND = np.array([21, 25, 48], dtype=np.float32)     # #151930
SIGNAL = np.array([167, 177, 255], dtype=np.float32)  # #A7B1FF
EMBER = np.array([255, 107, 53], dtype=np.float32)    # #FF6B35
WHITE = np.array([245, 248, 255], dtype=np.float32)


def render_hopalong():
    """Barry Martin Hopalong attractor density plot."""
    w, h = 900, 600
    acc = np.zeros((h, w), dtype=np.float32)
    a, b, c = 2.0, 1.0, 0.0
    x, y = 0.0, 0.0
    k = min(w, h) * 0.038
    cx, cy = w / 2, h / 2

    n_points = 1200000
    for _ in range(n_points):
        s = -1.0 if x < 0 else (1.0 if x > 0 else 0.0)
        nx = y - s * math.sqrt(abs(b * x - c))
        ny = a - x
        x, y = nx, ny
        px = int(cx + x * k)
        py = int(cy + y * k)
        if 0 <= px < w and 0 <= py < h:
            acc[py, px] += 1.0

    # Log tone mapping
    acc = np.log1p(acc * 0.5)
    max_val = np.percentile(acc[acc > 0], 99.5) if np.any(acc > 0) else 1.0
    norm = np.clip(acc / max_val, 0.0, 1.0)

    # Blur slightly for glow
    img_glow = Image.fromarray((norm * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.5))
    glow_arr = np.array(img_glow, dtype=np.float32) / 255.0

    out = np.zeros((h, w, 3), dtype=np.float32)
    for c_idx in range(3):
        out[:, :, c_idx] = GROUND[c_idx]

    # Colorize: core ember/white, outer filaments signal/violet
    for y_idx in range(h):
        for x_idx in range(w):
            v = norm[y_idx, x_idx]
            g = glow_arr[y_idx, x_idx]
            if v > 0 or g > 0:
                rad = math.sqrt((x_idx - cx)**2 + (y_idx - cy)**2) / (min(w, h) * 0.45)
                mix_col = SIGNAL * (1.0 - min(1.0, rad * 0.7)) + EMBER * min(1.0, rad * 0.7)
                col = mix_col * (v * 1.2 + g * 0.8) + WHITE * (v**2.2 * 1.5)
                out[y_idx, x_idx] = np.clip(GROUND * (1.0 - min(1.0, v + g)) + col, 0, 255)

    im = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))
    im.save(os.path.join(PREVIEWS_DIR, "hopalong-attractor.jpg"), quality=94)
    print("hopalong-attractor.jpg saved")


def render_klein():
    """Parametric Klein bottle 3D immersion."""
    w, h = 900, 600
    im = Image.new("RGB", (w, h), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    # Rotate around Y and X
    rotY = 0.85
    rotX = 0.55
    cy, cx = h / 2, w / 2

    def point(u, v):
        a = 2.0
        halfU = u * 0.5
        sinV, cosV = math.sin(v), math.cos(v)
        sin2V = math.sin(2.0 * v)
        sinHU, cosHU = math.sin(halfU), math.cos(halfU)
        cosU, sinU = math.cos(u), math.sin(u)

        rad = a + cosHU * sinV - sinHU * sin2V
        x = rad * cosU * 0.45
        y = rad * sinU * 0.45
        z = (sinHU * sinV + cosHU * sin2V) * 0.65

        # Rotate Y
        x1 = x * math.cos(rotY) + z * math.sin(rotY)
        z1 = -x * math.sin(rotY) + z * math.cos(rotY)
        # Rotate X
        y2 = y * math.cos(rotX) - z1 * math.sin(rotX)
        z2 = y * math.sin(rotX) + z1 * math.cos(rotX)

        fov = 3.2
        dist = 3.8 - z2
        px = cx + (x1 / dist) * fov * 280
        py = cy - (y2 / dist) * fov * 280
        return px, py, z2

    # Draw rings sorted back-to-front
    ribs = 64
    segs = 28
    rings = []
    for i in range(ribs):
        u = (i / ribs) * math.tau
        pts = [point(u, (j / segs) * math.tau) for j in range(segs + 1)]
        avg_z = sum(p[2] for p in pts) / len(pts)
        rings.append((avg_z, i / ribs, pts))

    rings.sort(key=lambda r: r[0])

    for avg_z, frac, pts in rings:
        depth_alpha = int(np.clip(140 + avg_z * 80, 50, 255))
        col_mix = SIGNAL * (1.0 - frac) + EMBER * frac
        r, g, b = [int(np.clip(c, 0, 255)) for c in col_mix]
        for j in range(len(pts) - 1):
            p1, p2 = pts[j], pts[j + 1]
            draw.line([(p1[0], p1[1]), (p2[0], p2[1])], fill=(r, g, b, depth_alpha), width=2)

    # Highlight longitudinal spines
    for s_idx in range(6):
        v = (s_idx / 6) * math.tau
        rail_pts = [point((i / ribs) * math.tau, v) for i in range(ribs + 1)]
        for i in range(len(rail_pts) - 1):
            p1, p2 = rail_pts[i], rail_pts[i + 1]
            draw.line([(p1[0], p1[1]), (p2[0], p2[1])], fill=(255, 180, 140, 190), width=2)

    # Orbiting pulse nodes
    for q in range(8):
        pos = point((q / 8.0) * math.tau, (q * 0.45) * math.tau)
        draw.ellipse([pos[0] - 6, pos[1] - 6, pos[0] + 6, pos[1] + 6], fill=(255, 230, 210, 240), outline=(255, 107, 53, 255))

    im.save(os.path.join(PREVIEWS_DIR, "klein-bottle-3d.jpg"), quality=94)
    print("klein-bottle-3d.jpg saved")


def render_neural_synapse():
    """Shader-accurate 900x600 render of Neural Synapse."""
    w, h = 900, 600
    aspect = w / h
    x = np.linspace(-1.2 * aspect, 1.2 * aspect, w, dtype=np.float32)
    y = np.linspace(-1.2, 1.2, h, dtype=np.float32)
    xx, yy = np.meshgrid(x, y)

    r = np.sqrt(xx**2 + yy**2)
    a = np.arctan2(yy, xx)

    t = 2.4
    rot = t * 0.4
    p1_x = xx * np.cos(rot) - yy * np.sin(rot)
    p1_y = xx * np.sin(rot) + yy * np.cos(rot)

    density = 1.35
    grid_x = np.sin(p1_x * density * np.pi + math.sin(t * 0.6))
    grid_y = np.sin(p1_y * density * np.pi + math.cos(t * 0.7))
    axon = np.exp(-np.abs(grid_x * grid_y) * 14.0)

    wave = np.sin(r * 12.0 - t * 4.0 + a * 2.0)
    pulses = np.maximum(0.0, wave)**5.5 * np.exp(-r * 0.75) * 1.5

    soma = np.exp(-r**2 / (0.24**2 * 0.45)) * 1.9
    ring = np.exp(-np.abs(r - 0.55) * 8.0) * 0.9

    out = np.zeros((h, w, 3), dtype=np.float32)
    for c in range(3):
        out[:, :, c] = GROUND[c]
        out[:, :, c] += SIGNAL[c] * axon * (0.7 + 0.6 / (r + 0.3))
        # Traveling sparks
        pulse_col = SIGNAL[c] * 0.4 + EMBER[c] * 0.6
        out[:, :, c] += pulse_col * pulses * 1.8
        # Central soma and vesicle ring
        core_col = EMBER[c] * 0.3 + WHITE[c] * 0.7
        out[:, :, c] += core_col * (soma + ring)

    vig = np.clip((1.9 - r) / 0.9, 0.0, 1.0)
    for c in range(3):
        out[:, :, c] = out[:, :, c] * vig + GROUND[c] * (1.0 - vig)

    im = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))
    im.save(os.path.join(PREVIEWS_DIR, "neural-synapse.jpg"), quality=94)
    print("neural-synapse.jpg saved")


def render_dsp_visual(name, is_bell=True):
    """Render glowing audio DSP visualizer for synth / effect."""
    w, h = 900, 600
    im = Image.new("RGB", (w, h), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    # Grid lines
    for gy in range(80, h, 80):
        draw.line([(0, gy), (w, gy)], fill=(32, 38, 70, 100), width=1)
    for gx in range(90, w, 90):
        draw.line([(gx, 0), (gx, h)], fill=(32, 38, 70, 100), width=1)

    cx, cy = w / 2, h / 2

    if is_bell:
        # Oscilloscope waveform showing FM bell harmonics + decay envelope
        pts = []
        for i in range(w):
            frac = i / w
            t = frac * 28.0
            env = math.exp(-frac * 2.8)
            mod = math.sin(t * 2.756) * 3.4 * env
            sig = math.sin(t + mod) * env * 160.0
            pts.append((i, cy + sig))

        # Glow trail
        for i in range(len(pts) - 1):
            draw.line([pts[i], pts[i + 1]], fill=(167, 177, 255, 90), width=6)
            draw.line([pts[i], pts[i + 1]], fill=(255, 107, 53, 160), width=3)
            draw.line([pts[i], pts[i + 1]], fill=(255, 245, 235, 240), width=1)

        # Spectral chimes / harmonic partial spikes
        partials = [1.0, 2.756, 3.84, 5.42, 7.1, 9.8]
        for p_idx, p in enumerate(partials):
            fx = int(cx - 300 + p_idx * 120)
            amp = math.exp(-p_idx * 0.45) * 140
            draw.line([(fx, cy + 180), (fx, cy + 180 - amp)], fill=(255, 107, 53, 200), width=4)
            draw.ellipse([fx - 5, cy + 180 - amp - 5, fx + 5, cy + 180 - amp + 5], fill=(255, 230, 210, 255))
    else:
        # Shimmer delay multi-tap reflections and diffusion loops
        for tap in range(7):
            t_frac = tap / 7.0
            delay_x = 80 + tap * 110
            decay = math.exp(-t_frac * 2.0)
            amp = decay * 140

            # Shimmer waveform packet
            w_pts = []
            for dx in range(-45, 46):
                px = delay_x + dx
                env = math.cos(dx / 45.0 * math.pi * 0.5) ** 2
                wy = math.sin(dx * 0.35 + tap * 1.5) * amp * env
                w_pts.append((px, cy - 30 + wy))

            for i in range(len(w_pts) - 1):
                draw.line([w_pts[i], w_pts[i + 1]], fill=(167, 177, 255, 120), width=4)
                draw.line([w_pts[i], w_pts[i + 1]], fill=(255, 107, 53, 200), width=2)
                draw.line([w_pts[i], w_pts[i + 1]], fill=(255, 255, 255, 240), width=1)

            # Diffusion echo rings
            draw.ellipse([delay_x - 30, cy + 110 - amp * 0.5, delay_x + 30, cy + 110 + amp * 0.5], outline=(167, 177, 255, 140), width=2)

    # Save 900x600 preview jpg
    im.save(os.path.join(PREVIEWS_DIR, f"{name}.jpg"), quality=94)
    print(f"{name}.jpg saved")

    # Save 960x600 template webp
    im_webp = im.resize((960, 600), Image.Resampling.LANCZOS)
    im_webp.save(os.path.join(TEMPLATES_DIR, f"{name}.webp"), "WEBP", quality=90)
    print(f"{name}.webp saved")


if __name__ == "__main__":
    render_hopalong()
    render_klein()
    render_neural_synapse()
    render_dsp_visual("fm-bell-synth", is_bell=True)
    render_dsp_visual("shimmer-delay-fx", is_bell=False)
    print("All previews generated successfully!")
