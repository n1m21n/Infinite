#!/usr/bin/env python3
"""Generate high-fidelity preview images for the 10 new creative library items (Pack 6).
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

def render_barnsley_fern():
    """Michael Barnsley's affine IFS fractal fern."""
    acc = np.zeros((H, W), dtype=np.float32)
    cx, cy = W * 0.5, H * 0.94
    scale = min(W, H) * 0.088

    x, y = 0.0, 0.0
    np.random.seed(42)

    # Pre-generate random numbers for high speed
    n_pts = 1200000
    r_vals = np.random.rand(n_pts)

    for i in range(n_pts):
        r = r_vals[i]
        if r < 0.01:
            nx = 0.0
            ny = 0.16 * y
        elif r < 0.86:
            nx = 0.85 * x + 0.04 * y
            ny = -0.04 * x + 0.85 * y + 1.6
        elif r < 0.93:
            nx = 0.20 * x - 0.26 * y
            ny = 0.23 * x + 0.22 * y + 1.6
        else:
            nx = -0.15 * x + 0.28 * y
            ny = 0.26 * x + 0.24 * y + 0.44

        x, y = nx, ny

        if i > 50:
            px = int(cx + x * scale)
            py = int(cy - y * scale)
            if 0 <= px < W and 0 <= py < H:
                acc[py, px] += 1.0

    acc = np.log1p(acc * 1.5)
    norm = np.clip(acc / (np.percentile(acc[acc > 0], 99.4) if np.any(acc > 0) else 1.0), 0.0, 1.0)
    out = np.zeros((H, W, 3), dtype=np.float32)
    for c_idx in range(3):
        out[:, :, c_idx] = GROUND[c_idx]

    for y_idx in range(H):
        frac_y = 1.0 - (y_idx / H)
        for x_idx in range(W):
            v = norm[y_idx, x_idx]
            if v > 0:
                frac = v**0.8
                mix_col = EMERALD * (1.0 - frac * 0.5) + SIGNAL * (frac * 0.7) + EMBER * (frac_y * 0.3)
                out[y_idx, x_idx] = np.clip(GROUND * (1.0 - v) + mix_col * v * 1.8 + WHITE * (v**3 * 1.2), 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "barnsley-fern.jpg"), quality=94)
    print("barnsley-fern.jpg saved")

def render_gingerbreadman_map():
    """Robert Devaney's chaotic gingerbreadman piecewise linear map."""
    acc = np.zeros((H, W), dtype=np.float32)
    cx, cy = W * 0.48, H * 0.52
    scale = min(W, H) * 0.082

    seeds = [
      (-0.1, 0.0), (-0.5, 0.3), (-1.2, 0.7), (-2.1, 1.4),
      (-2.8, 1.9), (-3.5, 2.6), (-0.8, -0.6), (-1.6, -1.2),
      (-2.4, -1.8), (-3.2, -2.5), (-4.1, 0.5), (-1.0, 2.2)
    ]

    steps_per_seed = 150000
    for sid, (sx, sy) in enumerate(seeds):
        x, y = sx, sy
        for i in range(steps_per_seed):
            nx = 1.0 - y + abs(x)
            ny = x
            x, y = nx, ny

            if i > 20:
                px = int(cx + x * scale)
                py = int(cy - y * scale)
                if 0 <= px < W and 0 <= py < H:
                    acc[py, px] += 1.0

    acc = np.log1p(acc * 2.0)
    norm = np.clip(acc / (np.percentile(acc[acc > 0], 99.2) if np.any(acc > 0) else 1.0), 0.0, 1.0)
    out = np.zeros((H, W, 3), dtype=np.float32)
    for c_idx in range(3):
        out[:, :, c_idx] = GROUND[c_idx]

    for y_idx in range(H):
        for x_idx in range(W):
            v = norm[y_idx, x_idx]
            if v > 0:
                frac = v**0.75
                mix_col = EMBER * (1.0 - frac) + SIGNAL * (frac * 0.6) + WHITE * (v**2 * 0.4)
                out[y_idx, x_idx] = np.clip(GROUND * (1.0 - v) + mix_col * v * 2.1, 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "gingerbreadman-map.jpg"), quality=94)
    print("gingerbreadman-map.jpg saved")

