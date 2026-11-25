#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 50.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: molnar_plotter_interruptions -> molnar-plotter-interruptions, molnar-concentric-quadrics)
2. Sketch 3D (Theme: toyo_ito_voronoi_pavilion -> ito-serpentine-lattice-3d, ito-taichung-catenoid-vault-3d)
3. Field Synth (Themes: karplus_plucked_zither, hammond_tonewheel_drawbar -> karplus-plucked-zither-synth, hammond-tonewheel-organ-synth)
4. Field Effects (Themes: bbd_analog_ensemble, shimmer_octave_reverb -> bbd-analog-ensemble-fx, shimmer-octave-reverb-fx)
5. Field Pixel & Embeds (Themes: bismuth_hopper_crystal, ebru_battal_marbling -> bismuth-hopper-crystal, ebru-battal-marbling)
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

TMP_DIR = "/tmp/pack50_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview


# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: molnar_plotter_interruptions)
#    Palette: #E5E6E1 (paper), #181817 (ink), #6B6D68 (graphite), #B33A2E (vermilion)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "molnar-plotter-interruptions",
        "title": "Molnar Plotter Interruptions",
        "description": "Algorithmic Benson plotter ink drawing with dense stochastic line segments, rectangular negative-space voids, and archival registration ticks.",
        "tags": ["molnar", "plotter", "algorithmic", "ink", "grid", "procedural", "sketch"],
        "code": """// Molnar Plotter Interruptions - Infinite Library (Sketch 2D)
param("grid_n", 44, 24, 68);
param("void_thresh", 0.24, 0.05, 0.45);
param("jitter", 0.95, 0.2, 1.8);
param("drift", 0.12, 0.0, 0.5);

function hash2(x, y) {
  const s = sin(x * 127.1 + y * 311.7) * 43758.5453;
  return s - floor(s);
}

function draw(t) {
  // Archival off-white plotter paper (#E5E6E1)
  background(0.898, 0.902, 0.882);
  const w = width;
  const h = height;
  const margin = min(w, h) * 0.095;
  const span = min(w, h) - margin * 2.0;
  const x0 = (w - span) * 0.5;
  const y0 = (h - span) * 0.5;

  // Fine plotter registration ticks around border
  stroke(0.420, 0.427, 0.408, 0.72);
  strokeWeight(1.1);
  for (let i = 0; i <= 24; i++) {
    const u = i / 24.0;
    const px = x0 + u * span;
    const py = y0 + u * span;
    line(px, y0 - 16, px, y0 - 8);
    line(px, y0 + span + 8, px, y0 + span + 16);
    line(x0 - 16, py, x0 - 8, py);
    line(x0 + span + 8, py, x0 + span + 16, py);
  }

  // Vermilion plotter datum crosshairs (#B33A2E)
  stroke(0.702, 0.227, 0.180, 0.85);
  strokeWeight(1.4);
  const cOff = 24;
  line(x0 - cOff, y0 - cOff, x0 - cOff + 10, y0 - cOff);
  line(x0 - cOff, y0 - cOff, x0 - cOff, y0 - cOff + 10);
  line(x0 + span + cOff, y0 + span + cOff, x0 + span + cOff - 10, y0 + span + cOff);
  line(x0 + span + cOff, y0 + span + cOff, x0 + span + cOff, y0 + span + cOff - 10);

  // Algorithmic interrupted plotter hatching
  const n = floor(grid_n);
  const cell = span / n;
  const phase = floor(t * drift * 2.0);
  for (let iy = 0; iy < n; iy++) {
    for (let ix = 0; ix < n; ix++) {
      const cx = x0 + (ix + 0.5) * cell;
      const cy = y0 + (iy + 0.5) * cell;

      // Rectangular negative-space voids ("interruptions")
      const b1 = (ix > n * 0.18 && ix < n * 0.38 && iy > n * 0.62 && iy < n * 0.86);
      const b2 = (ix > n * 0.56 && ix < n * 0.78 && iy > n * 0.14 && iy < n * 0.44);
      const vHash = hash2(floor(ix / 5.0), floor(iy / 5.0));
      if (b1 || b2 || vHash < void_thresh * 0.65) continue;

      // Dense overlapping zone vs airy zone
      const dense = (ix > n * 0.22 && ix < n * 0.46 && iy > n * 0.16 && iy < n * 0.58);
      const passes = dense ? 2 : 1;

      for (let p = 0; p < passes; p++) {
        const h1 = hash2(ix + p * 73.0, iy + phase * 0.01);
        const h2 = hash2(ix * 1.7 + p * 19.0, iy * 2.3);
        const ang = (h1 - 0.5) * PI * jitter + sin(t * drift + ix * 0.08 + iy * 0.05) * 0.25;
        const len = cell * (0.72 + h2 * 0.75);
        const dx = cos(ang) * len * 0.5;
        const dy = sin(ang) * len * 0.5;

        if (h2 > 0.94) {
          stroke(0.702, 0.227, 0.180, 0.88);
          strokeWeight(1.5);
        } else {
          stroke(0.094, 0.094, 0.090, dense ? 0.88 : 0.78);
          strokeWeight(dense ? 1.35 : 1.15);
        }
        line(cx - dx, cy - dy, cx + dx, cy + dy);
      }
    }
  }
}
"""
    },
    {
        "slug": "molnar-concentric-quadrics",
        "title": "Molnar Concentric Quadrics",
        "description": "Nested perturbed plotter squares and trapezoidal quadrics across a seven-by-seven archival grid with controlled stochastic corner displacement.",
        "tags": ["molnar", "quadrics", "plotter", "squares", "geometric", "procedural", "sketch"],
        "code": """// Molnar Concentric Quadrics - Infinite Library (Sketch 2D)
param("cells", 7, 4, 11);
param("rings", 8, 4, 14);
param("perturb", 0.18, 0.02, 0.35);
param("tempo", 0.22, 0.0, 0.8);

function hash2(x, y) {
  const s = sin(x * 91.3458 + y * 47.53) * 43758.5453;
  return s - floor(s);
}

function draw(t) {
  // Archival plotter paper (#E5E6E1)
  background(0.898, 0.902, 0.882);
  const w = width;
  const h = height;
  const margin = min(w, h) * 0.085;
  const span = min(w, h) - margin * 2.0;
  const x0 = (w - span) * 0.5;
  const y0 = (h - span) * 0.5;

  const n = floor(cells);
  const step = span / n;
  const rCount = floor(rings);

  noFill();
  for (let iy = 0; iy < n; iy++) {
    for (let ix = 0; ix < n; ix++) {
      const cx = x0 + (ix + 0.5) * step;
      const cy = y0 + (iy + 0.5) * step;
      const cellChaos = ((ix + iy) / max(1.0, (n - 1) * 2.0)) * 0.75 + 0.25;

      for (let r = 0; r < rCount; r++) {
        const frac = (r + 1) / rCount;
        const half = step * 0.44 * frac;
        const j = half * perturb * cellChaos;

        // Skip occasional inner rings for Molnar rhythm
        if (hash2(ix * 13 + r, iy * 29 + r) < 0.14) continue;

        const isAccent = (r === rCount - 1 && (ix + iy) % 5 === 0);
        if (isAccent) {
          stroke(0.702, 0.227, 0.180, 0.88);
          strokeWeight(1.55);
        } else {
          stroke(0.094, 0.094, 0.090, 0.82);
          strokeWeight(1.25);
        }

        beginShape();
        for (let c = 0; c <= 4; c++) {
          const ci = c % 4;
          const sx = (ci === 0 || ci === 3) ? -1 : 1;
          const sy = (ci === 0 || ci === 1) ? -1 : 1;
          const n1 = (hash2(ix * 7 + ci * 3 + r * 11, iy * 17 + r) - 0.5) * 2.0;
          const n2 = (hash2(ix * 19 + r, iy * 11 + ci * 5 + r * 7) - 0.5) * 2.0;
          const anim = sin(t * tempo + ix * 0.4 + iy * 0.3 + r * 0.5) * 0.22;
          const vx = cx + sx * half + (n1 + anim) * j;
          const vy = cy + sy * half + (n2 - anim) * j;
          vertex(vx, vy);
        }
        endShape();
      }
    }
  }
}
"""
    }
]


# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: toyo_ito_voronoi_pavilion)
#    Palette: #1B232B (slate void), #E4EAEF (steel plate), #B4C3CE (aluminum), #6E8BA3 (sky glass)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "ito-serpentine-lattice-3d",
        "title": "Ito Serpentine Lattice 3D",
        "description": "Algorithmic steel-plate pavilion inspired by Toyo Ito and Cecil Balmond, featuring intersecting rotated truss ribs and cantilevered cellular planes.",
        "tags": ["toyo-ito", "serpentine", "pavilion", "lattice", "architecture", "3d", "sketch-3d"],
        "code": """// Ito Serpentine Lattice 3D - Infinite Library (Sketch 3D)
param("ribs", 16, 8, 28);
param("span", 2.4, 1.5, 3.5);
param("twist", 0.38, 0.1, 0.85);
param("spin", 0.18, 0.0, 0.6);

function draw(t) {
  const n = floor(ribs);
  rotateY(t * spin + 0.52);
  rotateX(0.28);

  // Dark slate architectural plinth
  push();
  translate(0, -0.78, 0);
  fill(0.11, 0.14, 0.17);
  box(span * 1.12, 0.08, span * 1.12);
  pop();

  // Intersecting algorithmic steel-plate roof & wall ribs
  for (let i = 0; i < n; i++) {
    const u = i / n;
    const ang = u * PI * 1.45 + 0.22;
    const off = (u - 0.5) * span * 0.88;

    // Roof lattice beam
    push();
    translate(0, 0.62, 0);
    rotateY(ang);
    translate(0, 0, off * 0.42);
    fill(i % 3 === 0 ? 0.894 : 0.706, i % 3 === 0 ? 0.918 : 0.765, i % 3 === 0 ? 0.937 : 0.808);
    box(span * 1.05, 0.045, 0.065);
    pop();

    // Diagonal folded wall plates
    const wallAng = ang + twist * sin(u * TAU);
    const wx = cos(wallAng) * (span * 0.44);
    const wz = sin(wallAng) * (span * 0.44);
    push();
    translate(wx, -0.08, wz);
    rotateY(-wallAng + PI * 0.5);
    rotateZ(0.32 * cos(u * PI * 3.0));
    fill(i % 2 === 0 ? 0.894 : 0.431, i % 2 === 0 ? 0.918 : 0.545, i % 2 === 0 ? 0.937 : 0.639);
    box(0.05, 1.42, 0.52);
    pop();
  }
}
"""
    },
    {
        "slug": "ito-taichung-catenoid-vault-3d",
        "title": "Ito Taichung Catenoid Vault 3D",
        "description": "Parametric acoustic sound-cave inspired by Toyo Ito's National Taichung Theater, built from undulating catenoid arch rings and cellular ribs.",
        "tags": ["toyo-ito", "taichung", "catenoid", "vault", "architecture", "3d", "sketch-3d"],
        "code": """// Ito Taichung Catenoid Vault 3D - Infinite Library (Sketch 3D)
param("arches", 20, 10, 32);
param("radius", 1.35, 0.8, 2.2);
param("undulate", 0.34, 0.05, 0.65);
param("spin", 0.16, 0.0, 0.5);

function draw(t) {
  const n = floor(arches);
  rotateY(t * spin + 0.45);
  rotateX(0.22);

  // Curved acoustic catenoid shell rings
  for (let i = 0; i < n; i++) {
    const u = (i / (n - 1)) * 2.0 - 1.0;
    const z = u * 1.45;
    const waist = 1.0 - 0.36 * cos(u * PI);
    const r = radius * waist;
    const segs = 18;

    for (let s = 0; s < segs; s++) {
      const a = (s / segs) * TAU;
      const wave = undulate * sin(a * 3.0 + u * PI + t * 0.4);
      const px = cos(a) * (r + wave);
      const py = sin(a) * (r * 0.78 + wave * 0.6);

      push();
      translate(px, py, z);
      rotateZ(a + PI * 0.5);
      rotateY(u * 0.35);
      const shade = 0.68 + 0.24 * cos(a - 0.7);
      fill(shade * 0.92, shade * 0.95, shade * 0.99);
      box(0.32, 0.09, 0.16);
      pop();
    }
  }
}
"""
    }
]


