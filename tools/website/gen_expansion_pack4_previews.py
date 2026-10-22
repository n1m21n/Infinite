#!/usr/bin/env python3
"""Generate high-fidelity preview images for the 10 new creative library items (Pack 4).
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

def render_dadras():
    """Dadras attractor 5-parameter chaotic flow."""
    acc = np.zeros((H, W), dtype=np.float32)
    cx, cy = W / 2, H / 2
    k = min(W, H) * 0.038
    a, b, c, d, e = 3.0, 2.7, 1.7, 2.0, 9.0
    x, y, z = 0.8, -0.6, 0.4
    dt = 0.0016

    def derivs(px, py, pz):
        return py - a * px + b * py * pz, c * py - px * pz + pz, d * px * py - e * pz

    for i in range(1200000):
        k1x, k1y, k1z = derivs(x, y, z)
        k2x, k2y, k2z = derivs(x + 0.5*dt*k1x, y + 0.5*dt*k1y, z + 0.5*dt*k1z)
        k3x, k3y, k3z = derivs(x + 0.5*dt*k2x, y + 0.5*dt*k2y, z + 0.5*dt*k2z)
        k4x, k4y, k4z = derivs(x + dt*k3x, y + dt*k3y, z + dt*k3z)

        x += (dt/6.0) * (k1x + 2*k2x + 2*k3x + k4x)
        y += (dt/6.0) * (k1y + 2*k2y + 2*k3y + k4y)
        z += (dt/6.0) * (k1z + 2*k2z + 2*k3z + k4z)

        if i > 2000:
            rot = 0.65
            rx = x * math.cos(rot) - y * math.sin(rot)
            ry = (x * math.sin(rot) + y * math.cos(rot)) * 0.65 + z * 0.68
            px = int(cx + rx * k)
            py = int(cy - (ry - 4.5) * k)
            if 0 <= px < W and 0 <= py < H:
                acc[py, px] += 1.0

    acc = np.log1p(acc * 1.1)
    norm = np.clip(acc / np.percentile(acc[acc > 0], 99.6), 0.0, 1.0)
    out = np.zeros((H, W, 3), dtype=np.float32)
    for c_idx in range(3):
        out[:, :, c_idx] = GROUND[c_idx]

    for y_idx in range(H):
        for x_idx in range(W):
            v = norm[y_idx, x_idx]
            if v > 0:
                frac = v**0.8
                mix_col = EMERALD * (1.0 - frac) + EMBER * frac
                out[y_idx, x_idx] = np.clip(GROUND * (1.0 - v) + mix_col * v * 1.8 + WHITE * (v**3 * 1.4), 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "dadras-attractor.jpg"), quality=94)
    print("dadras-attractor.jpg saved")

def render_fourier_epicycles():
    """Fourier nested epicyclic rosettes."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2
    scale = min(W, H) * 0.22

    harmonics = 4.0
    ratio = 2.6
    decay = 0.65
    steps = 6000
    totalRot = math.pi * 2 * harmonics

    pts = []
    for i in range(steps + 1):
        theta = (i / steps) * totalRot
        x = math.cos(theta)
        y = math.sin(theta)

        r = decay
        freq = ratio
        x += math.cos(theta * freq) * r
        y += math.sin(theta * freq) * r

        r *= decay
        freq *= ratio
        x += math.cos(theta * freq) * r
        y += math.sin(theta * freq) * r

        r *= decay
        freq *= ratio
        x += math.cos(theta * freq) * r
        y += math.sin(theta * freq) * r

        pts.append((cx + x * scale, cy - y * scale))

    for i in range(1, len(pts)):
        frac = i / len(pts)
        r_c = int(EMERALD[0] * (1.0 - frac) + EMBER[0] * frac)
        g_c = int(EMERALD[1] * (1.0 - frac) + EMBER[1] * frac)
        b_c = int(EMERALD[2] * (1.0 - frac) + EMBER[2] * frac)
        draw.line([pts[i-1], pts[i]], fill=(r_c, g_c, b_c, 210), width=2)

    im = im.filter(ImageFilter.SMOOTH)
    im.save(os.path.join(PREVIEWS_DIR, "fourier-epicycles.jpg"), quality=94)
    print("fourier-epicycles.jpg saved")

