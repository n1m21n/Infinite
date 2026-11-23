#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 47.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: op_art_riley_vasarely -> riley-optical-wave-current, vasarely-plastic-sphere-lattice)
2. Sketch 3D (Theme: muqarnas_corbel_vault -> muqarnas-abencerrajes-star-dome-3d, muqarnas-isfahan-peacock-vault-3d)
3. Field Synth (Theme: crystallophone_glass_harmonica -> crystallophone-glass-harmonica-synth, binaural-celesta-bell-harp-synth)
4. Field Effects (Theme: nagra_tape_dispersion -> nagra-tape-flutter-loop-fx, schroeder-allpass-prism-fx)
5. Field Pixel & Embeds (Theme: ando_concrete_slit -> ando-concrete-light-slit, cathedral-rose-caustic-prism)
"""
import os
import sys
import json
import subprocess
import zipfile
from PIL import Image, ImageStat

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN = os.path.join(ROOT, "build", "Infinite.app", "Contents", "MacOS", "Infinite")
if not os.path.exists(BIN):
    BIN = os.path.expanduser("~/.local/bin/Infinite")
    CMD = [BIN]
else:
    CMD = [BIN, "--cli"]

LIB_DIR = os.path.join(ROOT, "website", "assets", "library")
SKETCH_DIR = os.path.join(LIB_DIR, "sketch")
PIXEL_DIR = os.path.join(LIB_DIR, "pixel")
PREVIEWS_DIR = os.path.join(LIB_DIR, "previews")
THUMBS_DIR = os.path.join(LIB_DIR, "thumbs")
TEMPLATES_DIR = os.path.join(ROOT, "website", "assets", "templates")
EMBEDS_DIR = os.path.join(ROOT, "website", "embeds")

TMP_DIR = "/tmp/pack47_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview


# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: op_art_riley_vasarely)
#    Palette: #111114 (deep ink), #F4EFE6 (warm paper),
#             #D97A2B (burnt ochre), #1E4B8A (cobalt)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "riley-optical-wave-current",
        "title": "Riley Optical Wave Current",
        "description": "Bridget Riley inspired Op-Art optical wave interference with phase-shifted undulating parallel ribbons, crisp ink contrast, and ochre-cobalt harmonics.",
        "tags": ["op-art", "riley", "interference", "waves", "moire", "procedural", "sketch"],
        "code": """// Riley Optical Wave Current - Infinite Library (Sketch 2D)
param("ribbons", 36, 18, 64);
param("crest_amp", 0.085, 0.02, 0.16);
param("phase_drift", 0.35, 0.0, 1.2);
param("accent_mix", 0.28, 0.0, 0.8);

function draw(t) {
  // Warm archival rag paper (#F4EFE6)
  background(0.957, 0.937, 0.902);
  const w = width;
  const h = height;
  const margin = w * 0.09;
  const gw = w - margin * 2.0;
  const gh = h - margin * 2.0;

  const nR = floor(ribbons);
  const stepX = gw / nR;
  const segs = 72;

  // Fine drafting border
  noFill();
  stroke(0.067, 0.067, 0.078, 0.35);
  strokeWeight(1.2);
  rect(margin, margin, gw, gh);

  // Undulating parallel Op-Art wave bands
  for (let i = 0; i < nR; i++) {
    const u = i / max(1, nR - 1);
    const baseX = margin + (i + 0.5) * stepX;

    // Alternating ink and chromatic harmonic ribbons
    if (i % 9 === 4 && accent_mix > 0.1) {
      stroke(0.851, 0.478, 0.169, 0.94); // Burnt ochre (#D97A2B)
    } else if (i % 9 === 7 && accent_mix > 0.15) {
      stroke(0.118, 0.294, 0.541, 0.92); // Cobalt (#1E4B8A)
    } else {
      stroke(0.067, 0.067, 0.078, 0.96); // Deep ink (#111114)
    }

    const wMod = 0.5 + 0.5 * sin(u * PI * 3.0 + t * phase_drift * 0.6);
    strokeWeight(max(1.4, stepX * (0.28 + 0.34 * wMod)));

    beginShape();
    for (let s = 0; s <= segs; s++) {
      const v = s / segs;
      const y = margin + v * gh;
      // Compression wave creates the physiological Op-Art shimmer
      const env = sin(v * PI);
      const wave1 = sin(v * TAU * 2.2 - u * 4.5 + t * phase_drift);
      const wave2 = 0.42 * sin(v * TAU * 4.4 + u * 6.0 - t * phase_drift * 0.7);
      const dx = (wave1 + wave2) * w * crest_amp * env;
      vertex(baseX + dx, y);
    }
    endShape();
  }

  // Subtle horizontal counter-ticks along outer frame
  stroke(0.067, 0.067, 0.078, 0.45);
  strokeWeight(1.0);
  for (let k = 0; k <= 12; k++) {
    const ty = margin + (k / 12.0) * gh;
    line(margin - 10, ty, margin - 3, ty);
    line(margin + gw + 3, ty, margin + gw + 10, ty);
  }
}
"""
    },
    {
        "slug": "vasarely-plastic-sphere-lattice",
        "title": "Vasarely Plastic Sphere Lattice",
        "description": "Victor Vasarely Vega inspired spherical bulge illusion on a warped geometric lattice with disciplined cobalt, ochre, ivory, and charcoal tiles.",
        "tags": ["op-art", "vasarely", "lattice", "sphere", "geometric", "procedural", "sketch"],
        "code": """// Vasarely Plastic Sphere Lattice - Infinite Library (Sketch 2D)
param("cells", 18, 10, 28);
param("bulge", 0.62, 0.15, 0.95);
param("pulse_speed", 0.30, 0.0, 1.0);
param("accent_shift", 0.45, 0.0, 1.0);

function warpPt(nx, ny, t) {
  const r = sqrt(nx * nx + ny * ny);
  const rad = 0.88;
  if (r < rad && r > 0.0001) {
    const k = r / rad;
    const bulgeAmt = bulge * (0.85 + 0.15 * sin(t * pulse_speed * 2.0));
    const scale = 1.0 + bulgeAmt * (1.0 - k * k) * 0.58;
    return [nx * scale, ny * scale, 1.0 - k * k];
  }
  return [nx, ny, 0.0];
}

