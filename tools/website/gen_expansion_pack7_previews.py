#!/usr/bin/env python3
"""Generate high-fidelity preview images for the 10 new creative library items (Pack 7).
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

def render_sprott_attractor():
    """Sprott minimal chaotic flow with quadratic nonlinearity."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5
    scale = min(W, H) * 0.28

    dt = 0.018
    x, y, z = 0.63, 0.47, -0.52
    steps = 18000
    pts = []
    speeds = []

    rotY = 0.85
    rotX = 0.52
    cosY, sinY = math.cos(rotY), math.sin(rotY)
    cosX, sinX = math.cos(rotX), math.sin(rotX)

    for i in range(steps):
        dx = y * z
        dy = x - y
        dz = 1.0 - x * y
        x += dx * dt
        y += dy * dt
        z += dz * dt

        if i > 200:
            speed = math.sqrt(dx * dx + dy * dy + dz * dz)
            x1 = x * cosY - y * sinY
            y1_ = x * sinY + y * cosY
            y1 = y1_ * cosX - z * sinX
            px = cx + x1 * scale
            py = cy - y1 * scale
            pts.append((px, py))
            speeds.append(speed)

    max_spd = max(speeds) if speeds else 1.0
    for i in range(len(pts) - 1):
        frac = i / len(pts)
        spd = speeds[i] / max_spd
        r = int(SIGNAL[0] * (1.0 - spd) + EMBER[0] * spd)
        g = int(SIGNAL[1] * (1.0 - spd) + EMBER[1] * spd)
        b = int(SIGNAL[2] * (1.0 - spd) + EMBER[2] * spd)
        alpha = int(255 * (0.35 + 0.65 * frac))
        draw.line([pts[i], pts[i+1]], fill=(r, g, b, alpha), width=2)

    im.save(os.path.join(PREVIEWS_DIR, "sprott-attractor.jpg"), quality=94)
    print("sprott-attractor.jpg saved")

