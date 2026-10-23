#!/usr/bin/env python3
"""Generate high-fidelity preview images for the 10 new creative library items (Pack 5).
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

def render_rabinovich_fabrikant():
    """Rabinovich-Fabrikant chaotic attractor dual spiraling vortices."""
    acc = np.zeros((H, W), dtype=np.float32)
    cx, cy = W / 2, H / 2
    k = min(W, H) * 0.16
    alpha, gamma = 0.14, 0.10
    x, y, z = -0.1, 0.5, 0.5
    dt = 0.0032

    def derivs(px, py, pz):
        dx = py * (pz - 1.0 + px * px) + gamma * px
        dy = px * (3.0 * pz + 1.0 - px * px) + gamma * py
        dz = -2.0 * pz * (alpha + px * py)
        return dx, dy, dz

    for i in range(1200000):
        k1x, k1y, k1z = derivs(x, y, z)
        k2x, k2y, k2z = derivs(x + 0.5*dt*k1x, y + 0.5*dt*k1y, z + 0.5*dt*k1z)
        k3x, k3y, k3z = derivs(x + 0.5*dt*k2x, y + 0.5*dt*k2y, z + 0.5*dt*k2z)
        k4x, k4y, k4z = derivs(x + dt*k3x, y + dt*k3y, z + dt*k3z)

        x += (dt/6.0) * (k1x + 2*k2x + 2*k3x + k4x)
        y += (dt/6.0) * (k1y + 2*k2y + 2*k3y + k4y)
        z += (dt/6.0) * (k1z + 2*k2z + 2*k3z + k4z)

        if abs(x) > 4.0 or abs(y) > 4.0 or z > 8.0:
            x, y, z = -0.1, 0.5, 0.5

        if i > 2000:
            rot = 0.75
            tilt = 0.70
            rx = x * math.cos(rot) - y * math.sin(rot)
            ry = (x * math.sin(rot) + y * math.cos(rot)) * math.cos(tilt) - z * math.sin(tilt)
            px = int(cx + rx * k)
            py = int(cy - ry * k + 20)
            if 0 <= px < W and 0 <= py < H:
                acc[py, px] += 1.0

    acc = np.log1p(acc * 1.2)
    norm = np.clip(acc / (np.percentile(acc[acc > 0], 99.5) if np.any(acc > 0) else 1.0), 0.0, 1.0)
    out = np.zeros((H, W, 3), dtype=np.float32)
    for c_idx in range(3):
        out[:, :, c_idx] = GROUND[c_idx]

    for y_idx in range(H):
        for x_idx in range(W):
            v = norm[y_idx, x_idx]
            if v > 0:
                frac = v**0.75
                mix_col = EMERALD * (1.0 - frac) + SIGNAL * (frac * 0.7) + EMBER * (frac * 0.3)
                out[y_idx, x_idx] = np.clip(GROUND * (1.0 - v) + mix_col * v * 1.9 + WHITE * (v**3 * 1.3), 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "rabinovich-fabrikant.jpg"), quality=94)
    print("rabinovich-fabrikant.jpg saved")

def render_penrose_tiling():
    """Aperiodic Penrose P3 tiling with 5-fold decagonal symmetry."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2
    radius = min(W, H) * 0.56
    phi = (1.0 + math.sqrt(5.0)) * 0.5

    triangles = []
    for i in range(10):
        a1 = (2 * i - 1) * math.pi / 10.0
        a2 = (2 * i + 1) * math.pi / 10.0
        bx = cx + radius * math.cos(a1)
        by = cy + radius * math.sin(a1)
        ccx = cx + radius * math.cos(a2)
        ccy = cy + radius * math.sin(a2)
        if i % 2 == 0:
            triangles.append((0, (cx, cy), (bx, by), (ccx, ccy)))
        else:
            triangles.append((0, (cx, cy), (ccx, ccy), (bx, by)))

    for _ in range(5):
        nextTris = []
        for ttype, a, b, c in triangles:
            if ttype == 0:
                px = a[0] + (b[0] - a[0]) / phi
                py = a[1] + (b[1] - a[1]) / phi
                nextTris.append((0, c, (px, py), b))
                nextTris.append((1, (px, py), c, a))
            else:
                qx = b[0] + (a[0] - b[0]) / phi
                qy = b[1] + (a[1] - b[1]) / phi
                rx = b[0] + (c[0] - b[0]) / phi
                ry = b[1] + (c[1] - b[1]) / phi
                nextTris.append((1, (rx, ry), (qx, qy), b))
                nextTris.append((0, (qx, qy), a, c))
                nextTris.append((1, (qx, qy), c, (rx, ry)))
        triangles = nextTris

    for ttype, a, b, c in triangles:
        midX = (a[0] + b[0] + c[0]) / 3.0
        midY = (a[1] + b[1] + c[1]) / 3.0
        d = math.sqrt((midX - cx)**2 + (midY - cy)**2) / radius
        if d > 1.15:
            continue

        g = 0.05
        ax = a[0] * (1.0 - g) + midX * g
        ay = a[1] * (1.0 - g) + midY * g
        bx = b[0] * (1.0 - g) + midX * g
        by = b[1] * (1.0 - g) + midY * g
        cx_ = c[0] * (1.0 - g) + midX * g
        cy_ = c[1] * (1.0 - g) + midY * g

        alpha = int(max(0.1, 1.0 - d * 0.45) * 230)
        if ttype == 0:
            fill_col = (167, 177, 255, alpha)
            outline_col = (245, 248, 255, 180)
        else:
            fill_col = (78, 242, 184, alpha)
            outline_col = (255, 107, 53, 180)

        draw.polygon([(ax, ay), (bx, by), (cx_, cy_)], fill=fill_col, outline=outline_col)

    im.save(os.path.join(PREVIEWS_DIR, "penrose-tiling.jpg"), quality=94)
    print("penrose-tiling.jpg saved")

