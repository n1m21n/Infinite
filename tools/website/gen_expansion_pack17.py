#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 17.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (minimal-signal-raster-scanner, minimal-signal-binary-matrix)
2. Sketch 3D (data-landscape-contour-mesh-3d, data-landscape-topological-ridge-3d)
3. Field Synth (liquid-dnb-roller-bass-synth, liquid-dnb-rhodes-glide-synth)
4. Field Effects (french-touch-disco-filter-fx, french-touch-phaser-sweep-fx)
5. Field Pixel (demoscene-plasma-sine-matrix, demoscene-plasma-rotozoom-bloom)
"""
import os, sys, json, subprocess, zipfile
from PIL import Image, ImageStat

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN = os.path.expanduser("~/Infinite/worktrees/main/build/Infinite.app/Contents/MacOS/Infinite")

LIB_DIR = os.path.join(ROOT, "website", "assets", "library")
SKETCH_DIR = os.path.join(LIB_DIR, "sketch")
PIXEL_DIR = os.path.join(LIB_DIR, "pixel")
PREVIEWS_DIR = os.path.join(LIB_DIR, "previews")
THUMBS_DIR = os.path.join(LIB_DIR, "thumbs")
TEMPLATES_DIR = os.path.join(ROOT, "website", "assets", "templates")
EMBEDS_DIR = os.path.join(ROOT, "website", "embeds")

TMP_DIR = "/tmp/pack17_build"
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
        "slug": "minimal-signal-raster-scanner",
        "title": "Minimal Signal Raster Scanner",
        "description": "Ryoji Ikeda inspired high-frequency raster scanner with horizontal scanlines, binary barcode pulses, and electric red strobe markers.",
        "tags": ["minimal", "signal", "raster", "scanner", "monochrome", "drawing"],
        "code": """// Minimal Signal Raster Scanner - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Ryoji Ikeda inspired high-frequency raster scanner with horizontal scanlines, binary barcode pulses, and electric red strobe markers.
param("scan_speed", 0.5, 0.1, 2.0);
param("line_density", 28.0, 10.0, 60.0);
param("strobe_rate", 1.2, 0.2, 4.0);
param("track_width", 0.85, 0.4, 0.95);

function draw(t) {
  // Deep carbon black background #101010
  background(0.063, 0.063, 0.063);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * track_width;
  const time = t * scan_speed;
  const lines = floor(line_density);

  push();
  translate(cx - span * 0.5, cy - span * 0.5);

  // Outer framing boundary
  noFill();
  stroke(0.94, 0.94, 0.94, 0.4);
  strokeWeight(1.5);
  rect(0, 0, span, span);

  // Raster scanlines & binary pulse bars
  for (let i = 0; i < lines; i++) {
    const y = (i / lines) * span;
    const h = span / lines * 0.7;

    // Scan segment alternating white and carbon blocks
    if (i % 2 === 0) {
      noStroke();
      fill(0.94, 0.94, 0.94);
      const segW = span * (0.15 + 0.35 * (sin(i * 1.7 + time) * 0.5 + 0.5));
      const segX = (span - segW) * (cos(i * 0.9 + time * 0.5) * 0.5 + 0.5);
      rect(segX, y, segW, h);

      // Barcode sub-ticks
      stroke(0.063, 0.063, 0.063);
      strokeWeight(1.2);
      for (let k = 0; k < 6; k++) {
        const tx = segX + (k / 5.0) * segW;
        line(tx, y, tx, y + h);
      }
    } else {
      // High-precision measurement grid line
      stroke(0.94, 0.94, 0.94, 0.35);
      strokeWeight(1.0);
      line(0, y + h * 0.5, span, y + h * 0.5);
    }

    // Electric signal red strobe indicator
    if (i % 7 === 3) {
      noStroke();
      fill(1.0, 0.2, 0.0);
      const rx = (sin(time * strobe_rate + i) * 0.5 + 0.5) * (span - 24.0);
      rect(rx, y, 22.0, h);
    }
  }

  // Central oscilloscope waveform ribbon
  stroke(1.0, 0.2, 0.0);
  strokeWeight(2.5);
  noFill();
  beginShape();
  for (let x = 0; x <= span; x += 8) {
    const normX = x / span;
    const wave = sin(normX * 18.0 + time * 4.0) * cos(normX * 6.0) * (span * 0.18);
    vertex(x, span * 0.5 + wave);
  }
  endShape();

  // Metric ticks along left and right
  stroke(0.94, 0.94, 0.94, 0.8);
  strokeWeight(1.5);
  for (let k = 0; k <= 10; k++) {
    const ty = (k / 10.0) * span;
    line(0, ty, 14, ty);
    line(span - 14, ty, span, ty);
  }

  pop();
}
"""
    },
    {
        "slug": "minimal-signal-binary-matrix",
        "title": "Minimal Signal Binary Matrix",
        "description": "Precision digital test pattern matrix with algorithmic binary telemetry, segmented waveform ribbons, and focal signal reticles.",
        "tags": ["minimal", "signal", "binary", "matrix", "datamatics", "drawing"],
        "code": """// Minimal Signal Binary Matrix - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Precision digital test pattern matrix with algorithmic binary telemetry, segmented waveform ribbons, and focal signal reticles.
param("clock_speed", 0.45, 0.1, 2.0);
param("matrix_cols", 8.0, 4.0, 16.0);
param("strobe_pulse", 1.5, 0.5, 3.0);
param("reticle_scale", 1.1, 0.5, 2.0);

