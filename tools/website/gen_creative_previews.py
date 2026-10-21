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

def render_harmonograph():
    """3-pendulum harmonograph resonance."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2
    scale = min(W, H) * 0.42

    steps = 4500
    dt = 0.035
    f1, f2 = 2.001, 2.01 * 2.001
    f3, f4 = 1.000, 1.000 * (2.01 * 0.5)
    d = 0.0009
    p1, p2, p3, p4 = 0.1, 0.1 + 0.25 * math.pi * 2, 0.07, 0.11

    pts = []
    for i in range(steps):
        s = i * dt
        decay = math.exp(-d * i)
        x = (math.sin(f1 * s + p1) * 0.55 + math.sin(f2 * s + p2) * 0.45) * decay
        y = (math.cos(f3 * s + p3) * 0.55 + math.cos(f4 * s + p4) * 0.45) * decay
        pts.append((cx + x * scale, cy + y * scale))

    for i in range(len(pts) - 1):
        frac = i / len(pts)
        r = int(SIGNAL[0] * (1 - frac) + EMBER[0] * frac)
        g = int(SIGNAL[1] * (1 - frac) + EMBER[1] * frac)
        b = int(SIGNAL[2] * (1 - frac) + EMBER[2] * frac)
        a = int(255 * (0.35 + 0.65 * (1.0 - frac)))
        draw.line([pts[i], pts[i+1]], fill=(r, g, b, a), width=2)

    im.save(os.path.join(PREVIEWS_DIR, "harmonograph-resonance.jpg"), quality=94)
    print("harmonograph-resonance.jpg saved")

def render_chua():
    """Chua circuit chaotic attractor."""
    acc = np.zeros((H, W), dtype=np.float32)
    cx, cy = W / 2, H / 2
    k = min(W, H) * 0.14
    alpha, beta = 10.2, 15.6
    m0, m1 = -1.143, -0.714
    x, y, z = 0.7, 0.0, 0.0
    dt = 0.008

    for i in range(1200000):
        gx = m1 * x + 0.5 * (m0 - m1) * (abs(x + 1.0) - abs(x - 1.0))
        dx = alpha * (y - x - gx)
        dy = x - y + z
        dz = -beta * y
        x += dx * dt
        y += dy * dt
        z += dz * dt

        if i > 2000:
            rx = x * math.cos(0.4) - z * math.sin(0.4)
            ry = y * 1.2 + (x * math.sin(0.4) + z * math.cos(0.4)) * 0.35
            px = int(cx + rx * k)
            py = int(cy + ry * k)
            if 0 <= px < W and 0 <= py < H:
                acc[py, px] += 1.0

    acc = np.log1p(acc * 0.8)
    norm = np.clip(acc / np.percentile(acc[acc > 0], 99.5), 0.0, 1.0)
    out = np.zeros((H, W, 3), dtype=np.float32)
    for c in range(3):
        out[:, :, c] = GROUND[c]

    for y_idx in range(H):
        for x_idx in range(W):
            v = norm[y_idx, x_idx]
            if v > 0:
                frac = v**0.8
                mix_col = SIGNAL * (1.0 - frac) + EMBER * frac
                out[y_idx, x_idx] = np.clip(GROUND * (1.0 - v) + mix_col * v * 1.8 + WHITE * (v**3 * 1.5), 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "chua-circuit.jpg"), quality=94)
    print("chua-circuit.jpg saved")

def render_hopf_fibration():
    """Hopf fibration 3D Villarceau circles."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2
    N = 32
    segments = 120
    R = 175.0
    r = 85.0

    for c in range(N):
        alpha = (c / N) * math.pi * 2
        pts = []
        for i in range(segments + 1):
            phi = (i / segments) * math.pi * 2
            theta = alpha + 0.35 * math.sin(phi * 2.0)
            x = R * math.cos(theta) - r * math.sin(phi) * math.sin(theta)
            y = R * math.sin(theta) + r * math.sin(phi) * math.cos(theta)
            z = r * math.cos(phi)

            # 3D rotation
            rotY = 0.65
            rotX = 0.5
            x1 = x * math.cos(rotY) + z * math.sin(rotY)
            z1 = -x * math.sin(rotY) + z * math.cos(rotY)
            y2 = y * math.cos(rotX) - z1 * math.sin(rotX)
            z2 = y * math.sin(rotX) + z1 * math.cos(rotX)

            pts.append((cx + x1, cy + y2, z2))

        frac = c / N
        cr = int(SIGNAL[0] * (1 - frac) + EMBER[0] * frac)
        cg = int(SIGNAL[1] * (1 - frac) + EMBER[1] * frac)
        cb = int(SIGNAL[2] * (1 - frac) + EMBER[2] * frac)

        for i in range(len(pts) - 1):
            p1, p2 = pts[i], pts[i+1]
            draw.line([(p1[0], p1[1]), (p2[0], p2[1])], fill=(cr, cg, cb, 210), width=2)

    im.save(os.path.join(PREVIEWS_DIR, "hopf-fibration-3d.jpg"), quality=94)
    print("hopf-fibration-3d.jpg saved")