def render_scherk_surface():
    """Scherk minimal surface 3D perspective projection."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2
    scale = min(W, H) * 0.35

    layers = 44
    steps = 50
    uLimit = 1.36
    vLimit = 1.36
    rotY = 0.45
    rotX = 0.72

    cosY, sinY = math.cos(rotY), math.sin(rotY)
    cosX, sinX = math.cos(rotX), math.sin(rotX)

    ribs = []
    for i in range(layers):
        uFrac = i / (layers - 1)
        u = -uLimit + uFrac * 2.0 * uLimit
        cosU = math.cos(u)
        pts = []
        for j in range(steps + 1):
            vFrac = j / steps
            v = -vLimit + vFrac * 2.0 * vLimit
            cosV = math.cos(v)
            ratio = max(0.001, min(1000.0, cosU / cosV))
            z = math.log(ratio) * 0.65

            x3d = u
            y3d = z
            z3d = v

            # 3D rotate
            x1 = x3d * cosY + z3d * sinY
            z1 = -x3d * sinY + z3d * cosY
            y1 = y3d * cosX - z1 * sinX
            z2 = y3d * sinX + z1 * cosX

            # Project
            dist = 3.5
            focal = 1.0 / (dist + z2 * 0.4)
            px = cx + x1 * scale * focal * 3.2
            py = cy - y1 * scale * focal * 3.2
            pts.append((px, py, z2))

        ribs.append((uFrac, pts))

    # Sort and draw
    for uFrac, pts in ribs:
        for j in range(len(pts) - 1):
            p1, p2 = pts[j], pts[j + 1]
            depth = (p1[2] + p2[2]) * 0.5
            frac = (uFrac + (j / steps) * 0.3) % 1.0
            r = int(SIGNAL[0] * (1.0 - frac) + EMERALD[0] * frac)
            g = int(SIGNAL[1] * (1.0 - frac) + EMERALD[1] * frac)
            b = int(SIGNAL[2] * (1.0 - frac) + EMERALD[2] * frac)
            alpha = int(np.clip(180 - depth * 40, 60, 250))
            draw.line([(p1[0], p1[1]), (p2[0], p2[1])], fill=(r, g, b, alpha), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "scherk-surface-3d.jpg"), quality=94)
    print("scherk-surface-3d.jpg saved")

def render_catalan_surface():
    """Catalan cycloidal ruled minimal surface 3D."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2
    scale = min(W, H) * 0.18

    layers = 44
    steps = 52
    u_range = 6.8
    v_range = 1.35
    rotY = 0.55
    rotX = 0.62

    cosY, sinY = math.cos(rotY), math.sin(rotY)
    cosX, sinX = math.cos(rotX), math.sin(rotX)

    ribs = []
    for i in range(layers):
        uFrac = i / (layers - 1)
        u = -u_range * 0.5 + uFrac * u_range
        sinU = math.sin(u)
        cosU = math.cos(u)
        sinHalfU = math.sin(u * 0.5)

        pts = []
        for j in range(steps + 1):
            vFrac = j / steps
            v = -v_range + vFrac * 2.0 * v_range
            coshV = math.cosh(v)
            sinhHalfV = math.sinh(v * 0.5)

            x3d = u - sinU * coshV
            y3d = 4.0 * sinHalfU * sinhHalfV
            z3d = 1.0 - cosU * coshV

            x1 = x3d * cosY + z3d * sinY
            z1 = -x3d * sinY + z3d * cosY
            y1 = y3d * cosX - z1 * sinX
            z2 = y3d * sinX + z1 * cosX

            dist = 4.2
            focal = 1.0 / (dist + z2 * 0.25)
            px = cx + x1 * scale * focal * 3.8
            py = cy - y1 * scale * focal * 3.8
            pts.append((px, py, z2))

        ribs.append((uFrac, pts))

    for uFrac, pts in ribs:
        for j in range(len(pts) - 1):
            p1, p2 = pts[j], pts[j + 1]
            depth = (p1[2] + p2[2]) * 0.5
            frac = (uFrac + (j / steps) * 0.4) % 1.0
            r = int(EMBER[0] * (1.0 - frac) + SIGNAL[0] * frac)
            g = int(EMBER[1] * (1.0 - frac) + SIGNAL[1] * frac)
            b = int(EMBER[2] * (1.0 - frac) + SIGNAL[2] * frac)
            alpha = int(np.clip(190 - depth * 35, 60, 255))
            draw.line([(p1[0], p1[1]), (p2[0], p2[1])], fill=(r, g, b, alpha), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "catalan-surface-3d.jpg"), quality=94)
    print("catalan-surface-3d.jpg saved")

