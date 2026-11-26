#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 51.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: max_bill_concrete_art -> bill-concrete-color-progression, bill-mobius-ribbon-projection)
2. Sketch 3D (Themes: jansen_strandbeest_linkage, aalto_viipuri_acoustic -> jansen-strandbeest-linkage-3d, aalto-viipuri-acoustic-fins-3d)
3. Field Synth (Themes: formant_pulsar_grain, modal_marimba_bar -> formant-pulsar-grain-synth, modal-marimba-bar-synth)
4. Field Effects (Themes: studer_tape_head_bump, granular_pitch_cloud -> studer-tape-head-bump-fx, granular-pitch-cloud-fx)
5. Field Pixel & Embeds (Themes: poincare_hyperbolic_tiling, max_bill_concrete_art -> poincare-hyperbolic-tessellation, max-bill-mobius-raymarch)
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

TMP_DIR = "/tmp/pack51_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview


# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: max_bill_concrete_art)
#    Palette: #F2EFE9 (bone paper), #8C3B2B (red granite), #1B498A (cobalt), #E0A526 (ochre), #181A20 (ink)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "bill-concrete-color-progression",
        "title": "Bill Concrete Color Progression",
        "description": "Swiss Concrete Art modular chromatic grid with rotated nested squares, mathematical hue progressions, and crisp bone-paper margins.",
        "tags": ["max-bill", "concrete-art", "swiss", "geometry", "grid", "procedural", "sketch"],
        "code": """// Bill Concrete Color Progression - Infinite Library (Sketch 2D)
param("grid_n", 6, 4, 10);
param("twist", 0.28, 0.0, 0.8);
param("margin_pct", 0.11, 0.05, 0.20);
param("tempo", 0.18, 0.0, 0.6);

function draw(t) {
  // Warm archival bone paper (#F2EFE9)
  background(0.949, 0.937, 0.914);
  const w = width;
  const h = height;
  const side = min(w, h);
  const margin = side * margin_pct;
  const span = side - margin * 2.0;
  const x0 = (w - span) * 0.5;
  const y0 = (h - span) * 0.5;

  // Fine Swiss typographic registration frame
  stroke(0.094, 0.102, 0.125, 0.35);
  strokeWeight(1.2);
  noFill();
  rect(x0 - 12, y0 - 12, span + 24, span + 24);

  // Corner datum crosshairs
  for (let c = 0; c < 4; c++) {
    const cx = (c % 2 === 0) ? x0 - 22 : x0 + span + 22;
    const cy = (c < 2) ? y0 - 22 : y0 + span + 22;
    line(cx - 6, cy, cx + 6, cy);
    line(cx, cy - 6, cx, cy + 6);
  }

  const n = floor(grid_n);
  const cell = span / n;

  // Concrete Art palette (Red Granite, Cobalt, Ochre, Ink, Slate)
  const palR = [0.549, 0.106, 0.878, 0.094, 0.255, 0.784];
  const palG = [0.231, 0.286, 0.647, 0.102, 0.329, 0.365];
  const palB = [0.169, 0.541, 0.149, 0.125, 0.420, 0.251];

  for (let iy = 0; iy < n; iy++) {
    for (let ix = 0; ix < n; ix++) {
      const cx = x0 + (ix + 0.5) * cell;
      const cy = y0 + (iy + 0.5) * cell;
      const idx = (ix * 3 + iy * 5) % 6;
      const idx2 = (ix + iy * 2 + 1) % 6;
      const idx3 = (ix * 2 + iy + 3) % 6;

      // Outer cell square
      push();
      translate(cx, cy);
      noStroke();
      fill(palR[idx], palG[idx], palB[idx], 0.94);
      rect(-cell * 0.48, -cell * 0.48, cell * 0.96, cell * 0.96);

      // Rotated inner diamond progression
      const ang = twist * sin(t * tempo + (ix + iy) * 0.35) + ((ix + iy) % 2 === 0 ? 0.785398 : 0.0);
      rotate(ang);
      fill(palR[idx2], palG[idx2], palB[idx2], 0.92);
      const s2 = cell * 0.62;
      rect(-s2 * 0.5, -s2 * 0.5, s2, s2);

      // Innermost concrete core square
      rotate(-ang * 1.5);
      fill(0.949, 0.937, 0.914, 0.96);
      const s3 = cell * 0.28;
      rect(-s3 * 0.5, -s3 * 0.5, s3, s3);

      // Center accent dot
      fill(palR[idx3], palG[idx3], palB[idx3], 0.95);
      circle(0, 0, cell * 0.12);
      pop();

      // Hairline division
      stroke(0.094, 0.102, 0.125, 0.22);
      strokeWeight(1.0);
      noFill();
      rect(x0 + ix * cell, y0 + iy * cell, cell, cell);
    }
  }
}
"""
    },
    {
        "slug": "bill-mobius-ribbon-projection",
        "title": "Bill Mobius Ribbon Projection",
        "description": "Orthographic parametric projection of Max Bill's Endless Loop granite sculpture with ruled isoparm contours and Swiss datum lines.",
        "tags": ["max-bill", "mobius", "topology", "swiss", "contour", "procedural", "sketch"],
        "code": """// Bill Mobius Ribbon Projection - Infinite Library (Sketch 2D)
param("u_steps", 96, 48, 160);
param("v_ribs", 14, 6, 24);
param("ribbon_w", 0.34, 0.15, 0.55);
param("spin", 0.22, 0.0, 0.6);

function draw(t) {
  // Warm limestone paper (#DFDAD0) for balanced luminance
  background(0.875, 0.855, 0.816);
  const w = width;
  const h = height;
  const side = min(w, h);
  const cx = w * 0.5;
  const cy = h * 0.47;
  const R = side * 0.27;

  // Architectural concrete plinth horizon & shadow band
  noStroke();
  fill(0.74, 0.72, 0.68, 0.85);
  rect(w * 0.12, cy + R * 0.92, w * 0.76, side * 0.14);
  stroke(0.094, 0.102, 0.125, 0.45);
  strokeWeight(1.5);
  line(w * 0.12, cy + R * 0.92, w * 0.88, cy + R * 0.92);

  // Fine Swiss elevation grid ticks
  stroke(0.094, 0.102, 0.125, 0.18);
  strokeWeight(1.0);
  for (let g = -4; g <= 4; g++) {
    const gx = cx + g * (side * 0.09);
    line(gx, h * 0.10, gx, h * 0.90);
  }
  for (let g = -3; g <= 3; g++) {
    const gy = cy + g * (side * 0.09);
    line(w * 0.10, gy, w * 0.90, gy);
  }

  const nu = floor(u_steps);
  const nv = floor(v_ribs);
  const rotY = t * spin + 0.55;
  const rotX = 0.42;
  const cyR = cos(rotY);
  const syR = sin(rotY);
  const cxR = cos(rotX);
  const sxR = sin(rotX);

  // Project Mobius strip ruled lines
  for (let i = 0; i < nu; i++) {
    const u = (i / nu) * TAU;
    const halfU = u * 0.5;
    const cu = cos(u);
    const su = sin(u);
    const ch = cos(halfU);
    const sh = sin(halfU);

    // Endpoints across ribbon width [-1, 1]
    const wHalf = R * ribbon_w;
    const rA = R + (-wHalf) * ch;
    const x3A = rA * cu;
    const y3A = (-wHalf) * sh;
    const z3A = rA * su;

    const rB = R + wHalf * ch;
    const x3B = rB * cu;
    const y3B = wHalf * sh;
    const z3B = rB * su;

    // 3D rotation
    const rxA = x3A * cyR - z3A * syR;
    const rzA = x3A * syR + z3A * cyR;
    const ryA = y3A * cxR - rzA * sxR;
    const depthA = y3A * sxR + rzA * cxR;

    const rxB = x3B * cyR - z3B * syR;
    const rzB = x3B * syR + z3B * cyR;
    const ryB = y3B * cxR - rzB * sxR;

    const shade = 0.5 + 0.5 * (depthA / (R * 1.4));
    // Red granite (#8C3B2B) to deep ink (#181A20)
    const rr = 0.549 * shade + 0.094 * (1.0 - shade);
    const gg = 0.231 * shade + 0.102 * (1.0 - shade);
    const bb = 0.169 * shade + 0.125 * (1.0 - shade);

    stroke(rr, gg, bb, 0.85);
    strokeWeight(2.2);
    line(cx + rxA, cy + ryA, cx + rxB, cy + ryB);
  }

  // Longitudinal isoparm curves
  noFill();
  for (let j = 0; j < nv; j++) {
    const vNorm = (j / (nv - 1)) * 2.0 - 1.0;
    const wOff = R * ribbon_w * vNorm;
    const isEdge = (j === 0 || j === nv - 1);
    stroke(isEdge ? 0.094 : 0.106, isEdge ? 0.102 : 0.286, isEdge ? 0.125 : 0.541, isEdge ? 0.92 : 0.42);
    strokeWeight(isEdge ? 2.4 : 1.1);
    beginShape();
    for (let i = 0; i <= nu; i++) {
      const u = (i / nu) * TAU;
      const r = R + wOff * cos(u * 0.5);
      const x3 = r * cos(u);
      const y3 = wOff * sin(u * 0.5);
      const z3 = r * sin(u);
      const rx = x3 * cyR - z3 * syR;
      const rz = x3 * syR + z3 * cyR;
      const ry = y3 * cxR - rz * sxR;
      vertex(cx + rx, cy + ry);
    }
    endShape();
  }
}
"""
    }
]


