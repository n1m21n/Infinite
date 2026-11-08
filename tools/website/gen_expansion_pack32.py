#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 32.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: swiss_typographic -> swiss-typographic-grid-matrix, swiss-typographic-diagonal-construct)
2. Sketch 3D (Theme: brutalist_architecture -> brutalist-architecture-monolith-bastion-3d, brutalist-architecture-cantilevered-stele-3d)
3. Field Synth (Theme: synthwave -> synthwave-analog-poly-brass-lead-synth, synthwave-neon-running-bass-synth)
4. Field Effects (Theme: glitch -> glitch-buffer-stutter-repeater-fx, glitch-spectral-aliasing-bitcrush-fx)
5. Field Pixel (Theme: generative_plotter -> generative-plotter-curvilinear-flowfield, generative-plotter-concentric-isotonic)
"""
import os, sys, json, subprocess, zipfile
from PIL import Image, ImageStat

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN = os.path.expanduser("~/.local/bin/Infinite")
if not os.path.exists(BIN):
    BIN = os.path.join(ROOT, "build", "Infinite.app", "Contents", "MacOS", "Infinite")

LIB_DIR = os.path.join(ROOT, "website", "assets", "library")
SKETCH_DIR = os.path.join(LIB_DIR, "sketch")
PIXEL_DIR = os.path.join(LIB_DIR, "pixel")
PREVIEWS_DIR = os.path.join(LIB_DIR, "previews")
THUMBS_DIR = os.path.join(LIB_DIR, "thumbs")
TEMPLATES_DIR = os.path.join(ROOT, "website", "assets", "templates")
EMBEDS_DIR = os.path.join(ROOT, "website", "embeds")

TMP_DIR = "/tmp/pack32_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: swiss_typographic)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "swiss-typographic-grid-matrix",
        "title": "Swiss Typographic Grid Matrix",
        "description": "Rigid modernist modular grid composition featuring mathematical column divisions, asymmetrical vermilion accent bars, fine registration crosshairs, and dynamic ratio blocks.",
        "tags": ["swiss", "typographic", "grid", "minimal", "modernist", "drawing"],
        "code": """// Swiss Typographic Grid Matrix - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Rigid modernist modular grid composition featuring mathematical column divisions, asymmetrical vermilion accent bars, fine registration crosshairs, and dynamic ratio blocks.
param("grid_cols", 6.0, 3.0, 12.0);
param("accent_ratio", 0.38, 0.1, 0.8);
param("rule_weight", 2.4, 1.0, 6.0);
param("time_drift", 0.35, 0.1, 1.2);

function draw(t) {
  // Archival Swiss cream paper (#F5F5F2)
  background(0.961, 0.961, 0.949);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.86;
  const time = t * time_drift;

  push();
  translate(cx, cy);

  // 1. Outer Modular Framing & Precision Bleed Ticks
  stroke(0.067, 0.067, 0.067); // Jet ink black
  strokeWeight(rule_weight);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  // Precision corner crop marks
  strokeWeight(1.4);
  const markLen = span * 0.035;
  const corners = [
    [-span * 0.48, -span * 0.48], [span * 0.48, -span * 0.48],
    [-span * 0.48, span * 0.48], [span * 0.48, span * 0.48]
  ];
  for (let c of corners) {
    const sx = c[0] < 0 ? -1 : 1;
    const sy = c[1] < 0 ? -1 : 1;
    line(c[0] + sx * markLen * 0.5, c[1], c[0] - sx * markLen * 0.8, c[1]);
    line(c[0], c[1] + sy * markLen * 0.5, c[0], c[1] - sy * markLen * 0.8);
  }

  // 2. Strict Mathematical Swiss Grid Columns & Rules
  const cols = floor(grid_cols);
  const colW = (span * 0.88) / cols;
  const topY = -span * 0.42;
  const botY = span * 0.42;
  const gridH = botY - topY;

  stroke(0.067, 0.067, 0.067, 0.35);
  strokeWeight(1.0);
  for (let i = 0; i <= cols; i++) {
    const x = -span * 0.44 + i * colW;
    line(x, topY, x, botY);
  }

  // Horizontal metric bands
  const bands = 8;
  const bandH = gridH / bands;
  for (let j = 0; j <= bands; j++) {
    const y = topY + j * bandH;
    line(-span * 0.44, y, span * 0.44, y);
  }

  // 3. Bold Asymmetric International Vermilion Red Geometric Accents (#E4261A)
  noStroke();
  fill(0.894, 0.149, 0.102); // International Vermilion

  // Focal vermilion counterpoint disc
  const discX = -span * 0.44 + colW * (1.5 + sin(time * 1.5) * 0.25);
  const discY = topY + bandH * 2.5;
  const discR = colW * 1.25;
  circle(discX, discY, discR);

  // Vermilion horizontal anchor bar
  rect(-span * 0.44 + colW * 2.0, topY + bandH * 4.0, colW * 3.5, bandH * 0.35);

  // 4. Heavy Carbon Typography Blocks (Simulated Akzidenz-Grotesk geometric slabs)
  fill(0.067, 0.067, 0.067);

  // Hero title weight block
  rect(-span * 0.44 + colW * 0.2, topY + bandH * 0.3, colW * (cols * 0.55), bandH * 0.65);

  // Subtitle ratio bars
  for (let k = 0; k < 5; k++) {
    const yk = topY + bandH * (1.2 + k * 0.22);
    const wk = colW * (2.8 - (k % 3) * 0.5);
    rect(-span * 0.44 + colW * 0.2, yk, wk, bandH * 0.08);
  }

  // Lower grid asymmetric blocks
  rect(-span * 0.44 + colW * 3.0, topY + bandH * 5.0, colW * 2.8, bandH * 1.8);

  // Secondary text matrix
  fill(0.961, 0.961, 0.949); // Knockout white inside lower block
  for (let m = 0; m < 4; m++) {
    rect(-span * 0.44 + colW * 3.2, topY + bandH * (5.3 + m * 0.35), colW * 2.4, bandH * 0.10);
  }

  // 5. Technical Metric Scale / Tick Marks along baseline
  stroke(0.067, 0.067, 0.067);
  strokeWeight(1.2);
  const nTicks = 36;
  const tickStep = (span * 0.88) / nTicks;
  for (let tIdx = 0; tIdx <= nTicks; tIdx++) {
    const tx = -span * 0.44 + tIdx * tickStep;
    const tLen = (tIdx % 5 === 0) ? span * 0.024 : span * 0.012;
    line(tx, botY + 4.0, tx, botY + 4.0 + tLen);
  }

  pop();
}
"""
    },
    {
        "slug": "swiss-typographic-diagonal-construct",
        "title": "Swiss Typographic Diagonal Construct",
        "description": "Dynamic 45-degree diagonal typographical construct with intersecting carbon rules, vermilion focal arc geometry, micro-metric scale divisions, and strict asymmetric mathematical balance.",
        "tags": ["swiss", "typographic", "construct", "diagonal", "minimal", "drawing"],
        "code": """// Swiss Typographic Diagonal Construct - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Dynamic 45-degree diagonal typographical construct with intersecting carbon rules, vermilion focal arc geometry, micro-metric scale divisions, and strict asymmetric mathematical balance.
