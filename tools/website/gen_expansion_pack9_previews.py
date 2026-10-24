#!/usr/bin/env python3
"""Generate high-fidelity preview images for the 10 new creative library items (Pack 9).
Matches brand design: midnight ground #151930, signal #A7B1FF, ember #FF6B35, emerald #4EF2B8.
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

W, H = 900, 600

def render_riemann_zeta_spiral():
    """Polar Dirichlet and Riemann zeta spiral harmonics."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5
    baseScale = min(W, H) * 0.22

    for r in range(1, 6):
        draw.ellipse([cx - baseScale * r * 0.7, cy - baseScale * r * 0.7,
                      cx + baseScale * r * 0.7, cy + baseScale * r * 0.7],
                     outline=(32, 38, 70, 120), width=1)

    steps = 480
    pts = []
    numN = 20
    s_val = 0.5
    for step in range(steps + 1):
        frac = step / steps
        currentT = 1.8 + frac * 5.0
        sumX, sumY = 0.0, 0.0
        for n in range(1, numN + 1):
            mag = math.pow(n, -s_val)
            phase = -currentT * math.log(n)
            sumX += mag * math.cos(phase)
            sumY += mag * math.sin(phase)
        px = cx + sumX * baseScale
        py = cy + sumY * baseScale
        pts.append((px, py, frac))

    for i in range(len(pts) - 1):
        p1, p2 = pts[i], pts[i + 1]
        frac = p1[2]
        col_vec = SIGNAL * (1.0 - frac) + EMBER * frac
        col = (int(col_vec[0]), int(col_vec[1]), int(col_vec[2]), int(140 + 115 * frac))
        draw.line([(p1[0], p1[1]), (p2[0], p2[1])], fill=col, width=int(2 + 2 * frac))

    # Satellites
    leadT = 1.8 + 5.0
    accumX, accumY = 0.0, 0.0
    for n in range(1, numN + 1):
        mag = math.pow(n, -s_val)
        phase = -leadT * math.log(n)
        nx = accumX + mag * math.cos(phase)
        ny = accumY + math.sin(phase)
        if n % 2 == 0:
            px = cx + nx * baseScale
            py = cy + ny * baseScale
            draw.ellipse([px - 4, py - 4, px + 4, py + 4], fill=(255, 107, 53, 230))
        accumX = nx
        accumY = ny

    # Glowing tip
    tip = pts[-1]
    draw.ellipse([tip[0] - 8, tip[1] - 8, tip[0] + 8, tip[1] + 8], fill=(255, 107, 53, 200))
    draw.ellipse([tip[0] - 4, tip[1] - 4, tip[0] + 4, tip[1] + 4], fill=(255, 245, 230, 255))

    im.save(os.path.join(PREVIEWS_DIR, "riemann-zeta-spiral.jpg"), quality=94)
    print("riemann-zeta-spiral.jpg saved")

