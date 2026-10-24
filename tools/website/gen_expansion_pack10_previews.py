#!/usr/bin/env python3
"""Generate high-fidelity preview images for the 10 new creative library items (Pack 10).
Matches brand design: midnight ground #151930, signal #A7B1FF, ember #FF6B35, emerald #4EF2B8, white.
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

def is_prime(n):
    if n < 2: return False
    if n in (2, 3): return True
    if n % 2 == 0 or n % 3 == 0: return False
    i = 5
    w = 2
    while i * i <= n:
        if n % i == 0: return False
        i += w
        w = 6 - w
    return True

def render_ulam_spiral_primes():
    """Ulam prime number polar spiral constellation with diagonal alignments."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5
    scale = min(W, H) * 0.44

    # Background polar grid
    for r in range(1, 6):
        draw.ellipse([cx - scale * (r / 5), cy - scale * (r / 5),
                      cx + scale * (r / 5), cy + scale * (r / 5)],
                     outline=(32, 38, 70, 110), width=1)

    # Archimedean / quadratic spiral line
    spiral_pts = []
    max_n = 1200
    for n in range(1, max_n + 1):
        theta = math.sqrt(n) * 2.5
        r = (math.sqrt(n) / math.sqrt(max_n)) * scale
        px = cx + r * math.cos(theta)
        py = cy + r * math.sin(theta)
        spiral_pts.append((px, py, n))

    for i in range(len(spiral_pts) - 1):
        draw.line([(spiral_pts[i][0], spiral_pts[i][1]), (spiral_pts[i+1][0], spiral_pts[i+1][1])],
                  fill=(36, 44, 80, 80), width=1)

    # Highlight prime numbers with glowing nodes
    for px, py, n in spiral_pts:
        if is_prime(n):
            frac = n / max_n
            # Check quadratic alignments (Euler prime polynomial n^2 - n + 41 or 4n^2 ...)
            is_euler = any(n == (x*x - x + 41) for x in range(1, 40))
            if is_euler:
                draw.ellipse([px - 5, py - 5, px + 5, py + 5], fill=(255, 107, 53, 230))
                draw.ellipse([px - 2, py - 2, px + 2, py + 2], fill=(255, 245, 235, 255))
            else:
                col_v = SIGNAL * (1.0 - frac) + EMERALD * frac
                draw.ellipse([px - 3, py - 3, px + 3, py + 3], fill=(int(col_v[0]), int(col_v[1]), int(col_v[2]), 200))

    # Center nucleus
    draw.ellipse([cx - 6, cy - 6, cx + 6, cy + 6], fill=(255, 107, 53, 255), outline=(255, 240, 220, 255))

    im.save(os.path.join(PREVIEWS_DIR, "ulam-spiral-primes.jpg"), quality=94)
    print("ulam-spiral-primes.jpg saved")

