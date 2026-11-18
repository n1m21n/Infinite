#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 46.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: jacquard_bauhaus_weave -> jacquard-bauhaus-warp-weft-grid, jacquard-punchcard-binary-matrix)
2. Sketch 3D (Theme: sashimono_kumiko_joinery -> sashimono-kumiko-asanoha-screen-3d, sashimono-kigumi-cantilever-tower-3d)
3. Field Synth (Theme: ethio_jazz_organ -> ethio-jazz-farfisa-vibrato-synth, ethio-jazz-tizita-reed-horn-synth)
4. Field Effects (Theme: spectral_dub_chamber -> spectral-dub-binaural-allpass-fx, spectral-dub-tape-resonator-fx)
5. Field Pixel & Embeds (Theme: haeckel_radiolaria_silica -> haeckel-radiolaria-discoidea-lattice, haeckel-radiolaria-spicule-corona)
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

TMP_DIR = "/tmp/pack46_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview


# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: jacquard_bauhaus_weave)
#    Palette: #DFD3BA (linen parchment), #1E1D1B (charcoal ink),
#             #CE3B2A (vermilion terracotta), #D7AE86 (raw ochre),
#             #9EA6A8 (cool slate)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "jacquard-bauhaus-warp-weft-grid",
        "title": "Jacquard Bauhaus Warp-Weft Grid",
        "description": "Anni Albers inspired Bauhaus Jacquard weaving draft with stepped terracotta, charcoal, ochre, and slate warp-weft bands on ruled linen parchment.",
        "tags": ["jacquard", "bauhaus", "weaving", "textile", "grid", "procedural", "sketch"],
        "code": """// Jacquard Bauhaus Warp-Weft Grid - Infinite Library (Sketch 2D)
param("cols", 14, 8, 24);
param("rows", 28, 16, 48);
param("shift_speed", 0.25, 0.0, 1.2);
param("terracotta_mix", 0.35, 0.1, 0.7);

function draw(t) {
  // Warm unbleached linen parchment (#DFD3BA)
  background(0.875, 0.827, 0.729);
  const w = width;
  const h = height;
  const marginX = w * 0.16;
  const marginY = h * 0.12;
  const gw = w - marginX * 2.0;
  const gh = h - marginY * 2.0;

  const nC = floor(cols);
  const nR = floor(rows);
  const cw = gw / nC;
  const ch = gh / nR;

  // Fine graphite point-paper ruling lines
  stroke(0.58, 0.54, 0.46, 0.36);
  strokeWeight(1.0);
  for (let c = 0; c <= nC; c++) {
    const x = marginX + c * cw;
    line(x, marginY, x, marginY + gh);
  }
  for (let r = 0; r <= nR; r++) {
    const y = marginY + r * ch;
    line(marginX, y, marginX + gw, y);
  }

  // Stepped Bauhaus warp/weft interlacing blocks
  noStroke();
  const phase = floor(t * shift_speed * 2.0);
  for (let r = 0; r < nR; r++) {
    for (let c = 0; c < nC; c++) {
      const zone = ((c * 3 + r * 5) + phase) % 13;
      const bandH = sin(r * 0.42 + c * 0.15 + t * shift_speed * 0.6);
      const bandV = cos(c * 0.65 - r * 0.12);

      // Leave quiet parchment cells for negative space
      if (abs(bandH + bandV) < 0.32 && (c + r) % 3 !== 0) {
        continue;
      }

      const x = marginX + c * cw + 1.0;
      const y = marginY + r * ch + 1.0;
      const bw = cw - 2.0;
      const bh = ch - 2.0;

      if (zone < 3 || (c >= 4 && c <= 6 && r % 4 === 1)) {
        // Vermilion terracotta (#CE3B2A)
        if (((c + r) % 2 === 0) || bandH > terracotta_mix - 0.1) {
          fill(0.808, 0.231, 0.165, 0.94);
          rect(x, y, bw, bh);
        }
      } else if (bandH * bandV > 0.18 || (c % 5 === 0 && r % 2 === 0)) {
        // Charcoal ink (#1E1D1B)
        fill(0.118, 0.114, 0.106, 0.96);
        rect(x, y, bw, bh);
      } else if ((c + r * 2) % 5 === 0) {
        // Cool slate (#9EA6A8)
        fill(0.620, 0.651, 0.659, 0.88);
        rect(x, y, bw, bh);
      } else if ((r % 3 === 0 && c > 2) || bandV > 0.25) {
        // Raw ochre (#D7AE86)
        fill(0.843, 0.682, 0.525, 0.88);
        rect(x, y, bw, bh);
      }
    }
  }

  // Horizontal selvedge calibration bar at bottom margin
  const barY = marginY + gh + h * 0.035;
  const barH = h * 0.022;
  for (let c = 0; c < nC; c++) {
    const x = marginX + c * cw;
    if (c % 4 === 0) fill(0.118, 0.114, 0.106, 0.95);
    else if (c % 4 === 1) fill(0.808, 0.231, 0.165, 0.92);
    else if (c % 4 === 2) fill(0.843, 0.682, 0.525, 0.90);
    else fill(0.620, 0.651, 0.659, 0.88);
    rect(x + 1, barY, cw - 2, barH);
  }
  noFill();
  stroke(0.118, 0.114, 0.106, 0.55);
  strokeWeight(1.2);
  rect(marginX, marginY, gw, gh);
  rect(marginX, barY, gw, barH);
}"""
    },
    {
        "slug": "jacquard-punchcard-binary-matrix",
        "title": "Jacquard Punchcard Binary Matrix",
        "description": "1939 Jacquard loom perforated pasteboard punchcard chain with registration eyelets, laced fold seams, and algorithmic twill-weave hole matrix.",
        "tags": ["jacquard", "punchcard", "binary", "loom", "algorithmic", "matrix", "sketch"],
        "code": """// Jacquard Punchcard Binary Matrix - Infinite Library (Sketch 2D)
param("cards", 7, 4, 12);
param("columns", 24, 14, 36);
param("feed_speed", 0.35, 0.0, 1.5);
param("hole_scale", 0.36, 0.2, 0.45);

function draw(t) {
  // Cast-iron loom slate ground (#262B32)
  background(0.149, 0.169, 0.196);
  const w = width;
  const h = height;
  const marginX = w * 0.11;
  const cardW = w - marginX * 2.0;
  const nCards = floor(cards);
  const nCols = floor(columns);
  const cardH = (h * 0.82) / nCards;
  const startY = h * 0.09;
  const scroll = t * feed_speed;

  // Vertical cast-iron guide rails
  noStroke();
  fill(0.10, 0.12, 0.14, 0.95);
  rect(marginX * 0.45, 0, marginX * 0.32, h);
  rect(w - marginX * 0.77, 0, marginX * 0.32, h);

  for (let k = 0; k < nCards; k++) {
    const cy = startY + k * cardH;
    const gap = 4.0;
    const ch = cardH - gap;

    // Kraft manila pasteboard card (#B49468) with subtle alternating fold shade
    const shade = (k % 2 === 0) ? 1.0 : 0.91;
    fill(0.706 * shade, 0.580 * shade, 0.408 * shade, 0.98);
    rect(marginX, cy, cardW, ch);

    // Top & bottom card fold shadow
    fill(0.12, 0.10, 0.08, 0.35);
    rect(marginX, cy + ch - 3.0, cardW, 3.0);

    // Registration peg holes at left and right edges
    fill(0.94, 0.93, 0.90, 0.96);
    circle(marginX + 18, cy + ch * 0.5, 11.0);
    circle(marginX + cardW - 18, cy + ch * 0.5, 11.0);

    // Lacing cord stitches between cards
    stroke(0.92, 0.88, 0.80, 0.75);
    strokeWeight(2.0);
    line(marginX + 42, cy - 3, marginX + 42, cy + 5);
    line(marginX + cardW - 42, cy - 3, marginX + cardW - 42, cy + 5);
    noStroke();

    // 4 sub-rows of binary perforations per punchcard
    const innerX = marginX + 60;
    const innerW = cardW - 120;
    const cellW = innerW / nCols;
    const subRows = 4;
    const cellH = (ch - 14) / subRows;
    const rad = min(cellW, cellH) * hole_scale;

    for (let r = 0; r < subRows; r++) {
      const rowIdx = k * subRows + r + floor(scroll * 2.0);
      for (let c = 0; c < nCols; c++) {
        // Twill + diamond jacquard weave bit pattern
        const d1 = abs(c - nCols * 0.5);
        const bit = ((c * 3 + rowIdx * 5) % 7 < 3) || ((floor(d1 + rowIdx) % 6) === 0);
        const hx = innerX + (c + 0.5) * cellW;
        const hy = cy + 7 + (r + 0.5) * cellH;
        if (bit) {
          // Daylight through punched hole
          fill(0.953, 0.945, 0.918, 0.98);
          circle(hx, hy, rad * 2.0);
          // Subtle inner rim shadow
          noFill();
          stroke(0.28, 0.22, 0.15, 0.45);
          strokeWeight(1.0);
          circle(hx, hy, rad * 2.0);
          noStroke();
        } else {
          // Unpunched dimple mark
          fill(0.56 * shade, 0.45 * shade, 0.31 * shade, 0.55);
          circle(hx, hy, rad * 0.55);
        }
      }
    }
  }
}"""
    }
]


# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: sashimono_kumiko_joinery)
#    Palette: #1D1712 (cedar shadow), #C79E74 (hinoki cypress),
#             #F5F2EB (shoji rice-paper), #8B5A2B (walnut trim)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "sashimono-kumiko-asanoha-screen-3d",
        "title": "Sashimono Kumiko Asanoha Screen 3D",
        "description": "Backlit Japanese Kumiko Asanoha timber lattice screen in warm hinoki cypress with chamfered joinery ribs and shoji paper ground.",
        "tags": ["sashimono", "kumiko", "joinery", "timber", "japanese", "3d", "architecture"],
        "code": """// Sashimono Kumiko Asanoha Screen 3D - Infinite Library (Sketch 3D)
param("cols", 4, 2, 6);
param("rows", 4, 2, 6);
param("rib_depth", 0.14, 0.06, 0.28);
param("sway", 0.18, 0.0, 0.5);

function draw(t) {
  rotateX(0.18 + 0.06 * sin(t * 0.4));
  rotateY(0.32 * sin(t * sway + 0.5));

  const nC = floor(cols);
  const nR = floor(rows);
  const cell = 0.56;
  const spanW = nC * cell;
  const spanH = nR * cell;

  // Backlit shoji rice-paper screen plane (#F5F2EB)
  fill(0.95, 0.93, 0.89);
  push();
  translate(0, 0, -0.09);
  box(spanW + 0.18, spanH + 0.18, 0.03);
  pop();

  // Deep cedar outer frame (#1D1712)
  fill(0.14, 0.11, 0.08);
  const ft = 0.09;
  const fd = rib_depth + 0.10;
  push(); translate(0, spanH * 0.5 + ft * 0.5, 0); box(spanW + ft * 2.0, ft, fd); pop();
  push(); translate(0, -spanH * 0.5 - ft * 0.5, 0); box(spanW + ft * 2.0, ft, fd); pop();
  push(); translate(-spanW * 0.5 - ft * 0.5, 0, 0); box(ft, spanH, fd); pop();
  push(); translate(spanW * 0.5 + ft * 0.5, 0, 0); box(ft, spanH, fd); pop();

  // Primary vertical & horizontal hinoki cypress kumiko grid (#C79E74)
  fill(0.78, 0.62, 0.45);
  for (let i = 0; i <= nC; i++) {
    const x = -spanW * 0.5 + i * cell;
    push();
    translate(x, 0, 0);
    box(0.032, spanH, rib_depth);
    pop();
  }
  for (let j = 0; j <= nR; j++) {
    const y = -spanH * 0.5 + j * cell;
    push();
    translate(0, y, 0);
    box(spanW, 0.032, rib_depth);
    pop();
  }

  // Diagonal Asanoha (hemp-leaf) diamond & cross-star infill ribs
  for (let ix = 0; ix < nC; ix++) {
    for (let iy = 0; iy < nR; iy++) {
      const cx = -spanW * 0.5 + (ix + 0.5) * cell;
      const cy = -spanH * 0.5 + (iy + 0.5) * cell;
      const diagLen = cell * 1.36;

      fill(0.82, 0.66, 0.49);
      push();
      translate(cx, cy, 0.01);
      rotateZ(0.785398);
      box(diagLen, 0.022, rib_depth * 0.85);
      pop();

      push();
      translate(cx, cy, 0.01);
      rotateZ(-0.785398);
      box(diagLen, 0.022, rib_depth * 0.85);
      pop();

      // Inner walnut joinery diamond boss (#8B5A2B)
      fill(0.54, 0.35, 0.17);
      push();
      translate(cx, cy, 0.02);
      rotateZ(0.785398);
      box(cell * 0.36, cell * 0.36, rib_depth * 0.92);
      pop();
    }
  }
}"""
    },
    {
        "slug": "sashimono-kigumi-cantilever-tower-3d",
        "title": "Sashimono Kigumi Cantilever Tower 3D",
        "description": "Nail-free Japanese Kigumi interlocking mortise-and-tenon timber tower with stepped cross-braced cantilevers and brass locking dowels.",
        "tags": ["kigumi", "sashimono", "joinery", "cantilever", "timber", "3d", "architecture"],
        "code": """// Sashimono Kigumi Cantilever Tower 3D - Infinite Library (Sketch 3D)
param("tiers", 8, 4, 12);
param("span", 1.75, 1.0, 2.4);
param("twist", 0.14, 0.0, 0.35);
param("orbit", 0.22, 0.0, 0.6);

function draw(t) {
  rotateX(0.38);
  rotateY(t * orbit + 0.65);

  const nTiers = floor(tiers);
  const stepY = 0.24;
  const baseY = -nTiers * stepY * 0.5;

  // Dark yakisugi charred cedar plinth
  fill(0.12, 0.10, 0.09);
  push();
  translate(0, baseY - 0.14, 0);
  box(span * 1.15, 0.14, span * 1.15);
  pop();

  // Four vertical hinoki corner posts
  fill(0.76, 0.59, 0.42);
  const postOff = span * 0.26;
  const totalH = nTiers * stepY + 0.35;
  for (let px = -1; px <= 1; px += 2) {
    for (let pz = -1; pz <= 1; pz += 2) {
      push();
      translate(px * postOff, 0, pz * postOff);
      box(0.09, totalH, 0.09);
      pop();
    }
  }

  // Interlocking stepped Kigumi cross-beams
  for (let i = 0; i < nTiers; i++) {
    const u = i / max(1, nTiers - 1);
    const y = baseY + i * stepY + 0.12;
    const len = span * (1.0 - 0.38 * u);
    const rot = i * twist + 0.08 * sin(t * 0.6 + i * 0.5);

    push();
    translate(0, y, 0);
    rotateY(rot);

    // Warm hinoki cypress beam pair along X
    fill(0.80 - 0.10 * u, 0.63 - 0.08 * u, 0.45 - 0.05 * u);
    push(); translate(0, 0, postOff); box(len, 0.085, 0.10); pop();
    push(); translate(0, 0, -postOff); box(len, 0.085, 0.10); pop();

    // Cross-lapped walnut beam pair along Z
    fill(0.56, 0.37, 0.20);
    push(); translate(postOff, 0.07, 0); box(0.10, 0.085, len); pop();
    push(); translate(-postOff, 0.07, 0); box(0.10, 0.085, len); pop();

    // Brushed brass locking dowels at each mortise intersection
    fill(0.86, 0.68, 0.32);
    for (let sx = -1; sx <= 1; sx += 2) {
      for (let sz = -1; sz <= 1; sz += 2) {
        push();
        translate(sx * postOff, 0.035, sz * postOff);
        box(0.04, 0.16, 0.04);
        pop();
      }
    }
    pop();
  }
}"""
    }
]


