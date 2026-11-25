#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 51.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: ukiyoe_cartography -> ukiyoe-contour-archipelago, ukiyoe-seigaiha-tide-chart)
2. Sketch 3D (Theme: brutalist_anechoic_vault -> anechoic-wedge-chamber-3d, brutalist-coffered-oculus-3d)
3. Field Synth (Themes: modal_vibraphone_mallet, polivoks_dual_opamp_synth -> modal-vibraphone-mallet-synth, polivoks-dual-opamp-synth)
4. Field Effects (Themes: dub_tape_pingpong_fx, bode_frequency_shifter_fx -> dub-tape-dispersion-fx, bode-frequency-shifter-fx)
5. Field Pixel & Embeds (Themes: phosphor_lissajous_scope, voronoi_basalt_fracture -> phosphor-lissajous-scope, voronoi-basalt-fracture)
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
# 1. Sketch 2D Sources (Theme: ukiyoe_cartography)
#    Palette: #E4DFCE (washi), #163759 (prussian blue), #8FB5AC (celadon),
#             #C6A76C (ochre), #B83A2A (vermilion hanko), #191E24 (sumi ink)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "ukiyoe-contour-archipelago",
        "title": "Ukiyo-e Contour Archipelago",
        "description": "Edo-period woodblock bathymetric archipelago with Prussian-blue contour rings, celadon coastal shoals, compass rhumb lines, and a vermilion hanko cartouche.",
        "tags": ["ukiyoe", "woodblock", "cartography", "contour", "prussian", "procedural", "sketch"],
        "code": """// Ukiyo-e Contour Archipelago - Infinite Library (Sketch 2D)
param("contours", 16, 8, 28);
param("swell", 0.35, 0.05, 0.9);
param("shoals", 5, 2, 9);
param("drift", 0.14, 0.0, 0.5);

function fieldVal(nx, ny, t) {
  const d1 = sqrt((nx + 0.18) * (nx + 0.18) + (ny - 0.12) * (ny - 0.12));
  const d2 = sqrt((nx - 0.24) * (nx - 0.24) + (ny + 0.18) * (ny + 0.18));
  const d3 = sqrt((nx - 0.08) * (nx - 0.08) + (ny - 0.32) * (ny - 0.32));
  const undul = 0.08 * sin(nx * 7.0 + t * drift) * cos(ny * 6.0 - t * drift * 0.7);
  const ridge = exp(-d1 * 4.2) + 0.85 * exp(-d2 * 5.0) + 0.65 * exp(-d3 * 6.5) + undul;
  return ridge;
}

function draw(t) {
  // Warm aged washi paper (#E4DFCE)
  background(0.894, 0.875, 0.808);
  const w = width;
  const h = height;
  const margin = min(w, h) * 0.085;
  const span = min(w, h) - margin * 2.0;
  const x0 = (w - span) * 0.5;
  const y0 = (h - span) * 0.5;

  // Outer woodblock registration double border (sumi ink #191E24)
  noFill();
  stroke(0.098, 0.118, 0.141, 0.92);
  strokeWeight(2.6);
  rect(x0, y0, span, span);
  strokeWeight(1.0);
  rect(x0 + 8, y0 + 8, span - 16, span - 16);

  // Subtle ochre wood-grain horizon washes (#C6A76C)
  noStroke();
  for (let b = 0; b < 5; b++) {
    const by = y0 + 18 + b * (span * 0.045);
    fill(0.776, 0.655, 0.424, 0.18 - b * 0.025);
    rect(x0 + 10, by, span - 20, span * 0.026);
  }

  // Compass rose & rhumb lines radiating from maritime datum
  const rx = x0 + span * 0.72;
  const ry = y0 + span * 0.26;
  stroke(0.098, 0.118, 0.141, 0.18);
  strokeWeight(0.9);
  for (let i = 0; i < 16; i++) {
    const a = (i * TAU) / 16.0;
    line(rx, ry, rx + cos(a) * span * 0.85, ry + sin(a) * span * 0.85);
  }
  stroke(0.086, 0.216, 0.349, 0.45);
  strokeWeight(1.3);
  circle(rx, ry, span * 0.11);
  circle(rx, ry, span * 0.035);

  // Bathymetric contour lattice via marching-style cell segments
  const res = 54;
  const cw = (span - 24) / res;
  const ox = x0 + 12;
  const oy = y0 + 12;
  const levels = floor(contours);

  for (let iy = 0; iy < res; iy++) {
    for (let ix = 0; ix < res; ix++) {
      const nx = (ix / res) * 2.0 - 1.0;
      const ny = (iy / res) * 2.0 - 1.0;
      const v = fieldVal(nx, ny, t);
      const q = floor(v * levels);
      const frac = v * levels - q;

      const px = ox + ix * cw;
      const py = oy + iy * cw;

      // Coastal celadon shoal wash (#8FB5AC)
      if (v > 0.28 && v < 0.56) {
        noStroke();
        fill(0.561, 0.710, 0.675, 0.34);
        rect(px, py, cw + 0.8, cw + 0.8);
      } else if (v >= 0.56 && v < 0.82) {
        noStroke();
        fill(0.776, 0.655, 0.424, 0.32);
        rect(px, py, cw + 0.8, cw + 0.8);
      }

      // Crisp Prussian blue woodblock contour hatches (#163759)
      if (frac < 0.28) {
        const ang = atan2(ny + 0.12, nx + 0.18) + PI * 0.5 + sin(v * 12.0 + t * drift) * swell * 0.35;
        const dx = cos(ang) * cw * 0.65;
        const dy = sin(ang) * cw * 0.65;
        stroke(0.086, 0.216, 0.349, v > 0.45 ? 0.92 : 0.68);
        strokeWeight(v > 0.65 ? 2.1 : 1.35);
        line(px + cw * 0.5 - dx, py + cw * 0.5 - dy, px + cw * 0.5 + dx, py + cw * 0.5 + dy);
      }

      // Fine foam stipple dots along wave crests
      if ((ix * 7 + iy * 13) % floor(shoals + 4) === 0 && v > 0.18 && v < 0.42) {
        noStroke();
        fill(0.086, 0.216, 0.349, 0.48);
        circle(px + cw * 0.5, py + cw * 0.5, 2.2);
      }
    }
  }

  // Vermilion Edo cartouche & hanko seal (#B83A2A)
  const cx = x0 + 26;
  const cy = y0 + 26;
  fill(0.894, 0.875, 0.808, 0.94);
  stroke(0.098, 0.118, 0.141, 0.92);
  strokeWeight(1.8);
  rect(cx, cy, 44, 152);
  stroke(0.722, 0.227, 0.165, 0.95);
  strokeWeight(1.4);
  rect(cx + 5, cy + 5, 34, 142);

  // Inner geometric woodblock seal glyph
  noStroke();
  fill(0.722, 0.227, 0.165, 0.94);
  rect(cx + 10, cy + 114, 24, 24);
  fill(0.894, 0.875, 0.808, 0.95);
  rect(cx + 15, cy + 119, 14, 4);
  rect(cx + 20, cy + 119, 4, 14);
}
"""
    },
    {
        "slug": "ukiyoe-seigaiha-tide-chart",
        "title": "Ukiyo-e Seigaiha Tide Chart",
        "description": "Layered Japanese seigaiha wave fans modulated by tidal harmonic interference on aged washi paper with Prussian-blue, celadon, and ochre woodblock inks.",
        "tags": ["ukiyoe", "seigaiha", "wave", "woodblock", "japanese", "procedural", "sketch"],
        "code": """// Ukiyo-e Seigaiha Tide Chart - Infinite Library (Sketch 2D)
param("rows", 11, 6, 16);
param("arcs", 5, 3, 8);
param("tideAmp", 0.28, 0.0, 0.7);
param("drift", 0.18, 0.0, 0.6);

function draw(t) {
  // Warm aged washi ground (#E4DFCE)
  background(0.894, 0.875, 0.808);
  const w = width;
  const h = height;
  const margin = min(w, h) * 0.09;
  const span = min(w, h) - margin * 2.0;
  const x0 = (w - span) * 0.5;
  const y0 = (h - span) * 0.5;

  // Ochre woodblock sky gradient band (#C6A76C)
  noStroke();
  for (let i = 0; i < 10; i++) {
    fill(0.776, 0.655, 0.424, 0.24 * (1.0 - i / 10.0));
    rect(x0, y0 + i * (span * 0.025), span, span * 0.026);
  }

  // Seigaiha fan lattice
  const nRows = floor(rows);
  const nCols = nRows;
  const cellW = span / nCols;
  const cellH = (span * 0.76) / nRows;
  const maxR = cellW * 0.68;
  const nArcs = floor(arcs);

  for (let r = 0; r < nRows; r++) {
    const shift = (r % 2 === 1) ? cellW * 0.5 : 0.0;
    const cyBase = y0 + span * 0.24 + r * cellH;
    for (let c = -1; c <= nCols; c++) {
      const cx = x0 + c * cellW + shift;
      if (cx < x0 - cellW * 0.35 || cx > x0 + span + cellW * 0.35) continue;

      const phase = sin(c * 0.55 + r * 0.42 - t * drift * 2.2) * tideAmp;
      const cy = cyBase + phase * cellH * 0.55;

      // Solid mask behind each fan to create authentic overlapping scales
      noStroke();
      fill(0.894, 0.875, 0.808);
      beginShape();
      for (let s = 0; s <= 18; s++) {
        const ang = PI + (s / 18.0) * PI;
        vertex(cx + cos(ang) * maxR, cy + sin(ang) * maxR);
      }
      endShape();

      // Concentric Prussian blue (#163759) & celadon (#8FB5AC) arcs
      noFill();
      for (let a = 0; a < nArcs; a++) {
        const u = 1.0 - a / nArcs;
        const rad = maxR * u;
        if (rad < 3.0) continue;
        if (a % 2 === 0) {
          stroke(0.086, 0.216, 0.349, 0.92);
          strokeWeight(a === 0 ? 2.2 : 1.5);
        } else {
          stroke(0.561, 0.710, 0.675, 0.90);
          strokeWeight(1.8);
        }
        beginShape();
        for (let s = 0; s <= 18; s++) {
          const ang = PI + (s / 18.0) * PI;
          vertex(cx + cos(ang) * rad, cy + sin(ang) * rad);
        }
        endShape();
      }

      // Vermilion crest accent on resonant peaks (#B83A2A)
      if (phase > tideAmp * 0.45 && (r + c) % 3 === 0) {
        stroke(0.722, 0.227, 0.165, 0.88);
        strokeWeight(1.8);
        beginShape();
        for (let s = 0; s <= 10; s++) {
          const ang = PI * 1.15 + (s / 10.0) * (PI * 0.70);
          vertex(cx + cos(ang) * maxR * 0.22, cy + sin(ang) * maxR * 0.22);
        }
        endShape();
      }
    }
  }

  // Crisp frame mask & woodblock border
  noFill();
  stroke(0.098, 0.118, 0.141, 0.94);
  strokeWeight(3.0);
  rect(x0, y0, span, span);
  strokeWeight(1.1);
  rect(x0 + 8, y0 + 8, span - 16, span - 16);

  // Registration ticks along bottom datum
  stroke(0.722, 0.227, 0.165, 0.85);
  strokeWeight(1.4);
  for (let i = 1; i < 12; i++) {
    const tx = x0 + (i / 12.0) * span;
    line(tx, y0 + span + 6, tx, y0 + span + 14);
  }
}
"""
    }
]


# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: brutalist_anechoic_vault)
#    Palette: #232A30 (slate), #7A8284 (concrete), #4E5146 (olive-charcoal), #D9A84E (sodium amber)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "anechoic-wedge-chamber-3d",
        "title": "Anechoic Wedge Chamber 3D",
        "description": "Orthogonal acoustic anechoic chamber with alternating pyramidal absorption wedges, suspended calibration walkway, and sodium-amber datum frame.",
        "tags": ["anechoic", "acoustics", "brutalist", "wedges", "architecture", "3d", "sketch"],
        "code": """// Anechoic Wedge Chamber 3D - Infinite Library (Sketch 3D)
param("clusters", 5, 3, 7);
param("wedgeLen", 0.52, 0.25, 0.85);
param("breath", 0.12, 0.0, 0.35);
param("spin", 0.14, 0.0, 0.5);

function draw(t) {
  const n = floor(clusters);
  rotateY(t * spin + 0.58);
  rotateX(0.32);

  // Concrete chamber base slab (#232A30)
  push();
  translate(0, -0.95, 0);
  fill(0.137, 0.165, 0.188);
  box(2.7, 0.1, 2.7);
  pop();

  // Suspended sodium-amber acoustic measurement datum frame (#D9A84E)
  push();
  translate(0, -0.58, 0);
  fill(0.851, 0.659, 0.306);
  box(1.9, 0.03, 0.04);
  box(0.04, 0.03, 1.9);
  pop();

  // Alternating orthogonal acoustic absorption wedge clusters
  const step = 0.44;
  const half = (n - 1) * 0.5;
  for (let iy = 0; iy < n; iy++) {
    for (let ix = 0; ix < n; ix++) {
      const px = (ix - half) * step;
      const py = (iy - half) * step + 0.05;
      const parity = (ix + iy) % 2;
      const pulse = 1.0 + breath * sin(t * 1.4 + ix * 0.6 + iy * 0.5);

      // Back wall wedge cluster
      push();
      translate(px, py, -1.05);
      rotateX(PI * 0.5);
      if (parity === 1) rotateY(PI * 0.5);
      fill(parity === 0 ? 0.306 : 0.478, parity === 0 ? 0.318 : 0.510, parity === 0 ? 0.275 : 0.518);
      cone(0.19, wedgeLen * pulse);
      pop();

      // Left wall wedge cluster
      push();
      translate(-1.05, py, px);
      rotateZ(-PI * 0.5);
      if (parity === 0) rotateY(PI * 0.5);
      fill(parity === 1 ? 0.306 : 0.420, parity === 1 ? 0.318 : 0.451, parity === 1 ? 0.275 : 0.460);
      cone(0.19, wedgeLen * pulse);
      pop();

      // Floor sub-grid wedges
      if (ix > 0 && iy > 0 && ix < n - 1 && iy < n - 1) {
        push();
        translate(px, -0.78, (iy - half) * step);
        fill(0.24, 0.26, 0.25);
        cone(0.16, wedgeLen * 0.65);
        pop();
      }
    }
  }

  // Central omnidirectional measurement microphone mast
  push();
  translate(0, -0.1, 0);
  fill(0.851, 0.659, 0.306);
  cylinder(0.025, 0.95);
  translate(0, 0.52, 0);
  fill(0.92, 0.93, 0.95);
  sphere(0.085);
  pop();
}
"""
    },
    {
        "slug": "brutalist-coffered-oculus-3d",
        "title": "Brutalist Coffered Oculus 3D",
        "description": "Monumental radial coffered concrete vault ribs converging toward a stepped oculus with recessed acoustic bays and a warm sodium-amber light ring.",
        "tags": ["brutalist", "coffered", "vault", "concrete", "architecture", "3d", "sketch"],
        "code": """// Brutalist Coffered Oculus 3D - Infinite Library (Sketch 3D)
param("ribs", 16, 8, 24);
param("tiers", 4, 2, 6);
param("radius", 1.35, 0.9, 1.9);
param("spin", 0.12, 0.0, 0.5);

function draw(t) {
  const nRibs = floor(ribs);
  const nTiers = floor(tiers);
  rotateY(t * spin + 0.4);
  rotateX(0.42);

  // Brutalist plinth & perimeter ring (#232A30)
  push();
  translate(0, -0.82, 0);
  fill(0.137, 0.165, 0.188);
  cylinder(radius * 1.08, 0.12);
  pop();

  // Radial concrete structural vault ribs (#7A8284)
  for (let i = 0; i < nRibs; i++) {
    const a = (i * TAU) / nRibs;
    push();
    rotateY(a);
    for (let k = 0; k < nTiers; k++) {
      const u = k / nTiers;
      const r = radius * (0.95 - u * 0.62);
      const y = -0.52 + u * 1.15;
      const tilt = 0.25 + u * 0.72;
      push();
      translate(r, y, 0);
      rotateZ(tilt);
      fill(k % 2 === 0 ? 0.478 : 0.365, k % 2 === 0 ? 0.510 : 0.400, k % 2 === 0 ? 0.518 : 0.415);
      box(0.11, 0.46, 0.16);
      pop();
    }
    pop();
  }

  // Stepped horizontal coffer rings & recessed shadow bays
  for (let k = 0; k < nTiers; k++) {
    const u = k / nTiers;
    const r = radius * (0.92 - u * 0.58);
    const y = -0.35 + u * 0.95;
    for (let i = 0; i < nRibs; i++) {
      const a = ((i + 0.5) * TAU) / nRibs;
      push();
      rotateY(a);
      translate(r, y, 0);
      fill(0.20, 0.23, 0.25);
      box(0.08, 0.08, (TAU * r / nRibs) * 0.85);
      pop();
    }
  }

  // Warm sodium-amber oculus light collar (#D9A84E)
  const oculusR = radius * 0.28;
  for (let i = 0; i < 12; i++) {
    const a = (i * TAU) / 12.0;
    push();
    rotateY(a);
    translate(oculusR, 0.72, 0);
    fill(0.851, 0.659, 0.306);
    box(0.06, 0.08, (TAU * oculusR / 12.0) * 0.92);
    pop();
  }
}
"""
    }
]