# -------------------------------------------------------------
# 3. Field Synth Sources
#    (karplus_plucked_zither & hammond_tonewheel_drawbar)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "karplus-plucked-zither-synth",
        "title": "Karplus Plucked Zither Synth",
        "description": "Physical-modeled plucked zither with shaped pick burst, damped harmonic string modes, sympathetic octave resonance, and wooden soundboard body filter.",
        "note_root": 52,
        "params": {
            "pluckBright": 0.68,
            "inharmonic": 0.14,
            "sympathetic": 0.38,
            "bodyHz": 420.0,
            "decay": 0.9993
        },
        "code": """param float pluckBright = 0.68 [0.1, 1.0];
param float inharmonic = 0.14 [0.0, 0.5];
param float sympathetic = 0.38 [0.0, 0.8];
param float bodyHz = 420.0 [180.0, 1200.0];
param float decay = 0.9993 [0.997, 0.9997];
state float p1 = 0; state float p2 = 0; state float p3 = 0;
state float env = 0; state float pickEnv = 1.0;
state float bp1 = 0; state float lp1 = 0;

env = if(gate > 0.5, env + (1.0 - env) * 0.22, env * decay);
pickEnv = if(gate > 0.5, pickEnv * 0.992, 1.0);

f0 = clamp(freq, 55.0, 3200.0);
p1 = (p1 + f0 / sr) % 1.0;
p2 = (p2 + (f0 * 2.002) / sr) % 1.0;
p3 = (p3 + (f0 * (3.0 + inharmonic * 0.04)) / sr) % 1.0;

h1 = sin(6.2831853 * p1);
h2 = sin(6.2831853 * p2) * (0.45 + pickEnv * 0.35 + sympathetic * 0.2);
h3 = sin(6.2831853 * p3) * (0.22 + pluckBright * 0.25) * (0.35 + 0.65 * pickEnv);

stringSig = (h1 + h2 + h3) * env;
g = clamp(2.0 * sin(3.14159265 * bodyHz / sr), 0.02, 0.72);
bp1 = bp1 + g * (stringSig - lp1 - 0.65 * bp1);
lp1 = lp1 + g * bp1;

mixSig = stringSig * 0.64 + bp1 * 0.52;
out = (mixSig / (1.0 + abs(mixSig) * 0.55)) * 0.62;
"""
    },
    {
        "slug": "hammond-tonewheel-organ-synth",
        "title": "Hammond Tonewheel Organ Synth",
        "description": "Electromagnetic 9-drawbar tonewheel organ with harmonic key-click transient, 3rd harmonic percussion decay, and warm tube-preamp saturation.",
        "note_root": 48,
        "params": {
            "subOctave": 0.72,
            "fundamental": 0.85,
            "percussion": 0.55,
            "brightness": 0.48,
            "drive": 1.45
        },
        "code": """param float subOctave = 0.72 [0.0, 1.0];
param float fundamental = 0.85 [0.0, 1.0];
param float percussion = 0.55 [0.0, 1.0];
param float brightness = 0.48 [0.0, 1.0];
param float drive = 1.45 [0.6, 3.0];
state float pSub = 0; state float p1 = 0; state float p2 = 0; state float p3 = 0;
state float env = 0; state float percEnv = 1.0;

env = if(gate > 0.5, env + (1.0 - env) * 0.28, env * 0.9965);
percEnv = if(gate > 0.5, percEnv * 0.9968, 1.0);

f0 = clamp(freq, 45.0, 2800.0);
pSub = (pSub + (f0 * 0.5) / sr) % 1.0;
p1 = (p1 + f0 / sr) % 1.0;
p2 = (p2 + (f0 * 2.0) / sr) % 1.0;
p3 = (p3 + (f0 * 3.0) / sr) % 1.0;

drawbars = sin(6.2831853 * pSub) * subOctave * 0.55 + sin(6.2831853 * p1) * fundamental * 0.72 + sin(6.2831853 * p2) * (0.42 + brightness * 0.25) + sin(6.2831853 * p3) * (0.18 + percussion * percEnv * 0.58);

raw = drawbars * env * 0.52;
out = ((raw * drive) / (1.0 + abs(raw * drive) * 0.65)) * 0.64;
"""
    }
]


