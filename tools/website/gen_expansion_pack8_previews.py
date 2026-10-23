#!/usr/bin/env python3
"""Generate high-fidelity preview images for the 10 new creative library items (Pack 8).
Matches brand design: midnight ground #151930, signal #A7B1FF, ember #FF6B35, emerald #4EF2B8.
"""
import os, math, sys
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

def render_hyperbolic_poincare_disk():
    """Conformal Poincaré disk with {5, 4} hyperbolic tiling and geodesic arcs."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5
    diskR = min(W, H) * 0.44

    # Horizon circle at infinity
    draw.ellipse([cx - diskR, cy - diskR, cx + diskR, cy + diskR],
                 outline=(167, 177, 255, 220), width=3)

    p = 5
    coshR = math.cos(math.pi / 4.0) / math.sin(math.pi / p)
    tanhHalfR = math.sqrt((coshR - 1.0) / (coshR + 1.0))
    r0 = tanhHalfR

    verts = []
    for i in range(p):
        angle = (i * 2.0 * math.pi) / p
        verts.append((cx + diskR * r0 * math.cos(angle), cy + diskR * r0 * math.sin(angle)))

    def draw_geodesic(p1, p2, col, width):
        u1 = (p1[0] - cx) / diskR
        v1 = (p1[1] - cy) / diskR
        u2 = (p2[0] - cx) / diskR
        v2 = (p2[1] - cy) / diskR
        cross = u1 * v2 - v1 * u2
        if abs(cross) < 1e-4:
            draw.line([p1, p2], fill=col, width=width)
            return
        d1 = 1.0 + u1 * u1 + v1 * v1
        d2 = 1.0 + u2 * u2 + v2 * v2
        cX = (v2 * d1 - v1 * d2) / (2.0 * cross)
        cY = (u1 * d2 - u2 * d1) / (2.0 * cross)
        rC2 = cX * cX + cY * cY - 1.0
        if rC2 <= 0:
            draw.line([p1, p2], fill=col, width=width)
            return
        rC = math.sqrt(rC2)
        a1 = math.atan2(v1 - cY, u1 - cX)
        a2 = math.atan2(v2 - cY, u2 - cX)
        diff = a2 - a1
        while diff > math.pi: diff -= 2.0 * math.pi
        while diff < -math.pi: diff += 2.0 * math.pi
        steps = 24
        prev = p1
        for k in range(1, steps + 1):
            a = a1 + (diff * k) / steps
            px = cx + (cX + rC * math.cos(a)) * diskR
            py = cy + (cY + rC * math.sin(a)) * diskR
            draw.line([prev, (px, py)], fill=col, width=width)
            prev = (px, py)

    # Draw central hyperbolic geodesics
    for i in range(p):
        draw_geodesic(verts[i], verts[(i + 1) % p], (167, 177, 255, 230), 3)

    # Inverted concentric tiers
    for tier in range(1, 4):
        frac = tier / 3.0
        r_tier = r0 + (1.0 - r0) * (1.0 - math.pow(0.5, tier))
        col = (int(EMERALD[0] * (1.0 - frac) + EMBER[0] * frac),
               int(EMERALD[1] * (1.0 - frac) + EMBER[1] * frac),
               int(EMERALD[2] * (1.0 - frac) + EMBER[2] * frac), 180)
        n_pts = p * (2 ** tier)
        tier_verts = []
        for k in range(n_pts):
            a = (k * 2.0 * math.pi) / n_pts
            tier_verts.append((cx + diskR * r_tier * math.cos(a), cy + diskR * r_tier * math.sin(a)))
        for k in range(n_pts):
            draw_geodesic(tier_verts[k], tier_verts[(k + 1) % n_pts], col, max(1, 3 - tier))

    # Glowing quanta
    for q in range(8):
        ang = (q * 2.0 * math.pi) / 8
        qx = cx + diskR * r0 * 0.7 * math.cos(ang)
        qy = cy + diskR * r0 * 0.7 * math.sin(ang)
        draw.ellipse([qx - 5, qy - 5, qx + 5, qy + 5], fill=(255, 107, 53, 240))

    im.save(os.path.join(PREVIEWS_DIR, "hyperbolic-poincare-disk.jpg"), quality=94)
    print("hyperbolic-poincare-disk.jpg saved")

def render_chladni_plate_harmonics():
    """Ernst Chladni acoustic nodal patterns on a square plate with sand grains."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5
    size = min(W, H) * 0.8
    half = size * 0.5

    # Plate boundary
    draw.rounded_rectangle([cx - half, cy - half, cx + half, cy + half], radius=12,
                           outline=(167, 177, 255, 120), width=2)

    m, n = 3.0, 5.0
    a, b = 1.0, 1.0

    # Continuous nodal field rendering
    grid = 80
    for gy in range(grid):
        y0 = ((gy / grid) - 0.5) * 2.0
        y1 = (((gy + 1) / grid) - 0.5) * 2.0
        py0 = cy + y0 * half
        py1 = cy + y1 * half
        for gx in range(grid):
            x0 = ((gx / grid) - 0.5) * 2.0
            x1 = (((gx + 1) / grid) - 0.5) * 2.0
            px0 = cx + x0 * half
            px1 = cx + x1 * half

            def w(x, y):
                return a * math.sin(n * math.pi * x) * math.sin(m * math.pi * y) - \
                       b * math.sin(m * math.pi * x) * math.sin(n * math.pi * y)

            v00, v10 = w(x0, y0), w(x1, y0)
            v01, v11 = w(x0, y1), w(x1, y1)
            minV = min(v00, min(v10, min(v01, v11)))
            maxV = max(v00, max(v10, max(v01, v11)))

            if minV <= 0 <= maxV:
                draw.line([(px0, (py0 + py1) * 0.5), (px1, (py0 + py1) * 0.5)],
                          fill=(78, 242, 184, 140), width=2)

    # Sand particles along nodal lines
    np.random.seed(42)
    for _ in range(1600):
        gx = np.random.uniform(-0.95, 0.95)
        gy = np.random.uniform(-0.95, 0.95)
        # Nudge toward nodal line
        for _ in range(5):
            val = a * math.sin(n * math.pi * gx) * math.sin(m * math.pi * gy) - \
                  b * math.sin(m * math.pi * gx) * math.sin(n * math.pi * gy)
            gx -= val * 0.05
            gy -= val * 0.05
        if abs(val) < 0.25:
            px = cx + gx * half
            py = cy + gy * half
            draw.ellipse([px - 1.8, py - 1.8, px + 1.8, py + 1.8], fill=(255, 107, 53, 220))

    im.save(os.path.join(PREVIEWS_DIR, "chladni-plate-harmonics.jpg"), quality=94)
    print("chladni-plate-harmonics.jpg saved")

