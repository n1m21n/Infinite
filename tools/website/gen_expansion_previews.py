#!/usr/bin/env python3
"""Generate high-fidelity preview images for the 5 new library items.
Matches brand design: midnight ground #151930, signal #A7B1FF, ember #FF6B35.
"""
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
EMERALD = np.array([78, 242, 184], dtype=np.float32)  # #4EF2B8
WHITE = np.array([245, 248, 255], dtype=np.float32)


def render_gumowski_mira():
    """Gumowski-Mira chaotic attractor density plot."""
    w, h = 900, 600
    acc = np.zeros((h, w), dtype=np.float32)
    alpha = 0.008
    mu = -0.49
    x, y = 0.1, 0.1
    cx, cy = w / 2, h / 2
    k = min(w, h) * 0.040

    def g(val):
        return mu * val + (2.0 * (1.0 - mu) * val * val) / (1.0 + val * val)

    n_points = 1400000
    for _ in range(n_points):
        gx = g(x)
        nx = y + alpha * (1.0 - 0.05 * y * y) * y + gx
        gnx = g(nx)
        ny = -x + gnx
        x, y = nx, ny

        px = int(cx + x * k)
        py = int(cy + y * k)
        if 0 <= px < w and 0 <= py < h:
            acc[py, px] += 1.0

    acc = np.log1p(acc * 0.5)
    max_val = np.percentile(acc[acc > 0], 99.6) if np.any(acc > 0) else 1.0
    norm = np.clip(acc / max_val, 0.0, 1.0)

    # Slight blur for luminescence
    img_glow = Image.fromarray((norm * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.4))
    glow_arr = np.array(img_glow, dtype=np.float32) / 255.0

    out = np.zeros((h, w, 3), dtype=np.float32)
    for c_idx in range(3):
        out[:, :, c_idx] = GROUND[c_idx]

    for y_idx in range(h):
        for x_idx in range(w):
            v = norm[y_idx, x_idx]
            g_val = glow_arr[y_idx, x_idx]
            if v > 0 or g_val > 0:
                dist = math.sqrt((x_idx - cx)**2 + (y_idx - cy)**2) / (min(w, h) * 0.46)
                mix_col = SIGNAL * (1.0 - min(1.0, dist * 0.75)) + EMBER * min(1.0, dist * 0.75)
                col = mix_col * (v * 1.25 + g_val * 0.75) + WHITE * (v**2.2 * 1.6)
                out[y_idx, x_idx] = np.clip(GROUND * (1.0 - min(1.0, v + g_val)) + col, 0, 255)

    im = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))
    im.save(os.path.join(PREVIEWS_DIR, "gumowski-mira.jpg"), quality=94)
    print("gumowski-mira.jpg saved")


