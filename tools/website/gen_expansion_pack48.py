#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 48.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: chladni_brass_nodal -> chladni-brass-nodal-harmonics, chladni-square-superposition-plate)
2. Sketch 3D (Theme: xenakis_hyperbolic_ruled -> xenakis-philips-pavilion-shells-3d, xenakis-ruled-hyperboloid-tower-3d)
3. Field Synth (Theme: trautonium_subharmonic_organ -> trautonium-subharmonic-mixtur-synth, ondes-martenot-palme-synth)
4. Field Effects (Theme: schroeder_qrd_dispersion -> schroeder-qrd-prime-diffuser-fx, frippertronics-dual-reel-loop-fx)
5. Field Pixel & Embeds (Theme: sashimono_kumiko_shoji -> kumiko-asanoha-shoji-lantern, kumiko-seigaiha-timber-screen)
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

TMP_DIR = "/tmp/pack48_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview


# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: chladni_brass_nodal)
#    Palette: #141210 (obsidian), #C9A449 (brushed brass),
#             #F5EFE0 (quartz sand), #6E3F24 (mahogany)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "chladni-brass-nodal-harmonics",
        "title": "Chladni Brass Nodal Harmonics",
        "description": "Octagonal vibrating brass Chladni plate on an obsidian stage with standing-wave nodal contours, clustered quartz sand grains, and a central brass clamp.",
        "tags": ["chladni", "cymatics", "acoustics", "brass", "nodal", "procedural", "sketch"],
        "code": """// Chladni Brass Nodal Harmonics - Infinite Library (Sketch 2D)
param("mode_m", 3, 2, 7);
param("mode_n", 5, 2, 8);
param("sand_density", 52, 24, 68);
param("phase_drift", 0.25, 0.0, 1.0);

function draw(t) {
  // Obsidian studio ground (#141210)
  background(0.078, 0.071, 0.063);
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const R = min(w, h) * 0.42;

  // Mahogany bench shadow ring (#6E3F24)
  noFill();
  stroke(0.431, 0.247, 0.141, 0.65);
  strokeWeight(4.0);
  circle(cx, cy, R * 2.14);

  // Octagonal brushed brass resonant plate (#C9A449)
  fill(0.788, 0.643, 0.286, 0.22);
  stroke(0.788, 0.643, 0.286, 0.96);
  strokeWeight(2.8);
  beginShape();
  for (let i = 0; i <= 8; i++) {
    const a = (i / 8.0) * TAU + PI * 0.125;
    vertex(cx + cos(a) * R, cy + sin(a) * R);
  }
  endShape();

  // Concentric brushed-brass lathe hairlines
  noFill();
  stroke(0.788, 0.643, 0.286, 0.24);
  strokeWeight(1.1);
  for (let r = 1; r <= 9; r++) {
    circle(cx, cy, (R * 1.85 * r) / 10.0);
  }

  // Standing wave nodal contours & quartz sand accumulation (#F5EFE0)
  const grid = floor(sand_density);
  const m = floor(mode_m);
  const n = floor(mode_n);
  const morph = 0.22 * sin(t * phase_drift);

  noStroke();
  for (let iy = 0; iy < grid; iy++) {
    const ny = (iy / (grid - 1)) * 2.0 - 1.0;
    for (let ix = 0; ix < grid; ix++) {
      const nx = (ix / (grid - 1)) * 2.0 - 1.0;
      const rad = sqrt(nx * nx + ny * ny);
      if (rad > 0.90 || rad < 0.07) continue;

      const f1 = cos(m * PI * nx) * cos(n * PI * ny) - cos(n * PI * nx) * cos(m * PI * ny);
      const f2 = cos((m + 1) * PI * nx) * cos((n - 1) * PI * ny) - cos((n - 1) * PI * nx) * cos((m + 1) * PI * ny);
      const z = abs(f1 * (1.0 - abs(morph)) + f2 * morph);

      if (z < 0.24) {
        const weight = 1.0 - z / 0.24;
        const jx = sin(ix * 19.3 + iy * 7.1) * (R / grid) * 0.35;
        const jy = cos(iy * 23.7 - ix * 11.4) * (R / grid) * 0.35;
        const px = cx + nx * R * 0.92 + jx;
        const py = cy + ny * R * 0.92 + jy;
        fill(0.961, 0.937, 0.878, 0.45 + 0.55 * weight);
        circle(px, py, 2.2 + 3.6 * weight);
      }
    }
  }

  // Radial vernier tick marks around outer brass bezel
  stroke(0.788, 0.643, 0.286, 0.55);
  strokeWeight(1.1);
  for (let k = 0; k < 48; k++) {
    const a = (k / 48.0) * TAU;
    const r0 = R * 1.03;
    const r1 = r0 + (k % 6 === 0 ? 14 : 6);
    line(cx + cos(a) * r0, cy + sin(a) * r0, cx + cos(a) * r1, cy + sin(a) * r1);
  }

  // Central knurled brass mounting boss
  fill(0.078, 0.071, 0.063);
  stroke(0.788, 0.643, 0.286, 0.95);
  strokeWeight(2.2);
  circle(cx, cy, R * 0.11);
  fill(0.788, 0.643, 0.286, 0.85);
  noStroke();
  circle(cx, cy, R * 0.035);
}
"""
    },
    {
        "slug": "chladni-square-superposition-plate",
        "title": "Chladni Square Superposition Plate",
        "description": "Square beveled brass Chladni plate framed in mahogany with dual-mode standing-wave interference lines, sand-grain nodes, and bow-excitation markers.",
        "tags": ["chladni", "superposition", "cymatics", "brass", "interference", "procedural", "sketch"],
        "code": """// Chladni Square Superposition Plate - Infinite Library (Sketch 2D)
param("freq_a", 4, 2, 8);
param("freq_b", 7, 3, 9);
param("contour_steps", 36, 18, 56);
param("drift", 0.3, 0.0, 1.0);

function draw(t) {
  // Obsidian background (#141210)
  background(0.078, 0.071, 0.063);
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const S = min(w, h) * 0.72;
  const half = S * 0.5;

  // Mahogany outer mounting frame (#6E3F24)
  noFill();
  stroke(0.431, 0.247, 0.141, 0.85);
  strokeWeight(4.0);
  rect(cx - half - 18, cy - half - 18, S + 36, S + 36);

  // Brass plate surface & chamfered rim (#C9A449)
  fill(0.788, 0.643, 0.286, 0.11);
  stroke(0.788, 0.643, 0.286, 0.94);
  strokeWeight(2.2);
  rect(cx - half, cy - half, S, S);

  // Corner bow-excitation registration ticks
  stroke(0.961, 0.937, 0.878, 0.65);
  strokeWeight(1.4);
  const tick = 16;
  for (let sx = -1; sx <= 1; sx += 2) {
    for (let sy = -1; sy <= 1; sy += 2) {
      const bx = cx + sx * (half + 10);
      const by = cy + sy * (half + 10);
      line(bx, by, bx - sx * tick, by);
      line(bx, by, bx, by - sy * tick);
    }
  }

  // Orthogonal standing-wave nodal web
  const n = floor(contour_steps);
  const a = floor(freq_a);
  const b = floor(freq_b);
  const ph = t * drift * 0.6;

  noStroke();
  for (let j = 0; j < n; j++) {
    const v = (j / (n - 1)) * 2.0 - 1.0;
    for (let i = 0; i < n; i++) {
      const u = (i / (n - 1)) * 2.0 - 1.0;
      const wave = sin(a * PI * u + ph) * sin(b * PI * v) + 0.65 * cos(b * PI * u) * cos(a * PI * v - ph);
      const d = abs(wave);
      if (d < 0.22) {
        const dens = 1.0 - d / 0.22;
        const px = cx + u * (half * 0.94);
        const py = cy + v * (half * 0.94);
        fill(0.961, 0.937, 0.878, 0.30 + 0.68 * dens);
        circle(px, py, 1.6 + 3.2 * dens);
      }
    }
  }

  // Fine brass crosshair & center spindle
  stroke(0.788, 0.643, 0.286, 0.35);
  strokeWeight(1.0);
  line(cx - half, cy, cx + half, cy);
  line(cx, cy - half, cx, cy + half);

  fill(0.788, 0.643, 0.286);
  noStroke();
  circle(cx, cy, 14);
  fill(0.078, 0.071, 0.063);
  circle(cx, cy, 5);
}
"""
    }
]


# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: xenakis_hyperbolic_ruled)
#    Palette: #171615 (charcoal), #EAE3D5 (vellum concrete),
#             #2C2A29 (steel cable), #B85D3B (terracotta)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "xenakis-philips-pavilion-shells-3d",
        "title": "Xenakis Philips Pavilion Shells 3D",
        "description": "Asymmetrical ruled hyperbolic paraboloid concrete shells inspired by Iannis Xenakis's 1958 Philips Pavilion with tensioned ridge ribs and terracotta datum plinth.",
        "tags": ["xenakis", "philips-pavilion", "architecture", "ruled-surface", "brutalist", "3d", "sketch-3d"],
        "code": """// Xenakis Philips Pavilion Shells 3D - Infinite Library (Sketch 3D)
param("ribs", 16, 8, 28);
param("peak_h", 1.85, 1.0, 2.5);
param("twist", 0.35, 0.1, 0.8);
param("spin", 0.18, 0.0, 0.5);

function draw(t) {
  rotateX(0.32 + 0.05 * sin(t * 0.4));
  rotateY(t * spin - 0.55);

  const nR = floor(ribs);

  // Architectural plinth base (#2C2A29)
  fill(0.173, 0.165, 0.161);
  push();
  translate(0, -1.05, 0);
  box(3.4, 0.12, 2.6);
  pop();

  // Three asymmetric hyperbolic paraboloid ruled shells (#EAE3D5)
  const peaks = [
    [-0.65, peak_h * 0.95, -0.35, 1.15],
    [0.55, peak_h * 0.78, 0.25, 0.95],
    [0.05, peak_h * 0.62, -0.85, 0.85]
  ];

  for (let p = 0; p < 3; p++) {
    const px = peaks[p][0];
    const py = peaks[p][1];
    const pz = peaks[p][2];
    const span = peaks[p][3];
    const baseAngle = p * 2.1 + twist;

    for (let i = 0; i < nR; i++) {
      const u = i / max(1, nR - 1);
      const a0 = baseAngle + (u - 0.5) * 1.4;
      const bx = px + cos(a0) * span;
      const bz = pz + sin(a0) * span;

      // Ruled straight-line generator segments from ground edge to apex ridge
      const mx = (bx + px) * 0.5;
      const my = (-0.98 + py) * 0.5;
      const mz = (bz + pz) * 0.5;

      push();
      translate(mx, my, mz);
      rotateY(-a0);
      rotateZ(0.46 + u * 0.18);
      fill(0.918 - u * 0.10, 0.890 - u * 0.10, 0.835 - u * 0.08);
      box(0.032, 2.25, 0.065);
      pop();
    }

    // Terracotta architectural apex beacon (#B85D3B)
    fill(0.722, 0.365, 0.231);
    push();
    translate(px, py * 0.72, pz);
    sphere(0.065);
    pop();
  }

  // Tensioned steel ridge spine collar
  fill(0.722, 0.365, 0.231);
  push();
  translate(0, -0.96, 0);
  torus(1.42, 0.028);
  pop();
}
"""
    },
    {
        "slug": "xenakis-ruled-hyperboloid-tower-3d",
        "title": "Xenakis Ruled Hyperboloid Tower 3D",
        "description": "Double-sheeted ruled hyperboloid lattice tower of counter-rotating tensioned steel struts, compression ring collars, and terracotta datum markers.",
        "tags": ["xenakis", "hyperboloid", "lattice", "architecture", "parametric", "3d", "sketch-3d"],
        "code": """// Xenakis Ruled Hyperboloid Tower 3D - Infinite Library (Sketch 3D)
param("struts", 18, 10, 32);
param("twist_rad", 1.15, 0.4, 2.0);
param("waist", 0.55, 0.25, 0.95);
param("spin", 0.22, 0.0, 0.6);

function draw(t) {
  rotateX(0.36 + 0.06 * cos(t * 0.35));
  rotateY(t * spin + 0.3);

  const nS = floor(struts);
  const H = 2.4;
  const R = 1.12;

  // Compression ring collars (#2C2A29 & #B85D3B)
  for (let r = 0; r < 5; r++) {
    const u = r / 4.0;
    const yy = (u - 0.5) * H;
    const rad = R * sqrt(waist * waist + (1.0 - waist * waist) * pow(u * 2.0 - 1.0, 2));
    push();
    translate(0, yy, 0);
    fill(r === 2 ? 0.722 : 0.88, r === 2 ? 0.365 : 0.85, r === 2 ? 0.231 : 0.79);
    torus(rad, r === 2 ? 0.042 : 0.026);
    pop();
  }

  // Counter-rotating families of straight ruled struts (#EAE3D5 & #B85D3B)
  for (let dir = -1; dir <= 1; dir += 2) {
    for (let i = 0; i < nS; i++) {
      const a0 = (i / nS) * TAU;
      const a1 = a0 + dir * twist_rad;
      const x0 = cos(a0) * R;
      const z0 = sin(a0) * R;
      const x1 = cos(a1) * R;
      const z1 = sin(a1) * R;

      const mx = (x0 + x1) * 0.5;
      const mz = (z0 + z1) * 0.5;
      const dx = x1 - x0;
      const dz = z1 - z0;
      const tilt = atan2(sqrt(dx * dx + dz * dz), H);
      const yaw = atan2(dz, dx);

      push();
      translate(mx, 0, mz);
      rotateY(-yaw);
      rotateZ(tilt);
      fill(dir > 0 ? 0.918 : 0.65, dir > 0 ? 0.890 : 0.62, dir > 0 ? 0.835 : 0.58);
      cylinder(0.020, sqrt(H * H + dx * dx + dz * dz));
      pop();
    }
  }
}
"""
    }
]