def render_vosim_voice():
    """VOSIM voice synth decaying squared-sine acoustic pulse bursts."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cy = H * 0.52

    # Draw grid
    for gx in range(0, W, 60):
        draw.line([(gx, 0), (gx, H)], fill=(32, 38, 63, 100), width=1)
    for gy in range(0, H, 60):
        draw.line([(0, gy), (W, gy)], fill=(32, 38, 63, 100), width=1)

    points = []
    subPoints = []
    formantPoints = []

    periods = 5.0
    for i in range(W):
        t = (i / W) * periods
        fundPhase = t % 1.0
        fRatio = 8.0
        pulseIdx = math.floor(fundPhase * fRatio)
        pulseFrac = (fundPhase * fRatio) % 1.0

        decay = 0.68
        pulse = (math.sin(math.pi * pulseFrac)**2) * (decay**pulseIdx) if pulseIdx < 5 else 0.0
        sub = math.sin(t * math.pi) * 0.32

        sig = pulse * 1.6 + sub
        points.append((i, cy - sig * 160))
        subPoints.append((i, cy - sub * 160))
        formantPoints.append((i, cy - pulse * 1.6 * 160))

    for i in range(W - 1):
        draw.line([subPoints[i], subPoints[i + 1]], fill=(78, 242, 184, 90), width=2)
        draw.line([formantPoints[i], formantPoints[i + 1]], fill=(255, 107, 53, 120), width=2)
        draw.line([points[i], points[i + 1]], fill=(167, 177, 255, 255), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "vosim-voice-synth.jpg"), quality=94)
    print("vosim-voice-synth.jpg saved")

def render_west_coast_fold():
    """West Coast fold synth complex FM + multi-stage wavefolding."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cy = H * 0.52

    for gx in range(0, W, 60):
        draw.line([(gx, 0), (gx, H)], fill=(32, 38, 63, 100), width=1)
    for gy in range(0, H, 60):
        draw.line([(0, gy), (W, gy)], fill=(32, 38, 63, 100), width=1)

    points = []
    corePoints = []
    folded1Points = []

    cycles = 4.0
    for i in range(W):
        t = (i / W) * cycles * 2.0 * math.pi
        mod = math.sin(t * 2.0) * 1.2
        core = math.sin(t + mod * 0.4) + 0.15

        f1 = math.sin(core * 2.5 * 1.5707963)
        f2 = math.sin(f1 * 2.5 * 1.5707963)

        points.append((i, cy - f2 * 170))
        corePoints.append((i, cy - core * 70))
        folded1Points.append((i, cy - f1 * 120))

    for i in range(W - 1):
        draw.line([corePoints[i], corePoints[i + 1]], fill=(78, 242, 184, 80), width=2)
        draw.line([folded1Points[i], folded1Points[i + 1]], fill=(167, 177, 255, 120), width=2)
        draw.line([points[i], points[i + 1]], fill=(255, 107, 53, 255), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "west-coast-fold-synth.jpg"), quality=94)
    print("west-coast-fold-synth.jpg saved")