function draw(t) {
  // Warm slate-ivory architectural frame (#1D222B)
  background(0.115, 0.133, 0.169);
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const span = min(w, h) * 0.46;

  const n = floor(cells);
  const step = 2.0 / n;

  for (let iy = 0; iy < n; iy++) {
    for (let ix = 0; ix < n; ix++) {
      const x0 = -1.0 + ix * step;
      const y0 = -1.0 + iy * step;
      const x1 = x0 + step;
      const y1 = y0 + step;

      const p00 = warpPt(x0, y0, t);
      const p10 = warpPt(x1, y0, t);
      const p11 = warpPt(x1, y1, t);
      const p01 = warpPt(x0, y1, t);

      const mx = (x0 + x1) * 0.5;
      const my = (y0 + y1) * 0.5;
      const distC = sqrt(mx * mx + my * my);
      const dome = max(0.0, 1.0 - distC / 1.15);
      const parity = (ix + iy) % 2;

      if (parity === 0) {
        // Warm ivory to burnt ochre lift on the spherical bulge
        const luma = 0.52 + 0.46 * pow(dome, 0.7);
        const rCol = min(1.0, luma * (0.98 + 0.04 * accent_shift));
        const gCol = min(1.0, luma * (0.90 - 0.18 * dome * accent_shift));
        const bCol = min(1.0, luma * (0.78 - 0.36 * dome * accent_shift));
        fill(rCol, gCol, bCol, 0.98);
      } else {
        // Deep ink to cobalt blue modulation
        const cb = pow(dome, 0.85);
        fill(0.10 + 0.08 * cb, 0.18 + 0.24 * cb, 0.34 + 0.48 * cb, 0.98);
      }

      stroke(0.067, 0.067, 0.078, 0.85);
      strokeWeight(1.4);

      beginShape();
      vertex(cx + p00[0] * span, cy + p00[1] * span);
      vertex(cx + p10[0] * span, cy + p10[1] * span);
      vertex(cx + p11[0] * span, cy + p11[1] * span);
      vertex(cx + p01[0] * span, cy + p01[1] * span);
      endShape();

      // Inner plastic-alphabet circle on dome cells
      if (dome > 0.12) {
        const icx = cx + (p00[0] + p10[0] + p11[0] + p01[0]) * 0.25 * span;
        const icy = cy + (p00[1] + p10[1] + p11[1] + p01[1]) * 0.25 * span;
        const radCell = (span / n) * (0.28 + 0.30 * dome);
        noStroke();
        if (parity === 0) {
          fill(0.118, 0.294, 0.541, 0.94);
        } else {
          fill(0.957, 0.937, 0.902, 0.94);
        }
        ellipse(icx, icy, radCell * 2.0, radCell * 2.0);
      }
    }
  }
}
"""
    }
]


# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: muqarnas_corbel_vault)
#    Palette: #1A1410 (deep umber), #C6934B (limestone gold),
#             #1B3B6F (cobalt), #38899C (turquoise)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "muqarnas-abencerrajes-star-dome-3d",
        "title": "Muqarnas Abencerrajes Star Dome 3D",
        "description": "Alhambra Abencerrajes inspired 16-fold corbelled muqarnas dome with tiered stalactite niches in warm limestone gold, deep umber shadows, and clerestory openings.",
        "tags": ["muqarnas", "alhambra", "dome", "architecture", "islamic", "3d", "sketch-3d"],
        "code": """// Muqarnas Abencerrajes Star Dome 3D - Infinite Library (Sketch 3D)
param("tiers", 6, 4, 8);
param("symmetry", 16, 8, 24);
param("corbel_step", 0.22, 0.12, 0.34);
param("orbit", 0.22, 0.0, 0.6);

function draw(t) {
  rotateX(0.55 + 0.08 * sin(t * 0.35));
  rotateY(t * orbit + 0.35);

  const nT = floor(tiers);
  const nS = floor(symmetry);

  // Octagonal limestone plinth base (#1A1410 / #C6934B)
  fill(0.22, 0.17, 0.13);
  push();
  translate(0, -1.05, 0);
  cylinder(1.95, 0.14);
  pop();

  // Concentric corbelled muqarnas stalactite tiers
  for (let tier = 0; tier < nT; tier++) {
    const u = tier / max(1, nT - 1);
    const radius = 1.65 * (1.0 - u * 0.72);
    const yPos = -0.82 + tier * corbel_step * 1.45;
    const count = max(8, floor(nS * (1.0 - u * 0.35)));
    const phaseOff = (tier % 2) * (PI / count);

    for (let i = 0; i < count; i++) {
      const a = (i / count) * TAU + phaseOff;
      const x = cos(a) * radius;
      const z = sin(a) * radius;

      // Warm Alhambra limestone gold & terracotta-ochre shading per tier
      const lum = 0.56 + 0.28 * (1.0 - u) + 0.08 * sin(i * 1.7);
      fill(lum * 0.96, lum * 0.74, lum * 0.42);

      push();
      translate(x, yPos, z);
      rotateY(-a);

      // Corbelled niche bracket & hanging stalactite prism
      box(0.24 * (1.0 - u * 0.3), 0.26, 0.28 * (1.0 - u * 0.25));

      // Hanging pendant muqarnas drop
      fill(lum * 0.82, lum * 0.60, lum * 0.32);
      translate(0, -0.16, 0.08);
      box(0.11 * (1.0 - u * 0.25), 0.18, 0.11 * (1.0 - u * 0.25));
      pop();
    }
  }

  // Clerestory lantern ring & glowing apex star
  fill(0.95, 0.86, 0.65);
  push();
  translate(0, -0.82 + nT * corbel_step * 1.45 + 0.05, 0);
  torus(0.38, 0.055);
  pop();

  // Radial star ribs at apex
  fill(0.78, 0.58, 0.30);
  for (let r = 0; r < 8; r++) {
    push();
    translate(0, -0.82 + nT * corbel_step * 1.45 + 0.08, 0);
    rotateY((r / 8.0) * PI);
    box(0.76, 0.05, 0.045);
    pop();
  }
}
"""
    },
    {
        "slug": "muqarnas-isfahan-peacock-vault-3d",
        "title": "Muqarnas Isfahan Peacock Vault 3D",
        "description": "Sheikh Lotfollah Mosque inspired peacock-feather radial dome with turquoise squinch ribs, cobalt calligraphy bands, and concentric gold tile medallions.",
        "tags": ["isfahan", "muqarnas", "tilework", "turquoise", "vault", "3d", "sketch-3d"],
        "code": """// Muqarnas Isfahan Peacock Vault 3D - Infinite Library (Sketch 3D)
param("rings", 7, 4, 10);
param("petals", 16, 10, 24);
param("rib_arch", 0.32, 0.15, 0.55);
param("spin", 0.18, 0.0, 0.5);

function draw(t) {
  rotateX(0.48 + 0.07 * cos(t * 0.3));
  rotateY(t * spin - 0.25);

  const nR = floor(rings);
  const nP = floor(petals);

  // Four turquoise glazed squinch ribs (#38899C) rising to the dome drum
  fill(0.22, 0.54, 0.61);
  for (let q = 0; q < 4; q++) {
    const qa = q * (PI * 0.5) + (PI * 0.25);
    for (let s = 0; s < 10; s++) {
      const u = s / 9.0;
      const rr = 1.75 - u * 0.55;
      const yy = -1.0 + sin(u * (PI * 0.5)) * 1.15;
      push();
      translate(cos(qa) * rr, yy, sin(qa) * rr);
      rotateY(-qa);
      rotateZ(u * 0.65);
      cylinder(0.085, 0.22);
      pop();
    }
  }

  // Cobalt blue glazed drum band (#1B3B6F)
  fill(0.106, 0.231, 0.435);
  push();
  translate(0, 0.15, 0);
  torus(1.22, 0.075);
  pop();

  // Concentric spiralling peacock-feather tile medallions (#C6934B & #1B3B6F)
  for (let r = 0; r < nR; r++) {
    const u = r / max(1, nR - 1);
    const phi = u * (PI * 0.5) * 0.92;
    const rad = 1.20 * cos(phi);
    const yy = 0.20 + 1.05 * sin(phi);
    const count = max(6, floor(nP * (1.0 - u * 0.55)));
    const twist = r * 0.18;

    for (let i = 0; i < count; i++) {
      const a = (i / count) * TAU + twist;
      const x = cos(a) * rad;
      const z = sin(a) * rad;

      push();
      translate(x, yy, z);
      rotateY(-a);
      rotateZ(phi);

      // Sandstone gold medallion tile
      fill(0.78 - u * 0.12, 0.58 - u * 0.10, 0.29);
      box(0.20 * (1.0 - u * 0.45), 0.05, 0.26 * (1.0 - u * 0.45));

      // Cobalt & turquoise inset boss
      fill(0.11, 0.24, 0.46);
      translate(0, 0.032, 0);
      box(0.10 * (1.0 - u * 0.45), 0.025, 0.13 * (1.0 - u * 0.45));
      pop();
    }
  }

  // Central golden sun rosette
  fill(0.92, 0.76, 0.42);
  push();
  translate(0, 1.26, 0);
  sphere(0.14);
  pop();
}
"""
    }
]


# -------------------------------------------------------------
# 3. Field Synth (Theme: crystallophone_glass_harmonica)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "crystallophone-glass-harmonica-synth",
        "title": "Crystallophone Glass Harmonica Synth",
        "description": "Bowed nested glass bowl harmonica synthesizer with modal overtone partials, rotational vibrato shimmer, and velocity-coupled harmonic bloom.",
        "note_root": 60,
        "params": {
            "shimmer": 0.48,
            "rotHz": 4.2,
            "rotDepth": 0.0055,
            "warmth": 1.45,
            "decay": 0.9994
        },
        "code": """param float shimmer = 0.48 [0.0, 1.0];
