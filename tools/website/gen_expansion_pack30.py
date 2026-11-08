#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 30.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (minimal-signal-oscilloscope-vector, minimal-signal-precision-graticule)
2. Sketch 3D (lilliputian-surreal-celestial-monolith-3d, lilliputian-surreal-orbital-observatory-3d)
3. Field Synth (acid-house-303-saw-squelch-synth, acid-house-accented-square-bass-synth)
4. Field Effects (future-garage-dusk-tape-delay-fx, future-garage-vinyl-dust-reverb-fx)
5. Field Pixel (indie-psychedelic-liquid-oil-wheel, indie-psychedelic-kaleido-prism-flower)
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

TMP_DIR = "/tmp/pack30_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: minimal_signal)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "minimal-signal-oscilloscope-vector",
        "title": "Minimal Signal Oscilloscope Vector",
        "description": "Ultra-precise monochrome signal visualization featuring calibrated raster lines, sweeping sinusoidal waveform harmonics, orthogonal graticule rules, and an amber strobe trigger pip.",
        "tags": ["minimal", "signal", "oscilloscope", "vector", "waveform", "drawing"],
        "code": """// Minimal Signal Oscilloscope Vector - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// High-precision vector oscilloscope waveform visualization with calibrated coordinate graticule, harmonic frequency traces, and amber trigger strobes.
param("frequency", 2.4, 0.5, 8.0);
param("harmonics", 3.0, 1.0, 6.0);
param("drift", 0.45, 0.1, 1.5);
param("sweep_gain", 1.1, 0.5, 2.0);

function draw(t) {
  background(0.06, 0.07, 0.09);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.86;
  const time = t * drift;

  push();
  translate(cx, cy);

  // 1. Oscilloscope graticule border & grid
  stroke(0.18, 0.22, 0.28);
  strokeWeight(2.0);
  noFill();
  rect(-span * 0.5, -span * 0.5, span, span);

  // 10x10 graticule grid subdivisions
  stroke(0.14, 0.18, 0.24);
  strokeWeight(1.2);
  const divs = 10;
  for (let i = 1; i < divs; i++) {
    const x = -span * 0.5 + (i / divs) * span;
    line(x, -span * 0.5, x, span * 0.5);
    line(-span * 0.5, x, span * 0.5, x);
  }

  // Major axis crosshairs with subdivision ticks
  stroke(0.28, 0.35, 0.45);
  strokeWeight(2.0);
  line(-span * 0.5, 0, span * 0.5, 0);
  line(0, -span * 0.5, 0, span * 0.5);

  const ticks = 50;
  for (let k = 0; k <= ticks; k++) {
    const p = -span * 0.5 + (k / ticks) * span;
    const len = (k % 5 === 0) ? span * 0.020 : span * 0.010;
    line(p, -len, p, len);
    line(-len, p, len, p);
  }

  // 2. Harmonic Lissajous reference orbit
  noFill();
  stroke(0.24, 0.32, 0.42);
  strokeWeight(1.8);
  const ringPts = 72;
  beginShape();
  for (let i = 0; i <= ringPts; i++) {
    const a = (i / ringPts) * Math.PI * 2.0;
    const r = span * 0.38 + sin(a * 4.0 + time * 1.5) * span * 0.03;
    vertex(cos(a) * r, sin(a) * r);
  }
  endShape();

  // 3. FFT spectrum bar readout across bottom
  const bars = 24;
  const barW = (span * 0.88) / bars;
  noStroke();
  for (let b = 0; b < bars; b++) {
    const frac = b / bars;
    const h = (sin(frac * 6.28 + time * 2.0) * 0.5 + 0.5) * (span * 0.12) + span * 0.02;
    fill(0.18, 0.26, 0.36);
    rect(-span * 0.44 + b * barW, span * 0.46 - h, barW * 0.75, h);
    fill(1.0, 0.72, 0.05);
    rect(-span * 0.44 + b * barW, span * 0.46 - h - span * 0.008, barW * 0.75, span * 0.006);
  }

  // 4. Primary waveform traces (Dual beam: CH1 White, CH2 Amber)
  const steps = 160;
  const nHarm = floor(harmonics);

  // CH2 Reference Waveform (Amber)
  stroke(1.0, 0.65, 0.05, 0.90);
  strokeWeight(2.8);
  noFill();
  beginShape();
  for (let i = 0; i <= steps; i++) {
    const frac = i / steps;
    const x = -span * 0.48 + frac * span * 0.96;
    const a = (frac * Math.PI * 2.0 * frequency * 0.5) - time * 1.8;
    const y = sin(a) * (span * 0.08 * sweep_gain) + span * 0.10;
    vertex(x, y);
  }
  endShape();

  // CH1 Main Signal Trace (Phosphor Cyan/White)
  stroke(0.96, 0.98, 1.0);
  strokeWeight(3.2);
  beginShape();
  for (let i = 0; i <= steps; i++) {
    const frac = i / steps;
    const x = -span * 0.48 + frac * span * 0.96;
    let y = 0;
    for (let h = 1; h <= nHarm; h++) {
      const a = (frac * Math.PI * 2.0 * frequency * h) - time * (2.0 + h * 0.5);
      y += (sin(a) / (h * 0.85)) * (span * 0.14 * sweep_gain);
    }
    vertex(x, y - span * 0.08);
  }
  endShape();

  // 5. Trigger marker and channel status badges
  const trigX = span * 0.28;
  stroke(1.0, 0.72, 0.05);
  strokeWeight(2.0);
  line(trigX - span * 0.03, -span * 0.08, trigX + span * 0.03, -span * 0.08);
  line(trigX, -span * 0.08 - span * 0.03, trigX, -span * 0.08 + span * 0.03);

  fill(1.0, 0.72, 0.05);
  noStroke();
  circle(trigX, -span * 0.08, span * 0.022);

  // Calibration corner boxes
  fill(0.96, 0.98, 1.0);
  rect(-span * 0.46, -span * 0.46, span * 0.08, span * 0.02);
  fill(1.0, 0.72, 0.05);
  rect(-span * 0.36, -span * 0.46, span * 0.04, span * 0.02);

  pop();
}
"""
    },
    {
        "slug": "minimal-signal-precision-graticule",
        "title": "Minimal Signal Precision Graticule",
        "description": "High-density technical vector graticule with calibrated sub-division scales, concentric radar sweep rings, binary telemetry data columns, and high-contrast signal crosshairs.",
        "tags": ["minimal", "signal", "graticule", "vector", "telemetry", "drawing"],
        "code": """// Minimal Signal Precision Graticule - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// High-density technical vector graticule with calibrated sub-division scales, concentric radar sweep rings, binary telemetry data columns, and high-contrast signal crosshairs.
param("scale_factor", 1.05, 0.5, 2.0);
param("sweep_speed", 0.40, 0.1, 1.5);
param("rings", 5.0, 3.0, 8.0);
param("density", 1.2, 0.5, 2.0);

function draw(t) {
  // Midnight telemetry black (#0a0d14)
  background(0.039, 0.051, 0.078);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88 * scale_factor;
  const time = t * sweep_speed;

  push();
  translate(cx, cy);

  // 1. Concentric radar telemetry circles
  const nRings = floor(rings);
  noFill();
  for (let i = 1; i <= nRings; i++) {
    const r = (i / nRings) * (span * 0.46);
    stroke(0.12, 0.16, 0.22);
    strokeWeight(1.2);
    circle(0, 0, r * 2.0);

    // Degree tick marks on outer ring
    if (i === nRings) {
      stroke(0.20, 0.26, 0.36);
      strokeWeight(1.4);
      const segs = 36;
      for (let s = 0; s < segs; s++) {
        const th = (s / segs) * Math.PI * 2.0;
        const len = (s % 9 === 0) ? span * 0.03 : (s % 3 === 0) ? span * 0.018 : span * 0.008;
        line(cos(th) * r, sin(th) * r, cos(th) * (r - len), sin(th) * (r - len));
      }
    }
  }

  // 2. Rotating radar sweep vector line & beam phosphor flare
  push();
  rotate(time * 1.8);
  stroke(1.0, 0.67, 0.02, 0.85); // Electric amber
  strokeWeight(2.0);
  line(0, 0, span * 0.46, 0);

  // Target strobe pips along the sweep line
  fill(1.0, 0.67, 0.02);
  noStroke();
  circle(span * 0.28, 0, span * 0.022);
  circle(span * 0.40, 0, span * 0.016);
  pop();

  // 3. Orthogonal coordinate calibration axes
  stroke(0.94, 0.96, 1.0);
  strokeWeight(1.8);
  line(-span * 0.46, 0, span * 0.46, 0);
  line(0, -span * 0.46, 0, span * 0.46);

  // Center optical crosshair reticle
  noFill();
  stroke(0.94, 0.96, 1.0);
  strokeWeight(2.2);
  circle(0, 0, span * 0.08);

  fill(0.94, 0.96, 1.0);
  circle(0, 0, span * 0.016);

  // 4. Binary telemetry data columns & step calibration scales
  const cols = 4;
  const rows = 12;
  const colW = span * 0.06;
  const rowH = span * 0.025;

  noStroke();
  for (let c = 0; c < cols; c++) {
    for (let r = 0; r < rows; r++) {
      const active = ((c * 7 + r * 13 + floor(time * 4.0)) % 5 === 0);
      if (active) {
        fill(1.0, 0.67, 0.02); // Amber active telemetry bit
      } else {
        fill(0.14, 0.18, 0.26);
      }
      rect(-span * 0.42 + c * colW, span * 0.14 + r * rowH, colW * 0.7, rowH * 0.6);
    }
  }

  // Symmetric telemetry readout on opposite quadrant
  for (let c = 0; c < cols; c++) {
    for (let r = 0; r < rows; r++) {
      const active = ((c * 11 + r * 5 + floor(time * 3.5)) % 4 === 0);
      if (active) {
        fill(0.94, 0.96, 1.0); // White data bit
      } else {
        fill(0.12, 0.15, 0.22);
      }
      rect(span * 0.22 + c * colW, -span * 0.40 + r * rowH, colW * 0.7, rowH * 0.6);
    }
  }

  // 5. Margin registration markers
  stroke(0.94, 0.96, 1.0, 0.7);
  strokeWeight(1.5);
  noFill();
  rect(-span * 0.46, -span * 0.46, span * 0.05, span * 0.05);
  rect(span * 0.41, -span * 0.46, span * 0.05, span * 0.05);
  rect(-span * 0.46, span * 0.41, span * 0.05, span * 0.05);
  rect(span * 0.41, span * 0.41, span * 0.05, span * 0.05);

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: lilliputian_surreal)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "lilliputian-surreal-celestial-monolith-3d",
        "title": "Lilliputian Surreal Celestial Monolith 3D",
        "description": "Metaphysical surrealist architectural landscape featuring an immense monolithic gateway tower and giant planetary sphere standing in a vast terracotta plain, orbited by miniature geometric satellite cubes.",
        "tags": ["surreal", "monolith", "celestial", "architecture", "minimal", "3d"],
        "code": """// Lilliputian Surreal Celestial Monolith 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Metaphysical surrealist architectural landscape featuring an immense monolithic gateway tower and giant planetary sphere standing in a vast terracotta plain, orbited by miniature geometric satellite cubes.
param("spin", 0.35, 0.1, 1.2);
param("scale_factor", 1.15, 0.5, 2.0);
param("monolith_height", 2.2, 1.0, 3.5);
param("satellites", 6.0, 3.0, 12.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.48 + sin(time * 0.12) * 0.04);
  rotateY(time * 0.24 + 0.7);

  // 1. Vast stepped terracotta desert plinth
  push();
  translate(0, 1.25 * scale_factor, 0);
  fill(0.769, 0.412, 0.333); // Dusty terracotta (#c46955)
  box(2.8 * scale_factor, 0.12, 2.8 * scale_factor);

  // Upper stepped terrace tier
  translate(0, -0.09, 0);
  fill(0.867, 0.835, 0.780); // Warm travertine cream (#ddd5c7)
  box(2.0 * scale_factor, 0.06, 2.0 * scale_factor);
  pop();

  // 2. Colossal surrealist monolith pylon tower (Alabaster monolith)
  push();
  translate(-0.35 * scale_factor, (1.25 - monolith_height * 0.5) * scale_factor, -0.2 * scale_factor);
  fill(0.867, 0.835, 0.780);
  box(0.45 * scale_factor, monolith_height * scale_factor, 0.35 * scale_factor);

  // Monolith interior gateway aperture
  push();
  translate(0, monolith_height * 0.25 * scale_factor, 0);
  fill(0.08, 0.10, 0.14); // Dark void portal
  box(0.22 * scale_factor, monolith_height * 0.45 * scale_factor, 0.38 * scale_factor);
  pop();

  // Terracotta cornice header
  push();
  translate(0, -monolith_height * 0.52 * scale_factor, 0);
  fill(0.769, 0.412, 0.333);
  box(0.55 * scale_factor, 0.10 * scale_factor, 0.45 * scale_factor);
  pop();
  pop();

  // 3. Giant celestial sphere suspended in space
  push();
  translate(0.55 * scale_factor, -0.35 * scale_factor, 0.25 * scale_factor);
  rotateY(-time * 0.5);

  // Sphere approximated by stepped equatorial & polar slabs
  fill(0.867, 0.835, 0.780);
  box(0.85 * scale_factor, 0.85 * scale_factor, 0.85 * scale_factor);

  // Equatorial terracotta ring band
  fill(0.769, 0.412, 0.333);
  box(1.2 * scale_factor, 0.08 * scale_factor, 1.2 * scale_factor);
  pop();

  // 4. Lilliputian micro-cubes & miniature satellite geometry
  const nSats = floor(satellites);
  for (let i = 0; i < nSats; i++) {
    const frac = i / nSats;
    const ang = frac * Math.PI * 2.0 + time * (0.8 + frac * 0.4);
    const rOrbit = (1.2 + sin(i * 1.5) * 0.3) * scale_factor;
    const yOrbit = (-0.8 + frac * 1.6) * scale_factor;

    push();
    translate(cos(ang) * rOrbit, yOrbit, sin(ang) * rOrbit);
    rotateX(ang * 2.0);
    rotateY(ang * 1.5);

    if (i % 2 === 0) {
      fill(0.482, 0.549, 0.494); // Dusty sage micro-cube
      box(0.12 * scale_factor, 0.12 * scale_factor, 0.12 * scale_factor);
    } else {
      fill(0.769, 0.412, 0.333); // Terracotta micro-pylon
      box(0.08 * scale_factor, 0.24 * scale_factor, 0.08 * scale_factor);
    }
    pop();
  }

  // 5. Solitary miniature observer pillar on plinth edge
  push();
  translate(1.0 * scale_factor, 1.15 * scale_factor, 0.9 * scale_factor);
  fill(0.482, 0.549, 0.494);
  box(0.08 * scale_factor, 0.22 * scale_factor, 0.08 * scale_factor);
  pop();

  pop();
}
"""
    },
    {
        "slug": "lilliputian-surreal-orbital-observatory-3d",
        "title": "Lilliputian Surreal Orbital Observatory 3D",
        "description": "Monumental armillary sphere observatory rising from a stepped travertine plinth, featuring colossal intersecting equatorial rings, towering observation spires, and suspended micro-cube constellations.",
        "tags": ["surreal", "observatory", "armillary", "sculpture", "celestial", "3d"],
        "code": """// Lilliputian Surreal Orbital Observatory 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Monumental armillary sphere observatory rising from a stepped travertine plinth, featuring colossal intersecting equatorial rings, towering observation spires, and suspended micro-cube constellations.