def render_gyroid_minimal_surface():
    """Alan Schoen's Gyroid minimal surface labyrinth projection."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5
    scale = min(W, H) * 0.22

    rotY, rotX = 0.85, 0.55
    cosY, sinY = math.cos(rotY), math.sin(rotY)
    cosX, sinX = math.cos(rotX), math.sin(rotX)

    def project(x, y, z):
        x1 = x * cosY - z * sinY
        z1 = x * sinY + z * cosY
        y1 = y * cosX - z1 * sinX
        return cx + x1 * scale, cy - y1 * scale

    ribs = 36
    uSteps = 50
    for i in range(ribs):
        fracZ = (i / (ribs - 1) - 0.5) * 2.0
        z = fracZ * math.pi * 1.5
        hFrac = i / ribs
        col = SIGNAL * (1.0 - hFrac) + EMERALD * hFrac
        r_c, g_c, b_c = [int(v) for v in np.clip(col, 0, 255)]

        prev_pt = None
        for j in range(uSteps + 1):
            fracU = (j / uSteps - 0.5) * 2.0
            x = fracU * math.pi * 1.5

            sX = math.sin(x)
            cZ = math.cos(z)
            target = -math.sin(z) * math.cos(x)
            amp = math.sqrt(sX * sX + cZ * cZ)
            if amp < 1e-4: continue
            ratio = max(-0.99, min(0.99, target / amp))
            y = math.asin(ratio) - math.atan2(cZ, sX)

            px, py = project(x, y, z)
            if prev_pt is not None and abs(py - prev_pt[1]) < scale * 0.8:
                draw.line([prev_pt, (px, py)], fill=(r_c, g_c, b_c, 190), width=2)
            prev_pt = (px, py)

    im.save(os.path.join(PREVIEWS_DIR, "gyroid-minimal-surface-3d.jpg"), quality=94)
    print("gyroid-minimal-surface-3d.jpg saved")

def render_villarceau_circles():
    """Yvon Villarceau bitangent circle families on a torus."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5
    scale = min(W, H) * 0.21

    rotY, rotX = 0.75, 0.6
    cosY, sinY = math.cos(rotY), math.sin(rotY)
    cosX, sinX = math.cos(rotX), math.sin(rotX)

    def project(x, y, z):
        x1 = x * cosY - z * sinY
        z1 = x * sinY + z * cosY
        y1 = y * cosX - z1 * sinX
        return cx + x1 * scale, cy - y1 * scale

    R, r = 1.45, 0.72
    alpha = math.asin(r / R)
    cosA, sinA = math.cos(alpha), math.sin(alpha)

    nCircles = 24
    pts = 64
    for sign in (1.0, -1.0):
        for i in range(nCircles):
            phi = (i / nCircles) * 2.0 * math.pi
            cosP, sinP = math.cos(phi), math.sin(phi)
            frac = i / nCircles
            col = (SIGNAL * (1.0 - frac) + EMBER * frac) if sign > 0 else EMERALD
            r_c, g_c, b_c = [int(v) for v in np.clip(col, 0, 255)]

            prev_pt = None
            for k in range(pts + 1):
                theta = (k / pts) * 2.0 * math.pi
                cX_ = R * math.cos(theta)
                cY_ = R * math.sin(theta) * cosA - sign * r * sinA
                cZ_ = R * math.sin(theta) * sinA + sign * r * cosA

                x = cX_ * cosP - cY_ * sinP
                y = cX_ * sinP + cY_ * cosP
                z = cZ_

                px, py = project(x, y, z)
                if prev_pt is not None:
                    draw.line([prev_pt, (px, py)], fill=(r_c, g_c, b_c, 180 if sign > 0 else 120), width=2)
                prev_pt = (px, py)

    im.save(os.path.join(PREVIEWS_DIR, "villarceau-circles-3d.jpg"), quality=94)
    print("villarceau-circles-3d.jpg saved")