# -------------------------------------------------------------
# 3. Field Synth Sources
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "modal-vibraphone-mallet-synth",
        "title": "Modal Vibraphone Mallet Synth",
        "description": "Struck modal vibraphone resonator with inharmonic bar overtones (1:3.99:9.96), mallet hardness transient, and motor-driven tremolo.",
        "code": """param float malletHard = 0.58 [0.1, 1.0];
param float motorHz = 4.2 [0.5, 9.0];
param float motorDepth = 0.38 [0.0, 0.85];
param float barSpread = 0.18 [0.0, 0.6];
param float decay = 0.9994 [0.997, 0.9997];
state float p1 = 0; state float p2 = 0; state float p3 = 0; state float pMot = 0;
state float env = 0; state float strike = 1.0; state float lp = 0;

env = if(gate > 0.5, env + (1.0 - env) * 0.26, env * decay);
strike = if(gate > 0.5, strike * 0.991, 1.0);

f0 = clamp(freq, 65.0, 2800.0);
p1 = (p1 + f0 / sr) % 1.0;
p2 = (p2 + (f0 * (3.99 + barSpread * 0.08)) / sr) % 1.0;
p3 = (p3 + (f0 * (9.96 - barSpread * 0.12)) / sr) % 1.0;
pMot = (pMot + motorHz / sr) % 1.0;

trem = 1.0 - motorDepth * 0.5 * (1.0 + sin(6.2831853 * pMot));
bar = (sin(6.2831853 * p1) + sin(6.2831853 * p2) * (0.25 + malletHard * 0.35) * strike + sin(6.2831853 * p3) * (0.12 + malletHard * 0.25) * strike * strike) * env * trem;

g = clamp(2.0 * sin(3.14159265 * (1200.0 + malletHard * 3600.0) / sr), 0.04, 0.88);
lp = lp + g * (bar - lp);
out = (lp / (1.0 + abs(lp) * 0.45)) * 0.68;
""",
        "params": {
            "malletHard": 0.58,
            "motorHz": 4.2,
            "motorDepth": 0.38,
            "barSpread": 0.18,
            "decay": 0.9994
        },
        "notes": [60, 64, 67, 71, 69, 67, 64, 62, 60, 67, 72, 71, 69, 64, 62, 59]
    },
    {
        "slug": "polivoks-dual-opamp-synth",
        "title": "Polivoks Dual Op-Amp Synth",
        "description": "Detuned dual-oscillator saw/pulse synth with Soviet op-amp resonant state-variable filter sweep, wavefolder grit, and sub-octave drive.",
        "code": """param float detune = 0.012 [0.001, 0.04];
param float fold = 1.45 [0.5, 3.2];
param float cutoffHz = 950.0 [180.0, 4200.0];
param float res = 0.62 [0.1, 0.88];
param float envAmt = 2200.0 [200.0, 5000.0];
state float p1 = 0; state float p2 = 0; state float pSub = 0;
state float env = 0; state float bp = 0; state float lp = 0;

env = if(gate > 0.5, env + (1.0 - env) * 0.18, env * 0.9980);

f0 = clamp(freq, 40.0, 2200.0);
p1 = (p1 + f0 / sr) % 1.0;
p2 = (p2 + (f0 * (1.0 + detune)) / sr) % 1.0;
pSub = (pSub + (f0 * 0.5) / sr) % 1.0;

raw = ((2.0 * p1 - 1.0) * 0.48 + if(p2 < 0.42, 0.36, -0.36) + sin(6.2831853 * pSub) * 0.52) * fold;
folded = sin(raw * 1.35) * 0.72 + (raw / (1.0 + abs(raw))) * 0.28;

g = clamp(2.0 * sin(3.14159265 * clamp(cutoffHz + env * envAmt, 120.0, 9500.0) / sr), 0.02, 0.84);
q = clamp(1.15 - res * 0.95, 0.14, 1.1);
bp = clamp(bp + g * (folded - lp - q * bp), -1.6, 1.6);
bp = bp / (1.0 + abs(bp) * 0.18);
lp = lp + g * bp;

out = (lp * env / (1.0 + abs(lp * env) * 0.45)) * 0.64;
""",
        "params": {
            "detune": 0.012,
            "fold": 1.45,
            "cutoffHz": 950.0,
            "res": 0.62,
            "envAmt": 2200.0
        },
        "notes": [45, 45, 48, 45, 50, 45, 52, 48, 45, 57, 55, 52, 50, 48, 45, 43]
    }
]