function draw(t) {
  // Pure carbon ground #101010
  background(0.063, 0.063, 0.063);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const minDim = min(width, height);
  const span = minDim * 0.86;
  const time = t * clock_speed;
  const cols = floor(matrix_cols);
  const colW = span / cols;

  push();
  translate(cx - span * 0.5, cy - span * 0.5);

  // Outer framing line
  noFill();
  stroke(0.94, 0.94, 0.94, 0.3);
  strokeWeight(1.0);
  rect(0, 0, span, span);

  // Vertical binary data tracks
  for (let c = 0; c < cols; c++) {
    const x = c * colW;

    // Track divider
    stroke(0.94, 0.94, 0.94, 0.2);
    strokeWeight(1.0);
    line(x, 0, x, span);

    // Segmented binary pulses in each column
    const rows = 12;
    const rowH = span / rows;
    for (let r = 0; r < rows; r++) {
      const y = r * rowH;
      const seedVal = sin(c * 17.3 + r * 31.7 + time * 2.5);

      if (seedVal > 0.15) {
        // High signal white telemetry block
        noStroke();
        fill(0.94, 0.94, 0.94);
        rect(x + 4.0, y + 3.0, colW - 8.0, rowH - 6.0);

        // Internal barcode slits
        stroke(0.063, 0.063, 0.063);
        strokeWeight(1.0);
        line(x + colW * 0.35, y + 3.0, x + colW * 0.35, y + rowH - 3.0);
        line(x + colW * 0.7, y + 3.0, x + colW * 0.7, y + rowH - 3.0);
      } else if (seedVal < -0.65) {
        // Critical signal red pulse marker
        noStroke();
        fill(1.0, 0.2, 0.0);
        rect(x + 6.0, y + rowH * 0.35, colW - 12.0, rowH * 0.3);
      } else {
        // Subtle measurement crosshair
        stroke(0.94, 0.94, 0.94, 0.4);
        strokeWeight(1.0);
        line(x + colW * 0.5 - 4.0, y + rowH * 0.5, x + colW * 0.5 + 4.0, y + rowH * 0.5);
      }
    }
  }

  // Central focal targeting reticle
  push();
  translate(span * 0.5, span * 0.5);
  noFill();
  stroke(1.0, 0.2, 0.0);
  strokeWeight(2.0);
  const retSize = minDim * 0.22 * reticle_scale;
  ellipse(0, 0, retSize, retSize);
  ellipse(0, 0, retSize * 0.5, retSize * 0.5);
  line(-retSize * 0.7, 0, retSize * 0.7, 0);
  line(0, -retSize * 0.7, 0, retSize * 0.7);

  // Red beacon dot
  noStroke();
  fill(1.0, 0.2, 0.0);
  circle(0, 0, 10.0);
  pop();

  // Top and bottom status text lines (simulated raster data)
  stroke(0.94, 0.94, 0.94, 0.85);
  strokeWeight(2.0);
  for (let k = 0; k < 18; k++) {
    const tx = (k / 18.0) * span + 6.0;
    line(tx, -12, tx + (k % 3 === 0 ? 12 : 5), -12);
    line(tx, span + 12, tx + (k % 2 === 0 ? 10 : 4), span + 12);
  }

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: data_landscape)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "data-landscape-contour-mesh-3d",
        "title": "Data Landscape Contour Mesh 3D",
        "description": "Peter Saville inspired procedural 3D topological contour mesh with stepped elevation strata and oceanic cyan depth shading.",
        "tags": ["data", "landscape", "contour", "mesh", "3d", "topography", "geometry"],
        "code": """// Data Landscape Contour Mesh 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Peter Saville inspired procedural 3D topological contour mesh with stepped elevation strata and oceanic cyan depth shading.
param("elevation_scale", 1.0, 0.2, 2.5);
param("strata_layers", 14.0, 6.0, 24.0);
param("rotation_speed", 0.35, 0.05, 1.2);
param("terrain_spread", 1.2, 0.5, 2.0);
param("ridge_steepness", 0.65, 0.1, 1.5);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.72 + sin(time * 0.2) * 0.08);
  rotateY(time * 0.35);

  const layers = floor(strata_layers);
  const spread = terrain_spread;

  for (let i = 0; i < layers; i++) {
    const norm = i / layers;
    const y = (norm - 0.5) * 1.4 * elevation_scale;
    const layerScale = (1.0 - norm * 0.45) * spread;

    push();
    translate(0, y, 0);
    rotateY(norm * 0.4 + sin(time * 0.3 + norm * 2.0) * 0.15);

    if (i < layers * 0.35) {
      fill(0.043, 0.075, 0.169); // Deep navy
    } else if (i < layers * 0.75) {
      fill(0.357, 0.753, 0.745); // Oceanic cyan
    } else {
      fill(0.11, 0.145, 0.255); // Slate indigo
    }

    const w = layerScale * (1.2 + sin(norm * 4.0 + time * 0.5) * 0.25);
    const d = layerScale * (0.9 + cos(norm * 3.5 + time * 0.4) * 0.2);
    box(w, 0.06 * elevation_scale, d);

    if (i % 3 === 0) {
      push();
      translate(w * 0.45, 0.12, d * 0.45);
      fill(1.0, 1.0, 1.0);
      box(0.05, 0.22, 0.05);
      pop();
      push();
      translate(-w * 0.45, 0.12, -d * 0.45);
      fill(0.357, 0.753, 0.745);
      box(0.05, 0.22, 0.05);
      pop();
    }

    pop();
  }

  push();
  translate(0, 0.85 * elevation_scale, 0);
  fill(0.043, 0.075, 0.169);
  sphere(0.09, 16, 16);
  pop();

  pop();
}
"""
    },
    {
        "slug": "data-landscape-topological-ridge-3d",
        "title": "Data Landscape Topological Ridge 3D",
        "description": "Generative data sculpture with undulating stacked frequency waveform ridges and floating structural datum planes.",
        "tags": ["data", "landscape", "topological", "ridge", "3d", "sculpture", "geometry"],
        "code": """// Data Landscape Topological Ridge 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Generative data sculpture with undulating stacked frequency waveform ridges and floating structural datum planes.
param("ridge_count", 9.0, 4.0, 16.0);
param("wave_amplitude", 0.65, 0.2, 1.8);
param("orbit_speed", 0.32, 0.05, 1.0);
param("span_width", 1.4, 0.6, 2.5);

function draw(t) {
  push();
  const time = t * orbit_speed;

  rotateX(0.68 + sin(time * 0.2) * 0.06);
  rotateY(time * 0.38);
  rotateZ(0.08);

  const ridges = floor(ridge_count);
  const totalDepth = 1.35;

  for (let r = 0; r < ridges; r++) {
    const zFrac = (r / ridges) - 0.5;
    const z = zFrac * totalDepth;

    push();
    translate(0, 0, z);

    if (r % 3 === 0) {
      fill(0.043, 0.075, 0.169); // Deep navy
    } else if (r % 3 === 1) {
      fill(0.357, 0.753, 0.745); // Oceanic cyan
    } else {
      fill(0.11, 0.145, 0.255); // Slate indigo
    }

    const segs = 16;
    const segW = span_width / segs;
    for (let s = 0; s < segs; s++) {
      const xFrac = (s / segs) - 0.5;
      const x = xFrac * span_width;

      const bell = exp(-xFrac * xFrac * 12.0);
      const wave = sin(s * 0.75 + r * 1.2 + time * 2.0) * wave_amplitude * bell;
      const h = 0.08 + wave * 0.8;

      push();
      translate(x, wave * 0.5, 0);
      box(segW * 0.88, h + 0.08, 0.06);

      if (h > 0.25) {
        push();
        translate(0, (h + 0.08) * 0.5 + 0.02, 0);
        fill(1.0, 1.0, 1.0);
        box(segW * 0.88, 0.03, 0.06);
        pop();
      }
      pop();
    }

    pop();
  }

  push();
  translate(0, -0.45, 0);
  fill(0.043, 0.075, 0.169);
  box(span_width * 1.08, 0.02, totalDepth * 1.15);
  pop();

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: liquid_dnb)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "liquid-dnb-roller-bass-synth",
        "title": "Liquid DnB Roller Bass Synth",
        "description": "Deep dual-oscillator rolling Reese bass synthesizer with smooth detune glide, analog saturation, and low-pass filter warmth.",
        "tags": ["synth", "liquid", "dnb", "bass", "reese", "drum-and-bass"],
        "note_root": 38,
        "params": {
            "detune": 0.45,
            "cutoff": 950.0,
            "drive": 1.6,
            "subLevel": 0.65,
            "decay": 0.55
        },
        "code": """param float detune = 0.45 [0.05, 1.2]
param float cutoff = 950.0 [80.0, 4500.0]
param float drive = 1.6 [0.5, 3.5]
param float subLevel = 0.65 [0.0, 1.0]
param float decay = 0.55 [0.1, 2.5]
state float phase1 = 0
state float phase2 = 0
state float subPhase = 0
state float curFreq = 55
state float env = 0
state float lp1 = 0
state float lp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decay), 0.00004, 0.01)
env = if(target > env, env + (target - env) * 0.15, env - env * decayRate)