def render_apollonian_gasket():
    """Fractal circle packing governed by the Descartes Circle Theorem."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5
    baseR = min(W, H) * 0.46

    circles = []
    circles.append({'x': cx, 'y': cy, 'r': baseR, 'k': -1.0 / baseR, 'gen': 0})

    r1 = baseR / (1.0 + 2.0 / math.sqrt(3.0))
    k1 = 1.0 / r1
    dist = baseR - r1

    for i in range(3):
        angle = (i * 2.0 * math.pi) / 3.0
        circles.append({
            'x': cx + dist * math.cos(angle),
            'y': cy + dist * math.sin(angle),
            'r': r1, 'k': k1, 'gen': 1
        })

    kInner = -circles[0]['k'] + 2.0 * math.sqrt(3.0) * k1
    rInner = abs(1.0 / kInner)
    circles.append({'x': cx, 'y': cy, 'r': rInner, 'k': kInner, 'gen': 1})

    queue = [
        (circles[0], circles[1], circles[2]),
        (circles[0], circles[2], circles[3]),
        (circles[0], circles[3], circles[1]),
        (circles[1], circles[2], circles[4]),
        (circles[2], circles[3], circles[4]),
        (circles[3], circles[1], circles[4])
    ]

    head = 0
    maxGen = 5
    while head < len(queue) and len(circles) < 1200:
        c1, c2, c3 = queue[head]
        head += 1
        gen = max(c1['gen'], max(c2['gen'], c3['gen'])) + 1
        if gen > maxGen:
            continue

        kSum = c1['k'] + c2['k'] + c3['k']
        kProd = c1['k'] * c2['k'] + c2['k'] * c3['k'] + c3['k'] * c1['k']
        if kProd < 0:
            continue
        kRoot = 2.0 * math.sqrt(kProd)
        k4a = kSum + kRoot
        k4b = kSum - kRoot
        k4 = k4a if k4a > 0 else k4b
        if k4 <= 0:
            continue
        r4 = 1.0 / k4
        if r4 < 1.5:
            continue

        wx = (c1['x'] * c1['k'] + c2['x'] * c2['k'] + c3['x'] * c3['k']) / (c1['k'] + c2['k'] + c3['k'])
        wy = (c1['y'] * c1['k'] + c2['y'] * c2['k'] + c3['y'] * c3['k']) / (c1['k'] + c2['k'] + c3['k'])
        dCenter = math.sqrt((wx - cx)**2 + (wy - cy)**2)
        if dCenter + r4 <= baseR * 1.02:
            newC = {'x': wx, 'y': wy, 'r': r4, 'k': k4, 'gen': gen}
            circles.append(newC)
            queue.append((c1, c2, newC))
            queue.append((c2, c3, newC))
            queue.append((c3, c1, newC))

    for c in circles:
        frac = c['gen'] / maxGen
        dRel = math.sqrt((c['x'] - cx)**2 + (c['y'] - cy)**2) / baseR
        mixCol = SIGNAL * (1.0 - frac) + EMERALD * (frac * 0.7) + EMBER * (dRel * 0.6)
        mixCol = np.clip(mixCol, 0, 255).astype(int)
        alpha = int(255 * (0.35 + 0.65 * (1.0 - frac * 0.5)))
        wLine = max(1, int(3 * (1.0 - frac * 0.5)))
        r = c['r']
        draw.ellipse([c['x'] - r, c['y'] - r, c['x'] + r, c['y'] + r],
                     outline=(int(mixCol[0]), int(mixCol[1]), int(mixCol[2]), alpha), width=wLine)

    im.save(os.path.join(PREVIEWS_DIR, "apollonian-gasket.jpg"), quality=94)
    print("apollonian-gasket.jpg saved")

def render_plucker_conoid():
    """Julius Plücker's ruled algebraic surface."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.52
    scaleR = min(W, H) * 0.38
    scaleH = min(W, H) * 0.22

    rotY = 0.75
    rotX = 0.58
    cosY, sinY = math.cos(rotY), math.sin(rotY)
    cosX, sinX = math.cos(rotX), math.sin(rotX)

    def project(x, y, z):
        x1 = x * cosY - z * sinY
        z1 = x * sinY + z * cosY
        y1 = y * cosX - z1 * sinX
        return cx + x1, cy - y1

    lobes = 3
    ribs = 64
    uSteps = 32

    # Ruling generator lines
    for i in range(ribs):
        fracV = i / ribs
        v = fracV * 2.0 * math.pi
        hFrac = fracV
        mixCol = SIGNAL * (1.0 - hFrac) + EMERALD * hFrac
        r_c, g_c, b_c = [int(v) for v in np.clip(mixCol, 0, 255)]

        prev_pt = None
        for j in range(uSteps + 1):
            u = -1.0 + (2.0 * j) / uSteps
            x = u * scaleR * math.cos(v)
            z = u * scaleR * math.sin(v)
            y = scaleH * math.sin(lobes * v)
            px, py = project(x, y, z)
            if prev_pt is not None:
                draw.line([prev_pt, (px, py)], fill=(r_c, g_c, b_c, 180), width=2)
            prev_pt = (px, py)

    # Concentric contour rings
    for r in range(1, 10):
        u = r / 9.0
        mixCol = EMBER * (u * 0.8) + WHITE * (1.0 - u * 0.5)
        r_c, g_c, b_c = [int(v) for v in np.clip(mixCol, 0, 255)]
        prev_pt = None
        ringPts = 80
        for k in range(ringPts + 1):
            v = (k / ringPts) * 2.0 * math.pi
            x = u * scaleR * math.cos(v)
            z = u * scaleR * math.sin(v)
            y = scaleH * math.sin(lobes * v)
            px, py = project(x, y, z)
            if prev_pt is not None:
                draw.line([prev_pt, (px, py)], fill=(r_c, g_c, b_c, 210), width=2)
            prev_pt = (px, py)

    im.save(os.path.join(PREVIEWS_DIR, "plucker-conoid-3d.jpg"), quality=94)
    print("plucker-conoid-3d.jpg saved")