def render_hyperbolic_helicoid():
    """Hyperbolic Helicoid minimal surface."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2
    theta = 0.5 * (math.pi * 0.5)
    cosT, sinT = math.cos(theta), math.sin(theta)
    uSteps, vSteps = 60, 24
    radius = 110.0

    for j in range(vSteps + 1):
        v = -1.35 + (j / vSteps) * 2.7
        sinhV = (math.exp(v) - math.exp(-v)) * 0.5
        coshV = (math.exp(v) + math.exp(-v)) * 0.5
        pts = []
        for i in range(uSteps + 1):
            u = -math.pi * 2.2 + (i / uSteps) * math.pi * 4.4
            x = (cosT * sinhV * math.sin(u) + sinT * coshV * math.cos(u)) * radius
            y = (-cosT * sinhV * math.cos(u) + sinT * coshV * math.sin(u)) * radius
            z = (u * cosT + v * sinT) * (radius * 0.45)

            # 3D rotation
            rotY = 0.8
            rotX = 0.45
            x1 = x * math.cos(rotY) + z * math.sin(rotY)
            z1 = -x * math.sin(rotY) + z * math.cos(rotY)
            y2 = y * math.cos(rotX) - z1 * math.sin(rotX)

            pts.append((cx + x1, cy + y2))

        frac = j / vSteps
        cr = int(EMBER[0] * (1 - frac) + SIGNAL[0] * frac)
        cg = int(EMBER[1] * (1 - frac) + SIGNAL[1] * frac)
        cb = int(EMBER[2] * (1 - frac) + SIGNAL[2] * frac)

        for i in range(len(pts) - 1):
            draw.line([pts[i], pts[i+1]], fill=(cr, cg, cb, 220), width=2)

    im.save(os.path.join(PREVIEWS_DIR, "hyperbolic-helicoid-3d.jpg"), quality=94)
    print("hyperbolic-helicoid-3d.jpg saved")

def render_granular_cloud():
    """Granular Cloud Synth waveform and spectrum cloud."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2
    np.random.seed(42)

    # Multi-grain scatter cloud
    for _ in range(350):
        gx = cx + np.random.normal(0, 220)
        gy = cy + np.random.normal(0, 110)
        gr = np.random.uniform(3, 16)
        alpha = int(np.random.uniform(40, 180))
        c = EMBER if np.random.rand() > 0.55 else SIGNAL
        draw.ellipse([gx - gr, gy - gr, gx + gr, gy + gr], fill=(int(c[0]), int(c[1]), int(c[2]), alpha))

    # Grain waveform trace
    pts = []
    for x in range(W):
        t = x / W * 18.0
        env = math.exp(-((x - cx) / 260.0)**2)
        y = cy + (math.sin(t * 12.0) * 0.6 + math.sin(t * 26.0) * 0.4) * 110.0 * env
        pts.append((x, y))

    for i in range(len(pts) - 1):
        draw.line([pts[i], pts[i+1]], fill=(int(WHITE[0]), int(WHITE[1]), int(WHITE[2]), 240), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "granular-cloud-synth.jpg"), quality=94)
    print("granular-cloud-synth.jpg saved")

