#!/usr/bin/env python3
"""Generate high-fidelity preview images for the 10 new creative library items (Pack 3).
Matches brand design: midnight ground #151930, signal #A7B1FF, ember #FF6B35, emerald #4EF2B8.
"""
import os, math, subprocess, sys
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

def render_chen():
    """Chen attractor chaotic dual butterfly scrolls."""
    acc = np.zeros((H, W), dtype=np.float32)
    cx, cy = W / 2, H / 2
    k = min(W, H) * 0.0125
    a, b, c = 35.0, 3.0, 28.0
    x, y, z = -0.1, 0.5, -0.6
    dt = 0.001

    def derivs(px, py, pz):
        return a * (py - px), (c - a) * px - px * pz + c * py, px * py - b * pz

    for i in range(1200000):
        k1x, k1y, k1z = derivs(x, y, z)
        k2x, k2y, k2z = derivs(x + 0.5*dt*k1x, y + 0.5*dt*k1y, z + 0.5*dt*k1z)
        k3x, k3y, k3z = derivs(x + 0.5*dt*k2x, y + 0.5*dt*k2y, z + 0.5*dt*k2z)
        k4x, k4y, k4z = derivs(x + dt*k3x, y + dt*k3y, z + dt*k3z)

        x += (dt/6.0) * (k1x + 2*k2x + 2*k3x + k4x)
        y += (dt/6.0) * (k1y + 2*k2y + 2*k3y + k4y)
        z += (dt/6.0) * (k1z + 2*k2z + 2*k3z + k4z)

        if i > 2000:
            rot = 0.75
            rx = x * math.cos(rot) - y * math.sin(rot)
            ry = (x * math.sin(rot) + y * math.cos(rot)) * 0.55 + z * 0.72
            px = int(cx + rx * k)
            py = int(cy - (ry - 20.0) * k)
            if 0 <= px < W and 0 <= py < H:
                acc[py, px] += 1.0

    acc = np.log1p(acc * 0.95)
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

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "chen-attractor.jpg"), quality=94)
    print("chen-attractor.jpg saved")

def render_spirograph():
    """Spirograph multi-frequency epitrochoid planetary lace."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2
    scale = min(W, H) * 0.145

    R, r, d = 1.8, 0.72, 1.15
    kRatio = (R + r) / r
    harmonics = 3.0
    steps = 4500

    pts = []
    for i in range(steps + 1):
        theta = (i / steps) * math.pi * 2 * harmonics
        x0 = (R + r) * math.cos(theta) - d * math.cos(kRatio * theta)
        y0 = (R + r) * math.sin(theta) - d * math.sin(kRatio * theta)
        hTheta = theta * 5.0
        px = x0 + math.cos(hTheta) * 0.22
        py = y0 + math.sin(hTheta) * 0.22
        pts.append((cx + px * scale, cy - py * scale))

    for i in range(1, len(pts)):
        frac = i / len(pts)
        r_c = int(EMBER[0] * frac + SIGNAL[0] * (1.0 - frac))
        g_c = int(EMBER[1] * frac + SIGNAL[1] * (1.0 - frac))
        b_c = int(EMBER[2] * frac + SIGNAL[2] * (1.0 - frac))
        draw.line([pts[i-1], pts[i]], fill=(r_c, g_c, b_c, 210), width=2)

    im = im.filter(ImageFilter.SMOOTH)
    im.save(os.path.join(PREVIEWS_DIR, "spirograph-epitrochoid.jpg"), quality=94)
    print("spirograph-epitrochoid.jpg saved")

def render_costa():
    """Costa's complete embedded minimal surface."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2
    scale = min(W, H) * 0.32

    layers = 38
    steps = 60
    uMin, uMax = -1.2, 1.2
    vMin, vMax = -math.pi + 0.1, math.pi - 0.1
    rotY = 0.65
    rotX = 0.55

    lines = []
    for i in range(layers):
        u = uMin + (i / (layers - 1)) * (uMax - uMin)
        pts = []
        for j in range(steps + 1):
            v = vMin + (j / steps) * (vMax - vMin)
            cosh2u = math.cosh(2.0 * u)
            cos2v = math.cos(2.0 * v)
            denom = max(0.15, cosh2u - cos2v)
            x = 0.5 * (u - (math.sinh(2.0 * u) * cos2v) / denom)
            y = -0.5 * (v + (math.sinh(2.0 * u) * math.sin(2.0 * v)) / denom)
            z = 1.414 * (math.sinh(u) * math.cos(v)) / math.sqrt(denom)

            # 3D rotation
            x1 = x * math.cos(rotY) - y * math.sin(rotY)
            y1 = x * math.sin(rotY) + y * math.cos(rotY)
            z1 = z * math.cos(rotX) - y1 * math.sin(rotX)
            y2 = z * math.sin(rotX) + y1 * math.cos(rotX)

            px = cx + x1 * scale
            py = cy - z1 * scale
            pts.append((px, py, y2))
        lines.append(pts)

    # Sort layers by average depth
    for pts in lines:
        for j in range(1, len(pts)):
            depth = pts[j][2]
            frac = (depth + 1.5) / 3.0
            frac = max(0.0, min(1.0, frac))
            c = SIGNAL * (1.0 - frac) + EMERALD * frac
            alpha = int(140 + 100 * frac)
            draw.line([(pts[j-1][0], pts[j-1][1]), (pts[j][0], pts[j][1])], fill=(int(c[0]), int(c[1]), int(c[2]), alpha), width=2)

    im = im.filter(ImageFilter.SMOOTH)
    im.save(os.path.join(PREVIEWS_DIR, "costa-surface-3d.jpg"), quality=94)
    print("costa-surface-3d.jpg saved")