def render_enneper():
    """Parametric Alfred Enneper minimal surface 3D projection."""
    w, h = 900, 600
    im = Image.new("RGB", (w, h), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    rotY = 0.75
    rotX = 0.52
    cy, cx = h / 2, w / 2

    def point(u, v):
        # Enneper surface
        x = u - (u**3) / 3.0 + u * (v**2)
        y = v - (v**3) / 3.0 + v * (u**2)
        z = u**2 - v**2

        sc = 0.45
        x, y, z = x * sc, y * sc, z * sc

        # Rotate Y
        x1 = x * math.cos(rotY) + z * math.sin(rotY)
        z1 = -x * math.sin(rotY) + z * math.cos(rotY)
        # Rotate X
        y2 = y * math.cos(rotX) - z1 * math.sin(rotX)
        z2 = y * math.sin(rotX) + z1 * math.cos(rotX)

        fov = 3.4
        dist = 4.2 - z2
        px = cx + (x1 / dist) * fov * 240
        py = cy - (y2 / dist) * fov * 240
        return px, py, z2

    ribs_u = 48
    segs_v = 36
    r_bound = 1.65

    # Collect u-ribs and v-ribs sorted by depth
    lines = []
    for i in range(ribs_u + 1):
        frac_u = i / ribs_u
        u = -r_bound + frac_u * 2.0 * r_bound
        pts = [point(u, -r_bound + (j / segs_v) * 2.0 * r_bound) for j in range(segs_v + 1)]
        avg_z = sum(p[2] for p in pts) / len(pts)
        lines.append((avg_z, frac_u, pts, False))

    for j in range(0, segs_v + 1, 2):
        frac_v = j / segs_v
        v = -r_bound + frac_v * 2.0 * r_bound
        pts = [point(-r_bound + (i / ribs_u) * 2.0 * r_bound, v) for i in range(ribs_u + 1)]
        avg_z = sum(p[2] for p in pts) / len(pts)
        lines.append((avg_z, frac_v, pts, True))

    lines.sort(key=lambda r: r[0])

    for avg_z, frac, pts, is_v in lines:
        alpha = int(np.clip(130 + avg_z * 90, 40, 255))
        if is_v:
            col_mix = EMBER * (1.0 - frac * 0.5) + WHITE * (frac * 0.5)
        else:
            col_mix = SIGNAL * (1.0 - frac) + EMBER * frac
        r, g, b = [int(np.clip(c, 0, 255)) for c in col_mix]
        w_line = 2 if not is_v else 1
        for idx in range(len(pts) - 1):
            p1, p2 = pts[idx], pts[idx + 1]
            draw.line([(p1[0], p1[1]), (p2[0], p2[1])], fill=(r, g, b, alpha), width=w_line)

    # Saddle energy nodes
    for q in range(8):
        ang = (q / 8.0) * math.tau
        qu = math.sin(ang) * 1.3
        qv = math.cos(ang * 1.5) * 1.3
        pos = point(qu, qv)
        draw.ellipse([pos[0] - 6, pos[1] - 6, pos[0] + 6, pos[1] + 6], fill=(255, 235, 220, 245), outline=(255, 107, 53, 255))

    im.save(os.path.join(PREVIEWS_DIR, "enneper-surface-3d.jpg"), quality=94)
    print("enneper-surface-3d.jpg saved")


def render_phase_distortion():
    """Render Phase Distortion Synth preview and webp template."""
    w, h = 900, 600
    im = Image.new("RGB", (w, h), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    for gy in range(80, h, 80):
        draw.line([(0, gy), (w, gy)], fill=(32, 38, 70, 100), width=1)
    for gx in range(90, w, 90):
        draw.line([(gx, 0), (gx, h)], fill=(32, 38, 70, 100), width=1)

    cy = h / 2

    # Draw Casio CZ resonant windowed waveform
    pts = []
    for i in range(w):
        frac = i / w
        cycle = (frac * 8.0) % 1.0
        env = math.exp(-frac * 2.2)
        # Resonant phase distortion
        res_phase = (cycle * 5.2) % 1.0
        res_window = 1.0 - cycle
        cz_wave = math.sin(res_phase * math.tau) * res_window
        sub = math.sin(frac * 8.0 * math.pi) * 0.35
        sig = (cz_wave + sub) * env * 175.0
        pts.append((i, cy + sig))

    for i in range(len(pts) - 1):
        draw.line([pts[i], pts[i + 1]], fill=(167, 177, 255, 90), width=6)
        draw.line([pts[i], pts[i + 1]], fill=(255, 107, 53, 160), width=3)
        draw.line([pts[i], pts[i + 1]], fill=(255, 250, 240, 240), width=1)

    # Draw resonant peak harmonic spectrum bars
    peaks = [1.0, 2.0, 3.0, 5.2, 6.2, 8.4, 11.0]
    for idx, p in enumerate(peaks):
        fx = int(120 + idx * 105)
        amp = math.exp(-abs(p - 5.2) * 0.4) * 150
        draw.line([(fx, cy + 180), (fx, cy + 180 - amp)], fill=(255, 107, 53, 210), width=5)
        draw.ellipse([fx - 5, cy + 180 - amp - 5, fx + 5, cy + 180 - amp + 5], fill=(255, 235, 215, 255))

    im.save(os.path.join(PREVIEWS_DIR, "phase-distortion-synth.jpg"), quality=94)
    print("phase-distortion-synth.jpg saved")

    im_webp = im.resize((960, 600), Image.Resampling.LANCZOS)
    im_webp.save(os.path.join(TEMPLATES_DIR, "phase-distortion-synth.webp"), "WEBP", quality=90)
    print("phase-distortion-synth.webp saved")


def render_formant_filter():
    """Render Formant Filter FX preview and webp template."""
    w, h = 900, 600
    im = Image.new("RGB", (w, h), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    for gy in range(80, h, 80):
        draw.line([(0, gy), (w, gy)], fill=(32, 38, 70, 100), width=1)
    for gx in range(90, w, 90):
        draw.line([(gx, 0), (gx, h)], fill=(32, 38, 70, 100), width=1)

    cy = h / 2

    # Draw dual resonant vowel formant spectrum curve
    f_curve = []
    peak1_x = 280  # F1 (~550 Hz)
    peak2_x = 580  # F2 (~1800 Hz)

    for x in range(w):
        d1 = (x - peak1_x) / 45.0
        d2 = (x - peak2_x) / 75.0
        v1 = math.exp(-d1**2) * 165.0
        v2 = math.exp(-d2**2) * 135.0
        v = v1 + v2 + 25.0 * math.exp(-((x - 100) / 120.0)**2)
        f_curve.append((x, cy + 150 - v))

    for i in range(len(f_curve) - 1):
        draw.line([f_curve[i], f_curve[i + 1]], fill=(167, 177, 255, 100), width=5)
        draw.line([f_curve[i], f_curve[i + 1]], fill=(78, 242, 184, 180), width=3)
        draw.line([f_curve[i], f_curve[i + 1]], fill=(255, 255, 255, 240), width=1)

    # Draw formant cavity resonance rings
    for c_center in (peak1_x, peak2_x):
        for ring_idx in range(4):
            rad = 20 + ring_idx * 28
            alp = int(180 - ring_idx * 40)
            draw.ellipse([c_center - rad, cy - 80 - rad * 0.6, c_center + rad, cy - 80 + rad * 0.6], outline=(255, 107, 53, alp), width=2)
        draw.ellipse([c_center - 6, cy - 80 - 6, c_center + 6, cy - 80 + 6], fill=(255, 235, 215, 255))

    im.save(os.path.join(PREVIEWS_DIR, "formant-filter-fx.jpg"), quality=94)
    print("formant-filter-fx.jpg saved")

    im_webp = im.resize((960, 600), Image.Resampling.LANCZOS)
    im_webp.save(os.path.join(TEMPLATES_DIR, "formant-filter-fx.webp"), "WEBP", quality=90)
    print("formant-filter-fx.webp saved")


def render_aurora_borealis():
    """Shader-accurate 900x600 render of Aurora Borealis."""
    w, h = 900, 600
    aspect = w / h
    x = np.linspace(-1.1 * aspect, 1.1 * aspect, w, dtype=np.float32)
    y = np.linspace(-1.1, 1.1, h, dtype=np.float32)
    xx, yy = np.meshgrid(x, y)

    t = 2.8
    speed = 1.4
    t_cur = t * speed * 0.35

    warp1 = np.sin(xx * 2.2 + t_cur * 1.8) * 0.28
    warp2 = np.cos(xx * 3.5 - t_cur * 1.2) * 0.16
    warp_y = yy + warp1 + warp2

    curtain1 = np.exp(-np.abs(warp_y) * (5.5 + np.sin(xx * 2.5 + t_cur) * 2.0))
    curtain2 = np.exp(-np.abs(warp_y + 0.32) * (7.0 + np.cos(xx * 4.0 - t_cur * 1.5) * 2.5)) * 0.65
    curtain = curtain1 + curtain2

    rays = np.sin(xx * 26.0 + t_cur * 3.2 + warp_y * 9.0)
    ray_sparks = np.maximum(0.0, rays)**4.2 * 1.35 * np.exp(-np.abs(yy) * 1.6)

    r = np.sqrt(xx**2 + yy**2)
    corona = np.exp(-r**2 * 2.2) * 0.45

    out = np.zeros((h, w, 3), dtype=np.float32)
    for c in range(3):
        out[:, :, c] = GROUND[c]

    # Ion gradient: emerald to signal
    for y_idx in range(h):
        for x_idx in range(w):
            py = yy[y_idx, x_idx]
            px = xx[y_idx, x_idx]
            curt = curtain[y_idx, x_idx]
            spk = ray_sparks[y_idx, x_idx]
            cor = corona[y_idx, x_idx]

            frac_y = np.clip(py * 1.2 + 0.5, 0.0, 1.0)
            ion_grad = EMERALD * (1.0 - frac_y) + SIGNAL * frac_y
            shift = (math.sin(t_cur * 0.8 + px) * 0.3 + 0.2)
            aurora_col = ion_grad * (1.0 - shift) + EMBER * shift

            col = aurora_col * curt * 1.8 + (EMERALD * 0.4 + WHITE * 0.6) * spk * 1.4 + SIGNAL * cor
            rad_falloff = np.clip((2.0 - r[y_idx, x_idx]) / 1.1, 0.0, 1.0)

            out[y_idx, x_idx] = np.clip(GROUND * (1.0 - np.clip(curt * 0.9 + spk * 0.5, 0, 1)) + col, 0, 255) * rad_falloff + GROUND * (1.0 - rad_falloff)

    im = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))
    im.save(os.path.join(PREVIEWS_DIR, "aurora-borealis.jpg"), quality=94)
    print("aurora-borealis.jpg saved")


def render_ikeda_attractor():
    """Ikeda laser resonator attractor density plot."""
    w, h = 900, 600
    acc = np.zeros((h, w), dtype=np.float32)
    u = 0.918
    c1 = 0.4
    c2 = 6.0
    x, y = 0.1, 0.1
    cx, cy = w * 0.44, h * 0.52
    k = min(w, h) * 0.165

    n_points = 1500000
    for _ in range(n_points):
        r2 = x * x + y * y
        tn = c1 - c2 / (1.0 + r2)
        cos_t = math.cos(tn)
        sin_t = math.sin(tn)
        nx = 1.0 + u * (x * cos_t - y * sin_t)
        ny = u * (x * sin_t + y * cos_t)
        x, y = nx, ny

        px = int(cx + x * k)
        py = int(cy + y * k)
        if 0 <= px < w and 0 <= py < h:
            acc[py, px] += 1.0

    acc = np.log1p(acc * 0.6)
    max_val = np.percentile(acc[acc > 0], 99.7) if np.any(acc > 0) else 1.0
    norm = np.clip(acc / max_val, 0.0, 1.0)

    # Slight blur for luminescence
    img_glow = Image.fromarray((norm * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.4))
    glow_arr = np.array(img_glow, dtype=np.float32) / 255.0

    out = np.zeros((h, w, 3), dtype=np.float32)
    for c_idx in range(3):
        out[:, :, c_idx] = GROUND[c_idx]

    for y_idx in range(h):
        for x_idx in range(w):
            v = norm[y_idx, x_idx]
            g_val = glow_arr[y_idx, x_idx]
            if v > 0 or g_val > 0:
                dist = math.sqrt((x_idx - cx)**2 + (y_idx - cy)**2) / (min(w, h) * 0.48)
                mix_col = SIGNAL * (1.0 - min(1.0, dist * 0.7)) + EMBER * min(1.0, dist * 0.7)
                col = mix_col * (v * 1.3 + g_val * 0.7) + WHITE * (v**2.2 * 1.6)
                out[y_idx, x_idx] = np.clip(GROUND * (1.0 - min(1.0, v + g_val)) + col, 0, 255)

    im = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))
    im.save(os.path.join(PREVIEWS_DIR, "ikeda-attractor.jpg"), quality=94)
    print("ikeda-attractor.jpg saved")