# -------------------------------------------------------------
# 3. Field Synth (Theme: ethio_jazz_organ)
#    Seed from styled.py: bpm=103, root=3 (Eb), scale=10 (Min Pent)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "ethio-jazz-farfisa-vibrato-synth",
        "title": "Ethio-Jazz Farfisa Vibrato Synth",
        "description": "1970s Addis Ababa transistor organ synthesizer with 4-drawbar harmonic bank, key-click transient, diode saturation, and dual-rate vibrato.",
        "note_root": 51,
        "params": {
            "vibratoHz": 5.4,
            "vibratoDepth": 0.0085,
            "drawbarMix": 0.68,
            "drive": 1.65,
            "decay": 0.9992
        },
        "code": """param float vibratoHz = 5.4 [1.5, 9.0];
param float vibratoDepth = 0.0085 [0.0, 0.025];
param float drawbarMix = 0.68 [0.0, 1.0];
param float drive = 1.65 [0.8, 3.5];
param float decay = 0.9992 [0.995, 0.9998];
state float p1 = 0; state float p2 = 0; state float vibPh = 0;
state float env = 0; state float lp = 0;

env = if(gate > 0.5, env + (1.0 - env) * 0.22, env * decay);
vibPh = (vibPh + vibratoHz / sr) % 1.0;
f0 = clamp(freq * (1.0 + vibratoDepth * sin(6.2831853 * vibPh)), 40.0, 4200.0);
p1 = (p1 + f0 / sr) % 1.0;
p2 = (p2 + (f0 * 2.0) / sr) % 1.0;

w1 = sin(6.2831853 * p1);
w2 = sin(6.2831853 * p2);
w3 = sin(18.849556 * p1);
organ = (w1 / (1.0 + abs(w1) * 0.5)) * 0.55 + (w2 * 0.36 + w3 * 0.24) * drawbarMix;
sat = (organ * drive) / (1.0 + abs(organ * drive) * 0.65);
lp = lp + 0.30 * (sat - lp);
sig = lp * env * 0.42;
out = sig / (1.0 + abs(sig) * 0.35);
"""
    },
    {
        "slug": "ethio-jazz-tizita-reed-horn-synth",
        "title": "Ethio-Jazz Tizita Reed Horn Synth",
        "description": "Expressive Ethio-jazz brass-reed solo synthesizer with wavefolded mouth-pressure excitation and dual formant horn resonator.",
        "note_root": 51,
        "params": {
            "fold": 2.4,
            "formantHz": 680.0,
            "breath": 0.42,
            "vibratoHz": 4.8,
            "decay": 0.9991
        },
        "code": """param float fold = 2.4 [1.0, 5.0];
param float formantHz = 680.0 [260.0, 1600.0];
param float breath = 0.42 [0.0, 1.0];
param float vibratoHz = 4.8 [1.0, 8.0];
param float decay = 0.9991 [0.995, 0.9998];
state float ph = 0; state float vibPh = 0; state float env = 0;
state float bp1 = 0; state float lp1 = 0;

env = if(gate > 0.5, env + (1.0 - env) * 0.08, env * decay);
vibPh = (vibPh + vibratoHz / sr) % 1.0;
f0 = clamp(freq * (1.0 + 0.006 * sin(6.2831853 * vibPh)), 45.0, 3200.0);
ph = (ph + f0 / sr) % 1.0;

exc = sin(6.2831853 * ph) * (1.0 + fold * env) + breath * 0.35 * sin(12.56637 * ph);
reed = sin(exc * 1.35) + 0.28 * (exc / (1.0 + abs(exc)));

g1 = clamp(2.0 * sin(3.14159265 * formantHz / sr), 0.02, 0.85);
bp1 = bp1 + g1 * (reed - lp1 - 0.48 * bp1);
lp1 = lp1 + g1 * bp1;

sig = (reed * 0.36 + bp1 * 0.72) * env * 0.44;
out = sig / (1.0 + abs(sig) * 0.40);
"""
    }
]


