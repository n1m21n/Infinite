#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 40.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: op_art -> op-art-riley-perceptual-moire-grid, op-art-vasarely-hyperbolic-deformation)
2. Sketch 3D (Theme: brutalist_architecture -> brutalist-architecture-spomenik-monolithic-pylon-3d, brutalist-architecture-cantilever-colonnade-citadel-3d)
3. Field Synth (Theme: idm -> idm-crystalline-granular-bell-synth, idm-algorithmic-drift-reese-synth)
4. Field Effects (Theme: breakbeat -> breakbeat-transient-saturation-drive-fx, breakbeat-syncopated-tape-echo-fx)
5. Field Pixel & Embeds (Theme: analog_crt -> analog-crt-aperture-grille-phosphor-bloom, analog-crt-electron-beam-raster-deflection)
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

TMP_DIR = "/tmp/pack40_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: op_art)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "op-art-riley-perceptual-moire-grid",
        "title": "Op Art Riley Perceptual Moiré Grid",
        "description": "Perceptual optical illusion drafting system rendering high-contrast moiré interference bands through overlapping sinusoidal lines and mathematical wave displacement.",
        "tags": ["op-art", "bridget-riley", "moire", "wave", "optical", "procedural", "contrast"],
        "code": """// Op Art Riley Perceptual Moire Grid - Infinite Library (Sketch 2D)
param("frequency", 28.0, 12.0, 60.0);
param("amplitude", 36.0, 10.0, 80.0);
param("line_weight", 1.8, 0.8, 4.0);
param("contrast", 1.25, 0.8, 1.8);

function draw(t) {
  // Deep ivory background (#f8f8f5)
  background(0.972, 0.972, 0.960);

  const w = width;
  const h = height;
  const margin = 64;
  const innerW = w - margin * 2;
  const innerH = h - margin * 2;

  // Outer framing boundary
  stroke(0.05, 0.05, 0.06);
  strokeWeight(line_weight * 1.5);
  noFill();
  rect(margin, margin, innerW, innerH);

  // Optical wave field: dense horizontal sinusoidal ribbons creating moire vibration
  const numLines = floor(frequency * 2.2);
  const dy = innerH / numLines;

  for (let i = 0; i < numLines; i++) {
    const y0 = margin + i * dy;
    const normI = i / numLines;

    // Bridget Riley undulating wave displacement
    const phaseShift = normI * Math.PI * 4.0;
    const amp = amplitude * Math.sin(normI * Math.PI);

    // Dynamic stroke weight modulation creating compression/dilation banding
    const sw = (line_weight * 0.8) + Math.abs(Math.sin(normI * Math.PI * 3.0)) * line_weight * 1.6;
    strokeWeight(sw);
    stroke(0.04, 0.04, 0.05);
    noFill();

    // Render continuous wave path
    const steps = 90;
    const dx = innerW / steps;
    let prevX = margin;
    let prevY = y0 + Math.sin(phaseShift) * amp;

    for (let s = 1; s <= steps; s++) {
      const curX = margin + s * dx;
      const wave = Math.sin((s / steps) * Math.PI * 6.0 + phaseShift);
      const wave2 = Math.cos((s / steps) * Math.PI * 3.0 - phaseShift * 0.5);
      const curY = y0 + (wave * 0.7 + wave2 * 0.3) * amp;
      line(prevX, prevY, curX, curY);
      prevX = curX;
      prevY = curY;
    }
  }

  // Vertical intersecting counter-phase interference lines (creating moire diamonds)
  strokeWeight(line_weight * 0.9);
  stroke(0.04, 0.04, 0.05, 0.75);
  const numV = 24;
  const dv = innerW / numV;
  for (let j = 0; j <= numV; j++) {
    const vx = margin + j * dv;
    const vPhase = (j / numV) * Math.PI * 3.0;
    const vAmp = amplitude * 0.45;
    let px = vx + Math.sin(vPhase) * vAmp;
    let py = margin;
    for (let s = 1; s <= 60; s++) {
      const cy = margin + (s / 60) * innerH;
      const cx = vx + Math.sin((s / 60) * Math.PI * 4.0 + vPhase) * vAmp;
      line(px, py, cx, cy);
      px = cx;
      py = cy;
    }
  }

  // Optical focal diamond tension accents
  noStroke();
  fill(0.04, 0.04, 0.05);
  const cx = w * 0.5;
  const cy = h * 0.5;
  ellipse(cx, cy, 24, 24);
  stroke(0.972, 0.972, 0.960);
  strokeWeight(2.0);
  line(cx - 16, cy, cx + 16, cy);
  line(cx, cy - 16, cx, cy + 16);
}
"""
    },
    {
        "slug": "op-art-vasarely-hyperbolic-deformation",
        "title": "Op Art Vasarely Hyperbolic Deformation",
        "description": "Algorithmic Vasarely-inspired optical art lattice producing spherical bulge distortion across an undulating alternating checkerboard field.",
        "tags": ["op-art", "vasarely", "hyperbolic", "checkerboard", "distortion", "procedural"],
        "code": """// Op Art Vasarely Hyperbolic Deformation - Infinite Library (Sketch 2D)
param("grid_size", 22.0, 12.0, 36.0);
param("bulge_radius", 360.0, 150.0, 500.0);
param("bulge_power", 1.8, 0.6, 3.2);
param("contrast", 1.2, 0.8, 1.8);

function draw(t) {
  // Midnight ground (#0c0d12)
  background(0.047, 0.051, 0.071);

  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const margin = 64;
  const innerW = w - margin * 2;
  const innerH = h - margin * 2;

  // Framing boundary
  stroke(0.92, 0.92, 0.95);
  strokeWeight(2.0);
  noFill();
  rect(margin, margin, innerW, innerH);

  // Hyperbolic spherical transformation function
  function warpPoint(px, py) {
    const dx = px - cx;
    const dy = py - cy;
    const dist = Math.sqrt(dx * dx + dy * dy);
    if (dist < bulge_radius && dist > 0.001) {
      const normDist = dist / bulge_radius;
      const factor = Math.sin((normDist * Math.PI) * 0.5);
      const warpedDist = Math.pow(factor, 1.0 / bulge_power) * bulge_radius;
      const scale = warpedDist / dist;
      return [cx + dx * scale, cy + dy * scale];
    }
    return [px, py];
  }

  const n = floor(grid_size);
  const stepX = innerW / n;
  const stepY = innerH / n;

  noStroke();
  for (let y = 0; y < n; y++) {
    for (let x = 0; x < n; x++) {
      const p0 = warpPoint(margin + x * stepX, margin + y * stepY);
      const p1 = warpPoint(margin + (x + 1) * stepX, margin + y * stepY);
      const p2 = warpPoint(margin + (x + 1) * stepX, margin + (y + 1) * stepY);
      const p3 = warpPoint(margin + x * stepX, margin + (y + 1) * stepY);

      const isEven = (x + y) % 2 === 0;
      if (isEven) {
        fill(0.95, 0.95, 0.97);
      } else {
        const cellDist = Math.hypot(p0[0] - cx, p0[1] - cy);
        if (cellDist < bulge_radius * 0.65) {
          fill(0.92, 0.22, 0.16); // Optical vermilion accent (#EB3829)
        } else {
          fill(0.12, 0.13, 0.18); // Deep charcoal
        }
      }

      triangle(p0[0], p0[1], p1[0], p1[1], p2[0], p2[1]);
      triangle(p0[0], p0[1], p2[0], p2[1], p3[0], p3[1]);
    }
  }

  // Precision concentric graticule rings
  stroke(0.95, 0.95, 0.97, 0.65);
  strokeWeight(1.2);
  noFill();
  ellipse(cx, cy, bulge_radius * 0.7, bulge_radius * 0.7);
  ellipse(cx, cy, bulge_radius * 1.4, bulge_radius * 1.4);
  ellipse(cx, cy, bulge_radius * 2.0, bulge_radius * 2.0);

  // Crosshair focal alignment
  line(cx - 36, cy, cx + 36, cy);
  line(cx, cy - 36, cx, cy + 36);
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: brutalist_architecture)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "brutalist-architecture-spomenik-monolithic-pylon-3d",
        "title": "Brutalist Architecture Spomenik Monolithic Pylon 3D",
        "description": "Monolithic concrete Spomenik monument monumentally towering with cantilevered faceted pylons, stepped concrete slabs, and heavy architectural shadow masses.",
        "tags": ["brutalist", "architecture", "concrete", "spomenik", "monolith", "pylon", "3d"],
        "code": """// Brutalist Architecture Spomenik Monolithic Pylon 3D - Infinite Library (Sketch 3D)
param("spin", 0.35, 0.1, 1.2);
param("scale_factor", 1.2, 0.5, 2.2);
param("pylon_count", 6.0, 3.0, 12.0);
param("cantilever", 1.3, 0.6, 2.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.48 + sin(time * 0.2) * 0.06);
  rotateY(time * 0.35 + 0.6);

  // Central stepped monolithic concrete plinth core (#7a7a78)
  fill(0.48, 0.48, 0.47);
  box(0.52 * scale_factor, 0.72 * scale_factor, 0.52 * scale_factor);

  // Heavy stepped cantilevered base tiers
  for (let b = 1; b <= 3; b++) {
    push();
    translate(0, -0.36 * scale_factor - b * 0.10 * scale_factor, 0);
    fill(0.32 - b * 0.04, 0.32 - b * 0.04, 0.31 - b * 0.04);
    box((0.52 + b * 0.35) * scale_factor, 0.09 * scale_factor, (0.52 + b * 0.35) * scale_factor);
    pop();
  }

  // Radial cantilevered monolithic pylon fins (Spomenik wing structures)
  const nPylons = floor(pylon_count);
  for (let i = 0; i < nPylons; i++) {
    const angle = (i / nPylons) * Math.PI * 2.0;
    push();
    rotateY(angle);
    translate(0.55 * scale_factor * cantilever, 0.15 * scale_factor, 0);
    rotateZ(0.35);

    if (i % 2 === 0) {
      fill(0.85, 0.84, 0.81); // Warm stone / pale concrete (#d9d6cf)
    } else {
      fill(0.42, 0.42, 0.41); // Raw weathered aggregate concrete (#6b6b69)
    }

    box(0.18 * scale_factor, 0.85 * scale_factor, 0.38 * scale_factor);

    push();
    translate(0, 0.45 * scale_factor, 0);
    fill(0.24, 0.24, 0.24);
    box(0.32 * scale_factor, 0.12 * scale_factor, 0.46 * scale_factor);
    pop();

    pop();
  }

  // Top structural crown slab
  push();
  translate(0, 0.42 * scale_factor, 0);
  fill(0.85, 0.84, 0.81);
  box(0.68 * scale_factor, 0.08 * scale_factor, 0.68 * scale_factor);
  pop();

  pop();
}
"""
    },
    {
        "slug": "brutalist-architecture-cantilever-colonnade-citadel-3d",
        "title": "Brutalist Architecture Cantilever Colonnade Citadel 3D",
        "description": "Towering stepped brutalist citadel with interlocking cantilevered concrete decks, raw modular colonnades, and deep architectural chiaroscuro.",
        "tags": ["brutalist", "architecture", "concrete", "cantilever", "colonnade", "citadel", "3d"],
        "code": """// Brutalist Architecture Cantilever Colonnade Citadel 3D - Infinite Library (Sketch 3D)