def render_inharmonic_chime():
    """Inharmonic plate modal chime spectrum and multi-frequency ringing waveform."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5

    # 6 Bessel zeros partial bars
    ratios = [1.0, 2.76, 5.40, 8.93, 13.34, 18.64]
    amps = [1.0, 0.85, 0.65, 0.45, 0.30, 0.18]

    bar_w = 48
    gap = 28
    start_x = (W - (len(ratios) * (bar_w + gap) - gap)) // 2
    base_y = H * 0.78

    for idx in range(len(ratios)):
        bx = start_x + idx * (bar_w + gap)
        bh = int(amps[idx] * 210)
        by = base_y - bh
        frac = idx / len(ratios)
        col = EMBER * (1.0 - frac) + SIGNAL * frac
        r, g, b = [int(v) for v in np.clip(col, 0, 255)]
        draw.rectangle([bx, by, bx + bar_w, base_y], fill=(r, g, b, 220))
        draw.rectangle([bx, by, bx + bar_w, by + 6], fill=(245, 248, 255, 255))

    # Inharmonic ringing waveform trace at top
    wave_pts = []
    n_pts = 700
    cy_wave = H * 0.28
    for i in range(n_pts):
        x_frac = i / n_pts
        phase = x_frac * 8.0 * math.pi
        decay = math.exp(-x_frac * 2.5)
        val = sum(math.sin(phase * ratios[k]) * amps[k] * math.exp(-x_frac * ratios[k] * 0.3)
                  for k in range(len(ratios))) * decay * 0.35
        px = 60 + x_frac * (W - 120)
        py = cy_wave - val * 120.0
        wave_pts.append((px, py))

    for i in range(len(wave_pts) - 1):
        draw.line([wave_pts[i], wave_pts[i+1]], fill=(78, 242, 184, 230), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "inharmonic-chime-synth.jpg"), quality=94)
    print("inharmonic-chime-synth.jpg saved")

def render_analog_supershape_pad():
    """Analog supershape pad waveform with 3 detuned cores and 24dB ladder filter curve."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5

    # Oscilloscope grid
    for gx in range(60, W, 80):
        draw.line([(gx, 40), (gx, H - 40)], fill=(32, 38, 63, 120), width=1)
    for gy in range(40, H, 60):
        draw.line([(60, gy), (W - 60, gy)], fill=(32, 38, 63, 120), width=1)

    pts = []
    n_pts = 750
    for i in range(n_pts):
        phase = (i / n_pts) * 6.0 * math.pi
        o1 = math.sin(phase)
        o2 = math.sin(phase * 1.015)
        o3 = math.sin(phase * 0.985)
        raw = (o1 + o2 * 0.85 + o3 * 0.85) * 0.4
        fold = raw + math.sin(raw * 2.5) * 0.3
        y_val = fold / (1.0 + abs(fold) * 0.3)
        px = 60 + (i / n_pts) * (W - 120)
        py = cy - y_val * 170.0
        pts.append((px, py))

    for i in range(len(pts) - 1):
        draw.line([pts[i], pts[i+1]], fill=(167, 177, 255, 90), width=8)
    for i in range(len(pts) - 1):
        frac = i / len(pts)
        r = int(SIGNAL[0] * (1.0 - frac) + EMBER[0] * frac)
        g = int(SIGNAL[1] * (1.0 - frac) + EMBER[1] * frac)
        b = int(SIGNAL[2] * (1.0 - frac) + EMBER[2] * frac)
        draw.line([pts[i], pts[i+1]], fill=(r, g, b, 240), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "analog-supershape-pad-synth.jpg"), quality=94)
    print("analog-supershape-pad-synth.jpg saved")