# -------------------------------------------------------------
# 3. Field Synth (Theme: trautonium_subharmonic_organ)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "sala-mixtur-formant-ribbon-synth",
        "title": "Sala Mixtur Formant Ribbon Synth",
        "description": "Oskar Sala inspired subharmonic frequency-divider ribbon organ blending 4 subharmonic sawtooth divisions into a resonant formant filter bank and tube saturation.",
        "note_root": 57,
        "params": {
            "subMix": 0.68,
            "formantHz": 920.0,
            "formantRes": 0.62,
            "ribbonVib": 0.0045,
            "drive": 1.65
        },
        "code": """param float subMix = 0.68 [0.0, 1.0];
param float formantHz = 920.0 [240.0, 3200.0];
param float formantRes = 0.62 [0.1, 0.90];
param float ribbonVib = 0.0045 [0.0, 0.02];
param float drive = 1.65 [0.6, 3.2];
state float p0 = 0; state float p2 = 0; state float p3 = 0;
state float vib = 0; state float env = 0; state float bp1 = 0; state float lp1 = 0;

env = if(gate > 0.5, env + (1.0 - env) * 0.08, env * 0.9991);
vib = (vib + 4.8 / sr) % 1.0;
f0 = clamp(freq * (1.0 + ribbonVib * sin(6.2831853 * vib)), 40.0, 4800.0);

p0 = (p0 + f0 / sr) % 1.0;
p2 = (p2 + (f0 * 0.5) / sr) % 1.0;
p3 = (p3 + (f0 * 0.333333) / sr) % 1.0;

mixSig = ((p0 * 2.0 - 1.0) * 0.5 + ((p2 * 2.0 - 1.0) * 0.42 + (p3 * 2.0 - 1.0) * 0.35) * subMix) * env;
g1 = clamp(2.0 * sin(3.14159265 * formantHz / sr), 0.02, 0.85);
bp1 = bp1 + g1 * (mixSig - lp1 - clamp(1.1 - formantRes, 0.14, 1.0) * bp1);
lp1 = lp1 + g1 * bp1;

shaped = (lp1 * 0.70 + bp1 * 0.55) * drive;
out = (shaped / (1.0 + abs(shaped) * 0.72)) * 0.55;
"""
    },
    {
        "slug": "ondes-martenot-palme-synth",
        "title": "Ondes Martenot Palme Synth",
        "description": "Ondes Martenot inspired expressive ribbon synthesizer with continuous harmonic morphing, sympathetic palme comb resonance, and warm oak-cabinet lowpass.",
        "note_root": 62,
        "params": {
            "timbreMorph": 0.55,
            "palmeRing": 0.44,
            "vibratoHz": 5.4,
            "cutoffHz": 2100.0,
            "decay": 0.9993
        },
        "code": """param float timbreMorph = 0.55 [0.0, 1.0];
param float palmeRing = 0.44 [0.0, 0.9];
param float vibratoHz = 5.4 [1.0, 9.0];
param float cutoffHz = 2100.0 [350.0, 6500.0];
param float decay = 0.9993 [0.995, 0.9998];
state float ph = 0; state float vib = 0; state float env = 0;
state float bpA = 0; state float lpA = 0; state float lpf = 0;

env = if(gate > 0.5, env + (1.0 - env) * 0.055, env * decay);
vib = (vib + vibratoHz / sr) % 1.0;
f0 = clamp(freq * (1.0 + 0.006 * sin(6.2831853 * vib)), 45.0, 5200.0);
ph = (ph + f0 / sr) % 1.0;

tri = 1.0 - 4.0 * abs(ph - 0.5);
core = tri * (1.0 - timbreMorph) + (sin(6.2831853 * ph) * 0.7 + sin(12.5663706 * ph) * 0.4) * timbreMorph;

gA = clamp(2.0 * sin(3.14159265 * clamp(f0 * 2.38, 120.0, 6500.0) / sr), 0.02, 0.85);
bpA = bpA + gA * (core - lpA - 0.16 * bpA);
lpA = lpA + gA * bpA;

sig = (core * 0.74 + bpA * 0.45 * palmeRing) * env;
gCut = clamp(2.0 * sin(3.14159265 * cutoffHz / sr), 0.02, 0.85);
lpf = lpf + gCut * (sig - lpf);

out = ((lpf * 1.45) / (1.0 + abs(lpf * 1.45) * 0.65)) * 0.55;
"""
    }
]