def render_catenoid_helicoid():
    """Bonnet isometric transformation minimal surface."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2
    scale = min(W, H) * 0.165

    alpha = math.pi * 0.25 # intermediate Bonnet minimal state
    cosA = math.cos(alpha)
    sinA = math.sin(alpha)
    rotY = 0.55
    rotX = 0.45

    rings = 36
    segs = 54
    uMin, uMax = -math.pi, math.pi
    vMin, vMax = -1.6, 1.6

    for i in range(rings + 1):
        u = uMin + (i / rings) * (uMax - uMin)
        pts = []
        for j in range(segs + 1):
            v = vMin + (j / segs) * (vMax - vMin)
            sinhV = math.sinh(v)
            coshV = math.cosh(v)
            sinU = math.sin(u)
            cosU = math.cos(u)

            x = cosA * sinhV * sinU + sinA * coshV * cosU
            y = -cosA * sinhV * cosU + sinA * coshV * sinU
            z = u * cosA + v * sinA

            x1 = x * math.cos(rotY) - y * math.sin(rotY)
            y1 = x * math.sin(rotY) + y * math.cos(rotY)
            z1 = z * math.cos(rotX) - y1 * math.sin(rotX)
            y2 = z * math.sin(rotX) + y1 * math.cos(rotX)

            px = cx + x1 * scale
            py = cy - z1 * scale
            pts.append((px, py, y2))

        frac = i / rings
        col = EMBER * frac + SIGNAL * (1.0 - frac)
        for j in range(1, len(pts)):
            draw.line([(pts[j-1][0], pts[j-1][1]), (pts[j][0], pts[j][1])], fill=(int(col[0]), int(col[1]), int(col[2]), 190), width=2)

    im = im.filter(ImageFilter.SMOOTH)
    im.save(os.path.join(PREVIEWS_DIR, "catenoid-helicoid-3d.jpg"), quality=94)
    print("catenoid-helicoid-3d.jpg saved")

def render_vector_morph():
    """Vector Morph Synth: 4-quadrant planar vector waves."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2

    # Draw vector joystick coordinate grid
    grid_w = min(W, H) * 0.42
    draw.rectangle([cx - grid_w, cy - grid_w, cx + grid_w, cy + grid_w], outline=(167, 177, 255, 60), width=1)
    draw.line([cx - grid_w, cy, cx + grid_w, cy], fill=(167, 177, 255, 50), width=1)
    draw.line([cx, cy - grid_w, cx, cy + grid_w], fill=(167, 177, 255, 50), width=1)

    # 4 Corner label boxes
    draw.ellipse([cx - grid_w - 6, cy - grid_w - 6, cx - grid_w + 6, cy - grid_w + 6], fill=(255, 107, 53, 220))
    draw.ellipse([cx + grid_w - 6, cy - grid_w - 6, cx + grid_w + 6, cy - grid_w + 6], fill=(167, 177, 255, 220))
    draw.ellipse([cx - grid_w - 6, cy + grid_w - 6, cx - grid_w + 6, cy + grid_w + 6], fill=(78, 242, 184, 220))
    draw.ellipse([cx + grid_w - 6, cy + grid_w - 6, cx + grid_w + 6, cy + grid_w + 6], fill=(245, 248, 255, 220))

    # Orbital trajectory
    orbit_pts = []
    for step in range(360):
        ang = step * math.pi / 180.0
        ox = cx + math.cos(ang) * (grid_w * 0.55)
        oy = cy + math.sin(ang) * (grid_w * 0.45)
        orbit_pts.append((ox, oy))
    for step in range(1, len(orbit_pts)):
        draw.line([orbit_pts[step-1], orbit_pts[step]], fill=(255, 107, 53, 110), width=1)

    # Waveform traces across the canvas
    wave_y = cy
    prev = None
    for x in range(W):
        phase = (x / W) * 8.0 * math.pi
        wA = math.sin(phase)
        wB = ((phase % (2 * math.pi)) / math.pi - 1.0) * 0.8
        wMix = wA * 0.6 + wB * 0.4
        y = wave_y + wMix * 85.0
        if prev:
            draw.line([prev, (x, y)], fill=(167, 177, 255, 230), width=3)
        prev = (x, y)

    im = im.filter(ImageFilter.SMOOTH)
    im.save(os.path.join(PREVIEWS_DIR, "vector-morph-synth.jpg"), quality=94)
    print("vector-morph-synth.jpg saved")