def render_cayley_hyperbolic_tree():
    """Recursive Bethe lattice and Cayley tree on Poincaré disk."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5
    diskR = min(W, H) * 0.44

    draw.ellipse([cx - diskR, cy - diskR, cx + diskR, cy + diskR],
                 outline=(167, 177, 255, 230), width=3)

    for r in range(1, 4):
        draw.ellipse([cx - diskR * (r / 4), cy - diskR * (r / 4),
                      cx + diskR * (r / 4), cy + diskR * (r / 4)],
                     outline=(32, 38, 70, 100), width=1)

    def draw_geodesic(u1, v1, u2, v2, col, width):
        cross = u1 * v2 - v1 * u2
        if abs(cross) < 1e-4:
            draw.line([(cx + u1 * diskR, cy + v1 * diskR), (cx + u2 * diskR, cy + v2 * diskR)], fill=col, width=width)
            return
        d1 = 1.0 + u1 * u1 + v1 * v1
        d2 = 1.0 + u2 * u2 + v2 * v2
        cX = (v2 * d1 - v1 * d2) / (2.0 * cross)
        cY = (u1 * d2 - u2 * d1) / (2.0 * cross)
        rC2 = cX * cX + cY * cY - 1.0
        if rC2 <= 0:
            draw.line([(cx + u1 * diskR, cy + v1 * diskR), (cx + u2 * diskR, cy + v2 * diskR)], fill=col, width=width)
            return
        rC = math.sqrt(rC2)
        a1 = math.atan2(v1 - cY, u1 - cX)
        a2 = math.atan2(v2 - cY, u2 - cX)
        diff = a2 - a1
        while diff > math.pi: diff -= 2.0 * math.pi
        while diff < -math.pi: diff += 2.0 * math.pi
        segs = 16
        prev = (cx + u1 * diskR, cy + v1 * diskR)
        for k in range(1, segs + 1):
            a = a1 + (diff * k) / segs
            nx = cx + (cX + rC * math.cos(a)) * diskR
            ny = cy + (cY + rC * math.sin(a)) * diskR
            draw.line([prev, (nx, ny)], fill=col, width=width)
            prev = (nx, ny)

    def branch(d, maxD, u0, v0, ang, span):
        if d > maxD: return
        stepDist = 0.28 * math.pow(0.72, d)
        rCur = math.sqrt(u0 * u0 + v0 * v0)
        targetR = min(0.92, rCur + stepDist)
        branches = 3
        childCount = branches if d == 0 else branches - 1
        for b in range(childCount):
            offset = 0 if childCount == 1 else ((b / (childCount - 1)) - 0.5) * span
            childAng = ang + offset
            u1 = targetR * math.cos(childAng)
            v1 = targetR * math.sin(childAng)

            frac = d / maxD
            col_v = SIGNAL * (1.0 - frac) + EMERALD * frac
            draw_geodesic(u0, v0, u1, v1, (int(col_v[0]), int(col_v[1]), int(col_v[2]), 200), max(1, 3 - d // 2))

            if d == maxD:
                draw.ellipse([cx + u1 * diskR - 4, cy + v1 * diskR - 4,
                              cx + u1 * diskR + 4, cy + v1 * diskR + 4], fill=(255, 107, 53, 230))
            else:
                draw.ellipse([cx + u1 * diskR - 2, cy + v1 * diskR - 2,
                              cx + u1 * diskR + 2, cy + v1 * diskR + 2], fill=(245, 248, 255, 200))

            branch(d + 1, maxD, u1, v1, childAng, span * 0.75)

    branches = 3
    for r in range(branches):
        ang = (r * 2.0 * math.pi) / branches
        u1 = 0.28 * math.cos(ang)
        v1 = 0.28 * math.sin(ang)
        draw_geodesic(0, 0, u1, v1, (167, 177, 255, 240), 3)
        branch(1, 5, u1, v1, ang, 1.4)

    draw.ellipse([cx - 6, cy - 6, cx + 6, cy + 6], fill=(167, 177, 255, 255))
    im.save(os.path.join(PREVIEWS_DIR, "cayley-hyperbolic-tree.jpg"), quality=94)
    print("cayley-hyperbolic-tree.jpg saved")

def render_pseudosphere_tractroid():
    """Tractroid constant negative Gaussian curvature surface."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5

    rotY, rotX = 0.65, 0.42
    scale = 135.0

    def project(x, y, z):
        x1 = x * math.cos(rotY) + z * math.sin(rotY)
        z1 = -x * math.sin(rotY) + z * math.cos(rotY)
        y2 = y * math.cos(rotX) - z1 * math.sin(rotX)
        z2 = y * math.sin(rotX) + z1 * math.cos(rotX)
        dist = 4.2 - z2
        px = cx + (x1 / dist) * 3.4 * scale
        py = cy - (y2 / dist) * 3.4 * scale
        return px, py, z2

    numRibs = 32
    stepsU = 30
    uMax = 2.4
    lines = []

    for sign in (-1, 1):
        for r in range(numRibs):
            v = (r / numRibs) * 2.0 * math.pi
            pts = []
            for s in range(stepsU + 1):
                u = 0.05 + (s / stepsU) * (uMax - 0.05)
                coshU = (math.exp(u) + math.exp(-u)) * 0.5
                sechU = 1.0 / coshU
                tanhU = (math.exp(u) - math.exp(-u)) / (math.exp(u) + math.exp(-u))
                x = 1.0 * sechU * math.cos(v)
                y = sign * 1.8 * (u - tanhU)
                z = 1.0 * sechU * math.sin(v)
                pts.append(project(x, y, z))
            avgZ = sum(p[2] for p in pts) / len(pts)
            lines.append((avgZ, r / numRibs, pts))

    lines.sort(key=lambda item: item[0])

    for avgZ, frac, pts in lines:
        alpha = int(np.clip(120 + avgZ * 80, 40, 240))
        col_v = SIGNAL * (1.0 - frac) + EMBER * frac
        col = (int(col_v[0]), int(col_v[1]), int(col_v[2]), alpha)
        for i in range(len(pts) - 1):
            draw.line([(pts[i][0], pts[i][1]), (pts[i+1][0], pts[i+1][1])], fill=col, width=2)

    # Equatorial ring
    ringPts = [project(1.0 * math.cos(i / 40.0 * 2 * math.pi), 0, 1.0 * math.sin(i / 40.0 * 2 * math.pi)) for i in range(41)]
    for i in range(len(ringPts) - 1):
        draw.line([(ringPts[i][0], ringPts[i][1]), (ringPts[i+1][0], ringPts[i+1][1])], fill=(255, 107, 53, 240), width=3)

    # Satellite beads
    for s in range(8):
        uOrb = (s / 8.0 * 2.0 - 1.0) * 1.6
        signOrb = 1 if uOrb >= 0 else -1
        absU = max(0.05, abs(uOrb))
        coshU = (math.exp(absU) + math.exp(-absU)) * 0.5
        sechU = 1.0 / coshU
        tanhU = (math.exp(absU) - math.exp(-absU)) / (math.exp(absU) + math.exp(-absU))
        vOrb = (s / 8.0) * math.pi * 4.0
        x = 1.0 * sechU * math.cos(vOrb)
        y = signOrb * 1.8 * (absU - tanhU)
        z = 1.0 * sechU * math.sin(vOrb)
        p = project(x, y, z)
        draw.ellipse([p[0] - 5, p[1] - 5, p[0] + 5, p[1] + 5], fill=(255, 107, 53, 255), outline=(255, 235, 220, 255))

    im.save(os.path.join(PREVIEWS_DIR, "pseudosphere-tractroid-3d.jpg"), quality=94)
    print("pseudosphere-tractroid-3d.jpg saved")