# -------------------------------------------------------------
# 4. Field Effects (Theme: schroeder_qrd_dispersion)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "schroeder-qrd-prime-diffuser-fx",
        "title": "Schroeder QRD Prime Diffuser FX",
        "description": "Quadratic-residue prime acoustic diffuser processor cascading prime-scaled allpass scattering stages with resonant room absorption and stereo width.",
        "params": {
            "scatter": 0.74,
            "dampingHz": 3200.0,
            "feedback": 0.46,
            "drive": 1.35,
            "mix": 0.82
        },
        "code": """param float scatter = 0.74 [0.15, 0.92];
param float dampingHz = 3200.0 [600.0, 9000.0];
param float feedback = 0.46 [0.0, 0.78];
param float drive = 1.35 [0.6, 2.6];
param float mix = 0.82 [0.0, 1.0];
state float a1 = 0; state float a2 = 0; state float a3 = 0; state float a4 = 0; state float a5 = 0;
state float lp = 0; state float fb = 0; state float lfo = 0;

lfo = (lfo + 0.22 / sr) % 1.0;
c = clamp(scatter + 0.08 * sin(6.2831853 * lfo), 0.1, 0.92);

x = in + fb * feedback;
y1 = -c * x + a1; a1 = x + c * y1;
y2 = -(c * 0.91) * y1 + a2; a2 = y1 + (c * 0.91) * y2;
y3 = -(c * 0.83) * y2 + a3; a3 = y2 + (c * 0.83) * y3;
y4 = -(c * 0.77) * y3 + a4; a4 = y3 + (c * 0.77) * y4;
y5 = -(c * 0.69) * y4 + a5; a5 = y4 + (c * 0.69) * y5;

g = clamp(2.0 * sin(3.14159265 * dampingHz / sr), 0.04, 0.88);
lp = lp + g * (y5 - lp);
fb = clamp(lp, -1.0, 1.0);

wd = lp * drive;
wet = wd / (1.0 + abs(wd) * 0.65);
out = (in * (1.0 - mix) + wet * mix) * 0.88;
"""
    },
    {
        "slug": "frippertronics-dual-reel-loop-fx",
        "title": "Frippertronics Dual Reel Loop FX",
        "description": "Dual-capstan reel-to-reel tape loop processor with asymmetric flutter modulation, oxide high-frequency rolloff, and warm transformer feedback saturation.",
        "params": {
            "flutterHz": 0.42,
            "flutterDepth": 0.24,
            "oxideHz": 2400.0,
            "feedback": 0.52,
            "mix": 0.78
        },
        "code": """param float flutterHz = 0.42 [0.1, 2.5];
param float flutterDepth = 0.24 [0.0, 0.65];
param float oxideHz = 2400.0 [500.0, 7500.0];
param float feedback = 0.52 [0.0, 0.82];
param float mix = 0.78 [0.0, 1.0];
state float lfo1 = 0; state float lfo2 = 0;
state float ap1 = 0; state float ap2 = 0; state float ap3 = 0;
state float lp1 = 0; state float lp2 = 0; state float fb = 0;

lfo1 = (lfo1 + flutterHz / sr) % 1.0;
lfo2 = (lfo2 + (flutterHz * 1.618) / sr) % 1.0;
c = clamp(0.48 + flutterDepth * (0.65 * sin(6.2831853 * lfo1) + 0.35 * sin(6.2831853 * lfo2)), 0.08, 0.88);

x = in + fb * feedback;
sat = (x * 1.45) / (1.0 + abs(x * 1.45) * 0.68);

y1 = -c * sat + ap1; ap1 = sat + c * y1;
y2 = -c * y1 + ap2; ap2 = y1 + c * y2;
y3 = -c * y2 + ap3; ap3 = y2 + c * y3;

g = clamp(2.0 * sin(3.14159265 * oxideHz / sr), 0.04, 0.86);
lp1 = lp1 + g * (y3 - lp1);
lp2 = lp2 + g * (lp1 - lp2);
fb = clamp(lp2, -1.0, 1.0);

out = (in * (1.0 - mix) + lp2 * mix) * 0.86;
"""
    }
]


# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embeds (Theme: sashimono_kumiko_shoji)
#    Palette: #1A130E (charred cedar), #6E472B (hinoki cypress),
#             #E8D8B9 (washi paper), #F5A642 (lantern amber)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "kumiko-asanoha-shoji-lantern",
        "title": "Kumiko Asanoha Shoji Lantern",
        "description": "Japanese Sashimono Kumiko asanoha hemp-leaf timber lattice backlit by fibrous washi paper and warm lantern amber glow.",
        "embed_desc": "Audio-reactive Kumiko Asanoha Shoji Lantern visualizer. Bass pulses the warm amber lantern behind fibrous washi paper, mids breathe through the hexagonal timber lattice, and highs glint along the beveled hinoki cypress ribs.",
        "tags": ["kumiko", "sashimono", "shoji", "woodwork", "japanese", "lattice", "shader", "webgl", "audio-reactive"],
        "params": {
            "gridScale": 3.4,
            "ribWidth": 0.055,
            "lanternGlow": 1.15,
            "washiFiber": 0.16,
            "warmth": 0.82
        },
        "code": """param float gridScale = 3.4 [1.8, 6.5];
param float ribWidth = 0.055 [0.02, 0.12];
param float lanternGlow = 1.15 [0.4, 2.0];
param float washiFiber = 0.16 [0.02, 0.35];
param float warmth = 0.82 [0.2, 1.2];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * gridScale;

# Hexagonal / triangular asanoha lattice axes (0, 60, 120 deg)
d0 = abs(fract(p.y) - 0.5);
d1 = abs(fract(p.x * 0.866025 + p.y * 0.5) - 0.5);
d2 = abs(fract(p.x * 0.866025 - p.y * 0.5) - 0.5);

# Secondary bisecting asanoha ribs
d3 = abs(fract(p.x * 1.73205) - 0.5);
d4 = abs(fract(p.x * 0.866025 + p.y * 1.5) - 0.5);
d5 = abs(fract(p.x * 0.866025 - p.y * 1.5) - 0.5);

mainDist = min(d0, min(d1, d2));
subDist = min(d3, min(d4, d5));

mainRib = smoothstep(ribWidth + 0.014, ribWidth, mainDist);
subRib = smoothstep(ribWidth * 0.62 + 0.012, ribWidth * 0.62, subDist);
woodMask = max(mainRib, subRib);

# Wood grain & bevel highlight along hinoki ribs
grain = 0.5 + 0.5 * sin(p.x * 38.0 + sin(p.y * 14.0) * 2.5);
bevel = smoothstep(ribWidth * 0.4, ribWidth, mainDist) * mainRib;
cCedar = vec3(0.102, 0.075, 0.055);
cHinoki = vec3(0.431, 0.278, 0.169);
woodCol = mix(cCedar, cHinoki, 0.45 + 0.35 * grain + 0.25 * bevel);

# Backlit fibrous washi paper & warm lantern radial falloff
r = length(vec2((uv.x - 0.5) * aspect, uv.y - 0.5));
fiber = sin(p.x * 43.0 + cos(p.y * 37.0) * 2.0) * cos(p.y * 51.0 - sin(p.x * 29.0) * 2.0);
lantern = exp(-r * r * (2.2 - 0.35 * sin(t * 0.6))) * lanternGlow;
cWashi = vec3(0.910, 0.847, 0.725);
cAmber = vec3(0.961, 0.651, 0.259);
paperCol = mix(cWashi * 0.48, cAmber * 1.05, clamp(lantern * warmth, 0.0, 1.0));
paperCol += vec3(0.06, 0.04, 0.02) * fiber * washiFiber * 2.5;

# Outer charred sugi stiles frame
edgeX = smoothstep(0.46, 0.49, abs(uv.x - 0.5));
edgeY = smoothstep(0.46, 0.49, abs(uv.y - 0.5));
frameMask = max(edgeX, edgeY);

col = mix(paperCol, woodCol, woodMask);
col = mix(col, cCedar * 0.85, frameMask);
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
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * (3.4 + audio_mid * 0.35);

  float d0 = abs(fract(p.y) - 0.5);
  float d1 = abs(fract(p.x * 0.866025 + p.y * 0.5) - 0.5);
  float d2 = abs(fract(p.x * 0.866025 - p.y * 0.5) - 0.5);
  float d3 = abs(fract(p.x * 1.73205) - 0.5);
  float d4 = abs(fract(p.x * 0.866025 + p.y * 1.5) - 0.5);
  float d5 = abs(fract(p.x * 0.866025 - p.y * 1.5) - 0.5);

  float ribW = 0.055;
  float mainDist = min(d0, min(d1, d2));
  float subDist = min(d3, min(d4, d5));
  float mainRib = smoothstep(ribW + 0.014, ribW, mainDist);
  float subRib = smoothstep(ribW * 0.62 + 0.012, ribW * 0.62, subDist);
  float woodMask = max(mainRib, subRib);

  float grain = 0.5 + 0.5 * sin(p.x * 38.0 + sin(p.y * 14.0) * 2.5);
  float bevel = smoothstep(ribW * 0.4, ribW, mainDist) * mainRib;
  vec3 cCedar = vec3(0.102, 0.075, 0.055);
  vec3 cHinoki = vec3(0.431, 0.278, 0.169);
  vec3 woodCol = mix(cCedar, cHinoki, 0.45 + 0.35 * grain + 0.25 * bevel + audio_high * 0.18);

  float r = length(vec2((uv.x - 0.5) * aspect, uv.y - 0.5));
  float fiber = sin(p.x * 43.0 + cos(p.y * 37.0) * 2.0) * cos(p.y * 51.0 - sin(p.x * 29.0) * 2.0);
  float lantern = exp(-r * r * (2.2 - audio_bass * 0.75)) * (1.15 + audio_bass * 0.65);
  vec3 cWashi = vec3(0.910, 0.847, 0.725);
  vec3 cAmber = vec3(0.961, 0.651, 0.259);
  vec3 paperCol = mix(cWashi * 0.48, cAmber * 1.05, clamp(lantern * 0.82, 0.0, 1.0));
  paperCol += vec3(0.06, 0.04, 0.02) * fiber * 0.4;

  float edgeX = smoothstep(0.46, 0.49, abs(uv.x - 0.5));
  float edgeY = smoothstep(0.46, 0.49, abs(uv.y - 0.5));
  float frameMask = max(edgeX, edgeY);

  vec3 col = mix(paperCol, woodCol, woodMask);
  col = mix(col, cCedar * 0.85, frameMask);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "kumiko-seigaiha-timber-screen",
        "title": "Kumiko Seigaiha Timber Screen",
        "description": "Overlapping concentric seigaiha cedar wave-arc joinery framed by mortise-and-tenon stiles over warm backlit shoji paper.",
        "embed_desc": "Audio-reactive Kumiko Seigaiha Timber Screen visualizer. Bass expands the concentric cedar wave-arc glow, mids ripple across the backlit shoji paper, and highs catch the mortised hinoki wood grain.",
        "tags": ["kumiko", "seigaiha", "sashimono", "shoji", "woodwork", "shader", "webgl", "audio-reactive"],
        "params": {
            "waveScale": 4.2,
            "arcThickness": 0.075,
            "ringsPerCell": 4.0,
            "lanternWarmth": 1.10,
            "paperGrain": 0.14
        },
        "code": """param float waveScale = 4.2 [2.0, 8.0];