def render_bbd_bucket_brigade():
    """BBD bucket brigade analog multi-stage charge transfer delay."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cy = H * 0.5

    for gx in range(0, W, 60):
        draw.line([(gx, 0), (gx, H)], fill=(32, 38, 63, 100), width=1)
    for gy in range(0, H, 60):
        draw.line([(0, gy), (W, gy)], fill=(32, 38, 63, 100), width=1)

    # Draw discrete capacitor bucket cascade steps
    stages = 32
    stageW = W / stages
    for s in range(stages):
        sx = s * stageW
        t = s / stages * 4.0 * math.pi
        amp = math.sin(t) * math.exp(-s * 0.05)
        barH = amp * 160
        r = int(EMBER[0] * (1.0 - s / stages) + SIGNAL[0] * (s / stages))
        g = int(EMBER[1] * (1.0 - s / stages) + SIGNAL[1] * (s / stages))
        b = int(EMBER[2] * (1.0 - s / stages) + SIGNAL[2] * (s / stages))

        y0 = min(cy, cy - barH)
        y1 = max(cy, cy - barH)
        draw.rectangle([(sx + 3, y0), (sx + stageW - 3, y1)], fill=(r, g, b, 160), outline=(r, g, b, 230))

    # Draw smooth analog reconstructed filter output
    smoothPts = []
    for i in range(W):
        t = (i / W) * 4.0 * math.pi
        amp = math.sin(t - 0.35) * math.exp(-(i / W) * 1.5) * 160
        smoothPts.append((i, cy - amp))

    for i in range(W - 1):
        draw.line([smoothPts[i], smoothPts[i + 1]], fill=(78, 242, 184, 240), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "bbd-bucket-brigade-fx.jpg"), quality=94)
    print("bbd-bucket-brigade-fx.jpg saved")

def render_dynamic_transient_shaper():
    """Dual-envelope differential transient designer with attack punch peaks."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cy = H * 0.65

    for gx in range(0, W, 60):
        draw.line([(gx, 0), (gx, H)], fill=(32, 38, 63, 100), width=1)
    for gy in range(0, H, 60):
        draw.line([(0, gy), (W, gy)], fill=(32, 38, 63, 100), width=1)

    # Render transient punch peaks
    origPts, fastPts, slowPts, punchPts = [], [], [], []
    for i in range(W):
        beatT = (i % 225) / 225.0
        drum = math.exp(-beatT * 14.0) * math.sin(beatT * 40.0)
        fast = math.exp(-beatT * 12.0)
        slow = math.exp(-beatT * 3.5)
        punch = drum * (1.0 + max(0.0, fast - slow) * 2.8)

        origPts.append((i, cy - drum * 180))
        fastPts.append((i, cy - fast * 190))
        slowPts.append((i, cy - slow * 190))
        punchPts.append((i, cy - punch * 180))

    for i in range(W - 1):
        draw.line([slowPts[i], slowPts[i + 1]], fill=(167, 177, 255, 110), width=2)
        draw.line([fastPts[i], fastPts[i + 1]], fill=(78, 242, 184, 130), width=2)
        draw.line([origPts[i], origPts[i + 1]], fill=(78, 242, 184, 70), width=2)
        draw.line([punchPts[i], punchPts[i + 1]], fill=(255, 107, 53, 255), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "dynamic-transient-shaper-fx.jpg"), quality=94)
    print("dynamic-transient-shaper-fx.jpg saved")