def render_kummer_surface():
    """Kummer quartic nodal surface with 16 double nodes."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5

    rotY, rotX = 0.55, 0.48
    scale = 140.0

    def project(x, y, z):
        x1 = x * math.cos(rotY) + z * math.sin(rotY)
        z1 = -x * math.sin(rotY) + z * math.cos(rotY)
        y2 = y * math.cos(rotX) - z1 * math.sin(rotX)
        z2 = y * math.sin(rotX) + z1 * math.cos(rotX)
        dist = 4.0 - z2
        px = cx + (x1 / dist) * 3.4 * scale
        py = cy - (y2 / dist) * 3.4 * scale
        return px, py, z2

    uSteps = 30
    vSteps = 48
    lines = []

    for i in range(uSteps):
        theta = (i / uSteps) * math.pi
        sinT = math.sin(theta)
        cosT = math.cos(theta)
        pts = []
        for j in range(vSteps + 1):
            phi = (j / vSteps) * 2.0 * math.pi
            mod = 1.0 + 0.35 * (math.sin(2.0 * theta) * math.sin(2.0 * phi) + math.cos(3.0 * theta))
            r = 1.1 * (0.8 + 0.3 * mod)
            x = r * sinT * math.cos(phi)
            y = r * cosT
            z = r * sinT * math.sin(phi)
            pts.append(project(x, y, z))
        avgZ = sum(p[2] for p in pts) / len(pts)
        lines.append((avgZ, i / uSteps, pts))

    lines.sort(key=lambda item: item[0])

    for avgZ, frac, pts in lines:
        alpha = int(np.clip(130 + avgZ * 80, 40, 240))
        col_v = EMERALD * (1.0 - frac) + SIGNAL * frac
        col = (int(col_v[0]), int(col_v[1]), int(col_v[2]), alpha)
        for i in range(len(pts) - 1):
            draw.line([(pts[i][0], pts[i][1]), (pts[i+1][0], pts[i+1][1])], fill=col, width=2)

    # 16 Double-point singularity nodes
    nodes = [
        [1, 1, 1], [1, 1, -1], [1, -1, 1], [1, -1, -1],
        [-1, 1, 1], [-1, 1, -1], [-1, -1, 1], [-1, -1, -1],
        [1.4, 0, 0], [-1.4, 0, 0], [0, 1.4, 0], [0, -1.4, 0],
        [0, 0, 1.4], [0, 0, -1.4], [0.8, 0.8, 0], [-0.8, -0.8, 0]
    ]
    for pt in nodes:
        length = math.sqrt(sum(c*c for c in pt))
        normSc = 1.25 / (length or 1)
        p = project(pt[0] * normSc, pt[1] * normSc, pt[2] * normSc)
        draw.ellipse([p[0] - 6, p[1] - 6, p[0] + 6, p[1] + 6], fill=(255, 107, 53, 240), outline=(255, 235, 220, 255))

    im.save(os.path.join(PREVIEWS_DIR, "kummer-surface-3d.jpg"), quality=94)
    print("kummer-surface-3d.jpg saved")

def render_spectral_drone():
    """Spectral drone synth Chebyshev partials and harmonic spectrum."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    for gy in range(80, H, 80):
        draw.line([(0, gy), (W, gy)], fill=(32, 38, 70, 100), width=1)
    for gx in range(90, W, 90):
        draw.line([(gx, 0), (gx, H)], fill=(32, 38, 70, 100), width=1)

    cy = H * 0.45

    # Multi-partial waveform
    pts = []
    for i in range(W):
        frac = i / W
        phase1 = frac * 6.0 * math.pi
        phase2 = phase1 * 1.035
        phase3 = phase1 * 0.965
        s1 = math.sin(phase1)
        s2 = math.sin(phase2)
        s3 = math.sin(phase3)
        cheb = 2.0 * s1 * s1 - 1.0
        env = math.exp(-frac * 1.4)
        sig = (s1 * 0.5 + s2 * 0.3 + s3 * 0.2 + cheb * 0.45) * env * 155.0
        pts.append((i, cy + sig))

    for i in range(len(pts) - 1):
        draw.line([pts[i], pts[i+1]], fill=(167, 177, 255, 120), width=6)
        draw.line([pts[i], pts[i+1]], fill=(78, 242, 184, 180), width=3)
        draw.line([pts[i], pts[i+1]], fill=(255, 255, 255, 240), width=1)

    # Chebyshev partial bars
    for idx in range(8):
        fx = int(100 + idx * 95)
        amp = (1.0 / (idx * 0.4 + 1.0)) * 140
        draw.line([(fx, cy + 200), (fx, cy + 200 - amp)], fill=(255, 107, 53, 210), width=6)
        draw.ellipse([fx - 5, cy + 200 - amp - 5, fx + 5, cy + 200 - amp + 5], fill=(255, 235, 215, 255))

    im.save(os.path.join(PREVIEWS_DIR, "spectral-drone-synth.jpg"), quality=94)
    print("spectral-drone-synth.jpg saved")

    im_webp = im.resize((960, 600), Image.Resampling.LANCZOS)
    im_webp.save(os.path.join(TEMPLATES_DIR, "spectral-drone-synth.webp"), "WEBP", quality=90)
    print("spectral-drone-synth.webp saved")