param float rotHz = 4.2 [1.0, 8.0];
param float rotDepth = 0.0055 [0.0, 0.02];
param float warmth = 1.45 [0.6, 3.0];
param float decay = 0.9994 [0.996, 0.9998];
state float p1 = 0; state float p2 = 0; state float p3 = 0;
state float vibPh = 0; state float env = 0; state float lp = 0;

env = if(gate > 0.5, env + (1.0 - env) * 0.045, env * decay);
vibPh = (vibPh + rotHz / sr) % 1.0;
f0 = clamp(freq * (1.0 + rotDepth * sin(6.2831853 * vibPh)), 60.0, 4800.0);

p1 = (p1 + f0 / sr) % 1.0;
p2 = (p2 + (f0 * 2.756) / sr) % 1.0;
p3 = (p3 + (f0 * 5.404) / sr) % 1.0;

bowl1 = sin(6.2831853 * p1);
bowl2 = sin(6.2831853 * p2) * (0.28 * shimmer * env);
bowl3 = sin(6.2831853 * p3) * (0.12 * shimmer * env * env);

raw = (bowl1 * 0.68 + bowl2 + bowl3) * warmth;
sat = raw / (1.0 + abs(raw) * 0.55);
lp = lp + 0.24 * (sat - lp);
sig = lp * env * 0.45;
out = sig / (1.0 + abs(sig) * 0.35);
"""
    },
    {
        "slug": "binaural-celesta-bell-harp-synth",
        "title": "Binaural Celesta Bell Harp Synth",
        "description": "Struck steel-plate celesta and crystal harp hybrid with FM mallet transient, wavefolded overtone body, and micro-detuned shimmer.",
        "note_root": 62,
        "params": {
            "fmIndex": 1.85,
            "fold": 1.65,
            "detune": 0.0038,
            "brightHz": 2600.0,
            "decay": 0.9991
        },
        "code": """param float fmIndex = 1.85 [0.2, 5.0];
param float fold = 1.65 [1.0, 4.0];
param float detune = 0.0038 [0.0, 0.015];
param float brightHz = 2600.0 [600.0, 6000.0];
param float decay = 0.9991 [0.995, 0.9997];
state float pc1 = 0; state float pc2 = 0; state float pm = 0;
state float env = 0; state float strikeEnv = 0; state float lp = 0;

env = if(gate > 0.5, env + (1.0 - env) * 0.25, env * decay);
strikeEnv = if(gate > 0.5, strikeEnv + (1.0 - strikeEnv) * 0.40, strikeEnv * 0.9965);

f0 = clamp(freq, 55.0, 4400.0);
pm = (pm + (f0 * 3.01) / sr) % 1.0;
modSig = sin(6.2831853 * pm) * fmIndex * strikeEnv;

pc1 = (pc1 + (f0 * (1.0 - detune)) / sr) % 1.0;
pc2 = (pc2 + (f0 * (1.0 + detune)) / sr) % 1.0;

c1 = sin(6.2831853 * pc1 + modSig);
c2 = sin(6.2831853 * pc2 + modSig * 0.85);
folded = sin((c1 + c2) * 0.5 * fold);

g = clamp(2.0 * sin(3.14159265 * brightHz / sr), 0.05, 0.85);
lp = lp + g * (folded - lp);
sig = lp * env * 0.44;
out = sig / (1.0 + abs(sig) * 0.38);
"""
    }
]


# -------------------------------------------------------------
# 4. Field Effects (Theme: nagra_tape_dispersion)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "nagra-tape-flutter-loop-fx",
        "title": "Nagra Tape Flutter Loop FX",
        "description": "Reel-to-reel Nagra tape hysteresis saturator with dual-LFO capstan wow and flutter, oxide high-frequency rolloff, and resonant tape feedback.",
        "params": {
            "wowHz": 0.45,
            "flutterHz": 5.8,
            "oxideHz": 2400.0,
            "drive": 1.75,
            "mix": 0.82
        },
        "code": """param float wowHz = 0.45 [0.1, 2.0];
param float flutterHz = 5.8 [2.0, 12.0];
param float oxideHz = 2400.0 [600.0, 6000.0];
param float drive = 1.75 [0.8, 3.5];
param float mix = 0.82 [0.0, 1.0];
state float pWow = 0; state float pFlu = 0;
state float ap1 = 0; state float ap2 = 0;
state float lp1 = 0; state float lp2 = 0; state float fb = 0;

pWow = (pWow + wowHz / sr) % 1.0;
pFlu = (pFlu + flutterHz / sr) % 1.0;
modC = clamp(0.45 + 0.22 * sin(6.2831853 * pWow) + 0.10 * sin(6.2831853 * pFlu), 0.1, 0.88);

x = in + fb * 0.36;
# Hysteresis soft-clipping
sat = (x * drive) / (1.0 + abs(x * drive) * 0.68);

# Capstan phase-drift allpass pair
y1 = -modC * sat + ap1; ap1 = sat + modC * y1;
y2 = -modC * y1 + ap2; ap2 = y1 + modC * y2;

# Oxide high-frequency rolloff
g = clamp(2.0 * sin(3.14159265 * oxideHz / sr), 0.04, 0.85);
lp1 = lp1 + g * (y2 - lp1);
lp2 = lp2 + g * (lp1 - lp2);
fb = clamp(lp2, -1.0, 1.0);

out = (in * (1.0 - mix) + lp2 * mix) * 0.52;
"""
    },
    {
        "slug": "schroeder-allpass-prism-fx",
        "title": "Schroeder Allpass Prism FX",
        "description": "Cascaded 4-stage Schroeder allpass dispersion prism with modulated resonant bandpass feedback matrix and warm transformer saturation.",
        "params": {
            "centerHz": 1150.0,
            "dispersion": 0.72,
            "feedback": 0.48,
            "drive": 1.40,
            "mix": 0.85
        },
        "code": """param float centerHz = 1150.0 [220.0, 3800.0];
param float dispersion = 0.72 [0.15, 0.92];
param float feedback = 0.48 [0.0, 0.80];
param float drive = 1.40 [0.6, 2.8];
param float mix = 0.85 [0.0, 1.0];
state float a1 = 0; state float a2 = 0; state float a3 = 0; state float a4 = 0;
state float bp = 0; state float lp = 0; state float fb = 0; state float lfo = 0;

lfo = (lfo + 0.28 / sr) % 1.0;
modHz = clamp(centerHz * (0.76 + 0.28 * sin(6.2831853 * lfo)), 150.0, 5000.0);
g = clamp(2.0 * sin(3.14159265 * modHz / sr), 0.02, 0.85);

x = in + fb * feedback;
bp = bp + g * (x - lp - 0.55 * bp);
lp = lp + g * bp;
fSig = lp * 0.62 + bp * 0.54;

c = dispersion;
y1 = -c * fSig + a1; a1 = fSig + c * y1;
y2 = -c * y1 + a2; a2 = y1 + c * y2;
y3 = -c * y2 + a3; a3 = y2 + c * y3;
y4 = -c * y3 + a4; a4 = y3 + c * y4;
fb = clamp(y4, -1.1, 1.1);