def render_henneberg_surface():
    """Ernst Henneberg's non-orientable minimal surface 3D."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2
    scale = min(W, H) * 0.19

    layers = 46
    steps = 54
    u_range = 1.25
    v_range = math.pi
    rotY = 0.52
    rotX = 0.48

    cosY, sinY = math.cos(rotY), math.sin(rotY)
    cosX, sinX = math.cos(rotX), math.sin(rotX)

    ribs = []
    for i in range(layers):
        uFrac = i / (layers - 1)
        u = -u_range + uFrac * 2.0 * u_range
        sinhU = math.sinh(u)
        sinh3U = math.sinh(3.0 * u)
        cosh2U = math.cosh(2.0 * u)

        pts = []
        for j in range(steps + 1):
            vFrac = j / steps
            v = -v_range * 0.5 + vFrac * v_range
            cosV = math.cos(v)
            sinV = math.sin(v)
            cos3V = math.cos(3.0 * v)
            sin3V = math.sin(3.0 * v)
            cos2V = math.cos(2.0 * v)

            x3d = 2.0 * sinhU * cosV - (2.0 / 3.0) * sinh3U * cos3V
            y3d = 2.0 * cosh2U * cos2V - 1.0
            z3d = 2.0 * sinhU * sinV + (2.0 / 3.0) * sinh3U * sin3V

            x1 = x3d * cosY + z3d * sinY
            z1 = -x3d * sinY + z3d * cosY
            y1 = y3d * cosX - z1 * sinX
            z2 = y3d * sinX + z1 * cosX

            dist = 4.5
            focal = 1.0 / (dist + z2 * 0.22)
            px = cx + x1 * scale * focal * 3.6
            py = cy - y1 * scale * focal * 3.6
            pts.append((px, py, z2))

        ribs.append((uFrac, pts))

    for uFrac, pts in ribs:
        for j in range(len(pts) - 1):
            p1, p2 = pts[j], pts[j + 1]
            depth = (p1[2] + p2[2]) * 0.5
            frac = (uFrac + (j / steps) * 0.4) % 1.0
            r = int(SIGNAL[0] * (1.0 - frac) + EMERALD[0] * frac)
            g = int(SIGNAL[1] * (1.0 - frac) + EMERALD[1] * frac)
            b = int(SIGNAL[2] * (1.0 - frac) + EMERALD[2] * frac)
            alpha = int(np.clip(190 - depth * 35, 60, 255))
            draw.line([(p1[0], p1[1]), (p2[0], p2[1])], fill=(r, g, b, alpha), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "henneberg-surface-3d.jpg"), quality=94)
    print("henneberg-surface-3d.jpg saved")

def render_jeener_klein():
    """Patrice Jeener's 8-surface Klein bottle minimal immersion 3D."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2
    scale = min(W, H) * 0.26

    rings = 42
    slices = 52
    rotY = 0.62
    rotX = 0.55

    cosY, sinY = math.cos(rotY), math.sin(rotY)
    cosX, sinX = math.cos(rotX), math.sin(rotX)

    ribs = []
    for i in range(rings):
        uFrac = i / rings
        u = uFrac * 2.0 * math.pi
        halfU = u * 0.5
        cosHalfU = math.cos(halfU)
        sinHalfU = math.sin(halfU)
        cosU = math.cos(u)
        r = 2.0 - cosU

        pts = []
        for j in range(slices + 1):
            vFrac = j / slices
            v = vFrac * 2.0 * math.pi
            cosV = math.cos(v)
            sinV = math.sin(v)
            sin2V = math.sin(2.0 * v)

            lobe = r * cosHalfU - sinHalfU * sin2V
            x3d = lobe * cosV * 0.55
            y3d = (r * sinHalfU + cosHalfU * sin2V) * 0.55
            z3d = lobe * sinV * 0.55

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
            frac = (uFrac + (j / slices) * 0.4) % 1.0
            r = int(EMBER[0] * (1.0 - frac) + SIGNAL[0] * frac)
            g = int(EMBER[1] * (1.0 - frac) + SIGNAL[1] * frac)
            b = int(EMBER[2] * (1.0 - frac) + SIGNAL[2] * frac)
            alpha = int(np.clip(190 - depth * 35, 60, 255))
            draw.line([(p1[0], p1[1]), (p2[0], p2[1])], fill=(r, g, b, alpha), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "jeener-klein-3d.jpg"), quality=94)
    print("jeener-klein-3d.jpg saved")