def render_multiband_tape_saturator():
    """3-band crossover tape saturator transfer curve and hysteresis loop."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5

    # Tape magnetic hysteresis S-curves
    n_pts = 500
    pts_sat = []
    pts_lin = []
    for i in range(n_pts):
        x_val = -2.5 + (5.0 * i) / n_pts
        y_sat = math.tanh(x_val * 1.3)
        px = cx + x_val * 130.0
        py = cy - y_sat * 180.0
        pts_sat.append((px, py))
        pts_lin.append((px, cy - (x_val * 0.4) * 180.0))

    # Grid axes
    draw.line([(cx - 320, cy), (cx + 320, cy)], fill=(45, 52, 85, 200), width=2)
    draw.line([(cx, cy - 220), (cx, cy + 220)], fill=(45, 52, 85, 200), width=2)

    # Reference linear response
    for i in range(len(pts_lin) - 1):
        draw.line([pts_lin[i], pts_lin[i+1]], fill=(80, 90, 130, 140), width=1)

    # Glowing saturation curve
    for i in range(len(pts_sat) - 1):
        draw.line([pts_sat[i], pts_sat[i+1]], fill=(255, 107, 53, 90), width=7)
    for i in range(len(pts_sat) - 1):
        draw.line([pts_sat[i], pts_sat[i+1]], fill=(255, 107, 53, 240), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "multiband-tape-saturator-fx.jpg"), quality=94)
    print("multiband-tape-saturator-fx.jpg saved")

def render_dimension_spatial_chorus():
    """Multi-stage bucket brigade chorus with out-of-phase LFO spatial diffusion."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5

    # Out-of-phase dual LFO Lissajous orbit
    pts_lissa = []
    n_lissa = 600
    for i in range(n_lissa):
        theta = (i / n_lissa) * 2.0 * math.pi
        lx = math.sin(theta) * 220.0
        ly = math.cos(theta * 1.0 + math.pi * 0.25) * 160.0
        pts_lissa.append((cx + lx, cy + ly))

    for i in range(len(pts_lissa) - 1):
        frac = i / len(pts_lissa)
        col = SIGNAL * (1.0 - frac) + EMERALD * frac
        r, g, b = [int(v) for v in np.clip(col, 0, 255)]
        draw.line([pts_lissa[i], pts_lissa[i+1]], fill=(r, g, b, 190), width=2)

    # Concentric BBD spatial tap circles
    for r in (50, 110, 170, 230):
        draw.ellipse([cx - r, cy - r * 0.7, cx + r, cy + r * 0.7],
                     outline=(167, 177, 255, 80), width=1)

    im.save(os.path.join(PREVIEWS_DIR, "dimension-spatial-chorus-fx.jpg"), quality=94)
    print("dimension-spatial-chorus-fx.jpg saved")