def render_mandelbrot_buddhabrot_path():
    """Buddhabrot escaping orbit trajectory filaments and probability density."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.48, H * 0.5
    scale = min(W, H) * 0.40

    # Draw complex axes
    draw.line([(cx - scale * 1.5, cy), (cx + scale * 1.2, cy)], fill=(32, 38, 70, 110), width=1)
    draw.line([(cx, cy - scale * 1.2), (cx, cy + scale * 1.2)], fill=(32, 38, 70, 110), width=1)

    # Compute Buddhabrot orbits
    np.random.seed(42)
    num_samples = 450
    max_iter = 80
    trajectories = []

    for _ in range(num_samples):
        cr = np.random.uniform(-2.0, 1.0)
        ci = np.random.uniform(-1.5, 1.5)
        zr, zi = 0.0, 0.0
        orbit = []
        escaped = False
        for it in range(max_iter):
            zr_new = zr * zr - zi * zi + cr
            zi_new = 2.0 * zr * zi + ci
            zr, zi = zr_new, zi_new
            orbit.append((zr, zi))
            if zr * zr + zi * zi > 4.0:
                escaped = True
                break
        if escaped and len(orbit) > 4:
            trajectories.append(orbit)

    # Plot orbital filaments
    for orbit in trajectories:
        pts = []
        for zr, zi in orbit:
            px = cx + zr * scale * 0.8
            py = cy + zi * scale * 0.8
            pts.append((px, py))
        frac = len(orbit) / max_iter
        col_v = EMERALD * (1.0 - frac) + SIGNAL * frac
        col = (int(col_v[0]), int(col_v[1]), int(col_v[2]), int(40 + 100 * frac))
        for k in range(len(pts) - 1):
            draw.line([pts[k], pts[k+1]], fill=col, width=1)
            # Symmetric reflection across imaginary axis for Buddhabrot symmetry
            p1_sym = (pts[k][0], 2 * cy - pts[k][1])
            p2_sym = (pts[k+1][0], 2 * cy - pts[k+1][1])
            draw.line([p1_sym, p2_sym], fill=col, width=1)

    # Critical orbit traps
    traps = [(0, 0), (-0.75, 0), (-0.12, 0.74), (-0.12, -0.74), (-1.25, 0)]
    for tx, ty in traps:
        px = cx + tx * scale * 0.8
        py = cy + ty * scale * 0.8
        draw.ellipse([px - 5, py - 5, px + 5, py + 5], fill=(255, 107, 53, 230), outline=(255, 235, 220, 255))

    im.save(os.path.join(PREVIEWS_DIR, "mandelbrot-buddhabrot-path.jpg"), quality=94)
    print("mandelbrot-buddhabrot-path.jpg saved")

def render_cross_cap_surface():
    """Non-orientable cross-cap immersion of RP2 with Whitney pinch points."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5

    rotY, rotX = 0.62, 0.44
    scale = 145.0

    def project(x, y, z):
        x1 = x * math.cos(rotY) + z * math.sin(rotY)
        z1 = -x * math.sin(rotY) + z * math.cos(rotY)
        y2 = y * math.cos(rotX) - z1 * math.sin(rotX)
        z2 = y * math.sin(rotX) + z1 * math.cos(rotX)
        dist = 4.2 - z2
        px = cx + (x1 / dist) * 3.4 * scale
        py = cy - (y2 / dist) * 3.4 * scale
        return px, py, z2

    uSteps = 36
    vSteps = 48
    lines = []

    for i in range(uSteps):
        u = (i / uSteps) * math.pi
        sinU = math.sin(u)
        cosU = math.cos(u)
        sin2U = math.sin(2.0 * u)
        cos2U = math.cos(2.0 * u)
        pts = []
        for j in range(vSteps + 1):
            v = (j / vSteps) * math.pi
            cosV = math.cos(v)
            sin2V = math.sin(2.0 * v)
            cos2V = cosV * cosV
            # Standard cross-cap parametric equation:
            # x = r * sin(u) * sin(2v) / 2
            # y = r * sin(2u) * cos^2(v)
            # z = r * cos(2u) * cos^2(v)
            r = 1.3
            x = r * sinU * sin2V * 0.5
            y = r * sin2U * cos2V
            z = r * cos2U * cos2V
            pts.append(project(x, y, z))
        avgZ = sum(p[2] for p in pts) / len(pts)
        lines.append((avgZ, i / uSteps, pts))

    lines.sort(key=lambda item: item[0])

    for avgZ, frac, pts in lines:
        alpha = int(np.clip(120 + avgZ * 85, 40, 240))
        col_v = SIGNAL * (1.0 - frac) + EMBER * frac
        col = (int(col_v[0]), int(col_v[1]), int(col_v[2]), alpha)
        for i in range(len(pts) - 1):
            draw.line([(pts[i][0], pts[i][1]), (pts[i+1][0], pts[i+1][1])], fill=col, width=2)

    # Whitney pinch points
    pinch_pts = [project(0, 1.3, 1.3), project(0, -1.3, 1.3), project(0, 0, -1.3)]
    for p in pinch_pts:
        draw.ellipse([p[0] - 6, p[1] - 6, p[0] + 6, p[1] + 6], fill=(78, 242, 184, 255), outline=(255, 255, 255, 255))

    im.save(os.path.join(PREVIEWS_DIR, "cross-cap-surface-3d.jpg"), quality=94)
    print("cross-cap-surface-3d.jpg saved")