def render_phase_resonance():
    """Casio CZ-style phase resonance waveform with synchronous windowing."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cy = H * 0.52

    # Draw grid
    for gx in range(0, W, 60):
        draw.line([(gx, 0), (gx, H)], fill=(32, 38, 63, 100), width=1)
    for gy in range(0, H, 60):
        draw.line([(0, gy), (W, gy)], fill=(32, 38, 63, 100), width=1)

    points = []
    winPoints = []
    carrierPoints = []

    periods = 4.0
    for i in range(W):
        t = (i / W) * periods
        fundPhase = t % 1.0
        resMult = 6.0
        resPhase = (fundPhase * resMult) % 1.0

        carrier = math.cos(2.0 * math.pi * resPhase)
        win = 1.0 - fundPhase
        sig = carrier * win * 0.75 + math.sin(2.0 * math.pi * fundPhase) * 0.25

        points.append((i, cy - sig * 160))
        winPoints.append((i, cy - win * 160))
        carrierPoints.append((i, cy - carrier * 0.7 * 160))

    for i in range(W - 1):
        draw.line([winPoints[i], winPoints[i + 1]], fill=(78, 242, 184, 90), width=2)
        draw.line([carrierPoints[i], carrierPoints[i + 1]], fill=(255, 107, 53, 110), width=2)
        draw.line([points[i], points[i + 1]], fill=(167, 177, 255, 255), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "phase-resonance-synth.jpg"), quality=94)
    print("phase-resonance-synth.jpg saved")

def render_scanned_synthesis():
    """Scanned synthesis physical modeling mass-spring finite element trajectory."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cy = H * 0.52

    for gx in range(0, W, 60):
        draw.line([(gx, 0), (gx, H)], fill=(32, 38, 63, 100), width=1)
    for gy in range(0, H, 60):
        draw.line([(0, gy), (W, gy)], fill=(32, 38, 63, 100), width=1)

    pts = []
    pts_orbit = []
    periods = 5.0
    for i in range(W):
        t = (i / W) * periods
        m1 = math.sin(t * 2.0 * math.pi) * 0.8
        m2 = math.sin(t * 4.0 * math.pi + 0.8) * 0.5
        m3 = math.sin(t * 6.0 * math.pi + 1.6) * 0.3
        sig = math.tanh((m1 + m2 + m3) * 1.5)
        pts.append((i, cy - sig * 150))
        pts_orbit.append((i, cy - (m1 + m2) * 110))

    for i in range(W - 1):
        draw.line([pts_orbit[i], pts_orbit[i + 1]], fill=(78, 242, 184, 110), width=2)
        draw.line([pts[i], pts[i + 1]], fill=(255, 107, 53, 255), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "scanned-synthesis-synth.jpg"), quality=94)
    print("scanned-synthesis-synth.jpg saved")