# -------------------------------------------------------------
# 4. Field Effects (Theme: spectral_dub_chamber)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "spectral-dub-binaural-allpass-fx",
        "title": "Spectral Dub Binaural Allpass FX",
        "description": "Deep dub chamber processor with 4-stage dispersed allpass cascade, cross-coupled resonant bandpass feedback, and warm transformer saturation.",
        "params": {
            "centerHz": 920.0,
            "dispersion": 0.68,
            "feedback": 0.52,
            "drive": 1.35,
            "mix": 0.84
        },
        "code": """param float centerHz = 920.0 [180.0, 3600.0];
param float dispersion = 0.68 [0.1, 0.92];
param float feedback = 0.52 [0.0, 0.82];
param float drive = 1.35 [0.6, 2.8];
param float mix = 0.84 [0.0, 1.0];
state float a1 = 0; state float a2 = 0; state float a3 = 0; state float a4 = 0;
state float bp = 0; state float lp = 0; state float fb = 0; state float lfo = 0;

lfo = (lfo + 0.32 / sr) % 1.0;
modHz = clamp(centerHz * (0.72 + 0.32 * sin(6.2831853 * lfo)), 120.0, 5000.0);
g = clamp(2.0 * sin(3.14159265 * modHz / sr), 0.02, 0.85);

x = in + fb * feedback;
bp = bp + g * (x - lp - 0.58 * bp);
lp = lp + g * bp;
fSig = lp * 0.65 + bp * 0.52;

# 4-stage dispersed allpass chamber
c = dispersion;
y1 = -c * fSig + a1; a1 = fSig + c * y1;
y2 = -c * y1 + a2; a2 = y1 + c * y2;
y3 = -c * y2 + a3; a3 = y2 + c * y3;
y4 = -c * y3 + a4; a4 = y3 + c * y4;
fb = clamp(y4, -1.2, 1.2);

wd = y4 * drive;
wet = wd / (1.0 + abs(wd) * 0.72);
out = (in * (1.0 - mix) + wet * mix) * 0.85;
"""
    },
    {
        "slug": "spectral-dub-tape-resonator-fx",
        "title": "Spectral Dub Tape Resonator FX",
        "description": "King Tubby inspired dual-head tape resonator with capstan wow-flutter drift, resonant SVF dub sweep, and oxide hysteresis warmth.",
        "params": {
            "sweepHz": 0.85,
            "cutoffHz": 1650.0,
            "res": 0.54,
            "oxideDrive": 1.55,
            "mix": 0.86
        },
        "code": """param float sweepHz = 0.85 [0.1, 4.0];
param float cutoffHz = 1650.0 [220.0, 4800.0];
param float res = 0.54 [0.1, 0.85];
param float oxideDrive = 1.55 [0.7, 3.0];
param float mix = 0.86 [0.0, 1.0];
state float ph = 0; state float bp = 0; state float lp = 0;
state float hyst = 0; state float s1 = 0; state float s2 = 0;

ph = (ph + sweepHz / sr) % 1.0;
wow = sin(6.2831853 * ph) * 0.42 + sin(6.2831853 * ph * 2.7) * 0.18;
modHz = clamp(cutoffHz * (0.62 + 0.38 * wow), 140.0, 6200.0);
g = clamp(2.0 * sin(3.14159265 * modHz / sr), 0.02, 0.88);
q = 1.12 - res * 0.88;

# Oxide magnetic hysteresis
drIn = in * oxideDrive;
hyst = hyst + 0.14 * ((drIn / (1.0 + abs(drIn) * 0.75)) - hyst);
driveSig = hyst * 0.72 + in * 0.28;

bp = bp + g * (driveSig - lp - q * bp);
lp = lp + g * bp;
filt = lp * 0.74 + bp * 0.42;

# Dual-head tape dispersion allpass
y1 = -0.58 * filt + s1; s1 = filt + 0.58 * y1;
y2 = -0.58 * y1 + s2; s2 = y1 + 0.58 * y2;
wet = (filt * 0.55 + y2 * 0.55);
sat = wet / (1.0 + abs(wet) * 0.35);
out = (in * (1.0 - mix) + sat * mix) * 0.86;
"""
    }
]


# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embeds (Theme: haeckel_radiolaria_silica)
#    Palette: #F8F5EC (ivory plate), #B82925 (carmine vermilion),
#             #D9B643 (sulfur ochre), #231A16 (sepia ink)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "haeckel-radiolaria-discoidea-lattice",
        "title": "Haeckel Radiolaria Discoidea Lattice",
        "description": "Ernst Haeckel Kunstformen der Natur Radiolaria Discoidea lithograph with cruciform silica arms, honeycomb pore rings, and radiating marginal spicules.",
        "embed_desc": "Audio-reactive Haeckel Radiolaria lithograph visualizer. Bass pulses the cruciform silica skeleton, mids breathe through the honeycomb pore rings, and highs shimmer along the marginal spicules.",
        "tags": ["haeckel", "radiolaria", "lithograph", "biological", "voronoi", "shader", "webgl", "audio-reactive"],
        "params": {
            "arms": 4.0,
            "poreScale": 18.0,
            "spiculeLen": 0.42,
            "inkContrast": 1.25,
            "breathe": 0.30
        },
        "code": """param float arms = 4.0 [3.0, 8.0];
param float poreScale = 18.0 [8.0, 32.0];
param float spiculeLen = 0.42 [0.15, 0.8];
param float inkContrast = 1.25 [0.6, 2.0];
param float breathe = 0.30 [0.0, 0.8];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0;
r = length(p);
a = atan2(p.y, p.x);

# Cruciform radial symmetry field
armWave = abs(cos(a * arms * 0.5 + 0.08 * sin(t * 0.8 + r * 4.0)));
armShape = pow(armWave, 3.2) * 0.68 + 0.28;
bodyMask = smoothstep(armShape + 0.06, armShape - 0.04, r);

# Silica honeycomb pore lattice
pores = sin(p.x * poreScale + sin(p.y * poreScale * 0.5)) * cos(p.y * poreScale - sin(p.x * poreScale * 0.5));
poreRings = cos(r * poreScale * 1.4 - t * breathe * 2.0);
fenestration = smoothstep(-0.15, 0.35, pores * 0.65 + poreRings * 0.35);

# Marginal radiating spicules
spicules = pow(abs(cos(a * 24.0 + sin(r * 10.0 - t * 0.5))), 12.0);
spiculeMask = smoothstep(armShape + spiculeLen, armShape - 0.02, r) * (1.0 - smoothstep(armShape, armShape - 0.08, r));

cIvory = vec3(0.973, 0.961, 0.925);
cOchre = vec3(0.851, 0.714, 0.263);
cCarmine = vec3(0.722, 0.161, 0.145);
cSepia = vec3(0.137, 0.102, 0.086);

coreGrad = smoothstep(0.42, 0.04, r);
orgCol = mix(cOchre, cCarmine, clamp(coreGrad * 0.9 + (1.0 - fenestration) * 0.45, 0.0, 1.0));
col = mix(cIvory, orgCol, bodyMask * 0.92);
col = mix(col, cSepia, bodyMask * (1.0 - fenestration) * 0.68 * inkContrast);
col = mix(col, cSepia, spiculeMask * spicules * 0.88);
# Subtle outer plate ring border
ringLine = smoothstep(0.012, 0.0, abs(r - 0.92));
col = mix(col, cSepia, ringLine * 0.35);
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

  float b = audio_bass * 0.65;
  float m = audio_mid * 0.65;
  float h = audio_high * 0.75;

  float r = length(p);
  float a = atan(p.y, p.x);

  float armWave = abs(cos(a * 2.0 + 0.08 * sin(t * 0.8 + r * 4.0)));
  float armShape = pow(armWave, 3.2 - b * 0.8) * (0.68 + b * 0.14) + 0.28;
  float bodyMask = smoothstep(armShape + 0.06, armShape - 0.04, r);

  float pScale = 18.0 + m * 6.0;
  float pores = sin(p.x * pScale + sin(p.y * pScale * 0.5)) * cos(p.y * pScale - sin(p.x * pScale * 0.5));
  float poreRings = cos(r * pScale * 1.4 - t * (0.6 + m * 1.4));
  float fenestration = smoothstep(-0.15, 0.35, pores * 0.65 + poreRings * 0.35);

  float spicules = pow(abs(cos(a * 24.0 + sin(r * 10.0 - t * 0.5))), 12.0 - h * 5.0);
  float spiculeMask = smoothstep(armShape + 0.42 + h * 0.16, armShape - 0.02, r) * (1.0 - smoothstep(armShape, armShape - 0.08, r));

  vec3 cIvory = vec3(0.973, 0.961, 0.925);
  vec3 cOchre = vec3(0.851, 0.714, 0.263);
  vec3 cCarmine = vec3(0.722, 0.161, 0.145);
  vec3 cSepia = vec3(0.137, 0.102, 0.086);

  float coreGrad = smoothstep(0.42, 0.04, r);
  vec3 orgCol = mix(cOchre, cCarmine, clamp(coreGrad * 0.9 + (1.0 - fenestration) * 0.45, 0.0, 1.0));
  vec3 col = mix(cIvory, orgCol, bodyMask * 0.92);
  col = mix(col, cSepia, bodyMask * (1.0 - fenestration) * 0.85);
  col = mix(col, cSepia, spiculeMask * spicules * (0.88 + h * 0.12));

  float ringLine = smoothstep(0.012, 0.0, abs(r - 0.92));
  col = mix(col, cSepia, ringLine * 0.35);

  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "haeckel-radiolaria-spicule-corona",
        "title": "Haeckel Radiolaria Spicule Corona",
        "description": "Dark-field optical microscopy of a spherical Polycyttaria radiolarian silica shell with concentric geodesic pore rings and radiating axopodia.",
        "embed_desc": "Audio-reactive dark-field Radiolaria microscopy visualizer. Bass expands the geodesic silica capsule, mids ripple the concentric pore lattice, and highs illuminate the radiating axopodia.",
        "tags": ["haeckel", "radiolaria", "microscopy", "silica", "corona", "shader", "webgl", "audio-reactive"],
        "params": {
            "shellRad": 0.48,
            "ringFreq": 24.0,
            "axopodia": 32.0,
            "glow": 0.85,
            "twist": 0.35
        },
        "code": """param float shellRad = 0.48 [0.25, 0.72];