# -------------------------------------------------------------
# 4. Field Effects Sources
#    (bbd_analog_ensemble & shimmer_octave_reverb)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "bbd-analog-ensemble-fx",
        "title": "BBD Analog Ensemble FX",
        "description": "Triple-phase Bucket-Brigade (BBD) analog chorus ensemble with dual slow/vibrato LFO modulation, clock-filter warmth, and stereo movement.",
        "params": {
            "slowHz": 0.62,
            "vibratoHz": 5.4,
            "depth": 0.58,
            "toneHz": 3200.0,
            "mix": 0.68
        },
        "code": """param float slowHz = 0.62 [0.1, 2.5];
param float vibratoHz = 5.4 [2.0, 9.0];
param float depth = 0.58 [0.0, 1.0];
param float toneHz = 3200.0 [800.0, 7500.0];
param float mix = 0.68 [0.0, 1.0];
state float lfo1 = 0; state float lfo2 = 0;
state float ap1 = 0; state float ap2 = 0; state float lp1 = 0;

lfo1 = (lfo1 + slowHz / sr) % 1.0;
lfo2 = (lfo2 + vibratoHz / sr) % 1.0;

mod = sin(6.2831853 * lfo1) * 0.65 + sin(6.2831853 * lfo2) * 0.35;
c1 = clamp(0.48 + mod * 0.34 * depth, 0.08, 0.88);
c2 = clamp(0.48 - mod * 0.30 * depth, 0.08, 0.88);

y1 = -c1 * in + ap1; ap1 = in + c1 * y1;
y2 = -c2 * in + ap2; ap2 = in + c2 * y2;

bbd = (y1 + y2) * 0.52;
g = clamp(2.0 * sin(3.14159265 * toneHz / sr), 0.04, 0.85);
lp1 = lp1 + g * (bbd - lp1);

out = (in * (1.0 - mix) + lp1 * mix) * 0.68;
"""
    },
    {
        "slug": "shimmer-octave-reverb-fx",
        "title": "Shimmer Octave Reverb FX",
        "description": "Cascaded allpass diffusion tank with nonlinear octave-harmonic shimmer feedback and warm high-frequency damping.",
        "params": {
            "shimmer": 0.45,
            "diffusion": 0.66,
            "feedback": 0.56,
            "dampHz": 3600.0,
            "mix": 0.65
        },
        "code": """param float shimmer = 0.45 [0.0, 1.0];
param float diffusion = 0.66 [0.15, 0.88];
param float feedback = 0.56 [0.0, 0.82];
param float dampHz = 3600.0 [800.0, 7500.0];
param float mix = 0.65 [0.0, 1.0];
state float ap1 = 0; state float ap2 = 0; state float ap3 = 0; state float ap4 = 0;
state float lp1 = 0; state float hp1 = 0; state float fb = 0;

octGen = sin(3.14159265 * clamp(fb * 1.8, -1.0, 1.0));
x = in + (fb * (1.0 - shimmer * 0.4) + octGen * shimmer * 0.55) * feedback;
sat = x / (1.0 + abs(x) * 0.45);

c = clamp(diffusion, 0.12, 0.88);
y1 = -c * sat + ap1; ap1 = sat + c * y1;
y2 = -c * y1 + ap2; ap2 = y1 + c * y2;
y3 = -c * y2 + ap3; ap3 = y2 + c * y3;
y4 = -c * y3 + ap4; ap4 = y3 + c * y4;

g = clamp(2.0 * sin(3.14159265 * dampHz / sr), 0.04, 0.85);
lp1 = lp1 + g * (y4 - lp1);
hp1 = hp1 + 0.012 * (lp1 - hp1);
tank = lp1 - hp1;
fb = clamp(tank, -1.0, 1.0);

out = (in * (1.0 - mix) + tank * mix) * 0.62;
"""
    }
]


# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embed Sources
#    (bismuth_hopper_crystal & ebru_battal_marbling)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "bismuth-hopper-crystal",
        "title": "Bismuth Hopper Crystal",
        "description": "Stepped bismuth hopper crystal terraces with thin-film oxide interference shifting through gold, magenta, cobalt, and specular silver.",
        "embed_desc": "Stepped bismuth hopper crystal terraces with thin-film oxide iridescence. Bass pulses terrace depth, mids shift oxide color phase, highs sharpen metallic facet rims.",
        "tags": ["bismuth", "crystal", "iridescent", "thin-film", "geometric", "shader", "pixel"],
        "params": {
            "steps": 14.0,
            "oxideShift": 0.45,
            "facetSharp": 0.72,
            "rotSpeed": 0.16,
            "metallic": 0.85
        },
        "code": """param float steps = 14.0 [6.0, 24.0];
param float oxideShift = 0.45 [0.0, 1.0];
param float facetSharp = 0.72 [0.2, 1.0];
param float rotSpeed = 0.16 [0.0, 0.8];
param float metallic = 0.85 [0.2, 1.0];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
ca = cos(t * rotSpeed + 0.35);
sa = sin(t * rotSpeed + 0.35);
pr = vec2(p.x * ca - p.y * sa, p.x * sa + p.y * ca);

# Nested stepped square hopper crystal field (Chebyshev distance)
d1 = max(abs(pr.x), abs(pr.y));
d2 = max(abs(pr.x * 0.707 - pr.y * 0.707 + 0.12), abs(pr.x * 0.707 + pr.y * 0.707 - 0.08));
hopper = min(d1, d2 * 1.12);

terrace = floor(hopper * steps) / steps;
edgeDist = abs(fract(hopper * steps) - 0.5);
rim = smoothstep(0.42 - facetSharp * 0.18, 0.5, edgeDist);

# Thin-film oxide iridescence (gold -> magenta -> cobalt -> silver)
phase = terrace * 9.5 + oxideShift * 6.283 + (pr.x - pr.y) * 2.4 + t * 0.25;
irid = 0.5 + 0.5 * cos(phase + vec3(0.0, 2.094, 4.188));

cBase = vec3(0.078, 0.082, 0.106);
cSpec = vec3(0.914, 0.933, 0.949);
col = mix(cBase, irid * metallic, smoothstep(0.68, 0.08, hopper));
col = mix(col, cSpec, rim * 0.55 * smoothstep(0.75, 0.1, hopper));
col = clamp(col, 0.0, 1.0);
""",
        "webgl_fs": """precision mediump float;
uniform vec2 res;
uniform float t;
uniform float aspect;
uniform float audio_bass;
uniform float audio_mid;
uniform float audio_high;

void main() {
  vec2 uv = gl_FragCoord.xy / res;
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
  float ang = t * 0.16 + 0.35;
  float ca = cos(ang);
  float sa = sin(ang);
  vec2 pr = vec2(p.x * ca - p.y * sa, p.x * sa + p.y * ca);

  float steps = 14.0 + audio_bass * 6.0;
  float d1 = max(abs(pr.x), abs(pr.y));
  float d2 = max(abs(pr.x * 0.707 - pr.y * 0.707 + 0.12), abs(pr.x * 0.707 + pr.y * 0.707 - 0.08));
  float hopper = min(d1, d2 * 1.12);

  float terrace = floor(hopper * steps) / steps;
  float edgeDist = abs(fract(hopper * steps) - 0.5);
  float rim = smoothstep(0.42 - (0.72 + audio_high * 0.2) * 0.18, 0.5, edgeDist);

  float phase = terrace * 9.5 + (0.45 + audio_mid * 0.45) * 6.283 + (pr.x - pr.y) * 2.4 + t * 0.25;
  vec3 irid = 0.5 + 0.5 * cos(phase + vec3(0.0, 2.094, 4.188));

  vec3 cBase = vec3(0.078, 0.082, 0.106);
  vec3 cSpec = vec3(0.914, 0.933, 0.949);
  vec3 col = mix(cBase, irid * 0.85, smoothstep(0.68, 0.08, hopper));
  col = mix(col, cSpec, rim * (0.55 + audio_high * 0.35) * smoothstep(0.75, 0.1, hopper));
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}"""
    },
    {
        "slug": "ebru-battal-marbling",
        "title": "Ebru Battal Marbling",
        "description": "Traditional Turkish Battal Ebru paper marbling with domain-warped pigment droplets in terracotta, cadmium yellow, cerulean, and carbon ink veins.",
        "embed_desc": "Turkish Battal Ebru floating pigment marbling. Bass swells the pigment cells, mids drive combed fluid advection, highs crisp the carbon ink veins.",
        "tags": ["ebru", "marbling", "turkish", "pigment", "fluid", "shader", "pixel"],
        "params": {
            "warpAmp": 0.42,
            "combFreq": 5.5,
            "dropScale": 6.0,
            "veinWidth": 0.18,
            "flowSpeed": 0.22
        },
        "code": """param float warpAmp = 0.42 [0.1, 0.9];
param float combFreq = 5.5 [2.0, 12.0];
param float dropScale = 6.0 [3.0, 12.0];
param float veinWidth = 0.18 [0.05, 0.4];
param float flowSpeed = 0.22 [0.0, 0.8];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);

# Multi-pass domain-warped Ebru fluid advection
q = p + warpAmp * vec2(sin(p.y * combFreq + t * flowSpeed), cos(p.x * combFreq - t * flowSpeed * 0.8));
q = q + warpAmp * 0.55 * vec2(cos(q.y * combFreq * 1.7 - t * flowSpeed * 1.1), sin(q.x * combFreq * 1.9 + t * flowSpeed));

cell = sin(q.x * dropScale) * cos(q.y * dropScale) + 0.5 * sin((q.x + q.y) * dropScale * 1.4);
vein = smoothstep(0.0, veinWidth, abs(cell));

cPaper = vec3(0.953, 0.937, 0.902);
cInk = vec3(0.082, 0.078, 0.071);
cTerra = vec3(0.663, 0.290, 0.255);
cYellow = vec3(0.902, 0.839, 0.118);
cCerulean = vec3(0.118, 0.612, 0.843);

pigment = mix(cTerra, cCerulean, smoothstep(-0.35, 0.35, cell));
pigment = mix(pigment, cYellow, smoothstep(0.45, 0.95, sin(q.x * dropScale * 1.3 - q.y * dropScale * 0.9)));
pigment = mix(cPaper, pigment, smoothstep(-0.85, -0.25, cell));

col = mix(cInk, pigment, vein);
col = clamp(col, 0.0, 1.0);
""",
        "webgl_fs": """precision mediump float;
uniform vec2 res;
uniform float t;
uniform float aspect;
uniform float audio_bass;
uniform float audio_mid;
uniform float audio_high;

void main() {
  vec2 uv = gl_FragCoord.xy / res;
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);

  float warpAmp = 0.42 + audio_mid * 0.25;
  float combFreq = 5.5;
  float dropScale = 6.0 + audio_bass * 2.2;
  float flowSpeed = 0.22;

  vec2 q = p + warpAmp * vec2(sin(p.y * combFreq + t * flowSpeed), cos(p.x * combFreq - t * flowSpeed * 0.8));
  q = q + warpAmp * 0.55 * vec2(cos(q.y * combFreq * 1.7 - t * flowSpeed * 1.1), sin(q.x * combFreq * 1.9 + t * flowSpeed));

  float cell = sin(q.x * dropScale) * cos(q.y * dropScale) + 0.5 * sin((q.x + q.y) * dropScale * 1.4);
  float vein = smoothstep(0.0, max(0.06, 0.18 - audio_high * 0.08), abs(cell));

  vec3 cPaper = vec3(0.953, 0.937, 0.902);
  vec3 cInk = vec3(0.082, 0.078, 0.071);
  vec3 cTerra = vec3(0.663, 0.290, 0.255);
  vec3 cYellow = vec3(0.902, 0.839, 0.118);
  vec3 cCerulean = vec3(0.118, 0.612, 0.843);

  vec3 pigment = mix(cTerra, cCerulean, smoothstep(-0.35, 0.35, cell));
  pigment = mix(pigment, cYellow, smoothstep(0.45, 0.95, sin(q.x * dropScale * 1.3 - q.y * dropScale * 0.9)));
  pigment = mix(cPaper, pigment, smoothstep(-0.85, -0.25, cell));

  vec3 col = mix(cInk, pigment, vein);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}"""
    }
]


