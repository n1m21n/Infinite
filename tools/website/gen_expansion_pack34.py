#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 34.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: cymatics_sine -> cymatics-sine-chladni-plate-nodal-resonance, cymatics-sine-standing-wave-radial-ripples)
2. Sketch 3D (Theme: neo_brutalist -> neo-brutalist-cantilever-monolith-bastion-3d, neo-brutalist-stepped-colonnade-plinth-3d)
3. Field Synth (Theme: solo_piano -> solo-piano-acoustic-hammer-felt-synth, solo-piano-sympathetic-soundboard-resonance-synth)
4. Field Effects (Theme: dark_ambient -> dark-ambient-subterranean-drone-resonator-fx, dark-ambient-abyssal-space-diffusion-delay-fx)
5. Field Pixel (Theme: bauhaus_geometry -> bauhaus-geometry-constructivist-orbital-harmony, bauhaus-geometry-kinetic-quadrant-tessellation)
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

TMP_DIR = "/tmp/pack34_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: cymatics_sine)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "cymatics-sine-chladni-plate-nodal-resonance",
        "title": "Cymatics Sine Chladni Plate Nodal Resonance",
        "description": "Procedural Chladni acoustic pattern rendering two-dimensional standing wave nodal curves on a resonant brass plate with fine sand grain stippling and harmonic interference rings.",
        "tags": ["cymatics", "chladni", "nodal", "resonance", "sand", "drawing"],
        "code": """// Cymatics Sine Chladni Plate Nodal Resonance - Infinite Library (Sketch 2D)
param("m_mode", 3.0, 2.0, 7.0);
param("n_mode", 5.0, 2.0, 7.0);
param("grain_density", 0.65, 0.2, 1.0);
param("vibration", 0.30, 0.05, 0.8);

function draw(t) {
  // Deep acoustic resonance plate background (#0B0E14)
  background(0.043, 0.055, 0.078);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;
  const time = t * vibration;

  push();
  translate(cx, cy);

  // 1. Resonant Brass Plate Outer Boundary & Fastener Pins
  stroke(0.773, 0.627, 0.349); // Resonant Brass (#C5A059)
  strokeWeight(3.5);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  stroke(0.306, 0.804, 0.769, 0.65); // Interference Cyan (#4ECDC4)
  strokeWeight(1.5);
  rect(-span * 0.465, -span * 0.465, span * 0.93, span * 0.93);

  // Brass corner mounting bolts
  fill(0.773, 0.627, 0.349);
  noStroke();
  const boltR = span * 0.016;
  const boltOffset = span * 0.45;
  circle(-boltOffset, -boltOffset, boltR * 2.0);
  circle(boltOffset, -boltOffset, boltR * 2.0);
  circle(-boltOffset, boltOffset, boltR * 2.0);
  circle(boltOffset, boltOffset, boltR * 2.0);

  // 2. Harmonic Antinode Velocity Fields (Concentric Acoustic Ripples)
  stroke(0.306, 0.804, 0.769, 0.25);
  strokeWeight(1.2);
  noFill();
  for (let r = 0.08; r < 0.44; r += 0.035) {
    const wave = sin(r * 32.0 - time * 2.5) * 0.008;
    rect(-span * (r + wave), -span * (r + wave), span * (r + wave) * 2.0, span * (r + wave) * 2.0, 4.0);
  }

  // 3. Chladni 2D Mathematical Standing Wave Nodal Contours
  // Nodal equation: a*sin(n*pi*x)*sin(m*pi*y) + b*sin(m*pi*x)*sin(n*pi*y) = 0
  const m = floor(m_mode);
  const n = floor(n_mode);
  const aCoeff = 1.0;
  const bCoeff = -0.85 + sin(time) * 0.25;

  // Draw continuous nodal line contours
  stroke(0.957, 0.965, 0.973, 0.85); // Pure silica sand (#F4F6F8)
  strokeWeight(2.2);
  noFill();

  const gridSteps = 72;
  const stepSize = (span * 0.86) / gridSteps;
  const halfGrid = span * 0.43;

  for (let iy = 0; iy < gridSteps; iy++) {
    const yNorm = (iy / gridSteps) * 2.0 - 1.0;
    beginShape();
    let inContour = false;
    for (let ix = 0; ix <= gridSteps; ix++) {
      const xNorm = (ix / gridSteps) * 2.0 - 1.0;
      const xPix = -halfGrid + ix * stepSize;
      const yPix = -halfGrid + iy * stepSize;

      const chladni = aCoeff * sin(n * Math.PI * xNorm * 0.5) * sin(m * Math.PI * yNorm * 0.5)
                    + bCoeff * sin(m * Math.PI * xNorm * 0.5) * sin(n * Math.PI * yNorm * 0.5);

      if (abs(chladni) < 0.14) {
        vertex(xPix, yPix + chladni * 12.0);
        inContour = true;
      } else if (inContour) {
        endShape();
        beginShape();
        inContour = false;
      }
    }
    endShape();
  }

  // 4. Stippled Sand Grain Accumulations on Nodal Ridge Trajectories
  noStroke();
  fill(0.957, 0.965, 0.973, 0.90);
  const nGrains = floor(420 * grain_density);
  for (let i = 0; i < nGrains; i++) {
    const u = ((i * 137.5) % 360) * (Math.PI / 180.0);
    const rad = (0.05 + 0.38 * ((i * 0.618033) % 1.0)) * span;
    const gx = cos(u) * rad;
    const gy = sin(u) * rad;
    const gxNorm = gx / (span * 0.43);
    const gyNorm = gy / (span * 0.43);

    const val = aCoeff * sin(n * Math.PI * gxNorm * 0.5) * sin(m * Math.PI * gyNorm * 0.5)
              + bCoeff * sin(m * Math.PI * gxNorm * 0.5) * sin(n * Math.PI * gyNorm * 0.5);

    if (abs(val) < 0.22) {
      const gSize = 1.5 + ((i % 5) * 0.6);
      circle(gx, gy, gSize);
    }
  }

  // 5. Central Acoustic Transducer Exciter Core
  fill(0.773, 0.627, 0.349);
  circle(0, 0, span * 0.055);
  fill(0.306, 0.804, 0.769);
  circle(0, 0, span * 0.032);
  fill(0.043, 0.055, 0.078);
  circle(0, 0, span * 0.015);

  pop();
}
"""
    },
    {
        "slug": "cymatics-sine-standing-wave-radial-ripples",
        "title": "Cymatics Sine Standing Wave Radial Ripples",
        "description": "Mathematical acoustic standing wave visualization computing two-dimensional Bessel radial interference patterns, hyperbolic saddle nodes, and circular brass plate boundary rings.",
        "tags": ["cymatics", "standingwave", "bessel", "ripples", "brass", "drawing"],
        "code": """// Cymatics Sine Standing Wave Radial Ripples - Infinite Library (Sketch 2D)
param("radial_modes", 8.0, 4.0, 16.0);
param("angular_sectors", 6.0, 2.0, 12.0);
param("sand_concentration", 0.70, 0.2, 1.0);
param("drift", 0.25, 0.05, 0.7);

function draw(t) {
  // Deep obsidian/charcoal plate (#080B11)
  background(0.031, 0.043, 0.067);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;
  const time = t * drift;

  push();
  translate(cx, cy);

  // 1. Heavy Circular Resonator Brass Rim with Calibrated Degree Ticks
  const plateRadius = span * 0.46;
  stroke(0.773, 0.627, 0.349); // Brass
  strokeWeight(3.5);
  noFill();
  circle(0, 0, plateRadius * 2.0);

  stroke(0.306, 0.804, 0.769, 0.6); // Cyan
  strokeWeight(1.2);
  circle(0, 0, plateRadius * 1.94);

  // Calibrated degree tick marks around brass perimeter
  stroke(0.773, 0.627, 0.349, 0.85);
  strokeWeight(1.4);
  const nTicks = 72;
  for (let k = 0; k < nTicks; k++) {
    const angle = (k / nTicks) * Math.PI * 2.0;
    const tickLen = (k % 6 === 0) ? span * 0.024 : span * 0.012;
    const r1 = plateRadius * 0.97;
    const r2 = r1 - tickLen;
    line(cos(angle) * r1, sin(angle) * r1, cos(angle) * r2, sin(angle) * r2);
  }

  // 2. Bessel Radial Standing Wave Rings (J_m(k*r) * cos(m*theta))
  const nRings = floor(radial_modes);
  const mSectors = floor(angular_sectors);

  for (let r = 1; r <= nRings; r++) {
    const rNorm = r / nRings;
    const rad = rNorm * plateRadius * 0.88;

    // Resonant ring contour in silica sand
    stroke(0.957, 0.965, 0.973, 0.80);
    strokeWeight(1.8 + (1.0 - rNorm) * 1.2);
    noFill();

    beginShape();
    const pts = 90;
    for (let p = 0; p <= pts; p++) {
      const a = (p / pts) * Math.PI * 2.0;
      const mod = sin(a * mSectors + time * 1.5) * (span * 0.016 * (1.0 - rNorm * 0.5));
      const px = cos(a) * (rad + mod);
      const py = sin(a) * (rad + mod);
      vertex(px, py);
    }
    endShape(1);

    // Cyan interference sub-harmonics between main rings
    if (r < nRings) {
      stroke(0.306, 0.804, 0.769, 0.30);
      strokeWeight(0.9);
      circle(0, 0, rad * 1.08 * 2.0);
    }
  }

  // 3. Hyperbolic Saddle Radial Spokes (Zero-Velocity Nodal Diameters)
  stroke(0.773, 0.627, 0.349, 0.55);
  strokeWeight(1.4);
  for (let s = 0; s < mSectors; s++) {
    const sa = (s / mSectors) * Math.PI;
    const offset = sin(time + s) * 0.03;
    line(cos(sa + offset) * plateRadius * 0.88, sin(sa + offset) * plateRadius * 0.88,
        -cos(sa + offset) * plateRadius * 0.88, -sin(sa + offset) * plateRadius * 0.88);
  }

  // 4. Stippled Sand Particles Trapped in Low-Energy Radial Traps
  noStroke();
  fill(0.957, 0.965, 0.973, 0.92);
  const nGrains = floor(450 * sand_concentration);
  for (let i = 0; i < nGrains; i++) {
    const angle = ((i * 77.3) % 360) * (Math.PI / 180.0);
    const ringChoice = (i % nRings) + 1;
    const baseR = (ringChoice / nRings) * plateRadius * 0.88;
    const jitter = sin(i * 12.3) * (span * 0.012);
    const rDist = baseR + jitter;

    const gx = cos(angle) * rDist;
    const gy = sin(angle) * rDist;
    const gSize = 1.4 + ((i % 4) * 0.7);
    circle(gx, gy, gSize);
  }

  // 5. Central Brass Hub & Transducer Exciter
  fill(0.773, 0.627, 0.349);
  circle(0, 0, span * 0.065);
  fill(0.306, 0.804, 0.769);
  circle(0, 0, span * 0.038);
  fill(0.031, 0.043, 0.067);
  circle(0, 0, span * 0.018);

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: neo_brutalist)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "neo-brutalist-cantilever-monolith-bastion-3d",
        "title": "Neo-Brutalist Cantilever Monolith Bastion 3D",
        "description": "Monumental three-dimensional neo-brutalist architectural sculpture featuring heavy cast concrete stepped plinths, cantilevered horizontal monolith slabs, terracotta structural accent beams, and deep shadow relief.",
        "tags": ["neobrutalist", "monolith", "bastion", "cantilever", "concrete", "3d"],
        "code": """// Neo-Brutalist Cantilever Monolith Bastion 3D - Infinite Library (Sketch 3D)
param("spin", 0.30, 0.1, 1.0);
param("scale_factor", 1.20, 0.6, 2.0);
param("cantilever_depth", 1.35, 0.6, 2.2);
param("terrace_count", 6.0, 3.0, 9.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.48 + sin(time * 0.12) * 0.03);
  rotateY(time * 0.25 + 0.65);

  // 1. Massive Stepped Foundation Plinth (Deep Structural Concrete #22252A)
  push();
  translate(0, 1.25 * scale_factor, 0);
  fill(0.133, 0.145, 0.165);
  box(2.8 * scale_factor, 0.15 * scale_factor, 2.8 * scale_factor);

  translate(0, -0.12 * scale_factor, 0);
  fill(0.251, 0.267, 0.298);
  box(2.3 * scale_factor, 0.10 * scale_factor, 2.3 * scale_factor);

  translate(0, -0.09 * scale_factor, 0);
  fill(0.420, 0.443, 0.478);
  box(1.8 * scale_factor, 0.08 * scale_factor, 1.8 * scale_factor);
  pop();

  // 2. Central Monolithic Structural Spine (Board-Marked Concrete & Terracotta Reveal)
  push();
  translate(0, 0.18 * scale_factor, 0);
  fill(0.553, 0.576, 0.612); // Medium board-marked concrete
  box(0.65 * scale_factor, 2.2 * scale_factor, 0.55 * scale_factor);

  // Recessed vertical terracotta oxide structural shear wall (#C84B31)
  translate(0.36 * scale_factor, 0, 0);
  fill(0.784, 0.294, 0.192);
  box(0.12 * scale_factor, 2.3 * scale_factor, 0.45 * scale_factor);

  translate(-0.72 * scale_factor, 0, 0);
  fill(0.133, 0.145, 0.165);
  box(0.12 * scale_factor, 1.9 * scale_factor, 0.45 * scale_factor);
  pop();

  // 3. Cantilevered Horizontal Bastion Slabs (Asymmetric Stepped Terraces)
  const nSlabs = floor(terrace_count);
  for (let k = 0; k < nSlabs; k++) {
    const frac = k / nSlabs;
    const yPos = (0.85 - k * 0.38) * scale_factor;
    const shiftX = ((k % 2 === 0) ? 0.42 : -0.42) * cantilever_depth * scale_factor;
    const shiftZ = ((k % 3 === 0) ? 0.32 : -0.32) * scale_factor;
    const slabW = (1.6 - frac * 0.35) * scale_factor;
    const slabD = (1.05 - frac * 0.22) * scale_factor;

    push();
    translate(shiftX, yPos, shiftZ);
    rotateY(k * 0.28 - 0.15);

    // Alternating concrete tones and terracotta accent lintels
    if (k % 3 === 0) {
      fill(0.855, 0.871, 0.898); // Light cast concrete
    } else if (k % 3 === 1) {
      fill(0.420, 0.443, 0.478); // Medium concrete
    } else {
      fill(0.784, 0.294, 0.192); // Terracotta oxide
    }
    box(slabW, 0.16 * scale_factor, slabD);

    // Deep shadow reveal under slab
    push();
    translate(0, 0.09 * scale_factor, 0);
    fill(0.082, 0.090, 0.106);
    box(slabW * 0.90, 0.04 * scale_factor, slabD * 0.90);
    pop();

    pop();
  }

  // 4. Heavy Diagonal Tension Buttreses & Outer Pylon Columns
  push();
  translate(0.55 * scale_factor, -0.10 * scale_factor, 0.35 * scale_factor);
  rotateZ(0.44);
  fill(0.784, 0.294, 0.192); // Terracotta accent column
  box(0.14 * scale_factor, 1.8 * scale_factor, 0.14 * scale_factor);
  pop();

  push();
  translate(-0.55 * scale_factor, -0.05 * scale_factor, -0.30 * scale_factor);
  rotateZ(-0.40);
  fill(0.251, 0.267, 0.298);
  box(0.16 * scale_factor, 1.7 * scale_factor, 0.14 * scale_factor);
  pop();

  // 5. Crown Observation Deck & Stele Finial
  push();
  translate(0, -1.15 * scale_factor, 0);
  fill(0.855, 0.871, 0.898);
  box(0.80 * scale_factor, 0.14 * scale_factor, 0.80 * scale_factor);

  translate(0, -0.12 * scale_factor, 0);
  fill(0.784, 0.294, 0.192);
  box(0.50 * scale_factor, 0.10 * scale_factor, 0.50 * scale_factor);

  translate(0, -0.09 * scale_factor, 0);
  fill(0.133, 0.145, 0.165);
  box(0.24 * scale_factor, 0.08 * scale_factor, 0.24 * scale_factor);
  pop();

  pop();
}
"""
    },
    {
        "slug": "neo-brutalist-stepped-colonnade-plinth-3d",
        "title": "Neo-Brutalist Stepped Colonnade Plinth 3D",
        "description": "Architectural three-dimensional neo-brutalist colonnade sculpture with interlocking stepped geometric pylons, raw cast concrete plinths, and cantilevered terrace slabs.",
        "tags": ["neobrutalist", "colonnade", "plinth", "stepped", "concrete", "3d"],
        "code": """// Neo-Brutalist Stepped Colonnade Plinth 3D - Infinite Library (Sketch 3D)