wd = y4 * drive;
wet = wd / (1.0 + abs(wd) * 0.70);
out = (in * (1.0 - mix) + wet * mix) * 0.86;
"""
    }
]


# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embeds (Theme: ando_concrete_slit)
#    Palette: #121417 (deep shadow), #3A3D40 (poured concrete),
#             #7A7F82 (lit concrete), #F4F7FA (daylight slit)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "ando-concrete-light-slit",
        "title": "Ando Concrete Light Slit",
        "description": "Tadao Ando Church of the Light inspired poured-concrete chamber with formwork tie-hole grid and blinding cruciform daylight slit casting volumetric rays.",
        "embed_desc": "Audio-reactive Tadao Ando brutalist concrete light-slit visualizer. Bass pulses the blinding cruciform daylight aperture, mids breathe through volumetric shadow shafts, and highs graze the poured-concrete formwork grid.",
        "tags": ["brutalist", "ando", "concrete", "light", "shadow", "architecture", "shader", "webgl", "audio-reactive"],
        "params": {
            "slitWidth": 0.032,
            "beamSpread": 0.45,
            "formworkScale": 6.0,
            "concreteGrain": 0.14,
            "glow": 1.15
        },
        "code": """param float slitWidth = 0.032 [0.01, 0.08];
param float beamSpread = 0.45 [0.15, 0.9];
param float formworkScale = 6.0 [3.0, 12.0];
param float concreteGrain = 0.14 [0.02, 0.35];
param float glow = 1.15 [0.5, 2.0];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0;

# Perspective concrete chamber walls & ceiling panels
ax = abs(p.x);
ay = abs(p.y);
wallDepth = max(ax, ay * 0.92);
inEndWall = step(wallDepth, 0.56);

# Formwork grid & tie-hole dots
gridP = p * formworkScale;
seamX = smoothstep(0.035, 0.0, abs(fract(gridP.x) - 0.5) - 0.465);
seamY = smoothstep(0.035, 0.0, abs(fract(gridP.y) - 0.5) - 0.465);
seams = max(seamX, seamY) * 0.25;

cellF = fract(gridP) - 0.5;
tieHole = smoothstep(0.055, 0.025, length(vec2(abs(cellF.x) - 0.26, cellF.y)));

# Subtle concrete aggregate texture
grain = sin(p.x * 47.0 + sin(p.y * 31.0)) * cos(p.y * 53.0 - sin(p.x * 29.0));
concreteBase = 0.24 + 0.06 * grain * concreteGrain - seams - tieHole * 0.16;

# Perspective darkening toward corners
vign = clamp(1.15 - wallDepth * 0.62, 0.18, 1.0);
wallCol = vec3(0.22, 0.24, 0.26) * (concreteBase + 0.15) * vign;

# Cruciform daylight slit on the east end wall
sw = slitWidth * (1.0 + 0.08 * sin(t * 0.8));
slitV = smoothstep(sw + 0.012, sw * 0.4, abs(p.x)) * smoothstep(0.56, 0.52, abs(p.y));
slitH = smoothstep(sw + 0.012, sw * 0.4, abs(p.y - 0.06)) * smoothstep(0.56, 0.52, abs(p.x));
slitMask = max(slitV, slitH);

# Volumetric light shafts radiating across side walls & floor
rayV = exp(-abs(p.x) * (3.2 - beamSpread * 1.4)) * smoothstep(0.95, 0.05, abs(p.y));
rayH = exp(-abs(p.y - 0.06) * (3.6 - beamSpread * 1.4)) * smoothstep(0.95, 0.05, abs(p.x));
volumetric = (rayV + rayH * 0.85) * 0.42 * glow;

cShadow = vec3(0.071, 0.078, 0.090);
cDaylight = vec3(0.957, 0.969, 0.980);

col = mix(cShadow, wallCol, 0.85);
col += vec3(0.45, 0.48, 0.52) * volumetric * (1.0 - inEndWall * 0.35);
col = mix(col, cDaylight, clamp(slitMask * 1.1, 0.0, 1.0));
col = clamp(col, 0.0, 1.0);
""",
        "glsl": """precision mediump float;
uniform vec2 res;
uniform float t;
uniform float aspect;
uniform float audio_bass;
uniform float audio_mid;
uniform float audio_high;