def render_seifert_surface():
    """Orientable topological Seifert surface spanning a knotted boundary."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5
    scale = min(W, H) * 0.16

    rotY = 0.8
    rotX = 0.5
    cosY, sinY = math.cos(rotY), math.sin(rotY)
    cosX, sinX = math.cos(rotX), math.sin(rotX)

    def project(x, y, z):
        x1 = x * cosY - z * sinY
        z1 = x * sinY + z * cosY
        y1 = y * cosX - z1 * sinX
        return cx + x1 * scale, cy - y1 * scale

    segs = 120
    strips = 24
    ribbon_w = 0.65
    qTwist = 3

    centerPts = []
    for i in range(segs + 1):
        u = (i / segs) * 2.0 * math.pi
        x = math.sin(u) + 2.0 * math.sin(2.0 * u)
        z = math.cos(u) - 2.0 * math.cos(2.0 * u)
        y = -math.sin(3.0 * u) * 1.2
        centerPts.append((x, y, z, u))

    for m in range(strips + 1):
        v = -1.0 + (2.0 * m) / strips
        fracV = m / strips
        mixCol = SIGNAL * (1.0 - fracV) + EMBER * fracV
        r_c, g_c, b_c = [int(val) for val in np.clip(mixCol, 0, 255)]

        prev_pt = None
        for i in range(segs + 1):
            c = centerPts[i]
            u = c[3]
            theta = (qTwist * u) * 0.5
            nx = math.cos(theta) * math.cos(u)
            ny = math.sin(theta)
            nz = math.cos(theta) * math.sin(u)

            x = c[0] + v * ribbon_w * nx
            y = c[1] + v * ribbon_w * ny
            z = c[2] + v * ribbon_w * nz
            px, py = project(x, y, z)
            if prev_pt is not None:
                draw.line([prev_pt, (px, py)], fill=(r_c, g_c, b_c, 190), width=2)
            prev_pt = (px, py)

    im.save(os.path.join(PREVIEWS_DIR, "seifert-surface-3d.jpg"), quality=94)
    print("seifert-surface-3d.jpg saved")

def render_feedback_fm_lead():
    """Two-operator FM synthesizer with nonlinear modulator self-feedback."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    # Oscilloscope waveform traces
    cx, cy = W * 0.5, H * 0.5
    n_pts = 800

    # Draw background oscilloscope grid
    for gx in range(50, W, 80):
        draw.line([(gx, 40), (gx, H - 40)], fill=(32, 38, 63, 120), width=1)
    for gy in range(40, H, 60):
        draw.line([(50, gy), (W - 50, gy)], fill=(32, 38, 63, 120), width=1)

    pts = []
    fb = 0.0
    for i in range(n_pts):
        phase = (i / n_pts) * 4.0 * math.pi
        m_phase = phase * 2.0
        m_sig = math.sin(m_phase + fb * 1.5)
        fb = m_sig
        c_sig = math.sin(phase + m_sig * 2.8)
        # Saturated ladder output
        y_val = c_sig / (1.0 + abs(c_sig) * 0.25)
        px = 60 + (i / n_pts) * (W - 120)
        py = cy - y_val * 180.0
        pts.append((px, py))

    # Glow under-trace
    for i in range(len(pts) - 1):
        frac = i / len(pts)
        draw.line([pts[i], pts[i+1]], fill=(167, 177, 255, 90), width=8)

    # Core sharp trace
    for i in range(len(pts) - 1):
        frac = i / len(pts)
        r = int(SIGNAL[0] * (1.0 - frac) + EMBER[0] * frac)
        g = int(SIGNAL[1] * (1.0 - frac) + EMBER[1] * frac)
        b = int(SIGNAL[2] * (1.0 - frac) + EMBER[2] * frac)
        draw.line([pts[i], pts[i+1]], fill=(r, g, b, 240), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "feedback-fm-lead-synth.jpg"), quality=94)
    print("feedback-fm-lead-synth.jpg saved")