def render_dini_surface():
    """Parametric Dini pseudospherical helicoid minimal surface 3D projection."""
    w, h = 900, 600
    im = Image.new("RGB", (w, h), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    rotY = 0.82
    rotX = 0.45
    rotZ = 0.18
    cy, cx = h / 2 + 10, w / 2

    sc = 0.72
    pitch = 0.16
    turns = 2.4
    max_u = turns * math.tau

    def point(u, v):
        sin_v = math.sin(v)
        x = sc * math.cos(u) * sin_v
        z = sc * math.sin(u) * sin_v
        y = sc * (math.cos(v) + math.log(math.tan(v * 0.5))) + pitch * u - 0.48

        # Rotate Y
        x1 = x * math.cos(rotY) + z * math.sin(rotY)
        z1 = -x * math.sin(rotY) + z * math.cos(rotY)
        # Rotate X
        y2 = y * math.cos(rotX) - z1 * math.sin(rotX)
        z2 = y * math.sin(rotX) + z1 * math.cos(rotX)
        # Rotate Z
        x3 = x1 * math.cos(rotZ) - y2 * math.sin(rotZ)
        y3 = x1 * math.sin(rotZ) + y2 * math.cos(rotZ)

        fov = 3.6
        dist = 4.5 - z2
        px = cx + (x3 / dist) * fov * 250
        py = cy - (y3 / dist) * fov * 250
        return px, py, z2

    ribs_u = 52
    segs_v = 30
    v_min, v_max = 0.09, 1.95

    lines = []
    # Longitudinal u-ribs
    for j in range(0, segs_v + 1, 2):
        frac_v = j / segs_v
        v = v_min + frac_v * (v_max - v_min)
        pts = [point((i / ribs_u) * max_u, v) for i in range(ribs_u + 1)]
        avg_z = sum(p[2] for p in pts) / len(pts)
        lines.append((avg_z, frac_v, pts, True))

    # Transverse v-rings
    for i in range(0, ribs_u + 1, 3):
        frac_u = i / ribs_u
        u = frac_u * max_u
        pts = [point(u, v_min + (j / segs_v) * (v_max - v_min)) for j in range(segs_v + 1)]
        avg_z = sum(p[2] for p in pts) / len(pts)
        lines.append((avg_z, frac_u, pts, False))

    lines.sort(key=lambda r: r[0])

    for avg_z, frac, pts, is_long in lines:
        alpha = int(np.clip(135 + avg_z * 95, 40, 255))
        if is_long:
            col_mix = SIGNAL * (1.0 - frac * 0.6) + EMBER * (frac * 0.6)
        else:
            col_mix = EMERALD * (1.0 - frac * 0.7) + WHITE * (frac * 0.7)
        r, g, b = [int(np.clip(c, 0, 255)) for c in col_mix]
        w_line = 2 if is_long else 1
        for idx in range(len(pts) - 1):
            p1, p2 = pts[idx], pts[idx + 1]
            draw.line([(p1[0], p1[1]), (p2[0], p2[1])], fill=(r, g, b, alpha), width=w_line)

    # Helical ridge quanta
    for q in range(7):
        ph = (q / 7.0)
        qu = ph * max_u
        qv = v_min + (0.5 + 0.45 * math.sin(q * 1.3)) * (v_max - v_min)
        pos = point(qu, qv)
        draw.ellipse([pos[0] - 6, pos[1] - 6, pos[0] + 6, pos[1] + 6], fill=(255, 235, 220, 245), outline=(255, 107, 53, 255))

    im.save(os.path.join(PREVIEWS_DIR, "dini-surface-3d.jpg"), quality=94)
    print("dini-surface-3d.jpg saved")


def render_wavetable_morph():
    """Render Wavetable Morph Synth preview and webp template."""
    w, h = 900, 600
    im = Image.new("RGB", (w, h), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    for gy in range(80, h, 80):
        draw.line([(0, gy), (w, gy)], fill=(32, 38, 70, 100), width=1)
    for gx in range(90, w, 90):
        draw.line([(gx, 0), (gx, h)], fill=(32, 38, 70, 100), width=1)

    cy = h / 2 - 20

    # Draw morphing wavetable waveform with continuous harmonic evolution
    pts = []
    for i in range(w):
        frac = i / w
        cycle = (frac * 6.0) % 1.0
        env = math.exp(-frac * 1.8)

        # Sine to saw to pulsed edge morph
        w_sine = math.sin(cycle * math.tau)
        w_saw = math.sin(cycle * math.tau) + 0.45 * math.sin(cycle * 2.0 * math.tau) + 0.22 * math.sin(cycle * 3.0 * math.tau)
        w_pulse = (math.sin(cycle * math.tau) + 0.6 * math.sin(cycle * 3.0 * math.tau) + 0.4 * math.sin(cycle * 5.0 * math.tau)) * 0.7

        morph = math.sin(frac * math.pi * 3.0) * 0.5 + 0.5
        t1 = min(1.0, morph * 2.0)
        s1 = w_sine * (1.0 - t1) + w_saw * t1
        t2 = max(0.0, (morph - 0.5) * 2.0)
        osc = s1 * (1.0 - t2) + w_pulse * t2

        sub = math.sin(frac * 6.0 * math.pi) * 0.38
        sig = (osc + sub) * env * 165.0
        pts.append((i, cy + sig))

    for i in range(len(pts) - 1):
        draw.line([pts[i], pts[i + 1]], fill=(167, 177, 255, 90), width=6)
        draw.line([pts[i], pts[i + 1]], fill=(78, 242, 184, 160), width=3)
        draw.line([pts[i], pts[i + 1]], fill=(255, 250, 240, 240), width=1)

    # Draw wavetable harmonic column spectrum
    harmonics = [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0]
    for idx, h_num in enumerate(harmonics):
        fx = int(110 + idx * 95)
        amp = (1.0 / math.sqrt(h_num)) * 140
        draw.line([(fx, cy + 200), (fx, cy + 200 - amp)], fill=(167, 177, 255, 200), width=6)
        draw.ellipse([fx - 5, cy + 200 - amp - 5, fx + 5, cy + 200 - amp + 5], fill=(255, 235, 215, 255))

    im.save(os.path.join(PREVIEWS_DIR, "wavetable-morph-synth.jpg"), quality=94)
    print("wavetable-morph-synth.jpg saved")

    im_webp = im.resize((960, 600), Image.Resampling.LANCZOS)
    im_webp.save(os.path.join(TEMPLATES_DIR, "wavetable-morph-synth.webp"), "WEBP", quality=90)
    print("wavetable-morph-synth.webp saved")


def render_wavefolder_drive():
    """Render Wavefolder Drive FX preview and webp template."""
    w, h = 900, 600
    im = Image.new("RGB", (w, h), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    for gy in range(80, h, 80):
        draw.line([(0, gy), (w, gy)], fill=(32, 38, 70, 100), width=1)
    for gx in range(90, w, 90):
        draw.line([(gx, 0), (gx, h)], fill=(32, 38, 70, 100), width=1)

    cy = h / 2 - 20

    # Draw Buchla-style wavefolded sine waveform
    pts = []
    for i in range(w):
        frac = i / w
        cycle = math.sin(frac * 8.0 * math.pi)
        drive = 2.8 + math.sin(frac * 2.0 * math.pi) * 1.2
        u = cycle * drive + 0.15

        fold1 = math.sin(u * 1.5707963)
        fold2 = math.sin(fold1 * 2.3561945 + u * 0.785398)
        fold3 = math.sin(fold2 * 3.1415926 + fold1 * 0.5)

        folded = fold1 * 0.25 + fold2 * 0.45 + fold3 * 0.3
        sig = folded * 155.0
        pts.append((i, cy + sig))

    for i in range(len(pts) - 1):
        draw.line([pts[i], pts[i + 1]], fill=(255, 107, 53, 90), width=6)
        draw.line([pts[i], pts[i + 1]], fill=(167, 177, 255, 170), width=3)
        draw.line([pts[i], pts[i + 1]], fill=(255, 255, 255, 240), width=1)

    # Draw folding harmonic transfer curve in corner
    cx_box = 720
    cy_box = cy + 130
    draw.rectangle([cx_box - 100, cy_box - 70, cx_box + 100, cy_box + 70], outline=(78, 242, 184, 180), width=1)
    t_pts = []
    for bx in range(-90, 91):
        in_v = (bx / 90.0) * 3.2
        out_v = math.sin(in_v * 1.57) * 0.5 + math.sin(in_v * 3.14) * 0.4
        t_pts.append((cx_box + bx, cy_box - out_v * 50))
    for i in range(len(t_pts) - 1):
        draw.line([t_pts[i], t_pts[i + 1]], fill=(78, 242, 184, 220), width=2)

    im.save(os.path.join(PREVIEWS_DIR, "wavefolder-drive-fx.jpg"), quality=94)
    print("wavefolder-drive-fx.jpg saved")

    im_webp = im.resize((960, 600), Image.Resampling.LANCZOS)
    im_webp.save(os.path.join(TEMPLATES_DIR, "wavefolder-drive-fx.webp"), "WEBP", quality=90)
    print("wavefolder-drive-fx.webp saved")


def render_cosmic_nebula():
    """Shader-accurate 900x600 render of Cosmic Nebula."""
    w, h = 900, 600
    aspect = w / h
    x = np.linspace(-1.1 * aspect, 1.1 * aspect, w, dtype=np.float32)
    y = np.linspace(-1.1, 1.1, h, dtype=np.float32)
    xx, yy = np.meshgrid(x, y)

    r = np.sqrt(xx**2 + yy**2)
    ang = np.arctan2(yy, xx)
    t_cur = 2.4 * 0.28

    swirl = ang + r * 1.8 - t_cur
    p_swirl_x = np.cos(swirl) * r
    p_swirl_y = np.sin(swirl) * r

    f1 = np.sin(p_swirl_x * 2.6 + t_cur * 1.4) * np.cos(p_swirl_y * 2.6 - t_cur * 0.9)
    f2 = np.sin(p_swirl_x * 5.5 - t_cur * 1.8) * np.cos(p_swirl_y * 5.5 + t_cur * 1.2)
    filaments = np.exp(-np.abs(f1 + f2 * 0.45) * 3.4)

    core_r = r / 0.45
    core = np.exp(-core_r * core_r * 2.5) * 1.6

    stars = np.maximum(0.0, np.sin(xx * 48.0 + t_cur * 4.0) * np.sin(yy * 48.0 - t_cur * 3.5))**8.0
    stars *= np.exp(-r * 1.2) * 1.8

    out = np.zeros((h, w, 3), dtype=np.float32)
    for c in range(3):
        out[:, :, c] = GROUND[c]

    for y_idx in range(h):
        for x_idx in range(w):
            rad = r[y_idx, x_idx]
            theta = ang[y_idx, x_idx]
            fil = filaments[y_idx, x_idx]
            cor = core[y_idx, x_idx]
            st = stars[y_idx, x_idx]

            frac = np.clip(rad * 1.1 + math.sin(theta * 3.0 + t_cur) * 0.35, 0.0, 1.0)
            neb_grad = SIGNAL * (1.0 - frac) + EMBER * frac
            neb_col = neb_grad * 0.7 + EMERALD * 0.3

            col = neb_col * fil * 1.6 + (EMBER * 0.35 + WHITE * 0.65) * cor + WHITE * st * 1.5
            rad_falloff = np.clip((2.2 - rad) / 1.1, 0.0, 1.0)

            out[y_idx, x_idx] = np.clip(GROUND * (1.0 - np.clip(fil * 0.8 + cor * 0.6, 0, 1)) + col, 0, 255) * rad_falloff + GROUND * (1.0 - rad_falloff)

    im = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))
    im.save(os.path.join(PREVIEWS_DIR, "cosmic-nebula.jpg"), quality=94)
    print("cosmic-nebula.jpg saved")


def render_svensson_attractor():
    """Svensson chaotic attractor density plot."""
    w, h = 900, 600
    acc = np.zeros((h, w), dtype=np.float32)
    a = 1.40
    b = 1.56
    c = 1.40
    d = -1.56
    x, y = 0.1, 0.1
    cx, cy = w * 0.5, h * 0.5
    k = min(w, h) * 0.185

    n_points = 1800000
    for _ in range(n_points):
        nx = d * math.sin(a * x) - math.sin(b * y)
        ny = c * math.cos(a * x) + math.cos(b * y)
        x, y = nx, ny

        px = int(cx + x * k)
        py = int(cy + y * k)
        if 0 <= px < w and 0 <= py < h:
            acc[py, px] += 1.0

    acc = np.log1p(acc * 0.55)
    max_val = np.percentile(acc[acc > 0], 99.7) if np.any(acc > 0) else 1.0
    norm = np.clip(acc / max_val, 0.0, 1.0)

    # Slight blur for luminescence
    img_glow = Image.fromarray((norm * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.4))
    glow_arr = np.array(img_glow, dtype=np.float32) / 255.0

    out = np.zeros((h, w, 3), dtype=np.float32)
    for c_idx in range(3):
        out[:, :, c_idx] = GROUND[c_idx]

    for y_idx in range(h):
        for x_idx in range(w):
            v = norm[y_idx, x_idx]
            g_val = glow_arr[y_idx, x_idx]
            if v > 0 or g_val > 0:
                dist = math.sqrt((x_idx - cx)**2 + (y_idx - cy)**2) / (min(w, h) * 0.46)
                mix_col = SIGNAL * (1.0 - min(1.0, dist * 0.72)) + EMBER * min(1.0, dist * 0.72)
                col = mix_col * (v * 1.35 + g_val * 0.7) + WHITE * (v**2.2 * 1.5)
                out[y_idx, x_idx] = np.clip(GROUND * (1.0 - min(1.0, v + g_val)) + col, 0, 255)

    im = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))
    im.save(os.path.join(PREVIEWS_DIR, "svensson-attractor.jpg"), quality=94)
    print("svensson-attractor.jpg saved")


