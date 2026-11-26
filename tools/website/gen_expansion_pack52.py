#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 52.
Generates 10 items across the 5 core creative families:
1. Sketch 2D: penrose-ammann-rhombi, albers-chromatic-nesting
2. Sketch 3D: muqarnas-corbel-vault-3d, frei-otto-tensile-mast-3d
3. Field Synth: clavinet-d6-pickup-synth, bowed-bar-marimba-synth
4. Field Effects: hilbert-frequency-shifter-fx, wavefolder-tape-saturator-fx
5. Field Pixel & Embeds: faraday-standing-waves, voronoi-basalt-caustics
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
    ALT_BIN = "/Users/namansoni/Infinite/worktrees/main/build/Infinite.app/Contents/MacOS/Infinite"
    if os.path.exists(ALT_BIN):
        BIN = ALT_BIN
    else:
        BIN = os.path.expanduser("~/.local/bin/Infinite")
CMD = [BIN, "--cli"] if BIN.endswith("Contents/MacOS/Infinite") else [BIN]

LIB_DIR = os.path.join(ROOT, "website", "assets", "library")
SKETCH_DIR = os.path.join(LIB_DIR, "sketch")
PIXEL_DIR = os.path.join(LIB_DIR, "pixel")
PREVIEWS_DIR = os.path.join(LIB_DIR, "previews")
THUMBS_DIR = os.path.join(LIB_DIR, "thumbs")
TEMPLATES_DIR = os.path.join(ROOT, "website", "assets", "templates")
EMBEDS_DIR = os.path.join(ROOT, "website", "embeds")

TMP_DIR = "/tmp/pack52_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview


SKETCH_2D_ITEMS = [
    {
        "slug": "penrose-ammann-rhombi",
        "title": "Penrose Ammann Rhombi",
        "description": "Aperiodic P3 Penrose rhombus tessellation deflated from a tenfold sun wheel with weathered granite tones, ember star cores, and continuous Ammann ribbon arcs.",
        "tags": ["penrose", "aperiodic", "tessellation", "quasicrystal", "geometry", "procedural", "sketch"],
        "code": """// Penrose Ammann Rhombi - Infinite Library (Sketch 2D)
param("subdiv", 4, 2, 5);
param("arcWeight", 2.2, 0.8, 4.0);
param("pulse", 0.22, 0.0, 0.6);
param("drift", 0.08, 0.0, 0.4);

function clamp(v, lo, hi) {
  return min(max(v, lo), hi);
}

function draw(t) {
  // Architectural granite ground (#1C1F24)
  background(0.110, 0.122, 0.141);
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const R = min(w, h) * 0.44;
  const phi = 1.618033988749895;
  const invPhi = 1.0 / phi;

  // Build 10-fold sun wheel of Robinson triangles: [type (0=thin, 1=fat), ax, ay, bx, by, cx, cy]
  let tris = [];
  const rot = t * drift * 0.25;
  for (let i = 0; i < 10; i++) {
    const a0 = (i * TAU) / 10.0 + rot;
    const a1 = ((i + 1) * TAU) / 10.0 + rot;
    const bx = cx + cos(a0) * R;
    const by = cy + sin(a0) * R;
    const px = cx + cos(a1) * R;
    const py = cy + sin(a1) * R;
    if (i % 2 === 0) {
      tris.push([0, cx, cy, px, py, bx, by]);
    } else {
      tris.push([0, cx, cy, bx, by, px, py]);
    }
  }

  const levels = floor(subdiv);
  for (let s = 0; s < levels; s++) {
    const next = [];
    for (let k = 0; k < tris.length; k++) {
      const tr = tris[k];
      const tp = tr[0];
      const ax = tr[1], ay = tr[2];
      const bx = tr[3], by = tr[4];
      const cxp = tr[5], cyp = tr[6];
      if (tp === 0) {
        const px = ax + (bx - ax) * invPhi;
        const py = ay + (by - ay) * invPhi;
        next.push([0, cxp, cyp, px, py, bx, by]);
        next.push([1, px, py, cxp, cyp, ax, ay]);
      } else {
        const qx = bx + (ax - bx) * invPhi;
        const qy = by + (ay - by) * invPhi;
        const rx = bx + (cxp - bx) * invPhi;
        const ry = by + (cyp - by) * invPhi;
        next.push([1, rx, ry, cxp, cyp, ax, ay]);
        next.push([1, qx, qy, rx, ry, bx, by]);
        next.push([0, rx, ry, qx, qy, ax, ay]);
      }
    }
    tris = next;
  }

  // Draw deflated rhombi (pairs of Robinson triangles)
  for (let k = 0; k < tris.length; k++) {
    const tr = tris[k];
    const tp = tr[0];
    const ax = tr[1], ay = tr[2];
    const bx = tr[3], by = tr[4];
    const cxp = tr[5], cyp = tr[6];
    const mx = (bx + cxp) * 0.5;
    const my = (by + cyp) * 0.5;
    const dist = sqrt((mx - cx) * (mx - cx) + (my - cy) * (my - cy)) / R;
    const wave = sin(dist * 10.0 - t * 1.4) * pulse;

    if (tp === 0) {
      // Acute thin tile: deep slate vs warm stone
      const lum = clamp(0.24 + 0.08 * sin(dist * 6.0) + wave * 0.14, 0.12, 0.55);
      fill(lum, lum * 1.04, lum * 1.10);
    } else {
      // Fat tile: weathered granite or ember accent near core
      if (dist < 0.24 && (k % 5 === 0)) {
        fill(0.784, 0.325, 0.212, 0.92);
      } else {
        const lum = clamp(0.42 + 0.10 * cos(dist * 5.0) + wave * 0.12, 0.22, 0.72);
        fill(lum, lum * 0.99, lum * 0.96);
      }
    }
    stroke(0.110, 0.122, 0.141, 0.75);
    strokeWeight(1.0);
    beginShape();
    vertex(ax, ay);
    vertex(bx, by);
    vertex(cxp, cyp);
    endShape(CLOSE);

    // Ammann-style ribbon chord across each triangle
    const p1x = ax + (bx - ax) * 0.618;
    const p1y = ay + (by - ay) * 0.618;
    const p2x = ax + (cxp - ax) * 0.382;
    const p2y = ay + (cyp - ay) * 0.382;
    if (tp === 0) {
      stroke(0.941, 0.929, 0.898, 0.78);
    } else {
      stroke(0.157, 0.173, 0.200, 0.65);
    }
    strokeWeight(arcWeight);
    line(p1x, p1y, p2x, p2y);
  }

  // Outer architectural circular datum & tenfold ticks
  noFill();
  stroke(0.941, 0.929, 0.898, 0.45);
  strokeWeight(1.5);
  circle(cx, cy, R * 2.08);
  for (let i = 0; i < 20; i++) {
    const a = (i * TAU) / 20.0 + rot;
    const r0 = R * 1.04;
    const r1 = R * (i % 2 === 0 ? 1.09 : 1.065);
    line(cx + cos(a) * r0, cy + sin(a) * r0, cx + cos(a) * r1, cy + sin(a) * r1);
  }
}
"""
    },
    {
        "slug": "albers-chromatic-nesting",
        "title": "Albers Chromatic Nesting",
        "description": "Four-plate Homage to the Square color-interaction study with asymmetrical bottom-weighted nested planes, brushed frames, and subtle temporal luminance drift.",
        "tags": ["albers", "bauhaus", "color", "square", "minimal", "procedural", "sketch"],
        "code": """// Albers Chromatic Nesting - Infinite Library (Sketch 2D)
param("shift", 0.06, 0.0, 0.25);
param("frameW", 5.0, 2.0, 10.0);
param("gap", 0.065, 0.03, 0.12);
param("speed", 0.35, 0.0, 1.2);

function clamp(v, lo, hi) {
  return min(max(v, lo), hi);
}

function drawPanel(px, py, size, cols, t, phase) {
  // Brushed aluminum museum frame
  noStroke();
  fill(0.62, 0.64, 0.65);
  rect(px - frameW, py - frameW, size + frameW * 2.0, size + frameW * 2.0);
  fill(0.14, 0.15, 0.16);
  rect(px - 1.5, py - 1.5, size + 3.0, size + 3.0);

  // 4 nested squares with Albers bottom-heavy ratio: bottom=1u, side=2u, top=3u
  const ratios = [1.0, 0.78, 0.56, 0.35];
  const breathe = sin(t * speed + phase) * shift;
  for (let i = 0; i < 4; i++) {
    const s = size * ratios[i];
    const dx = (size - s) * 0.5;
    // Bottom margin is 1/4 of vertical slack, top margin is 3/4 of vertical slack
    const dy = (size - s) * 0.75;
    const c = cols[i];
    const r = clamp(c[0] + (i >= 2 ? breathe : 0.0), 0.0, 1.0);
    const g = clamp(c[1] + (i >= 2 ? breathe * 0.6 : 0.0), 0.0, 1.0);
    const b = clamp(c[2] - (i >= 2 ? breathe * 0.4 : 0.0), 0.0, 1.0);
    fill(r, g, b);
    rect(px + dx, py + dy, s, s);
  }
}

function draw(t) {
  // Warm museum gallery plaster (#DCDAD4)
  background(0.863, 0.855, 0.831);
  const w = width;
  const h = height;
  const span = min(w, h);
  const margin = span * 0.09;
  const g = span * gap;
  const cell = (span - margin * 2.0 - g) * 0.5;
  const x0 = (w - (cell * 2.0 + g)) * 0.5;
  const y0 = (h - (cell * 2.0 + g)) * 0.5;

  // Fine Bauhaus registration grid hairlines
  stroke(0.18, 0.19, 0.20, 0.18);
  strokeWeight(1.0);
  line(w * 0.5, margin * 0.35, w * 0.5, h - margin * 0.35);
  line(margin * 0.35, h * 0.5, w - margin * 0.35, h * 0.5);

  // 1. Emerald -> Pine -> Deep Teal
  const p1 = [[0.035, 0.365, 0.278], [0.055, 0.294, 0.259], [0.067, 0.235, 0.243], [0.082, 0.192, 0.220]];
  // 2. Burnt Ochre -> Marigold -> Cadmium -> Amber
  const p2 = [[0.725, 0.392, 0.118], [0.804, 0.518, 0.114], [0.878, 0.690, 0.078], [0.808, 0.620, 0.075]];
  // 3. Cobalt Ultramarine -> Petrol -> Viridian -> Emerald
  const p3 = [[0.047, 0.302, 0.569], [0.047, 0.341, 0.427], [0.039, 0.388, 0.365], [0.027, 0.435, 0.314]];
  // 4. Terracotta Rust -> Charcoal Umber -> Deep Teal -> Slate
  const p4 = [[0.541, 0.165, 0.094], [0.165, 0.169, 0.157], [0.059, 0.255, 0.259], [0.090, 0.325, 0.314]];

  drawPanel(x0, y0, cell, p1, t, 0.0);
  drawPanel(x0 + cell + g, y0, cell, p2, t, 1.57);
  drawPanel(x0, y0 + cell + g, cell, p3, t, 3.14);
  drawPanel(x0 + cell + g, y0 + cell + g, cell, p4, t, 4.71);

  // Plate index marks at bottom
  noStroke();
  fill(0.16, 0.17, 0.18, 0.65);
  for (let i = 0; i < 4; i++) {
    const bx = x0 + (i % 2) * (cell + g);
    const by = y0 + floor(i / 2) * (cell + g) + cell + 14;
    rect(bx, by, 18, 3);
    rect(bx + 24, by, 6, 3);
  }
}
"""
    }
]