# -------------------------------------------------------------
# 4. Field Effects Sources
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "dub-tape-dispersion-fx",
        "title": "Dub Tape Dispersion FX",
        "description": "Asymmetric tape-saturated dub resonator with capstan wow drift, 4-stage allpass phase dispersion, and damped bandpass feedback.",
        "code": """param float wowHz = 0.52 [0.1, 2.2];
param float drive = 1.9 [0.8, 3.8];
param float dispersion = 0.62 [0.1, 0.9];
param float dubHz = 1350.0 [350.0, 4500.0];
param float mix = 0.72 [0.0, 1.0];
state float pWow = 0;
state float ap1 = 0; state float ap2 = 0; state float ap3 = 0; state float ap4 = 0;
state float bp = 0; state float lp = 0; state float fb = 0;

pWow = (pWow + wowHz / sr) % 1.0;
c = clamp(dispersion + 0.18 * sin(6.2831853 * pWow), 0.12, 0.88);

x = in + fb * 0.42;
# Asymmetric magnetic tape saturation
asym = x + 0.12 * x * x;
sat = (asym * drive) / (1.0 + abs(asym * drive) * 0.72);

# 4-stage allpass dispersion network
y1 = -c * sat + ap1; ap1 = sat + c * y1;
y2 = -c * y1 + ap2; ap2 = y1 + c * y2;
y3 = -c * y2 + ap3; ap3 = y2 + c * y3;
y4 = -c * y3 + ap4; ap4 = y3 + c * y4;

# Damped dub bandpass feedback tone
g = clamp(2.0 * sin(3.14159265 * dubHz / sr), 0.03, 0.82);
bp = bp + g * (y4 - lp - 0.62 * bp);
lp = lp + g * bp;
fb = clamp(lp * 0.55 + bp * 0.45, -1.0, 1.0);

wet = y4 * 0.55 + lp * 0.45;
out = (in * (1.0 - mix) + wet * mix) * 0.64;
""",
        "params": {
            "wowHz": 0.52,
            "drive": 1.9,
            "dispersion": 0.62,
            "dubHz": 1350.0,
            "mix": 0.72
        },
        "notes": [48, 55, 58, 60, 58, 55, 51, 48, 48, 60, 63, 62, 58, 55, 53, 51]
    },
    {
        "slug": "bode-frequency-shifter-fx",
        "title": "Bode Frequency Shifter FX",
        "description": "Single-sideband Hilbert quadrature frequency shifter with dual allpass phase-splitting network, carrier detune, and metallic feedback.",
        "code": """param float shiftHz = 18.5 [-120.0, 120.0];
param float feedback = 0.28 [0.0, 0.65];
param float toneHz = 3600.0 [800.0, 8000.0];
param float mix = 0.65 [0.0, 1.0];
state float pCar = 0;
state float i1 = 0; state float i2 = 0;
state float q1 = 0; state float q2 = 0;
state float lp = 0; state float fb = 0;

pCar = (pCar + abs(shiftHz) / sr) % 1.0;
ang = 6.2831853 * pCar * if(shiftHz >= 0.0, 1.0, -1.0);

x = in + fb * feedback;

# Quadrature allpass Hilbert approximation pair
cI1 = 0.6923; cI2 = 0.9360;
cQ1 = 0.4021; cQ2 = 0.8561;

yi1 = -cI1 * x + i1; i1 = x + cI1 * yi1;
sigI = -cI2 * yi1 + i2; i2 = yi1 + cI2 * sigI;

yq1 = -cQ1 * x + q1; q1 = x + cQ1 * yq1;
sigQ = -cQ2 * yq1 + q2; q2 = yq1 + cQ2 * sigQ;

# Single-sideband modulation: I*cos - Q*sin
ssb = sigI * cos(ang) - sigQ * sin(ang);

g = clamp(2.0 * sin(3.14159265 * toneHz / sr), 0.05, 0.88);
lp = lp + g * (ssb - lp);
fb = clamp(lp, -1.0, 1.0);

out = (in * (1.0 - mix) + lp * mix) * 0.62;
""",
        "params": {
            "shiftHz": 18.5,
            "feedback": 0.28,
            "toneHz": 3600.0,
            "mix": 0.65
        },
        "notes": [57, 60, 64, 67, 69, 67, 64, 60, 55, 59, 62, 67, 64, 62, 60, 57]
    }
]


# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embed Sources
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "phosphor-lissajous-scope",
        "title": "Phosphor Lissajous Scope",
        "description": "Tektronix P31 phosphor CRT oscilloscope tracing harmonic Lissajous knots with exponential beam bloom, graticule ticks, and CRT glass vignette.",
        "embed_desc": "Tektronix P31 phosphor CRT oscilloscope Lissajous trace. Bass drives harmonic knot scale, mids modulate phase ratio, highs sharpen phosphor beam bloom.",
        "tags": ["oscilloscope", "lissajous", "phosphor", "crt", "vector", "shader", "pixel"],
        "code": """param float ratioX = 3.0 [1.0, 7.0];
param float ratioY = 4.0 [1.0, 8.0];
param float beamGlow = 0.018 [0.005, 0.045];
param float phaseDrift = 0.42 [0.0, 1.5];
param float graticule = 0.35 [0.0, 0.8];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.25;

# CRT graticule grid and center axis ticks
gx = abs(fract(p.x * 2.5 + 0.5) - 0.5);
gy = abs(fract(p.y * 2.5 + 0.5) - 0.5);
grid = (1.0 - smoothstep(0.0, 0.014, min(gx, gy))) * 0.22;
axis = (1.0 - smoothstep(0.0, 0.012, min(abs(p.x), abs(p.y)))) * 0.45;
tickX = (1.0 - smoothstep(0.0, 0.012, abs(p.y))) * (1.0 - smoothstep(0.0, 0.06, abs(fract(p.x * 10.0 + 0.5) - 0.5))) * 0.35;
tickY = (1.0 - smoothstep(0.0, 0.012, abs(p.x))) * (1.0 - smoothstep(0.0, 0.06, abs(fract(p.y * 10.0 + 0.5) - 0.5))) * 0.35;

# Analytical Lissajous curve branches (3:4 harmonic knot, zero-loop)
r = 0.76;
nx = clamp(p.x / r, -0.999, 0.999);
th = atan2(nx, sqrt(max(1.0 - nx * nx, 0.0001)));
ph = t * phaseDrift + 0.65;
mask = step(abs(p.x), r);

a0 = (th + ph) / ratioX;
a1 = (3.14159265 - th + ph) / ratioX;
a2 = (th + 6.2831853 + ph) / ratioX;
a3 = (3.14159265 - th + 6.2831853 + ph) / ratioX;
a4 = (th + 12.5663706 + ph) / ratioX;
a5 = (3.14159265 - th + 12.5663706 + ph) / ratioX;

d0 = abs(p.y - r * sin(ratioY * a0));
d1 = abs(p.y - r * sin(ratioY * a1));
d2 = abs(p.y - r * sin(ratioY * a2));
d3 = abs(p.y - r * sin(ratioY * a3));
d4 = abs(p.y - r * sin(ratioY * a4));
d5 = abs(p.y - r * sin(ratioY * a5));
minD = min(min(min(d0, d1), min(d2, d3)), min(d4, d5)) + (1.0 - mask) * 5.0;

# Secondary harmonic envelope ring
ringD = abs(length(p) - 0.88) * 1.4;
minD = min(minD, ringD + 0.012);

core = exp(-minD / (beamGlow * 0.55));
halo = exp(-minD / (beamGlow * 3.4)) * 0.65;
beam = clamp(core + halo, 0.0, 1.4);

cBg = vec3(0.024, 0.169, 0.278);
cGrid = vec3(0.094, 0.482, 0.424);
cBeam = vec3(0.227, 1.000, 0.659);
cHot = vec3(0.880, 1.000, 0.940);

col = cBg + cGrid * (grid + axis + tickX + tickY) * graticule;
col = col + cBeam * halo + cHot * core * 0.85;
vig = smoothstep(1.35, 0.35, length(p * 0.65));
col = clamp(col * (0.45 + 0.55 * vig), 0.0, 1.0);
""",
        "params": {
            "ratioX": 3.0,
            "ratioY": 4.0,
            "beamGlow": 0.018,
            "phaseDrift": 0.42,
            "graticule": 0.35
        },
        "glsl_frag": """precision highp float;
uniform vec2 res;
uniform float t;
uniform float aspect;
uniform float audio_bass;
uniform float audio_mid;
uniform float audio_high;

void main() {
  vec2 uv = gl_FragCoord.xy / res;
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * (2.25 - audio_bass * 0.22);

  float gx = abs(fract(p.x * 2.5 + 0.5) - 0.5);
  float gy = abs(fract(p.y * 2.5 + 0.5) - 0.5);
  float grid = (1.0 - smoothstep(0.0, 0.014, min(gx, gy))) * 0.22;
  float axis = (1.0 - smoothstep(0.0, 0.012, min(abs(p.x), abs(p.y)))) * 0.45;

  float minD = 10.0;
  float ph = t * 0.42 + 0.65 + audio_mid * 0.9;
  float rX = 3.0;
  float rY = 4.0 + floor(audio_mid * 1.5);
  for (int k = 0; k < 56; k++) {
    float a = float(k) * 0.1122;
    vec2 lp = vec2(0.78 * sin(rX * a + ph), 0.78 * sin(rY * a));
    minD = min(minD, length(p - lp));
  }

  float glow = 0.018 + audio_high * 0.016;
  float core = exp(-minD / (glow * 0.35));
  float halo = exp(-minD / (glow * 2.8)) * (0.55 + audio_bass * 0.35);

  vec3 cBg = vec3(0.024, 0.169, 0.278);
  vec3 cGrid = vec3(0.094, 0.482, 0.424);
  vec3 cBeam = vec3(0.227, 1.000, 0.659);
  vec3 cHot = vec3(0.880, 1.000, 0.940);

  vec3 col = cBg + cGrid * (grid + axis) * 0.35;
  col += cBeam * halo + cHot * core * 0.85;
  float vig = smoothstep(1.35, 0.35, length(p * 0.65));
  col *= (0.45 + 0.55 * vig);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "voronoi-basalt-fracture",
        "title": "Voronoi Basalt Fracture",
        "description": "Polygonal Voronoi columnar basalt fracture field with faceted stone bevels, mineral shadow relief, and glowing ember fissure seams.",
        "embed_desc": "Polygonal Voronoi columnar basalt fracture field. Bass pulses tectonic elevation, mids shift mineral lighting angle, highs ignite ember fissure seams.",
        "tags": ["voronoi", "basalt", "geology", "fracture", "stone", "shader", "pixel"],
        "code": """param float scale = 4.8 [2.0, 9.0];