def render_resonant_brass():
    """Resonant brass synth lip-reed pressure differential waveform."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    for gy in range(80, H, 80):
        draw.line([(0, gy), (W, gy)], fill=(32, 38, 70, 100), width=1)
    for gx in range(90, W, 90):
        draw.line([(gx, 0), (gx, H)], fill=(32, 38, 70, 100), width=1)

    cy = H * 0.45

    pts = []
    for i in range(W):
        frac = i / W
        phase = (frac * 8.0) % 1.0
        saw = 2.0 * phase - 1.0
        reed = saw - saw * saw * saw * 0.33 * 0.85
        growl = math.sin(frac * 4.0 * math.pi) * 0.25
        u = (reed + growl) * 1.6
        sat = u / (1.0 + abs(u) * 0.35)
        env = math.exp(-frac * 1.8)
        sig = sat * env * 165.0
        pts.append((i, cy + sig))

    for i in range(len(pts) - 1):
        draw.line([pts[i], pts[i+1]], fill=(167, 177, 255, 120), width=6)
        draw.line([pts[i], pts[i+1]], fill=(255, 107, 53, 190), width=3)
        draw.line([pts[i], pts[i+1]], fill=(255, 255, 255, 240), width=1)

    # Horn bell acoustic flare
    for idx in range(7):
        fx = int(120 + idx * 105)
        amp = math.exp(-((idx - 3.5) / 2.0)**2) * 150
        draw.line([(fx, cy + 190), (fx, cy + 190 - amp)], fill=(78, 242, 184, 210), width=6)
        draw.ellipse([fx - 5, cy + 190 - amp - 5, fx + 5, cy + 190 - amp + 5], fill=(255, 245, 230, 255))

    im.save(os.path.join(PREVIEWS_DIR, "resonant-brass-synth.jpg"), quality=94)
    print("resonant-brass-synth.jpg saved")

    im_webp = im.resize((960, 600), Image.Resampling.LANCZOS)
    im_webp.save(os.path.join(TEMPLATES_DIR, "resonant-brass-synth.webp"), "WEBP", quality=90)
    print("resonant-brass-synth.webp saved")

def render_biquad_morph():
    """Biquad morph filter continuous state-variable resonance curve."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    for gy in range(80, H, 80):
        draw.line([(0, gy), (W, gy)], fill=(32, 38, 70, 100), width=1)
    for gx in range(90, W, 90):
        draw.line([(gx, 0), (gx, H)], fill=(32, 38, 70, 100), width=1)

    cy = H * 0.55

    # Continuous morph curves: LP -> Notch -> BP -> HP
    peak_x = 450
    curves = [
        # LP
        ([(x, cy - (150.0 / (1.0 + math.exp((x - peak_x) / 35.0)))) for x in range(W)], (167, 177, 255, 120), 2),
        # BP
        ([(x, cy - 180.0 * math.exp(-((x - peak_x) / 45.0)**2)) for x in range(W)], (255, 107, 53, 220), 3),
        # HP
        ([(x, cy - (150.0 / (1.0 + math.exp(-(x - peak_x) / 35.0)))) for x in range(W)], (78, 242, 184, 150), 2),
    ]

    for pts, col, width in curves:
        for i in range(len(pts) - 1):
            draw.line([pts[i], pts[i+1]], fill=col, width=width)

    # Resonance peak marker
    draw.ellipse([peak_x - 7, cy - 180 - 7, peak_x + 7, cy - 180 + 7], fill=(255, 107, 53, 255), outline=(255, 235, 220, 255))

    im.save(os.path.join(PREVIEWS_DIR, "biquad-morph-filter-fx.jpg"), quality=94)
    print("biquad-morph-filter-fx.jpg saved")

    im_webp = im.resize((960, 600), Image.Resampling.LANCZOS)
    im_webp.save(os.path.join(TEMPLATES_DIR, "biquad-morph-filter-fx.webp"), "WEBP", quality=90)
    print("biquad-morph-filter-fx.webp saved")