def render_schroeder_fdn_reverb():
    """Schroeder 4-line feedback delay network reverberator impulse response."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cy = H * 0.52

    for gx in range(0, W, 60):
        draw.line([(gx, 0), (gx, H)], fill=(32, 38, 63, 100), width=1)
    for gy in range(0, H, 60):
        draw.line([(0, gy), (W, gy)], fill=(32, 38, 63, 100), width=1)

    pts_env = []
    pts_reverb = []
    np.random.seed(101)
    noise = np.random.randn(W)

    for i in range(W):
        t = i / W
        env = math.exp(-t * 4.2)
        tail = noise[i] * env * (0.8 + 0.2 * math.sin(t * 30.0))
        pts_reverb.append((i, cy - tail * 170))
        pts_env.append((i, cy - env * 170))

    for i in range(W - 1):
        draw.line([pts_env[i], pts_env[i + 1]], fill=(255, 107, 53, 140), width=2)
        draw.line([pts_reverb[i], pts_reverb[i + 1]], fill=(167, 177, 255, 230), width=2)

    im.save(os.path.join(PREVIEWS_DIR, "schroeder-fdn-reverb-fx.jpg"), quality=94)
    print("schroeder-fdn-reverb-fx.jpg saved")

def render_barberpole_flanger():
    """Shepard-tone infinite barberpole flanger multi-tap response."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cy = H * 0.52

    for gx in range(0, W, 60):
        draw.line([(gx, 0), (gx, H)], fill=(32, 38, 63, 100), width=1)
    for gy in range(0, H, 60):
        draw.line([(0, gy), (W, gy)], fill=(32, 38, 63, 100), width=1)

    pts1, pts2, pts_comb = [], [], []
    for i in range(W):
        f = (i / W) * 8.0
        w1 = math.sin(f * math.pi * 0.5)**2
        w2 = math.cos(f * math.pi * 0.5)**2
        comb = math.sin(f * 2.8) * w1 + math.cos(f * 2.8 + 1.2) * w2
        pts1.append((i, cy - w1 * 130))
        pts2.append((i, cy - w2 * 130))
        pts_comb.append((i, cy - comb * 150))

    for i in range(W - 1):
        draw.line([pts1[i], pts1[i + 1]], fill=(78, 242, 184, 100), width=2)
        draw.line([pts2[i], pts2[i + 1]], fill=(255, 107, 53, 100), width=2)
        draw.line([pts_comb[i], pts_comb[i + 1]], fill=(167, 177, 255, 240), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "barberpole-flanger-fx.jpg"), quality=94)
    print("barberpole-flanger-fx.jpg saved")