def render_spectral_additive_organ():
    """Additive drawbar organ harmonics spectrum and interference waveform."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    # Drawbar harmonic bars (16', 8', 5 1/3', 4', 2 2/3', 2', etc.)
    bar_weights = [0.85, 1.0, 0.65, 0.8, 0.5, 0.4, 0.3, 0.2]
    bar_w = 40
    gap = 24
    start_x = (W - (len(bar_weights) * (bar_w + gap) - gap)) // 2
    base_y = H * 0.78

    for idx, weight in enumerate(bar_weights):
        bx = start_x + idx * (bar_w + gap)
        bh = int(weight * 220)
        by = base_y - bh
        frac = idx / len(bar_weights)
        col = EMBER * (1.0 - frac) + SIGNAL * frac
        r, g, b = [int(v) for v in np.clip(col, 0, 255)]
        draw.rectangle([bx, by, bx + bar_w, base_y], fill=(r, g, b, 220))
        # Drawbar cap highlight
        draw.rectangle([bx, by, bx + bar_w, by + 8], fill=(245, 248, 255, 255))

    # Additive wave summation overlay at top
    wave_pts = []
    n_pts = 600
    cy_wave = H * 0.28
    for i in range(n_pts):
        x_frac = i / n_pts
        phase = x_frac * 6.0 * math.pi
        val = sum(math.sin(phase * (idx + 1) * 0.5) * bar_weights[idx] for idx in range(len(bar_weights)))
        val /= sum(bar_weights)
        px = 60 + x_frac * (W - 120)
        py = cy_wave - val * 75.0
        wave_pts.append((px, py))

    for i in range(len(wave_pts) - 1):
        draw.line([wave_pts[i], wave_pts[i+1]], fill=(78, 242, 184, 230), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "spectral-additive-organ-synth.jpg"), quality=94)
    print("spectral-additive-organ-synth.jpg saved")

def render_stereo_pingpong_delay():
    """Stereo cross-feedback ping-pong delay visualization."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5

    # Cross-feedback ping-pong echo rings
    echos = 9
    for e in range(echos):
        frac = e / echos
        decay = math.exp(-0.4 * e)
        radius = 45 + e * 36
        offset = (e % 2 == 0) and -80 or 80
        ecx = cx + offset * (1.0 - frac * 0.5)
        col = SIGNAL if (e % 2 == 0) else EMERALD
        col = col * decay + GROUND * (1.0 - decay)
        r_c, g_c, b_c = [int(v) for v in np.clip(col, 0, 255)]
        alpha = int(255 * decay * 0.85)

        draw.ellipse([ecx - radius, cy - radius * 0.7, ecx + radius, cy + radius * 0.7],
                     outline=(r_c, g_c, b_c, alpha), width=3)

    # Central ping-pong waveform pulses
    ptsL, ptsR = [], []
    for i in range(300):
        t = i / 300.0
        x = 80 + t * (W - 160)
        yL = cy - 60 - math.sin(t * 36.0) * math.exp(-t * 3.5) * 65.0
        yR = cy + 60 + math.sin(t * 36.0 + 1.57) * math.exp(-t * 3.5) * 65.0
        ptsL.append((x, yL))
        ptsR.append((x, yR))

    for i in range(len(ptsL) - 1):
        draw.line([ptsL[i], ptsL[i+1]], fill=(167, 177, 255, 230), width=2)
        draw.line([ptsR[i], ptsR[i+1]], fill=(78, 242, 184, 230), width=2)

    im.save(os.path.join(PREVIEWS_DIR, "stereo-pingpong-delay-fx.jpg"), quality=94)
    print("stereo-pingpong-delay-fx.jpg saved")

def render_tube_preamp_overdrive():
    """Asymmetric triode tube overdrive transfer curve and saturated waveform."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.35, H * 0.5

    # Transfer curve grid
    for g in range(-150, 160, 50):
        draw.line([(cx + g, cy - 160), (cx + g, cy + 160)], fill=(32, 38, 63, 140), width=1)
        draw.line([(cx - 160, cy + g), (cx + 160, cy + g)], fill=(32, 38, 63, 140), width=1)

    # 12AX7 asymmetric soft-clipping transfer curve
    curve_pts = []
    for i in range(-150, 151):
        x = i / 60.0
        u = x * 2.2 + 0.35
        sat = u / (1.0 + abs(u) + 0.2 * (u * u))
        px = cx + i
        py = cy - sat * 85.0
        curve_pts.append((px, py))

    for i in range(len(curve_pts) - 1):
        draw.line([curve_pts[i], curve_pts[i+1]], fill=(255, 107, 53, 240), width=3)

    # Output clipped waveform on right side
    wave_cx = W * 0.72
    wave_pts = []
    for i in range(260):
        t = (i / 260.0) * 4.0 * math.pi
        raw = math.sin(t) * 2.8 + 0.35
        sat = raw / (1.0 + abs(raw) + 0.2 * (raw * raw))
        px = wave_cx - 130 + i
        py = cy - sat * 95.0
        wave_pts.append((px, py))

    for i in range(len(wave_pts) - 1):
        draw.line([wave_pts[i], wave_pts[i+1]], fill=(167, 177, 255, 230), width=2)

    im.save(os.path.join(PREVIEWS_DIR, "tube-preamp-overdrive-fx.jpg"), quality=94)
    print("tube-preamp-overdrive-fx.jpg saved")

def render_accretion_disk():
    """Relativistic black hole gravitational lensing and Kerr photon ring."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    aspect = W / H
    cx, cy = W * 0.5, H * 0.5
    scale = 2.2

    y_indices, x_indices = np.indices((H, W))
    p_x = ((x_indices / W) - 0.5) * aspect * scale
    p_y = ((y_indices / H) - 0.5) * scale

    r = np.sqrt(p_x**2 + p_y**2)
    angle = np.arctan2(p_y, p_x)

    # Relativistic deflection
    lens = 0.38
    rBent = r - (lens * 0.16) / np.maximum(r, 0.06)

    # 3D inclination
    tiltY = p_y * 2.9 + np.sin(p_x * 2.2) * 0.12
    rDisk = np.sqrt(p_x**2 + tiltY**2)

    isco = 0.38
    iscoEdge = np.clip((rDisk - (isco - 0.04)) / 0.13, 0.0, 1.0)
    diskGlow = np.exp(-np.abs(rDisk - 0.78) * 4.2) * iscoEdge

    doppler = 1.0 + 0.85 * (-p_x / np.maximum(rDisk, 0.1))
    photonRing = np.exp(-np.abs(rBent - isco * 0.88) * 32.0) * 2.5
    shadow = np.clip((rBent - isco * 0.70) / (isco * 0.22), 0.0, 1.0)

    for c in range(3):
        out[:, :, c] = GROUND[c]

    for y in range(H):
        for x in range(W):
            dg = diskGlow[y, x]
            dp = doppler[y, x]
            pr = photonRing[y, x]
            sh = shadow[y, x]
            r_val = r[y, x]

            frac = np.clip(dp * 0.5, 0.0, 1.0)
            colDisk = EMBER * (1.0 - frac) + SIGNAL * frac
            glow = dg * dp * 1.9

            c_vec = GROUND + colDisk * glow + (SIGNAL * 0.3 + WHITE * 0.7) * pr
            c_vec *= sh
            c_vec *= np.clip((2.2 - r_val) / 1.3, 0.0, 1.0)
            out[y, x] = np.clip(c_vec, 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "accretion-disk.jpg"), quality=94)
    print("accretion-disk.jpg saved")