SKETCH_3D_ITEMS = [
    {
        "slug": "muqarnas-corbel-vault-3d",
        "title": "Muqarnas Corbel Vault 3D",
        "description": "Tiered eightfold Islamic muqarnas honeycomb vault with stepped cobalt, turquoise, sandstone, and ochre-gold corbel niches rising toward a central oculus.",
        "tags": ["muqarnas", "islamic", "architecture", "vault", "corbel", "procedural", "3d"],
        "code": """// Muqarnas Corbel Vault 3D - Infinite Library (Sketch 3D)
param("tiers", 6, 4, 9);
param("facets", 12, 8, 20);
param("vaultH", 1.85, 1.2, 2.6);
param("spin", 0.16, 0.0, 0.6);

function draw(t) {
  rotateY(t * spin + 0.42);
  rotateX(0.32);

  // Dark sandstone architectural plinth
  push();
  translate(0, -0.95, 0);
  fill(0.14, 0.13, 0.12);
  cylinder(1.62, 0.12, 8);
  pop();

  // Outer colonnade & pointed arch piers
  const pierN = 8;
  for (let p = 0; p < pierN; p++) {
    const pa = (p * TAU) / pierN;
    const pr = 1.38;
    push();
    translate(cos(pa) * pr, -0.35, sin(pa) * pr);
    rotateY(-pa);
    fill(0.76, 0.67, 0.54);
    box(0.16, 1.15, 0.22);
    pop();
  }

  // Ascending tiered muqarnas honeycomb corbels
  const tn = floor(tiers);
  const fn = floor(facets);
  for (let tier = 0; tier < tn; tier++) {
    const u = tier / (tn - 1);
    const rad = 1.30 * (1.0 - u * 0.68);
    const y = -0.25 + u * vaultH * 0.72;
    const count = fn + (tier % 2 === 0 ? 0 : 4);
    const cellW = (TAU * rad) / count * 0.78;
    const cellH = 0.24 * (1.0 - u * 0.35);

    for (let i = 0; i < count; i++) {
      const a = (i * TAU) / count + tier * 0.18;
      push();
      translate(cos(a) * rad, y, sin(a) * rad);
      rotateY(-a + PI * 0.5);
      rotateX(0.28 + u * 0.25);

      if ((tier + i) % 3 === 0) {
        fill(0.118, 0.267, 0.541); // Glazed cobalt
      } else if ((tier + i) % 3 === 1) {
        fill(0.784, 0.573, 0.212); // Ochre gold
      } else {
        fill(0.169, 0.478, 0.549); // Turquoise tile
      }
      box(cellW, cellH, 0.16);

      // Inner stalactite drop prism
      translate(0, -cellH * 0.42, 0.05);
      fill(0.84, 0.76, 0.64);
      box(cellW * 0.45, cellH * 0.55, 0.08);
      pop();
    }
  }

  // Central star-polygon cupola oculus
  push();
  translate(0, -0.25 + vaultH * 0.78, 0);
  fill(0.92, 0.78, 0.42);
  torus(0.32, 0.055, 8, 24);
  pop();
}
"""
    },
    {
        "slug": "frei-otto-tensile-mast-3d",
        "title": "Frei Otto Tensile Mast 3D",
        "description": "Cable-net saddle pavilion inspired by Frei Otto with tilted anodized steel masts, ember tension collars, and intersecting hyperbolic-paraboloid aluminum ribs.",
        "tags": ["frei-otto", "tensile", "pavilion", "architecture", "hyperbolic", "procedural", "3d"],
        "code": """// Frei Otto Tensile Mast 3D - Infinite Library (Sketch 3D)
param("gridN", 14, 8, 22);
param("span", 2.5, 1.6, 3.2);
param("sag", 0.55, 0.2, 0.9);
param("spin", 0.18, 0.0, 0.6);

function draw(t) {
  rotateY(t * spin + 0.55);
  rotateX(0.26);

  // Anodized slate plaza deck
  push();
  translate(0, -0.88, 0);
  fill(0.11, 0.13, 0.16);
  box(span * 1.18, 0.08, span * 1.18);
  pop();

  // Four outward-tilted perimeter steel masts with ember tension collars
  const corners = [
    [-1, -1], [1, -1], [1, 1], [-1, 1]
  ];
  for (let c = 0; c < 4; c++) {
    const sx = corners[c][0];
    const sz = corners[c][1];
    push();
    translate(sx * span * 0.44, -0.05, sz * span * 0.44);
    rotateZ(-sx * 0.22);
    rotateX(sz * 0.22);
    fill(0.20, 0.24, 0.29);
    cylinder(0.045, 1.78, 12);

    // Ember anchor collar at masthead
    translate(0, 0.82, 0);
    fill(0.878, 0.353, 0.212);
    cylinder(0.075, 0.10, 12);
    pop();
  }

  // Hyperbolic-paraboloid cable-net ribs (y = sag * (u^2 - v^2))
  const n = floor(gridN);
  for (let i = 0; i < n; i++) {
    const u = (i / (n - 1)) * 2.0 - 1.0;
    for (let j = 0; j < n - 1; j++) {
      const v0 = (j / (n - 1)) * 2.0 - 1.0;
      const v1 = ((j + 1) / (n - 1)) * 2.0 - 1.0;
      const vm = (v0 + v1) * 0.5;
      const y = 0.28 + sag * (u * u - vm * vm);
      const dy = sag * (-(v1 * v1 - v0 * v0));
      const dz = (v1 - v0) * span * 0.44;
      const segLen = sqrt(dz * dz + dy * dy);
      const tilt = atan2(dy, dz);

      push();
      translate(u * span * 0.44, y, vm * span * 0.44);
      rotateX(-tilt);
      fill(i % 2 === 0 ? 0.85 : 0.62, i % 2 === 0 ? 0.88 : 0.68, i % 2 === 0 ? 0.91 : 0.74);
      box(0.028, 0.028, segLen * 1.04);
      pop();
    }
  }

  // Cross-tension ribs along X
  for (let j = 0; j < n; j += 2) {
    const v = (j / (n - 1)) * 2.0 - 1.0;
    for (let i = 0; i < n - 1; i++) {
      const u0 = (i / (n - 1)) * 2.0 - 1.0;
      const u1 = ((i + 1) / (n - 1)) * 2.0 - 1.0;
      const um = (u0 + u1) * 0.5;
      const y = 0.28 + sag * (um * um - v * v);
      const dy = sag * (u1 * u1 - u0 * u0);
      const dx = (u1 - u0) * span * 0.44;
      const segLen = sqrt(dx * dx + dy * dy);
      const tilt = atan2(dy, dx);

      push();
      translate(um * span * 0.44, y, v * span * 0.44);
      rotateZ(tilt);
      fill(0.75, 0.79, 0.84);
      box(segLen * 1.04, 0.022, 0.022);
      pop();
    }
  }
}
"""
    }
]