curFreq = curFreq + (freq - curFreq) * 0.12
fBase = clamp(curFreq, 20.0, 2000.0)

step1 = fBase / sr
step2 = (fBase * (1.0 + detune * 0.018)) / sr
stepSub = (fBase * 0.5) / sr

phase1 = (phase1 + step1) % 1.0
phase2 = (phase2 + step2) % 1.0
subPhase = (subPhase + stepSub) % 1.0

saw1 = phase1 * 2.0 - 1.0
saw2 = phase2 * 2.0 - 1.0
sub = sin(subPhase * 6.2831853) * subLevel

raw = (saw1 * 0.45 + saw2 * 0.45 + sub) * env

u = raw * drive
sat = clamp(u - 0.25 * u * u * u, -1.0, 1.0)

fCut = clamp(cutoff / sr * 2.8, 0.005, 0.42)
lp1 = lp1 + fCut * (sat - lp1)
lp2 = lp2 + fCut * (lp1 - lp2)

out = lp2 * 0.28
"""
    },
    {
        "slug": "liquid-dnb-rhodes-glide-synth",
        "title": "Liquid DnB Rhodes Glide Synth",
        "description": "Lush electric piano chime synthesizer with velocity hammer bite, stereo harmonic shimmer, and gentle tape warmth.",
        "tags": ["synth", "liquid", "dnb", "rhodes", "electric-piano", "chime"],
        "note_root": 62,
        "params": {
            "tineMod": 0.72,
            "warmth": 0.65,
            "cutoff": 2600.0,
            "decay": 0.75,
            "tremolo": 0.25
        },
        "code": """param float tineMod = 0.72 [0.1, 2.0]