def render_magnetic_plasma_torus():
    """Magnetic plasma torus with helical flux lines and synchrotron radiation."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    aspect = W / H
    x = np.linspace(-0.5 * aspect, 0.5 * aspect, W)
    y = np.linspace(-0.5, 0.5, H)
    xx, yy = np.meshgrid(x, y)

    r = np.sqrt(xx*xx + yy*yy)
    ang = np.arctan2(yy, xx)

    majorRadius = 0.35
    minorRadius = 0.12
    torusDist = np.abs(r - majorRadius) - minorRadius

    helix = np.sin(ang * 8.0 + r * 28.0)
    filaments = np.clip(1.0 - np.abs(helix) / 0.18, 0.0, 1.0)

    core = np.clip((0.06 - torusDist) / 0.08, 0.0, 1.0)
    sheath = np.clip((0.14 - np.abs(torusDist)) / 0.14, 0.0, 1.0)
    flares = np.clip(1.0 - np.abs(np.sin(ang * 16.0)) / 0.2, 0.0, 1.0) * sheath

    for y_idx in range(H):
        for x_idx in range(W):
            a = ang[y_idx, x_idx]
            sh = sheath[y_idx, x_idx]
            cr = core[y_idx, x_idx]
            fl = flares[y_idx, x_idx]
            fil = filaments[y_idx, x_idx]

            mix_phase = math.sin(a * 4.0) * 0.5 + 0.5
            plasmaCol = EMBER * (1.0 - mix_phase) + SIGNAL * mix_phase
            plasmaCol = plasmaCol * (1.0 - fil * 0.7) + EMERALD * (fil * 0.7)

            glow = plasmaCol * (sh * 1.6 + cr * 1.2 + fl * 1.8 + fil * 0.6)
            total = np.clip(cr + sh + fil * 0.4 + fl, 0.0, 1.0)
            out[y_idx, x_idx] = np.clip(GROUND * (1.0 - total) + glow * total, 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "magnetic-plasma-torus.jpg"), quality=94)
    print("magnetic-plasma-torus.jpg saved")

def render_hyperbolic_tessellation():
    """Poincaré disk hyperbolic tessellation with quasicrystal reflection symmetry."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    aspect = W / H
    x = np.linspace(-0.5 * aspect, 0.5 * aspect, W) * 2.2
    y = np.linspace(-0.5, 0.5, H) * 2.2
    xx, yy = np.meshgrid(x, y)

    r = np.sqrt(xx*xx + yy*yy)
    ang = np.arctan2(yy, xx)

    diskR = 0.95
    inside = np.clip((diskR - r) / 0.04, 0.0, 1.0)
    horizon = np.clip((0.06 - np.abs(r - diskR)) / 0.06, 0.0, 1.0)

    # Simplified iterative inversion for preview
    curr_x = xx.copy()
    curr_y = yy.copy()
    for _ in range(4):
        lenSq = curr_x*curr_x + curr_y*curr_y
        mask = lenSq > 0.65
        factor = np.where(mask, 0.65 / np.maximum(lenSq, 0.001), 1.0)
        curr_x *= factor
        curr_y *= factor
        curr_ang = np.arctan2(curr_y, curr_x)
        sym_ang = np.mod(curr_ang, 2.0 * math.pi / 7.0) - math.pi / 7.0
        curr_len = np.sqrt(curr_x*curr_x + curr_y*curr_y)
        curr_x = np.cos(sym_ang) * curr_len
        curr_y = np.sin(sym_ang) * curr_len

    grid = np.sin(curr_x * 14.0) * np.sin(curr_y * 14.0)
    edges = np.clip((0.15 - np.abs(grid)) / 0.15, 0.0, 1.0)

    for y_idx in range(H):
        for x_idx in range(W):
            a = ang[y_idx, x_idx]
            ed = edges[y_idx, x_idx]
            ins = inside[y_idx, x_idx]
            hz = horizon[y_idx, x_idx]

            mix_phase = math.sin(a * 3.0) * 0.5 + 0.5
            spectral = SIGNAL * (1.0 - mix_phase) + EMERALD * mix_phase
            spectral = spectral * (1.0 - ed * 0.4) + EMBER * (ed * 0.4)

            glow = spectral * (ed * 1.5 + hz * 2.0 + 0.25)
            total = np.clip(ins * (ed + 0.2) + hz, 0.0, 1.0)
            out[y_idx, x_idx] = np.clip(GROUND * (1.0 - total) + glow * total, 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "hyperbolic-tessellation.jpg"), quality=94)
    print("hyperbolic-tessellation.jpg saved")

if __name__ == "__main__":
    render_barnsley_fern()
    render_gingerbreadman_map()
    render_henneberg_surface()
    render_jeener_klein()
    render_phase_resonance()
    render_scanned_synthesis()
    render_schroeder_fdn_reverb()
    render_barberpole_flanger()
    render_magnetic_plasma_torus()
    render_hyperbolic_tessellation()
    print("All 10 Pack 6 preview images successfully generated!")

    # Run library thumbs generator
    thumbs_script = os.path.join(ROOT, "tools", "website", "gen_library_thumbs.py")
    if os.path.exists(thumbs_script):
        subprocess.run([sys.executable, thumbs_script], check=True)