def render_chromatic_quasicrystal():
    """Aperiodic 8-fold quasicrystalline wave interference."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    aspect = W / H
    scale = 2.2

    y_indices, x_indices = np.indices((H, W))
    p_x = ((x_indices / W) - 0.5) * aspect * scale
    p_y = ((y_indices / H) - 0.5) * scale

    r = np.sqrt(p_x**2 + p_y**2)
    freq = 15.0

    w0 = np.cos(p_x * freq)
    w1 = np.cos((p_x * 0.92388 + p_y * 0.38268) * freq)
    w2 = np.cos((p_x * 0.70711 + p_y * 0.70711) * freq)
    w3 = np.cos((p_x * 0.38268 + p_y * 0.92388) * freq)
    w4 = np.cos(p_y * freq)
    w5 = np.cos((-p_x * 0.38268 + p_y * 0.92388) * freq)
    w6 = np.cos((-p_x * 0.70711 + p_y * 0.70711) * freq)
    w7 = np.cos((-p_x * 0.92388 + p_y * 0.38268) * freq)

    psi = (w0 + w1 + w2 + w3 + w4 + w5 + w6 + w7) * 0.125
    psiNorm = np.clip(psi * 0.5 + 0.5, 0.0, 1.0)
    peaks = psiNorm ** 4.5
    rings = np.sin(r * 20.0) * 0.5 + 0.5

    for y in range(H):
        for x in range(W):
            pk = peaks[y, x]
            rg = rings[y, x]
            r_val = r[y, x]
            ps = psi[y, x]

            colorGrad = SIGNAL * (0.5 + 0.5 * np.sin(ps * 4.5)) + EMERALD * (0.5 - 0.5 * np.sin(ps * 4.5))
            colorGrad = colorGrad * (1.0 - rg * 0.35) + EMBER * (rg * 0.35)

            c_vec = GROUND + colorGrad * pk * 2.5 + SIGNAL * rg * 0.22 * np.exp(-r_val * 1.4)
            c_vec *= np.clip((2.2 - r_val) / 1.35, 0.0, 1.0)
            out[y, x] = np.clip(c_vec, 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "chromatic-quasicrystal.jpg"), quality=94)
    print("chromatic-quasicrystal.jpg saved")

def main():
    render_sprott_attractor()
    render_apollonian_gasket()
    render_plucker_conoid()
    render_seifert_surface()
    render_feedback_fm_lead()
    render_spectral_additive_organ()
    render_stereo_pingpong_delay()
    render_tube_preamp_overdrive()
    render_accretion_disk()
    render_chromatic_quasicrystal()
    print("All Pack 7 previews generated successfully.")

if __name__ == "__main__":
    main()