def render_bour_surface():
    """Parametric Edmond Bour minimal surface 3D projection."""
    w, h = 900, 600
    im = Image.new("RGB", (w, h), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    rotY = 0.72
    rotX = 0.55
    cy, cx = h / 2, w / 2

    def point(u, v):
        cosV = math.cos(v)
        sinV = math.sin(v)
        cos2V = math.cos(2.0 * v)
        sin2V = math.sin(2.0 * v)
        cos15V = math.cos(1.5 * v)
        u15 = u * math.sqrt(max(0.0, u))

        sc = 1.05
        x = sc * (u * cosV - 0.5 * u * u * cos2V)
        z = sc * (-u * sinV - 0.5 * u * u * sin2V)
        y = sc * (1.3333333 * u15 * cos15V) - 0.15

        # Rotate Y
        x1 = x * math.cos(rotY) + z * math.sin(rotY)
        z1 = -x * math.sin(rotY) + z * math.cos(rotY)
        # Rotate X
        y2 = y * math.cos(rotX) - z1 * math.sin(rotX)
        z2 = y * math.sin(rotX) + z1 * math.cos(rotX)

        fov = 3.6
        pz = z2 + 3.4
        f = fov / pz if pz > 0.05 else 1.0
        sx = cx + x1 * f * 260.0
        sy = cy + y2 * f * 260.0
        return (sx, sy, z2)

    u_min, u_max = 0.06, 1.35
    max_v = math.tau
    u_count = 38
    v_count = 48

    lines = []
    # Longitudinal ribs
    for j in range(0, v_count + 1, 2):
        frac_v = j / v_count
        v = frac_v * max_v
        pts = [point(u_min + (i / u_count) * (u_max - u_min), v) for i in range(u_count + 1)]
        avg_z = sum(p[2] for p in pts) / len(pts)
        lines.append((avg_z, frac_v, pts, True))

    # Transversal rings
    for i in range(1, u_count + 1, 3):
        frac_u = i / u_count
        u = u_min + frac_u * (u_max - u_min)
        pts = [point(u, (j / v_count) * max_v) for j in range(v_count + 1)]
        avg_z = sum(p[2] for p in pts) / len(pts)
        lines.append((avg_z, frac_u, pts, False))

    lines.sort(key=lambda r: r[0])

    for avg_z, frac, pts, is_long in lines:
        alpha = int(np.clip(140 + avg_z * 90, 40, 255))
        if is_long:
            col_mix = SIGNAL * (1.0 - frac * 0.5) + EMERALD * (frac * 0.5)
        else:
            col_mix = EMBER * (1.0 - frac * 0.6) + WHITE * (frac * 0.6)
        r, g, b = [int(np.clip(c, 0, 255)) for c in col_mix]
        w_line = 2 if is_long else 1
        for idx in range(len(pts) - 1):
            p1, p2 = pts[idx], pts[idx + 1]
            draw.line([(p1[0], p1[1]), (p2[0], p2[1])], fill=(r, g, b, alpha), width=w_line)

    # Petal boundary quanta
    for q in range(6):
        ph = q / 6.0
        qv = ph * max_v
        qu = u_min + (0.55 + 0.4 * math.sin(q * 1.5)) * (u_max - u_min)
        pos = point(qu, qv)
        draw.ellipse([pos[0] - 6, pos[1] - 6, pos[0] + 6, pos[1] + 6], fill=(255, 240, 225, 245), outline=(255, 107, 53, 255))

    im.save(os.path.join(PREVIEWS_DIR, "bour-surface-3d.jpg"), quality=94)
    print("bour-surface-3d.jpg saved")


def render_additive_harmonic():
    """Render Additive Harmonic Synth preview and webp template."""
    w, h = 900, 600
    im = Image.new("RGB", (w, h), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    for gy in range(80, h, 80):
        draw.line([(0, gy), (w, gy)], fill=(32, 38, 70, 100), width=1)
    for gx in range(90, w, 90):
        draw.line([(gx, 0), (gx, h)], fill=(32, 38, 70, 100), width=1)

    cy = h / 2 - 20

    # Draw additive harmonic overtone composite waveform
    pts = []
    tilt = 1.25
    for i in range(w):
        frac = i / w
        cycle = (frac * 5.0) % 1.0
        env = math.exp(-frac * 1.5)

        sig_sum = 0.0
        for k in range(1, 9):
            amp_k = (1.0 / (k ** tilt))
            if k % 2 == 0:
                amp_k *= 0.65
            if k >= 7:
                amp_k *= 1.4
            sig_sum += math.sin(cycle * k * math.tau) * amp_k

        sig = sig_sum * env * 140.0
        pts.append((i, cy + sig))

    for i in range(len(pts) - 1):
        draw.line([pts[i], pts[i + 1]], fill=(167, 177, 255, 90), width=6)
        draw.line([pts[i], pts[i + 1]], fill=(78, 242, 184, 160), width=3)
        draw.line([pts[i], pts[i + 1]], fill=(255, 250, 240, 240), width=1)

    # Draw 8 individual additive harmonic bars
    for k in range(1, 9):
        fx = int(95 + (k - 1) * 95)
        bar_h = int((1.0 / (k ** 1.1)) * 170.0)
        draw.line([(fx, cy + 200), (fx, cy + 200 - bar_h)], fill=(167, 177, 255, 210), width=8)
        draw.ellipse([fx - 6, cy + 200 - bar_h - 6, fx + 6, cy + 200 - bar_h + 6], fill=(255, 107, 53, 255))

    im.save(os.path.join(PREVIEWS_DIR, "additive-harmonic-synth.jpg"), quality=94)
    print("additive-harmonic-synth.jpg saved")

    im_webp = im.resize((960, 600), Image.Resampling.LANCZOS)
    im_webp.save(os.path.join(TEMPLATES_DIR, "additive-harmonic-synth.webp"), "WEBP", quality=90)
    print("additive-harmonic-synth.webp saved")


def render_comb_resonator():
    """Render Comb Resonator FX preview and webp template."""
    w, h = 900, 600
    im = Image.new("RGB", (w, h), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    for gy in range(80, h, 80):
        draw.line([(0, gy), (w, gy)], fill=(32, 38, 70, 100), width=1)
    for gx in range(90, w, 90):
        draw.line([(gx, 0), (gx, h)], fill=(32, 38, 70, 100), width=1)

    cy = h / 2 - 20

    # Draw tuned comb resonator impulse response with harmonic ringing
    pts = []
    for i in range(w):
        frac = i / w
        time_ms = frac * 20.0
        env = math.exp(-frac * 3.5)
        res1 = math.sin(time_ms * 4.4 * math.tau) * 0.65
        res2 = math.sin(time_ms * 6.6 * math.tau) * 0.45
        res3 = math.sin(time_ms * 8.8 * math.tau) * 0.25

        sig = (res1 + res2 + res3) * env * 160.0
        pts.append((i, cy + sig))

    for i in range(len(pts) - 1):
        draw.line([pts[i], pts[i + 1]], fill=(78, 242, 184, 90), width=6)
        draw.line([pts[i], pts[i + 1]], fill=(167, 177, 255, 160), width=3)
        draw.line([pts[i], pts[i + 1]], fill=(255, 255, 255, 240), width=1)

    # Draw tuned comb peaks response curve in corner box
    cx_box = 720
    cy_box = cy + 130
    draw.rectangle([cx_box - 100, cy_box - 70, cx_box + 100, cy_box + 70], outline=(255, 107, 53, 180), width=1)
    c_pts = []
    for bx in range(-90, 91):
        freq_ratio = (bx + 90) / 180.0 * 6.0
        comb_peak = abs(math.cos(freq_ratio * math.pi)) ** 8.0
        c_pts.append((cx_box + bx, cy_box + 60 - comb_peak * 110.0))
    for i in range(len(c_pts) - 1):
        draw.line([c_pts[i], c_pts[i + 1]], fill=(255, 107, 53, 230), width=2)

    im.save(os.path.join(PREVIEWS_DIR, "comb-resonator-fx.jpg"), quality=94)
    print("comb-resonator-fx.jpg saved")

    im_webp = im.resize((960, 600), Image.Resampling.LANCZOS)
    im_webp.save(os.path.join(TEMPLATES_DIR, "comb-resonator-fx.webp"), "WEBP", quality=90)
    print("comb-resonator-fx.webp saved")


def render_spectral_caustics():
    """Shader-accurate 900x600 render of Spectral Caustics."""
    w, h = 900, 600
    aspect = w / h
    x = np.linspace(-1.1 * aspect, 1.1 * aspect, w, dtype=np.float32)
    y = np.linspace(-1.1, 1.1, h, dtype=np.float32)
    xx, yy = np.meshgrid(x, y)

    r = np.sqrt(xx**2 + yy**2)
    t_cur = 2.4 * 0.30

    def calc_wave(disp):
        coord_x = xx * (1.0 + disp * 0.08)
        coord_y = yy * (1.0 + disp * 0.08)
        w1 = np.sin(coord_x * 3.4 + coord_y * 2.1 + t_cur * 1.25)
        w2 = np.sin(-coord_x * 2.6 + coord_y * 3.7 - t_cur * 0.95)
        w3 = np.cos(coord_x * 4.8 - coord_y * 3.2 + t_cur * 1.55)
        w4 = np.sin(coord_x * 6.6 + coord_y * 6.1 - t_cur * 1.85)
        wave = w1 * 0.38 + w2 * 0.32 + w3 * 0.22 + w4 * 0.12
        return np.maximum(0.0, 1.0 - np.abs(wave))**5.5

    c_r = calc_wave(-0.06)
    c_g = calc_wave(0.0)
    c_b = calc_wave(0.06)
    caustic_sum = (c_r + c_g + c_b) * 0.3333333

    core = np.exp(-r * r * 1.6) * 1.2
    sparkles = np.maximum(0.0, np.sin(xx * 36.0 + t_cur * 3.5) * np.sin(yy * 36.0 - t_cur * 3.0))**7.0
    sparkles *= np.exp(-r * 1.1) * 1.6

    out = np.zeros((h, w, 3), dtype=np.float32)
    for c in range(3):
        out[:, :, c] = GROUND[c]

    for y_idx in range(h):
        for x_idx in range(w):
            rad = r[y_idx, x_idx]
            cr = c_r[y_idx, x_idx]
            cg = c_g[y_idx, x_idx]
            cb = c_b[y_idx, x_idx]
            c_sum = caustic_sum[y_idx, x_idx]
            cor = core[y_idx, x_idx]
            sp = sparkles[y_idx, x_idx]

            caustic_col = np.array([cr * 0.9 + cg * 0.2, cg * 0.95 + cb * 0.2, cb * 1.1 + cr * 0.1], dtype=np.float32)
            caustic_col = caustic_col * EMERALD * 1.4 * 0.55 + SIGNAL * c_sum * 1.6 * 0.45

            col = caustic_col * 1.4 + (EMERALD * 0.5 + SIGNAL * 0.5) * cor * 0.85 + EMBER * (c_sum**2.2) * 1.1 + WHITE * sp * 1.6
            rad_falloff = np.clip((2.3 - rad) / 1.1, 0.0, 1.0)

            out[y_idx, x_idx] = np.clip(GROUND * (1.0 - np.clip(c_sum * 0.8 + cor * 0.5, 0, 1)) + col, 0, 255) * rad_falloff + GROUND * (1.0 - rad_falloff)

    im = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))
    im.save(os.path.join(PREVIEWS_DIR, "spectral-caustics.jpg"), quality=94)
    print("spectral-caustics.jpg saved")


def render_tinkerbell_map():
    """Tinkerbell chaotic attractor density plot."""
    w, h = 900, 600
    acc = np.zeros((h, w), dtype=np.float32)
    a = 0.90
    b = -0.6013
    c = 2.0
    d = 0.50
    x, y = -0.72, -0.64
    cx, cy = w * 0.5 + 40, h * 0.5 + 25
    k = min(w, h) * 0.23

    n_points = 2000000
    for _ in range(n_points):
        nx = x * x - y * y + a * x + b * y
        ny = 2.0 * x * y + c * x + d * y
        x, y = nx, ny

        if abs(x) > 10.0 or abs(y) > 10.0:
            x, y = -0.72, -0.64
            continue

        px = int(cx + x * k)
        py = int(cy + y * k)
        if 0 <= px < w and 0 <= py < h:
            acc[py, px] += 1.0

    acc = np.log1p(acc * 0.6)
    max_val = np.percentile(acc[acc > 0], 99.7) if np.any(acc > 0) else 1.0
    norm = np.clip(acc / max_val, 0.0, 1.0)

    # Slight blur for luminescence
    img_glow = Image.fromarray((norm * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.4))
    glow_arr = np.array(img_glow, dtype=np.float32) / 255.0

    out = np.zeros((h, w, 3), dtype=np.float32)
    for c_idx in range(3):
        out[:, :, c_idx] = GROUND[c_idx]

    for y_idx in range(h):
        for x_idx in range(w):
            v = norm[y_idx, x_idx]
            g_val = glow_arr[y_idx, x_idx]
            if v > 0 or g_val > 0:
                dist = math.sqrt((x_idx - cx)**2 + (y_idx - cy)**2) / (min(w, h) * 0.46)
                mix_col = SIGNAL * (1.0 - min(1.0, dist * 0.70)) + EMBER * min(1.0, dist * 0.70)
                col = mix_col * (v * 1.35 + g_val * 0.75) + WHITE * (v**2.2 * 1.6)
                out[y_idx, x_idx] = np.clip(GROUND * (1.0 - min(1.0, v + g_val)) + col, 0, 255)

    im = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))
    im.save(os.path.join(PREVIEWS_DIR, "tinkerbell-map.jpg"), quality=94)
    print("tinkerbell-map.jpg saved")