def render_harmonic_tremolo():
    """Harmonic tremolo panner dual-band crossover anti-phase envelope."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    for gy in range(80, H, 80):
        draw.line([(0, gy), (W, gy)], fill=(32, 38, 70, 100), width=1)
    for gx in range(90, W, 90):
        draw.line([(gx, 0), (gx, H)], fill=(32, 38, 70, 100), width=1)

    cy = H * 0.48

    # Low-band tremolo
    pts_low = []
    # High-band tremolo (anti-phase)
    pts_high = []

    for i in range(W):
        frac = i / W
        lfo_low = 0.5 + 0.45 * math.sin(frac * 8.0 * math.pi)
        lfo_high = 0.5 - 0.45 * math.sin(frac * 8.0 * math.pi)

        carrier_low = math.sin(frac * 32.0 * math.pi) * lfo_low * 130.0
        carrier_high = math.sin(frac * 80.0 * math.pi) * lfo_high * 110.0

        pts_low.append((i, cy - 60 + carrier_low))
        pts_high.append((i, cy + 60 + carrier_high))

    for i in range(len(pts_low) - 1):
        draw.line([pts_low[i], pts_low[i+1]], fill=(255, 107, 53, 190), width=2)
    for i in range(len(pts_high) - 1):
        draw.line([pts_high[i], pts_high[i+1]], fill=(78, 242, 184, 190), width=2)

    im.save(os.path.join(PREVIEWS_DIR, "harmonic-tremolo-panner-fx.jpg"), quality=94)
    print("harmonic-tremolo-panner-fx.jpg saved")

    im_webp = im.resize((960, 600), Image.Resampling.LANCZOS)
    im_webp.save(os.path.join(TEMPLATES_DIR, "harmonic-tremolo-panner-fx.webp"), "WEBP", quality=90)
    print("harmonic-tremolo-panner-fx.webp saved")

def render_stellar_nursery_nebula():
    """Volumetric stellar nursery nebula with protostellar core."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    aspect = W / H
    scale = 2.2

    y_indices, x_indices = np.indices((H, W))
    p_x = ((x_indices / W) - 0.5) * aspect * scale
    p_y = ((y_indices / H) - 0.5) * scale

    r = np.sqrt(p_x**2 + p_y**2)
    ang = np.arctan2(p_y, p_x)

    w1 = np.sin(p_x * 4.0 + 1.2 + np.sin(p_y * 3.0)) * 0.35
    w2 = np.cos(p_y * 5.0 - 0.8 + np.cos(p_x * 4.0)) * 0.25
    turb_x = p_x + w1 * 0.6
    turb_y = p_y + w2 * 0.6

    dist = np.sqrt(turb_x**2 + turb_y**2)
    gas1 = np.exp(-dist * 3.2) * (0.8 + 0.4 * np.sin(ang * 3.0 + 1.4))
    gas2 = np.exp(-np.abs(turb_y + np.sin(turb_x * 6.0) * 0.18) * 8.0) * 0.7
    cloud = gas1 + gas2

    core = np.exp(-r * 7.5) * 2.2
    star_sparks = np.maximum(0.0, np.sin(p_x * 24.0 + np.sin(p_y * 18.0) * 4.0))**8.0 * np.exp(-r * 2.2) * 2.2

    fade = np.clip((2.4 - r) / 1.4, 0.0, 1.0)

    for y in range(H):
        for x in range(W):
            c = cloud[y, x]
            g2 = gas2[y, x]
            cr = core[y, x]
            spk = star_sparks[y, x]

            cGas = SIGNAL * (1.0 - min(1.0, c * 1.2)) + EMBER * min(1.0, c * 1.2)
            cGas = cGas * (1.0 - min(1.0, g2 * 1.4)) + EMERALD * min(1.0, g2 * 1.4)

            col = GROUND + cGas * c * 1.5 + EMBER * cr + WHITE * (cr * 0.6 + spk)
            col = GROUND * (1.0 - fade[y, x]) + col * fade[y, x]
            out[y, x] = np.clip(col, 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "stellar-nursery-nebula.jpg"), quality=94)
    print("stellar-nursery-nebula.jpg saved")