SYNTH_ITEMS = [
    {
        "slug": "clavinet-d6-pickup-synth",
        "title": "Clavinet D6 Pickup Synth",
        "description": "Physical-modeled Hohner Clavinet D6 with damped string tangent strike, asymmetric dual-coil magnetic pickup comb filtering, and warm preamp grit.",
        "notes": [40, 43, 47, 52, 47, 43, 40, 45, 48, 52, 55, 52, 48, 45, 43, 47],
        "params": {
            "pickupPos": 0.28,
            "bite": 0.72,
            "damp": 0.9991,
            "preamp": 0.45,
            "bodyHz": 1650.0
        },
        "code": """param float pickupPos = 0.28 [0.08, 0.48];
param float bite = 0.72 [0.1, 1.0];
param float damp = 0.9991 [0.997, 0.9997];
param float preamp = 0.45 [0.0, 1.0];
param float bodyHz = 1650.0 [600.0, 3800.0];
state float p1 = 0; state float p2 = 0; state float p3 = 0;
state float env = 0; state float strikeEnv = 1.0;
state float lp1 = 0; state float bp1 = 0;

env = if(gate > 0.5, env + (1.0 - env) * 0.28, env * damp);
strikeEnv = if(gate > 0.5, strikeEnv * 0.988, 1.0);

f0 = clamp(freq, 45.0, 3200.0);
p1 = (p1 + f0 / sr) % 1.0;
p2 = (p2 + (f0 * 2.001) / sr) % 1.0;
p3 = (p3 + (f0 * 3.003) / sr) % 1.0;

h1 = sin(6.2831853 * p1) * sin(3.14159265 * pickupPos);
h2 = sin(6.2831853 * p2) * sin(6.2831853 * pickupPos) * (0.65 + strikeEnv * 0.45);
h3 = sin(6.2831853 * p3) * sin(9.424778 * pickupPos) * (0.35 + bite * 0.55) * strikeEnv;

driven = (h1 + h2 + h3) * (1.2 + preamp * 2.2) * env;
sat = driven / (1.0 + abs(driven) * 0.65);

g = clamp(6.2831853 * bodyHz / sr, 0.04, 0.72);
bp1 = bp1 + g * (sat - lp1 - 0.58 * bp1);
lp1 = lp1 + g * bp1;

out = (sat * 0.55 + bp1 * 0.48) * 0.58;
"""
    },
    {
        "slug": "bowed-bar-marimba-synth",
        "title": "Bowed Bar Marimba Synth",
        "description": "Modal rosewood bar percussion and bowed-mallet synthesizer with four tuned non-harmonic overtones, tremolo bow excitation, and resonant tube body filter.",
        "notes": [55, 59, 62, 67, 62, 59, 55, 57, 60, 64, 67, 71, 67, 64, 60, 57],
        "params": {
            "malletHard": 0.65,
            "bowMix": 0.42,
            "tremHz": 4.2,
            "spread": 0.18,
            "decay": 0.9994
        },
        "code": """param float malletHard = 0.65 [0.1, 1.0];
param float bowMix = 0.42 [0.0, 1.0];
param float tremHz = 4.2 [0.5, 10.0];
param float spread = 0.18 [0.0, 0.5];
param float decay = 0.9994 [0.997, 0.9998];
state float p1 = 0; state float p2 = 0; state float p3 = 0; state float lfo = 0;
state float env = 0; state float bow = 0; state float mal = 1.0; state float lp1 = 0;

env = if(gate > 0.5, env + (1.0 - env) * 0.20, env * decay);
bow = if(gate > 0.5, bow + (1.0 - bow) * 0.004, bow * 0.9992);
mal = if(gate > 0.5, mal * 0.985, 1.0);
lfo = (lfo + tremHz / sr) % 1.0;

f0 = clamp(freq, 65.0, 2800.0);
p1 = (p1 + f0 / sr) % 1.0;
p2 = (p2 + f0 * (3.98 + spread * 0.12) / sr) % 1.0;
p3 = (p3 + f0 * 9.78 / sr) % 1.0;

sig = (sin(6.2831853 * p1) * (env + bow * bowMix * 0.7) + sin(6.2831853 * p2) * malletHard * 0.55 * mal + sin(6.2831853 * p3) * malletHard * 0.2 * mal) * (0.78 + 0.22 * sin(6.2831853 * lfo));
lp1 = lp1 + 0.18 * (sig - lp1);
out = ((sig * 0.55 + lp1 * 0.55) / (1.0 + abs(sig) * 0.45)) * 0.25;
"""
    }
]

