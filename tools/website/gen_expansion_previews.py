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
    print("All previews rendered successfully!")