def render_quantum_chromodynamic_lattice():
    """SU(3) gluon flux tubes and instanton vortex lattice."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    aspect = W / H
    scale = 2.2

    y_indices, x_indices = np.indices((H, W))
    p_x = ((x_indices / W) - 0.5) * aspect * scale
    p_y = ((y_indices / H) - 0.5) * scale

    r = np.sqrt(p_x**2 + p_y**2)
    ang = np.arctan2(p_y, p_x)

    latticeScale = 12.0
    grid_x = (p_x * latticeScale + 0.3) % 1.0 - 0.5
    grid_y = (p_y * latticeScale + 0.3) % 1.0 - 0.5
    gridDist = np.minimum(np.abs(grid_x), np.abs(grid_y))

    vortex = np.sin(4.0 * ang + r * 10.0)
    flux = np.exp(-gridDist * 16.0 * 0.85) * 1.6
    nodeGlow = np.exp(-np.sqrt(grid_x**2 + grid_y**2) * 12.0) * (1.2 + 0.5 * vortex)

    fade = np.clip((2.3 - r) / 1.3, 0.0, 1.0)

    for y in range(H):
        for x in range(W):
            fl = flux[y, x]
            ng = nodeGlow[y, x]
            vx = vortex[y, x]
            rad = r[y, x]

            colorField = SIGNAL * (0.5 + 0.5 * math.sin(rad * 6.0)) + EMERALD * (0.5 - 0.5 * math.sin(rad * 6.0))
            colorField = colorField * 0.5 + EMBER * (0.5 + 0.5 * vx)

            col = GROUND + colorField * fl + (EMBER * 0.7 + WHITE * 0.3) * ng
            col = GROUND * (1.0 - fade[y, x]) + col * fade[y, x]
            out[y, x] = np.clip(col, 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "quantum-chromodynamic-lattice.jpg"), quality=94)
    print("quantum-chromodynamic-lattice.jpg saved")

def main():
    render_riemann_zeta_spiral()
    render_cayley_hyperbolic_tree()
    render_pseudosphere_tractroid()
    render_kummer_surface()
    render_spectral_drone()
    render_resonant_brass()
    render_biquad_morph()
    render_harmonic_tremolo()
    render_stellar_nursery_nebula()
    render_quantum_chromodynamic_lattice()
    print("All Pack 9 preview images rendered successfully!")

if __name__ == "__main__":
    main()