param float warmth = 0.65 [0.1, 1.0]
param float cutoff = 2600.0 [200.0, 8000.0]
param float decay = 0.75 [0.1, 3.0]
param float tremolo = 0.25 [0.0, 0.8]
state float phaseC = 0
state float phaseM = 0
state float env = 0
state float tineEnv = 0
state float lfo = 0
state float lp1 = 0
state float curFreq = 220

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decay), 0.00003, 0.01)
env = if(target > env, env + (target - env) * 0.25, env - env * decayRate)
tineEnv = if(target > tineEnv, 1.0, tineEnv * 0.9982)

curFreq = curFreq + (freq - curFreq) * 0.15
fBase = clamp(curFreq, 30.0, 3000.0)

stepC = fBase / sr
stepM = (fBase * 3.0) / sr

phaseC = (phaseC + stepC) % 1.0
phaseM = (phaseM + stepM) % 1.0
lfo = (lfo + 5.5 / sr) % 1.0

modSig = sin(phaseM * 6.2831853) * tineMod * tineEnv
car = sin((phaseC + modSig * 0.25) * 6.2831853)

trem = 1.0 - tremolo * 0.5 * (1.0 + sin(lfo * 6.2831853))
raw = car * env * trem

sat = clamp(raw * (1.0 + warmth * 0.5), -1.0, 1.0)
fCut = clamp(cutoff / sr * 2.8, 0.008, 0.45)
lp1 = lp1 + fCut * (sat - lp1)