# -------------------------------------------------------------
# 2. Sketch 3D Sources (Themes: jansen_strandbeest_linkage, aalto_viipuri_acoustic)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "jansen-strandbeest-linkage-3d",
        "title": "Jansen Strandbeest Linkage 3D",
        "description": "Articulated Theo Jansen Strandbeest crank-rocker walking linkage array in cream-ochre PVC struts and graphite pivot joints.",
        "tags": ["jansen", "strandbeest", "kinetic", "linkage", "mechanism", "3d", "sketch-3d"],
        "code": """// Jansen Strandbeest Linkage 3D - Infinite Library (Sketch 3D)
param("legs", 8, 4, 14);
param("stride", 0.55, 0.25, 0.9);
param("speed", 1.4, 0.2, 3.0);
param("spin", 0.18, 0.0, 0.6);

function draw(t) {
  const n = floor(legs);
  rotateY(t * spin + 0.52);
  rotateX(0.18);

  // Central spine axle
  push();
  fill(0.137, 0.149, 0.180);
  box(0.12, 0.12, n * 0.34);
  pop();

  for (let i = 0; i < n; i++) {
    const z = (i - (n - 1) * 0.5) * 0.32;
    const phase = t * speed + (i * TAU) / n;
    const crankX = cos(phase) * 0.28 * stride;
    const crankY = sin(phase) * 0.28 * stride + 0.25;

    for (let side = -1; side <= 1; side += 2) {
      const xOff = side * 0.42;
      const kneeX = xOff + crankX * 0.8 + side * 0.45;
      const kneeY = crankY - 0.35 + 0.18 * sin(phase + side);
      const footX = xOff + side * 0.38 - crankX * 0.6;
      const footY = -0.95 + max(0.0, sin(phase + 0.8)) * 0.28;

      // Upper strut (spine to crank)
      push();
      translate((xOff + crankX) * 0.5, (0.25 + crankY) * 0.5, z);
      rotateZ(atan2(crankY - 0.25, crankX - xOff));
      fill(0.902, 0.871, 0.784);
      box(0.55, 0.055, 0.055);
      pop();

      // Outer triangular truss strut (crank to knee)
      push();
      translate((crankX + kneeX) * 0.5, (crankY + kneeY) * 0.5, z);
      rotateZ(atan2(kneeY - crankY, kneeX - crankX));
      fill(0.784, 0.722, 0.604);
      box(0.75, 0.05, 0.05);
      pop();

      // Lower walking shin (knee to foot)
      push();
      translate((kneeX + footX) * 0.5, (kneeY + footY) * 0.5, z);
      rotateZ(atan2(footY - kneeY, footX - kneeX));
      fill(0.902, 0.871, 0.784);
      box(0.88, 0.048, 0.048);
      pop();

      // Graphite pivot joints
      push();
      translate(kneeX, kneeY, z);
      fill(0.137, 0.149, 0.180);
      sphere(0.065);
      pop();

      push();
      translate(footX, footY, z);
      fill(0.545, 0.271, 0.075);
      box(0.11, 0.06, 0.11);
      pop();
    }
  }
}
"""
    },
    {
        "slug": "aalto-viipuri-acoustic-fins-3d",
        "title": "Aalto Viipuri Acoustic Fins 3D",
        "description": "Undulating laminated birch acoustic ceiling ribs inspired by Alvar Aalto's Viipuri Library lecture hall over a deep slate-teal space.",
        "tags": ["aalto", "viipuri", "acoustic", "timber", "architecture", "3d", "sketch-3d"],
        "code": """// Aalto Viipuri Acoustic Fins 3D - Infinite Library (Sketch 3D)
param("fins", 22, 10, 36);
param("segments", 20, 10, 32);
param("wave_amp", 0.36, 0.1, 0.7);
param("spin", 0.16, 0.0, 0.5);

function draw(t) {
  const nf = floor(fins);
  const ns = floor(segments);
  rotateY(t * spin + 0.48);
  rotateX(0.26);

  // Architectural side wall beams (#264653 slate teal)
  for (let s = -1; s <= 1; s += 2) {
    push();
    translate(s * 1.48, -0.35, 0);
    fill(0.149, 0.275, 0.325);
    box(0.09, 1.35, 2.75);
    pop();
  }

  // Undulating laminated birch acoustic ceiling fins
  for (let i = 0; i < nf; i++) {
    const u = i / (nf - 1);
    const z = (u - 0.5) * 2.55;

    for (let j = 0; j < ns; j++) {
      const v = j / (ns - 1);
      const x = (v - 0.5) * 2.75;

      const wave = wave_amp * (
        sin(v * PI * 2.5 + u * 1.8 + t * 0.35) * 0.65 +
        cos(v * PI * 4.0 - u * 2.4) * 0.35
      );
      const y = 0.25 + wave;
      const slope = cos(v * PI * 2.5 + u * 1.8 + t * 0.35) * wave_amp * 0.55;

      push();
      translate(x, y, z);
      rotateZ(slope);
      const warm = 0.78 + 0.18 * sin(u * PI + v * 2.0);
      // Warm Finnish birch (#D4A373 / #E9C46A)
      fill(warm * 0.94, warm * 0.74, warm * 0.48);
      box(0.17, 0.14, 0.055);
      pop();
    }
  }
}
"""
    }
]