def render_kuen_surface():
    """Parametric Alfred Kuen pseudospherical surface 3D projection."""
    w, h = 900, 600
    im = Image.new("RGB", (w, h), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    rotY = 0.68
    rotX = 0.50
    cy, cx = h / 2, w / 2

    def point(u, v):
        sinV = math.sin(v)
        cosV = math.cos(v)
        sinU = math.sin(u)
        cosU = math.cos(u)

        denom = 1.0 + u * u * sinV * sinV
        sc = 1.15
        x = sc * (2.0 * (cosU + u * sinU) * sinV) / denom
        z = sc * (2.0 * (sinU - u * cosU) * sinV) / denom
        v_clamped = max(0.04, min(math.pi - 0.04, v))
        zCoord = math.log(math.tan(v_clamped * 0.5)) + (2.0 * cosV) / denom
        y = sc * zCoord * 0.68

        # Rotate Y
        x1 = x * math.cos(rotY) + z * math.sin(rotY)
        z1 = -x * math.sin(rotY) + z * math.cos(rotY)
        # Rotate X
        y2 = y * math.cos(rotX) - z1 * math.sin(rotX)
        z2 = y * math.sin(rotX) + z1 * math.cos(rotX)

        fov = 3.6
        pz = z2 + 3.8
        f = fov / pz if pz > 0.05 else 1.0
        sx = cx + x1 * f * 240.0
        sy = cy + y2 * f * 240.0
        return (sx, sy, z2)

    u_min, u_max = -3.8, 3.8
    v_min, v_max = 0.12, math.pi - 0.12
    u_count = 36
    v_count = 46

    lines = []
    # Longitudinal ribs
    for i in range(0, u_count + 1, 2):
        frac_u = i / u_count
        u = u_min + frac_u * (u_max - u_min)
        pts = [point(u, v_min + (j / v_count) * (v_max - v_min)) for j in range(v_count + 1)]
        avg_z = sum(p[2] for p in pts) / len(pts)
        lines.append((avg_z, frac_u, pts, True))

    # Transversal rings
    for j in range(1, v_count + 1, 3):
        frac_v = j / v_count
        v = v_min + frac_v * (v_max - v_min)
        pts = [point(u_min + (i / u_count) * (u_max - u_min), v) for i in range(u_count + 1)]
        avg_z = sum(p[2] for p in pts) / len(pts)
        lines.append((avg_z, frac_v, pts, False))

    lines.sort(key=lambda r: r[0])

    for avg_z, frac, pts, is_long in lines:
        alpha = int(np.clip(140 + avg_z * 85, 45, 255))
        if is_long:
            col_mix = SIGNAL * (1.0 - frac * 0.45) + EMERALD * (frac * 0.45)
        else:
            col_mix = EMBER * (1.0 - frac * 0.55) + WHITE * (frac * 0.55)
        r, g, b = [int(np.clip(c, 0, 255)) for c in col_mix]
        w_line = 2 if is_long else 1
        for idx in range(len(pts) - 1):
            p1, p2 = pts[idx], pts[idx + 1]
            draw.line([(p1[0], p1[1]), (p2[0], p2[1])], fill=(r, g, b, alpha), width=w_line)

    # Orbiting quanta
    for q in range(8):
        frac_k = q / 8.0
        orb_u = u_min + frac_k * (u_max - u_min)
        orb_v = v_min + (0.5 + 0.38 * math.sin(q * 1.8)) * (v_max - v_min)
        pos = point(orb_u, orb_v)
        draw.ellipse([pos[0] - 6, pos[1] - 6, pos[0] + 6, pos[1] + 6], fill=(255, 240, 225, 245), outline=(78, 242, 184, 255))

    im.save(os.path.join(PREVIEWS_DIR, "kuen-surface-3d.jpg"), quality=94)
    print("kuen-surface-3d.jpg saved")


def render_hard_sync_synth():
    """Render Hard Sync Synth preview and webp template."""
    w, h = 900, 600
    im = Image.new("RGB", (w, h), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    for gy in range(80, h, 80):
        draw.line([(0, gy), (w, gy)], fill=(32, 38, 70, 100), width=1)
    for gx in range(90, w, 90):
        draw.line([(gx, 0), (gx, h)], fill=(32, 38, 70, 100), width=1)

    cy = h / 2 - 20

    # Draw hard-sync oscillator waveform with sharp resets
    pts = []
    sync_ratio = 3.2
    for i in range(w):
        frac = i / w
        cycle = (frac * 4.0) % 1.0
        env = math.exp(-frac * 1.4)

        # Master reset forces slave phase reset
        slave_phase = (cycle * sync_ratio) % 1.0
        saw_slave = 2.0 * slave_phase - 1.0
        sub_wave = math.sin(cycle * 0.5 * math.tau) * 0.35

        sig = (saw_slave * 0.75 + sub_wave) * env * 155.0
        pts.append((i, cy + sig))

    for i in range(len(pts) - 1):
        # Don't draw across master reset jumps
        if abs(pts[i][1] - pts[i + 1][1]) < 110.0:
            draw.line([pts[i], pts[i + 1]], fill=(167, 177, 255, 90), width=6)
            draw.line([pts[i], pts[i + 1]], fill=(255, 107, 53, 160), width=3)
            draw.line([pts[i], pts[i + 1]], fill=(255, 250, 240, 240), width=1)

    # Draw sync formant peak response in corner box
    cx_box = 720
    cy_box = cy + 130
    draw.rectangle([cx_box - 100, cy_box - 70, cx_box + 100, cy_box + 70], outline=(78, 242, 184, 180), width=1)
    c_pts = []
    for bx in range(-90, 91):
        freq_ratio = (bx + 90) / 180.0 * 6.0
        sync_peak = math.exp(-((freq_ratio - 3.2) ** 2) / 0.8) * 110.0
        sub_peak = math.exp(-((freq_ratio - 0.5) ** 2) / 0.3) * 60.0
        c_pts.append((cx_box + bx, cy_box + 60 - (sync_peak + sub_peak)))
    for i in range(len(c_pts) - 1):
        draw.line([c_pts[i], c_pts[i + 1]], fill=(78, 242, 184, 230), width=2)

    im.save(os.path.join(PREVIEWS_DIR, "hard-sync-synth.jpg"), quality=94)
    print("hard-sync-synth.jpg saved")

    im_webp = im.resize((960, 600), Image.Resampling.LANCZOS)
    im_webp.save(os.path.join(TEMPLATES_DIR, "hard-sync-synth.webp"), "WEBP", quality=90)
    print("hard-sync-synth.webp saved")


def render_ring_modulator_fx():
    """Render Ring Modulator FX preview and webp template."""
    w, h = 900, 600
    im = Image.new("RGB", (w, h), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    for gy in range(80, h, 80):
        draw.line([(0, gy), (w, gy)], fill=(32, 38, 70, 100), width=1)
    for gx in range(90, w, 90):
        draw.line([(gx, 0), (gx, h)], fill=(32, 38, 70, 100), width=1)

    cy = h / 2 - 20

    # Draw diode ring modulated waveform (carrier x input beat envelopes)
    pts = []
    for i in range(w):
        frac = i / w
        time_ms = frac * 20.0
        env = math.exp(-frac * 1.8)
        carrier = math.sin(time_ms * 7.5 * math.tau)
        in_sig = math.sin(time_ms * 1.2 * math.tau)

        # Diode bridge ring multiplication
        ring = (in_sig * carrier) * 1.3
        sat_ring = ring / (1.0 + abs(ring) * 0.25)
        sig = sat_ring * env * 150.0
        pts.append((i, cy + sig))

    for i in range(len(pts) - 1):
        draw.line([pts[i], pts[i + 1]], fill=(78, 242, 184, 90), width=6)
        draw.line([pts[i], pts[i + 1]], fill=(167, 177, 255, 160), width=3)
        draw.line([pts[i], pts[i + 1]], fill=(255, 255, 255, 240), width=1)

    # Draw sum and difference sideband peaks in corner box
    cx_box = 720
    cy_box = cy + 130
    draw.rectangle([cx_box - 100, cy_box - 70, cx_box + 100, cy_box + 70], outline=(255, 107, 53, 180), width=1)
    c_pts = []
    for bx in range(-90, 91):
        freq_ratio = (bx + 90) / 180.0 * 6.0
        # Sideband peaks at fc - fm (2.1) and fc + fm (3.9)
        sb1 = math.exp(-((freq_ratio - 2.1) ** 2) / 0.15) * 105.0
        sb2 = math.exp(-((freq_ratio - 3.9) ** 2) / 0.15) * 105.0
        c_pts.append((cx_box + bx, cy_box + 60 - (sb1 + sb2)))
    for i in range(len(c_pts) - 1):
        draw.line([c_pts[i], c_pts[i + 1]], fill=(255, 107, 53, 230), width=2)

    im.save(os.path.join(PREVIEWS_DIR, "ring-modulator-fx.jpg"), quality=94)
    print("ring-modulator-fx.jpg saved")

    im_webp = im.resize((960, 600), Image.Resampling.LANCZOS)
    im_webp.save(os.path.join(TEMPLATES_DIR, "ring-modulator-fx.webp"), "WEBP", quality=90)
    print("ring-modulator-fx.webp saved")


def render_plasma_filament():
    """Shader-accurate 900x600 render of Plasma Filament."""
    w, h = 900, 600
    aspect = w / h
    x = np.linspace(-1.1 * aspect, 1.1 * aspect, w, dtype=np.float32)
    y = np.linspace(-1.1, 1.1, h, dtype=np.float32)
    xx, yy = np.meshgrid(x, y)

    r = np.sqrt(xx**2 + yy**2)
    angle = np.arctan2(yy, xx)
    t_cur = 2.4 * 0.35

    twist = 2.4
    filaments = 5.0
    spiral1 = np.sin(angle * filaments + r * twist - t_cur * 2.5)
    spiral2 = np.cos(-angle * (filaments * 0.8) + r * (twist * 1.3) + t_cur * 1.9)
    filament_arc = np.maximum(0.0, 1.0 - np.abs(spiral1 * 0.6 + spiral2 * 0.4))**6.5

    core_glow = np.exp(-r * r * 2.2) * 1.6
    shockwave = np.sin(r * 22.0 - t_cur * 5.0) * np.exp(-r * 1.5) * 0.5
    micro_sparks = np.maximum(0.0, np.sin(xx * 40.0 + t_cur * 4.0) * np.sin(yy * 40.0 - t_cur * 3.5))**8.0
    micro_sparks *= np.exp(-r * 1.2) * 1.8

    out = np.zeros((h, w, 3), dtype=np.float32)
    for c in range(3):
        out[:, :, c] = GROUND[c]

    for y_idx in range(h):
        for x_idx in range(w):
            rad = r[y_idx, x_idx]
            ang = angle[y_idx, x_idx]
            arc = filament_arc[y_idx, x_idx]
            core = core_glow[y_idx, x_idx]
            sw = shockwave[y_idx, x_idx]
            sp = micro_sparks[y_idx, x_idx]
            s2 = spiral2[y_idx, x_idx]

            mix_t = np.clip(rad * 0.9 + math.sin(ang * 3.0 + t_cur) * 0.2, 0.0, 1.0)
            plasma_grad = EMBER * (1.0 - mix_t) + SIGNAL * mix_t
            s2_clamped = np.clip(s2, 0.0, 1.0) * 0.45
            fil_col = plasma_grad * (1.0 - s2_clamped) + EMERALD * s2_clamped

            col = fil_col * arc * 2.4
            col += (EMBER * 0.5 + SIGNAL * 0.5) * core * 1.1
            col += SIGNAL * max(0.0, sw) * 1.4
            col += WHITE * sp * 1.8

            rad_falloff = np.clip((2.3 - rad) / 1.1, 0.0, 1.0)
            out[y_idx, x_idx] = np.clip(GROUND * (1.0 - np.clip(arc * 0.8 + core * 0.5, 0, 1)) + col, 0, 255) * rad_falloff + GROUND * (1.0 - rad_falloff)

    im = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))
    im.save(os.path.join(PREVIEWS_DIR, "plasma-filament.jpg"), quality=94)
    print("plasma-filament.jpg saved")


def render_halvorsen_attractor():
    """Halvorsen chaotic strange attractor density plot."""
    w, h = 900, 600
    acc = np.zeros((h, w), dtype=np.float32)
    a = 1.89
    x, y, z = -1.48, -0.22, 0.54
    cx, cy = w * 0.5, h * 0.5
    k = min(w, h) * 0.038
    dt = 0.0075

    rotY = 0.65
    rotX = 0.52
    cosY, sinY = math.cos(rotY), math.sin(rotY)
    cosX, sinX = math.cos(rotX), math.sin(rotX)

    n_points = 1800000
    for _ in range(n_points):
        dx = -a * x - 4.0 * y - 4.0 * z - y * y
        dy = -a * y - 4.0 * z - 4.0 * x - z * z
        dz = -a * z - 4.0 * x - 4.0 * y - x * x
        x += dx * dt
        y += dy * dt
        z += dz * dt

        if abs(x) > 50.0 or abs(y) > 50.0 or abs(z) > 50.0:
            x, y, z = -1.48, -0.22, 0.54
            continue

        x1 = x * cosY + z * sinY
        z1 = -x * sinY + z * cosY
        y2 = y * cosX - z1 * sinX

        px = int(cx + x1 * k)
        py = int(cy + y2 * k)
        if 0 <= px < w and 0 <= py < h:
            acc[py, px] += 1.0

    acc = np.log1p(acc * 0.5)
    max_val = np.percentile(acc[acc > 0], 99.7) if np.any(acc > 0) else 1.0
    norm = np.clip(acc / max_val, 0.0, 1.0)

    img_glow = Image.fromarray((norm * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.4))
    glow_arr = np.array(img_glow, dtype=np.float32) / 255.0

    out = np.zeros((h, w, 3), dtype=np.float32)
    for c_idx in range(3):
        out[:, :, c_idx] = GROUND[c_idx]

    for y_idx in range(h):
        for x_idx in range(w):
            v = norm[y_idx, x_idx]
            g_val = glow_arr[y_idx, x_idx]
            if v > 0 or g_val > 0:
                dist = math.sqrt((x_idx - cx)**2 + (y_idx - cy)**2) / (min(w, h) * 0.46)
                mix_col = SIGNAL * (1.0 - min(1.0, dist * 0.70)) + EMBER * min(1.0, dist * 0.70)
                col = mix_col * (v * 1.35 + g_val * 0.75) + WHITE * (v**2.2 * 1.6)
                out[y_idx, x_idx] = np.clip(GROUND * (1.0 - min(1.0, v + g_val)) + col, 0, 255)

    im = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))
    im.save(os.path.join(PREVIEWS_DIR, "halvorsen-attractor.jpg"), quality=94)
    print("halvorsen-attractor.jpg saved")