out = lp1 * 0.35
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: french_filter_house)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "french-touch-disco-filter-fx",
        "title": "French Touch Disco Filter FX",
        "description": "Classic 4-pole French Touch resonant low-pass filter with rhythmic LFO sweep, saturation drive, and disco pump character.",
        "tags": ["effect", "filter", "french-touch", "disco", "house", "audio-effects"],
        "params": {
            "sweepRate": 1.25,
            "resonance": 0.72,
            "cutoff": 1500.0,
            "drive": 1.35,
            "mix": 0.75
        },
        "code": """param float sweepRate = 1.25 [0.1, 10.0]
param float resonance = 0.72 [0.1, 0.96]
param float cutoff = 1500.0 [80.0, 9000.0]
param float drive = 1.35 [0.5, 3.5]
param float mix = 0.75 [0.0, 1.0]
state float lfo = 0
state float s1 = 0
state float s2 = 0
state float s3 = 0
state float s4 = 0

lfo = (lfo + sweepRate / sr) % 1.0
sweep = sin(lfo * 6.2831853) * 0.42 + 0.58

u = in * drive
sat = clamp(u - 0.2 * u * u * u, -1.0, 1.0)

fCut = clamp((cutoff * sweep) / sr * 3.14159265, 0.005, 0.48)
fb = resonance * 3.6
inputSignal = sat - s4 * fb

s1 = s1 + fCut * (inputSignal - s1)
s2 = s2 + fCut * (s1 - s2)
s3 = s3 + fCut * (s2 - s3)
s4 = s4 + fCut * (s3 - s4)

out = (in * (1.0 - mix) + s4 * mix) * 1.15
"""
    },
    {
        "slug": "french-touch-phaser-sweep-fx",
        "title": "French Touch Phaser Sweep FX",
        "description": "Creamy 6-stage all-pass phaser with wide stereo modulation, regenerative resonance feedback, and analog warmth.",
        "tags": ["effect", "phaser", "french-touch", "sweep", "house", "audio-effects"],
        "params": {
            "rate": 0.65,
            "depth": 0.8,
            "feedback": 0.62,
            "baseFreq": 800.0,
            "mix": 0.5
        },
        "code": """param float rate = 0.65 [0.05, 5.0]
param float depth = 0.8 [0.1, 1.0]
param float feedback = 0.62 [0.0, 0.92]
param float baseFreq = 800.0 [100.0, 3000.0]
param float mix = 0.5 [0.0, 1.0]
state float lfo = 0
state float ap1_x = 0
state float ap1_y = 0
state float ap2_x = 0
state float ap2_y = 0
state float ap3_x = 0
state float ap3_y = 0
state float ap4_x = 0
state float ap4_y = 0

lfo = (lfo + rate / sr) % 1.0
mod = sin(lfo * 6.2831853) * 0.5 + 0.5
fSweep = clamp(baseFreq * (1.0 + mod * depth * 3.0), 50.0, 12000.0)

w = clamp(fSweep * 3.14159265 / sr, 0.01, 0.85)
alpha = (1.0 - w) / (1.0 + w)

inp = in + ap4_y * feedback

y1 = alpha * inp + ap1_x - alpha * ap1_y
ap1_x = inp
ap1_y = y1

y2 = alpha * y1 + ap2_x - alpha * ap2_y
ap2_x = y1
ap2_y = y2

y3 = alpha * y2 + ap3_x - alpha * ap3_y
ap3_x = y2
ap3_y = y3

y4 = alpha * y3 + ap4_x - alpha * ap4_y
ap4_x = y3
ap4_y = y4

wet = y4
out = (in * (1.0 - mix) + wet * mix) * 0.92
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel Sources (Theme: demoscene_plasma)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "demoscene-plasma-sine-matrix",
        "title": "Demoscene Plasma Sine Matrix",
        "description": "Classic demoscene trigonometric sine-plasma shader with multi-frequency wave summing, fire orange palette cycling, and electric cyan edge glows.",
        "embed_desc": "Classic demoscene trigonometric sine-plasma shader with multi-frequency wave summing, fire orange palette cycling, and electric cyan edge glows. Bass accelerates plasma turbulence and central pulsation, mids cycle interference harmonics, highs illuminate electric cyan coronal flares.",
        "tags": ["pixel", "demoscene", "plasma", "shader", "sine", "retro"],
        "params": {
            "waveSpeed": 0.85,
            "freqX": 4.5,
            "freqY": 4.0,
            "swirl": 1.35,
            "glow": 0.8
        },
        "code": """param float waveSpeed = 0.85 [0.1, 3.0]
param float freqX = 4.5 [1.0, 12.0]
param float freqY = 4.0 [1.0, 12.0]
param float swirl = 1.35 [0.2, 4.0]
param float glow = 0.8 [0.1, 1.5]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.4
r = length(p)
ang = atan2(p.y, p.x)
time = t * waveSpeed

w1 = sin(p.x * freqX + time * 1.5)
w2 = sin(p.y * freqY - time * 1.2)
w3 = sin((p.x + p.y) * (freqX * 0.7) + time * 0.9)
w4 = sin(r * (freqY * 1.2) - time * 2.2 + ang * swirl)

plasma = (w1 + w2 + w3 + w4) * 0.25

cBlack = vec3(0.02, 0.02, 0.04)
cFire = vec3(1.0, 0.23, 0.0)
cCyan = vec3(0.0, 0.88, 1.0)
cWhite = vec3(1.0, 1.0, 1.0)

ramp1 = mix(cBlack, cFire, clamp(plasma * 1.2 + 0.5, 0.0, 1.0))
ramp2 = mix(ramp1, cCyan, clamp(sin(plasma * 6.2831853 + time) * 0.5 + 0.5, 0.0, 1.0) * glow)
col = mix(ramp2, cWhite, clamp(plasma * 2.0 - 0.8, 0.0, 1.0))
alpha = 1.0
""",
        "glsl": """
      precision highp float;
      uniform vec2 res;
      uniform float t;
      uniform float aspect;
      uniform float audio_bass;
      uniform float audio_mid;
      uniform float audio_high;

      void main() {
        vec2 uv = gl_FragCoord.xy / res;
        vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.4;
        float r = length(p);
        float ang = atan(p.y, p.x);
        float time = t * (0.85 + audio_bass * 1.5);

        float fx = 4.5 + audio_mid * 2.5;
        float fy = 4.0 + audio_mid * 2.0;

        float w1 = sin(p.x * fx + time * 1.6);
        float w2 = sin(p.y * fy - time * 1.3);
        float w3 = sin((p.x + p.y) * (fx * 0.7) + time * 1.1);
        float w4 = sin(r * (fy * 1.2) - time * 2.4 + ang * (1.35 + audio_bass * 1.2));

        float plasma = (w1 + w2 + w3 + w4) * 0.25;

        vec3 cBlack = vec3(0.02, 0.02, 0.04);
        vec3 cFire = vec3(1.0, 0.23, 0.0);
        vec3 cCyan = vec3(0.0, 0.88, 1.0);
        vec3 cWhite = vec3(1.0, 1.0, 1.0);

        vec3 ramp1 = mix(cBlack, cFire, clamp(plasma * 1.2 + 0.5, 0.0, 1.0));
        vec3 ramp2 = mix(ramp1, cCyan, clamp(sin(plasma * 6.2831853 + time) * 0.5 + 0.5, 0.0, 1.0) * (0.8 + audio_high * 0.8));
        vec3 col = mix(ramp2, cWhite, clamp(plasma * 2.0 - 0.8 + audio_high * 0.5, 0.0, 1.0));

        gl_FragColor = vec4(col, 1.0);
      }
"""
    },
    {
        "slug": "demoscene-plasma-rotozoom-bloom",
        "title": "Demoscene Plasma Rotozoom Bloom",
        "description": "Amiga style rotozoom plasma shader with rotating spiral coordinates, chromatic wave interference, and intense thermal bloom.",
        "embed_desc": "Amiga style rotozoom plasma shader with rotating spiral coordinates, chromatic wave interference, and intense thermal bloom. Bass drives dynamic coordinate rotation and zoom breathing, mids expand radial spiral interference bands, highs spark blazing white-hot thermal blooms.",
        "tags": ["pixel", "demoscene", "rotozoom", "plasma", "shader", "bloom"],
        "params": {
            "rotSpeed": 0.65,
            "zoom": 2.8,
            "harmonics": 5.0,
            "thermalBloom": 0.75,
            "phaseShift": 0.5
        },
        "code": """param float rotSpeed = 0.65 [0.1, 2.5]
param float zoom = 2.8 [0.5, 6.0]
param float harmonics = 5.0 [2.0, 10.0]
param float thermalBloom = 0.75 [0.1, 1.5]
param float phaseShift = 0.5 [0.0, 1.0]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5)
r = length(p)
ang = atan2(p.y, p.x)
time = t * rotSpeed

rotAng = ang + time * 0.8
pRot = vec2(cos(rotAng), sin(rotAng)) * r * zoom

v1 = sin(pRot.x * harmonics + time * 2.0)
v2 = cos(pRot.y * harmonics - time * 1.6)
v3 = sin((pRot.x * cos(time * 0.5) + pRot.y * sin(time * 0.5)) * 4.0)
v4 = cos(r * 12.0 - time * 3.0)

wave = (v1 + v2 + v3 + v4) * 0.25