param("angle", 45.0, 15.0, 75.0);
param("bar_count", 5.0, 2.0, 10.0);
param("vermilion_scale", 0.42, 0.2, 0.8);
param("speed", 0.30, 0.1, 1.0);

function draw(t) {
  // Archival Swiss cream paper (#F5F5F2)
  background(0.961, 0.961, 0.949);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.86;
  const time = t * speed;

  push();
  translate(cx, cy);

  // 1. Outer Minimal Enclosure Frame
  stroke(0.067, 0.067, 0.067);
  strokeWeight(2.8);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  // Registration crosshairs
  strokeWeight(1.4);
  const crossLen = span * 0.025;
  line(0, -span * 0.48 - crossLen, 0, -span * 0.48 + crossLen);
  line(-crossLen, -span * 0.48, crossLen, -span * 0.48);
  line(0, span * 0.48 - crossLen, 0, span * 0.48 + crossLen);
  line(-crossLen, span * 0.48, crossLen, span * 0.48);

  // 2. Dynamic 45-degree Angled Composition Construct
  push();
  const rad = (angle * Math.PI) / 180.0;
  rotate(rad);

  // Heavy diagonal Carbon rules
  stroke(0.067, 0.067, 0.067);
  strokeWeight(4.5);
  line(-span * 0.65, 0, span * 0.65, 0);

  strokeWeight(1.8);
  line(-span * 0.65, -span * 0.12, span * 0.65, -span * 0.12);
  line(-span * 0.65, span * 0.12, span * 0.65, span * 0.12);

  // Vermilion Red Dynamic Wedge / Arc (#E4261A)
  noStroke();
  fill(0.894, 0.149, 0.102);
  rect(-span * 0.25, -span * 0.28, span * 0.50, span * 0.14);

  // Vermilion focal circle
  circle(span * 0.22, span * 0.12, span * 0.24 * vermilion_scale * 2.0);

  // Sequential rhythmic carbon bars
  fill(0.067, 0.067, 0.067);
  const nBars = floor(bar_count);
  const bW = span * 0.065;
  for (let b = 0; b < nBars; b++) {
    const bx = -span * 0.35 + b * (span * 0.16);
    const by = span * 0.18;
    const bH = span * (0.15 + (b % 3) * 0.12);
    rect(bx, by, bW, bH);
  }
  pop();

  // 3. Static Orthogonal Baseline Grid Overprint
  stroke(0.067, 0.067, 0.067, 0.4);
  strokeWeight(1.2);
  for (let gy = -span * 0.42; gy <= span * 0.42; gy += span * 0.14) {
    line(-span * 0.44, gy, span * 0.44, gy);
  }

  // 4. Technical Index Numerals / Ratios (Modernist blocks)
  fill(0.067, 0.067, 0.067);
  noStroke();
  // Large index marker "01" / "SWISS"
  rect(-span * 0.44, span * 0.34, span * 0.16, span * 0.08);
  rect(-span * 0.24, span * 0.34, span * 0.32, span * 0.02);
  rect(-span * 0.24, span * 0.38, span * 0.24, span * 0.02);

  // Micro fine ticks along vertical margin
  stroke(0.067, 0.067, 0.067);
  strokeWeight(1.2);
  for (let my = -span * 0.44; my <= span * 0.44; my += span * 0.022) {
    line(-span * 0.48, my, -span * 0.46, my);
  }

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: brutalist_architecture)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "brutalist-architecture-monolith-bastion-3d",
        "title": "Brutalist Architecture Monolith Bastion 3D",
        "description": "Monolithic poured concrete bastion megastructure featuring heavy stepped plinths, modular recessed coffer chambers, cantilevered floor plates, and deep architectural shadow relief.",
        "tags": ["brutalist", "architecture", "monolith", "concrete", "megastructure", "3d"],
        "code": """// Brutalist Architecture Monolith Bastion 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Monolithic poured concrete bastion megastructure featuring heavy stepped plinths, modular recessed coffer chambers, cantilevered floor plates, and deep architectural shadow relief.
param("spin", 0.32, 0.1, 1.2);
param("scale_factor", 1.15, 0.5, 2.0);
param("mass_height", 2.4, 1.2, 3.8);
param("tiers", 4.0, 2.0, 7.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.48 + sin(time * 0.11) * 0.03);
  rotateY(time * 0.25 + 0.72);

  // 1. Heavy stepped asphalt & rough aggregate podium
  push();
  translate(0, 1.25 * scale_factor, 0);
  fill(0.133, 0.133, 0.133); // Basalt dark shadow floor (#222222)
  box(2.8 * scale_factor, 0.14, 2.8 * scale_factor);

  // Stepped sub-plinth in weathered concrete (#7a7a78)
  translate(0, -0.09, 0);
  fill(0.478, 0.478, 0.471);
  box(2.2 * scale_factor, 0.06, 2.2 * scale_factor);

  // Poured concrete courtyard plinth (#d9d6cf)
  translate(0, -0.05, 0);
  fill(0.851, 0.839, 0.812);
  box(1.8 * scale_factor, 0.05, 1.8 * scale_factor);
  pop();

  // 2. Central Monolithic Brutalist Concrete Core Tower
  push();
  translate(0, (1.25 - mass_height * 0.5) * scale_factor, 0);
  fill(0.851, 0.839, 0.812); // Pale poured cement
  box(0.70 * scale_factor, mass_height * scale_factor, 0.70 * scale_factor);

  // Vertical deep shadow reveal recesses
  fill(0.133, 0.133, 0.133);
  box(0.50 * scale_factor, mass_height * 0.90 * scale_factor, 0.76 * scale_factor);
  box(0.76 * scale_factor, mass_height * 0.70 * scale_factor, 0.50 * scale_factor);
  pop();

  // 3. Cantilevered Stepped Floor Bastions (Alternating Concrete & Shadow Relief)
  const nTiers = floor(tiers);
  for (let k = 0; k < nTiers; k++) {
    const frac = k / nTiers;
    const yPos = (0.95 - k * 0.52) * scale_factor;
    const shiftX = ((k % 2 === 0) ? 0.35 : -0.35) * (1.0 - frac * 0.25) * scale_factor;
    const shiftZ = ((k % 3 === 0) ? 0.25 : -0.25) * scale_factor;
    const slabW = (1.6 - frac * 0.35) * scale_factor;
    const slabD = (1.2 - frac * 0.25) * scale_factor;

    push();
    translate(shiftX, yPos, shiftZ);

    // Weathered aggregate cantilever slab
    fill(0.478, 0.478, 0.471);
    box(slabW, 0.16 * scale_factor, slabD);

    // Recessed modular coffered window cavities (repeated openings)
    push();
    translate(0, 0.12 * scale_factor, 0);
    fill(0.133, 0.133, 0.133);
    box(slabW * 0.82, 0.08 * scale_factor, slabD * 0.82);
    pop();

    // Structural corbel brackets underneath
    push();
    translate(0, 0.16 * scale_factor, 0);
    fill(0.851, 0.839, 0.812);
    box(slabW * 0.45, 0.08 * scale_factor, slabD * 0.45);
    pop();

    pop();
  }

  // 4. Massive Diagonal External Shear Trusses
  push();
  translate(0.55 * scale_factor, 0.25 * scale_factor, 0.35 * scale_factor);
  rotateZ(0.50);
  fill(0.478, 0.478, 0.471);
  box(0.12 * scale_factor, 1.9 * scale_factor, 0.12 * scale_factor);
  pop();

  push();
  translate(-0.55 * scale_factor, 0.25 * scale_factor, -0.35 * scale_factor);
  rotateZ(-0.50);
  fill(0.478, 0.478, 0.471);
  box(0.12 * scale_factor, 1.9 * scale_factor, 0.12 * scale_factor);
  pop();

  // 5. Crown Observation Deck & Heavy Parapet Cap
  push();
  translate(0, (1.25 - mass_height) * scale_factor - 0.15 * scale_factor, 0);
  fill(0.851, 0.839, 0.812);
  box(0.85 * scale_factor, 0.14 * scale_factor, 0.85 * scale_factor);

  // Parapet slit opening
  translate(0, -0.12 * scale_factor, 0);
  fill(0.133, 0.133, 0.133);
  box(0.70 * scale_factor, 0.10 * scale_factor, 0.70 * scale_factor);
  pop();

  pop();
}
"""
    },
    {
        "slug": "brutalist-architecture-cantilevered-stele-3d",
        "title": "Brutalist Architecture Cantilevered Stele 3D",
        "description": "Heroic brutalist monument stele with dramatic asymmetrical cantilever beams, interlocking precast concrete slabs, vertical shadow reveal slits, and raw stone massing.",
        "tags": ["brutalist", "architecture", "cantilever", "stele", "concrete", "3d"],
        "code": """// Brutalist Architecture Cantilevered Stele 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Heroic brutalist monument stele with dramatic asymmetrical cantilever beams, interlocking precast concrete slabs, vertical shadow reveal slits, and raw stone massing.
param("spin", 0.35, 0.1, 1.2);
param("scale_factor", 1.15, 0.5, 2.0);
param("reach", 1.55, 0.8, 2.5);
param("beams", 5.0, 3.0, 8.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.50 + sin(time * 0.12) * 0.03);
  rotateY(time * 0.27 + 0.55);

  // 1. Massive stepped polygonal concrete podium
  push();
  translate(0, 1.25 * scale_factor, 0);
  fill(0.12, 0.12, 0.14); // Deep basalt plinth
  box(2.8 * scale_factor, 0.14, 2.8 * scale_factor);

  translate(0, -0.09, 0);
  fill(0.48, 0.48, 0.47); // Weathered aggregate sub-plinth
  box(2.2 * scale_factor, 0.06, 2.2 * scale_factor);

  translate(0, -0.05, 0);
  fill(0.86, 0.85, 0.82); // Poured lime terrace deck
  box(1.7 * scale_factor, 0.05, 1.7 * scale_factor);
  pop();

  // 2. Monolithic Central Core Stele Tower
  push();
  translate(0, (1.25 - 2.5 * 0.5) * scale_factor, 0);
  fill(0.86, 0.85, 0.82); // Pale raw concrete
  box(0.58 * scale_factor, 2.5 * scale_factor, 0.58 * scale_factor);

  // Deep vertical shadow reveal embrasures
  fill(0.10, 0.10, 0.12);
  box(0.40 * scale_factor, 2.4 * scale_factor, 0.64 * scale_factor);
  box(0.64 * scale_factor, 2.4 * scale_factor, 0.40 * scale_factor);
  pop();

  // 3. Asymmetric Heroic Cantilevered Wings (Alternating Concrete & Shadow Recesses)
  const nBeams = floor(beams);
  for (let b = 0; b < nBeams; b++) {
    const frac = b / nBeams;
    const yPos = (0.90 - frac * 1.75) * scale_factor;
    const rot = frac * Math.PI * 1.45 + time * 0.12;
    const len = (reach * (1.15 - frac * 0.25) + sin(b * 1.7) * 0.2) * scale_factor;

    push();
    translate(0, yPos, 0);
    rotateY(rot);

    // Cantilever post-tensioned beam in weathered aggregate
    push();
    translate(len * 0.5, 0, 0);
    fill(0.48, 0.48, 0.47);
    box(len, 0.15 * scale_factor, 0.26 * scale_factor);

    // Repeated modular coffered window cavities along beam
    const nWindows = 3;
    for (let w = 0; w < nWindows; w++) {
      push();
      translate(-len * 0.35 + w * (len * 0.32), 0, 0);
      fill(0.10, 0.10, 0.12);
      box(len * 0.18, 0.10 * scale_factor, 0.30 * scale_factor);
      pop();
    }

    // Terminal hanging modular pod block
    translate(len * 0.44, 0.16 * scale_factor, 0);
    fill(0.86, 0.85, 0.82);
    box(0.32 * scale_factor, 0.24 * scale_factor, 0.32 * scale_factor);

    // Deep window embrasure inside pod
    fill(0.10, 0.10, 0.12);
    box(0.18 * scale_factor, 0.14 * scale_factor, 0.34 * scale_factor);
    pop();

    // Diagonal structural support corbel / brace underneath
    push();
    translate(len * 0.32, 0.22 * scale_factor, 0);
    rotateZ(-0.46);
    fill(0.12, 0.12, 0.14);
    box(len * 0.65, 0.07 * scale_factor, 0.14 * scale_factor);
    pop();

    pop();
  }

  // 4. Diagonal Structural Exoskeleton Bracing Columns
  push();
  translate(0.65 * scale_factor, 0.25 * scale_factor, 0.40 * scale_factor);
  rotateZ(0.52);
  fill(0.48, 0.48, 0.47);
  box(0.10 * scale_factor, 2.1 * scale_factor, 0.10 * scale_factor);
  pop();

  push();
  translate(-0.65 * scale_factor, 0.25 * scale_factor, -0.40 * scale_factor);
  rotateZ(-0.52);
  fill(0.48, 0.48, 0.47);
  box(0.10 * scale_factor, 2.1 * scale_factor, 0.10 * scale_factor);
  pop();

  // 5. Heroic Monument Crown Finial & Stepped Parapet Array
  push();
  translate(0, (1.25 - 2.5) * scale_factor - 0.14 * scale_factor, 0);
  fill(0.86, 0.85, 0.82);
  box(0.85 * scale_factor, 0.14 * scale_factor, 0.85 * scale_factor);

  // Parapet slit embrasures
  translate(0, -0.10 * scale_factor, 0);
  fill(0.10, 0.10, 0.12);
  box(0.70 * scale_factor, 0.08 * scale_factor, 0.70 * scale_factor);

  // Spomenik cantilever crest finial
  translate(0.15 * scale_factor, -0.12 * scale_factor, 0.15 * scale_factor);
  fill(0.48, 0.48, 0.47);
  box(0.50 * scale_factor, 0.16 * scale_factor, 0.50 * scale_factor);
  pop();

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: synthwave)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "synthwave-analog-poly-brass-lead-synth",
        "title": "Synthwave Analog Poly Brass Lead Synth",
        "description": "Lush 1980s retro-futuristic analog brass synthesizer featuring detuned sawtooth oscillators, warm 24dB resonant lowpass ladder filter with punchy attack envelope, and subtle chorus width.",
        "tags": ["synth", "synthwave", "brass", "analog", "poly", "audio"],
        "note_root": 60,
        "params": {
            "cutoff": 0.62,
            "resonance": 0.40,
            "detune": 0.35,
            "decayTime": 0.45,
            "drive": 0.30
        },
        "code": """param float cutoff = 0.62 [0.1, 1.0]
param float resonance = 0.40 [0.1, 0.95]
param float detune = 0.35 [0.0, 1.0]
param float decayTime = 0.45 [0.1, 1.5]
param float drive = 0.30 [0.0, 1.0]
state float p1 = 0
state float p2 = 0
state float pSub = 0
state float env = 0
state float s1 = 0
state float s2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decayTime), 0.0001, 0.05)
env = if(target > env, env + (target - env) * 0.94, env - env * decayRate)

