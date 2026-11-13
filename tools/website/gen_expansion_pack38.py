#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 38.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: ditherpunk -> ditherpunk-atkinson-cluster-matrix, ditherpunk-bayer-crosshatch-monolith)
2. Sketch 3D (Theme: y2k_acid -> y2k-acid-hyper-starburst-polyhedron-3d, y2k-acid-chrome-metaball-lattice-3d)
3. Field Synth (Theme: jungle -> jungle-sub-bass-pressure-synth, jungle-atmospheric-pad-drone-synth)
4. Field Effects (Theme: deep_house -> deep-house-warm-analog-chorus-fx, deep-house-dusty-tape-space-delay-fx)
5. Field Pixel & Embeds (Theme: glitch_datamosh -> glitch-datamosh-chroma-displacement-scan, glitch-datamosh-macroblock-slice-corruption)
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

TMP_DIR = "/tmp/pack38_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: ditherpunk)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "ditherpunk-atkinson-cluster-matrix",
        "title": "Ditherpunk Atkinson Cluster Matrix",
        "description": "Algorithmic 1-bit drafting system rendering concentric circular threshold fields with pseudo-random error diffusion, high-density stipple clusters, and technical coordinate markers.",
        "tags": ["dither", "ditherpunk", "1-bit", "atkinson", "matrix", "procedural"],
        "code": """// Ditherpunk Atkinson Cluster Matrix - Infinite Library (Sketch 2D)
param("rings", 12.0, 6.0, 24.0);
param("sectors", 24.0, 12.0, 48.0);
param("line_weight", 1.5, 0.8, 3.0);
param("contrast", 1.15, 0.8, 1.8);

function draw(t) {
  // Midnight charcoal canvas (#0d0e15)
  background(0.05, 0.055, 0.08);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const maxR = min(width, height) * 0.42;

  // Outer technical coordinate framing
  stroke(0.85, 0.88, 0.94);
  strokeWeight(line_weight * 1.2);
  noFill();
  rect(48, 48, width - 96, height - 96);
  rect(56, 56, width - 112, height - 112);

  // Border tick marks
  stroke(0.92, 0.94, 0.98);
  strokeWeight(1.0);
  for (let x = 80; x <= width - 80; x += 36) {
    line(x, 48, x, 54);
    line(x, height - 48, x, height - 54);
  }
  for (let y = 80; y <= height - 80; y += 36) {
    line(48, y, 54, y);
    line(width - 48, y, width - 54, y);
  }

  // Radial sector ray spokes (dither matrix radial threshold)
  const numSectors = floor(sectors);
  for (let i = 0; i < numSectors; i++) {
    const angle = (i / numSectors) * Math.PI * 2.0;
    const cosA = Math.cos(angle);
    const sinA = Math.sin(angle);
    const isMajor = (i % 4 === 0);
    const r1 = isMajor ? maxR * 0.15 : maxR * 0.35;
    const r2 = isMajor ? maxR * 1.02 : maxR * 0.95;

    stroke(isMajor ? 0.95 : 0.45, isMajor ? 0.95 : 0.52, isMajor ? 0.98 : 0.65, isMajor ? 0.85 : 0.4);
    strokeWeight(isMajor ? line_weight : 1.0);
    line(cx + cosA * r1, cy + sinA * r1, cx + cosA * r2, cy + sinA * r2);
  }

  // Concentric dithered ring boundaries and crosshatch arcs
  const numRings = floor(rings);
  for (let r = 1; r <= numRings; r++) {
    const frac = r / numRings;
    const rad = frac * maxR;

    noFill();
    stroke(0.72, 0.78, 0.88, 0.65);
    strokeWeight(line_weight);
    ellipse(cx, cy, rad * 2.0, rad * 2.0);

    // Stepped crosshatch chords
    if (r % 2 === 0) {
      stroke(0.88, 0.92, 0.98, 0.5);
      strokeWeight(1.0);
      const chordStep = 18;
      for (let k = -rad; k <= rad; k += chordStep) {
        const halfSpan = Math.sqrt(Math.max(0, rad * rad - k * k));
        line(cx - halfSpan, cy + k, cx + halfSpan, cy + k);
      }
    }
  }

  // Center crosshairs
  stroke(1.0, 1.0, 1.0);
  strokeWeight(1.5);
  line(cx - 32, cy, cx + 32, cy);
  line(cx, cy - 32, cx, cy + 32);
}
"""
    },
    {
        "slug": "ditherpunk-bayer-crosshatch-monolith",
        "title": "Ditherpunk Bayer Crosshatch Monolith",
        "description": "Procedural 1-bit structural composition simulating ordered Bayer matrix crosshatching, isometric geometric stepped monoliths, and dense ordered dither field gradients.",
        "tags": ["dither", "ditherpunk", "1-bit", "bayer", "crosshatch", "monolith"],
        "code": """// Ditherpunk Bayer Crosshatch Monolith - Infinite Library (Sketch 2D)
param("tiers", 5.0, 3.0, 8.0);
param("line_weight", 1.4, 0.8, 3.0);
param("hatch_density", 14.0, 8.0, 24.0);
param("contrast", 1.2, 0.8, 1.8);

function draw(t) {
  // Midnight ground (#0c0d14)
  background(0.047, 0.051, 0.078);

  const cx = width * 0.5;
  const cy = height * 0.52;
  const w = width;
  const h = height;

  // 1. Framing graticule
  stroke(0.82, 0.86, 0.94);
  strokeWeight(line_weight * 1.2);
  noFill();
  rect(48, 48, w - 96, h - 96);
  rect(56, 56, w - 112, h - 112);

  // Calibration tick marks
  stroke(0.90, 0.92, 0.96);
  strokeWeight(1.0);
  for (let x = 80; x <= w - 80; x += 36) {
    line(x, 48, x, 54);
    line(x, h - 48, x, h - 54);
  }
  for (let y = 80; y <= h - 80; y += 36) {
    line(48, y, 54, y);
    line(w - 48, y, w - 54, y);
  }

  // 2. Stepped Isometric Monolith Tiers
  const nTiers = floor(tiers);
  const isoAngle = Math.PI / 6.0; // 30 degrees
  const cosIso = Math.cos(isoAngle);
  const sinIso = Math.sin(isoAngle);

  for (let i = nTiers - 1; i >= 0; i--) {
    const frac = i / nTiers;
    const tierW = (180.0 + (nTiers - 1 - i) * 60.0);
    const tierH = 46.0;
    const baseY = cy + (i - nTiers * 0.5) * 68.0;

    // Center apex of isometric cube
    const px = cx;
    const py = baseY;

    // Vertices of top isometric diamond
    const xTop = px;
    const yTop = py - tierW * 0.5 * sinIso;
    const xRight = px + tierW * 0.5 * cosIso;
    const yRight = py;
    const xBottom = px;
    const yBottom = py + tierW * 0.5 * sinIso;
    const xLeft = px - tierW * 0.5 * cosIso;
    const yLeft = py;

    // Top face ordered crosshatch (Bayer level 1: high reflectance)
    stroke(0.94, 0.96, 0.98);
    strokeWeight(line_weight);
    line(xTop, yTop, xRight, yRight);
    line(xRight, yRight, xBottom, yBottom);
    line(xBottom, yBottom, xLeft, yLeft);
    line(xLeft, yLeft, xTop, yTop);

    const step = 16.0;
    for (let d = -tierW * 0.4; d <= tierW * 0.4; d += step) {
      line(px + d * cosIso, py + d * sinIso - tierW * 0.25 * sinIso,
           px + d * cosIso, py + d * sinIso + tierW * 0.25 * sinIso);
    }

    // Left vertical face (Bayer level 2: medium hatch)
    stroke(0.68, 0.74, 0.86);
    strokeWeight(1.0);
    line(xLeft, yLeft, xLeft, yLeft + tierH);
    line(xBottom, yBottom, xBottom, yBottom + tierH);
    line(xLeft, yLeft + tierH, xBottom, yBottom + tierH);

    for (let k = 4.0; k < tierH; k += 8.0) {
      line(xLeft, yLeft + k, xBottom, yBottom + k);
    }

    // Right vertical face (Bayer level 3: dense crosshatch)
    stroke(0.42, 0.48, 0.62);
    line(xRight, yRight, xRight, yRight + tierH);
    line(xBottom, yBottom + tierH, xRight, yRight + tierH);

    for (let k = 3.0; k < tierH; k += 6.0) {
      line(xBottom, yBottom + k, xRight, yRight + k);
      line(xBottom + (xRight - xBottom) * (k / tierH), yBottom + k,
           xBottom + (xRight - xBottom) * (k / tierH), yBottom + tierH);
    }
  }

  // Datum indicator
  stroke(1.0, 1.0, 1.0);
  strokeWeight(1.2);
  line(cx - 24, h - 32, cx + 24, h - 32);
  line(cx, h - 44, cx, h - 20);
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: y2k_acid)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "y2k-acid-hyper-starburst-polyhedron-3d",
        "title": "Y2K Acid Hyper Starburst Polyhedron 3D",
        "description": "Monumental Y2K cyber-rave 3D composition featuring spiked metallic starburst clusters, iridescent rotating orbital rings, and hyper-stylized rave geometric facets.",
        "tags": ["y2k", "acid", "3d", "rave", "starburst", "cyber", "chrome"],
        "code": """// Y2K Acid Hyper Starburst Polyhedron 3D - Infinite Library (Sketch 3D)
param("spin", 0.45, 0.1, 1.5);
param("scale_factor", 1.2, 0.5, 2.5);
param("spikes", 8.0, 4.0, 16.0);
param("ring_count", 3.0, 1.0, 6.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.48 + sin(time * 0.2) * 0.08);
  rotateY(time * 0.35 + 0.4);

  // Central polyhedral chrome starburst core (#d4d8e8)
  fill(0.83, 0.85, 0.91);
  box(0.55 * scale_factor, 0.55 * scale_factor, 0.55 * scale_factor);

  // Spiked starburst pylons projecting along 3 axes
  const numSpikes = floor(spikes);
  for (let i = 0; i < numSpikes; i++) {
    const angle = (i / numSpikes) * Math.PI * 2.0;
    push();
    rotateZ(angle);
    rotateY(i * 0.4);
    translate(0.65 * scale_factor, 0, 0);

    // Alternate electric rave accents: neon magenta (#ff007f) and electric cyan (#00f0ff)
    if (i % 2 === 0) {
      fill(1.0, 0.0, 0.5);
    } else {
      fill(0.0, 0.94, 1.0);
    }
    box(0.65 * scale_factor, 0.08 * scale_factor, 0.08 * scale_factor);
    pop();
  }

  // Interlocking orbital cybernetic rings
  const nRings = floor(ring_count);
  for (let r = 0; r < nRings; r++) {
    push();
    rotateX(0.7 * r + time * 0.15);
    rotateY(0.9 * r + time * 0.22);

    const rad = (0.95 + r * 0.35) * scale_factor;
    const segments = 24;
    for (let s = 0; s < segments; s++) {
      const a1 = (s / segments) * Math.PI * 2.0;
      const x1 = Math.cos(a1) * rad;
      const y1 = Math.sin(a1) * rad;

      push();
      translate(x1, y1, 0);
      rotateZ(a1);
      if (r === 0) {
        fill(0.92, 0.94, 0.98); // Chrome silver
      } else if (r === 1) {
        fill(0.46, 1.0, 0.01); // Lime green (#76ff03)
      } else {
        fill(0.75, 0.2, 0.9);  // Electric violet
      }
      box(0.18 * scale_factor, 0.05 * scale_factor, 0.05 * scale_factor);
      pop();
    }
    pop();
  }

  pop();
}
"""
    },
    {
        "slug": "y2k-acid-chrome-metaball-lattice-3d",
        "title": "Y2K Acid Chrome Metaball Lattice 3D",
        "description": "Procedural 3D liquid mercury structure simulating floating cybernetic chrome spheres, interlocking helical satellite antennas, and reflective metallic rave plinths.",
        "tags": ["y2k", "acid", "3d", "chrome", "metaball", "rave", "helical"],
        "code": """// Y2K Acid Chrome Metaball Lattice 3D - Infinite Library (Sketch 3D)