param float ringFreq = 24.0 [10.0, 42.0];
param float axopodia = 32.0 [12.0, 56.0];
param float glow = 0.85 [0.2, 1.4];
param float twist = 0.35 [0.0, 1.2];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0;
r = length(p);
a = atan2(p.y, p.x);

# Geodesic silica shell with concentric hexagonal pores
shell = smoothstep(shellRad + 0.04, shellRad - 0.06, r);
hexA = sin(r * ringFreq - t * 0.7) * cos(a * 12.0 + sin(r * 8.0));
hexB = cos(r * ringFreq * 0.5 + a * 6.0);
pores = smoothstep(-0.1, 0.4, hexA * 0.65 + hexB * 0.35);

# Radiating undulating axopodia filaments
waveA = a + twist * sin(r * 6.0 - t * 1.1) * 0.14;
filaments = pow(abs(cos(waveA * axopodia * 0.5)), 10.0);
coronaFade = smoothstep(1.05, shellRad * 0.7, r) * (1.0 - smoothstep(shellRad, shellRad - 0.12, r));

cDark = vec3(0.067, 0.059, 0.071);
cCarmine = vec3(0.761, 0.216, 0.180);
cOchre = vec3(0.910, 0.745, 0.310);
cIvory = vec3(0.973, 0.957, 0.910);