def render_ferrofluid_spikes():
    """Magnetic liquid ferrofluid hedgehog spikes simulation."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    y, x = np.ogrid[:H, :W]
    dx = (x - W / 2) / (min(W, H) * 0.5)
    dy = (y - H / 2) / (min(W, H) * 0.5)
    r = np.sqrt(dx**2 + dy**2)
    ang = np.arctan2(dy, dx)

    spikes = 12.0
    harm1 = np.sin(ang * spikes)
    harm2 = np.cos(ang * spikes * 0.5 + 0.8)
    spikeMod = (harm1 * 0.7 + harm2 * 0.3) * 0.26

    spikeR = 0.32 + spikeMod
    distCore = r - spikeR

    rings = np.sin(r * 28.0)
    fieldLines = np.clip(1.0 - np.abs(rings) / 0.3, 0.0, 1.0)

    core = np.clip(-distCore / 0.08, 0.0, 1.0)
    edge = np.clip(1.0 - np.abs(distCore) / 0.07, 0.0, 1.0)

    for y_idx in range(H):
        for x_idx in range(W):
            cr = core[y_idx, x_idx]
            ed = edge[y_idx, x_idx]
            fl = fieldLines[y_idx, x_idx]
            a = ang[y_idx, x_idx]
            rad = r[y_idx, x_idx]

            sheen = SIGNAL * (np.sin(a * 3.0) * 0.5 + 0.5) + EMERALD * (np.cos(a * 3.0) * 0.5 + 0.5)
            glow = sheen * (ed * 1.8 + cr * 0.7 + fl * 0.45) + EMBER * (ed * 0.8)
            vig = np.clip((1.7 - rad) / 0.6, 0.0, 1.0)
            out[y_idx, x_idx] = np.clip((GROUND * (1.0 - cr * 0.5) + glow) * vig + GROUND * (1.0 - vig), 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "ferrofluid-spikes.jpg"), quality=94)
    print("ferrofluid-spikes.jpg saved")

def render_prismatic_hologram():
    """Holographic transmission diffraction grating fringes."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    y, x = np.ogrid[:H, :W]
    dx = (x - W / 2) / (min(W, H) * 0.5)
    dy = (y - H / 2) / (min(W, H) * 0.5)
    r = np.sqrt(dx**2 + dy**2)
    ang = np.arctan2(dy, dx)

    grating = 22.0
    g1 = np.sin((dx * 0.707 + dy * 0.707) * grating)
    g2 = np.sin((dx * 0.707 - dy * 0.707) * grating)
    fringes = g1 * g2

    fringeR = np.sin(fringes * 4.0 + 1.6) * 0.5 + 0.5
    fringeG = np.sin(fringes * 4.0) * 0.5 + 0.5
    fringeB = np.sin(fringes * 4.0 - 1.6) * 0.5 + 0.5

    ring = np.clip(1.0 - np.abs(r - 0.42 - 0.06 * np.sin(ang * 6.0)) / 0.08, 0.0, 1.0)
    streaks = np.clip(np.abs(np.cos(ang * 8.0))**10, 0.0, 1.0)

    for y_idx in range(H):
        for x_idx in range(W):
            fr_r = fringeR[y_idx, x_idx]
            fr_g = fringeG[y_idx, x_idx]
            fr_b = fringeB[y_idx, x_idx]
            rg = ring[y_idx, x_idx]
            st = streaks[y_idx, x_idx]
            rad = r[y_idx, x_idx]

            iris = np.array([fr_r * 255, fr_g * 240, fr_b * 255], dtype=np.float32)
            holoCol = iris * 0.65 + SIGNAL * 0.35 + EMBER * (st * 0.7) + EMERALD * (rg * 0.5)
            vig = np.clip((1.6 - rad) / 0.7, 0.0, 1.0)
            out[y_idx, x_idx] = np.clip(GROUND * (1.0 - vig) + holoCol * vig, 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "prismatic-hologram.jpg"), quality=94)
    print("prismatic-hologram.jpg saved")

if __name__ == "__main__":
    render_rabinovich_fabrikant()
    render_penrose_tiling()
    render_scherk_surface()
    render_catalan_surface()
    render_vosim_voice()
    render_west_coast_fold()
    render_bbd_bucket_brigade()
    render_dynamic_transient_shaper()
    render_ferrofluid_spikes()
    render_prismatic_hologram()
    print("All 10 Pack 5 preview images successfully generated!")

    # Run library thumbs generator
    thumbs_script = os.path.join(ROOT, "tools", "website", "gen_library_thumbs.py")
    if os.path.exists(thumbs_script):
        subprocess.run([sys.executable, thumbs_script], check=True)