def render_modal_bar():
    """Modal Bar Synth harmonic modes and resonant bars."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2

    # Resonant bars
    modes = [1.0, 2.756, 5.404, 8.933]
    colors = [EMBER, SIGNAL, EMERALD, WHITE]

    for m_idx, (m, col) in enumerate(zip(modes, colors)):
        pts = []
        amp = 90.0 / (m_idx + 1)**0.8
        y_center = cy - 100 + m_idx * 65
        for x in range(W):
            t = (x - cx) / W * 8.0
            val = math.cos(t * m * 3.14159) * math.exp(-abs(t) * 0.6)
            pts.append((x, y_center + val * amp))

        for i in range(len(pts) - 1):
            draw.line([pts[i], pts[i+1]], fill=(int(col[0]), int(col[1]), int(col[2]), 220), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "modal-bar-synth.jpg"), quality=94)
    print("modal-bar-synth.jpg saved")

def render_spectral_diffuser():
    """Spectral Diffuser FX allpass diffusion lattice."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2

    # Nested diffuse halo rings
    for r in range(20, 280, 16):
        frac = r / 280.0
        alpha = int(255 * math.sin(frac * math.pi) * 0.65)
        cr = int(SIGNAL[0] * (1 - frac) + EMBER[0] * frac)
        cg = int(SIGNAL[1] * (1 - frac) + EMBER[1] * frac)
        cb = int(SIGNAL[2] * (1 - frac) + EMBER[2] * frac)
        draw.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(cr, cg, cb, alpha), width=2)

    # Dispersed ray filaments
    for a_deg in range(0, 360, 6):
        rad = math.radians(a_deg)
        length = 120 + 90 * math.sin(rad * 8.0)
        x2 = cx + math.cos(rad) * length
        y2 = cy + math.sin(rad) * length
        draw.line([(cx, cy), (x2, y2)], fill=(int(EMERALD[0]), int(EMERALD[1]), int(EMERALD[2]), 120), width=1)

    im.save(os.path.join(PREVIEWS_DIR, "spectral-diffuser-fx.jpg"), quality=94)
    print("spectral-diffuser-fx.jpg saved")

def render_vowel_resonator():
    """Vowel Resonator FX phonetic formant envelope curve."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2

    formants = [(350, 80), (1200, 120), (2600, 150)]
    pts = []
    for x in range(W):
        freq = x / W * 3600.0
        resp = 0.0
        for f0, bw in formants:
            resp += 1.0 / (1.0 + ((freq - f0) / bw)**2)
        y = cy + 120 - resp * 140.0
        pts.append((x, y))

    for i in range(len(pts) - 1):
        frac = i / len(pts)
        cr = int(EMBER[0] * (1 - frac) + SIGNAL[0] * frac)
        cg = int(EMBER[1] * (1 - frac) + SIGNAL[1] * frac)
        cb = int(EMBER[2] * (1 - frac) + SIGNAL[2] * frac)
        draw.line([pts[i], pts[i+1]], fill=(cr, cg, cb, 255), width=3)

    # Glow under curve
    for i in range(0, len(pts), 4):
        draw.line([pts[i], (pts[i][0], cy + 120)], fill=(int(SIGNAL[0]), int(SIGNAL[1]), int(SIGNAL[2]), 50), width=2)

    im.save(os.path.join(PREVIEWS_DIR, "vowel-resonator-fx.jpg"), quality=94)
    print("vowel-resonator-fx.jpg saved")

def render_supernova():
    """Supernova Remnant expanding blast wave."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    cx, cy = W / 2, H / 2
    for c in range(3):
        out[:, :, c] = GROUND[c]

    y_grid, x_grid = np.ogrid[:H, :W]
    dx = (x_grid - cx) / (min(W, H) * 0.45)
    dy = (y_grid - cy) / (min(W, H) * 0.45)
    r = np.sqrt(dx**2 + dy**2)
    a = np.arctan2(dy, dx)

    shock_r = 0.62
    shock = np.exp(-abs(r - shock_r) * 7.5)
    filaments = np.sin(a * 7.0 + np.sin(r * 18.0) * 2.2)
    fil_glow = np.clip(filaments, 0, 1)**2.5 * np.clip((1.2 - r) / 0.5, 0, 1)
    pulsar = np.exp(-r * 10.0) * 1.6

    for y_idx in range(H):
        for x_idx in range(W):
            rad = r[y_idx, x_idx]
            sw = shock[y_idx, x_idx]
            fg = fil_glow[y_idx, x_idx]
            pul = pulsar[y_idx, x_idx]
            ang = a[y_idx, x_idx]

            dop = math.sin(ang * 3.0) * 0.5 + 0.5
            plasma = EMBER * (1.0 - dop) + SIGNAL * dop
            plasma = plasma * (1.0 - fg * 0.4) + EMERALD * (fg * 0.4)

            col = GROUND * (1.0 - sw) + plasma * (sw * 1.8 + fg * 1.4) + WHITE * pul
            vignette = np.clip((1.8 - rad) / 0.8, 0, 1)
            out[y_idx, x_idx] = np.clip(col * vignette + GROUND * (1.0 - vignette), 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "supernova-remnant.jpg"), quality=94)
    print("supernova-remnant.jpg saved")