# -------------------------------------------------------------
# 3. Field Synth Sources
#    (formant_pulsar_grain & modal_marimba_bar)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "formant-pulsar-grain-synth",
        "title": "Formant Pulsar Grain Synth",
        "description": "Curtis Roads-inspired pulsar synthesis with raised-cosine Hann windowing, dual vocal formant carriers, and warm soft-clipped resonance.",
        "note_root": 50,
        "params": {
            "formant1": 4.2,
            "formant2": 7.6,
            "duty": 0.42,
            "tilt": 0.55,
            "decay": 0.9992
        },
        "code": """param float formant1 = 4.2 [1.2, 12.0];
param float formant2 = 7.6 [2.0, 18.0];
param float duty = 0.42 [0.12, 0.95];
param float tilt = 0.55 [0.0, 1.0];
param float decay = 0.9992 [0.996, 0.9997];
state float p0 = 0; state float vib = 0;
state float env = 0; state float lp1 = 0; state float lp2 = 0;

env = if(gate > 0.5, env + (1.0 - env) * 0.18, env * decay);
vib = (vib + 4.8 / sr) % 1.0;

f0 = clamp(freq * (1.0 + 0.004 * sin(6.2831853 * vib)), 45.0, 2400.0);
p0 = (p0 + f0 / sr) % 1.0;

# Raised-cosine pulsar window inside fundamental period
dPhase = min(1.0, p0 / max(0.08, duty));
win = 0.5 - 0.5 * cos(6.2831853 * dPhase);

c1 = sin(6.2831853 * p0 * formant1);
c2 = sin(6.2831853 * p0 * formant2 + 0.7);
pulsar = win * (c1 * (1.0 - tilt * 0.45) + c2 * tilt * 0.65);

# Fundamental sub-anchor + body filter
raw = pulsar * 0.78 + sin(6.2831853 * p0) * 0.34;
g = clamp(2.0 * sin(3.14159265 * (650.0 + env * 2800.0) / sr), 0.05, 0.85);
lp1 = lp1 + g * (raw - lp1);
lp2 = lp2 + g * (lp1 - lp2);

drv = lp2 * 1.45;
sat = drv / (1.0 + abs(drv));
out = sat * env * 0.78;
"""
    },
    {
        "slug": "modal-marimba-bar-synth",
        "title": "Modal Marimba Bar Synth",
        "description": "Euler-Bernoulli stiff-bar modal resonator bank with inharmonic bar modes, cosine mallet strike transient, and wooden resonator body.",
        "note_root": 55,
        "params": {
            "hardness": 0.62,
            "stiffness": 0.18,
            "brightHz": 3200.0,
            "decay": 0.9994
        },
        "code": """param float hardness = 0.62 [0.1, 1.0];
param float stiffness = 0.18 [0.0, 0.5];
param float brightHz = 3200.0 [800.0, 7000.0];
param float decay = 0.9994 [0.997, 0.9997];
state float m1 = 0; state float m2 = 0; state float m3 = 0;
state float env1 = 0; state float env2 = 0; state float strike = 1.0;
state float lp1 = 0;

env1 = if(gate > 0.5, env1 + (1.0 - env1) * 0.30, env1 * decay);
env2 = if(gate > 0.5, env2 + (1.0 - env2) * 0.35, env2 * (decay - 0.0018));
strike = if(gate > 0.5, strike * 0.988, 1.0);

f0 = clamp(freq, 65.0, 2600.0);
m1 = (m1 + f0 / sr) % 1.0;
m2 = (m2 + (f0 * (2.756 + stiffness * 0.25)) / sr) % 1.0;
m3 = (m3 + (f0 * (5.404 + stiffness * 0.45)) / sr) % 1.0;

bar = sin(6.2831853 * m1) * env1 * 0.78 + sin(6.2831853 * m2) * env2 * 0.38 * hardness + sin(6.2831853 * m3) * (env2 * 0.20 + strike * 0.30) * hardness;

g = clamp(2.0 * sin(3.14159265 * brightHz / sr), 0.05, 0.85);
lp1 = lp1 + g * (bar - lp1);

drv = lp1 * 1.3;
out = (drv / (1.0 + abs(drv))) * 0.36;
"""
    }
]