def render_barth_sextic_surface():
    """Degree-6 Barth sextic algebraic surface with 65 double points and icosahedral symmetry."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W * 0.5, H * 0.5

    rotY, rotX = 0.58, 0.46
    scale = 140.0
    phi = (1.0 + math.sqrt(5.0)) * 0.5

    def project(x, y, z):
        x1 = x * math.cos(rotY) + z * math.sin(rotY)
        z1 = -x * math.sin(rotY) + z * math.cos(rotY)
        y2 = y * math.cos(rotX) - z1 * math.sin(rotX)
        z2 = y * math.sin(rotX) + z1 * math.cos(rotX)
        dist = 4.0 - z2
        px = cx + (x1 / dist) * 3.4 * scale
        py = cy - (y2 / dist) * 3.4 * scale
        return px, py, z2

    uSteps = 32
    vSteps = 50
    lines = []

    for i in range(uSteps):
        theta = (i / uSteps) * math.pi
        sinT = math.sin(theta)
        cosT = math.cos(theta)
        pts = []
        for j in range(vSteps + 1):
            v = (j / vSteps) * 2.0 * math.pi
            # Icosahedral sextic radial harmonic modulation
            c5 = math.cos(5.0 * v)
            s5 = math.sin(5.0 * v)
            mod = 1.0 + 0.32 * (sinT**5 * c5 + cosT * (sinT**3 * s5))
            r = 1.15 * (0.75 + 0.35 * mod)
            x = r * sinT * math.cos(v)
            y = r * cosT
            z = r * sinT * math.sin(v)
            pts.append(project(x, y, z))
        avgZ = sum(p[2] for p in pts) / len(pts)
        lines.append((avgZ, i / uSteps, pts))

    lines.sort(key=lambda item: item[0])

    for avgZ, frac, pts in lines:
        alpha = int(np.clip(130 + avgZ * 85, 40, 240))
        col_v = EMERALD * (1.0 - frac) + SIGNAL * frac
        col = (int(col_v[0]), int(col_v[1]), int(col_v[2]), alpha)
        for i in range(len(pts) - 1):
            draw.line([(pts[i][0], pts[i][1]), (pts[i+1][0], pts[i+1][1])], fill=col, width=2)

    # 12 Principal icosahedral nodal points
    nodes = [
        [0, 1, phi], [0, 1, -phi], [0, -1, phi], [0, -1, -phi],
        [1, phi, 0], [1, -phi, 0], [-1, phi, 0], [-1, -phi, 0],
        [phi, 0, 1], [phi, 0, -1], [-phi, 0, 1], [-phi, 0, -1]
    ]
    for pt in nodes:
        length = math.sqrt(sum(c*c for c in pt))
        normSc = 1.25 / (length or 1)
        p = project(pt[0] * normSc, pt[1] * normSc, pt[2] * normSc)
        draw.ellipse([p[0] - 5, p[1] - 5, p[0] + 5, p[1] + 5], fill=(255, 107, 53, 240), outline=(255, 240, 225, 255))

    im.save(os.path.join(PREVIEWS_DIR, "barth-sextic-surface-3d.jpg"), quality=94)
    print("barth-sextic-surface-3d.jpg saved")

def render_buchla_complex_oscillator():
    """Buchla complex oscillator triangle core and multi-stage wavefolding."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    for gy in range(80, H, 80):
        draw.line([(0, gy), (W, gy)], fill=(32, 38, 70, 100), width=1)
    for gx in range(90, W, 90):
        draw.line([(gx, 0), (gx, H)], fill=(32, 38, 70, 100), width=1)

    cy = H * 0.45

    # Folded waveform
    pts = []
    for i in range(W):
        frac = i / W
        phase = (frac * 6.0) % 1.0
        # Triangle core
        tri = 4.0 * abs(phase - 0.5) - 1.0
        # Audio-rate FM modulation
        mod = math.sin(frac * 12.0 * math.pi) * 0.4
        tri_fm = tri + mod
        # Multi-stage wavefolding: sin-based fold
        fold = math.sin(tri_fm * 2.8)
        u = fold * 1.5
        sat = u / (1.0 + abs(u) * 0.3)
        env = math.exp(-frac * 1.5)
        sig = sat * env * 160.0
        pts.append((i, cy + sig))

    for i in range(len(pts) - 1):
        draw.line([pts[i], pts[i+1]], fill=(167, 177, 255, 120), width=6)
        draw.line([pts[i], pts[i+1]], fill=(255, 107, 53, 200), width=3)
        draw.line([pts[i], pts[i+1]], fill=(255, 255, 255, 240), width=1)

    # Harmonic fold bars
    for idx in range(8):
        fx = int(105 + idx * 95)
        amp = math.exp(-((idx - 2.5) / 2.2)**2) * 145
        draw.line([(fx, cy + 195), (fx, cy + 195 - amp)], fill=(78, 242, 184, 210), width=6)
        draw.ellipse([fx - 5, cy + 195 - amp - 5, fx + 5, cy + 195 - amp + 5], fill=(255, 245, 230, 255))

    im.save(os.path.join(PREVIEWS_DIR, "buchla-complex-oscillator-synth.jpg"), quality=94)
    print("buchla-complex-oscillator-synth.jpg saved")

    im_webp = im.resize((960, 600), Image.Resampling.LANCZOS)
    im_webp.save(os.path.join(TEMPLATES_DIR, "buchla-complex-oscillator-synth.webp"), "WEBP", quality=90)
    print("buchla-complex-oscillator-synth.webp saved")