def render_calabi_yau():
    """Calabi-Yau 3D manifold cross-section projection."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2
    scale = min(W, H) * 0.24

    layers = 44
    steps = 56
    folds = 5.0
    rotX = 0.62
    rotY = 0.85

    cosX, sinX = math.cos(rotX), math.sin(rotX)
    cosY, sinY = math.cos(rotY), math.sin(rotY)

    for i in range(layers):
        uFrac = i / layers
        u = uFrac * math.pi * 2.0
        alpha = (2.0 * math.pi * uFrac) / folds
        pts = []

        for j in range(steps + 1):
            vFrac = j / steps
            v = -0.5 * math.pi + vFrac * math.pi

            cosV = math.cos(v)
            sinV = math.sin(v)
            r = 1.0 + 0.32 * math.cos(folds * u)

            x0 = r * cosV * math.cos(u + alpha)
            y0 = r * cosV * math.sin(u + alpha)
            z0 = 0.65 * sinV + 0.28 * math.sin(folds * v)

            # 3D rotation
            x1 = x0 * cosY - z0 * sinY
            z1 = x0 * sinY + z0 * cosY
            y1 = y0 * cosX - z1 * sinX
            z2 = y0 * sinX + z1 * cosX

            persp = 1.0 / (1.0 + z2 * 0.22)
            pts.append((cx + x1 * scale * persp, cy - y1 * scale * persp))

        frac = i / layers
        r_c = int(SIGNAL[0] * (1.0 - frac) + EMERALD[0] * frac)
        g_c = int(SIGNAL[1] * (1.0 - frac) + EMERALD[1] * frac)
        b_c = int(SIGNAL[2] * (1.0 - frac) + EMERALD[2] * frac)

        for j in range(1, len(pts)):
            draw.line([pts[j-1], pts[j]], fill=(r_c, g_c, b_c, 190), width=2)

    im = im.filter(ImageFilter.SMOOTH)
    im.save(os.path.join(PREVIEWS_DIR, "calabi-yau-3d.jpg"), quality=94)
    print("calabi-yau-3d.jpg saved")

def render_richmond_surface():
    """Herbert Richmond minimal surface with monkey saddle branching."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2
    scale = min(W, H) * 0.22

    m = 3.0
    layers = 40
    steps = 56
    rotX, rotY = 0.72, 0.75
    cosX, sinX = math.cos(rotX), math.sin(rotX)
    cosY, sinY = math.cos(rotY), math.sin(rotY)

    for i in range(layers):
        uFrac = i / (layers - 1)
        u = 0.28 + uFrac * 1.05
        pts = []

        for j in range(steps + 1):
            vFrac = j / steps
            v = -math.pi + vFrac * math.pi * 2.0

            p1 = 2.0 * m - 1.0
            uInv = 1.0 / (2.0 * u)
            uPowP = (u ** p1) / (2.0 * p1)

            x0 = -uInv * math.cos(v) - uPowP * math.cos(p1 * v)
            y0 = -uInv * math.sin(v) + uPowP * math.sin(p1 * v)
            z0 = (1.0 / m) * (u ** m) * math.cos(m * v)

            x1 = x0 * cosY - z0 * sinY
            z1 = x0 * sinY + z0 * cosY
            y1 = y0 * cosX - z1 * sinX
            z2 = y0 * sinX + z1 * cosX

            persp = 1.0 / (1.0 + z2 * 0.2)
            pts.append((cx + x1 * scale * persp, cy - y1 * scale * persp))

        frac = i / layers
        r_c = int(EMBER[0] * frac + SIGNAL[0] * (1.0 - frac))
        g_c = int(EMBER[1] * frac + SIGNAL[1] * (1.0 - frac))
        b_c = int(EMBER[2] * frac + SIGNAL[2] * (1.0 - frac))

        for j in range(1, len(pts)):
            draw.line([pts[j-1], pts[j]], fill=(r_c, g_c, b_c, 195), width=2)

    im = im.filter(ImageFilter.SMOOTH)
    im.save(os.path.join(PREVIEWS_DIR, "richmond-surface-3d.jpg"), quality=94)
    print("richmond-surface-3d.jpg saved")