param("spin", 0.36, 0.1, 1.2);
param("scale_factor", 1.15, 0.5, 2.0);
param("ring_radius", 1.1, 0.6, 1.8);
param("spire_height", 2.0, 1.0, 3.2);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.52 + sin(time * 0.14) * 0.04);
  rotateY(time * 0.26 + 0.6);

  // 1. Stepped travertine foundation base
  push();
  translate(0, 1.25 * scale_factor, 0);
  fill(0.839, 0.800, 0.737); // Travertine base
  box(2.6 * scale_factor, 0.12, 2.6 * scale_factor);

  translate(0, -0.09, 0);
  fill(0.769, 0.412, 0.333); // Terracotta sub-tier
  box(1.8 * scale_factor, 0.06, 1.8 * scale_factor);
  pop();

  // 2. Colossal vertical observation spire
  push();
  translate(-0.45 * scale_factor, (1.25 - spire_height * 0.5) * scale_factor, -0.3 * scale_factor);
  fill(0.482, 0.549, 0.494); // Dusty sage monolithic needle
  box(0.18 * scale_factor, spire_height * scale_factor, 0.18 * scale_factor);

  // Crown finial cube
  translate(0, -spire_height * 0.52 * scale_factor, 0);
  fill(0.769, 0.412, 0.333);
  box(0.30 * scale_factor, 0.30 * scale_factor, 0.30 * scale_factor);
  pop();

  // 3. Central armillary core sphere and gimbal axes
  push();
  translate(0.15 * scale_factor, 0, 0.1 * scale_factor);

  // Alabaster core cube-sphere
  fill(0.922, 0.894, 0.839);
  box(0.55 * scale_factor, 0.55 * scale_factor, 0.55 * scale_factor);

  // Intersecting colossal armillary rings
  const rRing = ring_radius * scale_factor;
  const nSegs = 20;

  // Horizontal ring (Terracotta)
  push();
  rotateY(time * 0.6);
  for (let s = 0; s < nSegs; s++) {
    const th = (s / nSegs) * Math.PI * 2.0;
    push();
    translate(cos(th) * rRing, 0, sin(th) * rRing);
    rotateY(-th);
    fill(0.769, 0.412, 0.333);
    box(0.06 * scale_factor, 0.08 * scale_factor, (2.0 * Math.PI * rRing / nSegs) * 0.95);
    pop();
  }
  pop();

  // Inclined meridian ring (Travertine)
  push();
  rotateZ(0.65);
  rotateX(-time * 0.5);
  for (let s = 0; s < nSegs; s++) {
    const th = (s / nSegs) * Math.PI * 2.0;
    push();
    translate(0, cos(th) * rRing, sin(th) * rRing);
    rotateX(-th);
    fill(0.839, 0.800, 0.737);
    box(0.08 * scale_factor, 0.06 * scale_factor, (2.0 * Math.PI * rRing / nSegs) * 0.95);
    pop();
  }
  pop();

  pop();

  // 4. Lilliputian orbiting micro-constellation cubes
  for (let k = 0; k < 6; k++) {
    const ang = (k / 6.0) * Math.PI * 2.0 - time * 0.75;
    const rad = 1.45 * scale_factor;
    const yP = sin(k * 2.1 + time * 1.2) * 0.4 * scale_factor - 0.2 * scale_factor;

    push();
    translate(cos(ang) * rad, yP, sin(ang) * rad);
    rotateY(ang);
    fill(0.482, 0.549, 0.494);
    box(0.10 * scale_factor, 0.10 * scale_factor, 0.10 * scale_factor);
    pop();
  }

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: acid_house)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "acid-house-303-saw-squelch-synth",
        "title": "Acid House 303 Saw Squelch Synth",
        "description": "Authentic Chicago acid house bassline synthesizer featuring a band-limited sawtooth oscillator, cascading 4-pole diode ladder filter with screaming 18dB resonance, punchy accent decay, and tanh drive.",
        "tags": ["synth", "acid", "tb303", "sawtooth", "filter", "audio"],
        "note_root": 36,
        "params": {
            "cutoff": 0.55,
            "resonance": 0.75,
            "envMod": 0.65,
            "decayTime": 0.32,
            "drive": 0.45
        },
        "code": """param float cutoff = 0.55 [0.1, 1.0]
param float resonance = 0.75 [0.1, 0.95]
param float envMod = 0.65 [0.0, 1.0]
param float decayTime = 0.32 [0.08, 0.9]
param float drive = 0.45 [0.0, 1.0]
state float phase = 0
state float env = 0
state float s1 = 0
state float s2 = 0
state float s3 = 0
state float s4 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decayTime), 0.0001, 0.06)
env = if(target > env, env + (target - env) * 0.94, env - env * decayRate)

dp = freq / sr
phase = phase + dp
phase = if(phase >= 1.0, phase - 1.0, phase)
saw = phase * 2.0 - 1.0

fNorm = clamp(cutoff * 0.18 + env * envMod * 0.32, 0.015, 0.46)
kRes = clamp(resonance * 3.8, 0.2, 3.85)

fb = s4 * kRes
inSig = saw * (1.0 + drive * 1.8) - fb
s1 = s1 + fNorm * (inSig - s1)
s2 = s2 + fNorm * (s1 - s2)
s3 = s3 + fNorm * (s2 - s3)
s4 = s4 + fNorm * (s3 - s4)

sat = (s4 / (1.0 + abs(s4 * 0.6))) * env
out = sat * 0.44
"""
    },
    {
        "slug": "acid-house-accented-square-bass-synth",
        "title": "Acid House Accented Square Bass Synth",
        "description": "Hollow, punchy Roland TB-303 square wave bass synthesizer with asymmetric pulse shaping, aggressive non-linear diode resonance sweep, fast accent decay envelope, and warm analog saturation.",
        "tags": ["synth", "acid", "tb303", "square", "bass", "audio"],
        "note_root": 36,
        "params": {
            "cutoff": 0.50,
            "resonance": 0.72,
            "pulseWidth": 0.58,
            "decayTime": 0.26,
            "drive": 0.40
        },
        "code": """param float cutoff = 0.50 [0.1, 1.0]
param float resonance = 0.72 [0.1, 0.95]
param float pulseWidth = 0.58 [0.2, 0.8]
param float decayTime = 0.26 [0.06, 0.8]
param float drive = 0.40 [0.0, 1.0]
state float phase = 0
state float env = 0
state float s1 = 0
state float s2 = 0
state float s3 = 0
state float s4 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decayTime), 0.0001, 0.08)
env = if(target > env, env + (target - env) * 0.95, env - env * decayRate)

dp = freq / sr
phase = phase + dp
phase = if(phase >= 1.0, phase - 1.0, phase)
sq = if(phase < pulseWidth, 1.0, -1.0)

fNorm = clamp(cutoff * 0.16 + env * 0.34, 0.015, 0.45)
kRes = clamp(resonance * 3.75, 0.2, 3.80)

fb = s4 * kRes
inSig = sq * (1.0 + drive * 1.6) - fb
s1 = s1 + fNorm * (inSig - s1)
s2 = s2 + fNorm * (s1 - s2)
s3 = s3 + fNorm * (s2 - s3)
s4 = s4 + fNorm * (s3 - s4)

sat = (s4 / (1.0 + abs(s4 * 0.55))) * env
out = sat * 0.42
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: future_garage)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "future-garage-dusk-tape-delay-fx",
        "title": "Future Garage Dusk Tape Delay FX",
        "description": "Nocturnal UK future garage tape delay effect featuring dual stereo delay taps, subtle wow and flutter pitch modulation, warm feedback saturation, and gentle high-frequency tape damping.",
        "tags": ["effect", "delay", "tape", "futuregarage", "lofi", "audio"],
        "params": {
            "time": 0.35,
            "flutter": 0.35,
            "feedback": 0.45,
            "damping": 0.50,
            "mix": 0.50
        },
        "code": """param float time = 0.35 [0.05, 0.8]
param float flutter = 0.35 [0.0, 1.0]
param float feedback = 0.45 [0.0, 0.85]
param float damping = 0.50 [0.1, 1.0]
param float mix = 0.50 [0.0, 1.0]
state float pLfo1 = 0
state float pLfo2 = 0
state float tap1 = 0
state float tap2 = 0
state float tap3 = 0
state float lp = 0

dp1 = 1.2 / sr
pLfo1 = pLfo1 + dp1
pLfo1 = if(pLfo1 >= 1.0, pLfo1 - 1.0, pLfo1)
lfo1 = sin(pLfo1 * 6.2831853)

dp2 = 0.35 / sr
pLfo2 = pLfo2 + dp2
pLfo2 = if(pLfo2 >= 1.0, pLfo2 - 1.0, pLfo2)
lfo2 = sin(pLfo2 * 6.2831853)

fCut = clamp(0.15 + (1.0 - damping) * 0.25, 0.02, 0.45)
lp = lp + fCut * (in - lp)

modRate = clamp(0.08 + flutter * 0.03 * (lfo1 + lfo2 * 0.5), 0.02, 0.35)
tap1 = tap1 + modRate * (lp + tap3 * feedback * 0.45 - tap1)
tap2 = tap2 + modRate * 0.88 * (tap1 - tap2)
tap3 = tap3 + modRate * 0.76 * (tap2 - tap3)

delayed = (tap2 + tap3) * 0.85
sat = (delayed / (1.0 + abs(delayed * 0.4)))

processed = in * (1.0 - mix) + sat * mix
out = processed * 1.55
"""
    },
    {
        "slug": "future-garage-vinyl-dust-reverb-fx",
        "title": "Future Garage Vinyl Dust Reverb FX",
        "description": "Atmospheric Burial-inspired space processor combining procedural vinyl surface crackle, diffuse multi-tap reverb diffusion, warm low-mid resonance, and dusk lowpass filtering.",
        "tags": ["effect", "reverb", "vinyl", "dust", "futuregarage", "audio"],
        "params": {
            "decay": 0.65,
            "dust": 0.25,
            "tone": 0.45,
            "warmth": 0.50,
            "mix": 0.55
        },
        "code": """param float decay = 0.65 [0.1, 1.0]
param float dust = 0.25 [0.0, 0.8]
param float tone = 0.45 [0.1, 1.0]
param float warmth = 0.50 [0.0, 1.0]
param float mix = 0.55 [0.0, 1.0]
state float noiseState = 0.12345
state float d1 = 0
state float d2 = 0
state float d3 = 0
state float d4 = 0
state float lp = 0

noiseState = (noiseState * 196314.165 + 90763.3) % 1.0
randNoise = noiseState * 2.0 - 1.0
crackle = if(abs(randNoise) > 0.992, randNoise * 0.35 * dust, 0.0)

fCut = clamp(0.08 + tone * 0.32, 0.02, 0.45)
lp = lp + fCut * ((in + crackle) - lp)

kDiff = clamp(0.12 * (1.0 + decay * 0.8), 0.04, 0.38)
d1 = d1 + kDiff * (lp + d4 * 0.28 * decay - d1)
d2 = d2 + kDiff * 0.91 * (d1 - d2)
d3 = d3 + kDiff * 0.82 * (d2 - d3)
d4 = d4 + kDiff * 0.74 * (d3 - d4)

diffuse = (d2 - d3 + d4) * 0.95
sat = (diffuse / (1.0 + abs(diffuse * warmth * 0.5)))

processed = in * (1.0 - mix) + sat * mix
out = processed * 1.60
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel Sources & Embeds (Theme: indie_psychedelic)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "indie-psychedelic-liquid-oil-wheel",
        "title": "Indie Psychedelic Liquid Oil Wheel",
        "description": "1960s Joshua Light Show liquid projector simulation with swirling immiscible colored oils, audio-reactive viscosity shear, undulating color separation, and warm psychedelic chromatic fringes.",
        "embed_desc": "Audio-reactive psychedelic liquid oil wheel projection inspired by 1960s Joshua Light Show liquid experiments. Bass frequencies swell viscous oil droplet boundaries, mid frequencies modulate liquid shear turbulence across marigold and magenta pigments, and high frequencies illuminate iridescent emerald edges.",
        "tags": ["pixel", "psychedelic", "liquid", "oil", "fluid", "shader"],
        "params": {
            "speed": 0.40,
            "viscosity": 1.20,
            "swirl": 1.40,
            "contrast": 1.15,
            "glow": 0.55
        },
        "code": """param float speed = 0.40 [0.1, 1.5]
param float viscosity = 1.20 [0.4, 2.5]
param float swirl = 1.40 [0.2, 3.0]
param float contrast = 1.15 [0.6, 2.0]
param float glow = 0.55 [0.1, 1.2]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.4
time = t * speed

r = length(p)
theta = atan2(p.y, p.x) + (swirl / (r + 0.25))

w1 = sin(r * 12.0 - time * 2.4 + theta * 3.0)
w2 = cos(p.x * 10.0 * viscosity + time * 1.6) * sin(p.y * 10.0 * viscosity - time * 1.3)
wave = (w1 + w2) * 0.5

rDisp = wave * 0.08
dispP = p + vec2(rDisp, -rDisp)
waveDisp = sin(length(dispP) * 14.0 - time * 2.0 + theta * 2.0)

cDark = vec3(0.05, 0.02, 0.08)
cMarigold = vec3(0.98, 0.64, 0.07)
cMagenta = vec3(0.88, 0.10, 0.46)
cMint = vec3(0.02, 0.84, 0.63)

base = mix(cDark, cMagenta, clamp(wave * 0.5 + 0.5, 0.0, 1.0))
col = mix(base, cMarigold, clamp(waveDisp * 0.6 + 0.4, 0.0, 1.0))

rim = 1.0 - smoothstep(0.28, 0.65, abs(wave))
col = mix(col, cMint, rim * glow)

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
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.4;
  float time = t * 0.40;

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  float r = length(p);
  float theta = atan(p.y, p.x) + ((1.40 + b * 1.0) / (r + 0.25));

  float scale = 12.0 + m * 4.0;
  float w1 = sin(r * scale - time * 2.4 + theta * 3.0);
  float w2 = cos(p.x * 10.0 * (1.20 + h * 0.4) + time * 1.6) * sin(p.y * 10.0 * (1.20 + h * 0.4) - time * 1.3);
  float wave = (w1 + w2) * 0.5 * (1.0 + b * 0.35);

  float rDisp = wave * 0.08;
  vec2 dispP = p + vec2(rDisp, -rDisp);
  float waveDisp = sin(length(dispP) * (14.0 + m * 5.0) - time * 2.0 + theta * 2.0);

  vec3 cDark = vec3(0.05, 0.02, 0.08);
  vec3 cMarigold = vec3(0.98, 0.64, 0.07);
  vec3 cMagenta = vec3(0.88, 0.10, 0.46);
  vec3 cMint = vec3(0.02, 0.84, 0.63);

  vec3 base = mix(cDark, cMagenta, clamp(wave * 0.5 + 0.5, 0.0, 1.0));
  vec3 col = mix(base, cMarigold, clamp(waveDisp * 0.6 + 0.4, 0.0, 1.0));

  float rim = 1.0 - smoothstep(0.28, 0.65, abs(wave));
  col = mix(col, cMint, rim * (0.55 + h * 0.45));

  gl_FragColor = vec4(clamp(col * 1.15, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "indie-psychedelic-kaleido-prism-flower",
        "title": "Indie Psychedelic Kaleido Prism Flower",
        "description": "Breathing kaleidoscopic mandala unfolding radiating fractal petal rings, audio-reactive radial expansion, chromatic prism dispersion, and pulsating harmonic energy.",
        "embed_desc": "Audio-reactive psychedelic mandala visualizer featuring an 8-fold symmetric petal flower. Bass pulses inflate radial petal contours, mid frequencies drive chromatic prism refraction across electric magenta and marigold bands, and high frequencies ignite cyan perimeter highlights.",
        "tags": ["pixel", "psychedelic", "kaleidoscope", "mandala", "prism", "shader"],
        "params": {
            "speed": 0.36,
            "petals": 8.0,
            "rippleScale": 14.0,
            "contrast": 1.16,
            "glow": 0.60
        },
        "code": """param float speed = 0.36 [0.1, 1.5]
param float petals = 8.0 [4.0, 16.0]
param float rippleScale = 14.0 [6.0, 28.0]
param float contrast = 1.16 [0.6, 2.0]
param float glow = 0.60 [0.1, 1.2]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.4
time = t * speed

r = length(p)
theta = atan2(p.y, p.x)
sector = 6.2831853 / petals
modTheta = abs(mod(theta + time * 0.22, sector) - sector * 0.5)

kp = vec2(cos(modTheta), sin(modTheta)) * r

w = sin(kp.x * rippleScale - time * 2.0) * cos(kp.y * rippleScale + time * 1.6)
ring = sin(r * 18.0 - time * 2.8)
sig = (w + ring) * 0.5

cDark = vec3(0.04, 0.02, 0.07)
cMagenta = vec3(0.88, 0.10, 0.46)
cMarigold = vec3(0.98, 0.64, 0.07)
cCyan = vec3(0.0, 0.88, 0.95)

col = mix(cDark, cMagenta, clamp(sig * 0.5 + 0.5, 0.0, 1.0))
col = mix(col, cMarigold, clamp(w * 0.7 + 0.3, 0.0, 1.0))

star = exp(-abs(sig) * (5.2 - glow * 2.0))
col = mix(col, cCyan, star * 0.75)

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
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.4;
  float time = t * 0.36;

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  float r = length(p);
  float theta = atan(p.y, p.x);
  float petals = 8.0;
  float sector = 6.2831853 / petals;
  float modTheta = abs(mod(theta + time * 0.22, sector) - sector * 0.5);

  vec2 kp = vec2(cos(modTheta), sin(modTheta)) * r;

  float rippleScale = 14.0 + m * 5.0;
  float w = sin(kp.x * rippleScale - time * 2.0) * cos(kp.y * rippleScale + time * 1.6);
  float ring = sin(r * (18.0 + b * 6.0) - time * 2.8);
  float sig = (w + ring) * 0.5 * (1.0 + b * 0.35);

  vec3 cDark = vec3(0.04, 0.02, 0.07);
  vec3 cMagenta = vec3(0.88, 0.10, 0.46);
  vec3 cMarigold = vec3(0.98, 0.64, 0.07);
  vec3 cCyan = vec3(0.0, 0.88, 0.95);

  vec3 col = mix(cDark, cMagenta, clamp(sig * 0.5 + 0.5, 0.0, 1.0));
  vec3 col2 = mix(col, cMarigold, clamp(w * 0.7 + 0.3, 0.0, 1.0));

  float star = exp(-abs(sig) * (5.2 - (0.60 + h * 0.4) * 2.0));
  col2 = mix(col2, cCyan, star * 0.75);

  gl_FragColor = vec4(clamp(col2 * 1.15, 0.0, 1.0), 1.0);
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
    print("=== Creative Expansion Pack 30: Build and Gate Runner ===")
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
            print(f"FAIL validate: {slug}\\n{v.stdout}")
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        frame_png = os.path.join(out_frame_dir, "frame.png")
        f_proc = subprocess.run([BIN, "frame", test_inf, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        if '"ok":true' not in f_proc.stdout or not os.path.exists(frame_png):
            print(f"FAIL frame: {slug}\\n{f_proc.stdout}")
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
            print(f"FAIL validate: {slug}\\n{v.stdout}")
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        frame_png = os.path.join(out_frame_dir, "frame.png")
        f_proc = subprocess.run([BIN, "frame", test_inf, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        if '"ok":true' not in f_proc.stdout or not os.path.exists(frame_png):
            print(f"FAIL frame: {slug}\\n{f_proc.stdout}")
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
  i note3 {nr+3}
  i note4 {nr}
  i note5 {nr+5}
  i note6 {nr+7}
  i note7 {nr+10}
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
node 4 Modulators Audio Analyze
  id ana
  pos 0 2200
end
node 5 Source Shape
  id pic
  pos 0 162
  c fillColor 0.18 0.22 0.32
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
note synth 0 notes
aud speakers 0 synth
aud ana 0 synth
cable out 0 pic
mod pic sizeX ana 0 0 1 0.5 0.14 0.38
mod pic sizeY ana 0 0 1 0.5 0.14 0.38
"""
        inf_file = os.path.join(TEMPLATES_DIR, f"{slug}.inf")
        with open(inf_file, "w") as f:
            f.write(inf_content)
        it["bytes"] = os.path.getsize(inf_file)

        v = subprocess.run([BIN, "validate", inf_file], capture_output=True, text=True)
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\\n{v.stdout}")
            failed = True
            continue

        sum_json = os.path.join(TMP_DIR, f"{slug}_sum.json")
        s = subprocess.run([BIN, "audio", inf_file, sum_json, "--duration", "6"], capture_output=True, text=True)
        if '"ok":true' not in s.stdout or not os.path.exists(sum_json):
            print(f"FAIL audio command: {slug}\\n{s.stdout}")
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
node 5 Modulators Audio Analyze
  id ana
  pos 0 2200
end
node 6 Source Shape
  id pic
  pos 0 162
  c fillColor 0.15 0.20 0.30
  f size 0.24
end
node 7 Utility Output
  id out
  pos 460 0
end
node 8 Compositing Comment
  pos 0 722
  s text {it['title']}. {it['description']}
  f width 340
  f height 120
end
note osc 0 notes
aud fx 0 osc
aud speakers 0 fx
aud ana 0 fx
cable out 0 pic
mod pic sizeX ana 0 0 1 0.5 0.14 0.38
mod pic sizeY ana 0 0 1 0.5 0.14 0.38
"""
        inf_file = os.path.join(TEMPLATES_DIR, f"{slug}.inf")
        with open(inf_file, "w") as f:
            f.write(inf_content)
        it["bytes"] = os.path.getsize(inf_file)

        v = subprocess.run([BIN, "validate", inf_file], capture_output=True, text=True)
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\\n{v.stdout}")
            failed = True
            continue

        sum_json = os.path.join(TMP_DIR, f"{slug}_sum.json")
        s = subprocess.run([BIN, "audio", inf_file, sum_json, "--duration", "6"], capture_output=True, text=True)
        if '"ok":true' not in s.stdout or not os.path.exists(sum_json):
            print(f"FAIL audio command: {slug}\\n{s.stdout}")
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
            print(f"FAIL validate: {slug}\\n{v.stdout}")
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        frame_png = os.path.join(out_frame_dir, "frame.png")
        f_proc = subprocess.run([BIN, "frame", test_inf, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        if '"ok":true' not in f_proc.stdout or not os.path.exists(frame_png):
            print(f"FAIL frame: {slug}\\n{f_proc.stdout}")
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
                "domain": "audio",
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