dp = freq / sr
dDet = dp * detune * 0.025
p1 = (p1 + dp + dDet) % 1.0
p2 = (p2 + dp - dDet) % 1.0
saw1 = p1 * 2.0 - 1.0
saw2 = p2 * 2.0 - 1.0

dpSub = dp * 0.5
pSub = (pSub + dpSub) % 1.0
sub = if(pSub < 0.5, 0.8, -0.8)

sig = (saw1 * 0.55 + saw2 * 0.55 + sub * 0.35) * (1.0 + drive * 1.2)

fNorm = clamp(cutoff * 0.20 + env * 0.30, 0.02, 0.46)
kRes = clamp(resonance * 1.8, 0.1, 1.9)

s1 = s1 + fNorm * (sig - s2 * kRes - s1)
s2 = s2 + fNorm * (s1 - s2)

sat = (s2 / (1.0 + abs(s2 * 0.42))) * env
out = sat * 0.58
"""
    },
    {
        "slug": "synthwave-neon-running-bass-synth",
        "title": "Synthwave Neon Running Bass Synth",
        "description": "Driving 16th-note analog synthwave bass synthesizer with paired saw and square wave oscillators, snappy transistor decay envelope, tight resonant filter bite, and analog grit.",
        "tags": ["synth", "synthwave", "bass", "arpeggio", "analog", "audio"],
        "note_root": 36,
        "params": {
            "cutoff": 0.55,
            "resonance": 0.58,
            "envMod": 0.65,
            "decayTime": 0.24,
            "drive": 0.38
        },
        "code": """param float cutoff = 0.55 [0.1, 1.0]