cDark = vec3(0.01, 0.01, 0.03)
cOrange = vec3(1.0, 0.35, 0.02)
cElectricCyan = vec3(0.0, 0.9, 1.0)
cWhiteHot = vec3(1.0, 1.0, 1.0)

m1 = mix(cDark, cOrange, clamp(wave * 0.8 + 0.5, 0.0, 1.0))
m2 = mix(m1, cElectricCyan, clamp(sin(wave * 4.0 + r * 6.0 + phaseShift * 3.14) * 0.5 + 0.5, 0.0, 1.0) * 0.85)
col = mix(m2, cWhiteHot, clamp(cos(r * 8.0 - time * 2.0) * 0.5 + 0.5, 0.0, 1.0) * thermalBloom * clamp(wave + 0.5, 0.0, 1.0))
alpha = 1.0
""",
        "glsl": """
      precision highp float;
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
        float time = t * (0.65 + audio_bass * 1.4);

        float rotAng = ang + time * (0.8 + audio_bass * 1.2);
        vec2 pRot = vec2(cos(rotAng), sin(rotAng)) * r * (2.8 + audio_bass * 1.8);

        float harm = 5.0 + audio_mid * 3.0;
        float v1 = sin(pRot.x * harm + time * 2.2);
        float v2 = cos(pRot.y * harm - time * 1.8);
        float v3 = sin((pRot.x * cos(time * 0.5) + pRot.y * sin(time * 0.5)) * 4.0);
        float v4 = cos(r * (12.0 + audio_mid * 6.0) - time * 3.2);

        float wave = (v1 + v2 + v3 + v4) * 0.25;

        vec3 cDark = vec3(0.01, 0.01, 0.03);
        vec3 cOrange = vec3(1.0, 0.35, 0.02);
        vec3 cElectricCyan = vec3(0.0, 0.9, 1.0);
        vec3 cWhiteHot = vec3(1.0, 1.0, 1.0);

        vec3 m1 = mix(cDark, cOrange, clamp(wave * 0.8 + 0.5, 0.0, 1.0));
        vec3 m2 = mix(m1, cElectricCyan, clamp(sin(wave * 4.0 + r * 6.0 + 1.57) * 0.5 + 0.5, 0.0, 1.0) * (0.85 + audio_high * 0.6));
        vec3 col = mix(m2, cWhiteHot, clamp(cos(r * 8.0 - time * 2.0) * 0.5 + 0.5, 0.0, 1.0) * (0.75 + audio_high * 1.0) * clamp(wave + 0.5, 0.0, 1.0));

        gl_FragColor = vec4(col, 1.0);
      }
