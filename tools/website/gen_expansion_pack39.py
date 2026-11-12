#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 39.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: swiss_typographic -> swiss-typographic-modular-grid-harmony, swiss-typographic-geometric-tension-matrix)
2. Sketch 3D (Theme: holographic_iridescent -> holographic-iridescent-prismatic-polytope-3d, holographic-iridescent-faceted-torus-lattice-3d)
3. Field Synth (Theme: neurofunk -> neurofunk-modulated-reese-bass-synth, neurofunk-cybernetic-growl-lead-synth)
4. Field Effects (Theme: french_filter_house -> french-filter-house-pumping-disco-sweep-fx, french-filter-house-creamy-phaser-drive-fx)
5. Field Pixel & Embeds (Theme: cymatics_sine -> cymatics-sine-chladni-plate-resonance, cymatics-sine-harmonic-modal-interference)
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

TMP_DIR = "/tmp/pack39_build"
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
        "slug": "swiss-typographic-modular-grid-harmony",
        "title": "Swiss Typographic Modular Grid Harmony",
        "description": "Asymmetric constructivist layout constructed on a 12-column modular typographic grid with mathematical proportion divisions, vermilion geometric accents, and high-contrast typographic hierarchy.",
        "tags": ["swiss", "typographic", "grid", "constructivist", "minimal", "procedural"],
        "code": """// Swiss Typographic Modular Grid Harmony - Infinite Library (Sketch 2D)
param("columns", 12.0, 6.0, 16.0);
param("line_weight", 1.5, 0.8, 3.0);
param("accent_scale", 1.0, 0.5, 2.0);
param("contrast", 1.15, 0.8, 1.8);

function draw(t) {
  // Warm cream archival paper ground (#F5F5F2)
  background(0.961, 0.961, 0.949);

  const w = width;
  const h = height;
  const margin = 72;
  const innerW = w - margin * 2;
  const innerH = h - margin * 2;

  // 1. Technical framing & crop marks
  stroke(0.067, 0.067, 0.067);
  strokeWeight(line_weight * 1.4);
  noFill();
  rect(margin, margin, innerW, innerH);

  // Exterior corner crop marks
  strokeWeight(1.0);
  line(margin - 24, margin, margin - 6, margin);
  line(margin, margin - 24, margin, margin - 6);
  line(w - margin + 6, margin, w - margin + 24, margin);
  line(w - margin, margin - 24, w - margin, margin - 6);
  line(margin - 24, h - margin, margin - 6, h - margin);
  line(margin, h - margin + 6, margin, h - margin + 24);
  line(w - margin + 6, h - margin, w - margin + 24, h - margin);
  line(w - margin, h - margin + 6, w - margin, h - margin + 24);

  // Precision graticule ticks
  for (let x = margin + 36; x < w - margin; x += 36) {
    line(x, margin, x, margin + 8);
    line(x, h - margin, x, h - margin - 8);
  }
  for (let y = margin + 36; y < h - margin; y += 36) {
    line(margin, y, margin + 8, y);
    line(w - margin, y, w - margin - 8, y);
  }

  // 2. 12-Column Typographic Modular Grid Lines
  const cols = floor(columns);
  const colWidth = innerW / cols;
  stroke(0.78, 0.78, 0.76, 0.65);
  strokeWeight(0.8);
  for (let c = 1; c < cols; c++) {
    const x = margin + c * colWidth;
    line(x, margin, x, h - margin);
  }

  // Horizontal baseline grid
  const rowHeight = innerH / 16.0;
  for (let r = 1; r < 16; r++) {
    const y = margin + r * rowHeight;
    line(margin, y, w - margin, y);
  }

  // 3. Asymmetric Constructivist Modules (Black & Vermilion #E4261A)
  noStroke();
  fill(0.894, 0.149, 0.102);
  const vX = margin + colWidth * 2;
  const vY = margin + rowHeight * 3;
  const vW = colWidth * 5 * accent_scale;
  const vH = rowHeight * 4;
  rect(vX, vY, vW, vH);

  fill(0.067, 0.067, 0.067);
  rect(margin + colWidth * 7, margin + rowHeight * 7, colWidth * 4, rowHeight * 6);

  fill(0.067, 0.067, 0.067);
  rect(margin + colWidth * 1, margin + rowHeight * 11, colWidth * 4, rowHeight * 1.5);
  rect(margin + colWidth * 8, margin + rowHeight * 2, colWidth * 3, rowHeight * 0.8);

  stroke(0.894, 0.149, 0.102);
  strokeWeight(line_weight * 2.2);
  line(margin + colWidth * 2, margin + rowHeight * 14, margin + colWidth * 10, margin + rowHeight * 6);

  fill(0.067, 0.067, 0.067);
  noStroke();
  const circleX = margin + colWidth * 4;
  const circleY = margin + rowHeight * 9;
  ellipse(circleX, circleY, colWidth * 2.8, colWidth * 2.8);

  fill(0.961, 0.961, 0.949);
  ellipse(circleX, circleY, colWidth * 1.4, colWidth * 1.4);

  fill(0.894, 0.149, 0.102);
  ellipse(circleX, circleY, colWidth * 0.4, colWidth * 0.4);

  stroke(0.067, 0.067, 0.067);
  strokeWeight(1.2);
  line(circleX - 32, circleY, circleX + 32, circleY);
  line(circleX, circleY - 32, circleX, circleY + 32);
}
"""
    },
    {
        "slug": "swiss-typographic-geometric-tension-matrix",
        "title": "Swiss Typographic Geometric Tension Matrix",
        "description": "Dynamic Josef Müller-Brockmann inspired composition exploring radial and orthogonal typographic grid tension with vermilion arc sectors, stepped rules, and modernist structural cadence.",
        "tags": ["swiss", "typographic", "muller-brockmann", "tension", "matrix", "geometric"],
        "code": """// Swiss Typographic Geometric Tension Matrix - Infinite Library (Sketch 2D)
param("arcs", 6.0, 3.0, 10.0);
param("line_weight", 1.8, 0.8, 3.5);
param("accent_scale", 1.1, 0.5, 2.0);
param("contrast", 1.2, 0.8, 1.8);

function draw(t) {
  // Archival cream paper ground (#F5F5F2)
  background(0.961, 0.961, 0.949);

  const w = width;
  const h = height;
  const margin = 72;
  const innerW = w - margin * 2;
  const innerH = h - margin * 2;

  // 1. Framing graticule & registration marks
  stroke(0.067, 0.067, 0.067);
  strokeWeight(line_weight * 1.3);
  noFill();
  rect(margin, margin, innerW, innerH);

  strokeWeight(1.0);
  for (let x = margin + 36; x < w - margin; x += 36) {
    line(x, margin, x, margin + 7);
    line(x, h - margin, x, h - margin - 7);
  }
  for (let y = margin + 36; y < h - margin; y += 36) {
    line(margin, y, margin + 7, y);
    line(w - margin, y, w - margin - 7, y);
  }

  // 2. Muller-Brockmann concentric quadrant arcs radiating from lower-left focal datum
  const originX = margin + innerW * 0.25;
  const originY = h - margin - innerH * 0.25;
  const numArcs = floor(arcs);
  const maxR = innerW * 0.72;

  for (let i = 1; i <= numArcs; i++) {
    const frac = i / numArcs;
    const rad = frac * maxR;

    if (i % 2 === 0) {
      stroke(0.894, 0.149, 0.102);
      strokeWeight(line_weight * 2.4);
    } else {
      stroke(0.067, 0.067, 0.067);
      strokeWeight(line_weight * 1.2);
    }
    noFill();
    ellipse(originX, originY, rad * 2.0, rad * 2.0);
  }

  // 3. Orthogonal Structural Blocks & Stepped Rules
  noStroke();
  fill(0.894, 0.149, 0.102);
  rect(margin + innerW * 0.52, margin + innerH * 0.12, innerW * 0.38, innerH * 0.32);

  fill(0.067, 0.067, 0.067);
  rect(margin + innerW * 0.65, margin + innerH * 0.50, innerW * 0.25, innerH * 0.38);

  stroke(0.067, 0.067, 0.067);
  strokeWeight(line_weight * 1.5);
  const startY = margin + innerH * 0.18;
  for (let k = 0; k < 6; k++) {
    const yPos = startY + k * 28;
    line(margin + 40, yPos, margin + innerW * 0.44, yPos);
  }

  stroke(0.894, 0.149, 0.102);
  strokeWeight(line_weight * 2.0);
  line(originX, originY, originX + maxR * 0.75, originY - maxR * 0.75);

  fill(0.894, 0.149, 0.102);
  noStroke();
  ellipse(originX, originY, 18, 18);
  stroke(0.961, 0.961, 0.949);
  strokeWeight(2.0);
  line(originX - 14, originY, originX + 14, originY);
  line(originX, originY - 14, originX, originY + 14);
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: holographic_iridescent)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "holographic-iridescent-prismatic-polytope-3d",
        "title": "Holographic Iridescent Prismatic Polytope 3D",
        "description": "Faceted holographic crystal polytope with rotating iridescent orbital planes, dispersion refraction facets, and lustrous pastel rainbow metallic sheen.",
        "tags": ["holographic", "iridescent", "3d", "prismatic", "polytope", "crystal", "geometry"],
        "code": """// Holographic Iridescent Prismatic Polytope 3D - Infinite Library (Sketch 3D)
param("spin", 0.40, 0.1, 1.5);
param("scale_factor", 1.25, 0.5, 2.5);
param("facets", 8.0, 4.0, 16.0);
param("ring_density", 4.0, 1.0, 6.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.45 + sin(time * 0.22) * 0.08);
  rotateY(time * 0.38 + 0.5);

  // Central prismatic crystal polytope core (#ffffff)
  fill(0.96, 0.98, 1.0);
  box(0.58 * scale_factor, 0.58 * scale_factor, 0.58 * scale_factor);

  // Radial dispersion crystal facets projecting outward
  const numFacets = floor(facets);
  for (let i = 0; i < numFacets; i++) {
    const angle = (i / numFacets) * Math.PI * 2.0;
    push();
    rotateZ(angle);
    rotateY(i * 0.5 + time * 0.1);
    translate(0.68 * scale_factor, 0, 0);

    if (i % 3 === 0) {
      fill(0.663, 0.941, 1.0); // Soft cyan/pearl (#a9f0ff)
    } else if (i % 3 === 1) {
      fill(1.0, 0.702, 0.961); // Iridescent pink (#ffb3f5)
    } else {
      fill(0.839, 1.0, 0.620); // Mint green (#d6ff9e)
    }
    box(0.62 * scale_factor, 0.12 * scale_factor, 0.12 * scale_factor);
    pop();
  }

  // Interlocking orbital holographic rings
  const nRings = floor(ring_density);
  for (let r = 0; r < nRings; r++) {
    push();
    rotateX(0.8 * r + time * 0.18);
    rotateY(1.1 * r + time * 0.26);

    const rad = (0.98 + r * 0.32) * scale_factor;
    const segments = 24;
    for (let s = 0; s < segments; s++) {
      const a = (s / segments) * Math.PI * 2.0;
      const x = Math.cos(a) * rad;
      const y = Math.sin(a) * rad;

      push();
      translate(x, y, 0);
      rotateZ(a);
      if (r % 2 === 0) {
        fill(1.0, 0.702, 0.961); // Pink
      } else {
        fill(0.663, 0.941, 1.0); // Cyan
      }
      box(0.16 * scale_factor, 0.05 * scale_factor, 0.05 * scale_factor);
      pop();
    }
    pop();
  }

  pop();
}
"""
    },
    {
        "slug": "holographic-iridescent-faceted-torus-lattice-3d",
        "title": "Holographic Iridescent Faceted Torus Lattice 3D",
        "description": "Procedural 3D iridescent ribbon structure featuring interlocking crystalline torus rings, floating prismatic pearl satellite beads, and soft chromatic refraction planes.",
        "tags": ["holographic", "iridescent", "3d", "torus", "lattice", "ribbon", "prismatic"],
        "code": """// Holographic Iridescent Faceted Torus Lattice 3D - Infinite Library (Sketch 3D)