FX_ITEMS = [
    {
        "slug": "hilbert-frequency-shifter-fx",
        "title": "Hilbert Frequency Shifter FX",
        "description": "Single-sideband Bode-style quadrature frequency shifter with a 4-pole allpass Hilbert network, metallic detuning, and barber-pole spiral feedback.",
        "notes": [48, 52, 55, 60, 55, 52, 48, 50, 53, 57, 60, 64, 60, 57, 53, 50],
        "params": {
            "shiftHz": 18.0,
            "sideband": 0.85,
            "feedback": 0.32,
            "toneHz": 4200.0,
            "mix": 0.65
        },
        "code": """param float shiftHz = 18.0 [-120.0, 120.0];
param float sideband = 0.85 [-1.0, 1.0];
param float feedback = 0.32 [0.0, 0.75];
param float toneHz = 4200.0 [800.0, 8000.0];
param float mix = 0.65 [0.0, 1.0];
state float osc = 0; state float fb = 0;
state float a1 = 0; state float a2 = 0; state float b1 = 0; state float b2 = 0;
state float lp = 0;

x = in + fb * feedback;

# 4-pole quadrature allpass Hilbert pair (I and Q 90-deg offset)
yI1 = -0.69238 * x + a1; a1 = x + 0.69238 * yI1;
yI2 = -0.93606 * yI1 + a2; a2 = yI1 + 0.93606 * yI2;

yQ1 = -0.40219 * x + b1; b1 = x + 0.40219 * yQ1;
yQ2 = -0.85617 * yQ1 + b2; b2 = yQ1 + 0.85617 * yQ2;

osc = (osc + shiftHz / sr) % 1.0;
c = cos(6.2831853 * osc);
s = sin(6.2831853 * osc);

ssb = yI2 * c - yQ2 * s * sideband;
g = clamp(2.0 * sin(3.14159265 * toneHz / sr), 0.04, 0.85);
lp = lp + g * (ssb - lp);
fb = clamp(lp, -0.95, 0.95);

out = (in * (1.0 - mix) + lp * mix) * 0.68;
"""
    },
    {
        "slug": "wavefolder-tape-saturator-fx",
        "title": "Wavefolder Tape Saturator FX",
        "description": "Asymmetric triode-tape soft-clipping saturator paired with a Lockhart sine wavefolder stage, pre-emphasis tilt, and dynamic bias sag.",
        "notes": [45, 48, 52, 57, 52, 48, 45, 47, 50, 55, 59, 62, 59, 55, 50, 47],
        "params": {
            "foldDrive": 2.4,
            "biasSym": 0.24,
            "tapeWarmth": 2800.0,
            "sag": 0.35,
            "mix": 0.72
        },
        "code": """param float foldDrive = 2.4 [1.0, 6.0];
param float biasSym = 0.24 [0.0, 0.6];
param float tapeWarmth = 2800.0 [700.0, 7500.0];
param float sag = 0.35 [0.0, 0.8];
param float mix = 0.72 [0.0, 1.0];
state float env = 0; state float lp1 = 0; state float dc = 0;

env = env * 0.996 + abs(in) * 0.004;
dynDrive = foldDrive / (1.0 + env * sag * 2.2);

pre = in * dynDrive + biasSym;
folded = sin(pre * 1.45) * 0.65 + (pre / (1.0 + abs(pre) * 0.85)) * 0.45;
dc = dc * 0.998 + folded * 0.002;
cleanFold = folded - dc;

g = clamp(2.0 * sin(3.14159265 * tapeWarmth / sr), 0.04, 0.85);
lp1 = lp1 + g * (cleanFold - lp1);

out = (in * (1.0 - mix) + lp1 * mix) * 0.66;
"""
    }
]