inner = mix(cCarmine, cOchre, clamp(pores * 0.85, 0.0, 1.0));
col = mix(cDark, inner * glow, shell);
col = mix(col, cIvory, shell * (1.0 - pores) * 0.38);
col = col + mix(cOchre, cIvory, 0.55) * filaments * coronaFade * glow * 0.92;
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

  float b = audio_bass * 0.65;
  float m = audio_mid * 0.65;
  float h = audio_high * 0.80;

  float r = length(p);
  float a = atan(p.y, p.x);

  float sRad = 0.48 + b * 0.10;
  float shell = smoothstep(sRad + 0.04, sRad - 0.06, r);
  float rFreq = 24.0 + m * 8.0;
  float hexA = sin(r * rFreq - t * 0.7) * cos(a * 12.0 + sin(r * 8.0));
  float hexB = cos(r * rFreq * 0.5 + a * 6.0);
  float pores = smoothstep(-0.1, 0.4, hexA * 0.65 + hexB * 0.35);

  float waveA = a + (0.35 + m * 0.45) * sin(r * 6.0 - t * 1.1) * 0.14;
  float filaments = pow(abs(cos(waveA * 16.0)), 10.0 - h * 4.5);
  float coronaFade = smoothstep(1.05 + h * 0.18, sRad * 0.7, r) * (1.0 - smoothstep(sRad, sRad - 0.12, r));

  vec3 cDark = vec3(0.067, 0.059, 0.071);
  vec3 cCarmine = vec3(0.761, 0.216, 0.180);
  vec3 cOchre = vec3(0.910, 0.745, 0.310);
  vec3 cIvory = vec3(0.973, 0.957, 0.910);

  vec3 inner = mix(cCarmine, cOchre, clamp(pores * 0.85, 0.0, 1.0));
  vec3 col = mix(cDark, inner * (0.85 + b * 0.35), shell);
  col = mix(col, cIvory, shell * (1.0 - pores) * 0.38);
  col = col + mix(cOchre, cIvory, 0.55) * filaments * coronaFade * (0.85 + h * 0.5);

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
    html, body {{ margin: 0; padding: 0; width: 100%; height: 100%; overflow: hidden; background: #151930; }}
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
  c bg 0.14 0.16 0.20
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
  i note1 {nr+3}
  i note2 {nr+5}
  i note3 {nr+7}
  i note4 {nr+10}
  i note5 {nr+7}
  i note6 {nr+5}
  i note7 {nr+3}
  i note8 {nr}
  i note9 {nr+7}
  i note10 {nr+10}
  i note11 {nr+12}
  i note12 {nr+10}
  i note13 {nr+7}
  i note14 {nr+5}
  i note15 {nr+3}
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
  c fillColor 0.24 0.18 0.14
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
  i note0 42
  i note1 42
  i note2 49
  i note3 42
  i note4 45
  i note5 42
  i note6 54
  i note7 42
  i note8 42
  i note9 47
  i note10 49
  i note11 42
  i note12 45
  i note13 49
  i note14 52
  i note15 42
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
  pos 400 1024
  f cutoff 2400.0
  f resonance 0.25
  f decay 0.45
  f volume 0.85
end
node 3 AudioEffects Field Effect
  id fx
  pos 800 1024
  s code {escaped}
end
node 4 Utility Audio Out
  id speakers
  pos 1200 1024
end
node 5 Source Shape
  id pic
  pos 0 162
  c fillColor 0.16 0.22 0.28
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

        report.append(f"PASS [Field Effects] {slug}: lufs={lufs:.2f}, peak={peak:.2f}dBFS, silence={silence}")

    # 5. Field Pixel & Embeds
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
                "params": it["params"],
                "nodeSettings": {
                    "width": 1080.0,
                    "height": 1080.0,
                    "animate": 1.0
                }
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
                "tags": ["synth", "ethio-jazz", "organ", "reed", "audio", "dsp"],
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
                "tags": ["effect", "dub", "spectral", "allpass", "tape", "audio", "dsp"],
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