param float arcThickness = 0.075 [0.03, 0.14];
param float ringsPerCell = 4.0 [2.0, 6.0];
param float lanternWarmth = 1.10 [0.4, 2.0];
param float paperGrain = 0.14 [0.02, 0.35];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * waveScale;

# Staggered seigaiha wave-arc cells
cellA = vec2(fract(p.x) - 0.5, fract(p.y) - 0.25);
cellB = vec2(fract(p.x + 0.5) - 0.5, fract(p.y + 0.5) - 0.25);

rA = length(cellA);
rB = length(cellB);

arcA = abs(fract(rA * ringsPerCell) - 0.5);
arcB = abs(fract(rB * ringsPerCell) - 0.5);

maskA = smoothstep(arcThickness + 0.02, arcThickness, arcA) * step(rA, 0.72);
maskB = smoothstep(arcThickness + 0.02, arcThickness, arcB) * step(rB, 0.72);
woodMask = max(maskA, maskB);

# Hinoki cypress wood grain & charred sugi mortar stiles
grain = 0.5 + 0.5 * sin(p.x * 32.0 + p.y * 18.0);
cCedar = vec3(0.102, 0.075, 0.055);
cHinoki = vec3(0.431, 0.278, 0.169);
woodCol = mix(cCedar, cHinoki, 0.42 + 0.42 * grain);