PIXEL_ITEMS = [
    {
        "slug": "faraday-standing-waves",
        "title": "Faraday Standing Waves",
        "description": "Subharmonic Faraday standing-wave interference lattice inside a circular fluid dish with champagne meniscus highlights and warm taupe troughs.",
        "embed_desc": "Audio-reactive Faraday standing-wave fluid lattice. Bass drives harmonic wave amplitude, mids shift symmetry, highs sharpen meniscus speculars.",
        "tags": ["faraday", "cymatics", "standing-waves", "fluid", "interference", "pixel", "glsl"],
        "params": {
            "freqMode": 16.0,
            "symmetry": 6.0,
            "meniscus": 0.72,
            "damping": 0.35,
            "speed": 0.85
        },
        "code": """param float freqMode = 16.0 [6.0, 28.0];
param float symmetry = 6.0 [3.0, 10.0];
param float meniscus = 0.72 [0.2, 1.0];
param float damping = 0.35 [0.0, 1.0];
param float speed = 0.85 [0.1, 2.0];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
r = length(p);
ang = atan2(p.y, p.x);

# Circular Petri dish boundary & rim meniscus
dish = smoothstep(0.46, 0.435, r);
rim = smoothstep(0.455, 0.442, r) * smoothstep(0.425, 0.440, r);

# Superposed standing plane waves (Faraday hexagonal/quasicrystal lattice)
k = freqMode * 3.14159;
w1 = cos(p.x * k + cos(t * speed)) * cos(p.y * k * 0.866);
w2 = cos((p.x * 0.5 + p.y * 0.866) * k) * cos((p.x * 0.5 - p.y * 0.866) * k + sin(t * speed * 0.7));
w3 = cos(r * k * 1.35 - t * speed * 1.5) * cos(ang * 6.0);
wsum = (w1 * 0.45 + w2 * 0.40 + w3 * 0.30) * exp(-r * damping * 1.2);

ridge = smoothstep(-0.22, 0.38, wsum);
sp = clamp(wsum * 0.85 + 0.48, 0.0, 1.0);
spec = sp * sp * sp * sp * (0.4 + meniscus * 0.8);

cTrough = vec3(0.145, 0.122, 0.102);
cTaupe = vec3(0.545, 0.471, 0.404);
cChampagne = vec3(0.957, 0.922, 0.863);
cTable = vec3(0.275, 0.231, 0.192);

fluid = mix(cTrough, cTaupe, ridge);
fluid = mix(fluid, cChampagne, clamp(spec, 0.0, 1.0));
col = mix(cTable * (0.85 - r * 0.35), fluid, dish);
col = mix(col, cChampagne, rim * 0.75);
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
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
  float r = length(p);
  float ang = atan(p.y, p.x);

  float dish = smoothstep(0.46, 0.435, r);
  float rim = smoothstep(0.455, 0.442, r) * smoothstep(0.425, 0.440, r);

  float k = (16.0 + audio_bass * 8.0) * 3.14159;
  float sym = 6.0 + floor(audio_mid * 4.0);
  float w1 = cos(p.x * k + cos(t * 0.85)) * cos(p.y * k * 0.866);
  float w2 = cos((p.x * 0.5 + p.y * 0.866) * k) * cos((p.x * 0.5 - p.y * 0.866) * k + sin(t * 0.6));
  float w3 = cos(r * k * 1.4 - t * 1.3) * cos(ang * sym);
  float wsum = (w1 * 0.42 + w2 * 0.38 + w3 * 0.28) * exp(-r * 0.55);

  float ridge = smoothstep(-0.15, 0.55, wsum);
  float spec = pow(clamp(wsum * 0.9 + 0.35 + audio_high * 0.25, 0.0, 1.0), 4.5 + audio_high * 3.0);

  vec3 cTrough = vec3(0.118, 0.098, 0.082);
  vec3 cTaupe = vec3(0.420, 0.365, 0.314);
  vec3 cChampagne = vec3(0.949, 0.914, 0.855);
  vec3 cTable = vec3(0.290, 0.243, 0.200);

  vec3 fluid = mix(cTrough, cTaupe, ridge);
  fluid = mix(fluid, cChampagne, spec * (0.85 + audio_high * 0.15));
  vec3 col = mix(cTable * (0.85 - r * 0.35), fluid, dish);
  col = mix(col, cChampagne, rim * 0.75);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "voronoi-basalt-caustics",
        "title": "Voronoi Basalt Caustics",
        "description": "Hexagonal columnar basalt fracture cells with refracted prismatic water caustics and slate-mineral bevels.",
        "embed_desc": "Audio-reactive columnar basalt fracture cells with refracted water caustics. Bass pulses cell relief, mids warp caustics, highs brighten edge glint.",
        "tags": ["voronoi", "basalt", "caustics", "mineral", "refraction", "pixel", "glsl"],
        "params": {
            "scale": 5.5,
            "bevel": 0.12,
            "caustic": 0.78,
            "drift": 0.22,
            "contrast": 0.82
        },
        "code": """param float scale = 5.5 [3.0, 10.0];
param float bevel = 0.14 [0.03, 0.35];
param float caustic = 0.85 [0.2, 1.0];
param float drift = 0.22 [0.0, 0.8];
param float contrast = 0.85 [0.3, 1.0];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * scale;
# Hexagonal columnar basalt lattice (3-axis projection)
q = vec2(p.x * 1.1547, p.y + p.x * 0.57735);
ip = floor(q);
fp = fract(q) - 0.5;
h = fract(sin(ip.x * 127.1 + ip.y * 311.7) * 43758.5453);

# Hexagonal cell distance to column wall
ax = abs(fp.x);
ay = abs(fp.y);
hexD = max(ax, ay * 0.5 + ax * 0.866025);
seam = smoothstep(0.49, 0.49 - bevel, hexD);
rim = smoothstep(0.44, 0.48, hexD) * smoothstep(0.51, 0.48, hexD);

# Refracted water caustics across basalt tops
w1 = sin(p.x * 4.2 + sin(p.y * 3.1 + t * drift * 3.0) + h * 3.14);
w2 = cos(p.y * 4.8 - sin(p.x * 3.5 - t * drift * 2.5));
cw = clamp(0.5 + 0.5 * (w1 * 0.55 + w2 * 0.45), 0.0, 1.0);
caus = cw * cw * cw * caustic;

cBasalt = vec3(0.065, 0.082, 0.105);
cSlate = vec3(0.165, 0.220, 0.275) * (0.70 + 0.55 * h);
cCaustic = vec3(0.580, 0.820, 0.925);
cRim = vec3(0.894, 0.937, 0.961);

col = mix(cBasalt, cSlate, seam);
col = col + cCaustic * caus * seam * contrast;
col = mix(col, cRim, rim * (0.45 + 0.35 * sin(h * 12.0 + t)));
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
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * (5.5 + audio_bass * 1.4);
  vec2 q = vec2(p.x * 1.1547, p.y + p.x * 0.57735);
  vec2 ip = floor(q);
  vec2 fp = fract(q) - 0.5;
  float h = fract(sin(ip.x * 127.1 + ip.y * 311.7) * 43758.5453);

  float ax = abs(fp.x);
  float ay = abs(fp.y);
  float hexD = max(ax, ay * 0.5 + ax * 0.866025);
  float bevel = 0.14 + audio_bass * 0.05;
  float seam = smoothstep(0.49, 0.49 - bevel, hexD);
  float rim = smoothstep(0.44, 0.48, hexD) * smoothstep(0.51, 0.48, hexD);

  float w1 = sin(p.x * (4.2 + audio_mid * 2.0) + sin(p.y * 3.1 + t * 0.66) + h * 3.14);
  float w2 = cos(p.y * (4.8 + audio_mid * 2.0) - sin(p.x * 3.5 - t * 0.55));
  float cw = clamp(0.5 + 0.5 * (w1 * 0.55 + w2 * 0.45), 0.0, 1.0);
  float caus = cw * cw * cw * (0.85 + audio_mid * 0.35);

  vec3 cBasalt = vec3(0.065, 0.082, 0.105);
  vec3 cSlate = vec3(0.165, 0.220, 0.275) * (0.70 + 0.55 * h);
  vec3 cCaustic = vec3(0.580, 0.820, 0.925);
  vec3 cRim = vec3(0.894, 0.937, 0.961);

  vec3 col = mix(cBasalt, cSlate, seam);
  col = col + cCaustic * caus * seam * 0.85;
  col = mix(col, cRim, rim * (0.45 + audio_high * 0.45) * (0.5 + 0.5 * sin(h * 12.0 + t)));
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
"""
    }
]


def write_notes_block(notes):
    lines = []
    for i, n in enumerate(notes):
        lines.append(f"  i note{i} {n}")
    for i in range(len(notes)):
        lines.append(f"  b en{i} 1")
    return "\n".join(lines)


def make_embed_html(it):
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>{it['title']}</title>
</head>
<body>
  <canvas id="gl"></canvas>
  <script>
    const canvas = document.getElementById("gl");
    const gl = canvas.getContext("webgl");
    const vsSource = `attribute vec2 position; void main() {{ gl_Position = vec4(position, 0.0, 1.0); }}`;
    const fsSource = `{it['glsl']}`;

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
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1]), gl.STATIC_DRAW);
    const positionLocation = gl.getAttribLocation(program, "position");
    gl.enableVertexAttribArray(positionLocation);
    gl.vertexAttribPointer(positionLocation, 2, gl.FLOAT, false, 0, 0);

    const uRes = gl.getUniformLocation(program, "res");
    const uTime = gl.getUniformLocation(program, "t");
    const uAspect = gl.getUniformLocation(program, "aspect");
    const uBass = gl.getUniformLocation(program, "audio_bass");
    const uMid = gl.getUniformLocation(program, "audio_mid");
    const uHigh = gl.getUniformLocation(program, "audio_high");

    let audioCtx, analyser, dataArray;
    document.getElementById("btn-audio").onclick = async () => {{
      if (audioCtx) return;
      try {{
        const stream = await navigator.mediaDevices.getUserMedia({{ audio: true }});
        audioCtx = new (window.AudioContext || window.webkitAudioContext)();
        const source = audioCtx.createMediaStreamSource(stream);
        analyser = audioCtx.createAnalyser();
        analyser.fftSize = 256;
        source.connect(analyser);
        dataArray = new Uint8Array(analyser.frequencyBinCount);
        document.getElementById("btn-audio").innerText = "Microphone on";
      }} catch (e) {{
        document.getElementById("btn-audio").innerText = "Microphone blocked";
      }}
    }};

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
      let bass = 0.25 + 0.15 * Math.sin(t * 1.4);
      let mid = 0.2 + 0.12 * Math.cos(t * 0.9);
      let high = 0.15 + 0.1 * Math.sin(t * 2.1);
      if (analyser) {{
        analyser.getByteFrequencyData(dataArray);
        bass = (dataArray[1] + dataArray[2] + dataArray[3]) / (3.0 * 255.0);
        mid = (dataArray[10] + dataArray[15] + dataArray[20]) / (3.0 * 255.0);
        high = (dataArray[40] + dataArray[55] + dataArray[70]) / (3.0 * 255.0);
      }}
      gl.uniform2f(uRes, canvas.width, canvas.height);
      gl.uniform1f(uTime, t);
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
            f.write(it["code"].rstrip() + "\n")
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

        frame_png = os.path.join(TMP_DIR, f"{slug}.png")
        fp = subprocess.run(CMD + ["frame", test_inf, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        if '"ok":true' not in fp.stdout or not os.path.exists(frame_png):
            print(f"FAIL frame: {slug}\n{fp.stdout}\n{fp.stderr}")
            failed = True
            continue

        im = Image.open(frame_png)
        lum = ImageStat.Stat(im.convert("L")).mean[0]
        sc = library.interest(frame_png)
        if not (14.0 <= lum <= 228.0) or sc < 0.45:
            print(f"FAIL metrics {slug}: lum={lum:.1f}, interest={sc:.3f}")
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
            f.write(it["code"].rstrip() + "\n")
        it["bytes"] = os.path.getsize(js_file)

        escaped = it["code"].replace("\\", "\\\\").replace("\n", "\\n")
        inf_content = f"""infinite-patch 1
node 1 3D Sketch 3D
  id sk3d
  pos 0 0
  s code {escaped}
end
node 2 3D Render 3D
  id r3d
  pos 360 0
  f width 1080
  f height 1080
end
node 3 Utility Output
  id out
  pos 720 0
end
geo r3d 0 sk3d
cable out 0 r3d
"""
        test_inf = os.path.join(TMP_DIR, f"{slug}.inf")
        with open(test_inf, "w") as f:
            f.write(inf_content)

        v = subprocess.run(CMD + ["validate", test_inf], capture_output=True, text=True)
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}\n{v.stderr}")
            failed = True
            continue

        frame_png = os.path.join(TMP_DIR, f"{slug}.png")
        fp = subprocess.run(CMD + ["frame", test_inf, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        if '"ok":true' not in fp.stdout or not os.path.exists(frame_png):
            print(f"FAIL frame: {slug}\n{fp.stdout}\n{fp.stderr}")
            failed = True
            continue

        im = Image.open(frame_png)
        lum = ImageStat.Stat(im.convert("L")).mean[0]
        sc = library.interest(frame_png)
        if not (14.0 <= lum <= 228.0) or sc < 0.45:
            print(f"FAIL metrics {slug}: lum={lum:.1f}, interest={sc:.3f}")
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
        notes_blk = write_notes_block(it["notes"])
        inf_content = f"""infinite-patch 1
# {it['title']}
node 1 Notes Note Sequencer
  id notes
  pos 0 1024
  f rateBeats 0.5
  i steps 16
{notes_blk}
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

        sum_json = os.path.join(TMP_DIR, f"{slug}_audio.json")
        s = subprocess.run(CMD + ["audio", inf_file, sum_json], capture_output=True, text=True)
        if s.returncode != 0 or not os.path.exists(sum_json):
            print(f"FAIL audio: {slug}\n{s.stdout}\n{s.stderr}")
            failed = True
            continue

        d = json.load(open(sum_json))
        lufs = d.get("integrated_lufs")
        peak = d.get("sample_peak_dbfs")
        silence = d.get("silence_ratio")
        if lufs is None or not (-26.0 <= lufs <= -8.0) or peak is None or peak >= -0.5 or silence != 0:
            print(f"FAIL audio gates {slug}: lufs={lufs}, peak={peak}, silence={silence}")
            failed = True
            continue

        frame_png = os.path.join(TMP_DIR, f"{slug}.png")
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
        notes_blk = write_notes_block(it["notes"])
        inf_content = f"""infinite-patch 1
# {it['title']}
node 1 Notes Note Sequencer
  id notes
  pos 0 1024
  f rateBeats 0.5
  i steps 16
{notes_blk}
end
node 2 Synths Analog
  id osc
  pos 360 1024
  f cutoff 2400.0
  f resonance 0.25
  f decay 0.45
  f volume 0.85
end
node 3 AudioEffects Field Effect
  id fx
  pos 720 1024
  s code {escaped}
end
node 4 Utility Audio Out
  id speakers
  pos 1080 1024
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

        sum_json = os.path.join(TMP_DIR, f"{slug}_audio.json")
        s = subprocess.run(CMD + ["audio", inf_file, sum_json], capture_output=True, text=True)
        if s.returncode != 0 or not os.path.exists(sum_json):
            print(f"FAIL audio: {slug}\n{s.stdout}\n{s.stderr}")
            failed = True
            continue

        d = json.load(open(sum_json))
        lufs = d.get("integrated_lufs")
        peak = d.get("sample_peak_dbfs")
        silence = d.get("silence_ratio")
        if lufs is None or not (-26.0 <= lufs <= -8.0) or peak is None or peak >= -0.5 or silence != 0:
            print(f"FAIL audio gates {slug}: lufs={lufs}, peak={peak}, silence={silence}")
            failed = True
            continue

        frame_png = os.path.join(TMP_DIR, f"{slug}.png")
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

        frame_png = os.path.join(TMP_DIR, f"{slug}.png")
        fp = subprocess.run(CMD + ["frame", test_inf, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        if '"ok":true' not in fp.stdout or not os.path.exists(frame_png):
            print(f"FAIL frame: {slug}\n{fp.stdout}\n{fp.stderr}")
            failed = True
            continue

        im = Image.open(frame_png)
        lum = ImageStat.Stat(im.convert("L")).mean[0]
        sc = library.interest(frame_png)
        if not (14.0 <= lum <= 228.0) or sc < 0.45:
            print(f"FAIL metrics {slug}: lum={lum:.1f}, interest={sc:.3f}")
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
                "tags": ["synth", "clavinet", "marimba", "audio", "dsp"],
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
                "tags": ["effect", "hilbert", "wavefolder", "tape", "audio", "dsp"],
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
                tb_data["counts"][key] = {"font-size": 2, "hex": 6}
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