void main() {
  vec2 uv = gl_FragCoord.xy / res;
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0;

  float ax = abs(p.x);
  float ay = abs(p.y);
  float wallDepth = max(ax, ay * 0.92);
  float inEndWall = step(wallDepth, 0.56);

  vec2 gridP = p * 6.0;
  float seamX = smoothstep(0.035, 0.0, abs(fract(gridP.x) - 0.5) - 0.465);
  float seamY = smoothstep(0.035, 0.0, abs(fract(gridP.y) - 0.5) - 0.465);
  float seams = max(seamX, seamY) * 0.25;

  vec2 cellF = fract(gridP) - 0.5;
  float tieHole = smoothstep(0.055, 0.025, length(vec2(abs(cellF.x) - 0.26, cellF.y)));

  float grain = sin(p.x * 47.0 + sin(p.y * 31.0)) * cos(p.y * 53.0 - sin(p.x * 29.0));
  float concreteBase = 0.24 + 0.06 * grain * (0.14 + audio_high * 0.16) - seams - tieHole * 0.16;

  float vign = clamp(1.15 - wallDepth * 0.62, 0.18, 1.0);
  vec3 wallCol = vec3(0.22, 0.24, 0.26) * (concreteBase + 0.15) * vign;

  float sw = 0.032 + audio_bass * 0.028;
  float slitV = smoothstep(sw + 0.012, sw * 0.4, abs(p.x)) * smoothstep(0.56, 0.52, abs(p.y));
  float slitH = smoothstep(sw + 0.012, sw * 0.4, abs(p.y - 0.06)) * smoothstep(0.56, 0.52, abs(p.x));
  float slitMask = max(slitV, slitH);

  float beamSpread = 0.45 + audio_mid * 0.45;
  float rayV = exp(-abs(p.x) * (3.2 - beamSpread * 1.4)) * smoothstep(0.95, 0.05, abs(p.y));
  float rayH = exp(-abs(p.y - 0.06) * (3.6 - beamSpread * 1.4)) * smoothstep(0.95, 0.05, abs(p.x));
  float volumetric = (rayV + rayH * 0.85) * 0.48 * (1.0 + audio_mid * 0.55);

  vec3 cShadow = vec3(0.071, 0.078, 0.090);
  vec3 cDaylight = vec3(0.957, 0.969, 0.980);

  vec3 col = mix(cShadow, wallCol, 0.85);
  col += vec3(0.45, 0.48, 0.52) * volumetric * (1.0 - inEndWall * 0.35);
  col = mix(col, cDaylight, clamp(slitMask * 1.1, 0.0, 1.0));
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "cathedral-rose-caustic-prism",
        "title": "Cathedral Rose Caustic Prism",
        "description": "Gothic stone rose-window tracery with 12-fold radial lancet apertures refracting prismatic caustic shafts across dark cathedral masonry.",
        "embed_desc": "Audio-reactive Cathedral Rose Caustic Prism visualizer. Bass opens the radial stone tracery, mids sweep the prismatic caustic shafts, and highs shimmer across the stained-glass bevels.",
        "tags": ["cathedral", "caustics", "rose-window", "refraction", "architecture", "shader", "webgl", "audio-reactive"],
        "params": {
            "petals": 12.0,
            "tracery": 0.24,
            "prismSpread": 0.55,
            "causticGlow": 1.1,
            "rotation": 0.12
        },
        "code": """param float petals = 12.0 [6.0, 18.0];
param float tracery = 0.24 [0.08, 0.45];
param float prismSpread = 0.55 [0.1, 1.0];
param float causticGlow = 1.1 [0.4, 2.0];
param float rotation = 0.12 [0.0, 0.6];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0;
r = length(p);
a = atan2(p.y, p.x) + t * rotation;

# Radial rose-window stone tracery ribs & stained-glass lead cames
spoke = abs(cos(a * petals * 0.5));
subSpoke = abs(cos(a * petals * 1.5));
ring1 = abs(r - 0.28);
ring2 = abs(r - 0.52);
ring3 = abs(r - 0.74);
ring4 = abs(r - 0.90);

stoneRibs = smoothstep(tracery, tracery * 0.25, spoke) * smoothstep(0.14, 0.22, r);
leadCames = smoothstep(0.08, 0.02, subSpoke) * smoothstep(0.32, 0.45, r) * 0.85;
stoneRings = smoothstep(0.028, 0.006, min(min(ring1, ring2), min(ring3, ring4)));
quarry = smoothstep(0.08, 0.02, abs(sin(p.x * 22.0 + p.y * 22.0)) * abs(sin(p.x * 22.0 - p.y * 22.0)));
oculus = smoothstep(0.14, 0.09, r);
traceryMask = clamp(max(max(stoneRibs, leadCames), stoneRings) * smoothstep(0.95, 0.88, r) + (1.0 - smoothstep(0.88, 0.94, r)), 0.0, 1.0);

# Prismatic stained glass caustic dispersion
caustic = 0.5 + 0.5 * cos(a * petals + r * 18.0 - t * 1.4);
facet = 0.5 + 0.5 * sin(a * petals * 2.0 - r * 24.0);
cRuby = vec3(0.94, 0.26, 0.28);
cAmber = vec3(0.98, 0.78, 0.28);
cCobalt = vec3(0.20, 0.50, 0.92);

glassCol = mix(cCobalt, cRuby, smoothstep(0.15, 0.85, sin(a * 3.0 + r * 6.0) * 0.5 + 0.5));
glassCol = mix(glassCol, cAmber, pow(caustic * facet, 1.4) * prismSpread);
glassCol *= (0.45 + 0.55 * (1.0 - quarry));

# Sweeping volumetric light shafts & stone masonry contrast
shaft = exp(-r * 1.1) * (0.55 + 0.45 * pow(abs(cos(a * petals * 0.5)), 2.5)) * causticGlow;
ashlar = 0.06 * step(0.92, abs(sin(p.x * 10.0))) + 0.06 * step(0.92, abs(sin(p.y * 14.0 + floor(p.x * 5.0) * 1.5)));
cStone = vec3(0.18, 0.17, 0.19) + ashlar;

aperture = (1.0 - traceryMask) * smoothstep(0.94, 0.08, r);
col = mix(cStone, glassCol * (0.78 + 0.58 * caustic), aperture);
col += glassCol * shaft * 0.58;
col = mix(col, vec3(0.98, 0.96, 0.92), oculus * 0.95);
col = clamp(col, 0.0, 1.0);
""",
        "glsl": """precision mediump float;
uniform vec2 res;
uniform float t;
uniform float aspect;
uniform float audio_bass;
uniform float audio_mid;
uniform float audio_high;

void main() {
  vec2 uv = gl_FragCoord.xy / res;
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0;
  float r = length(p);
  float a = atan(p.y, p.x) + t * 0.12;

  float petals = 12.0;
  float tracery = 0.24 - audio_bass * 0.08;
  float spoke = abs(cos(a * petals * 0.5));
  float ring1 = abs(r - 0.34);
  float ring2 = abs(r - 0.64);
  float ring3 = abs(r - 0.86);

  float stoneRibs = smoothstep(tracery, tracery * 0.4, spoke) * smoothstep(0.18, 0.26, r);
  float stoneRings = smoothstep(0.032, 0.012, min(ring1, min(ring2, ring3)));
  float oculus = smoothstep(0.14 + audio_bass * 0.03, 0.10, r);
  float traceryMask = clamp(max(stoneRibs, stoneRings) * smoothstep(0.92, 0.86, r) + (1.0 - smoothstep(0.86, 0.92, r)), 0.0, 1.0);

  float caustic = 0.5 + 0.5 * cos(a * petals + r * 12.0 - t * (1.4 + audio_mid * 0.8));
  vec3 cRuby = vec3(0.84, 0.22, 0.26);
  vec3 cAmber = vec3(0.92, 0.68, 0.24);
  vec3 cCobalt = vec3(0.16, 0.40, 0.78);

  vec3 glassCol = mix(cCobalt, cRuby, smoothstep(0.2, 0.8, sin(a * 3.0 + r * 4.0) * 0.5 + 0.5));
  glassCol = mix(glassCol, cAmber, pow(caustic, 2.2) * (0.55 + audio_high * 0.45));

  float shaft = exp(-r * 1.8) * (0.45 + 0.55 * pow(abs(cos(a * petals * 0.5)), 4.0)) * (1.1 + audio_mid * 0.7);
  vec3 cStone = vec3(0.082, 0.086, 0.102);

  float aperture = (1.0 - traceryMask) * smoothstep(0.90, 0.12, r);
  vec3 col = mix(cStone, glassCol * (0.65 + 0.55 * caustic), aperture);
  col += glassCol * shaft * 0.38;
  col = mix(col, vec3(0.95, 0.94, 0.90), oculus * 0.85);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
"""
    }
]


def make_embed_html(item):
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>{item['title']} — Infinite</title>
  <style>
    html, body {{ margin: 0; padding: 0; width: 100%; height: 100%; overflow: hidden; background: #121417; }}
    canvas {{ display: block; width: 100%; height: 100%; }}
  </style>
</head>
<body>
  <canvas id="gl"></canvas>
  <script>
    const canvas = document.getElementById("gl");
    const gl = canvas.getContext("webgl");

    const vsSource = `
      attribute vec2 position;
      void main() {{
        gl_Position = vec4(position, 0.0, 1.0);
      }}
    `;

    const fsSource = `
{item['glsl']}
`;

    function createShader(gl, type, source) {{
      const shader = gl.createShader(type);
      gl.shaderSource(shader, source);
      gl.compileShader(shader);
      if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {{
        console.error(gl.getShaderInfoLog(shader));
        gl.deleteShader(shader);
        return null;
      }}
      return shader;
    }}

    const program = gl.createProgram();
    gl.attachShader(program, createShader(gl, gl.VERTEX_SHADER, vsSource));
    gl.attachShader(program, createShader(gl, gl.FRAGMENT_SHADER, fsSource));
    gl.linkProgram(program);
    gl.useProgram(program);

    const buffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
      -1.0, -1.0,  1.0, -1.0, -1.0,  1.0,
      -1.0,  1.0,  1.0, -1.0,  1.0,  1.0
    ]), gl.STATIC_DRAW);

    const pos = gl.getAttribLocation(program, "position");
    gl.enableVertexAttribArray(pos);
    gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 0, 0);

    const uRes = gl.getUniformLocation(program, "res");
    const uTime = gl.getUniformLocation(program, "t");
    const uAspect = gl.getUniformLocation(program, "aspect");
    const uBass = gl.getUniformLocation(program, "audio_bass");
    const uMid = gl.getUniformLocation(program, "audio_mid");
    const uHigh = gl.getUniformLocation(program, "audio_high");

    let audioCtx, analyser, dataArray;
    let bass = 0, mid = 0, high = 0;

    document.getElementById("btn-audio")?.addEventListener("click", async () => {{
      if (audioCtx) return;
      try {{
        const stream = await navigator.mediaDevices.getUserMedia({{ audio: true }});
        audioCtx = new (window.AudioContext || window.webkitAudioContext)();
        const src = audioCtx.createMediaStreamSource(stream);
        analyser = audioCtx.createAnalyser();
        analyser.fftSize = 256;
        src.connect(analyser);
        dataArray = new Uint8Array(analyser.frequencyBinCount);
        document.getElementById("btn-audio")?.classList.add("on");
      }} catch (e) {{
        document.getElementById("btn-audio")?.classList.add("off");
      }}
    }});

    function resize() {{
      const dpr = Math.min(window.devicePixelRatio || 1, 2);
      canvas.width = canvas.clientWidth * dpr;
      canvas.height = canvas.clientHeight * dpr;
      gl.viewport(0, 0, canvas.width, canvas.height);
    }}
    window.addEventListener("resize", resize);
    resize();

    let start = performance.now();
    function render(now) {{
      const tt = (now - start) * 0.001;
      if (analyser) {{
        analyser.getByteFrequencyData(dataArray);
        let b = 0, m = 0, h = 0;
        for (let i = 0; i < 6; i++) b += dataArray[i];
        for (let i = 6; i < 32; i++) m += dataArray[i];
        for (let i = 32; i < 96; i++) h += dataArray[i];
        bass = bass * 0.8 + (b / (6 * 255)) * 0.2;
        mid = mid * 0.8 + (m / (26 * 255)) * 0.2;
        high = high * 0.8 + (h / (64 * 255)) * 0.2;
      }} else {{
        bass = 0.24 + 0.14 * Math.sin(tt * 1.8);
        mid = 0.22 + 0.14 * Math.sin(tt * 1.4 + 1.2);
        high = 0.20 + 0.12 * Math.sin(tt * 3.1 + 2.4);
      }}

      gl.uniform2f(uRes, canvas.width, canvas.height);
      gl.uniform1f(uTime, tt);
      gl.uniform1f(uAspect, canvas.width / Math.max(1, canvas.height));
      gl.uniform1f(uBass, bass);
      gl.uniform1f(uMid, mid);
      gl.uniform1f(uHigh, high);

      gl.drawArrays(gl.TRIANGLES, 0, 6);
      requestAnimationFrame(render);
    }}
    requestAnimationFrame(render);
  </script>