param("spin", 0.30, 0.1, 1.2);
param("scale_factor", 1.15, 0.5, 2.2);
param("tiers", 5.0, 3.0, 8.0);
param("column_density", 8.0, 4.0, 16.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.52 + sin(time * 0.18) * 0.05);
  rotateY(time * 0.32 + 0.85);

  const nTiers = floor(tiers);
  const nCols = floor(column_density);

  for (let k = 0; k < nTiers; k++) {
    const yOffset = (k - nTiers * 0.5) * 0.32 * scale_factor;
    const tierWidth = (1.45 - k * 0.18) * scale_factor;
    const tierDepth = (1.45 - k * 0.18) * scale_factor;

    push();
    translate(0, yOffset, 0);

    if (k % 2 === 0) {
      fill(0.84, 0.83, 0.80); // Pale cast concrete (#d6d4cc)
    } else {
      fill(0.48, 0.48, 0.46); // Raw aggregate grey (#7a7a75)
    }
    box(tierWidth, 0.07 * scale_factor, tierDepth);

    if (k < nTiers - 1) {
      const colHeight = 0.25 * scale_factor;
      for (let c = 0; c < nCols; c++) {
        const frac = c / nCols;
        const colAngle = frac * Math.PI * 2.0;
        const colRadius = (tierWidth * 0.42);

        push();
        translate(Math.cos(colAngle) * colRadius, colHeight * 0.5, Math.sin(colAngle) * colRadius);
        fill(0.26, 0.26, 0.26); // Deep shadow concrete pillar (#424242)
        box(0.08 * scale_factor, colHeight, 0.08 * scale_factor);
        pop();
      }

      push();
      translate(0, colHeight * 0.5, 0);
      fill(0.38, 0.38, 0.37);
      box(tierWidth * 0.45, colHeight, tierDepth * 0.45);
      pop();
    }
    pop();
  }

  for (let b = 0; b < 4; b++) {
    const angle = b * Math.PI * 0.5 + 0.3;
    push();
    rotateY(angle);
    translate(0.85 * scale_factor, 0.05 * scale_factor, 0);
    fill(0.88, 0.87, 0.84);
    box(0.45 * scale_factor, 0.06 * scale_factor, 0.32 * scale_factor);
    pop();
  }

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: idm)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "idm-crystalline-granular-bell-synth",
        "title": "IDM Crystalline Granular Bell Synth",
        "description": "Micro-tuned crystalline FM bell synthesizer featuring non-harmonic golden-ratio modulator ratios, shimmering high-frequency dispersion, and exponential decay envelopes.",
        "note_root": 60, # C3
        "params": {
            "fmRatio": 2.618,
            "fmDepth": 1.6,
            "brightness": 2800.0,
            "shimmer": 0.40,
            "decay": 0.45
        },
        "code": """param float fmRatio = 2.618 [1.0, 6.0];
param float fmDepth = 1.6 [0.0, 4.0];
param float brightness = 2800.0 [400.0, 8000.0];
param float shimmer = 0.40 [0.0, 1.0];
param float decay = 0.45 [0.1, 1.2];
state float pCarrier = 0;
state float pMod = 0;
state float pShimmer = 0;
state float env = 0;
state float lp = 0;

target = if(gate > 0.5, 1.0, 0.0);
decayRate = clamp(1.0 / (sr * decay), 0.0001, 0.05);
env = if(target > env, env + (target - env) * 0.15, env - env * decayRate);

fCarrier = clamp(freq, 20.0, 4000.0);
fMod = fCarrier * fmRatio;
fShimmer = fCarrier * 4.0;

stepCarrier = fCarrier / sr;
stepMod = fMod / sr;
stepShimmer = fShimmer / sr;

pCarrier = (pCarrier + stepCarrier) % 1.0;
pMod = (pMod + stepMod) % 1.0;
pShimmer = (pShimmer + stepShimmer) % 1.0;

modSig = sin(6.2831853 * pMod) * fmDepth * env;
carrierSig = sin(6.2831853 * (pCarrier + modSig));
shimSig = sin(6.2831853 * pShimmer) * shimmer * env;

raw = carrierSig * 0.75 + shimSig * 0.25;

fCut = clamp(brightness / sr * 3.14, 0.01, 0.45);
lp = lp + fCut * (raw - lp);

out = lp * env * 0.32;
"""
    },
    {
        "slug": "idm-algorithmic-drift-reese-synth",
        "title": "IDM Algorithmic Drift Reese Synth",
        "description": "Complex drifting IDM bass synthesizer featuring phase-jittered dual detuned saw/pulse oscillators, tape-flutter pitch modulation, and soft asymmetric saturation.",
        "note_root": 36, # C1
        "params": {
            "cutoff": 950.0,
            "detune": 0.025,
            "flutter": 0.35,
            "drive": 2.0,
            "decay": 0.40
        },
        "code": """param float cutoff = 950.0 [100.0, 3500.0];
param float detune = 0.025 [0.005, 0.08];
param float flutter = 0.35 [0.0, 1.0];
param float drive = 2.0 [1.0, 5.0];
param float decay = 0.40 [0.1, 1.0];
state float p1 = 0;
state float p2 = 0;
state float lfo = 0;
state float env = 0;
state float lp1 = 0;
state float lp2 = 0;

target = if(gate > 0.5, 1.0, 0.0);
decayRate = clamp(1.0 / (sr * decay), 0.0001, 0.05);
env = if(target > env, env + (target - env) * 0.12, env - env * decayRate);

lfo = (lfo + 3.2 / sr) % 1.0;
drift = sin(6.2831853 * lfo) * flutter * 0.008;

baseF = clamp(freq, 20.0, 1200.0);
step1 = (baseF * (1.0 - detune + drift)) / sr;
step2 = (baseF * (1.0 + detune - drift)) / sr;

p1 = (p1 + step1) % 1.0;
p2 = (p2 + step2) % 1.0;

saw1 = 2.0 * p1 - 1.0;
saw2 = 2.0 * p2 - 1.0;
subWave = sin(6.2831853 * p1) * 0.35;

raw = (saw1 + saw2) * 0.40 + subWave * 0.30;
u = raw * drive;
sat = u / (1.0 + abs(u) * 0.35);

fCut = clamp((cutoff + env * 1200.0) / sr * 3.14, 0.01, 0.45);
lp1 = lp1 + fCut * (sat - lp1);
lp2 = lp2 + fCut * (lp1 - lp2);

out = lp2 * (0.35 + env * 0.65) * 0.20;
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: breakbeat)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "breakbeat-transient-saturation-drive-fx",
        "title": "Breakbeat Transient Saturation Drive FX",
        "description": "Dynamic breakbeat drum bus saturator featuring punchy transient envelope detection, analog diode clipping, and low-end warmth emphasis.",
        "params": {
            "drive": 2.4,
            "punch": 0.65,
            "warmth": 0.55,
            "mix": 0.75
        },
        "code": """param float drive = 2.4 [1.0, 6.0];
param float punch = 0.65 [0.0, 1.0];
param float warmth = 0.55 [0.0, 1.0];
param float mix = 0.75 [0.0, 1.0];
state float env = 0;
state float lp = 0;

sigIn = in;
inAbs = abs(sigIn);
env = if(inAbs > env, env + (inAbs - env) * 0.25, env - env * 0.005);

gain = 1.0 + env * punch * 2.0;
boosted = sigIn * gain * drive;

sat = boosted / (1.0 + abs(boosted) * 0.35);

fWarm = clamp(1800.0 / sr * 3.14, 0.01, 0.45);
lp = lp + fWarm * (sat - lp);
shaped = sat * (1.0 - warmth * 0.4) + lp * (warmth * 0.4);

processed = sigIn * (1.0 - mix) + shaped * mix * 0.85;
out = processed * 0.88;
"""
    },
    {
        "slug": "breakbeat-syncopated-tape-echo-fx",
        "title": "Breakbeat Syncopated Tape Echo FX",
        "description": "Tempo-synced syncopated ping-pong tape echo featuring dotted-eighth delay taps, tape flutter modulation, and high-frequency saturation in the feedback loop.",
        "params": {
            "time": 0.38,
            "flutter": 0.30,
            "feedback": 0.48,
            "damping": 0.55,
            "mix": 0.50
        },
        "code": """param float time = 0.38 [0.05, 0.8];
param float flutter = 0.30 [0.0, 1.0];
param float feedback = 0.48 [0.0, 0.85];
param float damping = 0.55 [0.1, 1.0];
param float mix = 0.50 [0.0, 1.0];
state float pLfo = 0;
state float tap1 = 0;
state float tap2 = 0;
state float tap3 = 0;
state float tap4 = 0;
state float lp = 0;

pLfo = (pLfo + 2.4 / sr) % 1.0;
wobble = sin(pLfo * 6.2831853) * flutter * 0.025;

fCut = clamp(0.12 + (1.0 - damping) * 0.28, 0.02, 0.45);
lp = lp + fCut * (in - lp);

rate = clamp(0.09 + wobble, 0.03, 0.35);
tap1 = tap1 + rate * (lp + tap4 * feedback * 0.42 - tap1);
tap2 = tap2 + rate * 0.86 * (tap1 - tap2);
tap3 = tap3 + rate * 0.74 * (tap2 - tap3);
tap4 = tap4 + rate * 0.62 * (tap3 - tap4);

echo = (tap2 * 0.4 + tap3 * 0.35 + tap4 * 0.25) * 1.1;
sat = echo / (1.0 + abs(echo) * 0.35);

processed = in * (1.0 - mix) + sat * mix;
out = processed * 1.45;
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel & Embeds Sources (Theme: analog_crt)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "analog-crt-aperture-grille-phosphor-bloom",
        "title": "Analog CRT Aperture Grille Phosphor Bloom",
        "description": "Cathode-ray tube Trinitron simulation rendering RGB vertical phosphor aperture grille stripes, spherical tube curvature, magnetic beam bloom, and audio-reactive scanline luminescence.",
        "embed_desc": "Audio-reactive Trinitron CRT visualizer. Bass modulates magnetic deflection and spherical tube barrel distortion, mids ignite RGB phosphor triad bloom, and highs vibrate raster scanline luminescence.",
        "tags": ["analog", "crt", "trinitron", "phosphor", "scanlines", "retro", "shader", "webgl", "audio-reactive"],
        "params": {
            "curvature": 0.12,
            "scanlines": 220.0,
            "phosphorDensity": 240.0,
            "bloom": 1.6,
            "contrast": 1.2
        },
        "code": """param float curvature = 0.12 [0.0, 0.3];
param float scanlines = 220.0 [80.0, 360.0];
param float phosphorDensity = 240.0 [80.0, 480.0];
param float bloom = 1.6 [0.5, 3.0];
param float contrast = 1.2 [0.8, 1.8];

p = (uv - 0.5) * 2.0;
r2 = p.x * p.x + p.y * p.y;
distortedP = p * (1.0 + r2 * curvature);

bezelFade = 1.0 - smoothstep(0.92, 0.98, max(abs(distortedP.x), abs(distortedP.y)));

time = t * 0.4;

# Dual-channel Lissajous beam trajectories
beam1Dist = abs(distortedP.y - sin(distortedP.x * 4.0 * 3.14159 + time * 0.8) * 0.35);
beam2Dist = abs(distortedP.y - cos(distortedP.x * 2.0 * 3.14159 - time * 0.5) * 0.45);

g1 = 1.0 / (1.0 + beam1Dist * 22.0);
c1 = 1.0 / (1.0 + beam1Dist * 90.0);

g2 = 1.0 / (1.0 + beam2Dist * 22.0);
c2 = 1.0 / (1.0 + beam2Dist * 90.0);

# Oscilloscope graticule grid lines (calibrated graticule)
gratX = abs(sin(distortedP.x * 10.0 * 3.14159));
gratY = abs(sin(distortedP.y * 10.0 * 3.14159));
gridLine = (1.0 - smoothstep(0.92, 0.98, gratX)) + (1.0 - smoothstep(0.92, 0.98, gratY));
graticule = clamp(gridLine * 0.22, 0.0, 0.4);

# Trinitron vertical RGB aperture grille stripes
stripePhase = distortedP.x * phosphorDensity * 3.14159;
rStripe = 0.5 + 0.5 * sin(stripePhase);
gStripe = 0.5 + 0.5 * sin(stripePhase + 2.094);
bStripe = 0.5 + 0.5 * sin(stripePhase + 4.188);
phosphor = vec3(rStripe, gStripe, bStripe) * 0.65 + 0.35;

# Horizontal raster scanline modulation
scanlineMod = 0.80 + 0.20 * sin(distortedP.y * scanlines * 3.14159);

# Phosphor palette (P31 classic CRT green/amber & cyan beam trails)
cPhosphor1 = vec3(0.20, 0.96, 0.58);
cPhosphor2 = vec3(0.35, 0.75, 1.0);
cCore = vec3(0.92, 1.0, 0.94);
cDark = vec3(0.04, 0.065, 0.05);

beamCol1 = mix(cDark, cPhosphor1, g1 * bloom) + cCore * c1 * 0.8;
beamCol2 = mix(vec3(0.0), cPhosphor2, g2 * bloom * 0.8) + cCore * c2 * 0.7;
combinedBeam = beamCol1 + beamCol2 + vec3(0.12, 0.28, 0.20) * graticule;

finalCol = combinedBeam * phosphor * scanlineMod * bezelFade;

col = clamp(finalCol * contrast, 0.0, 1.0);
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

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  float r2 = p.x * p.x + p.y * p.y;
  float curv = 0.12 + b * 0.08;
  vec2 distortedP = p * (1.0 + r2 * curv);

  float bezelFade = 1.0 - smoothstep(0.92, 0.98, max(abs(distortedP.x), abs(distortedP.y)));

  float time = t * 0.40;

  float fX = 4.0 + floor(m * 2.0);
  float fY = 2.0 + sin(time * 0.3) * 0.4;
  float beam1Dist = abs(distortedP.y - sin(distortedP.x * fX * 3.14159 + time * 0.8 + b * 1.2) * (0.35 + b * 0.15));
  float beam2Dist = abs(distortedP.y - cos(distortedP.x * fY * 3.14159 - time * 0.5 + m * 0.8) * (0.45 + m * 0.12));

  float g1 = 1.0 / (1.0 + beam1Dist * (22.0 - b * 6.0));
  float c1 = 1.0 / (1.0 + beam1Dist * 90.0);

  float g2 = 1.0 / (1.0 + beam2Dist * (22.0 - m * 6.0));
  float c2 = 1.0 / (1.0 + beam2Dist * 90.0);

  float gratX = abs(sin(distortedP.x * 10.0 * 3.14159));
  float gratY = abs(sin(distortedP.y * 10.0 * 3.14159));
  float gridLine = (1.0 - smoothstep(0.92, 0.98, gratX)) + (1.0 - smoothstep(0.92, 0.98, gratY));
  float graticule = clamp(gridLine * 0.22, 0.0, 0.4);

  float stripePhase = distortedP.x * 240.0 * 3.14159;
  vec3 phosphor = vec3(
    0.5 + 0.5 * sin(stripePhase),
    0.5 + 0.5 * sin(stripePhase + 2.094),
    0.5 + 0.5 * sin(stripePhase + 4.188)
  ) * 0.65 + 0.35;

  float scanlineMod = 0.80 + 0.20 * sin(distortedP.y * 220.0 * 3.14159 + h * 4.0);

  vec3 cPhosphor1 = vec3(0.20, 0.96, 0.58);
  vec3 cPhosphor2 = vec3(0.35, 0.75, 1.0);
  vec3 cCore = vec3(0.92, 1.0, 0.94);
  vec3 cDark = vec3(0.04, 0.065, 0.05);

  vec3 beamCol1 = mix(cDark, cPhosphor1, g1 * (1.6 + b * 0.8)) + cCore * c1 * 0.8;
  vec3 beamCol2 = mix(vec3(0.0), cPhosphor2, g2 * (1.4 + m * 0.8)) + cCore * c2 * 0.7;
  vec3 combinedBeam = beamCol1 + beamCol2 + vec3(0.12, 0.28, 0.20) * graticule * (1.0 + h * 0.5);

  vec3 finalCol = combinedBeam * phosphor * scanlineMod * bezelFade;

  gl_FragColor = vec4(clamp(finalCol * 1.2, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "analog-crt-electron-beam-raster-deflection",
        "title": "Analog CRT Electron Beam Raster Deflection",
        "description": "High-voltage electron beam deflection oscilloscope rendering phosphor trace persistence, magnetic yoke sweep distortion, and calibrated raster graticule lines.",
        "embed_desc": "Audio-reactive CRT electron beam oscilloscope. Bass drives magnetic deflection coil amplitude, mids shape Lissajous beam trajectories, and highs energize phosphor trace bloom.",
        "tags": ["analog", "crt", "electron-beam", "oscilloscope", "raster", "phosphor", "shader", "webgl", "audio-reactive"],
        "params": {
            "deflection": 0.85,
            "scanlines": 240.0,
            "rasterScale": 12.0,
            "bloom": 1.75,
            "contrast": 1.25
        },
        "code": """param float deflection = 0.85 [0.2, 1.8];
param float scanlines = 240.0 [80.0, 360.0];
param float rasterScale = 12.0 [6.0, 24.0];
param float bloom = 1.75 [0.5, 3.0];
param float contrast = 1.25 [0.8, 1.8];

p = (uv - 0.5) * 2.0;
r2 = p.x * p.x + p.y * p.y;
distortedP = p * (1.0 + r2 * 0.14);

bezelFade = 1.0 - smoothstep(0.92, 0.98, max(abs(distortedP.x), abs(distortedP.y)));

time = t * 0.35;

# Triple-harmonic Lissajous deflection figure (magnetic X-Y deflection coils)
theta = atan2(distortedP.y, distortedP.x);
rad = length(distortedP);

deflectedY = sin(distortedP.x * 3.0 * 3.14159 + time * 0.7) * 0.42 * deflection;
deflectedX = cos(distortedP.y * 2.0 * 3.14159 - time * 0.5) * 0.42 * deflection;

beam1 = abs(distortedP.y - deflectedY);
beam2 = abs(distortedP.x - deflectedX);
beamOrbit = abs(rad - (0.48 + 0.12 * sin(theta * 5.0 + time)));

g1 = 1.0 / (1.0 + beam1 * 26.0);
c1 = 1.0 / (1.0 + beam1 * 95.0);

g2 = 1.0 / (1.0 + beam2 * 26.0);
c2 = 1.0 / (1.0 + beam2 * 95.0);

gOrbit = 1.0 / (1.0 + beamOrbit * 30.0);
cOrbit = 1.0 / (1.0 + beamOrbit * 110.0);

# Oscilloscope coordinate graticule grid
gratX = abs(sin(distortedP.x * rasterScale * 3.14159));
gratY = abs(sin(distortedP.y * rasterScale * 3.14159));
gridLine = (1.0 - smoothstep(0.92, 0.98, gratX)) + (1.0 - smoothstep(0.92, 0.98, gratY));
graticule = clamp(gridLine * 0.20, 0.0, 0.35);

# Horizontal raster scanline modulation
scanlineMod = 0.82 + 0.18 * sin(distortedP.y * scanlines * 3.14159);

# Amber/Gold high-persistence CRT phosphor palette (#FFB000 / #FF7700)
cAmber = vec3(1.0, 0.68, 0.12);
cGold = vec3(1.0, 0.88, 0.45);
cCore = vec3(1.0, 0.98, 0.85);
cDark = vec3(0.05, 0.035, 0.02);

col1 = mix(cDark, cAmber, g1 * bloom * 0.7) + cCore * c1 * 0.6;
col2 = mix(vec3(0.0), cGold, g2 * bloom * 0.7) + cCore * c2 * 0.6;
colOrbit = mix(vec3(0.0), cAmber, gOrbit * bloom * 0.8) + cCore * cOrbit * 0.7;

combined = col1 + col2 + colOrbit + vec3(0.24, 0.16, 0.06) * graticule;
finalCol = combined * scanlineMod * bezelFade;

col = clamp(finalCol * contrast, 0.0, 1.0);
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

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  float r2 = p.x * p.x + p.y * p.y;
  vec2 distortedP = p * (1.0 + r2 * (0.14 + b * 0.06));

  float bezelFade = 1.0 - smoothstep(0.92, 0.98, max(abs(distortedP.x), abs(distortedP.y)));

  float time = t * 0.35;

  float theta = atan(distortedP.y, distortedP.x);
  float rad = length(distortedP);

  float effDeflection = (0.85 + b * 0.5);
  float deflectedY = sin(distortedP.x * (3.0 + floor(m * 2.0)) * 3.14159 + time * 0.7) * 0.42 * effDeflection;
  float deflectedX = cos(distortedP.y * 2.0 * 3.14159 - time * 0.5 + b * 0.8) * 0.42 * effDeflection;

  float beam1 = abs(distortedP.y - deflectedY);
  float beam2 = abs(distortedP.x - deflectedX);
  float beamOrbit = abs(rad - (0.48 + 0.12 * sin(theta * 5.0 + time) + b * 0.15));

  float g1 = 1.0 / (1.0 + beam1 * 26.0);
  float c1 = 1.0 / (1.0 + beam1 * 95.0);

  float g2 = 1.0 / (1.0 + beam2 * 26.0);
  float c2 = 1.0 / (1.0 + beam2 * 95.0);

  float gOrbit = 1.0 / (1.0 + beamOrbit * 30.0);
  float cOrbit = 1.0 / (1.0 + beamOrbit * 110.0);

  float gratX = abs(sin(distortedP.x * 12.0 * 3.14159));
  float gratY = abs(sin(distortedP.y * 12.0 * 3.14159));
  float gridLine = (1.0 - smoothstep(0.92, 0.98, gratX)) + (1.0 - smoothstep(0.92, 0.98, gratY));
  float graticule = clamp(gridLine * 0.20, 0.0, 0.35);

  float scanlineMod = 0.82 + 0.18 * sin(distortedP.y * 240.0 * 3.14159 + h * 3.0);

  vec3 cAmber = vec3(1.0, 0.68, 0.12);
  vec3 cGold = vec3(1.0, 0.88, 0.45);
  vec3 cCore = vec3(1.0, 0.98, 0.85);
  vec3 cDark = vec3(0.05, 0.035, 0.02);

  vec3 col1 = mix(cDark, cAmber, g1 * (1.75 + b * 0.8) * 0.7) + cCore * c1 * 0.6;
  vec3 col2 = mix(vec3(0.0), cGold, g2 * (1.75 + m * 0.8) * 0.7) + cCore * c2 * 0.6;
  vec3 colOrbit = mix(vec3(0.0), cAmber, gOrbit * (1.75 + b * 0.6) * 0.8) + cCore * cOrbit * 0.7;

  vec3 combined = col1 + col2 + colOrbit + vec3(0.24, 0.16, 0.06) * graticule * (1.0 + h * 0.4);
  vec3 finalCol = combined * scanlineMod * bezelFade;

  gl_FragColor = vec4(clamp(finalCol * 1.25, 0.0, 1.0), 1.0);
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
        let bSum = 0, mSum = 0, hSum = 0;
        for (let i = 0; i < 4; i++) bSum += freqData[i];
        for (let i = 4; i < 16; i++) mSum += freqData[i];
        for (let i = 16; i < 32; i++) hSum += freqData[i];
        const targetB = (bSum / 4) / 255;
        const targetM = (mSum / 12) / 255;
        const targetH = (hSum / 16) / 255;
        bass += (targetB - bass) * 0.25;
        mid += (targetM - mid) * 0.2;
        high += (targetH - high) * 0.2;
      }} else {{
        bass = 0.25 + 0.25 * Math.sin(t * 2.2);
        mid = 0.25 + 0.25 * Math.sin(t * 3.1 + 1.2);
        high = 0.25 + 0.25 * Math.sin(t * 4.5 + 2.4);
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
  c bg 0.08 0.08 0.09
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
  i note2 {nr+3}
  i note3 {nr+7}
  i note4 {nr}
  i note5 {nr+3}
  i note6 {nr+12}
  i note7 {nr+7}
  i note8 {nr}
  i note9 {nr}
  i note10 {nr+3}
  i note11 {nr+5}
  i note12 {nr}
  i note13 {nr+12}
  i note14 {nr+7}
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
                "tags": ["synth", "idm", "audio", "dsp", "bell", "reese", "bass"],
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
                "tags": ["effect", "breakbeat", "audio", "dsp", "drive", "saturation", "echo", "delay"],
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