param("spin", 0.38, 0.1, 1.5);
param("scale_factor", 1.2, 0.5, 2.5);
param("torus_segments", 28.0, 16.0, 48.0);
param("satellites", 6.0, 3.0, 12.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.55 + sin(time * 0.18) * 0.06);
  rotateY(time * 0.34 + 0.4);

  const nSeg = floor(torus_segments);
  const majorR = 1.15 * scale_factor;

  for (let i = 0; i < nSeg; i++) {
    const a = (i / nSeg) * Math.PI * 2.0;
    const x = Math.cos(a) * majorR;
    const z = Math.sin(a) * majorR;
    const y = Math.sin(a * 3.0 + time * 0.5) * 0.18 * scale_factor;

    push();
    translate(x, y, z);
    rotateY(-a);
    rotateZ(time * 0.4);

    if (i % 3 === 0) {
      fill(0.663, 0.941, 1.0); // Cyan
    } else if (i % 3 === 1) {
      fill(1.0, 0.702, 0.961); // Pink
    } else {
      fill(0.839, 1.0, 0.620); // Mint
    }
    box(0.24 * scale_factor, 0.14 * scale_factor, 0.14 * scale_factor);
    pop();
  }

  for (let i = 0; i < nSeg; i++) {
    const a = (i / nSeg) * Math.PI * 2.0;
    const x = Math.cos(a) * (majorR * 0.75);
    const y = Math.sin(a) * (majorR * 0.75);
    const z = Math.cos(a * 2.0 + time * 0.4) * 0.15 * scale_factor;

    push();
    translate(x, y, z);
    rotateZ(-a);
    fill(0.96, 0.98, 1.0); // Prismatic white
    box(0.18 * scale_factor, 0.10 * scale_factor, 0.10 * scale_factor);
    pop();
  }

  const nSat = floor(satellites);
  for (let k = 0; k < nSat; k++) {
    const sa = (k / nSat) * Math.PI * 2.0 + time * 0.6;
    const sr = 1.55 * scale_factor;
    const sx = Math.cos(sa) * sr;
    const sy = Math.sin(sa * 2.0) * 0.45 * scale_factor;
    const sz = Math.sin(sa) * sr;

    push();
    translate(sx, sy, sz);
    fill(1.0, 0.702, 0.961); // Pink pearl
    box(0.22 * scale_factor, 0.22 * scale_factor, 0.22 * scale_factor);
    pop();
  }

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: neurofunk)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "neurofunk-modulated-reese-bass-synth",
        "title": "Neurofunk Modulated Reese Bass Synth",
        "description": "Menacing neurofunk reese bass synthesizer featuring dual detuned phase-beating sawtooth oscillators, a sweeping modulated notch-lowpass filter, and multi-stage diode overdrive.",
        "note_root": 36, # C1
        "params": {
            "cutoff": 850.0,
            "detune": 0.02,
            "drive": 2.4,
            "sub": 0.55,
            "decay": 0.38
        },
        "code": """param float cutoff = 850.0 [80.0, 4000.0];
param float detune = 0.02 [0.005, 0.06];
param float drive = 2.4 [1.0, 5.0];
param float sub = 0.55 [0.0, 1.0];
param float decay = 0.38 [0.1, 1.0];
state float p1 = 0;
state float p2 = 0;
state float env = 0;
state float lp1 = 0;
state float lp2 = 0;

target = if(gate > 0.5, 1.0, 0.0);
decayRate = clamp(1.0 / (sr * decay), 0.0001, 0.05);
env = if(target > env, env + (target - env) * 0.1, env - env * decayRate);

step1 = clamp(freq * (1.0 - detune), 20.0, 2000.0) / sr;
step2 = clamp(freq * (1.0 + detune), 20.0, 2000.0) / sr;
p1 = (p1 + step1) % 1.0;
p2 = (p2 + step2) % 1.0;

saw1 = 2.0 * p1 - 1.0;
saw2 = 2.0 * p2 - 1.0;
subWave = sin(3.14159265 * p1) * sub;

raw = (saw1 + saw2) * 0.45 + subWave * 0.45;
u = raw * drive;
sat = u / (1.0 + abs(u) * 0.35);

fCut = clamp((cutoff + env * 1400.0) / sr * 3.14, 0.01, 0.45);
lp1 = lp1 + fCut * (sat - lp1);
lp2 = lp2 + fCut * (lp1 - lp2);

out = lp2 * (0.35 + env * 0.65) * 0.28;
"""
    },
    {
        "slug": "neurofunk-cybernetic-growl-lead-synth",
        "title": "Neurofunk Cybernetic Growl Lead Synth",
        "description": "Aggressive cybernetic growl synthesizer engineered for techstep and neurofunk leads with formant-shifted resonant filtering, wavefolded harmonic drive, and snappy envelope modulation.",
        "note_root": 48, # C2
        "params": {
            "formant": 1200.0,
            "drive": 2.2,
            "decay": 0.35,
            "detune": 0.015
        },
        "code": """param float formant = 1200.0 [200.0, 4000.0];
param float drive = 2.2 [1.0, 5.0];
param float decay = 0.35 [0.1, 0.9];
param float detune = 0.015 [0.002, 0.05];
state float p1 = 0;
state float p2 = 0;
state float env = 0;
state float bp1 = 0;
state float bp2 = 0;

target = if(gate > 0.5, 1.0, 0.0);
decayRate = clamp(1.0 / (sr * decay), 0.0001, 0.05);
env = if(target > env, env + (target - env) * 0.12, env - env * decayRate);

step1 = clamp(freq * (1.0 - detune), 20.0, 3000.0) / sr;
step2 = clamp(freq * (1.0 + detune), 20.0, 3000.0) / sr;
p1 = (p1 + step1) % 1.0;
p2 = (p2 + step2) % 1.0;

saw1 = 2.0 * p1 - 1.0;
saw2 = 2.0 * p2 - 1.0;
raw = (saw1 + saw2) * 0.5;

u = raw * drive;
sat = u / (1.0 + abs(u) * 0.35);

fCut = clamp((formant + env * 1400.0) / sr * 3.14, 0.01, 0.45);
bp1 = bp1 + fCut * (sat - bp1);
bp2 = bp2 + fCut * (bp1 - bp2);

out = bp2 * (0.35 + env * 0.65) * 0.28;
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: french_filter_house)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "french-filter-house-pumping-disco-sweep-fx",
        "title": "French Filter House Pumping Disco Sweep FX",
        "description": "Classic French touch resonant disco lowpass sweep filter featuring rhythmic envelope ducking, warm transistor drive, and sweeping resonant cutoff modulation.",
        "params": {
            "cutoff": 2200.0,
            "resonance": 0.65,
            "pumpRate": 0.50,
            "pumpDepth": 0.65,
            "drive": 1.6,
            "mix": 0.70
        },
        "code": """param float cutoff = 2200.0 [200.0, 6000.0];
param float resonance = 0.65 [0.0, 0.92];
param float pumpRate = 0.50 [0.1, 2.0];
param float pumpDepth = 0.65 [0.0, 1.0];
param float drive = 1.6 [1.0, 4.0];
param float mix = 0.70 [0.0, 1.0];
state float lfo = 0;
state float lp1 = 0;
state float lp2 = 0;

lfo = (lfo + pumpRate * 0.002) % 1.0;
pump = (0.5 - 0.5 * cos(6.2831853 * lfo)) * pumpDepth;

u = in * drive;
sat = u / (1.0 + abs(u) * 0.25);

effCut = clamp(cutoff * (1.0 - pump * 0.75), 150.0, 8000.0);
fCut = clamp(effCut / sr * 3.14, 0.01, 0.45);

fb = lp2 * resonance * 1.4;
lp1 = lp1 + fCut * ((sat - fb) - lp1);
lp2 = lp2 + fCut * (lp1 - lp2);

processed = in * (1.0 - mix) + lp2 * mix * 1.15;
out = processed * 0.90;
"""
    },
    {
        "slug": "french-filter-house-creamy-phaser-drive-fx",
        "title": "French Filter House Creamy Phaser Drive FX",
        "description": "Silky multi-stage vintage analog phaser and warm console saturation effect designed for French house rhodes stabs, guitars, and filtered disco master buses.",
        "params": {
            "rate": 0.40,
            "depth": 0.60,
            "feedback": 0.45,
            "warmth": 0.70,
            "mix": 0.55
        },
        "code": """param float rate = 0.40 [0.05, 1.5];
param float depth = 0.60 [0.1, 0.95];
param float feedback = 0.45 [0.0, 0.85];
param float warmth = 0.70 [0.1, 1.0];
param float mix = 0.55 [0.0, 1.0];
state float lfo = 0;
state float ap1 = 0;
state float ap2 = 0;
state float ap3 = 0;
state float ap4 = 0;
state float fb = 0;

lfo = (lfo + rate * 0.0015) % 1.0;
mod = (0.5 + 0.5 * sin(6.2831853 * lfo)) * depth;

kRate = clamp(0.08 + mod * 0.18, 0.02, 0.35);

x = in + fb * feedback * 0.6;
ap1 = ap1 + kRate * (x - ap1);
ap2 = ap2 + kRate * (ap1 - ap2);
ap3 = ap3 + kRate * (ap2 - ap3);
ap4 = ap4 + kRate * (ap3 - ap4);

phaseSig = ap4;
u = phaseSig * (1.0 + warmth * 0.5);
fb = u / (1.0 + abs(u) * 0.3);

processed = in * (1.0 - mix) + fb * mix * 1.1;
out = processed * 0.90;
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embeds (Theme: cymatics_sine)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "cymatics-sine-chladni-plate-resonance",
        "title": "Cymatics Sine Chladni Plate Resonance",
        "description": "Mathematical simulation of 2D Chladni plate acoustic resonance where sand particles settle along nodal lines determined by harmonic vibrational modal frequencies.",
        "embed_desc": "Audio-reactive Chladni plate cymatics visualizer. Bass shifts modal standing-wave frequencies, mids warp geometric nodal lines, and highs vibrate granular sand dispersion.",
        "tags": ["cymatics", "chladni", "nodal", "resonance", "sine", "shader", "webgl", "audio-reactive"],
        "params": {
            "modeM": 5.0,
            "modeN": 3.0,
            "lineSharpness": 16.0,
            "speed": 0.35,
            "contrast": 1.25
        },
        "code": """param float modeM = 5.0 [1.0, 12.0];
param float modeN = 3.0 [1.0, 12.0];
param float lineSharpness = 16.0 [6.0, 36.0];
param float speed = 0.35 [0.05, 1.0];
param float contrast = 1.25 [0.8, 1.8];

p = (uv - 0.5) * 2.0;
time = t * speed;

m = modeM + sin(time * 0.4) * 0.5;
n = modeN + cos(time * 0.3) * 0.5;

pi = 3.14159265;
term1 = sin(n * pi * p.x) * sin(m * pi * p.y);
term2 = sin(m * pi * p.x) * sin(n * pi * p.y);
w = term1 - term2;

nodalDist = abs(w);
sandLine = 1.0 - clamp(nodalDist * lineSharpness, 0.0, 1.0);
sandGlow = 1.0 - clamp(nodalDist * (lineSharpness * 0.35), 0.0, 1.0);

cDark = vec3(0.03, 0.035, 0.07);
cCyan = vec3(0.490, 0.976, 1.0);
cWhite = vec3(0.96, 0.98, 1.0);

col = mix(cDark, cCyan, sandGlow * 0.85);
col = mix(col, cWhite, sandLine * 0.95);

plateDist = length(p);
plateEdge = 1.0 - smoothstep(0.92, 0.98, plateDist);
col = col * plateEdge;

out = clamp(col * contrast, 0.0, 1.0);
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
  vec2 p = (uv - 0.5) * 2.0;
  p.x *= aspect;
  float time = t * 0.35;

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  float modeM = 5.0 + sin(time * 0.4 + b * 2.0) * 1.2;
  float modeN = 3.0 + cos(time * 0.3 + m * 2.0) * 1.2;

  float pi = 3.14159265;
  vec2 wp = p * (1.0 + b * 0.15);
  float term1 = sin(modeN * pi * wp.x) * sin(modeM * pi * wp.y);
  float term2 = sin(modeM * pi * wp.x) * sin(modeN * pi * wp.y);
  float w = term1 - term2;

  float nodalDist = abs(w);
  float sandLine = 1.0 - clamp(nodalDist * (16.0 + h * 8.0), 0.0, 1.0);
  float sandGlow = 1.0 - clamp(nodalDist * 5.5, 0.0, 1.0);

  vec3 cDark = vec3(0.03, 0.035, 0.07);
  vec3 cCyan = vec3(0.490, 0.976, 1.0);
  vec3 cWhite = vec3(0.96, 0.98, 1.0);

  vec3 col = mix(cDark, cCyan, sandGlow * (0.85 + m * 0.3));
  col = mix(col, cWhite, sandLine * (0.95 + h * 0.4));

  float plateDist = length(p);
  float plateEdge = 1.0 - smoothstep(0.92, 0.98, plateDist);
  col = col * plateEdge;

  gl_FragColor = vec4(clamp(col * 1.25, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "cymatics-sine-harmonic-modal-interference",
        "title": "Cymatics Sine Harmonic Modal Interference",
        "description": "Radial Bessel modal acoustic interference pattern simulating circular diaphragm cymatic vibration, concentric standing waves, and luminescent nodal particle clustering.",
        "embed_desc": "Audio-reactive circular modal cymatics visualizer. Bass drives radial Bessel diaphragm oscillation, mids modulate harmonic interference sectors, and highs disperse resonant nodal ripples.",
        "tags": ["cymatics", "bessel", "harmonic", "modal", "interference", "audio-reactive", "shader", "webgl"],
        "params": {
            "rings": 7.0,
            "sectors": 8.0,
            "radialSharpness": 14.0,
            "speed": 0.30,
            "contrast": 1.25
        },
        "code": """param float rings = 7.0 [2.0, 16.0];
param float sectors = 8.0 [2.0, 16.0];
param float radialSharpness = 14.0 [5.0, 30.0];
param float speed = 0.30 [0.05, 1.0];
param float contrast = 1.25 [0.8, 1.8];

p = (uv - 0.5) * 2.0;
time = t * speed;

r = length(p);
theta = atan2(p.y, p.x);

rMod = sin(time * 0.35) * 0.4;
effRings = rings + rMod;

pi = 3.14159265;
radialWave = cos(effRings * pi * r);
angularWave = cos(sectors * theta + time * 0.2);
modalPattern = radialWave * angularWave;

nodal = abs(modalPattern);
line1 = 1.0 - clamp(nodal * radialSharpness, 0.0, 1.0);
glow = 1.0 - clamp(nodal * (radialSharpness * 0.3), 0.0, 1.0);

cDark = vec3(0.025, 0.03, 0.065);
cCyan = vec3(0.45, 0.95, 1.0);
cWhite = vec3(0.95, 0.98, 1.0);

col = mix(cDark, cCyan, glow * 0.82);
col = mix(col, cWhite, line1 * 0.95);

plateEdge = 1.0 - smoothstep(0.92, 0.98, r);
col = col * plateEdge;

out = clamp(col * contrast, 0.0, 1.0);
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
  vec2 p = (uv - 0.5) * 2.0;
  p.x *= aspect;
  float time = t * 0.30;

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  float r = length(p) * (1.0 - b * 0.12);
  float theta = atan(p.y, p.x);

  float effRings = 7.0 + sin(time * 0.35 + b * 1.8) * 1.2;
  float effSectors = 8.0 + floor(m * 3.0);

  float pi = 3.14159265;
  float radialWave = cos(effRings * pi * r);
  float angularWave = cos(effSectors * theta + time * 0.2 + m * 0.8);
  float modalPattern = radialWave * angularWave;

  float nodal = abs(modalPattern);
  float line1 = 1.0 - clamp(nodal * (14.0 + h * 6.0), 0.0, 1.0);
  float glow = 1.0 - clamp(nodal * 4.5, 0.0, 1.0);

  vec3 cDark = vec3(0.025, 0.03, 0.065);
  vec3 cCyan = vec3(0.45, 0.95, 1.0);
  vec3 cWhite = vec3(0.95, 0.98, 1.0);

  vec3 col = mix(cDark, cCyan, glow * (0.82 + m * 0.3));
  col = mix(col, cWhite, line1 * (0.95 + h * 0.4));

  float plateEdge = 1.0 - smoothstep(0.92, 0.98, length(p));
  col = col * plateEdge;

  gl_FragColor = vec4(clamp(col * 1.25, 0.0, 1.0), 1.0);
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
  <meta name="theme-color" content="#151930">
  <title>{item['title']} | Infinite</title>
  <meta name="description" content="{item['embed_desc']}">
  <link rel="canonical" href="https://infinitedevices.org/embeds/{item['slug']}.html">
  <meta property="og:type" content="website">
  <meta property="og:title" content="{item['title']}">
  <meta property="og:description" content="{item['embed_desc']}">
  <meta property="og:url" content="https://infinitedevices.org/embeds/{item['slug']}.html">
  <meta property="og:image" content="https://infinitedevices.org/assets/og-image.jpg">
  <meta name="twitter:card" content="summary_large_image">
  <style>
    :root {{ --ground: #151930; --glass: rgba(32, 38, 63, 0.78); --ink: #EEF1FA; --ember: #FF6B35; --signal: #A7B1FF; --shadow: rgba(4, 5, 14, 0.45); }}
    * {{ box-sizing: border-box; margin: 0; padding: 0; }}
    html, body {{ width: 100%; height: 100%; overflow: hidden; background: var(--ground); font-family: Geist, system-ui, -apple-system, sans-serif; }}
    canvas {{ width: 100%; height: 100%; display: block; }}
    .badge, .btn {{
      position: absolute; bottom: 14px; z-index: 10; display: inline-flex; align-items: center; gap: 8px; min-height: 40px; padding: 0 16px;
      border-radius: 999px; background: var(--glass); color: var(--ink); font-size: 11.5px; font-weight: 500; text-decoration: none;
      -webkit-backdrop-filter: blur(14px) saturate(1.5); backdrop-filter: blur(14px) saturate(1.5); box-shadow: 0 6px 16px var(--shadow);
      transition: transform 0.18s ease-out, background-color 0.18s ease-out;
    }}
    .badge {{ right: 14px; width: 44px; padding: 0; justify-content: center; }}
    .badge-icon {{ width: 16px; height: 16px; border-radius: 50%; background: linear-gradient(135deg, var(--ember), var(--signal)); }}
    .controls {{ position: absolute; left: 14px; bottom: 14px; z-index: 10; }}
    .controls .btn {{ position: static; border: 0; cursor: pointer; font-family: inherit; min-height: 44px; width: 44px; padding: 0; justify-content: center; }}
    .badge:hover, .btn:hover {{ transform: translateY(-2px); background: rgba(52, 59, 94, 0.9); }}
    .sr-only {{ position: absolute; width: 1px; height: 1px; overflow: hidden; clip: rect(0 0 0 0); white-space: nowrap; }}
    .fallback {{ color: var(--ink); padding: 24px; font-size: 13.5px; }}
  </style>
</head>
<body>
  <main>
    <h1 class="sr-only">{item['title']}</h1>
    <canvas id="gl" role="img" aria-label="{item['title']}, an audio-reactive visual"></canvas>

    <div class="controls">
      <button class="btn" id="btn-audio" type="button" aria-label="Enable microphone" title="Enable microphone">mic</button>
      <button class="btn" id="btn-share" type="button" aria-label="Share" title="Share">share</button>
    </div>

    <a href="https://infinitedevices.org/" target="_blank" rel="noopener" class="badge" title="Infinite">
      <span class="badge-icon" aria-hidden="true"></span>
      <span class="sr-only">made in infinite</span>
    </a>
  </main>

  <script>
    const canvas = document.getElementById("gl");
    const gl = canvas.getContext("webgl");
    if (!gl) {{ document.body.innerHTML = '<p class="fallback">WebGL required.</p>'; }}

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
    print("=== Creative Expansion Pack 39 Generator & Verification Gate Runner ===")
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
  c bg 0.075 0.067 0.11
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
  i note2 {nr}
  i note3 {nr+3}
  i note4 {nr}
  i note5 {nr}
  i note6 {nr+12}
  i note7 {nr}
  i note8 {nr}
  i note9 {nr}
  i note10 {nr+3}
  i note11 {nr}
  i note12 {nr}
  i note13 {nr+12}
  i note14 {nr+3}
  i note15 {nr}
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
  f resonance 0.28
  f decay 0.30
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

    print("\n=== Verification Summary ===")
    for line in report:
        print(f"  {line}")

    if failed or len(report) < 10:
        print(f"\nABORTING: Not all 10 items passed gates! (passed {len(report)}/10)")
        sys.exit(1)

    print("\nAll 10 items successfully passed engine verification gates!")

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
                "tags": ["synth", "neurofunk", "audio", "dsp", "reese", "bass"],
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
                "tags": ["effect", "french-filter-house", "audio", "dsp", "disco", "filter", "phaser"],
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