param float seamWidth = 0.065 [0.02, 0.16];
param float emberGlow = 0.85 [0.1, 1.5];
param float drift = 0.18 [0.0, 0.6];
param float bevel = 0.75 [0.2, 1.0];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * scale;
# Subtle geological warp
pw = p + vec2(0.14 * sin(p.y * 1.9 + t * drift), 0.14 * cos(p.x * 1.7 - t * drift));

# Hexagonal columnar basalt lattice (two offset rectangular grids)
s = vec2(1.0, 1.7320508);
h = s * 0.5;
a = pw - s * floor(pw / s) - h;
b = (pw - h) - s * floor((pw - h) / s) - h;
da = dot(a, a);
db = dot(b, b);
gv = if(da < db, a, b);
id = pw - gv;

# Exact distance to hexagonal column boundary
px = abs(gv.x);
py = abs(gv.y);
hexDist = max(dot(vec2(px, py), normalize(s)), px);
edgeDist = 0.5 - hexDist;

seam = 1.0 - smoothstep(0.0, seamWidth, edgeDist);
nrm = normalize(gv + vec2(0.001, 0.001));
stoneShade = clamp(dot(nrm, normalize(vec2(0.65, 0.75))) * 0.5 + 0.5, 0.0, 1.0);
cellTone = 0.5 + 0.5 * sin(id.x * 12.9898 + id.y * 78.233);