def render_resonant_drone():
    """Resonant Drone Synth: microtonal cluster beating Lissajous."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2
    scale = min(W, H) * 0.42

    steps = 4200
    pts = []
    for i in range(steps):
        t = i * 0.008
        x = math.sin(t * 1.000) * 0.38 + math.sin(t * 1.498) * 0.32 + math.sin(t * 2.996) * 0.22
        y = math.cos(t * 1.004) * 0.38 + math.cos(t * 2.001) * 0.32 + math.cos(t * 0.500) * 0.22
        pts.append((cx + x * scale, cy - y * scale))

    for i in range(1, len(pts)):
        frac = i / len(pts)
        c = EMERALD * frac + SIGNAL * (1.0 - frac)
        draw.line([pts[i-1], pts[i]], fill=(int(c[0]), int(c[1]), int(c[2]), 170), width=2)

    im = im.filter(ImageFilter.SMOOTH)
    im.save(os.path.join(PREVIEWS_DIR, "resonant-drone-synth.jpg"), quality=94)
    print("resonant-drone-synth.jpg saved")

def render_granular_freeze():
    """Granular Freeze Reverb FX: infinite spectral freeze diffusion cloud."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    cx, cy = W / 2, H / 2
    for c_idx in range(3):
        out[:, :, c_idx] = GROUND[c_idx]

    y_grid, x_grid = np.ogrid[:H, :W]
    dx = (x_grid - cx) / (min(W, H) * 0.45)
    dy = (y_grid - cy) / (min(W, H) * 0.45)
    r = np.sqrt(dx**2 + dy**2)
    a = np.arctan2(dy, dx)

    rays = np.cos(a * 14.0 + np.sin(r * 18.0) * 1.8)
    freeze_core = np.clip((0.6 - r) / 0.5, 0, 1)**2.0
    cloud = np.clip((rays - 0.2) / 0.75, 0, 1) * np.clip((0.9 - r) / 0.8, 0, 1)

    spark = np.sin(dx * 28.0) * np.cos(dy * 28.0)
    grains = np.clip((spark - 0.6) / 0.38, 0, 1)**2.0

    for y_idx in range(H):
        for x_idx in range(W):
            rad = r[y_idx, x_idx]
            fc = freeze_core[y_idx, x_idx]
            cl = cloud[y_idx, x_idx]
            gr = grains[y_idx, x_idx]

            mix_v = min(1.0, rad * 1.5)
            glow = SIGNAL * (1.0 - mix_v) + EMERALD * mix_v
            diffuse = glow * (fc * 1.5 + cl * 1.8)
            shimmer = EMBER * gr * 1.4

            col = GROUND * (1.0 - min(1.0, fc + cl)) + diffuse + shimmer
            vig = np.clip((1.6 - rad) / 0.7, 0, 1)
            out[y_idx, x_idx] = np.clip(col * vig + GROUND * (1.0 - vig), 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "granular-freeze-reverb-fx.jpg"), quality=94)
    print("granular-freeze-reverb-fx.jpg saved")