def render_string_resonator_modal():
    """String resonator modal bank inharmonic partials and hammer strike."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    for gy in range(80, H, 80):
        draw.line([(0, gy), (W, gy)], fill=(32, 38, 70, 100), width=1)
    for gx in range(90, W, 90):
        draw.line([(gx, 0), (gx, H)], fill=(32, 38, 70, 100), width=1)

    cy = H * 0.45

    # Coupled inharmonic modal sum waveform
    pts = []
    for i in range(W):
        frac = i / W
        phase1 = frac * 8.0 * math.pi
        phase2 = phase1 * 2.76
        phase3 = phase1 * 5.40
        strike = math.exp(-frac * 12.0) * math.sin(frac * 40.0 * math.pi) * 0.8
        modal = (math.sin(phase1) * 0.5 + math.sin(phase2) * 0.3 + math.sin(phase3) * 0.2)
        env = math.exp(-frac * 1.8)
        sig = (modal + strike) * env * 155.0
        pts.append((i, cy + sig))

    for i in range(len(pts) - 1):
        draw.line([pts[i], pts[i+1]], fill=(167, 177, 255, 120), width=6)
        draw.line([pts[i], pts[i+1]], fill=(78, 242, 184, 190), width=3)
        draw.line([pts[i], pts[i+1]], fill=(255, 255, 255, 240), width=1)

    # Inharmonic modal spectrum bars
    ratios = [1.0, 2.76, 5.40, 8.93, 13.34, 18.64, 24.8]
    for idx, r in enumerate(ratios):
        fx = int(90 + idx * 105)
        amp = (1.0 / (idx * 0.45 + 1.0)) * 140
        draw.line([(fx, cy + 195), (fx, cy + 195 - amp)], fill=(255, 107, 53, 210), width=6)
        draw.ellipse([fx - 5, cy + 195 - amp - 5, fx + 5, cy + 195 - amp + 5], fill=(255, 240, 220, 255))

    im.save(os.path.join(PREVIEWS_DIR, "string-resonator-modal-synth.jpg"), quality=94)
    print("string-resonator-modal-synth.jpg saved")

    im_webp = im.resize((960, 600), Image.Resampling.LANCZOS)
    im_webp.save(os.path.join(TEMPLATES_DIR, "string-resonator-modal-synth.webp"), "WEBP", quality=90)
    print("string-resonator-modal-synth.webp saved")

def render_hilbert_frequency_shifter():
    """Hilbert quadrature frequency shifter analytic single-sideband curve."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    for gy in range(80, H, 80):
        draw.line([(0, gy), (W, gy)], fill=(32, 38, 70, 100), width=1)
    for gx in range(90, W, 90):
        draw.line([(gx, 0), (gx, H)], fill=(32, 38, 70, 100), width=1)

    cy = H * 0.48

    # In-phase (I) and Quadrature (Q) shifted waveforms
    pts_i = []
    pts_q = []
    pts_ssb = []

    for i in range(W):
        frac = i / W
        phase = frac * 10.0 * math.pi
        carrier = frac * 14.0 * math.pi
        sig_i = math.sin(phase) * 60.0
        sig_q = math.cos(phase) * 60.0
        # Single-sideband sum: I*cos(c) - Q*sin(c)
        ssb = (math.sin(phase) * math.cos(carrier) - math.cos(phase) * math.sin(carrier)) * 120.0

        pts_i.append((i, cy - 70 + sig_i))
        pts_q.append((i, cy - 70 + sig_q))
        pts_ssb.append((i, cy + 70 + ssb))

    for i in range(len(pts_i) - 1):
        draw.line([pts_i[i], pts_i[i+1]], fill=(167, 177, 255, 140), width=2)
        draw.line([pts_q[i], pts_q[i+1]], fill=(78, 242, 184, 140), width=2)
        draw.line([pts_ssb[i], pts_ssb[i+1]], fill=(255, 107, 53, 210), width=3)

    im.save(os.path.join(PREVIEWS_DIR, "hilbert-frequency-shifter-fx.jpg"), quality=94)
    print("hilbert-frequency-shifter-fx.jpg saved")

    im_webp = im.resize((960, 600), Image.Resampling.LANCZOS)
    im_webp.save(os.path.join(TEMPLATES_DIR, "hilbert-frequency-shifter-fx.webp"), "WEBP", quality=90)
    print("hilbert-frequency-shifter-fx.webp saved")