param("spin", 0.35, 0.1, 1.2);
param("scale_factor", 1.20, 0.6, 2.0);
param("colonnade_span", 1.25, 0.6, 2.2);
param("tiers", 6.0, 3.0, 9.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.48 + sin(time * 0.12) * 0.03);
  rotateY(time * 0.28 + 0.62);

  // 1. Massive Stepped Base Foundation Plinths (#22252A to #DCE0E8)
  push();
  translate(0, 1.22 * scale_factor, 0);
  fill(0.122, 0.133, 0.153);
  box(2.8 * scale_factor, 0.14 * scale_factor, 2.8 * scale_factor);

  translate(0, -0.10 * scale_factor, 0);
  fill(0.243, 0.259, 0.290);
  box(2.3 * scale_factor, 0.10 * scale_factor, 2.3 * scale_factor);

  translate(0, -0.08 * scale_factor, 0);
  fill(0.420, 0.443, 0.478);
  box(1.8 * scale_factor, 0.08 * scale_factor, 1.8 * scale_factor);
  pop();

  // 2. Colonnade of Monolithic Pylons with Heavy Concrete Lintels
  const nPylons = 8;
  for (let i = 0; i < nPylons; i++) {
    const angle = (i / nPylons) * Math.PI * 2.0;
    const rDist = 0.95 * colonnade_span * scale_factor;
    const px = cos(angle) * rDist;
    const pz = sin(angle) * rDist;
    const pylonH = (1.5 + ((i % 3) * 0.35)) * scale_factor;

    push();
    translate(px, 0.30 * scale_factor, pz);

    if (i % 2 === 0) {
      fill(0.855, 0.871, 0.898); // Light concrete pylon
    } else {
      fill(0.784, 0.294, 0.192); // Terracotta accent pylon
    }
    box(0.24 * scale_factor, pylonH, 0.24 * scale_factor);

    // Overhanging bracket block on top
    translate(0, -pylonH * 0.5 - 0.05 * scale_factor, 0);
    fill(0.122, 0.133, 0.153);
    box(0.36 * scale_factor, 0.10 * scale_factor, 0.36 * scale_factor);
    pop();
  }

  // 3. Central Monolithic Bastion Core
  push();
  translate(0, 0.12 * scale_factor, 0);
  fill(0.420, 0.443, 0.478);
  box(0.85 * scale_factor, 2.1 * scale_factor, 0.85 * scale_factor);

  // Terracotta vertical shear wall
  translate(0, 0, 0.44 * scale_factor);
  fill(0.784, 0.294, 0.192);
  box(0.40 * scale_factor, 2.2 * scale_factor, 0.10 * scale_factor);
  pop();

  // 4. Interlocking Cantilevered Terrace Decks & Perimeter Lintels
  const nTiers = floor(tiers);
  for (let k = 0; k < nTiers; k++) {
    const frac = k / nTiers;
    const yPos = (0.80 - k * 0.36) * scale_factor;
    const rot = k * 0.45;
    const deckW = (1.8 - frac * 0.35) * scale_factor;
    const deckD = (0.65 - frac * 0.15) * scale_factor;

    push();
    translate(0, yPos, 0);
    rotateY(rot);

    if (k % 2 === 0) {
      fill(0.855, 0.871, 0.898); // Crisp pale concrete
    } else {
      fill(0.784, 0.294, 0.192); // Terracotta lintel
    }
    box(deckW, 0.12 * scale_factor, deckD);

    // Shadow bracket underneath
    translate(0, 0.08 * scale_factor, 0);
    fill(0.122, 0.133, 0.153);
    box(deckW * 0.88, 0.04 * scale_factor, deckD * 0.88);
    pop();
  }

  // 5. Diagonal Structural Flying Buttresses
  push();
  translate(0.65 * scale_factor, -0.10 * scale_factor, 0.45 * scale_factor);
  rotateZ(0.46);
  fill(0.784, 0.294, 0.192);
  box(0.14 * scale_factor, 1.9 * scale_factor, 0.14 * scale_factor);
  pop();

  push();
  translate(-0.65 * scale_factor, -0.05 * scale_factor, -0.40 * scale_factor);
  rotateZ(-0.42);
  fill(0.855, 0.871, 0.898);
  box(0.16 * scale_factor, 1.8 * scale_factor, 0.14 * scale_factor);
  pop();

  // 6. Crown Observation Stele & Canopy
  push();
  translate(0, -1.18 * scale_factor, 0);
  fill(0.855, 0.871, 0.898);
  box(1.05 * scale_factor, 0.12 * scale_factor, 1.05 * scale_factor);

  translate(0, -0.10 * scale_factor, 0);
  fill(0.784, 0.294, 0.192);
  box(0.60 * scale_factor, 0.08 * scale_factor, 0.60 * scale_factor);

  translate(0, -0.08 * scale_factor, 0);
  fill(0.122, 0.133, 0.153);
  box(0.28 * scale_factor, 0.08 * scale_factor, 0.28 * scale_factor);
  pop();

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: solo_piano)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "solo-piano-acoustic-hammer-felt-synth",
        "title": "Solo Piano Acoustic Hammer Felt Synth",
        "description": "Intimate solo acoustic felt piano synthesizer with dynamic hammer strike velocity, dual detuned acoustic string harmonics, warm soundboard wooden resonance, and delicate felt damping decay.",
        "tags": ["synth", "piano", "felt", "acoustic", "hammer", "audio"],
        "note_root": 60,
        "params": {
            "cutoff": 0.52,
            "resonance": 0.28,
            "hammer": 0.42,
            "decayTime": 0.65,
            "feltDamp": 0.48
        },
        "code": """param float cutoff = 0.52 [0.1, 1.0]
param float resonance = 0.28 [0.1, 0.85]
param float hammer = 0.42 [0.0, 1.0]
param float decayTime = 0.65 [0.1, 2.0]
param float feltDamp = 0.48 [0.0, 1.0]
state float p1 = 0
state float p2 = 0
state float env = 0
state float strikeEnv = 0
state float s1 = 0
state float s2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decayTime), 0.0001, 0.05)
env = if(target > env, env + (target - env) * 0.95, env - env * decayRate)

strikeRate = clamp(1.0 / (sr * 0.012), 0.001, 0.2)
strikeEnv = if(target > strikeEnv, strikeEnv + (target - strikeEnv) * 0.98, strikeEnv - strikeEnv * strikeRate)

dp = freq / sr
p1 = (p1 + dp) % 1.0
tri = abs(p1 * 2.0 - 1.0) * 2.0 - 1.0

p2 = (p2 + dp * 2.003) % 1.0
h2 = (p2 * 2.0 - 1.0) * 0.30 * hammer

hammerImpulse = strikeEnv * hammer * 0.6
sig = (tri * 0.72 + h2 * 0.38 + hammerImpulse * tri) * 1.15

fNorm = clamp(cutoff * 0.17 + env * (1.0 - feltDamp * 0.55) * 0.24, 0.015, 0.44)
kRes = clamp(resonance * 1.45, 0.1, 1.75)

s1 = s1 + fNorm * (sig - s2 * kRes - s1)
s2 = s2 + fNorm * (s1 - s2)

sat = (s2 / (1.0 + abs(s2 * 0.35))) * env
out = sat * 0.54
"""
    },
    {
        "slug": "solo-piano-sympathetic-soundboard-resonance-synth",
        "title": "Solo Piano Sympathetic Soundboard Resonance Synth",
        "description": "Expressive acoustic solo piano synthesizer featuring sympathetic string resonance modeling, harmonic damper release envelope, warm wooden soundboard acoustic body filtering, and natural dynamic strike response.",
        "tags": ["synth", "piano", "soundboard", "resonance", "sympathetic", "audio"],
        "note_root": 55,
        "params": {
            "cutoff": 0.56,
            "resonance": 0.34,
            "soundboard": 0.50,
            "decayTime": 0.70,
            "harmonics": 0.38
        },
        "code": """param float cutoff = 0.56 [0.1, 1.0]
param float resonance = 0.34 [0.1, 0.90]
param float soundboard = 0.50 [0.0, 1.0]
param float decayTime = 0.70 [0.1, 2.2]
param float harmonics = 0.38 [0.0, 1.0]
state float p1 = 0
state float p2 = 0
state float p3 = 0
state float env = 0
state float s1 = 0
state float s2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decayTime), 0.0001, 0.05)
env = if(target > env, env + (target - env) * 0.96, env - env * decayRate)

dp = freq / sr
p1 = (p1 + dp) % 1.0
tri = abs(p1 * 2.0 - 1.0) * 2.0 - 1.0

p2 = (p2 + dp * 1.002) % 1.0
subTri = abs(p2 * 2.0 - 1.0) * 2.0 - 1.0

p3 = (p3 + dp * 3.004) % 1.0
symp = (p3 * 2.0 - 1.0) * 0.25 * harmonics

sig = (tri * 0.60 + subTri * 0.45 + symp * 0.35) * (1.0 + soundboard * 0.5)

fNorm = clamp(cutoff * 0.16 + env * 0.25, 0.018, 0.44)
kRes = clamp(resonance * 1.5, 0.1, 1.8)

s1 = s1 + fNorm * (sig - s2 * kRes - s1)
s2 = s2 + fNorm * (s1 - s2)

sat = (s2 / (1.0 + abs(s2 * 0.38))) * env
out = sat * 0.52
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: dark_ambient)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "dark-ambient-subterranean-drone-resonator-fx",
        "title": "Dark Ambient Subterranean Drone Resonator FX",
        "description": "Subterranean dark ambient drone resonator with dual tuned low-frequency feedback combs, cavernous body resonance, and warm analog saturation.",
        "tags": ["effect", "ambient", "drone", "resonator", "subterranean", "audio"],
        "params": {
            "depth": 0.60,
            "tone": 0.44,
            "feedback": 0.52,
            "subGain": 0.45,
            "mix": 0.55
        },
        "code": """param float depth = 0.60 [0.1, 1.0]
param float tone = 0.44 [0.1, 1.0]
param float feedback = 0.52 [0.0, 0.88]
param float subGain = 0.45 [0.0, 1.0]
param float mix = 0.55 [0.0, 1.0]
state float d1 = 0
state float d2 = 0
state float d3 = 0
state float lp = 0

fTone = clamp(0.04 + tone * 0.30, 0.015, 0.40)
kRate = clamp(0.08 * (1.0 + depth * 0.75), 0.02, 0.32)

inSat = in / (1.0 + abs(in * 0.30))
fbIn = inSat + d3 * feedback * 0.80

lp = lp + fTone * (fbIn - lp)

d1 = d1 + kRate * (lp - d1)
d2 = d2 + kRate * 0.85 * (d1 - d2)
d3 = d3 + kRate * 0.75 * (d2 - d3)

subDrone = (d2 * 0.65 + d3 * 0.55) * (1.0 + subGain * 0.7)
sat = subDrone / (1.0 + abs(subDrone * 0.28))
processed = in * (1.0 - mix) + sat * mix
out = processed * 0.70
"""
    },
    {
        "slug": "dark-ambient-abyssal-space-diffusion-delay-fx",
        "title": "Dark Ambient Abyssal Space Diffusion Delay FX",
        "description": "Abyssal dark ambient space diffusion reverberator featuring multi-stage allpass delay network, dark spectral air damping, and decaying cavernous reflections.",
        "tags": ["effect", "ambient", "reverb", "delay", "diffusion", "audio"],
        "params": {
            "decay": 0.58,
            "tone": 0.42,
            "size": 0.65,
            "diffusion": 0.50,
            "mix": 0.55
        },
        "code": """param float decay = 0.58 [0.1, 0.92]
param float tone = 0.42 [0.1, 1.0]
param float size = 0.65 [0.1, 1.0]
param float diffusion = 0.50 [0.0, 1.0]
param float mix = 0.55 [0.0, 1.0]
state float a1 = 0
state float a2 = 0
state float a3 = 0
state float a4 = 0
state float lp = 0

fTone = clamp(0.04 + tone * 0.32, 0.015, 0.42)
lp = lp + fTone * (in - lp)

kDiff = clamp(0.10 * (1.0 + size * 0.85), 0.03, 0.36)
kDecay = clamp(decay * 0.84, 0.1, 0.88)

a1 = a1 + kDiff * (lp + a4 * kDecay - a1)
a2 = a2 + kDiff * 0.82 * (a1 - a2)
a3 = a3 + kDiff * 0.70 * (a2 - a3)
a4 = a4 + kDiff * 0.60 * (a3 - a4)

diffSig = (a2 * 0.55 + a3 * 0.45 + a4 * 0.75) * (1.0 + diffusion * 0.5)
sat = diffSig / (1.0 + abs(diffSig * 0.28))
processed = in * (1.0 - mix) + sat * mix
out = processed * 0.72
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel Sources & Embeds (Theme: bauhaus_geometry)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "bauhaus-geometry-constructivist-orbital-harmony",
        "title": "Bauhaus Geometry Constructivist Orbital Harmony",
        "description": "Audio-reactive Bauhaus constructivist shader featuring rotating primary color quadrant disks, intersecting diagonal geometric axes, and concentric orbital ring harmonies pulsing to bass, mid, and high frequencies.",
        "embed_desc": "Audio-reactive Bauhaus constructivist visualizer featuring kinetic primary color quadrant disks, intersecting constructivist diagonal axes, and concentric orbital rings. Bass pulses the central crimson disk and geometric quadrant scale, mids rotate interlocking navy circular segments, and highs trigger cadmium yellow diagonal flares.",
        "tags": ["pixel", "bauhaus", "constructivist", "geometry", "orbital", "shader"],
        "params": {
            "speed": 0.28,
            "orbitScale": 0.55,
            "ringCount": 14.0,
            "geometryWarp": 0.40,
            "contrast": 1.15
        },
        "code": """param float speed = 0.28 [0.05, 1.0]
param float orbitScale = 0.55 [0.2, 1.0]
param float ringCount = 14.0 [6.0, 28.0]
param float geometryWarp = 0.40 [0.0, 1.0]
param float contrast = 1.15 [0.8, 1.6]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2
time = t * speed

r = length(p)
theta = atan2(p.y, p.x)

rot = theta + time * 0.6
rotP = vec2(r * cos(rot), r * sin(rot))

quadrant = step(0.0, rotP.x) * 2.0 + step(0.0, rotP.y)

warp = sin(theta * 4.0 + time * 1.5) * cos(r * 8.0 - time) * geometryWarp * 0.18
rings = sin((r + warp) * ringCount * 2.2)
ringLines = smoothstep(0.25, -0.25, abs(rings) - 0.26)

centerDisc = smoothstep(orbitScale * 0.45, orbitScale * 0.45 - 0.03, r)

diagAxis = abs(p.x + p.y * 0.6)
diagLine = smoothstep(0.035, 0.015, diagAxis) * step(r, 0.95)

diagAxis2 = abs(p.x - p.y * 1.2)
diagLine2 = smoothstep(0.025, 0.010, diagAxis2) * step(r, 0.85)

outerCircle = smoothstep(orbitScale * 1.35, orbitScale * 1.35 - 0.03, r) * (1.0 - smoothstep(orbitScale * 1.30, orbitScale * 1.30 - 0.03, r))

cCream = vec3(0.957, 0.937, 0.886)
cRed = vec3(0.839, 0.157, 0.157)
cNavy = vec3(0.114, 0.208, 0.341)
cYellow = vec3(0.988, 0.749, 0.286)
cBlack = vec3(0.067, 0.067, 0.067)

quadColor = mix(cRed, cNavy, step(1.5, quadrant))
quadColor = mix(quadColor, cYellow, step(2.5, quadrant))

col = mix(cCream, quadColor, ringLines * 0.85)
col = mix(col, cNavy, outerCircle * 0.9)
col = mix(col, cBlack, diagLine)
col = mix(col, cYellow, diagLine2 * 0.8)
col = mix(col, cRed, centerDisc)

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
  float time = t * 0.28;

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  float r = length(p);
  float theta = atan(p.y, p.x);

  float rot = theta + time * 0.6 + m * 0.4;
  vec2 rotP = vec2(r * cos(rot), r * sin(rot));

  float quadrant = step(0.0, rotP.x) * 2.0 + step(0.0, rotP.y);

  float warp = sin(theta * 4.0 + time * 1.5) * cos(r * 8.0 - time) * (0.40 + b * 0.25) * 0.18;
  float rings = sin((r + warp) * (14.0 + m * 5.0) * 2.2);
  float ringLines = smoothstep(0.25, -0.25, abs(rings) - (0.26 + b * 0.08));

  float centerScale = (0.55 * 0.45) * (1.0 + b * 0.35);
  float centerDisc = smoothstep(centerScale, centerScale - 0.03, r);

  float diagAxis = abs(p.x + p.y * 0.6);
  float diagLine = smoothstep(0.035 + h * 0.015, 0.015, diagAxis) * step(r, 0.95 + b * 0.1);

  float diagAxis2 = abs(p.x - p.y * 1.2);
  float diagLine2 = smoothstep(0.025 + h * 0.02, 0.010, diagAxis2) * step(r, 0.85);

  float outerR = 0.55 * 1.35 + b * 0.08;
  float outerCircle = smoothstep(outerR, outerR - 0.03, r) * (1.0 - smoothstep(outerR - 0.05, outerR - 0.08, r));

  vec3 cCream = vec3(0.957, 0.937, 0.886);
  vec3 cRed = vec3(0.839, 0.157, 0.157);
  vec3 cNavy = vec3(0.114, 0.208, 0.341);
  vec3 cYellow = vec3(0.988, 0.749, 0.286);
  vec3 cBlack = vec3(0.067, 0.067, 0.067);

  vec3 quadColor = mix(cRed, cNavy, step(1.5, quadrant));
  quadColor = mix(quadColor, cYellow, step(2.5, quadrant));

  vec3 col = mix(cCream, quadColor, ringLines * 0.85);
  col = mix(col, cNavy, outerCircle * 0.9);
  col = mix(col, cBlack, diagLine);
  col = mix(col, cYellow, diagLine2 * 0.8);
  col = mix(col, cRed, centerDisc);

  gl_FragColor = vec4(clamp(col * 1.15, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "bauhaus-geometry-kinetic-quadrant-tessellation",
        "title": "Bauhaus Geometry Kinetic Quadrant Tessellation",
        "description": "Audio-reactive Bauhaus geometric tessellation shader with rhythmic primary color concentric arcs, kinetic quadrant dividers, and constructivist diagonal bars reacting to bass impact, mid-range harmonics, and high-frequency flares.",
        "embed_desc": "Audio-reactive Bauhaus geometric tessellation visualizer featuring rhythmic primary color concentric arcs, kinetic quadrant dividers, and constructivist diagonal bars. Bass modulates the central geometric core and quadrant expansion, mids drive concentric circular arc rotation, and highs activate crisp constructivist cross-hatch stippling.",
        "tags": ["pixel", "bauhaus", "tessellation", "quadrant", "kinetic", "shader"],
        "params": {
            "speed": 0.26,
            "arcDensity": 16.0,
            "division": 4.0,
            "lineWeight": 0.35,
            "contrast": 1.15
        },
        "code": """param float speed = 0.26 [0.05, 1.0]
param float arcDensity = 16.0 [8.0, 32.0]
param float division = 4.0 [2.0, 8.0]
param float lineWeight = 0.35 [0.1, 0.8]
param float contrast = 1.15 [0.8, 1.6]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2
time = t * speed

r = length(p)
theta = atan2(p.y, p.x)

rot = theta + sin(time * 0.8) * 0.3
sec = floor((rot + 3.14159) / (6.28318 / division))

arcs = sin(r * arcDensity * 2.0 + sec * 1.2 - time * 2.0)
arcLines = smoothstep(lineWeight * 0.5, -lineWeight * 0.5, abs(arcs) - 0.3)

rectFrame = max(abs(p.x), abs(p.y))
frameBorder = smoothstep(0.85, 0.82, rectFrame) * (1.0 - smoothstep(0.82, 0.79, rectFrame))

subDisc = smoothstep(0.35, 0.32, r)
innerDisc = smoothstep(0.18, 0.15, r)

crossHatch = sin(p.x * 28.0 + p.y * 14.0) * cos(p.y * 28.0 - p.x * 14.0)
hatchMask = step(0.65, crossHatch) * step(r, 0.75) * (1.0 - subDisc)

cCream = vec3(0.957, 0.937, 0.886)
cRed = vec3(0.839, 0.157, 0.157)
cNavy = vec3(0.114, 0.208, 0.341)
cYellow = vec3(0.988, 0.749, 0.286)
cBlack = vec3(0.067, 0.067, 0.067)

secCol = mix(cRed, cNavy, step(1.0, mod(sec, 3.0)))
secCol = mix(secCol, cYellow, step(2.0, mod(sec, 3.0)))

col = mix(cCream, secCol, arcLines * 0.80)
col = mix(col, cNavy, hatchMask * 0.6)
col = mix(col, cBlack, frameBorder)
col = mix(col, cRed, subDisc)
col = mix(col, cYellow, innerDisc)

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
  float time = t * 0.26;

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  float r = length(p);
  float theta = atan(p.y, p.x);

  float rot = theta + sin(time * 0.8 + m * 0.5) * 0.3;
  float sec = floor((rot + 3.14159) / (6.28318 / 4.0));

  float arcs = sin(r * (16.0 + m * 6.0) * 2.0 + sec * 1.2 - time * 2.0);
  float arcLines = smoothstep((0.35 + b * 0.1) * 0.5, -(0.35 + b * 0.1) * 0.5, abs(arcs) - 0.3);

  float rectFrame = max(abs(p.x), abs(p.y));
  float frameBorder = smoothstep(0.85 + b * 0.05, 0.82, rectFrame) * (1.0 - smoothstep(0.82, 0.79, rectFrame));

  float subScale = 0.35 + b * 0.15;
  float subDisc = smoothstep(subScale, subScale - 0.03, r);
  float innerScale = 0.18 + b * 0.08;
  float innerDisc = smoothstep(innerScale, innerScale - 0.03, r);

  float crossHatch = sin(p.x * 28.0 + p.y * 14.0) * cos(p.y * 28.0 - p.x * 14.0);
  float hatchMask = step(0.65 - h * 0.15, crossHatch) * step(r, 0.75) * (1.0 - subDisc);

  vec3 cCream = vec3(0.957, 0.937, 0.886);
  vec3 cRed = vec3(0.839, 0.157, 0.157);
  vec3 cNavy = vec3(0.114, 0.208, 0.341);
  vec3 cYellow = vec3(0.988, 0.749, 0.286);
  vec3 cBlack = vec3(0.067, 0.067, 0.067);

  vec3 secCol = mix(cRed, cNavy, step(1.0, mod(sec, 3.0)));
  secCol = mix(secCol, cYellow, step(2.0, mod(sec, 3.0)));

  vec3 col = mix(cCream, secCol, arcLines * 0.80);
  col = mix(col, cNavy, hatchMask * 0.6);
  col = mix(col, cBlack, frameBorder);
  col = mix(col, cRed, subDisc);
  col = mix(col, cYellow, innerDisc);

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
      background: linear-gradient(135deg, #d96b43, #3c6e71);
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
    print("=== Creative Expansion Pack 34 Generator & Verification Gate Runner ===")
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
  f rateBeats 0.5
  i steps 16
  i note0 {nr}
  i note1 {nr}
  i note2 {nr+7}
  i note3 {nr+4}
  i note4 {nr}
  i note5 {nr+7}
  i note6 {nr+5}
  i note7 {nr+7}
  i note8 {nr-3}
  i note9 {nr-3}
  i note10 {nr+4}
  i note11 {nr}
  i note12 {nr-5}
  i note13 {nr-1}
  i note14 {nr+2}
  i note15 {nr+4}
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