"""
    }
]

# -------------------------------------------------------------
# HTML Embed Template
# -------------------------------------------------------------
def make_embed_html(item):
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
  <meta name="theme-color" content="#151930">
  <title>{item['title']}: audio-reactive visual | Infinite</title>
  <meta name="description" content="{item['embed_desc']} Free to embed, made in Infinite by Naman Soni.">
  <link rel="canonical" href="https://infinitedevices.org/embeds/{item['slug']}.html">
  <meta property="og:type" content="website">
  <meta property="og:title" content="{item['title']}: audio-reactive visual">
  <meta property="og:description" content="{item['embed_desc']}">
  <meta property="og:url" content="https://infinitedevices.org/embeds/{item['slug']}.html">
  <meta property="og:image" content="https://infinitedevices.org/assets/og-image.jpg">
  <meta name="twitter:card" content="summary_large_image">
  <style>
    /* Generated by tools/website/gen_embeds.py. Midnight ground, glass badge, pill controls, no outlines. */
    :root {{ --ground: #151930; --glass: rgba(32, 38, 63, 0.78); --ink: #EEF1FA; --ember: #FF6B35; --signal: #A7B1FF; --shadow: rgba(4, 5, 14, 0.45); }}
    * {{ box-sizing: border-box; margin: 0; padding: 0; }}
    html, body {{ width: 100%; height: 100%; overflow: hidden; background: var(--ground); font-family: Geist, system-ui, -apple-system, "Segoe UI", sans-serif; }}
    canvas {{ width: 100%; height: 100%; display: block; }}
    .badge, .btn {{
      position: absolute; bottom: 14px; z-index: 10; display: inline-flex; align-items: center; gap: 8px; min-height: 40px; padding: 0 16px;
      border-radius: 999px; background: var(--glass); color: var(--ink); font-size: 11.5px; font-weight: 500; text-decoration: none;
      -webkit-backdrop-filter: blur(14px) saturate(1.5); backdrop-filter: blur(14px) saturate(1.5); box-shadow: 0 6px 16px var(--shadow);
      transition: transform var(--dur-small) var(--ease-out), background-color var(--dur-small) var(--ease-out);
    }}
    .badge {{ right: 14px; width: 44px; padding: 0; justify-content: center; }}
    .badge strong {{ font-weight: 700; }}
    .badge-icon {{ width: 16px; height: 16px; border-radius: 50%; background: linear-gradient(135deg, var(--ember), var(--signal)); }}
    .controls {{ position: absolute; left: 14px; bottom: 14px; z-index: 10; }}
    .controls .btn {{ position: static; border: 0; cursor: pointer; font-family: inherit; min-height: 44px; width: 44px; padding: 0; justify-content: center; }}
    .controls .btn.on {{ background: var(--ember); color: #151930; }}
    .controls .btn.off {{ color: var(--muted, #8890B1); }}
    .badge:hover, .btn:hover {{ transform: translateY(-2px); background: rgba(52, 59, 94, 0.9); }}
    .badge:focus-visible, .btn:focus-visible {{ outline: 2px solid var(--ember); outline-offset: 2px; }}
    @media (prefers-reduced-motion: reduce) {{ .badge, .btn {{ transition: none; }} .badge:hover, .btn:hover {{ transform: none; }} }}
    .sr-only {{ position: absolute; width: 1px; height: 1px; overflow: hidden; clip: rect(0 0 0 0); white-space: nowrap; }}
    .fallback {{ color: var(--ink); padding: 24px; font-size: 13.5px; }}
  </style>
</head>
<body>
  <main>
    <h1 class="sr-only">{item['title']}</h1>
    <canvas id="gl" role="img" aria-label="{item['title']}, an audio-reactive visual"></canvas>

  <div class="controls">
    <button class="btn" id="btn-audio" type="button" aria-label="Enable microphone" title="Enable microphone"><svg viewBox="0 0 20 20" width="20" height="20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="7.5" y="2.5" width="5" height="9" rx="2.5"/><path d="M4.5 9.5a5.5 5.5 0 0 0 11 0M10 15v2.5"/></svg></button>
  </div>

  <a href="https://infinitedevices.org/" target="_blank" rel="noopener" class="badge" title="Infinite, a DAW for you">
    <span class="badge-icon" aria-hidden="true"></span>
    <span class="sr-only">made in infinite</span>
  </a>
  </main>

  <script>
    const canvas = document.getElementById("gl");
    const gl = canvas.getContext("webgl");
    if (!gl) {{
      document.body.innerHTML = '<p class="fallback">This visual needs WebGL, which this browser does not offer.</p>';
    }}

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
      return shader;
    }}

    const prog = gl.createProgram();
    gl.attachShader(prog, createShader(gl, gl.VERTEX_SHADER, vsSource));
    gl.attachShader(prog, createShader(gl, gl.FRAGMENT_SHADER, fsSource));
    gl.linkProgram(prog);
    gl.useProgram(prog);

    const posBuf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, posBuf);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1,-1, 1,-1, -1,1, -1,1, 1,-1, 1,1]), gl.STATIC_DRAW);

    const posLoc = gl.getAttribLocation(prog, "position");
    gl.enableVertexAttribArray(posLoc);
    gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 0, 0);

    const uRes = gl.getUniformLocation(prog, "res");
    const uT = gl.getUniformLocation(prog, "t");
    const uAspect = gl.getUniformLocation(prog, "aspect");
    const uBass = gl.getUniformLocation(prog, "audio_bass");
    const uMid = gl.getUniformLocation(prog, "audio_mid");
    const uHigh = gl.getUniformLocation(prog, "audio_high");

    let audioCtx = null, analyser = null, dataArray = null;
    let bass = 0, mid = 0, high = 0;

    const btnAudio = document.getElementById("btn-audio");
    btnAudio.addEventListener("click", async () => {{
      if (!audioCtx) {{
        try {{
          const stream = await navigator.mediaDevices.getUserMedia({{ audio: true }});
          audioCtx = new (window.AudioContext || window.webkitAudioContext)();
          const src = audioCtx.createMediaStreamSource(stream);
          analyser = audioCtx.createAnalyser();
          analyser.fftSize = 256;
          dataArray = new Uint8Array(analyser.frequencyBinCount);
          src.connect(analyser);
          btnAudio.classList.add("on");
          btnAudio.setAttribute("aria-label", "Microphone on");
        }} catch(e) {{
          btnAudio.classList.add("off");
          btnAudio.setAttribute("aria-label", "Microphone blocked");
        }}
      }}
    }});

    function resize() {{
      const dpr = Math.min(window.devicePixelRatio || 1, 2);
      const w = Math.floor(window.innerWidth * dpr);
      const h = Math.floor(window.innerHeight * dpr);
      if (canvas.width !== w || canvas.height !== h) {{
        canvas.width = w;
        canvas.height = h;
        gl.viewport(0, 0, w, h);
      }}
    }}
    window.addEventListener("resize", resize);
    resize();

    let start = performance.now();
    function render() {{
      resize();
      const now = performance.now();
      const t = (now - start) * 0.001;

      if (analyser && dataArray) {{
        analyser.getByteFrequencyData(dataArray);
        let b = 0, m = 0, h = 0;
        for (let i = 0; i < 8; i++) b += dataArray[i];
        for (let i = 8; i < 32; i++) m += dataArray[i];
        for (let i = 32; i < 96; i++) h += dataArray[i];
        bass = bass * 0.85 + (b / (8 * 255)) * 0.15;
        mid = mid * 0.85 + (m / (24 * 255)) * 0.15;
        high = high * 0.85 + (h / (64 * 255)) * 0.15;
      }}

      gl.uniform2f(uRes, canvas.width, canvas.height);
      gl.uniform1f(uT, t);
      gl.uniform1f(uAspect, canvas.width / canvas.height);
      gl.uniform1f(uBass, bass);
      gl.uniform1f(uMid, mid);
      gl.uniform1f(uHigh, high);

      gl.drawArrays(gl.TRIANGLES, 0, 6);
      requestAnimationFrame(render);
    }}
    requestAnimationFrame(render);
  </script>
</body>
</html>"""