def render_pulsar_train():
    """Pulsar Train Synth microsound particle burst waveform."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")
    cx, cy = W / 2, H / 2

    # Draw grid lines
    for gy in range(80, H - 80, 44):
        draw.line([(60, gy), (W - 60, gy)], fill=(32, 38, 63, 140), width=1)
    for gx in range(60, W - 60, 60):
        draw.line([(gx, 80), (gx, H - 80)], fill=(32, 38, 63, 140), width=1)

    pts = []
    duty = 0.45
    formant = 3.6
    nPeriods = 6
    totalSteps = 1200

    for i in range(totalSteps):
        xFrac = i / totalSteps
        phase = (xFrac * nPeriods) % 1.0
        env = math.sin(xFrac * math.pi)

        if phase < duty:
            pFrac = phase / duty
            hann = 0.5 - 0.5 * math.cos(math.pi * 2 * pFrac)
            osc = math.sin(math.pi * 2 * pFrac * formant)
            wave = osc * hann
        else:
            wave = 0.0

        sub = 0.3 * math.sin(math.pi * 2 * xFrac * nPeriods * 0.5)
        yVal = (wave + sub) * env * 140.0
        pts.append((60 + xFrac * (W - 120), cy - yVal))

    for i in range(1, len(pts)):
        frac = i / len(pts)
        r_c = int(SIGNAL[0] * (1.0 - frac) + EMERALD[0] * frac)
        g_c = int(SIGNAL[1] * (1.0 - frac) + EMERALD[1] * frac)
        b_c = int(SIGNAL[2] * (1.0 - frac) + EMERALD[2] * frac)
        draw.line([pts[i-1], pts[i]], fill=(r_c, g_c, b_c, 230), width=3)

    im = im.filter(ImageFilter.SMOOTH)
    im.save(os.path.join(PREVIEWS_DIR, "pulsar-train-synth.jpg"), quality=94)
    print("pulsar-train-synth.jpg saved")

def render_chaos_duffing():
    """Chaos Duffing Synth phase space portrait."""
    acc = np.zeros((H, W), dtype=np.float32)
    cx, cy = W / 2, H / 2
    scale = min(W, H) * 0.16

    pos = 0.1
    vel = 0.0
    drive = 1.3
    stiffness = 1.0
    damping = 0.22
    omega = 1.2
    dt = 0.005

    for i in range(400000):
        t = i * dt
        accel = pos - stiffness * (pos ** 3) - damping * vel + drive * math.cos(omega * t)
        vel += accel * dt
        pos += vel * dt

        if i > 2000:
            px = int(cx + pos * scale)
            py = int(cy - vel * scale * 0.8)
            if 0 <= px < W and 0 <= py < H:
                acc[py, px] += 1.0

    acc = np.log1p(acc * 1.5)
    norm = np.clip(acc / np.percentile(acc[acc > 0], 99.6), 0.0, 1.0)
    out = np.zeros((H, W, 3), dtype=np.float32)
    for c_idx in range(3):
        out[:, :, c_idx] = GROUND[c_idx]

    for y_idx in range(H):
        for x_idx in range(W):
            v = norm[y_idx, x_idx]
            if v > 0:
                frac = v**0.75
                mix_col = EMBER * frac + SIGNAL * (1.0 - frac)
                out[y_idx, x_idx] = np.clip(GROUND * (1.0 - v) + mix_col * v * 1.8 + WHITE * (v**3 * 1.5), 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "chaos-duffing-synth.jpg"), quality=94)
    print("chaos-duffing-synth.jpg saved")

def render_rotary_speaker():
    """Rotary Speaker Doppler FX acoustic interference waves."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    y, x = np.ogrid[:H, :W]
    dx = (x - W / 2) / (min(W, H) * 0.5)
    dy = (y - H / 2) / (min(W, H) * 0.5)
    r = np.sqrt(dx**2 + dy**2)
    a = np.arctan2(dy, dx)

    # Dual rotor Doppler wave interference
    hornWave = np.sin(r * 28.0 - a * 2.0) * np.exp(-r * 0.7)
    drumWave = np.sin(r * 14.0 + a * 1.0) * np.exp(-r * 0.5)
    doppler = (hornWave * 0.6 + drumWave * 0.4)
    core = np.clip(1.0 - r * 1.4, 0.0, 1.0)

    for y_idx in range(H):
        for x_idx in range(W):
            v = doppler[y_idx, x_idx]
            rad = r[y_idx, x_idx]
            col = GROUND * (1.0 - v * 0.4) + EMERALD * (v * 0.5 + 0.5) * 0.6 + EMBER * (max(0.0, v)**3.0) * 1.4
            vig = np.clip((1.6 - rad) / 0.65, 0, 1)
            out[y_idx, x_idx] = np.clip(col * vig + GROUND * (1.0 - vig), 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "rotary-speaker-doppler-fx.jpg"), quality=94)
    print("rotary-speaker-doppler-fx.jpg saved")

def render_spectral_gate():
    """Spectral Gate Stutter FX multiband transient time-frequency slice."""
    im = Image.new("RGB", (W, H), (21, 25, 48))
    draw = ImageDraw.Draw(im, "RGBA")

    # Draw spectrogram slices and stutter gate bars
    nBands = 36
    nSlices = 48
    bw = (W - 120) / nSlices
    bh = (H - 120) / nBands

    for b in range(nBands):
        y0 = 60 + b * bh
        for s in range(nSlices):
            x0 = 60 + s * bw
            gate = 1.0 if (s % 4 < 2) else 0.2
            freqFrac = b / nBands
            intensity = (math.sin(s * 0.4 + b * 0.3) * 0.5 + 0.5) * gate

            if intensity > 0.1:
                r_c = int(EMBER[0] * freqFrac + SIGNAL[0] * (1.0 - freqFrac))
                g_c = int(EMBER[1] * freqFrac + SIGNAL[1] * (1.0 - freqFrac))
                b_c = int(EMBER[2] * freqFrac + SIGNAL[2] * (1.0 - freqFrac))
                alpha = int(intensity * 210)
                draw.rectangle([(x0 + 1, y0 + 1), (x0 + bw - 2, y0 + bh - 2)], fill=(r_c, g_c, b_c, alpha))

    im = im.filter(ImageFilter.SMOOTH)
    im.save(os.path.join(PREVIEWS_DIR, "spectral-gate-stutter-fx.jpg"), quality=94)
    print("spectral-gate-stutter-fx.jpg saved")