# -------------------------------------------------------------
# 4. Field Effects Sources
#    (studer_tape_head_bump & granular_pitch_cloud)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "studer-tape-head-bump-fx",
        "title": "Studer Tape Head Bump FX",
        "description": "Analog reel-to-reel tape path with low-frequency head-bump resonance, asymmetric hysteresis saturation, and subtle wow/flutter allpass drift.",
        "params": {
            "drive": 1.65,
            "bumpHz": 88.0,
            "bumpGain": 0.42,
            "wowHz": 0.55,
            "mix": 0.78
        },
        "code": """param float drive = 1.65 [0.8, 3.5];
param float bumpHz = 88.0 [45.0, 160.0];
param float bumpGain = 0.42 [0.0, 0.85];
param float wowHz = 0.55 [0.1, 2.5];
param float mix = 0.78 [0.0, 1.0];
state float lfo = 0; state float ap1 = 0;
state float bpLow = 0; state float bpBand = 0; state float hfLp = 0;

lfo = (lfo + wowHz / sr) % 1.0;
c = clamp(0.42 + sin(6.2831853 * lfo) * 0.16, 0.1, 0.82);

# Subtle capstan wow/flutter dispersion
wowed = -c * in + ap1;
ap1 = in + c * wowed;

# Resonant head-bump shelf around bumpHz
fBump = clamp(2.0 * sin(3.14159265 * bumpHz / sr), 0.002, 0.25);
bpLow = bpLow + fBump * (wowed - bpBand - bpLow);
bpBand = bpBand + fBump * bpLow * 1.4;
bumped = wowed + bpBand * bumpGain;

# Asymmetric tape hysteresis saturation + 7.5 ips HF roll-off
drv = (bumped + 0.08 * bumped * bumped) * drive;
sat = (drv / (1.0 + abs(drv))) * 0.82;
gHf = clamp(2.0 * sin(3.14159265 * 5400.0 / sr), 0.08, 0.88);
hfLp = hfLp + gHf * (sat - hfLp);

out = (in * (1.0 - mix) + hfLp * mix) * 0.78;
"""
    },
    {
        "slug": "granular-pitch-cloud-fx",
        "title": "Granular Pitch Cloud FX",
        "description": "Dual-window Hann-enveloped granular micro-delay cloud with modulated allpass pitch drift, resonant feedback diffusion, and warm damping.",
        "params": {
            "grainRate": 4.5,
            "drift": 0.58,
            "feedback": 0.54,
            "dampHz": 3400.0,
            "mix": 0.66
        },
        "code": """param float grainRate = 4.5 [1.0, 14.0];
param float drift = 0.58 [0.0, 1.0];
param float feedback = 0.54 [0.0, 0.80];
param float dampHz = 3400.0 [900.0, 7500.0];
param float mix = 0.66 [0.0, 1.0];
state float gPh1 = 0; state float gPh2 = 0.5;
state float ap1 = 0; state float ap2 = 0; state float ap3 = 0;
state float lp1 = 0; state float fb = 0;

gPh1 = (gPh1 + grainRate / sr) % 1.0;
gPh2 = (gPh2 + grainRate / sr) % 1.0;

w1 = 0.5 - 0.5 * cos(6.2831853 * gPh1);
w2 = 0.5 - 0.5 * cos(6.2831853 * gPh2);

src = in + fb * feedback;
c1 = clamp(0.45 + sin(6.2831853 * gPh1) * 0.36 * drift, 0.08, 0.88);
c2 = clamp(0.45 - cos(6.2831853 * gPh2) * 0.36 * drift, 0.08, 0.88);

y1 = -c1 * src + ap1; ap1 = src + c1 * y1;
y2 = -c2 * src + ap2; ap2 = src + c2 * y2;
y3 = -0.62 * (y1 * w1 + y2 * w2) + ap3; ap3 = (y1 * w1 + y2 * w2) + 0.62 * y3;

g = clamp(2.0 * sin(3.14159265 * dampHz / sr), 0.04, 0.85);
lp1 = lp1 + g * (y3 - lp1);
fbDrv = lp1 * 1.15;
fb = fbDrv / (1.0 + abs(fbDrv));

out = (in * (1.0 - mix) + lp1 * mix) * 0.72;
"""
    }
]


# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embed Sources
#    (poincare_hyperbolic_tiling & max_bill_concrete_art)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "poincare-hyperbolic-tessellation",
        "title": "Poincare Hyperbolic Tessellation",
        "description": "Conformal Poincare disk hyperbolic circle-inversion tiling in De Stijl vermilion, cobalt, ochre, and ivory with crisp ink borders.",
        "embed_desc": "Conformal Poincare disk hyperbolic tessellation. Bass pulses the hyperbolic curvature, mids drift the conformal fold, highs sharpen the ink borders.",
        "tags": ["poincare", "hyperbolic", "tessellation", "geometry", "concrete", "shader", "pixel"],
        "params": {
            "foldDepth": 8.0,
            "zoom": 0.92,
            "edgeWidth": 0.025,
            "drift": 0.18
        },
        "code": """param float foldDepth = 8.0 [4.0, 14.0];
param float zoom = 0.92 [0.6, 1.25];
param float edgeWidth = 0.025 [0.008, 0.06];
param float drift = 0.18 [0.0, 0.6];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * (2.15 / zoom);
r2 = dot(p, p);

# Hyperbolic circle-inversion fold inside unit disk
ang = t * drift;
ca = cos(ang); sa = sin(ang);
z = vec2(p.x * ca - p.y * sa, p.x * sa + p.y * ca);

# Branchless hyperbolic reflection across 3 symmetry axes
cRad = 1.28;
cRad2 = cRad * cRad;
cOff = vec2(1.62, 0.0);
flips = 0.0;

z = vec2(abs(z.x), abs(z.y));
sw1 = step(z.x, z.y);
z = mix(z, vec2(z.y, z.x), sw1);
flips = flips + sw1;
d2 = dot(z - cOff, z - cOff);
inv1 = step(d2, cRad2);
z = mix(z, cOff + (z - cOff) * (cRad2 / max(0.001, d2)), inv1);
flips = flips + inv1;

z = vec2(abs(z.x), abs(z.y));
sw2 = step(z.x, z.y);
z = mix(z, vec2(z.y, z.x), sw2);
flips = flips + sw2;
d2 = dot(z - cOff, z - cOff);
inv2 = step(d2, cRad2);
z = mix(z, cOff + (z - cOff) * (cRad2 / max(0.001, d2)), inv2);
flips = flips + inv2;

z = vec2(abs(z.x), abs(z.y));
sw3 = step(z.x, z.y);
z = mix(z, vec2(z.y, z.x), sw3);
flips = flips + sw3;
d2 = dot(z - cOff, z - cOff);
inv3 = step(d2, cRad2);
z = mix(z, cOff + (z - cOff) * (cRad2 / max(0.001, d2)), inv3);
flips = flips + inv3;

z = vec2(abs(z.x), abs(z.y));
sw4 = step(z.x, z.y);
z = mix(z, vec2(z.y, z.x), sw4);
flips = flips + sw4;

# Tile coloring: Ivory, Vermilion, Cobalt, Ochre, Ink
edge = min(abs(z.x - z.y), abs(length(z - cOff) - cRad));
border = smoothstep(0.0, edgeWidth, edge);

cIvory = vec3(0.957, 0.945, 0.918);
cRed = vec3(0.851, 0.220, 0.118);
cBlue = vec3(0.106, 0.286, 0.541);
cOchre = vec3(0.878, 0.647, 0.149);
cInk = vec3(0.094, 0.102, 0.125);

sel = floor(fmod(flips, 4.0));
tileCol = mix(cIvory, cRed, step(0.5, sel) * step(sel, 1.5));
tileCol = mix(tileCol, cBlue, step(1.5, sel) * step(sel, 2.5));
tileCol = mix(tileCol, cOchre, step(2.5, sel));

col = mix(cInk, tileCol, border);
diskMask = smoothstep(1.02, 0.98, sqrt(r2));
col = mix(cInk * 1.15, col, diskMask);
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
  float zoom = 0.92 + audio_bass * 0.14;
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * (2.15 / zoom);
  float r2 = dot(p, p);

  float ang = t * 0.18 + audio_mid * 0.25;
  float ca = cos(ang), sa = sin(ang);
  vec2 z = vec2(p.x * ca - p.y * sa, p.x * sa + p.y * ca);

  float cRad = 1.28;
  vec2 cOff = vec2(1.62, 0.0);
  float flips = 0.0;

  for (int i = 0; i < 4; i++) {
    z = abs(z);
    if (z.y > z.x) { z = z.yx; flips += 1.0; }
    vec2 d = z - cOff;
    float d2 = dot(d, d);
    if (d2 < cRad * cRad) {
      z = cOff + d * (cRad * cRad / max(0.001, d2));
      flips += 1.0;
    }
  }

  float edge = min(abs(z.x - z.y), abs(length(z - cOff) - cRad));
  float border = smoothstep(0.0, max(0.01, 0.025 - audio_high * 0.012), edge);

  vec3 cIvory = vec3(0.957, 0.945, 0.918);
  vec3 cRed = vec3(0.851, 0.220, 0.118);
  vec3 cBlue = vec3(0.106, 0.286, 0.541);
  vec3 cOchre = vec3(0.878, 0.647, 0.149);
  vec3 cInk = vec3(0.094, 0.102, 0.125);

  float sel = floor(mod(flips, 4.0));
  vec3 tileCol = mix(cIvory, cRed, step(0.5, sel) * step(sel, 1.5));
  tileCol = mix(tileCol, cBlue, step(1.5, sel) * step(sel, 2.5));
  tileCol = mix(tileCol, cOchre, step(2.5, sel));

  vec3 col = mix(cInk, tileCol, border);
  float diskMask = smoothstep(1.02, 0.98, sqrt(r2));
  col = mix(cInk * 1.15, col, diskMask);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}"""
    },
    {
        "slug": "max-bill-mobius-raymarch",
        "title": "Max Bill Mobius Raymarch",
        "description": "3D SDF raymarched single-surface Mobius ribbon sculpture in warm red-oxide granite and brushed bronze with grazing sunlight.",
        "embed_desc": "Raymarched Max Bill Endless Loop granite sculpture. Bass pulses the ribbon thickness, mids twist the torsion, highs catch the bronze rim.",
        "tags": ["max-bill", "mobius", "raymarch", "sdf", "granite", "shader", "pixel"],
        "params": {
            "ribbonThick": 0.18,
            "twistRate": 1.5,
            "lightAz": 0.65,
            "spin": 0.24
        },
        "code": """param float ribbonThick = 0.18 [0.08, 0.32];
param float twistRate = 1.5 [0.5, 3.0];
param float lightAz = 0.65 [0.0, 3.14];
param float spin = 0.24 [0.0, 0.8];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
ang = atan2(p.y - 0.04, p.x);
rad = length(vec2(p.x, (p.y - 0.04) * 1.18));

# Analytic twisted Mobius ribbon cross-section in polar-toroidal space
phi = ang * twistRate + t * spin + 0.5;
rMod = 0.27 + 0.065 * cos(phi);
wMod = ribbonThick * (0.36 + 0.26 * abs(sin(phi * 0.5)));
dRibbon = abs(rad - rMod) - wMod;

# Secondary interlocking concrete loop
rMod2 = 0.27 - 0.065 * cos(phi);
wMod2 = ribbonThick * (0.28 + 0.22 * abs(cos(phi * 0.5)));
dRibbon2 = abs(rad - rMod2) - wMod2;
dSculpt = min(dRibbon, dRibbon2);

# Architectural limestone backdrop + concrete plinth
gridLine = step(0.96, fract(p.x * 8.0)) + step(0.96, fract(p.y * 8.0));
cBg = mix(vec3(0.86, 0.84, 0.79), vec3(0.74, 0.71, 0.66), clamp(uv.y, 0.0, 1.0));
cBg = mix(cBg, vec3(0.55, 0.53, 0.50), clamp(gridLine, 0.0, 1.0) * 0.25);

# Plinth below y = -0.31
isPlinth = step(p.y, -0.31);
plinthShade = mix(vec3(0.68, 0.66, 0.62), vec3(0.46, 0.44, 0.41), smoothstep(-0.31, -0.50, p.y));
shadBand = smoothstep(0.0, 0.36, abs(p.x));
plinthShade = mix(vec3(0.24, 0.22, 0.21), plinthShade, 0.35 + 0.65 * shadBand);
cScene = mix(cBg, plinthShade, isPlinth);

# Granite ribbon shading with directional light + bronze rim
normalAngle = ang + lightAz + sin(phi) * 0.6;
diff = 0.52 + 0.48 * cos(normalAngle);
grain = 0.88 + 0.12 * sin(p.x * 95.0 + p.y * 83.0) * cos(p.y * 91.0);
cGranite = vec3(0.549, 0.231, 0.169) * grain * (0.65 + 0.65 * diff);
cBronze = vec3(0.878, 0.647, 0.240);
rim = smoothstep(0.018, 0.0, abs(dSculpt));
cRib = mix(cGranite, cBronze, rim * 0.65);

mask = smoothstep(0.008, -0.004, dSculpt);
col = mix(cScene, cRib, mask);
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
  float ang = atan(p.y - 0.04, p.x);
  float rad = length(vec2(p.x, (p.y - 0.04) * 1.18));

  float ribbonThick = 0.18 + audio_bass * 0.06;
  float twistRate = 1.5;
  float phi = ang * twistRate + t * 0.24 + 0.5 + audio_mid * 0.4;

  float rMod = 0.27 + 0.065 * cos(phi);
  float wMod = ribbonThick * (0.36 + 0.26 * abs(sin(phi * 0.5)));
  float dRibbon = abs(rad - rMod) - wMod;

  float rMod2 = 0.27 - 0.065 * cos(phi);
  float wMod2 = ribbonThick * (0.28 + 0.22 * abs(cos(phi * 0.5)));
  float dRibbon2 = abs(rad - rMod2) - wMod2;
  float dSculpt = min(dRibbon, dRibbon2);

  float gridLine = step(0.96, fract(p.x * 8.0)) + step(0.96, fract(p.y * 8.0));
  vec3 cBg = mix(vec3(0.86, 0.84, 0.79), vec3(0.74, 0.71, 0.66), clamp(uv.y, 0.0, 1.0));
  cBg = mix(cBg, vec3(0.55, 0.53, 0.50), clamp(gridLine, 0.0, 1.0) * 0.25);

  float isPlinth = step(p.y, -0.31);
  vec3 plinthShade = mix(vec3(0.68, 0.66, 0.62), vec3(0.46, 0.44, 0.41), smoothstep(-0.31, -0.50, p.y));
  float shadBand = smoothstep(0.0, 0.36, abs(p.x));
  plinthShade = mix(vec3(0.24, 0.22, 0.21), plinthShade, 0.35 + 0.65 * shadBand);
  vec3 cScene = mix(cBg, plinthShade, isPlinth);

  float normalAngle = ang + 0.65 + sin(phi) * 0.6;
  float diff = 0.52 + 0.48 * cos(normalAngle);
  float grain = 0.88 + 0.12 * sin(p.x * 95.0 + p.y * 83.0) * cos(p.y * 91.0);
  vec3 cGranite = vec3(0.549, 0.231, 0.169) * grain * (0.65 + 0.65 * diff);
  vec3 cBronze = vec3(0.878, 0.647, 0.240);
  float rim = smoothstep(0.018, 0.0, abs(dSculpt));
  vec3 cRib = mix(cGranite, cBronze, rim * (0.65 + audio_high * 0.35));

  float mask = smoothstep(0.008, -0.004, dSculpt);
  vec3 col = mix(cScene, cRib, mask);
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
                "domain": "audio"
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
  i note0 50
  i note1 53
  i note2 57
  i note3 60
  i note4 62
  i note5 60
  i note6 57
  i note7 53
  i note8 50
  i note9 57
  i note10 60
  i note11 64
  i note12 60
  i note13 57
  i note14 53
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
                "tags": ["synth", "pulsar", "modal", "audio", "dsp"],
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
                "tags": ["effect", "tape", "granular", "audio", "dsp"],
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
                "params": len(it["params"]),
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