</body>
</html>
"""


def main():
    report = []
    failed = False

    # 1. Sketch 2D
    for it in SKETCH_2D_ITEMS:
        slug = it["slug"]
        js_file = os.path.join(SKETCH_DIR, f"{slug}.js")
        with open(js_file, "w") as f:
            f.write(it["code"])
        it["bytes"] = os.path.getsize(js_file)

        escaped = it["code"].replace("\\", "\\\\").replace("\n", "\\n")
        inf_content = f"""infinite-patch 1
node 1 Source Sketch
  id sk
  pos 0 0
  f width 1080
  f height 1080
  s code {escaped}
end
node 2 Utility Output
  id out
  pos 400 0
end
cable out 0 sk
"""
        test_inf = os.path.join(TMP_DIR, f"{slug}.inf")
        with open(test_inf, "w") as f:
            f.write(inf_content)

        v = subprocess.run(CMD + ["validate", test_inf], capture_output=True, text=True)
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}\n{v.stderr}")
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        frame_png = os.path.join(out_frame_dir, "frame.png")
        f_proc = subprocess.run(CMD + ["frame", test_inf, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        if '"ok":true' not in f_proc.stdout or not os.path.exists(frame_png):
            print(f"FAIL frame: {slug}\n{f_proc.stdout}\n{f_proc.stderr}")
            failed = True
            continue

        im = Image.open(frame_png)
        lum = ImageStat.Stat(im.convert("L")).mean[0]
        sc = library.interest(frame_png)

        if not (14.0 <= lum <= 228.0) or sc < 0.45:
            print(f"FAIL metrics for {slug}: lum={lum:.1f}, interest={sc:.3f}")
            failed = True
            continue

        prev_jpg = os.path.join(PREVIEWS_DIR, f"{slug}.jpg")
        im.convert("RGB").save(prev_jpg, quality=94)

        thumb_webp = os.path.join(THUMBS_DIR, f"{slug}.webp")
        im.convert("RGB").resize((540, 540), Image.Resampling.LANCZOS).save(thumb_webp, "WEBP", quality=85)

        report.append(f"PASS [Sketch 2D] {slug}: lum={lum:.1f}, interest={sc:.3f}")

    # 2. Sketch 3D
    for it in SKETCH_3D_ITEMS:
        slug = it["slug"]
        js_file = os.path.join(SKETCH_DIR, f"{slug}.js")
        with open(js_file, "w") as f:
            f.write(it["code"])
        it["bytes"] = os.path.getsize(js_file)

        escaped = it["code"].replace("\\", "\\\\").replace("\n", "\\n")
        inf_content = f"""infinite-patch 1
node 1 3D Sketch 3D
  id sk
  pos 0 0
  s code {escaped}
end
node 2 3D Render 3D
  id ren
  pos 400 0
  f width 1080
  f height 1080
  c bg 0.10 0.08 0.07
end
node 3 Utility Output
  id out
  pos 800 0
end
geo ren 0 sk
cable out 0 ren
"""
        test_inf = os.path.join(TMP_DIR, f"{slug}.inf")
        with open(test_inf, "w") as f:
            f.write(inf_content)

        v = subprocess.run(CMD + ["validate", test_inf], capture_output=True, text=True)
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}\n{v.stderr}")
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        frame_png = os.path.join(out_frame_dir, "frame.png")
        f_proc = subprocess.run(CMD + ["frame", test_inf, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        if '"ok":true' not in f_proc.stdout or not os.path.exists(frame_png):
            print(f"FAIL frame: {slug}\n{f_proc.stdout}\n{f_proc.stderr}")
            failed = True
            continue

        im = Image.open(frame_png)
        lum = ImageStat.Stat(im.convert("L")).mean[0]
        sc = library.interest(frame_png)

        if not (14.0 <= lum <= 228.0) or sc < 0.45:
            print(f"FAIL metrics for {slug}: lum={lum:.1f}, interest={sc:.3f}")
            failed = True
            continue

        prev_jpg = os.path.join(PREVIEWS_DIR, f"{slug}.jpg")
        im.convert("RGB").save(prev_jpg, quality=94)

        thumb_webp = os.path.join(THUMBS_DIR, f"{slug}.webp")
        im.convert("RGB").resize((540, 540), Image.Resampling.LANCZOS).save(thumb_webp, "WEBP", quality=85)

        report.append(f"PASS [Sketch 3D] {slug}: lum={lum:.1f}, interest={sc:.3f}")

    # 3. Field Synth
    for it in SYNTH_ITEMS:
        slug = it["slug"]
        field_dev = {
            "format": "field",
            "version": 1,
            "meta": {
                "name": it["title"],
                "author": "Infinite",
                "description": it["description"],
                "domain": "synth"
            },
            "device": {
                "code": it["code"],
                "params": it["params"],
                "nodeSettings": {"maxVoices": 8}
            }
        }
        field_file = os.path.join(TEMPLATES_DIR, f"{slug}.field")
        with open(field_file, "w") as f:
            json.dump(field_dev, f, indent=2)

        escaped = it["code"].replace("\\", "\\\\").replace("\n", "\\n")
        nr = it["note_root"]
        inf_content = f"""infinite-patch 1
# {it['title']}
node 1 Notes Note Sequencer
  id notes
  pos 0 1024
  f rateBeats 0.5
  i steps 16
  i note0 {nr}
  i note1 {nr+2}
  i note2 {nr+4}
  i note3 {nr+7}
  i note4 {nr+9}
  i note5 {nr+7}
  i note6 {nr+4}
  i note7 {nr+2}
  i note8 {nr}
  i note9 {nr+7}
  i note10 {nr+9}
  i note11 {nr+12}
  i note12 {nr+9}
  i note13 {nr+7}
  i note14 {nr+4}
  i note15 {nr+2}
  b en0 1
  b en1 1
  b en2 1
  b en3 1
  b en4 1
  b en5 1
  b en6 1
  b en7 1
  b en8 1
  b en9 1
  b en10 1
  b en11 1
  b en12 1
  b en13 1
  b en14 1
  b en15 1
end
node 2 Synths Field Synth
  id synth
  pos 480 1024
  s code {escaped}
end
node 3 Utility Audio Out
  id speakers
  pos 900 1024
end
node 4 Source Shape
  id pic
  pos 0 162
  c fillColor 0.24 0.30 0.36
  f size 0.24
end
node 5 Utility Output
  id out
  pos 460 0
end
node 6 Compositing Comment
  pos 0 722
  s text {it['title']}. {it['description']}
  f width 340
  f height 120