def render_hyperbolic_kaleidoscope():
    """Hyperbolic Kaleidoscope Poincaré disc tiling."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    cx, cy = W / 2, H / 2
    for c in range(3):
        out[:, :, c] = GROUND[c]

    y_grid, x_grid = np.ogrid[:H, :W]
    dx = (x_grid - cx) / (min(W, H) * 0.45)
    dy = (y_grid - cy) / (min(W, H) * 0.45)
    r = np.sqrt(dx**2 + dy**2)
    a = np.arctan2(dy, dx)

    poincare = np.clip((1.0 - r) * 25.0, 0, 1)
    inv_r = r / np.maximum(r * r, 0.04)
    p_x = dx * (1.0 - 0.4) + (dx / np.maximum(r * r, 0.04)) * 0.4
    p_y = dy * (1.0 - 0.4) + (dy / np.maximum(r * r, 0.04)) * 0.4
    fr = np.sqrt(p_x**2 + p_y**2)
    fa = np.arctan2(p_y, p_x)

    sector = math.pi * 2.0 / 7.0
    folded_a = np.abs(np.mod(fa, sector) - sector * 0.5)
    fx = np.cos(folded_a) * fr
    fy = np.sin(folded_a) * fr

    grid = np.sin(fx * 14.0) * np.sin(fy * 14.0)
    glow = np.abs(grid)**0.4

    for y_idx in range(H):
        for x_idx in range(W):
            rad = r[y_idx, x_idx]
            p_mask = poincare[y_idx, x_idx]
            g = glow[y_idx, x_idx]

            col = GROUND * (1.0 - g * 0.7) + SIGNAL * (g * 0.75)
            mix_e = math.sin(rad * 9.0) * 0.5 + 0.5
            col = col * (1.0 - mix_e * 0.6) + EMBER * (mix_e * 0.6)
            col += SIGNAL * math.exp(-abs(rad - 0.98) * 35.0) * 0.9

            out[y_idx, x_idx] = np.clip(col * p_mask + GROUND * (1.0 - p_mask), 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "hyperbolic-kaleidoscope.jpg"), quality=94)
    print("hyperbolic-kaleidoscope.jpg saved")

if __name__ == "__main__":
    render_harmonograph()
    render_chua()
    render_hopf_fibration()
    render_hyperbolic_helicoid()
    render_granular_cloud()
    render_modal_bar()
    render_spectral_diffuser()
    render_vowel_resonator()
    render_supernova()
    render_hyperbolic_kaleidoscope()
    print("All 10 preview images successfully generated!")

    # Automatically generate corresponding WebP thumbnails for library deck
    thumbs_script = os.path.join(ROOT, "tools", "website", "gen_library_thumbs.py")
    if os.path.exists(thumbs_script):
        import subprocess, sys
        subprocess.run([sys.executable, thumbs_script], check=True)
