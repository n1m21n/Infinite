#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 16.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (bauhaus-constructivist-grid, bauhaus-geometric-rotary)
2. Sketch 3D (suprematist-floating-monolith-3d, suprematist-geometric-cantilever-3d)
3. Field Synth (acid-house-transistor-lead-synth, acid-house-resonant-sub-synth)
4. Field Effects (psytrance-resonant-comb-fx, psytrance-spectral-filter-fx)
5. Field Pixel (indie-psychedelic-kaleido-melt, indie-psychedelic-liquid-dream)
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

TMP_DIR = "/tmp/pack16_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: bauhaus_geometry)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "bauhaus-constructivist-grid",
        "title": "Bauhaus Constructivist Grid",
        "description": "Modular constructivist grid of primary circular discs, quarter-circle wedges, and solid rectangular balance bars.",
        "tags": ["bauhaus", "constructivist", "grid", "geometric", "drawing"],
        "code": """// Bauhaus Constructivist Grid - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Modular constructivist grid of primary circular discs, quarter-circle wedges, and solid rectangular balance bars.
param("grid_divs", 4.0, 2.0, 8.0);
param("line_weight", 2.0, 0.5, 6.0);
param("rot_speed", 0.35, 0.05, 1.5);
param("margin", 0.08, 0.02, 0.2);

function draw(t) {
  // Warm Bauhaus cream ground #F2E8CF calibrated for luminance 14-228
  background(0.85, 0.82, 0.76);

  const minDim = min(width, height);
  const time = t * rot_speed;
  const divs = floor(grid_divs);
  const span = minDim * (1.0 - margin * 2.0);
  const cell = span / divs;
  const startX = (width - span) * 0.5;
  const startY = (height - span) * 0.5;

  push();

  // Outer framing boundary
  noFill();
  stroke(0.067, 0.067, 0.067);
  strokeWeight(line_weight * 1.5);
  rect(startX, startY, span, span);

  // Constructivist modular cells
  for (let j = 0; j < divs; j++) {
    for (let i = 0; i < divs; i++) {
      const cx = startX + (i + 0.5) * cell;
      const cy = startY + (j + 0.5) * cell;
      const idx = (i + j * divs);

      push();
      translate(cx, cy);

      // Grid line dividers
      noFill();
      stroke(0.067, 0.067, 0.067, 0.25);
      strokeWeight(line_weight * 0.5);
      rect(-cell * 0.5, -cell * 0.5, cell, cell);

      if (idx % 4 === 0) {
        // Primary Cobalt Blue solid circle with cut
        noStroke();
        fill(0.106, 0.231, 0.561); // #1B3B8F
        circle(0, 0, cell * 0.41);
        fill(0.847, 0.188, 0.122); // #D8301F
        rect(-cell * 0.41, -cell * 0.08, cell * 0.82, cell * 0.16);
      } else if (idx % 4 === 1) {
        // Bauhaus Vermilion Red quarter-circle
        noStroke();
        fill(0.847, 0.188, 0.122); // #D8301F
        beginShape();
        vertex(-cell * 0.4, cell * 0.4);
        for (let k = 0; k <= 16; k++) {
          const a = -1.570796 * k / 16.0;
          vertex(-cell * 0.4 + cos(a) * cell * 0.8, cell * 0.4 + sin(a) * cell * 0.8);
        }
        endShape(1);
        fill(0.067, 0.067, 0.067);
        rect(-cell * 0.4, -cell * 0.4, cell * 0.35, cell * 0.35);
      } else if (idx % 4 === 2) {
        // Cadmium Yellow diagonal bar
        push();
        rotate(0.785398 + sin(time * 0.5 + idx) * 0.2);
        noStroke();
        fill(0.949, 0.718, 0.020); // #F2B705
        rect(-cell * 0.45, -cell * 0.12, cell * 0.9, cell * 0.24);
        fill(0.067, 0.067, 0.067);
        circle(0, 0, cell * 0.12);
        pop();
      } else {
        // Intersecting black and red concentric rings
        noFill();
        stroke(0.067, 0.067, 0.067);
        strokeWeight(line_weight);
        ellipse(0, 0, cell * 0.7, cell * 0.7);
        stroke(0.847, 0.188, 0.122);
        ellipse(0, 0, cell * 0.35, cell * 0.35);
      }

      pop();
    }
  }

  pop();
}
"""
    },
    {
        "slug": "bauhaus-geometric-rotary",
        "title": "Bauhaus Geometric Rotary",
        "description": "Concentric circular arcs, rotating triangular wedges, and cantilever balance bars in dynamic equilibrium.",
        "tags": ["bauhaus", "rotary", "geometry", "constructivist", "procedural"],
        "code": """// Bauhaus Geometric Rotary - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Concentric circular arcs, rotating triangular wedges, and cantilever balance bars in dynamic equilibrium.
param("disc_radius", 0.42, 0.15, 0.85);
param("rot_speed", 0.35, 0.05, 1.5);
param("bars_count", 6.0, 3.0, 12.0);
param("line_weight", 2.2, 0.5, 6.0);
param("accent_scale", 1.0, 0.4, 2.0);

function draw(t) {
  // Warm Bauhaus cream ground #F2E8CF calibrated for luminance 14-228
  background(0.85, 0.82, 0.76);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const minDim = min(width, height);
  const R = minDim * disc_radius;
  const time = t * rot_speed;

  push();
  translate(cx, cy);

  // Outer framing square
  noFill();
  stroke(0.067, 0.067, 0.067);
  strokeWeight(line_weight * 1.5);
  rect(-minDim * 0.44, -minDim * 0.44, minDim * 0.88, minDim * 0.88);

  // 1. Large Cobalt Blue background balance crescent
  push();
  rotate(time * 0.2);
  noStroke();
  fill(0.106, 0.231, 0.561); // #1B3B8F
  circle(-R * 0.15, -R * 0.1, R * 1.35);
  fill(0.85, 0.82, 0.76);
  circle(R * 0.2, R * 0.1, R * 1.15);
  pop();

  // 2. Vermilion Red central dynamic sector
  push();
  rotate(-time * 0.4 + 0.5);
  fill(0.847, 0.188, 0.122); // #D8301F
  beginShape();
  vertex(0, 0);
  for (let k = 0; k <= 24; k++) {
    const a = -1.2 + (2.4 * k / 24.0);
    vertex(cos(a) * R * 0.95, sin(a) * R * 0.95);
  }
  endShape(1);
  pop();

  // 3. Cadmium Yellow cantilever balance bars
  const bars = floor(bars_count);
  for (let b = 0; b < bars; b++) {
    const angle = (b / bars) * 6.2831853 + time * 0.15;
    push();
    rotate(angle);
    noStroke();
    if (b % 2 === 0) {
      fill(0.949, 0.718, 0.020); // #F2B705
      rect(R * 0.35, -minDim * 0.025, R * 0.55, minDim * 0.05);
    } else {
      fill(0.067, 0.067, 0.067);
      rect(R * 0.4, -minDim * 0.015, R * 0.45, minDim * 0.03);
    }
    pop();
  }

  // 4. Heavy Carbon black central hub and concentric precision rings
  noFill();
  stroke(0.067, 0.067, 0.067);
  strokeWeight(line_weight);
  ellipse(0, 0, R * 1.8, R * 1.8);
  ellipse(0, 0, R * 1.2, R * 1.2);
  ellipse(0, 0, R * 0.6, R * 0.6);

  // Central solid core
  noStroke();
  fill(0.067, 0.067, 0.067);
  circle(0, 0, R * 0.28);
  fill(0.949, 0.718, 0.020);
  circle(0, 0, R * 0.1);

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: suprematist_blocks)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "suprematist-floating-monolith-3d",
        "title": "Suprematist Floating Monolith 3D",
        "description": "Suspended carbon black rectangular monolith flanked by dynamic red diagonal wedges and interlocking cantilever planes.",
        "tags": ["suprematist", "monolith", "3d", "constructivist", "geometry"],
        "code": """// Suprematist Floating Monolith 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Suspended carbon black rectangular monolith flanked by dynamic red diagonal wedges and interlocking cantilever planes.
param("monolith_scale", 1.0, 0.4, 2.0);
param("cantilever_spread", 0.65, 0.2, 1.5);
param("orbit_speed", 0.4, 0.05, 1.5);
param("wedge_tilt", 0.52, 0.1, 1.2);
param("strut_density", 4.0, 2.0, 8.0);

function draw(t) {
  push();
  const time = t * orbit_speed;

  // Gentle floating spatial drift
  rotateY(time * 0.45);
  rotateX(0.38 + sin(time * 0.3) * 0.15);
  rotateZ(sin(time * 0.25) * 0.12);

  // 1. Central Hero Monolith (Dense Carbon Black #111111)
  push();
  fill(0.067, 0.067, 0.067);
  box(0.42 * monolith_scale, 1.35 * monolith_scale, 0.32 * monolith_scale);
  pop();

  // 2. Suprematist Vermilion Red Angled Wedge Plane (#D9231F)
  push();
  translate(-0.45 * cantilever_spread, 0.25, 0.35);
  rotateZ(wedge_tilt);
  rotateY(0.25);
  fill(0.851, 0.137, 0.122);
  box(0.12, 0.95, 0.55);
  pop();

  // 3. Counterbalance Horizontal Slabs
  push();
  translate(0.55 * cantilever_spread, -0.32, -0.2);
  rotateX(-0.35);
  rotateZ(-0.25);
  fill(0.067, 0.067, 0.067);
  box(0.75, 0.08, 0.45);
  pop();

  // 4. Cadmium Golden Yellow Accent Bar (#E5A912)
  push();
  translate(0.15, 0.65 * monolith_scale, -0.35);
  rotateY(0.785);
  fill(0.92, 0.72, 0.12);
  box(0.08, 0.08, 0.85);
  pop();

  // 5. Tension Axis Rods (Interconnecting coordinate spines)
  const rods = floor(strut_density);
  for (let i = 0; i < rods; i++) {
    const frac = (i / rods) - 0.5;
    const y = frac * 1.4;
    push();
    fill(0.15, 0.15, 0.15);
    tube(-0.8 * cantilever_spread, y, frac * 0.4, 0.8 * cantilever_spread, y + 0.1, -frac * 0.4, 0.012, 6);
    pop();
  }

  // 6. Floating Satellite Discs/Spheres
  push();
  translate(-0.65 * cantilever_spread, -0.55, 0.45);
  fill(0.851, 0.137, 0.122);
  sphere(0.14, 16, 16);
  pop();

  push();
  translate(0.7 * cantilever_spread, 0.5, -0.4);
  fill(0.067, 0.067, 0.067);
  sphere(0.09, 12, 12);
  pop();

  pop();
}
"""
    },
    {
        "slug": "suprematist-geometric-cantilever-3d",
        "title": "Suprematist Geometric Cantilever 3D",
        "description": "Interlocking constructivist cantilever beams, offset rectangular planes, and hovering geometric volumes.",
        "tags": ["suprematist", "cantilever", "3d", "architecture", "geometry"],
        "code": """// Suprematist Geometric Cantilever 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Interlocking constructivist cantilever beams, offset rectangular planes, and hovering geometric volumes.
param("cantilever_reach", 1.1, 0.4, 2.0);
param("beam_depth", 0.16, 0.05, 0.35);
param("tumble_speed", 0.35, 0.05, 1.2);
param("red_plane_scale", 1.0, 0.4, 1.8);
param("grid_steps", 5.0, 2.0, 8.0);

function draw(t) {
  push();
  const time = t * tumble_speed;

  rotateY(time * 0.5);
  rotateX(0.42 + cos(time * 0.35) * 0.18);
  rotateZ(0.2 + sin(time * 0.28) * 0.1);

  const L = cantilever_reach;

  // 1. Long Primary Transverse Cantilever (Carbon Black #111111)
  push();
  translate(0, 0.1, 0);
  rotateZ(0.15);
  fill(0.067, 0.067, 0.067);
  box(L * 1.5, beam_depth, beam_depth * 1.4);
  pop();

  // 2. Vertical Piercing Pillar
  push();
  translate(L * 0.25, -0.15, 0);
  fill(0.067, 0.067, 0.067);
  box(beam_depth * 1.2, L * 1.3, beam_depth * 1.2);
  pop();

  // 3. Suprematist Dominant Red Floating Plane (#D9231F)
  push();
  translate(-L * 0.35, 0.32, L * 0.25);
  rotateX(0.55);
  rotateY(-0.35);
  rotateZ(0.785);
  fill(0.851, 0.137, 0.122);
  box(0.85 * red_plane_scale, 0.04, 0.65 * red_plane_scale);
  pop();

  // 4. Secondary Counter-Plane (Light Neutral White/Gray)
  push();
  translate(L * 0.45, -0.4, -L * 0.2);
  rotateY(0.8);
  rotateZ(-0.3);
  fill(0.2, 0.22, 0.25);
  box(0.55, 0.03, 0.75);
  pop();

  // 5. Intersecting Structural Coordinate Rods
  const steps = floor(grid_steps);
  for (let i = 0; i < steps; i++) {
    const u = (i / (steps - 1) - 0.5) * L * 1.2;
    push();
    fill(0.851, 0.137, 0.122);
    tube(u, -0.6, -0.3, u, 0.6, 0.3, 0.014, 6);
    pop();
  }

  // 6. Geometric Anchor Cube and Nodal Sphere
  push();
  translate(-L * 0.55, -0.25, -0.15);
  rotateX(0.785);
  fill(0.92, 0.72, 0.12);
  box(0.22, 0.22, 0.22);
  pop();

  push();
  translate(L * 0.6, 0.35, 0.2);
  fill(0.851, 0.137, 0.122);
  sphere(0.12, 16, 16);
  pop();

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
        "slug": "acid-house-transistor-lead-synth",
        "title": "Acid House Transistor Lead Synth",
        "description": "Resonant 3-pole transistor diode ladder lead synthesizer with accent modulation and vintage acid squelch.",
        "tags": ["synth", "acid", "house", "lead", "transistor"],
        "note_root": 36,
        "params": {
            "cutoff": 1400.0,
            "resonance": 0.78,
            "envMod": 0.75,
            "accent": 0.5,
            "decay": 0.35
        },
        "code": """param float cutoff = 1400.0 [80.0, 9000.0]
param float resonance = 0.78 [0.1, 0.98]
param float envMod = 0.75 [0.0, 1.0]
param float accent = 0.5 [0.0, 1.0]
param float decay = 0.35 [0.05, 2.0]
state float phase = 0
state float env = 0
state float curFreq = 110
state float lp1 = 0
state float lp2 = 0
state float lp3 = 0

target = if(gate > 0.5, 1.0, 0.0)
env = if(target > env, env + (target - env) * 0.1, env - env * (0.0006 / decay))

curFreq = curFreq + (freq - curFreq) * 0.08
stepFreq = clamp(curFreq, 20.0, 4000.0) / sr
phase = (phase + stepFreq) % 1.0

saw = phase * 2.0 - 1.0
square = if(phase > 0.5, 0.9, -0.9)
raw = saw * 0.7 + square * 0.3

fMod = clamp(cutoff + env * (4500.0 * (1.0 + accent * 0.5)), 60.0, 14000.0)
fNorm = clamp(fMod * 3.14159265 / sr, 0.01, 0.85)
fb = resonance * 3.2

drive = raw - lp3 * fb
lp1 = lp1 + fNorm * (drive - lp1)
lp2 = lp2 + fNorm * (lp1 - lp2)
lp3 = lp3 + fNorm * (lp2 - lp3)

sat = clamp(lp3 * 1.5, -1.0, 1.0)
out = sat * env * 0.38
"""
    },
    {
        "slug": "acid-house-resonant-sub-synth",
        "title": "Acid House Resonant Sub Synth",
        "description": "Deep resonant acid sub bass synthesizer with transistor glide, soft saturation drive, and sub-octave punch.",
        "tags": ["synth", "acid", "house", "sub", "bass"],
        "note_root": 31,
        "params": {
            "subDrive": 1.8,
            "resonance": 0.65,
            "cutoff": 850.0,
            "punch": 0.6,
            "decay": 0.45
        },
        "code": """param float subDrive = 1.8 [1.0, 4.0]
param float resonance = 0.65 [0.1, 0.95]
param float cutoff = 850.0 [80.0, 5000.0]
param float punch = 0.6 [0.0, 1.0]
param float decay = 0.45 [0.1, 2.0]
state float phase = 0
state float curFreq = 55
state float env = 0
state float pEnv = 0
state float lp1 = 0
state float lp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decay), 0.00005, 0.01)
env = if(target > env, env + (target - env) * 0.12, env - env * decayRate)

pEnv = if(target > pEnv, 1.0, pEnv * 0.998)
curFreq = curFreq + (freq * 0.5 - curFreq) * 0.1
f0 = clamp(curFreq + pEnv * punch * 50.0, 20.0, 800.0)

pStep = f0 / sr
phase = (phase + pStep) % 1.0

subSin = sin(phase * 6.2831853)
saw = (phase * 2.0 - 1.0) * 0.4
raw = (subSin + saw) * env

u = raw * subDrive
sat = clamp(u * 1.3, -1.0, 1.0)

fCut = clamp(cutoff / sr * 2.4, 0.005, 0.4)
diff = sat - lp2 * (resonance * 1.8) - lp1
lp1 = lp1 + fCut * diff
lp2 = lp2 + fCut * lp1

out = lp2 * 0.22
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: psytrance)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "psytrance-resonant-comb-fx",
        "title": "Psytrance Resonant Comb FX",
        "description": "Multi-tap resonant comb filter with kinetic sweep LFO modulation and harmonic feedback saturation.",
        "tags": ["effect", "psytrance", "comb", "filter", "resonance"],
        "params": {
            "combRate": 4.5,
            "resonance": 0.72,
            "feedbackMod": 0.55,
            "drive": 0.5,
            "mix": 0.5
        },
        "code": """param float combRate = 4.5 [0.1, 16.0]
param float resonance = 0.72 [0.1, 0.95]
param float feedbackMod = 0.55 [0.0, 1.0]
param float drive = 0.5 [0.0, 1.5]
param float mix = 0.5 [0.0, 1.0]
state float lfo = 0
state float d1 = 0
state float d2 = 0
state float d3 = 0
state float prevOut = 0

lfo = (lfo + combRate / sr) % 1.0
modSweep = (sin(lfo * 6.2831853) * 0.5 + 0.5) * feedbackMod

sig = in + prevOut * (resonance * (0.6 + modSweep * 0.35))
sat = clamp(sig * (1.0 + drive * 0.8), -1.0, 1.0)

coeff = clamp(0.25 + modSweep * 0.45, 0.05, 0.85)
d1 = d1 + coeff * (sat - d1)
d2 = d2 + coeff * (d1 - d2)
d3 = d3 + coeff * (d2 - d3)
prevOut = d3

out = (in * (1.0 - mix) + prevOut * mix) * 0.82
"""
    },
    {
        "slug": "psytrance-spectral-filter-fx",
        "title": "Psytrance Spectral Filter FX",
        "description": "Resonant state-variable spectral filter with audio-rate LFO frequency sweeps and feedback overdrive.",
        "tags": ["effect", "psytrance", "filter", "spectral", "sweep"],
        "params": {
            "filterRate": 6.0,
            "resonance": 0.75,
            "cutoff": 1600.0,
            "drive": 1.2,
            "mix": 0.5
        },
        "code": """param float filterRate = 6.0 [0.2, 20.0]
param float resonance = 0.75 [0.1, 0.96]
param float cutoff = 1600.0 [100.0, 8000.0]
param float drive = 1.2 [0.5, 3.0]
param float mix = 0.5 [0.0, 1.0]
state float lfo = 0
state float s1 = 0
state float s2 = 0

lfo = (lfo + filterRate / sr) % 1.0
sweep = sin(lfo * 6.2831853) * 0.45 + 0.55

u = in * drive
sat = clamp(u, -1.0, 1.0)

fCut = clamp((cutoff * sweep) / sr * 2.4, 0.005, 0.45)
feedback = (s2 * resonance * 2.5) / (1.0 + abs(s2))
diff = sat - feedback - s1
s1 = s1 + fCut * diff
s2 = s2 + fCut * s1

out = (in * (1.0 - mix) + s2 * mix) * 0.85
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel Sources (Theme: indie_psychedelic)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "indie-psychedelic-kaleido-melt",
        "title": "Indie Psychedelic Kaleido Melt",
        "description": "Kaleidoscopic radial symmetry melt shader with liquid color warping, gold iridescence, and turquoise glow.",
        "embed_desc": "Kaleidoscopic radial symmetry melt shader with liquid color warping, gold iridescence, and turquoise glow. Bass drives melt distortion and pulsation, mids cycle kaleidoscopic folds, highs ignite turquoise surface shimmer.",
        "tags": ["pixel", "psychedelic", "kaleidoscope", "melt", "shader"],
        "params": {
            "swirlRate": 0.85,
            "symmetry": 6.0,
            "meltDistort": 1.4,
            "colorPhase": 0.6,
            "glow": 0.7
        },
        "code": """param float swirlRate = 0.85 [0.1, 3.0]
param float symmetry = 6.0 [3.0, 12.0]
param float meltDistort = 1.4 [0.2, 4.0]
param float colorPhase = 0.6 [0.0, 1.0]
param float glow = 0.7 [0.1, 1.0]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2
r = length(p)
ang = atan2(p.y, p.x)
time = t * swirlRate

sector = 6.2831853 / symmetry
aFold = abs(mod(ang + time * 0.2, sector) - sector * 0.5)

pFold = vec2(cos(aFold), sin(aFold)) * r
swirl = sin(r * 5.0 - time * 2.0 + sin(aFold * 4.0) * meltDistort)
ripple = cos(pFold.x * 6.0 + pFold.y * 6.0 - time * 1.5)

val = swirl * 0.6 + ripple * 0.4

cDeep = vec3(0.08, 0.04, 0.16)
cMagenta = vec3(0.92, 0.12, 0.58)
cGold = vec3(0.98, 0.76, 0.15)
cTurquoise = vec3(0.12, 0.85, 0.82)

mix1 = mix(cDeep, cMagenta, clamp(val * 0.6 + 0.4, 0.0, 1.0))
mix2 = mix(mix1, cGold, clamp(sin(r * 4.0 - time + colorPhase * 3.14) * 0.5 + 0.5, 0.0, 1.0))
col = mix(mix2, cTurquoise, clamp(cos(aFold * 3.0 + r * 2.0) * 0.5 + 0.5, 0.0, 1.0) * glow)
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
        vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2;
        float r = length(p);
        float ang = atan(p.y, p.x);
        float time = t * (0.85 + audio_bass * 1.5);

        float sym = 6.0;
        float sector = 6.2831853 / sym;
        float aFold = abs(mod(ang + time * 0.25, sector) - sector * 0.5);

        vec2 pFold = vec2(cos(aFold), sin(aFold)) * r;
        float melt = 1.4 + audio_bass * 1.2;
        float swirl = sin(r * (5.0 + audio_mid * 4.0) - time * 2.2 + sin(aFold * 4.0) * melt);
        float ripple = cos(pFold.x * 6.0 + pFold.y * 6.0 - time * 1.8);

        float val = swirl * 0.6 + ripple * 0.4;

        vec3 cDeep = vec3(0.08, 0.04, 0.16);
        vec3 cMagenta = vec3(0.92, 0.12, 0.58);
        vec3 cGold = vec3(0.98, 0.76, 0.15);
        vec3 cTurquoise = vec3(0.12, 0.85, 0.82);

        vec3 mix1 = mix(cDeep, cMagenta, clamp(val * 0.6 + 0.4, 0.0, 1.0));
        vec3 mix2 = mix(mix1, cGold, clamp(sin(r * 4.0 - time + audio_mid * 3.14) * 0.5 + 0.5, 0.0, 1.0));
        vec3 col = mix(mix2, cTurquoise, clamp(cos(aFold * 3.0 + r * 2.0) * 0.5 + 0.5, 0.0, 1.0) * (0.7 + audio_high * 0.8));

        gl_FragColor = vec4(col, 1.0);
      }
"""
    },
    {
        "slug": "indie-psychedelic-liquid-dream",
        "title": "Indie Psychedelic Liquid Dream",
        "description": "Flowing reaction-diffusion liquid dream shader with multi-frequency fluid warping and vibrant neon blooms.",
        "embed_desc": "Flowing reaction-diffusion liquid dream shader with multi-frequency fluid warping and vibrant neon blooms. Bass accelerates fluid warps and center blob eruption, mids blend electric orange and lime contours, highs release pink pulse bloom.",
        "tags": ["pixel", "psychedelic", "fluid", "liquid", "shader"],
        "params": {
            "flowSpeed": 0.75,
            "fluidWarp": 2.2,
            "colorSpread": 1.1,
            "pulseBloom": 0.65,
            "density": 4.0
        },
        "code": """param float flowSpeed = 0.75 [0.1, 2.5]
param float fluidWarp = 2.2 [0.5, 5.0]
param float colorSpread = 1.1 [0.2, 2.0]
param float pulseBloom = 0.65 [0.1, 1.0]
param float density = 4.0 [1.0, 8.0]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0
r = length(p)
time = t * flowSpeed

w1 = sin(p.x * density + time * 1.2) + cos(p.y * density - time * 0.9)
w2 = sin(p.y * (density * 1.3) + time * 0.8 + w1 * 0.5)
pWarp = p + vec2(w1, w2) * (0.18 * fluidWarp)

blob1 = sin(length(pWarp - vec2(cos(time * 0.4) * 0.4, sin(time * 0.4) * 0.4)) * 6.0 - time * 1.8)
blob2 = cos(length(pWarp + vec2(sin(time * 0.5) * 0.35, cos(time * 0.5) * 0.35)) * 5.0 + time * 1.4)
fluidPattern = blob1 * 0.5 + blob2 * 0.5

cViolet = vec3(0.18, 0.04, 0.32)
cElectricOrange = vec3(0.98, 0.42, 0.08)
cLime = vec3(0.45, 0.92, 0.18)
cPink = vec3(0.95, 0.18, 0.68)

base = mix(cViolet, cElectricOrange, clamp(fluidPattern * 0.7 + 0.5, 0.0, 1.0))
layered = mix(base, cLime, clamp(sin(fluidPattern * 4.0 + time * colorSpread) * 0.5 + 0.5, 0.0, 1.0) * 0.75)
col = mix(layered, cPink, clamp(cos(r * 5.0 - time * 1.5) * 0.5 + 0.5, 0.0, 1.0) * pulseBloom)
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
        vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0;
        float r = length(p);
        float time = t * (0.75 + audio_bass * 1.6);

        float density = 4.0 + audio_mid * 2.5;
        float w1 = sin(p.x * density + time * 1.2) + cos(p.y * density - time * 0.9);
        float w2 = sin(p.y * (density * 1.3) + time * 0.8 + w1 * 0.5);
        vec2 pWarp = p + vec2(w1, w2) * (0.18 * (2.2 + audio_bass * 1.5));

        vec2 c1 = vec2(cos(time * 0.4) * 0.4, sin(time * 0.4) * 0.4);
        vec2 c2 = -vec2(sin(time * 0.5) * 0.35, cos(time * 0.5) * 0.35);
        float blob1 = sin(length(pWarp - c1) * 6.0 - time * 1.8);
        float blob2 = cos(length(pWarp + c2) * 5.0 + time * 1.4);
        float fluidPattern = blob1 * 0.5 + blob2 * 0.5;

        vec3 cViolet = vec3(0.18, 0.04, 0.32);
        vec3 cElectricOrange = vec3(0.98, 0.42, 0.08);
        vec3 cLime = vec3(0.45, 0.92, 0.18);
        vec3 cPink = vec3(0.95, 0.18, 0.68);

        vec3 base = mix(cViolet, cElectricOrange, clamp(fluidPattern * 0.7 + 0.5, 0.0, 1.0));
        vec3 layered = mix(base, cLime, clamp(sin(fluidPattern * 4.0 + time * 1.1) * 0.5 + 0.5, 0.0, 1.0) * (0.75 + audio_mid * 0.5));
        vec3 col = mix(layered, cPink, clamp(cos(r * (5.0 + audio_high * 4.0) - time * 1.5) * 0.5 + 0.5, 0.0, 1.0) * (0.65 + audio_high * 0.8));

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
  c bg 0.88 0.85 0.80
  f bgOpacity 1.0
  f lightIntensity 1.4
  c lightColor 1.0 0.98 0.95
  c ambient 0.4 0.4 0.4
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
        root = it["note_root"]
        inf_content = f"""infinite-patch 1
# {it['title']}
node 1 Notes Note Sequencer
  id notes
  pos 0 1024
  f rateBeats 0.25
  i steps 16
  i note0 {root}
  i note1 {root}
  i note2 {root+12}
  i note3 {root+7}
  i note4 {root}
  i note5 {root+12}
  i note6 {root+5}
  i note7 {root+7}
  i note8 {root}
  i note9 {root}
  i note10 {root+12}
  i note11 {root+7}
  i note12 {root}
  i note13 {root+12}
  i note14 {root+5}
  i note15 {root+10}
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
  c fillColor 0.16 0.32 0.45
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

        # Real engine frame preview
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
  f cutoff 2400.0
  f resonance 0.3
  f decay 0.2
  f volume 0.5
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
  c fillColor 0.28 0.18 0.45
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