end
note synth 0 notes
aud speakers 0 synth
cable out 0 pic
"""
        inf_file = os.path.join(TEMPLATES_DIR, f"{slug}.inf")
        with open(inf_file, "w") as f:
            f.write(inf_content)
        it["bytes"] = os.path.getsize(inf_file)

        v = subprocess.run(CMD + ["validate", inf_file], capture_output=True, text=True)
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}\n{v.stderr}")
            failed = True
            continue

        sum_json = os.path.join(TMP_DIR, f"{slug}_sum.json")
        s = subprocess.run(CMD + ["audio", inf_file, sum_json, "--duration", "6"], capture_output=True, text=True)
        if '"ok":true' not in s.stdout or not os.path.exists(sum_json):
            print(f"FAIL audio command: {slug}\n{s.stdout}\n{s.stderr}")
            failed = True
            continue

        d = json.load(open(sum_json))
        lufs = d.get("integrated_lufs")
        peak = d.get("sample_peak_dbfs")
        silence = d.get("silence_ratio")

        if lufs is None or not (-26.0 <= lufs <= -8.0) or peak is None or peak >= -0.5 or silence != 0:
            print(f"FAIL audio gates for {slug}: lufs={lufs}, peak={peak}, silence={silence}")
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        frame_png = os.path.join(out_frame_dir, "frame.png")
        subprocess.run(CMD + ["frame", inf_file, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        im = Image.open(frame_png)

        prev_jpg = os.path.join(PREVIEWS_DIR, f"{slug}.jpg")
        im.convert("RGB").save(prev_jpg, quality=94)

        thumb_webp = os.path.join(THUMBS_DIR, f"{slug}.webp")
        im.convert("RGB").resize((540, 540), Image.Resampling.LANCZOS).save(thumb_webp, "WEBP", quality=85)

        tmpl_webp = os.path.join(TEMPLATES_DIR, f"{slug}.webp")
        template_preview.render(inf_file, tmpl_webp)

        report.append(f"PASS [Field Synth] {slug}: lufs={lufs:.2f}, peak={peak:.2f}dBFS, silence={silence}")

    # 4. Field Effects
    for it in FX_ITEMS:
        slug = it["slug"]
        field_dev = {
            "format": "field",
            "version": 1,
            "meta": {
                "name": it["title"],
                "author": "Infinite",
                "description": it["description"],
                "domain": "effect"
            },
            "device": {
                "code": it["code"],
                "params": it["params"]
            }
        }
        field_file = os.path.join(TEMPLATES_DIR, f"{slug}.field")
        with open(field_file, "w") as f:
            json.dump(field_dev, f, indent=2)

        escaped = it["code"].replace("\\", "\\\\").replace("\n", "\\n")
        inf_content = f"""infinite-patch 1
# {it['title']}
node 1 Notes Note Sequencer
  id notes
  pos 0 1024
  f rateBeats 0.5
  i steps 16
  i note0 48
  i note1 51
  i note2 55
  i note3 58
  i note4 55
  i note5 51
  i note6 50
  i note7 53
  i note8 48
  i note9 55
  i note10 58
  i note11 60
  i note12 58
  i note13 55
  i note14 51
  i note15 50
  b en0 1
  b en1 1
  b en2 1
  b en3 1
  b en4 1
  b en5 1
  b en6 1
  b en7 1
  b en8 1
  b en9 1
  b en10 1
  b en11 1
  b en12 1
  b en13 1
  b en14 1
  b en15 1
end
node 2 Synths Analog
  id osc
  pos 320 1024
  f cutoff 2400.0
  f resonance 0.25
  f decay 0.45
  f volume 0.85
end
node 3 AudioEffects Field Effect
  id fx
  pos 640 1024
  s code {escaped}
end
node 4 Utility Audio Out
  id speakers
  pos 960 1024
end
node 5 Source Shape
  id pic
  pos 0 162
  c fillColor 0.28 0.22 0.16
  f size 0.24
end
node 6 Utility Output
  id out
  pos 460 0
end
node 7 Compositing Comment
  pos 0 722
  s text {it['title']}. {it['description']}
  f width 340
  f height 120
end
note osc 0 notes
aud fx 0 osc
aud speakers 0 fx
cable out 0 pic
"""
        inf_file = os.path.join(TEMPLATES_DIR, f"{slug}.inf")
        with open(inf_file, "w") as f:
            f.write(inf_content)
        it["bytes"] = os.path.getsize(inf_file)

        v = subprocess.run(CMD + ["validate", inf_file], capture_output=True, text=True)
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}\n{v.stderr}")
            failed = True
            continue

        sum_json = os.path.join(TMP_DIR, f"{slug}_sum.json")
        s = subprocess.run(CMD + ["audio", inf_file, sum_json, "--duration", "6"], capture_output=True, text=True)
        if '"ok":true' not in s.stdout or not os.path.exists(sum_json):
            print(f"FAIL audio command: {slug}\n{s.stdout}\n{s.stderr}")
            failed = True
            continue

        d = json.load(open(sum_json))
        lufs = d.get("integrated_lufs")
        peak = d.get("sample_peak_dbfs")
        silence = d.get("silence_ratio")

        if lufs is None or not (-26.0 <= lufs <= -8.0) or peak is None or peak >= -0.5 or silence != 0:
            print(f"FAIL audio gates for {slug}: lufs={lufs}, peak={peak}, silence={silence}")
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        frame_png = os.path.join(out_frame_dir, "frame.png")
        subprocess.run(CMD + ["frame", inf_file, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        im = Image.open(frame_png)

        prev_jpg = os.path.join(PREVIEWS_DIR, f"{slug}.jpg")
        im.convert("RGB").save(prev_jpg, quality=94)

        thumb_webp = os.path.join(THUMBS_DIR, f"{slug}.webp")
        im.convert("RGB").resize((540, 540), Image.Resampling.LANCZOS).save(thumb_webp, "WEBP", quality=85)

        tmpl_webp = os.path.join(TEMPLATES_DIR, f"{slug}.webp")
        template_preview.render(inf_file, tmpl_webp)

        report.append(f"PASS [Field FX] {slug}: lufs={lufs:.2f}, peak={peak:.2f}dBFS, silence={silence}")

    # 5. Field Pixel & WebGL Embed
    for it in PIXEL_ITEMS:
        slug = it["slug"]
        field_dev = {
            "format": "field",
            "version": 1,
            "meta": {
                "name": it["title"],
                "author": "Infinite",
                "description": it["description"],
                "domain": "pixel"
            },
            "device": {
                "code": it["code"],
                "params": it["params"]
            }
        }
        field_file = os.path.join(PIXEL_DIR, f"{slug}.field")
        with open(field_file, "w") as f:
            json.dump(field_dev, f, indent=2)
        it["bytes"] = os.path.getsize(field_file)

        embed_html_file = os.path.join(EMBEDS_DIR, f"{slug}.html")
        with open(embed_html_file, "w") as f:
            f.write(make_embed_html(it))

        escaped = it["code"].replace("\\", "\\\\").replace("\n", "\\n")
        inf_content = f"""infinite-patch 1
node 1 Source FieldPixel
  id px
  pos 0 0
  f width 1080
  f height 1080
  s code {escaped}
end
node 2 Utility Output
  id out
  pos 400 0