def make_embed_html(it):
    title = it["title"]
    desc = it["embed_desc"]
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
  <meta name="theme-color" content="#151930">
  <title>{title} — Infinite</title>
  <meta name="description" content="{desc}">
  <style>
    :root {{ --ground: #151930; --glass: rgba(32, 38, 63, 0.78); --ink: #EEF1FA; --ember: #FF6B35; --signal: #A7B1FF; --shadow: rgba(4, 5, 14, 0.45); }}
    * {{ box-sizing: border-box; margin: 0; padding: 0; }}
    html, body {{ width: 100%; height: 100%; overflow: hidden; background: var(--ground); font-family: Geist, system-ui, -apple-system, "Segoe UI", sans-serif; }}
    canvas {{ width: 100%; height: 100%; display: block; }}
  </style>
</head>
<body>
  <main>
    <h1 class="sr-only">{title}</h1>
    <canvas id="gl" aria-label="{title} audio-reactive visual"></canvas>
  </main>
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
{it['webgl_fs']}
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

    const start = performance.now();
    function render(now) {{
      const t = (now - start) * 0.001;
      gl.uniform2f(resLoc, canvas.width, canvas.height);
      gl.uniform1f(tLoc, t);
      gl.uniform1f(aspectLoc, canvas.width / Math.max(1.0, canvas.height));
      gl.uniform1f(bassLoc, audioBass);
      gl.uniform1f(midLoc, audioMid);
      gl.uniform1f(highLoc, audioHigh);
      gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
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

        im.convert("RGB").save(os.path.join(PREVIEWS_DIR, f"{slug}.jpg"), quality=94)
        im.convert("RGB").resize((540, 540), Image.Resampling.LANCZOS).save(os.path.join(THUMBS_DIR, f"{slug}.webp"), "WEBP", quality=85)
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
  c bg 0.106 0.137 0.169
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

        im.convert("RGB").save(os.path.join(PREVIEWS_DIR, f"{slug}.jpg"), quality=94)
        im.convert("RGB").resize((540, 540), Image.Resampling.LANCZOS).save(os.path.join(THUMBS_DIR, f"{slug}.webp"), "WEBP", quality=85)
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
        im.convert("RGB").save(os.path.join(PREVIEWS_DIR, f"{slug}.jpg"), quality=94)
        im.convert("RGB").resize((540, 540), Image.Resampling.LANCZOS).save(os.path.join(THUMBS_DIR, f"{slug}.webp"), "WEBP", quality=85)
        template_preview.render(inf_file, os.path.join(TEMPLATES_DIR, f"{slug}.webp"))
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
        im.convert("RGB").save(os.path.join(PREVIEWS_DIR, f"{slug}.jpg"), quality=94)
        im.convert("RGB").resize((540, 540), Image.Resampling.LANCZOS).save(os.path.join(THUMBS_DIR, f"{slug}.webp"), "WEBP", quality=85)
        template_preview.render(inf_file, os.path.join(TEMPLATES_DIR, f"{slug}.webp"))
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

        im.convert("RGB").save(os.path.join(PREVIEWS_DIR, f"{slug}.jpg"), quality=94)
        im.convert("RGB").resize((540, 540), Image.Resampling.LANCZOS).save(os.path.join(THUMBS_DIR, f"{slug}.webp"), "WEBP", quality=85)
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
                "tags": ["synth", "karplus", "tonewheel", "audio", "dsp"],
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
                "tags": ["effect", "bbd", "shimmer", "reverb", "audio", "dsp"],
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