cDark = vec3(0.110, 0.118, 0.133);
cMid = vec3(0.245, 0.258, 0.282);
cLight = vec3(0.470, 0.488, 0.515);
cEmber = vec3(1.000, 0.420, 0.208);

rock = mix(cDark, mix(cMid, cLight, stoneShade * bevel), 0.28 + 0.72 * cellTone);
rock = rock * smoothstep(0.0, seamWidth * 1.6, edgeDist);
col = rock + cEmber * seam * emberGlow * (0.65 + 0.35 * sin(t * 1.2 + cellTone * 6.28));
col = clamp(col, 0.0, 1.0);
""",
        "params": {
            "scale": 5.5,
            "seamWidth": 0.055,
            "emberGlow": 0.78,
            "drift": 0.15,
            "bevel": 0.68
        },
        "glsl_frag": """precision highp float;
uniform vec2 res;
uniform float t;
uniform float aspect;
uniform float audio_bass;
uniform float audio_mid;
uniform float audio_high;

void main() {
  vec2 uv = gl_FragCoord.xy / res;
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * (5.5 - audio_bass * 0.6);
  vec2 ip = floor(p);
  vec2 fp = fract(p);

  float f1 = 8.0;
  float f2 = 8.0;
  vec2 cellId = vec2(0.0);
  vec2 bestVec = vec2(0.0);

  for (int j = -1; j <= 1; j++) {
    for (int i = -1; i <= 1; i++) {
      vec2 nb = vec2(float(i), float(j));
      vec2 cid = ip + nb;
      float h1 = fract(sin(dot(cid, vec2(127.1, 311.7))) * 43758.5453);
      float h2 = fract(sin(dot(cid, vec2(269.5, 183.3))) * 43758.5453);
      vec2 pt = nb + 0.5 + 0.38 * sin(t * 0.15 + 6.2831 * vec2(h1, h2)) - fp;
      float d = dot(pt, pt);
      if (d < f1) {
        f2 = f1;
        f1 = d;
        cellId = cid;
        bestVec = pt;
      } else if (d < f2) {
        f2 = d;
      }
    }
  }

  float edgeDist = sqrt(f2) - sqrt(f1);
  float sw = 0.055 + audio_high * 0.035;
  float seam = 1.0 - smoothstep(0.0, sw, edgeDist);
  vec2 ldir = normalize(vec2(cos(audio_mid * 1.5 + 0.7), sin(audio_mid * 1.5 + 0.7)));
  float stoneShade = clamp(dot(normalize(bestVec + vec2(0.001)), ldir) * 0.5 + 0.5, 0.0, 1.0);
  float cellTone = fract(sin(dot(cellId, vec2(41.3, 289.1))) * 43758.5453);

  vec3 cDark = vec3(0.110, 0.118, 0.133);
  vec3 cMid = vec3(0.227, 0.239, 0.259);
  vec3 cLight = vec3(0.412, 0.427, 0.451);
  vec3 cEmber = vec3(1.000, 0.420, 0.208);

  vec3 rock = mix(cDark, mix(cMid, cLight, stoneShade * 0.68), 0.35 + 0.65 * cellTone);
  rock *= smoothstep(0.0, sw * 2.2, edgeDist);
  vec3 col = rock + cEmber * seam * (0.78 + audio_high * 0.75);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
"""
    }
]


def make_embed_html(it):
    title = it["title"]
    desc = it["embed_desc"]
    frag = it["glsl_frag"]
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
    <canvas id="gl" role="img" aria-label="{title}, an audio-reactive visual"></canvas>
  </main>
  <script>
    const canvas = document.getElementById("gl");
    const gl = canvas.getContext("webgl") || canvas.getContext("experimental-webgl");
    if (!gl) {{
      document.body.innerHTML = '<p class="fallback">This visual needs WebGL, which this browser does not offer.</p>';
      throw new Error("no webgl");
    }}
    const vsSource = "attribute vec2 position; void main() {{ gl_Position = vec4(position, 0.0, 1.0); }}";
    const fsSource = `{frag}`;
    function createShader(gl, type, source) {{
      const s = gl.createShader(type);
      gl.shaderSource(s, source);
      gl.compileShader(s);
      return s;
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


def write_notes_block(notes):
    lines = [f"  i note{i} {n}" for i, n in enumerate(notes)]
    lines += [f"  b en{i} 1" for i in range(len(notes))]
    return "\n".join(lines)


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
  i wave1 1
  f cutoff 2400.0
  f resonance 0.25
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
  c fillColor 0.24 0.30 0.36
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
                "tags": ["synth", "vibraphone", "polivoks", "audio", "dsp"],
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
                "tags": ["effect", "dub", "tape", "bode", "shifter", "audio", "dsp"],
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