param("spin", 0.42, 0.1, 1.5);
param("scale_factor", 1.15, 0.5, 2.5);
param("nodes", 6.0, 3.0, 12.0);
param("helix_turns", 3.0, 1.0, 6.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.52 + sin(time * 0.18) * 0.06);
  rotateY(time * 0.32 + 0.35);

  // Central chrome pedestal (#1a1829)
  push();
  translate(0, 1.25 * scale_factor, 0);
  fill(0.12, 0.11, 0.18);
  box(2.2 * scale_factor, 0.12, 2.2 * scale_factor);
  pop();

  // Floating cluster of cybernetic chrome metaball nodes
  const nNodes = floor(nodes);
  for (let i = 0; i < nNodes; i++) {
    const a = (i / nNodes) * Math.PI * 2.0;
    const r = (0.75 + sin(i * 1.5 + time * 0.4) * 0.25) * scale_factor;
    const y = (cos(i * 2.1 + time * 0.3) * 0.45) * scale_factor;

    push();
    translate(Math.cos(a) * r, y, Math.sin(a) * r);

    // Alternate chrome silver, electric cyan (#00ffff), and hot magenta (#ff007f)
    if (i % 3 === 0) {
      fill(0.88, 0.90, 0.96); // Chrome silver
    } else if (i % 3 === 1) {
      fill(0.0, 0.95, 1.0);  // Cyan
    } else {
      fill(1.0, 0.0, 0.55); // Magenta
    }

    box(0.38 * scale_factor, 0.38 * scale_factor, 0.38 * scale_factor);
    pop();
  }

  // Interlocking double-helix chrome strand wraps
  const steps = 36;
  for (let s = 0; s < steps; s++) {
    const frac = s / steps;
    const angle = frac * Math.PI * 2.0 * helix_turns + time * 0.6;
    const rad = 1.05 * scale_factor;
    const yPos = (frac - 0.5) * 2.2 * scale_factor;

    push();
    translate(Math.cos(angle) * rad, yPos, Math.sin(angle) * rad);
    fill(0.46, 1.0, 0.01); // Lime rave accent (#76ff03)
    box(0.09 * scale_factor, 0.09 * scale_factor, 0.09 * scale_factor);
    pop();

    push();
    translate(Math.cos(angle + Math.PI) * rad, yPos, Math.sin(angle + Math.PI) * rad);
    fill(0.95, 0.95, 0.98); // High-specular chrome
    box(0.09 * scale_factor, 0.09 * scale_factor, 0.09 * scale_factor);
    pop();
  }

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: jungle)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "jungle-sub-bass-pressure-synth",
        "title": "Jungle Sub Bass Pressure Synth",
        "description": "Heavyweight jungle 808 sub-bass synthesizer engineered for low-end sound system pressure with pitch-glide envelope dive, harmonic saturation drive, and resonant low-pass control.",
        "note_root": 36, # C1
        "params": {
            "cutoff": 450.0,
            "envMod": 0.65,
            "decay": 0.32,
            "drive": 2.4,
            "sub": 0.50
        },
        "code": """param float cutoff = 450.0 [60.0, 3000.0];
param float envMod = 0.65 [0.0, 1.0];
param float decay = 0.32 [0.05, 0.8];
param float drive = 2.4 [1.0, 6.0];
param float sub = 0.5 [0.0, 1.0];
state float phase = 0;
state float env = 0;
state float pitchEnv = 0;
state float lastGate = 0;
state float lp1 = 0;
state float lp2 = 0;

trig = if(gate > 0.5 && lastGate <= 0.5, 1.0, 0.0);
lastGate = gate;

pitchEnv = if(trig > 0.5, 1.0, pitchEnv - pitchEnv * clamp(35.0 / sr, 0.0001, 0.05));
decayRate = clamp(1.0 / (sr * decay), 0.0001, 0.05);
env = if(trig > 0.5, 1.0, env - env * decayRate);

effFreq = clamp(freq * (1.0 + pitchEnv * 1.8), 20.0, 1500.0);
phase = (phase + effFreq / sr) % 1.0;

sinWave = sin(6.2831853 * phase);
subWave = sin(3.14159265 * phase) * sub;
raw = sinWave + subWave * 0.5;

u = raw * drive;
sat = u / (1.0 + abs(u) * 0.4);

fCut = clamp((cutoff + env * envMod * 1200.0) / sr * 3.14, 0.01, 0.45);
lp1 = lp1 + fCut * (sat - lp1);
lp2 = lp2 + fCut * (lp1 - lp2);

out = lp2 * (0.35 + env * 0.65) * 0.22;
"""
    },
    {
        "slug": "jungle-atmospheric-pad-drone-synth",
        "title": "Jungle Atmospheric Pad Drone Synth",
        "description": "Haunting 90s intelligent jungle atmospheric synthesizer with detuned dual pulse-saw oscillators, slow sweeping resonant bandpass filter, and tape-saturated drone warmth.",
        "note_root": 48, # C2
        "params": {
            "cutoff": 1800.0,
            "res": 0.55,
            "detune": 0.012,
            "lfoRate": 0.25,
            "drive": 1.6,
            "decay": 0.45
        },
        "code": """param float cutoff = 1800.0 [200.0, 6000.0];
param float res = 0.55 [0.0, 0.92];
param float detune = 0.012 [0.001, 0.05];
param float lfoRate = 0.25 [0.05, 2.0];
param float drive = 1.6 [1.0, 4.0];
param float decay = 0.45 [0.1, 1.2];
state float p1 = 0;
state float p2 = 0;
state float lfo = 0;
state float env = 0;
state float s1 = 0;
state float s2 = 0;

target = if(gate > 0.5, 1.0, 0.0);
decayRate = clamp(1.0 / (sr * decay), 0.0001, 0.05);
env = if(target > env, env + (target - env) * 0.05, env - env * decayRate);

lfo = (lfo + lfoRate / sr) % 1.0;
lfoMod = sin(6.2831853 * lfo) * 450.0;

step1 = clamp(freq * (1.0 - detune), 20.0, 4000.0) / sr;
step2 = clamp(freq * (1.0 + detune), 20.0, 4000.0) / sr;
p1 = (p1 + step1) % 1.0;
p2 = (p2 + step2) % 1.0;

saw1 = 2.0 * p1 - 1.0;
saw2 = 2.0 * p2 - 1.0;
raw = (saw1 + saw2) * 0.5;

fCut = clamp((cutoff + lfoMod + env * 1200.0) / sr * 3.14, 0.01, 0.45);
u = raw * drive - s2 * res * 1.5;
sat = u / (1.0 + abs(u) * 0.3);

s1 = s1 + fCut * (sat - s1);
s2 = s2 + fCut * (s1 - s2);

out = s2 * (0.35 + env * 0.65) * 0.28;
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: deep_house)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "deep-house-warm-analog-chorus-fx",
        "title": "Deep House Warm Analog Chorus FX",
        "description": "Lush multi-stage analog bucket-brigade chorus and spatial widening effect engineered for deep house electric pianos, synths, and basslines with warm vintage saturation.",
        "params": {
            "rate": 0.35,
            "depth": 0.55,
            "feedback": 0.38,
            "warmth": 0.65,
            "mix": 0.50
        },
        "code": """param float rate = 0.35 [0.05, 1.0];
param float depth = 0.55 [0.05, 0.95];
param float feedback = 0.38 [0.0, 0.85];
param float warmth = 0.65 [0.1, 1.0];
param float mix = 0.50 [0.0, 1.0];
state float lfo1 = 0;
state float lfo2 = 0;
state float d1 = 0;
state float d2 = 0;
state float lp = 0;
state float fb = 0;

lfo1 = (lfo1 + rate * 0.0018) % 1.0;
lfo2 = (lfo2 + rate * 0.0023 + 0.25) % 1.0;
sin1 = sin(6.2831853 * lfo1) * depth * 0.032;
sin2 = cos(6.2831853 * lfo2) * depth * 0.028;

kRate1 = clamp(0.08 + sin1, 0.02, 0.35);
kRate2 = clamp(0.09 + sin2, 0.02, 0.35);

d1 = d1 + kRate1 * ((in + fb * feedback) - d1);
d2 = d2 + kRate2 * (d1 - d2);

fWarm = clamp(0.15 + (1.0 - warmth) * 0.25, 0.05, 0.45);
lp = lp + fWarm * (d2 - lp);

sat = lp / (1.0 + abs(lp * 0.25));
fb = sat;

chorus = (d1 * 0.6 + d2 * 0.4) * 1.15;
processed = in * (1.0 - mix) + chorus * mix;
out = processed * 0.92;
"""
    },
    {
        "slug": "deep-house-dusty-tape-space-delay-fx",
        "title": "Deep House Dusty Tape Space Delay FX",
        "description": "Vintage tape echo and dub delay processor featuring wow-and-flutter tape modulation, warm asymmetrical feedback saturation, and high-frequency damping.",
        "params": {
            "time": 0.38,
            "feedback": 0.55,
            "flutter": 0.35,
            "tone": 0.65,
            "mix": 0.45
        },
        "code": """param float time = 0.38 [0.05, 0.9];
param float feedback = 0.55 [0.0, 0.88];
param float flutter = 0.35 [0.0, 1.0];
param float tone = 0.65 [0.1, 1.0];
param float mix = 0.45 [0.0, 1.0];
state float lfo = 0;
state float d1 = 0;
state float d2 = 0;
state float d3 = 0;
state float lp = 0;
state float fb = 0;

lfo = (lfo + 0.0008) % 1.0;
wMod = sin(6.2831853 * lfo) * flutter * 0.015;

kRate = clamp(0.06 + (1.0 - time) * 0.22 + wMod, 0.02, 0.35);
d1 = d1 + kRate * ((in + fb * feedback) - d1);
d2 = d2 + kRate * 0.9 * (d1 - d2);
d3 = d3 + kRate * 0.8 * (d2 - d3);

fDamp = clamp(0.12 + tone * 0.28, 0.04, 0.45);
lp = lp + fDamp * (d3 - lp);

fb = lp / (1.0 + abs(lp * 0.28));
processed = in * (1.0 - mix) + fb * mix * 1.1;
out = processed * 0.95;
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embeds (Theme: glitch_datamosh)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "glitch-datamosh-chroma-displacement-scan",
        "title": "Glitch Datamosh Chroma Displacement Scan",
        "description": "Audio-reactive datamosh shader simulating compression artifact macroblocks, chromatic RGB channel splitting, horizontal sync tearing, and phosphorescent glitch scanlines.",
        "embed_desc": "Audio-reactive datamosh visualizer with macroblock slicing and chromatic RGB channel splitting. Bass triggers horizontal buffer displacement, mids warp color channels, and highs ignite digital scanlines.",
        "tags": ["glitch", "datamosh", "shader", "webgl", "chromatic", "macroblock", "audio-reactive"],
        "params": {
            "speed": 0.4,
            "blockSize": 18.0,
            "tearIntensity": 0.75,
            "chromaSplit": 0.035,
            "contrast": 1.25
        },
        "code": """param float speed = 0.4 [0.05, 1.0];
param float blockSize = 18.0 [6.0, 40.0];
param float tearIntensity = 0.75 [0.1, 1.5];
param float chromaSplit = 0.035 [0.005, 0.08];
param float contrast = 1.25 [0.8, 1.8];

p = uv;
time = t * speed;

gridY = floor(p.y * blockSize);
sliceHash = fract(sin(gridY * 127.1 + time * 3.7) * 43758.5453);
isTear = step(0.68, sliceHash);
tearOffset = (sliceHash - 0.5) * tearIntensity * isTear;

px = fract(p.x + tearOffset);
py = p.y;

uvR = vec2(fract(px + chromaSplit * isTear), py);
uvG = vec2(px, py);
uvB = vec2(fract(px - chromaSplit * isTear), py);

valR = 0.5 + 0.5 * sin(uvR.x * 14.0 + time * 4.0 + sin(uvR.y * 22.0));
valG = 0.5 + 0.5 * sin(uvG.x * 14.0 + time * 4.0 + sin(uvG.y * 22.0) + 1.2);
valB = 0.5 + 0.5 * cos(uvB.x * 14.0 + time * 4.0 + sin(uvB.y * 22.0) + 2.4);

scanline = step(0.5, fract(p.y * 120.0)) * 0.15;
blockEdge = step(0.92, fract(p.y * blockSize)) * 0.35;

col = vec3(valR, valG, valB);
cDark = vec3(0.05, 0.04, 0.09);
col = mix(cDark, col, 0.85);
col = col + vec3(scanline) + vec3(blockEdge * isTear);

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
  vec2 p = uv;
  float time = t * 0.4;

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  float blockSize = 18.0;
  float gridY = floor(p.y * blockSize);
  float sliceHash = fract(sin(gridY * 127.1 + time * 3.7) * 43758.5453);
  float isTear = step(0.65 - b * 0.15, sliceHash);
  float tearOffset = (sliceHash - 0.5) * (0.75 + b * 1.2) * isTear;

  float px = fract(p.x + tearOffset);
  float py = p.y;

  float chromaSplit = (0.035 + m * 0.045) * isTear;
  vec2 uvR = vec2(fract(px + chromaSplit), py);
  vec2 uvG = vec2(px, py);
  vec2 uvB = vec2(fract(px - chromaSplit), py);

  float valR = 0.5 + 0.5 * sin(uvR.x * 14.0 + time * 4.0 + sin(uvR.y * 22.0));
  float valG = 0.5 + 0.5 * sin(uvG.x * 14.0 + time * 4.0 + sin(uvG.y * 22.0) + 1.2);
  float valB = 0.5 + 0.5 * cos(uvB.x * 14.0 + time * 4.0 + sin(uvB.y * 22.0) + 2.4);

  float scanline = step(0.5, fract(p.y * 120.0)) * (0.15 + h * 0.35);
  float blockEdge = step(0.92, fract(p.y * blockSize)) * (0.35 + b * 0.45);

  vec3 col = vec3(valR, valG, valB);
  vec3 cDark = vec3(0.05, 0.04, 0.09);
  col = mix(cDark, col, 0.85);
  col += vec3(scanline) + vec3(blockEdge * isTear);

  gl_FragColor = vec4(clamp(col * 1.25, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "glitch-datamosh-macroblock-slice-corruption",
        "title": "Glitch Datamosh Macroblock Slice Corruption",
        "description": "Dynamic video buffer corruption visualizer featuring quantized MPEG macroblock compression tears, temporal motion vector shearing, and stark signal distortion.",
        "embed_desc": "Audio-reactive video compression corruption visualizer. Bass fractures the spatial macroblock grid, mids shift temporal motion vector shears, and highs trigger digital noise bursts.",
        "tags": ["glitch", "datamosh", "macroblock", "compression", "buffer-tear", "audioreactive"],
        "params": {
            "speed": 0.35,
            "blockScale": 24.0,
            "sliceShear": 0.85,
            "noiseIntensity": 0.65,
            "contrast": 1.2
        },
        "code": """param float speed = 0.35 [0.05, 1.0];
param float blockScale = 24.0 [8.0, 48.0];
param float sliceShear = 0.85 [0.1, 2.0];
param float noiseIntensity = 0.65 [0.1, 1.0];
param float contrast = 1.2 [0.8, 1.8];

p = uv;
time = t * speed;

blockY = floor(p.y * blockScale);
blockX = floor(p.x * blockScale);
cellHash = fract(sin(blockX * 37.1 + blockY * 71.7 + time * 2.3) * 43758.5453);
isGlitch = step(0.72, cellHash);

shear = (cellHash - 0.5) * sliceShear * isGlitch;
px = fract(p.x + shear);
py = p.y;

grain = fract(sin(px * 1234.5 + py * 6789.1 + time * 10.0) * 98765.43);
baseR = 0.5 + 0.5 * sin(px * 18.0 + time * 3.2);
baseG = 0.5 + 0.5 * sin(py * 20.0 - time * 2.8 + 1.5);
baseB = 0.5 + 0.5 * cos((px + py) * 16.0 + time * 4.0 + 3.0);

col = vec3(baseR, baseG, baseB);
cDark = vec3(0.04, 0.04, 0.07);
col = mix(cDark, col, 0.8);

blockBorder = step(0.9, fract(p.x * blockScale)) + step(0.9, fract(p.y * blockScale));
col = mix(col, vec3(0.95, 0.95, 0.98), blockBorder * 0.25 * isGlitch);
col = col + vec3(grain * noiseIntensity * isGlitch * 0.4);

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
  vec2 p = uv;
  float time = t * 0.35;

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  float blockScale = 24.0;
  float blockY = floor(p.y * blockScale);
  float blockX = floor(p.x * blockScale);
  float cellHash = fract(sin(blockX * 37.1 + blockY * 71.7 + time * 2.3) * 43758.5453);
  float isGlitch = step(0.70 - b * 0.18, cellHash);

  float shear = (cellHash - 0.5) * (0.85 + b * 1.4) * isGlitch;
  float px = fract(p.x + shear);
  float py = p.y;

  float grain = fract(sin(px * 1234.5 + py * 6789.1 + time * 10.0) * 98765.43);
  float baseR = 0.5 + 0.5 * sin(px * 18.0 + (time + m * 0.8) * 3.2);
  float baseG = 0.5 + 0.5 * sin(py * 20.0 - time * 2.8 + 1.5);
  float baseB = 0.5 + 0.5 * cos((px + py) * 16.0 + time * 4.0 + 3.0);

  vec3 col = vec3(baseR, baseG, baseB);
  vec3 cDark = vec3(0.04, 0.04, 0.07);
  col = mix(cDark, col, 0.8);

  float blockBorder = step(0.9, fract(p.x * blockScale)) + step(0.9, fract(p.y * blockScale));
  col = mix(col, vec3(0.95, 0.95, 0.98), blockBorder * 0.25 * isGlitch);
  col += vec3(grain * (0.65 + h * 0.85) * isGlitch * 0.4);

  gl_FragColor = vec4(clamp(col * 1.2, 0.0, 1.0), 1.0);
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
    print("=== Creative Expansion Pack 38 Generator & Verification Gate Runner ===")
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
  c bg 0.04 0.035 0.065
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
  i note2 {nr}
  i note3 {nr}
  i note4 {nr}
  i note5 {nr}
  i note6 {nr}
  i note7 {nr+12}
  i note8 {nr}
  i note9 {nr}
  i note10 {nr}
  i note11 {nr}
  i note12 {nr}
  i note13 {nr}
  i note14 {nr+3}
  i note15 {nr+12}
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
                "tags": ["synth", "jungle", "audio", "dsp", "sub", "bass"],
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
                "tags": ["effect", "deep-house", "audio", "dsp", "chorus", "delay"],
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