end
cable out 0 px
"""
        test_inf = os.path.join(TMP_DIR, f"{slug}.inf")
        with open(test_inf, "w") as f:
            f.write(inf_content)

        v = subprocess.run(CMD + ["validate", test_inf], capture_output=True, text=True)
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}\n{v.stderr}")
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        frame_png = os.path.join(out_frame_dir, "frame.png")
        f_proc = subprocess.run(CMD + ["frame", test_inf, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        if '"ok":true' not in f_proc.stdout or not os.path.exists(frame_png):
            print(f"FAIL frame: {slug}\n{f_proc.stdout}\n{f_proc.stderr}")
            failed = True
            continue

        im = Image.open(frame_png)
        lum = ImageStat.Stat(im.convert("L")).mean[0]
        sc = library.interest(frame_png)

        if not (14.0 <= lum <= 228.0) or sc < 0.45:
            print(f"FAIL metrics for {slug}: lum={lum:.1f}, interest={sc:.3f}")
            failed = True
            continue

        prev_jpg = os.path.join(PREVIEWS_DIR, f"{slug}.jpg")
        im.convert("RGB").save(prev_jpg, quality=94)

        thumb_webp = os.path.join(THUMBS_DIR, f"{slug}.webp")
        im.convert("RGB").resize((540, 540), Image.Resampling.LANCZOS).save(thumb_webp, "WEBP", quality=85)

        report.append(f"PASS [Field Pixel] {slug}: lum={lum:.1f}, interest={sc:.3f}")

    print("\n=== Verification Summary ===")
    for line in report:
        print(f"  {line}")

    if failed or len(report) < 10:
        print(f"\nABORTING: Not all 10 items passed gates! (passed {len(report)}/10)")
        sys.exit(1)

    print("\nAll 10 items successfully passed engine verification gates!")

    # Register in library index.json
    lib_index_file = os.path.join(LIB_DIR, "index.json")
    lib_data = json.load(open(lib_index_file))
    existing_lib_ids = {it["id"] for it in lib_data["items"]}

    for it in SKETCH_2D_ITEMS:
        if it["slug"] not in existing_lib_ids:
            lib_data["items"].append({
                "id": it["slug"],
                "name": it["title"],
                "kind": "Generator",
                "tags": [t.lower() for t in it["tags"]],
                "description": it["description"],
                "price": 0,
                "domain": "sketch",
                "category": "Sketch",
                "params": 4,
                "file": f"assets/library/sketch/{it['slug']}.js",
                "preview": f"assets/library/previews/{it['slug']}.jpg",
                "bytes": it["bytes"]
            })

    for it in SKETCH_3D_ITEMS:
        if it["slug"] not in existing_lib_ids:
            lib_data["items"].append({
                "id": it["slug"],
                "name": it["title"],
                "kind": "Generator",
                "tags": [t.lower() for t in it["tags"]],
                "description": it["description"],
                "price": 0,
                "domain": "sketch-3d",
                "category": "Sketch 3D",
                "params": 4,
                "file": f"assets/library/sketch/{it['slug']}.js",
                "preview": f"assets/library/previews/{it['slug']}.jpg",
                "bytes": it["bytes"]
            })

    for it in SYNTH_ITEMS:
        if it["slug"] not in existing_lib_ids:
            lib_data["items"].append({
                "id": it["slug"],
                "name": it["title"],
                "kind": "Generator",
                "tags": ["synth", "crystallophone", "glass-harmonica", "celesta", "audio", "dsp"],
                "description": it["description"],
                "price": 0,
                "domain": "synth",
                "category": "Synths",
                "params": len(it["params"]),
                "file": f"assets/templates/{it['slug']}.inf",
                "preview": f"assets/library/previews/{it['slug']}.jpg",
                "bytes": it["bytes"]
            })

    for it in FX_ITEMS:
        if it["slug"] not in existing_lib_ids:
            lib_data["items"].append({
                "id": it["slug"],
                "name": it["title"],
                "kind": "Generator",
                "tags": ["effect", "nagra", "tape", "allpass", "dispersion", "audio", "dsp"],
                "description": it["description"],
                "price": 0,
                "domain": "effect",
                "category": "Audio effects",
                "params": len(it["params"]),
                "file": f"assets/templates/{it['slug']}.inf",
                "preview": f"assets/library/previews/{it['slug']}.jpg",
                "bytes": it["bytes"]
            })

    for it in PIXEL_ITEMS:
        if it["slug"] not in existing_lib_ids:
            lib_data["items"].append({
                "id": it["slug"],
                "name": it["title"],
                "kind": "Generator",
                "tags": [t.lower() for t in it["tags"]],
                "description": it["description"],
                "price": 0,
                "domain": "pixel",
                "category": "Pixel",
                "params": 5,
                "file": f"assets/library/pixel/{it['slug']}.field",
                "preview": f"assets/library/previews/{it['slug']}.jpg",
                "bytes": it["bytes"]
            })

    lib_data["count"] = len(lib_data["items"])
    with open(lib_index_file, "w") as f:
        json.dump(lib_data, f, indent=2)
    print(f"Registered in {lib_index_file} (total {len(lib_data['items'])} items)")

    # Register in templates index.json
    tmpl_index_file = os.path.join(TEMPLATES_DIR, "index.json")
    tmpl_data = json.load(open(tmpl_index_file))
    existing_tmpl_ids = {it["id"] for it in tmpl_data["items"]}

    for it in SYNTH_ITEMS:
        if it["slug"] not in existing_tmpl_ids:
            tmpl_data["items"].append({
                "id": it["slug"],
                "name": it["title"],
                "category": "Templates",
                "kind": "Template",
                "group": "Synths",
                "description": it["description"],
                "price": 0,
                "preview": f"assets/templates/{it['slug']}.webp",
                "file": f"assets/templates/{it['slug']}.inf",
                "nodes": ["Note Sequencer", "Field Synth", "Audio Out"]
            })

    for it in FX_ITEMS:
        if it["slug"] not in existing_tmpl_ids:
            tmpl_data["items"].append({
                "id": it["slug"],
                "name": it["title"],
                "category": "Templates",
                "kind": "Template",
                "group": "Audio effects",
                "description": it["description"],
                "price": 0,
                "preview": f"assets/templates/{it['slug']}.webp",
                "file": f"assets/templates/{it['slug']}.inf",
                "nodes": ["Note Sequencer", "Analog", "Field Effect", "Audio Out"]
            })

    tmpl_data["count"] = len(tmpl_data["items"])
    with open(tmpl_index_file, "w") as f:
        json.dump(tmpl_data, f, indent=2)
    print(f"Registered in {tmpl_index_file} (total {len(tmpl_data['items'])} items)")

    # Register in embeds.json
    embeds_file = os.path.join(EMBEDS_DIR, "embeds.json")
    embeds_data = json.load(open(embeds_file))
    existing_embed_slugs = {it["slug"] for it in embeds_data["items"]}

    for it in PIXEL_ITEMS:
        if it["slug"] not in existing_embed_slugs:
            embeds_data["items"].append({
                "slug": it["slug"],
                "title": it["title"],
                "description": it["embed_desc"]
            })

    with open(embeds_file, "w") as f:
        json.dump(embeds_data, f, indent=2)
    print(f"Registered in {embeds_file} (total {len(embeds_data['items'])} items)")

    # Register in token_baseline.json
    tb_file = os.path.join(ROOT, "tools", "website", "token_baseline.json")
    if os.path.exists(tb_file):
        tb_data = json.load(open(tb_file))
        if "counts" not in tb_data:
            tb_data["counts"] = {}
        for it in PIXEL_ITEMS:
            key = f"embeds/{it['slug']}.html"
            if key not in tb_data["counts"]:
                tb_data["counts"][key] = {
                    "font-size": 2,
                    "hex": 6
                }
        with open(tb_file, "w") as f:
            json.dump(tb_data, f, indent=1)
        print(f"Registered embeds in {tb_file}")

    # Update infinite-library.zip
    zip_path = os.path.join(LIB_DIR, "infinite-library.zip")
    items = lib_data["items"]
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for it in items:
            src = os.path.join(ROOT, "website", it["file"])
            domain = it["domain"]
            bname = os.path.basename(it["file"])
            arcname = f"{domain}/{bname}"
            z.write(src, arcname)
    print(f"Updated {zip_path} (size: {os.path.getsize(zip_path)} bytes)")


if __name__ == "__main__":
    main()