def main():
    report = []
    failed = False

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

        v = subprocess.run([BIN, "--validate", test_inf], capture_output=True, text=True)
        if "ok\":true" not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}")
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        f_proc = subprocess.run([BIN, "--frame", test_inf, "0", out_frame_dir + "/", "--size", "1080x1080"], capture_output=True, text=True)
        if "ok\":true" not in f_proc.stdout:
            print(f"FAIL frame: {slug}\n{f_proc.stdout}")
            failed = True
            continue

        frame_png = os.path.join(out_frame_dir, "frame_00000.png")
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
  c bg 0.88 0.90 0.93
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

        v = subprocess.run([BIN, "--validate", test_inf], capture_output=True, text=True)
        if "ok\":true" not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}")
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        f_proc = subprocess.run([BIN, "--frame", test_inf, "0", out_frame_dir + "/", "--size", "1080x1080"], capture_output=True, text=True)
        if "ok\":true" not in f_proc.stdout:
            print(f"FAIL frame: {slug}\n{f_proc.stdout}")
            failed = True
            continue

        frame_png = os.path.join(out_frame_dir, "frame_00000.png")
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
  i note2 {nr+3}
  i note3 {nr+3}
  i note4 {nr+7}
  i note5 {nr+7}
  i note6 {nr+5}
  i note7 {nr+5}
  i note8 {nr}
  i note9 {nr}
  i note10 {nr+3}
  i note11 {nr+3}
  i note12 {nr+8}
  i note13 {nr+8}
  i note14 {nr+7}
  i note15 {nr+5}
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
  pos 1020 1024
end
node 4 Modulators Audio Analyze
  id ana
  pos 0 2200
end
node 5 Source Shape
  id pic
  pos 0 162
  c fillColor 0.12 0.35 0.58
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

        v = subprocess.run([BIN, "--validate", inf_file], capture_output=True, text=True)
        if "ok\":true" not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}")
            failed = True
            continue

        sum_json = os.path.join(TMP_DIR, f"{slug}_sum.json")
        s = subprocess.run([BIN, "--audio-summary", inf_file, sum_json, "--duration", "6"], capture_output=True, text=True)
        if "ok\":true" not in s.stdout or not os.path.exists(sum_json):
            print(f"FAIL audio summary command: {slug}\n{s.stdout}")
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
        f_proc = subprocess.run([BIN, "--frame", inf_file, "0", out_frame_dir + "/", "--size", "1080x1080"], capture_output=True, text=True)
        frame_png = os.path.join(out_frame_dir, "frame_00000.png")
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
  f cutoff 3000.0
  f resonance 0.35
  f decay 0.22
  f volume 0.55
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
  c fillColor 0.45 0.15 0.22
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

        v = subprocess.run([BIN, "--validate", inf_file], capture_output=True, text=True)
        if "ok\":true" not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}")
            failed = True
            continue

        sum_json = os.path.join(TMP_DIR, f"{slug}_sum.json")
        s = subprocess.run([BIN, "--audio-summary", inf_file, sum_json, "--duration", "6"], capture_output=True, text=True)
        if "ok\":true" not in s.stdout or not os.path.exists(sum_json):
            print(f"FAIL audio summary command: {slug}\n{s.stdout}")
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
        f_proc = subprocess.run([BIN, "--frame", inf_file, "0", out_frame_dir + "/", "--size", "1080x1080"], capture_output=True, text=True)
        frame_png = os.path.join(out_frame_dir, "frame_00000.png")
        im = Image.open(frame_png)

        prev_jpg = os.path.join(PREVIEWS_DIR, f"{slug}.jpg")
        im.convert("RGB").save(prev_jpg, quality=94)

        thumb_webp = os.path.join(THUMBS_DIR, f"{slug}.webp")
        im.convert("RGB").resize((540, 540), Image.Resampling.LANCZOS).save(thumb_webp, "WEBP", quality=85)

        tmpl_webp = os.path.join(TEMPLATES_DIR, f"{slug}.webp")
        template_preview.render(inf_file, tmpl_webp)

        report.append(f"PASS [Field Effects] {slug}: lufs={lufs:.2f}, peak={peak:.2f}dBFS, silence={silence}")

    # ---------------------------------------------------------
    # Process Family 5: Field Pixel & WebGL Embeds
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
                "nodeSettings": {"width": 1080.0, "height": 1080.0, "animate": 1.0}
            }
        }
        field_file = os.path.join(PIXEL_DIR, f"{slug}.field")
        with open(field_file, "w") as f:
            json.dump(field_dev, f, indent=2)
        it["bytes"] = os.path.getsize(field_file)

        embed_html = make_embed_html(it)
        embed_file = os.path.join(EMBEDS_DIR, f"{slug}.html")
        with open(embed_file, "w") as f:
            f.write(embed_html)

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

        v = subprocess.run([BIN, "--validate", test_inf], capture_output=True, text=True)
        if "ok\":true" not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}")
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        f_proc = subprocess.run([BIN, "--frame", test_inf, "0", out_frame_dir + "/", "--size", "1080x1080"], capture_output=True, text=True)
        if "ok\":true" not in f_proc.stdout:
            print(f"FAIL frame: {slug}\n{f_proc.stdout}")
            failed = True
            continue

        frame_png = os.path.join(out_frame_dir, "frame_00000.png")
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
                "domain": "sketch-3d",
                "category": "Sketch 3D",
                "params": 5,
                "file": f"assets/library/sketch/{it['slug']}.js",
                "preview": f"assets/library/previews/{it['slug']}.jpg",
                "bytes": it["bytes"]
            })

    for it in SYNTH_ITEMS:
        if it["slug"] not in existing_lib_ids:
            lib_data["items"].append({
                "id": it["slug"],
                "name": it["title"],
                "kind": "Synth",
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