def render_analog_chorus():
    """Analog Chorus Flanger FX: quadrature BBD delay lines."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2

    # Draw dual modulated waveforms
    for wave_idx, (offset, col) in enumerate([(0, EMERALD), (math.pi * 0.5, EMBER)]):
        prev = None
        for x in range(W):
            phase = (x / W) * 10.0 * math.pi
            mod = math.sin(phase * 0.3 + offset) * 0.35
            y_val = cy + (math.sin(phase + mod * 4.0) + math.sin(phase * 2.0) * 0.3) * 65.0 + (wave_idx - 0.5) * 45.0
            if prev:
                draw.line([prev, (x, y_val)], fill=(int(col[0]), int(col[1]), int(col[2]), 220), width=3)
            prev = (x, y_val)

    im = im.filter(ImageFilter.SMOOTH)
    im.save(os.path.join(PREVIEWS_DIR, "analog-chorus-flanger-fx.jpg"), quality=94)
    print("analog-chorus-flanger-fx.jpg saved")

def render_reaction_diffusion():
    """Reaction Diffusion Core: Gray-Scott Turing pattern."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    cx, cy = W / 2, H / 2
    for c_idx in range(3):
        out[:, :, c_idx] = GROUND[c_idx]

    y_grid, x_grid = np.ogrid[:H, :W]
    dx = (x_grid - cx) / (min(W, H) * 0.45)
    dy = (y_grid - cy) / (min(W, H) * 0.45)
    r = np.sqrt(dx**2 + dy**2)

    w1 = np.sin(dx * 24.0 + np.sin(dy * 18.0))
    w2 = np.cos(dy * 24.0 + np.cos(dx * 18.0))
    w3 = np.sin((dx + dy) * 16.0)

    chemB = np.clip(((w1 * w2 + w3 * 0.5) * 0.5 + 0.5 - 0.2) / 0.65, 0, 1)
    spots = np.clip((np.sin(dx * 38.0) * np.cos(dy * 38.0) + chemB - 0.72) / 0.26, 0, 1)**2.0
    core = np.clip((0.55 - r) / 0.45, 0, 1)**1.8

    for y_idx in range(H):
        for x_idx in range(W):
            rad = r[y_idx, x_idx]
            b_val = chemB[y_idx, x_idx]
            s_val = spots[y_idx, x_idx]
            c_val = core[y_idx, x_idx]

            turing = (EMERALD * (1.0 - b_val) + SIGNAL * b_val) * (0.5 + 0.8 * b_val)
            pattern = turing * (0.4 + 0.9 * c_val) + EMBER * s_val * 1.4

            col = GROUND * (1.0 - b_val * 0.8) + pattern
            vig = np.clip((1.65 - rad) / 0.7, 0, 1)
            out[y_idx, x_idx] = np.clip(col * vig + GROUND * (1.0 - vig), 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "reaction-diffusion-core.jpg"), quality=94)
    print("reaction-diffusion-core.jpg saved")

def render_cymatic_liquid():
    """Cymatic Liquid Matrix: Faraday standing wave resonance."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    cx, cy = W / 2, H / 2
    for c_idx in range(3):
        out[:, :, c_idx] = GROUND[c_idx]

    y_grid, x_grid = np.ogrid[:H, :W]
    dx = (x_grid - cx) / (min(W, H) * 0.45)
    dy = (y_grid - cy) / (min(W, H) * 0.45)
    r = np.sqrt(dx**2 + dy**2)
    a = np.arctan2(dy, dx)

    modeM = 6.0
    freq = 18.0
    modal = np.cos(a * modeM) * np.sin(r * freq)
    harm1 = np.cos(a * (modeM * 2.0)) * np.sin(r * (freq * 1.6))
    harm2 = np.sin(dx * 22.0) * np.sin(dy * 22.0)

    h = (modal * 0.55 + harm1 * 0.3 + harm2 * 0.15)
    rim = np.clip((0.6 - r) / 0.5, 0, 1)

    for y_idx in range(H):
        for x_idx in range(W):
            rad = r[y_idx, x_idx]
            h_val = h[y_idx, x_idx]
            rim_val = rim[y_idx, x_idx]

            fluid = GROUND * (1.0 - h_val * 0.4) + SIGNAL * (h_val * 0.5 + 0.5) * 0.6
            glow = EMERALD * rim_val * 0.7
            spec = EMBER * (max(0.0, h_val)**4.0) * 1.6

            col = fluid + glow + spec
            vig = np.clip((1.65 - rad) / 0.7, 0, 1)
            out[y_idx, x_idx] = np.clip(col * vig + GROUND * (1.0 - vig), 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "cymatic-liquid-matrix.jpg"), quality=94)
    print("cymatic-liquid-matrix.jpg saved")

if __name__ == "__main__":
    render_chen()
    render_spirograph()
    render_costa()
    render_catenoid_helicoid()
    render_vector_morph()
    render_resonant_drone()
    render_granular_freeze()
    render_analog_chorus()
    render_reaction_diffusion()
    render_cymatic_liquid()
    print("All 10 preview images successfully generated!")

    # Run library thumbs generator
    thumbs_script = os.path.join(ROOT, "tools", "website", "gen_library_thumbs.py")
    if os.path.exists(thumbs_script):
        subprocess.run([sys.executable, thumbs_script], check=True)