def render_hyperbolic_disk_fractal():
    """Conformal Poincaré disk with infinite Apollonian circle inversions."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    aspect = W / H
    scale = 2.3

    y_indices, x_indices = np.indices((H, W))
    p_x = ((x_indices / W) - 0.5) * aspect * scale
    p_y = ((y_indices / H) - 0.5) * scale

    r = np.sqrt(p_x**2 + p_y**2)
    disk = np.clip((1.02 - r) / 0.04, 0.0, 1.0)

    z_x = np.copy(p_x)
    z_y = np.copy(p_y)
    accum = np.zeros((H, W), dtype=np.float32)
    r0 = 0.58

    for _ in range(7):
        z_x = np.abs(z_x)
        z_y = np.abs(z_y)
        swap = z_x < z_y
        tmp = np.where(swap, z_x, z_y)
        z_x = np.where(swap, z_y, z_x)
        z_y = tmp

        d2 = z_x**2 + z_y**2
        invScale = (r0 * r0) / np.maximum(0.08, d2)
        z_x *= invScale
        z_y *= invScale
        accum += np.exp(-np.abs(d2 - r0 * r0) * 14.0)

    horizon = np.exp(-np.abs(r - 0.96) * 36.0) * 2.2

    for y in range(H):
        for x in range(W):
            ac = accum[y, x]
            r_val = r[y, x]
            phase = ac * 0.5 + r_val * 4.0
            vertexCol = SIGNAL * (0.5 + 0.5 * math.sin(phase * 3.5)) + EMERALD * (0.5 - 0.5 * math.sin(phase * 3.5))
            vertexCol = vertexCol * 0.5 + EMBER * (0.5 + 0.5 * math.cos(phase * 2.0))

            c_vec = GROUND + vertexCol * ac * 0.55 + (SIGNAL * 0.2 + WHITE * 0.8) * horizon[y, x]
            c_vec *= disk[y, x]
            out[y, x] = np.clip(c_vec, 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "hyperbolic-disk-fractal.jpg"), quality=94)
    print("hyperbolic-disk-fractal.jpg saved")

def render_magnetar_quantum_flux():
    """Ultra-magnetized neutron star dipole flux tubes and synchrotron jets."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    aspect = W / H
    scale = 2.2

    y_indices, x_indices = np.indices((H, W))
    p_x = ((x_indices / W) - 0.5) * aspect * scale
    p_y = ((y_indices / H) - 0.5) * scale

    r = np.sqrt(p_x**2 + p_y**2)
    angle = np.arctan2(p_y, p_x)

    dipoleR = np.maximum(0.12, r)
    sinTheta = p_x / dipoleR

    fluxLine = np.abs(dipoleR - sinTheta * sinTheta * 1.4 * 2.2)
    fluxGlow = np.exp(-fluxLine * 16.0)

    jetDist = np.abs(p_x) / np.maximum(0.15, np.abs(p_y) * 0.45 + 0.1)
    jetCollimated = np.exp(-jetDist * 18.0) * np.exp(-np.abs(p_y) * 0.5) * 1.8

    helicity = np.sin(angle * 4.0 + dipoleR * 8.0)
    tubeGlow = np.exp(-np.abs(helicity) * 6.0) * np.exp(-dipoleR * 1.5)

    coreGlow = np.exp(-r * 8.0) * 3.5
    fade = np.clip((2.2 - r) / 1.3, 0.0, 1.0)

    for y in range(H):
        for x in range(W):
            ang = angle[y, x]
            plasmaCol = EMBER * (0.5 + 0.5 * math.sin(ang * 2.0)) + SIGNAL * (0.5 - 0.5 * math.sin(ang * 2.0))
            plasmaCol = plasmaCol * 0.5 + EMERALD * (tubeGlow[y, x] * 0.6)

            c_vec = GROUND + plasmaCol * (fluxGlow[y, x] * 1.8 + tubeGlow[y, x] * 1.4) * 1.2
            c_vec += (SIGNAL * 0.25 + WHITE * 0.75) * jetCollimated[y, x] * 2.2 + WHITE * coreGlow[y, x]
            c_vec *= fade[y, x]
            out[y, x] = np.clip(c_vec, 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "magnetar-quantum-flux.jpg"), quality=94)
    print("magnetar-quantum-flux.jpg saved")

def main():
    render_hyperbolic_poincare_disk()
    render_chladni_plate_harmonics()
    render_gyroid_minimal_surface()
    render_villarceau_circles()
    render_inharmonic_chime()
    render_analog_supershape_pad()
    render_multiband_tape_saturator()
    render_dimension_spatial_chorus()
    render_hyperbolic_disk_fractal()
    render_magnetar_quantum_flux()
    print("All Pack 8 previews generated successfully.")

if __name__ == "__main__":
    main()