def render_crystal_polytope():
    """Crystal Polytope 4D regular polytope projection."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    y, x = np.ogrid[:H, :W]
    dx = (x - W / 2) / (min(W, H) * 0.5)
    dy = (y - H / 2) / (min(W, H) * 0.5)
    r = np.sqrt(dx**2 + dy**2)
    a = np.arctan2(dy, dx)

    facets = 8.0
    sec = 2.0 * math.pi / facets
    polyA = np.mod(a + 0.4, sec) - sec * 0.5
    dPoly = r * np.cos(polyA)

    shell1 = np.abs(np.sin(dPoly * 18.0 - 1.2))
    shell2 = np.abs(np.cos(r * 24.0))
    lattice = np.clip((0.14 - np.minimum(shell1, shell2)) / 0.12, 0.0, 1.0)

    core = np.clip((0.45 - r) / 0.45, 0.0, 1.0)
    facetsGlow = np.clip((0.35 - np.abs(dPoly - 0.22)) / 0.35, 0.0, 1.0)

    for y_idx in range(H):
        for x_idx in range(W):
            lat = lattice[y_idx, x_idx]
            rad = r[y_idx, x_idx]
            cr = core[y_idx, x_idx]
            fg = facetsGlow[y_idx, x_idx]

            polyCol = SIGNAL * 0.7 + EMERALD * 0.3 + EMBER * lat * 0.9
            glow = polyCol * (lat * 1.5 + fg * 0.9 + cr * 0.6)
            vig = np.clip((1.65 - rad) / 0.7, 0, 1)
            out[y_idx, x_idx] = np.clip((GROUND * (1.0 - cr * 0.5) + glow) * vig + GROUND * (1.0 - vig), 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "crystal-polytope.jpg"), quality=94)
    print("crystal-polytope.jpg saved")

def render_voronoi_bioluminescence():
    """Voronoi Bioluminescence organic living cellular foam."""
    out = np.zeros((H, W, 3), dtype=np.float32)
    y, x = np.ogrid[:H, :W]
    dx = (x - W / 2) / (min(W, H) * 0.5)
    dy = (y - H / 2) / (min(W, H) * 0.5)
    r = np.sqrt(dx**2 + dy**2)

    # Synthesize cellular Voronoi tissue pattern
    gp_x = dx * 6.0
    gp_y = dy * 6.0
    cell_wave = np.sin(gp_x * 2.0) * np.cos(gp_y * 2.0)
    rings = np.sin(r * 18.0)

    core = np.clip((0.4 - r) / 0.4, 0.0, 1.0)
    membrane = np.clip((np.abs(cell_wave) - 0.2) / 0.5, 0.0, 1.0)
    pulses = np.clip(1.0 - np.abs(rings - 0.3) / 0.4, 0.0, 1.0)

    for y_idx in range(H):
        for x_idx in range(W):
            cr = core[y_idx, x_idx]
            mb = membrane[y_idx, x_idx]
            pl = pulses[y_idx, x_idx]
            rad = r[y_idx, x_idx]

            bioCol = EMERALD * 0.7 + SIGNAL * 0.3 + EMBER * pl * 0.8
            pattern = bioCol * (cr * 1.5 + pl * 1.1) * mb + EMERALD * (1.0 - mb) * 0.5
            vig = np.clip((1.65 - rad) / 0.7, 0, 1)
            out[y_idx, x_idx] = np.clip((GROUND * (1.0 - cr * 0.4) + pattern) * vig + GROUND * (1.0 - vig), 0, 255)

    Image.fromarray(out.astype(np.uint8)).save(os.path.join(PREVIEWS_DIR, "voronoi-bioluminescence.jpg"), quality=94)
    print("voronoi-bioluminescence.jpg saved")

if __name__ == "__main__":
    render_dadras()
    render_fourier_epicycles()
    render_calabi_yau()
    render_richmond_surface()
    render_pulsar_train()
    render_chaos_duffing()
    render_rotary_speaker()
    render_spectral_gate()
    render_crystal_polytope()
    render_voronoi_bioluminescence()
    print("All 10 preview images successfully generated!")

    # Run library thumbs generator
    thumbs_script = os.path.join(ROOT, "tools", "website", "gen_library_thumbs.py")
    if os.path.exists(thumbs_script):
        subprocess.run([sys.executable, thumbs_script], check=True)