def render_granular_pitch_shifter_delay():
    """Granular pitch shifter micro-loop Hann windows and diffuse feedback."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    for gy in range(80, H, 80):
        draw.line([(0, gy), (W, gy)], fill=(32, 38, 70, 100), width=1)
    for gx in range(90, W, 90):
        draw.line([(gx, 0), (gx, H)], fill=(32, 38, 70, 100), width=1)

    cy = H * 0.46

    # Multiple overlapping grain window lobes
    num_grains = 7
    for g in range(num_grains):
        gx_center = 120 + g * 110
        grain_w = 160
        pts_grain = []
        for x in range(gx_center - grain_w // 2, gx_center + grain_w // 2):
            if 0 <= x < W:
                frac = (x - (gx_center - grain_w // 2)) / grain_w
                hann = 0.5 * (1.0 - math.cos(frac * 2.0 * math.pi))
                carrier = math.sin(frac * 16.0 * math.pi * (1.0 + g * 0.15))
                y = cy - 40 - hann * carrier * 85.0
                pts_grain.append((x, y))
        for i in range(len(pts_grain) - 1):
            col = (255, 107, 53, 190) if g % 2 == 0 else (78, 242, 184, 190)
            draw.line([pts_grain[i], pts_grain[i+1]], fill=col, width=2)

    # Diffuse feedback echo line below
    pts_tail = []
    for i in range(W):
        frac = i / W
        sig = math.sin(frac * 28.0 * math.pi) * math.exp(-frac * 1.2) * 65.0
        pts_tail.append((i, cy + 120 + sig))
    for i in range(len(pts_tail) - 1):
        draw.line([pts_tail[i], pts_tail[i+1]], fill=(167, 177, 255, 180), width=2)

    im.save(os.path.join(PREVIEWS_DIR, "granular-pitch-shifter-delay-fx.jpg"), quality=94)
    print("granular-pitch-shifter-delay-fx.jpg saved")

    im_webp = im.resize((960, 600), Image.Resampling.LANCZOS)
    im_webp.save(os.path.join(TEMPLATES_DIR, "granular-pitch-shifter-delay-fx.webp"), "WEBP", quality=90)
    print("granular-pitch-shifter-delay-fx.webp saved")

def render_superconducting_magnetic_vortex():
    """Abrikosov flux vortex lattice in type-II superconductor."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    aspect = W / H
    scale = 2.2

    y_indices, x_indices = np.indices((H, W))
    p_x = ((x_indices / W) - 0.5) * aspect * scale
    p_y = ((y_indices / H) - 0.5) * scale

    r = np.sqrt(p_x**2 + p_y**2)
    ang = np.arctan2(p_y, p_x)

    # Hexagonal vortex lattice
    lat_scale = 9.0
    u1 = p_x * lat_scale
    u2 = (p_x * 0.5 + p_y * 0.866) * lat_scale
    g1 = (u1 + 0.5) % 1.0 - 0.5
    g2 = (u2 + 0.5) % 1.0 - 0.5
    core_dist = np.sqrt(g1**2 + g2**2)

    vortex_core = np.exp(-core_dist * 14.0) * 2.2
    phase_ripple = np.sin(6.0 * ang + r * 12.0)
    shielding = np.exp(-r * 2.4) * (0.8 + 0.4 * phase_ripple)

    fade = np.clip((2.3 - r) / 1.3, 0.0, 1.0)

    for y in range(H):
        for x in range(W):
            vc = vortex_core[y, x]
            sh = shielding[y, x]
            rad = r[y, x]

            color_vortex = EMERALD * (0.5 + 0.5 * math.sin(rad * 7.0)) + SIGNAL * (0.5 - 0.5 * math.sin(rad * 7.0))
            col = GROUND + color_vortex * sh * 1.4 + (EMBER * 0.7 + WHITE * 0.3) * vc
            col = GROUND * (1.0 - fade[y, x]) + col * fade[y, x]
            out[y, x] = np.clip(col, 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "superconducting-magnetic-vortex.jpg"), quality=94)
    print("superconducting-magnetic-vortex.jpg saved")