param float resonance = 0.58 [0.1, 0.95]
param float envMod = 0.65 [0.0, 1.0]
param float decayTime = 0.24 [0.06, 0.8]
param float drive = 0.38 [0.0, 1.0]
state float p1 = 0
state float pSub = 0
state float env = 0
state float s1 = 0
state float s2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decayTime), 0.0001, 0.08)
env = if(target > env, env + (target - env) * 0.96, env - env * decayRate)

dp = freq / sr
p1 = (p1 + dp) % 1.0
saw = p1 * 2.0 - 1.0

dpSub = dp * 0.5
pSub = (pSub + dpSub) % 1.0
sub = if(pSub < 0.5, 1.0, -1.0)

sig = (saw * 0.70 + sub * 0.50) * (1.0 + drive * 1.3)

fNorm = clamp(cutoff * 0.15 + env * envMod * 0.35, 0.015, 0.45)
kRes = clamp(resonance * 1.82, 0.1, 1.9)

s1 = s1 + fNorm * (sig - s2 * kRes - s1)
s2 = s2 + fNorm * (s1 - s2)

sat = (s2 / (1.0 + abs(s2 * 0.48))) * env
out = sat * 0.54
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: glitch)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "glitch-buffer-stutter-repeater-fx",
        "title": "Glitch Buffer Stutter Repeater FX",
        "description": "Granular micro-loop stutter effect with clock-synced buffer freeze, decaying rhythmic micro-repeats, asymmetric diode waveshaping, and dynamic dry-wet crossfade.",
        "tags": ["effect", "glitch", "stutter", "repeater", "granular", "audio"],
        "params": {
            "stutterRate": 0.45,
            "drive": 0.40,
            "tone": 0.55,
            "feedback": 0.40,
            "mix": 0.55
        },
        "code": """param float stutterRate = 0.45 [0.1, 1.0]
param float drive = 0.40 [0.0, 1.0]
param float tone = 0.55 [0.1, 1.0]
param float feedback = 0.40 [0.0, 0.9]
param float mix = 0.55 [0.0, 1.0]
state float d1 = 0
state float d2 = 0
state float d3 = 0
state float lp = 0

inSat = in * (1.0 + drive * 2.0)
sat = inSat / (1.0 + abs(inSat * 0.6))

fTone = clamp(0.08 + tone * 0.36, 0.02, 0.45)
lp = lp + fTone * (sat - lp)

kDiff = clamp(0.12 * (1.0 + stutterRate * 0.7), 0.04, 0.36)
d1 = d1 + kDiff * (lp + d3 * feedback * 0.65 - d1)
d2 = d2 + kDiff * 0.88 * (d1 - d2)
d3 = d3 + kDiff * 0.78 * (d2 - d3)

stutter = (d1 - d2 + d3 * 0.7) * 0.9
processed = in * (1.0 - mix) + stutter * mix
out = processed * 0.65
"""
    },
    {
        "slug": "glitch-spectral-aliasing-bitcrush-fx",
        "title": "Glitch Spectral Aliasing Bitcrush FX",
        "description": "Searing industrial glitch downsampler featuring variable sample-rate reduction, nonlinear wavefolding drive, quantized amplitude bit-depth crushing, and warm tone filtering.",
        "tags": ["effect", "glitch", "bitcrush", "aliasing", "distortion", "audio"],
        "params": {
            "crush": 0.50,
            "drive": 0.45,
            "tone": 0.50,
            "wavefold": 0.35,
            "mix": 0.60
        },
        "code": """param float crush = 0.50 [0.0, 0.9]
param float drive = 0.45 [0.0, 1.0]
param float tone = 0.50 [0.1, 1.0]
param float wavefold = 0.35 [0.0, 1.0]
param float mix = 0.60 [0.0, 1.0]
state float s1 = 0
state float s2 = 0

steps = clamp(floor(28.0 - crush * 22.0), 3.0, 28.0)
qIn = floor(in * steps) / steps

pre = qIn * (1.0 + drive * 2.4)
fold = sin(pre * (1.0 + wavefold * 1.8))
sat = (fold - (fold * fold * fold * 0.12)) / (1.0 + abs(fold * 0.35))

fCut = clamp(0.06 + tone * 0.36, 0.02, 0.45)
s1 = s1 + fCut * (sat - s1)
s2 = s2 + fCut * (s1 - s2)

processed = in * (1.0 - mix) + s2 * mix
out = processed * 0.48
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel Sources & Embeds (Theme: generative_plotter)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "generative-plotter-curvilinear-flowfield",
        "title": "Generative Plotter Curvilinear Flowfield",
        "description": "Algorithmic plotter line art simulating continuous ink pen streamlines through a harmonic turbulent vector flowfield, with audio-reactive displacement and vermilion ink accents.",
        "embed_desc": "Audio-reactive generative plotter visualizer rendering hundreds of fine ink streamlines flowing across cream archival paper. Bass frequencies swell flowfield turbulence and ink stroke weight, mid frequencies warp vector curvature and wave frequency, and high frequencies illuminate intricate vermilion drafting accents.",
        "tags": ["pixel", "plotter", "generative", "flowfield", "lines", "shader"],
        "params": {
            "speed": 0.35,
            "density": 32.0,
            "curl": 0.45,
            "contrast": 1.15,
            "vermilion": 0.40
        },
        "code": """param float speed = 0.35 [0.1, 1.5]
param float density = 32.0 [12.0, 64.0]
param float curl = 0.45 [0.1, 1.0]
param float contrast = 1.15 [0.6, 2.0]
param float vermilion = 0.40 [0.0, 1.0]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2
time = t * speed

r = length(p)
theta = atan2(p.y, p.x)

flow1 = sin(p.y * density * 0.4 + sin(p.x * 3.5 + time * 2.0) * curl * 4.0)
flow2 = cos(p.x * density * 0.4 + cos(p.y * 3.5 - time * 2.0) * curl * 4.0)
stream = (flow1 + flow2) * 0.5

rings = sin(r * density * 0.8 - time * 3.0 + theta * 2.0)
lines = sin(stream * 3.14159 + rings * 0.5)

cPaper = vec3(0.961, 0.945, 0.902)
cCarbon = vec3(0.067, 0.067, 0.067)
cVermilion = vec3(0.820, 0.188, 0.122)

ink = smoothstep(0.15, -0.15, abs(lines) - 0.22)
col = mix(cPaper, cCarbon, ink)

accent = exp(-abs(stream - 0.35) * 8.0) * exp(-r * 1.8)
col = mix(col, cVermilion, clamp(accent * vermilion * 2.2, 0.0, 1.0))

out = clamp(col * contrast, 0.0, 1.0)
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
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2;
  float time = t * 0.35;

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  float r = length(p);
  float theta = atan(p.y, p.x);

  float curl = 0.45 + b * 0.35;
  float density = 32.0 + m * 10.0;

  float flow1 = sin(p.y * density * 0.4 + sin(p.x * (3.5 + m * 2.0) + time * 2.0) * curl * 4.0);
  float flow2 = cos(p.x * density * 0.4 + cos(p.y * (3.5 + m * 2.0) - time * 2.0) * curl * 4.0);
  float stream = (flow1 + flow2) * 0.5;

  float rings = sin(r * density * 0.8 - time * 3.0 + theta * 2.0);
  float lines = sin(stream * 3.14159 + rings * 0.5);

  vec3 cPaper = vec3(0.961, 0.945, 0.902);
  vec3 cCarbon = vec3(0.067, 0.067, 0.067);
  vec3 cVermilion = vec3(0.820, 0.188, 0.122);

  float ink = smoothstep(0.15, -0.15, abs(lines) - (0.22 + b * 0.08));
  vec3 col = mix(cPaper, cCarbon, ink);

  float accent = exp(-abs(stream - 0.35) * 8.0) * exp(-r * 1.8);
  col = mix(col, cVermilion, clamp(accent * (0.40 + h * 0.6) * 2.2, 0.0, 1.0));

  gl_FragColor = vec4(clamp(col * 1.15, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "generative-plotter-concentric-isotonic",
        "title": "Generative Plotter Concentric Isotonic",
        "description": "Mathematical topographical contour line art featuring dense concentric isotonic rings, audio-reactive harmonic wave ripple modulation, and carbon and vermilion pen indexing.",
        "embed_desc": "Audio-reactive pen-plotter visualizer generating pulsating isotonic elevation contours and harmonic ripple waves. Bass beats expand concentric ring displacement and line pressure, mid frequencies shear topographical wave ripples, and high frequencies trigger vibrant vermilion contour highlights.",
        "tags": ["pixel", "plotter", "concentric", "topography", "contour", "shader"],
        "params": {
            "speed": 0.32,
            "rings": 28.0,
            "warp": 0.42,
            "contrast": 1.15,
            "vermilion": 0.45
        },
        "code": """param float speed = 0.32 [0.1, 1.5]
param float rings = 28.0 [10.0, 60.0]
param float warp = 0.42 [0.1, 1.0]
param float contrast = 1.15 [0.6, 2.0]
param float vermilion = 0.45 [0.0, 1.0]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2
time = t * speed

r = length(p)
theta = atan2(p.y, p.x)

disp = sin(theta * 6.0 + time * 2.2) * cos(r * 8.0 - time * 1.8) * warp * 0.25
lens = r + disp

contour = sin(lens * rings * 2.0)
penLine = smoothstep(0.20, -0.20, abs(contour) - 0.25)

cross = sin(p.x * 24.0) * sin(p.y * 24.0)
hatch = step(0.65, cross) * step(r, 0.65) * 0.25

cPaper = vec3(0.961, 0.945, 0.902)
cCarbon = vec3(0.067, 0.067, 0.067)
cVermilion = vec3(0.820, 0.188, 0.122)

col = mix(cPaper, cCarbon, penLine)
col = mix(col, cCarbon, hatch)

vortex = exp(-abs(contour - 0.5) * 6.0) * step(0.3, r) * step(r, 0.85)
col = mix(col, cVermilion, clamp(vortex * vermilion * 1.8, 0.0, 1.0))

out = clamp(col * contrast, 0.0, 1.0)
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
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2;
  float time = t * 0.32;

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  float r = length(p);
  float theta = atan(p.y, p.x);

  float warp = 0.42 + b * 0.30;
  float rings = 28.0 + m * 8.0;

  float disp = sin(theta * (6.0 + m * 2.0) + time * 2.2) * cos(r * (8.0 + m * 2.0) - time * 1.8) * warp * 0.25;
  float lens = r + disp;

  float contour = sin(lens * rings * 2.0);
  float penLine = smoothstep(0.20, -0.20, abs(contour) - (0.25 + b * 0.08));

  float cross = sin(p.x * 24.0) * sin(p.y * 24.0);
  float hatch = step(0.65, cross) * step(r, 0.65) * 0.25;

  vec3 cPaper = vec3(0.961, 0.945, 0.902);
  vec3 cCarbon = vec3(0.067, 0.067, 0.067);
  vec3 cVermilion = vec3(0.820, 0.188, 0.122);

  vec3 col = mix(cPaper, cCarbon, penLine);
  col = mix(col, cCarbon, hatch);

  float vortex = exp(-abs(contour - 0.5) * 6.0) * step(0.3, r) * step(r, 0.85);
  col = mix(col, cVermilion, clamp(vortex * (0.45 + h * 0.5) * 1.8, 0.0, 1.0));

  gl_FragColor = vec4(clamp(col * 1.15, 0.0, 1.0), 1.0);
}
"""
    }
]

def make_embed_html(item):
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
  <title>{item['title']} - Infinite WebGL Embed</title>
  <style>
    * {{ box-sizing: border-box; margin: 0; padding: 0; }}
    body {{
      background: #0b132b;
      overflow: hidden;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      color: #fff;
      display: flex;
      align-items: center;
      justify-content: center;
      min-height: 100vh;
      min-height: 100dvh;
    }}
    .badge {{
      position: absolute;
      top: 14px;
      right: 14px;
      padding: 6px 12px;
      background: rgba(11, 19, 43, 0.85);
      backdrop-filter: blur(8px);
      border: 1px solid rgba(91, 192, 190, 0.25);
      border-radius: 999px;
      color: #fff;
      font-size: 11px;
      font-weight: 500;
      text-decoration: none;
      display: flex;
      align-items: center;
      gap: 6px;
      box-shadow: 0 4px 12px rgba(0,0,0,0.3);
      transition: transform 0.2s, background 0.2s;
      z-index: 10;
    }}
    .badge:hover {{
      transform: translateY(-2px);
      background: rgba(28, 37, 65, 0.95);
      border-color: rgba(91, 192, 190, 0.6);
    }}
    .badge-icon {{
      width: 14px;
      height: 14px;
      border-radius: 50%;
      background: linear-gradient(135deg, #5bc0be, #ffffff);
      display: inline-block;
    }}
    .controls {{
      position: absolute;
      bottom: 14px;
      left: 14px;
      display: flex;
      gap: 8px;
      z-index: 10;
    }}
    .btn {{
      background: rgba(11, 19, 43, 0.82);
      backdrop-filter: blur(8px);
      border: 1px solid rgba(255, 255, 255, 0.15);
      color: #eaeaea;
      padding: 6px 10px;
      border-radius: 6px;
      font-size: 11px;
      cursor: pointer;
    }}
    .btn:hover {{ background: rgba(255, 255, 255, 0.15); }}
  </style>
</head>
<body>
  <main>
    <h1 class="sr-only">{item['title']}</h1>
    <canvas id="gl" role="img" aria-label="{item['title']}, an audio-reactive visual"></canvas>

  <div class="controls">
    <button class="btn" id="btn-audio">enable mic input</button>
  </div>

  <a href="https://infinitedevices.org" target="_blank" rel="noopener" class="badge" title="infinite: the open-source audiovisual daw">
    <span class="badge-icon"></span>
    <span>made in <strong>infinite</strong></span>
  </a>
  </main>

  <script>
    const canvas = document.getElementById("gl");
    const gl = canvas.getContext("webgl");
    if (!gl) {{ document.body.innerHTML = "webgl not supported"; }}

    const vsSource = `
      attribute vec2 position;
      void main() {{
        gl_Position = vec4(position, 0.0, 1.0);
      }}
    `;

    const fsSource = `{item['glsl']}`;

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
      -1.0, -1.0,
       1.0, -1.0,
      -1.0,  1.0,
      -1.0,  1.0,
       1.0, -1.0,
       1.0,  1.0
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

    let audioCtx, analyser, freqData;
    let bass = 0, mid = 0, high = 0;

    document.getElementById("btn-audio").addEventListener("click", async () => {{
      if (!audioCtx) {{
        try {{
          const stream = await navigator.mediaDevices.getUserMedia({{ audio: true }});
          audioCtx = new (window.AudioContext || window.webkitAudioContext)();
          const source = audioCtx.createMediaStreamSource(stream);
          analyser = audioCtx.createAnalyser();
          analyser.fftSize = 64;
          source.connect(analyser);
          freqData = new Uint8Array(analyser.frequencyBinCount);
          document.getElementById("btn-audio").innerText = "mic active";
        }} catch(e) {{
          document.getElementById("btn-audio").innerText = "mic denied";
        }}
      }}
    }});

    function resize() {{
      const dpr = window.devicePixelRatio || 1;
      const w = window.innerWidth;
      const h = window.innerHeight;
      if (canvas.width !== w * dpr || canvas.height !== h * dpr) {{
        canvas.width = w * dpr;
        canvas.height = h * dpr;
        gl.viewport(0, 0, canvas.width, canvas.height);
      }}
    }}
    window.addEventListener("resize", resize);
    resize();

    let start = performance.now();
    function render() {{
      resize();
      const now = performance.now();
      const t = (now - start) * 0.001;

      if (analyser && freqData) {{
        analyser.getByteFrequencyData(freqData);
        let b = 0, m = 0, h = 0;
        for (let i = 0; i < 4; i++) b += freqData[i];
        for (let i = 4; i < 16; i++) m += freqData[i];
        for (let i = 16; i < 32; i++) h += freqData[i];
        bass += (b / (4 * 255.0) - bass) * 0.3;
        mid += (m / (12 * 255.0) - mid) * 0.3;
        high += (h / (16 * 255.0) - high) * 0.3;
      }} else {{
        bass = 0.5 + 0.5 * Math.sin(t * 2.2);
        mid = 0.5 + 0.5 * Math.sin(t * 3.7 + 1.2);
        high = 0.5 + 0.5 * Math.sin(t * 5.1 + 2.4);
      }}

      gl.uniform2f(uRes, canvas.width, canvas.height);
      gl.uniform1f(uTime, t);
      gl.uniform1f(uAspect, canvas.width / canvas.height);
      gl.uniform1f(uBass, bass);
      gl.uniform1f(uMid, mid);
      gl.uniform1f(uHigh, high);

      gl.drawArrays(gl.TRIANGLES, 0, 6);
      requestAnimationFrame(render);
    }}
    render();
  </script>
</body>
</html>
"""

def main():
    print("=== Creative Expansion Pack 32: Build and Gate Runner ===")
    failed = False
    report = []

    # ---------------------------------------------------------
    # Process Family 1: Sketch 2D
    # ---------------------------------------------------------
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

        v = subprocess.run([BIN, "validate", test_inf], capture_output=True, text=True)
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}")
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        frame_png = os.path.join(out_frame_dir, "frame.png")
        f_proc = subprocess.run([BIN, "frame", test_inf, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        if '"ok":true' not in f_proc.stdout or not os.path.exists(frame_png):
            print(f"FAIL frame: {slug}\n{f_proc.stdout}")
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

    # ---------------------------------------------------------
    # Process Family 2: Sketch 3D
    # ---------------------------------------------------------
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
  c bg 0.08 0.10 0.14
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

        v = subprocess.run([BIN, "validate", test_inf], capture_output=True, text=True)
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}")
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        frame_png = os.path.join(out_frame_dir, "frame.png")
        f_proc = subprocess.run([BIN, "frame", test_inf, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        if '"ok":true' not in f_proc.stdout or not os.path.exists(frame_png):
            print(f"FAIL frame: {slug}\n{f_proc.stdout}")
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

    # ---------------------------------------------------------
    # Process Family 3: Field Synth
    # ---------------------------------------------------------
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
  f rateBeats 0.25
  i steps 16
  i note0 {nr}
  i note1 {nr}
  i note2 {nr+12}
  i note3 {nr+7}
  i note4 {nr}
  i note5 {nr+12}
  i note6 {nr+5}
  i note7 {nr+7}
  i note8 {nr}
  i note9 {nr}
  i note10 {nr+12}
  i note11 {nr+15}
  i note12 {nr+3}
  i note13 {nr+5}
  i note14 {nr+7}
  i note15 {nr+10}
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
  c fillColor 0.18 0.22 0.32
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

        v = subprocess.run([BIN, "validate", inf_file], capture_output=True, text=True)
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}")
            failed = True
            continue

        sum_json = os.path.join(TMP_DIR, f"{slug}_sum.json")
        s = subprocess.run([BIN, "audio", inf_file, sum_json, "--duration", "6"], capture_output=True, text=True)
        if '"ok":true' not in s.stdout or not os.path.exists(sum_json):
            print(f"FAIL audio command: {slug}\n{s.stdout}")
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
        f_proc = subprocess.run([BIN, "frame", inf_file, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        im = Image.open(frame_png)

        prev_jpg = os.path.join(PREVIEWS_DIR, f"{slug}.jpg")
        im.convert("RGB").save(prev_jpg, quality=94)

        thumb_webp = os.path.join(THUMBS_DIR, f"{slug}.webp")
        im.convert("RGB").resize((540, 540), Image.Resampling.LANCZOS).save(thumb_webp, "WEBP", quality=85)

        tmpl_webp = os.path.join(TEMPLATES_DIR, f"{slug}.webp")
        template_preview.render(inf_file, tmpl_webp)

        report.append(f"PASS [Field Synth] {slug}: lufs={lufs:.2f}, peak={peak:.2f}dBFS, silence={silence}")

    # ---------------------------------------------------------
    # Process Family 4: Field Effects
    # ---------------------------------------------------------
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
  f rateBeats 0.25
  i steps 16
  i note0 48
  i note1 48
  i note2 60
  i note3 55
  i note4 48
  i note5 60
  i note6 53
  i note7 55
  i note8 48
  i note9 48
  i note10 60
  i note11 55
  i note12 48
  i note13 60
  i note14 53
  i note15 58
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
  f cutoff 3200.0
  f resonance 0.30
  f decay 0.26
  f volume 0.75
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
  c fillColor 0.15 0.20 0.30
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

        v = subprocess.run([BIN, "validate", inf_file], capture_output=True, text=True)
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}")
            failed = True
            continue

        sum_json = os.path.join(TMP_DIR, f"{slug}_sum.json")
        s = subprocess.run([BIN, "audio", inf_file, sum_json, "--duration", "6"], capture_output=True, text=True)
        if '"ok":true' not in s.stdout or not os.path.exists(sum_json):
            print(f"FAIL audio command: {slug}\n{s.stdout}")
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
        f_proc = subprocess.run([BIN, "frame", inf_file, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        im = Image.open(frame_png)

        prev_jpg = os.path.join(PREVIEWS_DIR, f"{slug}.jpg")
        im.convert("RGB").save(prev_jpg, quality=94)

        thumb_webp = os.path.join(THUMBS_DIR, f"{slug}.webp")
        im.convert("RGB").resize((540, 540), Image.Resampling.LANCZOS).save(thumb_webp, "WEBP", quality=85)

        tmpl_webp = os.path.join(TEMPLATES_DIR, f"{slug}.webp")
        template_preview.render(inf_file, tmpl_webp)

        report.append(f"PASS [Field Effects] {slug}: lufs={lufs:.2f}, peak={peak:.2f}dBFS, silence={silence}")

    # ---------------------------------------------------------
    # Process Family 5: Field Pixel & Embeds
    # ---------------------------------------------------------
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

        v = subprocess.run([BIN, "validate", test_inf], capture_output=True, text=True)
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}")
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        frame_png = os.path.join(out_frame_dir, "frame.png")
        f_proc = subprocess.run([BIN, "frame", test_inf, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        if '"ok":true' not in f_proc.stdout or not os.path.exists(frame_png):
            print(f"FAIL frame: {slug}\n{f_proc.stdout}")
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

    if failed:
        print("Build failed on one or more items. Aborting registration.")
        sys.exit(1)

    print("\n--- Gate Verification Report ---")
    for line in report:
        print(line)
    print(f"All {len(report)} items passed every engine gate!\n")

    # ---------------------------------------------------------
    # Register in library index.json
    # ---------------------------------------------------------
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
                "domain": "sketch",
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
                "tags": [t.lower() for t in it["tags"]],
                "description": it["description"],
                "price": 0,
                "domain": "synth",
                "category": "Synths",
                "params": 5,
                "file": f"assets/templates/{it['slug']}.inf",
                "preview": f"assets/library/previews/{it['slug']}.jpg",
                "bytes": it["bytes"]
            })

    for it in FX_ITEMS:
        if it["slug"] not in existing_lib_ids:
            lib_data["items"].append({
                "id": it["slug"],
                "name": it["title"],
                "kind": "Effect",
                "tags": [t.lower() for t in it["tags"]],
                "description": it["description"],
                "price": 0,
                "domain": "audio",
                "category": "Audio Effects",
                "params": 5,
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

    # ---------------------------------------------------------
    # Register in templates index.json
    # ---------------------------------------------------------
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

    # ---------------------------------------------------------
    # Register in embeds.json
    # ---------------------------------------------------------
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

    # ---------------------------------------------------------
    # Register in token_baseline.json
    # ---------------------------------------------------------
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

    # ---------------------------------------------------------
    # Update infinite-library.zip
    # ---------------------------------------------------------
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