def render_boy_surface():
    """Parametric Boy's surface 3D projection."""
    w, h = 900, 600
    im = Image.new("RGB", (w, h), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    rotY = 0.72
    rotX = 0.48
    cy, cx = h / 2, w / 2

    def point(u, v):
        sinV = math.sin(v)
        cosV = math.cos(v)
        sin2V = math.sin(2.0 * v)
        cosV2 = cosV * cosV
        sin3U = math.sin(3.0 * u)
        cosU = math.cos(u)
        sinU = math.sin(u)
        cos2U = math.cos(2.0 * u)
        sin2U = math.sin(2.0 * u)

        sqrt2 = 1.41421356
        denom = max(0.25, 2.0 - sqrt2 * sin3U * sin2V)

        sc = 0.72
        x = sc * (sqrt2 * cosV2 * cos2U + cosU * sin2V) / denom
        y = sc * (sqrt2 * cosV2 * sin2U - sinU * sin2V) / denom
        z = sc * (3.0 * cosV2 / denom - 1.0)

        # Rotate Y
        x1 = x * math.cos(rotY) + y * math.sin(rotY)
        z1 = -x * math.sin(rotY) + y * math.cos(rotY)
        # Rotate X
        y2 = z * math.cos(rotX) - z1 * math.sin(rotX)
        z2 = z * math.sin(rotX) + z1 * math.cos(rotX)

        fov = 3.2
        dist = 3.8 - z2
        px = cx + (x1 / dist) * fov * 230
        py = cy - (y2 / dist) * fov * 230
        return px, py, z2

    ribs_u = 40
    segs_v = 40

    lines = []
    for i in range(ribs_u + 1):
        frac_u = i / ribs_u
        u = frac_u * math.pi
        pts = [point(u, (j / segs_v) * (math.pi - 0.08) + 0.04) for j in range(segs_v + 1)]
        avg_z = sum(p[2] for p in pts) / len(pts)
        lines.append((avg_z, frac_u, pts, False))

    for j in range(0, segs_v + 1, 2):
        frac_v = j / segs_v
        v = 0.04 + frac_v * (math.pi - 0.08)
        pts = [point((i / ribs_u) * math.pi, v) for i in range(ribs_u + 1)]
        avg_z = sum(p[2] for p in pts) / len(pts)
        lines.append((avg_z, frac_v, pts, True))

    lines.sort(key=lambda r: r[0])

    for avg_z, frac, pts, is_v in lines:
        alpha = int(np.clip(135 + avg_z * 85, 40, 255))
        if is_v:
            col_mix = EMBER * (1.0 - frac * 0.5) + WHITE * (frac * 0.5)
        else:
            col_mix = SIGNAL * (1.0 - frac) + EMBER * frac
        r, g, b = [int(np.clip(c, 0, 255)) for c in col_mix]
        w_line = 2 if not is_v else 1
        for idx in range(len(pts) - 1):
            p1, p2 = pts[idx], pts[idx + 1]
            draw.line([(p1[0], p1[1]), (p2[0], p2[1])], fill=(r, g, b, alpha), width=w_line)

    for q in range(12):
        ang = (q / 12.0) * math.tau
        qu = (q / 12.0) * math.pi
        qv = 0.5 * math.pi + 0.35 * math.sin(ang * 2.0)
        pos = point(qu, qv)
        draw.ellipse([pos[0] - 5, pos[1] - 5, pos[0] + 5, pos[1] + 5], fill=(255, 235, 220, 245), outline=(255, 107, 53, 255))

    im.save(os.path.join(PREVIEWS_DIR, "boy-surface-3d.jpg"), quality=94)
    print("boy-surface-3d.jpg saved")


def render_subharmonic_synth():
    """Render Subharmonic Synth preview and webp template."""
    w, h = 900, 600
    im = Image.new("RGB", (w, h), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    for gy in range(80, h, 80):
        draw.line([(0, gy), (w, gy)], fill=(32, 38, 70, 100), width=1)
    for gx in range(90, w, 90):
        draw.line([(gx, 0), (gx, h)], fill=(32, 38, 70, 100), width=1)

    cy = h / 2 - 20

    pts = []
    for i in range(w):
        frac = i / w
        cycle = (frac * 6.0) % 1.0
        env = math.exp(-frac * 1.5)

        pw = 0.5 + 0.25 * math.sin(frac * 8.0)
        p_main = 1.0 if cycle < pw else -1.0
        saw_main = 2.0 * cycle - 1.0
        w_main = saw_main * 0.6 + p_main * 0.4

        sub1_cyc = (frac * 3.0) % 1.0
        w_sub1 = (2.0 * sub1_cyc - 1.0) * 0.45

        sub2_cyc = (frac * 2.0) % 1.0
        w_sub2 = (2.0 * abs(2.0 * sub2_cyc - 1.0) - 1.0) * 0.35

        sig = (w_main + w_sub1 + w_sub2) * env * 145.0
        pts.append((i, cy + sig))

    for i in range(len(pts) - 1):
        if abs(pts[i][1] - pts[i + 1][1]) < 120.0:
            draw.line([pts[i], pts[i + 1]], fill=(167, 177, 255, 90), width=6)
            draw.line([pts[i], pts[i + 1]], fill=(255, 107, 53, 160), width=3)
            draw.line([pts[i], pts[i + 1]], fill=(255, 250, 240, 240), width=1)

    cx_box = 720
    cy_box = cy + 130
    draw.rectangle([cx_box - 100, cy_box - 70, cx_box + 100, cy_box + 70], outline=(78, 242, 184, 180), width=1)
    bars = [
        (-60, 45),
        (-20, 65),
        (40, 95),
        (70, 35)
    ]
    for bx, b_h in bars:
        draw.line([(cx_box + bx, cy_box + 60), (cx_box + bx, cy_box + 60 - b_h)], fill=(78, 242, 184, 230), width=5)
        draw.ellipse([cx_box + bx - 4, cy_box + 60 - b_h - 4, cx_box + bx + 4, cy_box + 60 - b_h + 4], fill=(255, 255, 255, 255))

    im.save(os.path.join(PREVIEWS_DIR, "subharmonic-synth.jpg"), quality=94)
    print("subharmonic-synth.jpg saved")

    im_webp = im.resize((960, 600), Image.Resampling.LANCZOS)
    im_webp.save(os.path.join(TEMPLATES_DIR, "subharmonic-synth.webp"), "WEBP", quality=90)
    print("subharmonic-synth.webp saved")


def render_bitcrush_degrader():
    """Render Bitcrush Degrader FX preview and webp template."""
    w, h = 900, 600
    im = Image.new("RGB", (w, h), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    for gy in range(80, h, 80):
        draw.line([(0, gy), (w, gy)], fill=(32, 38, 70, 100), width=1)
    for gx in range(90, w, 90):
        draw.line([(gx, 0), (gx, h)], fill=(32, 38, 70, 100), width=1)

    cy = h / 2 - 20

    pts = []
    step_size = 14
    levels = 8
    held_val = 0
    for i in range(w):
        frac = i / w
        time_ms = frac * 16.0
        env = math.exp(-frac * 1.6)
        in_sig = math.sin(time_ms * 1.8 * math.tau) + 0.3 * math.sin(time_ms * 4.2 * math.tau)
        if i % step_size == 0:
            q = round(in_sig * levels) / levels
            held_val = q * env * 155.0
        pts.append((i, cy + held_val))

    for i in range(len(pts) - 1):
        draw.line([pts[i], pts[i + 1]], fill=(167, 177, 255, 90), width=5)
        draw.line([pts[i], pts[i + 1]], fill=(255, 107, 53, 170), width=3)
        draw.line([pts[i], pts[i + 1]], fill=(255, 250, 240, 240), width=1)

    cx_box = 720
    cy_box = cy + 130
    draw.rectangle([cx_box - 100, cy_box - 70, cx_box + 100, cy_box + 70], outline=(78, 242, 184, 180), width=1)
    c_pts = []
    for bx in range(-90, 91):
        f = (bx + 90) / 180.0 * 5.0
        nyq_alias = abs(math.sin(f * 3.14159)) * 75.0 * math.exp(-f * 0.3)
        c_pts.append((cx_box + bx, cy_box + 60 - nyq_alias))
    for i in range(len(c_pts) - 1):
        draw.line([c_pts[i], c_pts[i + 1]], fill=(78, 242, 184, 230), width=2)

    im.save(os.path.join(PREVIEWS_DIR, "bitcrush-degrader-fx.jpg"), quality=94)
    print("bitcrush-degrader-fx.jpg saved")

    im_webp = im.resize((960, 600), Image.Resampling.LANCZOS)
    im_webp.save(os.path.join(TEMPLATES_DIR, "bitcrush-degrader-fx.webp"), "WEBP", quality=90)
    print("bitcrush-degrader-fx.webp saved")


def render_solar_flare():
    """Shader-accurate 900x600 render of Solar Flare."""
    w, h = 900, 600
    aspect = w / h
    x = np.linspace(-1.1 * aspect, 1.1 * aspect, w, dtype=np.float32)
    y = np.linspace(-1.1, 1.1, h, dtype=np.float32)
    xx, yy = np.meshgrid(x, y)

    r = np.sqrt(xx**2 + yy**2)
    angle = np.arctan2(yy, xx)
    t_cur = 2.4 * 0.38

    turbulence = 2.8
    flare_count = 6.0
    loop1 = np.sin(angle * flare_count + np.sin(r * turbulence - t_cur * 2.2) * 2.5)
    loop2 = np.cos(-angle * (flare_count * 0.7) + np.cos(r * (turbulence * 1.4) + t_cur * 1.7) * 2.0)
    prominence = np.maximum(0.0, 1.0 - np.abs(loop1 * 0.55 + loop2 * 0.45))**5.5

    corona = np.exp(-r * r * 1.8) * 1.8
    sun_disc = np.clip((0.46 - r) / 0.10, 0.0, 1.0)
    cme_wave = np.sin(r * 24.0 - t_cur * 6.0) * np.exp(-r * 1.4) * 0.55
    granulation = np.maximum(0.0, np.sin(xx * 32.0 + t_cur * 3.5) * np.sin(yy * 32.0 - t_cur * 2.8))**6.0
    granulation *= np.exp(-r * 1.5) * 1.6

    SUN_CORE = np.array([255, 240, 217], dtype=np.float32)

    out = np.zeros((h, w, 3), dtype=np.float32)
    for c in range(3):
        out[:, :, c] = GROUND[c]

    for y_idx in range(h):
        for x_idx in range(w):
            rad = r[y_idx, x_idx]
            prom = prominence[y_idx, x_idx]
            cor = corona[y_idx, x_idx]
            disc = sun_disc[y_idx, x_idx]
            cme = cme_wave[y_idx, x_idx]
            gran = granulation[y_idx, x_idx]
            l2 = loop2[y_idx, x_idx]

            solar_grad = EMBER * (1.0 - np.clip(cor * 0.7 + disc * 0.5, 0.0, 1.0)) + SUN_CORE * np.clip(cor * 0.7 + disc * 0.5, 0.0, 1.0)
            flare_col = solar_grad * (1.0 - np.clip(l2 * 0.5 + 0.5, 0.0, 1.0) * 0.4) + SIGNAL * (np.clip(l2 * 0.5 + 0.5, 0.0, 1.0) * 0.4)

            col = flare_col * prom * 2.4
            col += (EMBER * 0.4 + SUN_CORE * 0.6) * cor * 1.2
            col += SIGNAL * max(0.0, cme) * 1.5
            col += SUN_CORE * gran * 1.6

            rad_falloff = np.clip((2.3 - rad) / 1.1, 0.0, 1.0)
            out[y_idx, x_idx] = np.clip(GROUND * (1.0 - np.clip(prom * 0.8 + cor * 0.5, 0, 1)) + col, 0, 255) * rad_falloff + GROUND * (1.0 - rad_falloff)

    im = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))
    im.save(os.path.join(PREVIEWS_DIR, "solar-flare.jpg"), quality=94)
    print("solar-flare.jpg saved")


if __name__ == "__main__":
    render_gumowski_mira()
    render_enneper()
    render_phase_distortion()
    render_formant_filter()
    render_aurora_borealis()
    render_ikeda_attractor()
    render_dini_surface()
    render_wavetable_morph()
    render_wavefolder_drive()
    render_cosmic_nebula()
    render_svensson_attractor()
    render_bour_surface()
    render_additive_harmonic()
    render_comb_resonator()
    render_spectral_caustics()
    render_tinkerbell_map()
    render_kuen_surface()
    render_hard_sync_synth()
    render_ring_modulator_fx()
    render_plasma_filament()
    render_halvorsen_attractor()
    render_boy_surface()
    render_subharmonic_synth()
    render_bitcrush_degrader()
    render_solar_flare()
    print("All previews rendered successfully!")