def render_gravitational_caustic_microlens():
    """Binary gravitational lens astroid caustics and Einstein ring."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    aspect = W / H
    scale = 2.2

    y_indices, x_indices = np.indices((H, W))
    p_x = ((x_indices / W) - 0.5) * aspect * scale
    p_y = ((y_indices / H) - 0.5) * scale

    r = np.sqrt(p_x**2 + p_y**2)

    # Binary lens critical astroid caustic: (x/a)^(2/3) + (y/b)^(2/3) = 1
    astroid = (np.abs(p_x / 0.7)**(2.0/3.0) + np.abs(p_y / 0.5)**(2.0/3.0))
    caustic_dist = np.abs(astroid - 1.0)
    caustic_glow = np.exp(-caustic_dist * 8.0) * 1.8

    # Einstein ring flare
    ring_dist = np.abs(r - 0.95)
    ring_flare = np.exp(-ring_dist * 18.0) * 1.5

    # Cusp point flares (4 cusps of astroid)
    cusp_flare = (np.exp(-np.sqrt((np.abs(p_x) - 0.7)**2 + p_y**2) * 12.0) +
                  np.exp(-np.sqrt(p_x**2 + (np.abs(p_y) - 0.5)**2) * 12.0)) * 2.0

    fade = np.clip((2.3 - r) / 1.3, 0.0, 1.0)

    for y in range(H):
        for x in range(W):
            cg = caustic_glow[y, x]
            rf = ring_flare[y, x]
            cf = cusp_flare[y, x]

            color_caustic = SIGNAL * cg + EMERALD * rf
            col = GROUND + color_caustic * 1.3 + (EMBER * 0.8 + WHITE * 0.2) * cf
            col = GROUND * (1.0 - fade[y, x]) + col * fade[y, x]
            out[y, x] = np.clip(col, 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "gravitational-caustic-microlens.jpg"), quality=94)
    print("gravitational-caustic-microlens.jpg saved")

def main():
    render_ulam_spiral_primes()
    render_mandelbrot_buddhabrot_path()
    render_cross_cap_surface()
    render_barth_sextic_surface()
    render_buchla_complex_oscillator()
    render_string_resonator_modal()
    render_hilbert_frequency_shifter()
    render_granular_pitch_shifter_delay()
    render_superconducting_magnetic_vortex()
    render_gravitational_caustic_microlens()
    print("All Pack 10 preview images rendered successfully!")

if __name__ == "__main__":
    main()