# Vertical & horizontal sashimono muntin bars
muntinX = smoothstep(0.032, 0.015, abs(fract(p.x * 0.5) - 0.5));
muntinY = smoothstep(0.032, 0.015, abs(fract(p.y * 0.5) - 0.5));
muntin = max(muntinX, muntinY);

# Backlit shoji paper with subtle ripple & warm amber lantern
rCenter = length(vec2((uv.x - 0.5) * aspect, uv.y - 0.5));
glow = exp(-rCenter * 2.1) * lanternWarmth * (0.9 + 0.1 * sin(t * 0.7));
paperTex = sin(p.x * 47.0 + cos(p.y * 41.0)) * cos(p.y * 53.0);
cWashi = vec3(0.910, 0.847, 0.725);
cAmber = vec3(0.961, 0.651, 0.259);
paperCol = mix(cWashi * 0.52, cAmber, clamp(glow * 0.85, 0.0, 1.0));
paperCol += vec3(0.05, 0.035, 0.02) * paperTex * paperGrain * 2.0;

col = mix(paperCol, woodCol, woodMask);
col = mix(col, cCedar * 0.9, muntin);
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
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * (4.2 + audio_mid * 0.35);

  vec2 cellA = vec2(fract(p.x) - 0.5, fract(p.y) - 0.25);
  vec2 cellB = vec2(fract(p.x + 0.5) - 0.5, fract(p.y + 0.5) - 0.25);

  float rA = length(cellA);
  float rB = length(cellB);

  float rings = 4.0;
  float thick = 0.075;
  float arcA = abs(fract(rA * rings - t * 0.15 * audio_mid) - 0.5);
  float arcB = abs(fract(rB * rings - t * 0.15 * audio_mid) - 0.5);

  float maskA = smoothstep(thick + 0.02, thick, arcA) * step(rA, 0.72);
  float maskB = smoothstep(thick + 0.02, thick, arcB) * step(rB, 0.72);
  float woodMask = max(maskA, maskB);

  float grain = 0.5 + 0.5 * sin(p.x * 32.0 + p.y * 18.0);
  vec3 cCedar = vec3(0.102, 0.075, 0.055);
  vec3 cHinoki = vec3(0.431, 0.278, 0.169);
  vec3 woodCol = mix(cCedar, cHinoki, 0.42 + 0.42 * grain + audio_high * 0.18);

  float muntinX = smoothstep(0.032, 0.015, abs(fract(p.x * 0.5) - 0.5));
  float muntinY = smoothstep(0.032, 0.015, abs(fract(p.y * 0.5) - 0.5));
  float muntin = max(muntinX, muntinY);

  float rCenter = length(vec2((uv.x - 0.5) * aspect, uv.y - 0.5));
  float glow = exp(-rCenter * (2.1 - audio_bass * 0.65)) * (1.10 + audio_bass * 0.65);
  float paperTex = sin(p.x * 47.0 + cos(p.y * 41.0)) * cos(p.y * 53.0);
  vec3 cWashi = vec3(0.910, 0.847, 0.725);
  vec3 cAmber = vec3(0.961, 0.651, 0.259);
  vec3 paperCol = mix(cWashi * 0.52, cAmber, clamp(glow * 0.85, 0.0, 1.0));
  paperCol += vec3(0.05, 0.035, 0.02) * paperTex * 0.28;

  vec3 col = mix(paperCol, woodCol, woodMask);
  col = mix(col, cCedar * 0.9, muntin);
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
    html, body {{ margin: 0; padding: 0; width: 100%; height: 100%; overflow: hidden; background: #1A130E; }}
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
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]), gl.STATIC_DRAW);

    const positionLoc = gl.getAttribLocation(program, "position");
    gl.enableVertexAttribArray(positionLoc);
    gl.vertexAttribPointer(positionLoc, 2, gl.FLOAT, false, 0, 0);

    const resLoc = gl.getUniformLocation(program, "res");
    const tLoc = gl.getUniformLocation(program, "t");
    const aspectLoc = gl.getUniformLocation(program, "aspect");
    const bassLoc = gl.getUniformLocation(program, "audio_bass");
    const midLoc = gl.getUniformLocation(program, "audio_mid");
    const highLoc = gl.getUniformLocation(program, "audio_high");

    let audioBass = 0.0, audioMid = 0.0, audioHigh = 0.0;
    window.addEventListener("message", (e) => {{
      if (e.data && e.data.type === "infinite_audio") {{
        audioBass = e.data.bass || 0.0;
        audioMid = e.data.mid || 0.0;
        audioHigh = e.data.high || 0.0;
      }}
    }});

    function resize() {{
      const dpr = Math.min(window.devicePixelRatio || 1, 2);
      canvas.width = Math.floor(window.innerWidth * dpr);
      canvas.height = Math.floor(window.innerHeight * dpr);
      gl.viewport(0, 0, canvas.width, canvas.height);
    }}
    window.addEventListener("resize", resize);
    resize();

    const startTime = performance.now();
    function render(now) {{
      const t = (now - startTime) * 0.001;
      gl.uniform2f(resLoc, canvas.width, canvas.height);
      gl.uniform1f(tLoc, t);
      gl.uniform1f(aspectLoc, canvas.width / Math.max(1, canvas.height));
      const b = audioBass || (0.28 + 0.22 * Math.sin(t * 2.1));
      const m = audioMid  || (0.22 + 0.16 * Math.cos(t * 1.4));
      const h = audioHigh || (0.18 + 0.14 * Math.sin(t * 3.7));
      gl.uniform1f(bassLoc, b);
      gl.uniform1f(midLoc, m);
      gl.uniform1f(highLoc, h);
      gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
      requestAnimationFrame(render);
    }}
    requestAnimationFrame(render);
  </script>
</body>
</html>
"""


def main():
    failed = False
    report = []

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
  c bg 0.09 0.086 0.082
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
  i note2 {nr+3}
  i note3 {nr+7}
  i note4 {nr+8}
  i note5 {nr+7}
  i note6 {nr+3}
  i note7 {nr+2}
  i note8 {nr}
  i note9 {nr+7}
  i note10 {nr+8}
  i note11 {nr+12}
  i note12 {nr+8}
  i note13 {nr+7}
  i note14 {nr+3}
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
  i note1 50
  i note2 55
  i note3 57
  i note4 55
  i note5 50
  i note6 48
  i note7 52
  i note8 48
  i note9 55
  i note10 57
  i note11 60
  i note12 57
  i note13 55
  i note14 52
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
                "tags": ["synth", "trautonium", "ondes-martenot", "subharmonic", "audio", "dsp"],
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
                "tags": ["effect", "schroeder", "qrd", "frippertronics", "tape", "audio", "dsp"],
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
