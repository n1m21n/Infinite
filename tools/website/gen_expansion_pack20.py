#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 20.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (acid-graphics-starburst-lattice, acid-graphics-warp-typography)
2. Sketch 3D (vaporwave-collage-ionic-colonnade-3d, vaporwave-collage-pedestal-temple-3d)
3. Field Synth (deep-house-rhodes-electric-synth, deep-house-organ-bass-synth)
4. Field Effects (jungle-amen-tape-delay-fx, jungle-sub-bass-resonator-fx)
5. Field Pixel (glitch-datamosh-buffer-tear, glitch-datamosh-pixel-displace)
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

TMP_DIR = "/tmp/pack20_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: acid_graphics)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "acid-graphics-starburst-lattice",
        "title": "Acid Graphics Starburst Lattice",
        "description": "Procedural rave-era acid graphics with concentric warped starburst contours, high-contrast chartreuse vector frames, and dense technical alignment ticks.",
        "tags": ["acid", "typography", "starburst", "vector", "drawing", "rave"],
        "code": """// Acid Graphics Starburst Lattice - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Procedural rave-era acid graphics with concentric warped starburst contours, high-contrast chartreuse vector frames, and dense technical alignment ticks.
param("zoom", 1.0, 0.5, 2.0);
param("line_weight", 2.0, 1.0, 6.0);
param("star_spikes", 8.0, 4.0, 16.0);
param("grid_density", 12.0, 6.0, 24.0);

function draw(t) {
  // Deep obsidian ground with safe luminance
  background(0.08, 0.08, 0.11);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88 * zoom;

  push();
  translate(cx, cy);

  // Outer framing boundary
  noFill();
  stroke(0.78, 1.0, 0.0); // Acid chartreuse
  strokeWeight(line_weight * 1.5);
  rect(-span * 0.5, -span * 0.5, span, span);

  // High-contrast coordinate crosshair grid
  stroke(0.28, 0.32, 0.40);
  strokeWeight(1.0);
  const step = span / grid_density;
  for (let k = -span * 0.5; k <= span * 0.5; k += step) {
    line(k, -span * 0.5, k, span * 0.5);
    line(-span * 0.5, k, span * 0.5, k);
  }

  // Multi-pointed rave acid starbursts
  const spikes = floor(star_spikes);
  const count = spikes * 2;
  const rOut = span * 0.38;
  const rIn = span * 0.14;

  // Starburst drop shadow/halo
  noFill();
  stroke(1.0, 0.0, 0.5, 0.85); // Electric magenta
  strokeWeight(line_weight + 2.0);
  beginShape();
  for (let i = 0; i <= count; i++) {
    const a = (i * 6.2831853) / count;
    const r = (i % 2 === 0 ? rOut * 1.04 : rIn * 0.95);
    vertex(cos(a) * r, sin(a) * r);
  }
  endShape();

  // Primary acid chartreuse starburst
  fill(0.78, 1.0, 0.0);
  stroke(0.08, 0.08, 0.11);
  strokeWeight(line_weight);
  beginShape();
  for (let i = 0; i <= count; i++) {
    const a = (i * 6.2831853) / count;
    const r = (i % 2 === 0 ? rOut : rIn);
    vertex(cos(a) * r, sin(a) * r);
  }
  endShape();

  // Concentric interior cutout rings
  fill(0.08, 0.08, 0.11);
  circle(0, 0, rIn * 1.3);

  noFill();
  stroke(1.0, 0.0, 0.5);
  strokeWeight(line_weight + 1.0);
  circle(0, 0, rIn * 0.9);

  stroke(0.95, 0.96, 1.0);
  strokeWeight(line_weight);
  circle(0, 0, rIn * 0.5);

  // Target reticle crosshairs
  stroke(0.78, 1.0, 0.0);
  line(-rOut * 1.25, 0, rOut * 1.25, 0);
  line(0, -rOut * 1.25, 0, rOut * 1.25);

  // Technical edge ticks
  stroke(0.95, 0.96, 1.0);
  strokeWeight(1.5);
  for (let d = -span * 0.45; d <= span * 0.45; d += span * 0.05) {
    line(d, -span * 0.5, d, -span * 0.5 + 12);
    line(d, span * 0.5 - 12, d, span * 0.5);
    line(-span * 0.5, d, -span * 0.5 + 12, d);
    line(span * 0.5 - 12, d, span * 0.5, d);
  }

  pop();
}
"""
    },
    {
        "slug": "acid-graphics-warp-typography",
        "title": "Acid Graphics Warp Typography",
        "description": "Algorithmic Warp-era typographic layout with curvilinear sine-distorted letterforms, bold chromatic contrast rules, and geometric registration marks.",
        "tags": ["acid", "typography", "warp", "layout", "procedural", "drawing"],
        "code": """// Acid Graphics Warp Typography - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Algorithmic Warp-era typographic layout with curvilinear sine-distorted letterforms, bold chromatic contrast rules, and geometric registration marks.
param("zoom", 1.0, 0.5, 2.0);
param("line_weight", 2.2, 1.0, 6.0);
param("wave_freq", 3.5, 1.0, 8.0);
param("accent_scale", 1.1, 0.5, 2.0);

function draw(t) {
  background(0.065, 0.055, 0.09);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88 * zoom;

  push();
  translate(cx, cy);

  noFill();
  stroke(0.78, 1.0, 0.0); // Acid chartreuse
  strokeWeight(line_weight * 1.5);
  rect(-span * 0.5, -span * 0.5, span, span);

  stroke(0.3, 0.22, 0.42);
  strokeWeight(1.5);
  const bands = 14;
  for (let b = 0; b < bands; b++) {
    const y0 = (-0.45 + (b / bands) * 0.9) * span;
    beginShape();
    for (let x = -span * 0.48; x <= span * 0.48; x += span * 0.04) {
      const w = sin(x * (0.012 * wave_freq) + b * 0.4) * (span * 0.06);
      vertex(x, y0 + w);
    }
    endShape();
  }

  noStroke();
  fill(0.78, 1.0, 0.0);
  rect(-span * 0.38, -span * 0.22, span * 0.16 * accent_scale, span * 0.55 * accent_scale);
  rect(-span * 0.14, -span * 0.22, span * 0.14 * accent_scale, span * 0.32 * accent_scale);

  fill(1.0, 0.0, 0.5);
  rect(-span * 0.14, span * 0.16, span * 0.45 * accent_scale, span * 0.14 * accent_scale);
  rect(span * 0.18, -span * 0.32, span * 0.18 * accent_scale, span * 0.62 * accent_scale);

  stroke(0.96, 0.98, 1.0);
  strokeWeight(line_weight);
  noFill();
  circle(span * 0.22, 0, span * 0.22 * accent_scale);
  circle(span * 0.22, 0, span * 0.08);

  stroke(0.78, 1.0, 0.0);
  line(-span * 0.45, span * 0.38, span * 0.45, span * 0.38);
  for (let s = -span * 0.42; s <= span * 0.42; s += span * 0.06) {
    line(s, span * 0.36, s, span * 0.40);
  }

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: vaporwave_collage)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "vaporwave-collage-ionic-colonnade-3d",
        "title": "Vaporwave Collage Ionic Colonnade 3D",
        "description": "Neoclassical postmodern architectural colonnade with fluted marble columns, tiered stepped podiums, floating platonic spheres, and pastel aesthetic volumes.",
        "tags": ["vaporwave", "neoclassical", "colonnade", "architecture", "marble", "3d"],
        "code": """// Vaporwave Collage Ionic Colonnade 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Neoclassical postmodern architectural colonnade with fluted marble columns, tiered stepped podiums, floating platonic spheres, and pastel aesthetic volumes.
param("colonnade_width", 1.0, 0.5, 2.0);
param("column_height", 1.0, 0.5, 2.0);
param("rotation_speed", 0.35, 0.1, 1.2);
param("sphere_orbit", 1.0, 0.4, 2.0);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.38 + sin(time * 0.2) * 0.05);
  rotateY(time * 0.35 + 0.45);

  fill(0.92, 0.90, 0.86);
  push();
  translate(0, -0.85, 0);
  box(1.7 * colonnade_width, 0.12, 1.2);
  translate(0, 0.09, 0);
  box(1.4 * colonnade_width, 0.09, 0.95);
  pop();

  const count = 4;
  for (let c = 0; c < count; c++) {
    const x = ((c / (count - 1)) - 0.5) * 1.1 * colonnade_width;

    push();
    translate(x, -0.22, 0);

    fill(0.88, 0.86, 0.82);
    push();
    translate(0, -0.48 * column_height, 0);
    box(0.18, 0.08, 0.18);
    pop();

    fill(0.95, 0.94, 0.92);
    cylinder(0.065, 0.9 * column_height, 14);

    fill(0.90, 0.88, 0.84);
    push();
    translate(0, 0.48 * column_height, 0);
    box(0.20, 0.08, 0.20);
    fill(1.0, 0.44, 0.81);
    push();
    translate(-0.09, 0, 0);
    cylinder(0.045, 0.18, 10);
    pop();
    push();
    translate(0.09, 0, 0);
    cylinder(0.045, 0.18, 10);
    pop();
    pop();

    pop();
  }

  fill(0.91, 0.89, 0.85);
  push();
  translate(0, 0.35 * column_height, 0);
  box(1.5 * colonnade_width, 0.16, 0.45);

  translate(0, 0.11, 0);
  fill(0.94, 0.93, 0.90);
  box(1.6 * colonnade_width, 0.08, 0.52);
  pop();

  fill(0.02, 0.80, 0.99);
  push();
  translate(0.55 * sphere_orbit, 0.65, 0.25);
  sphere(0.16, 16, 16);
  pop();

  fill(0.02, 1.0, 0.63);
  push();
  translate(-0.5 * sphere_orbit, -0.15, 0.4);
  sphere(0.12, 14, 14);
  pop();

  pop();
}
"""
    },
    {
        "slug": "vaporwave-collage-pedestal-temple-3d",
        "title": "Vaporwave Collage Pedestal Temple 3D",
        "description": "Monumental stepped temple pavilion with cantilevered entablature beams, hollow archways, tiered ceremonial plinths, and geometric sun disc accents.",
        "tags": ["vaporwave", "temple", "pedestal", "architecture", "geometry", "3d"],
        "code": """// Vaporwave Collage Pedestal Temple 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Monumental stepped temple pavilion with cantilevered entablature beams, hollow archways, tiered ceremonial plinths, and geometric sun disc accents.
param("temple_scale", 1.0, 0.5, 2.0);
param("arch_height", 1.0, 0.6, 1.8);
param("rotation_speed", 0.35, 0.1, 1.2);
param("disc_offset", 0.45, 0.1, 0.9);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.40 + sin(time * 0.2) * 0.04);
  rotateY(time * 0.32 + 0.5);

  fill(0.92, 0.90, 0.86);
  push();
  translate(0, -0.88, 0);
  box(1.7 * temple_scale, 0.12, 1.7 * temple_scale);
  translate(0, 0.10, 0);
  box(1.4 * temple_scale, 0.10, 1.4 * temple_scale);
  translate(0, 0.09, 0);
  box(1.15 * temple_scale, 0.08, 1.15 * temple_scale);
  pop();

  fill(0.95, 0.94, 0.92);
  for (let c = 0; c < 4; c++) {
    const ang = c * 1.5707963 + 0.785398;
    const px = cos(ang) * 0.55 * temple_scale;
    const pz = sin(ang) * 0.55 * temple_scale;
    push();
    translate(px, -0.22, pz);
    box(0.14, 0.95 * arch_height, 0.14);
    fill(1.0, 0.44, 0.81);
    translate(0, 0.5 * arch_height, 0);
    box(0.20, 0.06, 0.20);
    pop();
  }

  fill(0.88, 0.86, 0.82);
  push();
  translate(0, -0.15, 0);
  push();
  translate(-0.25, 0, 0);
  box(0.12, 0.8 * arch_height, 0.25);
  pop();
  push();
  translate(0.25, 0, 0);
  box(0.12, 0.8 * arch_height, 0.25);
  pop();
  push();
  translate(0, 0.44 * arch_height, 0);
  box(0.62, 0.12, 0.25);
  pop();
  pop();

  fill(0.93, 0.92, 0.89);
  push();
  translate(0, 0.45 * arch_height, 0);
  box(1.45 * temple_scale, 0.14, 1.45 * temple_scale);
  translate(0, 0.10, 0);
  box(1.25 * temple_scale, 0.08, 1.25 * temple_scale);
  pop();

  fill(0.02, 0.80, 0.99);
  push();
  translate(0, 0.75 * arch_height + disc_offset * 0.3, 0);
  sphere(0.22, 16, 16);
  pop();

  fill(0.02, 1.0, 0.63);
  push();
  translate(0.65, 0.1, 0.35);
  sphere(0.10, 12, 12);
  pop();

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: deep_house)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "deep-house-rhodes-electric-synth",
        "title": "Deep House Rhodes Electric Synth",
        "description": "Soulful deep house polyphonic synthesizer with detuned sine-saw blend, velocity-controlled resonant lowpass warmth, and lush harmonic decay.",
        "tags": ["synth", "deep-house", "chords", "polyphonic", "analog", "audio"],
        "note_root": 48,
        "params": {
            "cutoff": 1600.0,
            "resonance": 0.28,
            "detune": 0.008,
            "decay": 0.55,
            "warmth": 0.45
        },
        "code": """param float cutoff = 1600.0 [300.0, 6000.0]
param float resonance = 0.28 [0.0, 0.85]
param float detune = 0.008 [0.001, 0.03]
param float decay = 0.55 [0.1, 2.0]
param float warmth = 0.45 [0.0, 1.0]
state float p1 = 0
state float p2 = 0
state float env = 0
state float lp1 = 0
state float lp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decay), 0.00004, 0.02)
env = if(target > env, env + (target - env) * 0.25, env - env * decayRate)

fBase = clamp(freq, 35.0, 3500.0)
p1 = (p1 + fBase * (1.0 - detune) / sr) % 1.0
p2 = (p2 + fBase * (1.0 + detune) / sr) % 1.0

s1 = sin(p1 * 6.2831853)
s2 = (p2 * 2.0 - 1.0)
raw = (s1 * (1.0 - warmth * 0.5) + s2 * (0.3 + warmth * 0.5)) * env

fCut = clamp((cutoff + env * 2200.0) / sr * 2.2, 0.01, 0.45)
lp1 = lp1 + fCut * (raw - lp1 - resonance * lp2)
lp2 = lp2 + fCut * (lp1 - lp2)

u = lp2 * 1.35
sat = clamp(u - 0.2 * u * u * u, -0.92, 0.92)
out = sat * 0.36
"""
    },
    {
        "slug": "deep-house-organ-bass-synth",
        "title": "Deep House Organ Bass Synth",
        "description": "Classic Chicago deep house organ bass synthesizer with punchy percussive transient click, sub-harmonic octave warmth, and tight saturated decay.",
        "tags": ["synth", "deep-house", "bass", "organ", "sub", "audio"],
        "note_root": 36,
        "params": {
            "modRatio": 2.0,
            "modIndex": 1.35,
            "cutoff": 2200.0,
            "decay": 0.38,
            "subLevel": 0.35
        },
        "code": """param float modRatio = 2.0 [1.0, 4.0]
param float modIndex = 1.35 [0.0, 3.0]
param float cutoff = 2200.0 [400.0, 5500.0]
param float decay = 0.38 [0.08, 1.2]
param float subLevel = 0.35 [0.0, 0.8]
state float pCar = 0
state float pMod = 0
state float pSub = 0
state float env = 0
state float lp1 = 0
state float lp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decay), 0.00008, 0.02)
env = if(target > env, env + (target - env) * 0.3, env - env * decayRate)

fBase = clamp(freq, 30.0, 2000.0)
fMod = fBase * modRatio
fSub = fBase * 0.5

pMod = (pMod + fMod / sr) % 1.0
mSine = sin(pMod * 6.2831853) * modIndex * env

pCar = (pCar + (fBase + mSine * fBase) / sr) % 1.0
cSine = sin(pCar * 6.2831853)

pSub = (pSub + fSub / sr) % 1.0
subSine = sin(pSub * 6.2831853) * subLevel

raw = (cSine * 0.75 + subSine) * env

fCut = clamp((cutoff + env * 1600.0) / sr * 2.2, 0.01, 0.45)
lp1 = lp1 + fCut * (raw - lp1 - 0.25 * lp2)
lp2 = lp2 + fCut * (lp1 - lp2)

u = lp2 * 1.35
sat = clamp(u - 0.22 * u * u * u, -0.92, 0.92)

out = sat * 0.36
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: jungle)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "jungle-amen-tape-delay-fx",
        "title": "Jungle Amen Tape Delay FX",
        "description": "Vintage sound system tape delay processor with 16th-sync ping-pong repeats, high-frequency tape saturation damping, and analog flutter modulation.",
        "tags": ["effect", "jungle", "delay", "tape", "echo", "audio"],
        "params": {
            "feedback": 0.54,
            "damping": 0.42,
            "tone": 2600.0,
            "mix": 0.6,
            "flutter": 0.25
        },
        "code": """param float feedback = 0.54 [0.1, 0.85]
param float damping = 0.42 [0.05, 0.9]
param float tone = 2600.0 [400.0, 6000.0]
param float mix = 0.6 [0.0, 1.0]
param float flutter = 0.25 [0.0, 0.8]
state float d1 = 0
state float d2 = 0
state float d3 = 0
state float d4 = 0
state float lp = 0
state float lfo = 0

lfo = (lfo + 0.48 / sr) % 1.0
flut = sin(lfo * 6.2831853) * flutter * 0.05

sig = in + d4 * feedback

d1 = d1 + 0.25 * (sig - d1)
d2 = d2 + 0.18 * (d1 - d2)
d3 = d3 + 0.12 * (d2 - d3)
d4 = d4 + 0.09 * (d3 - d4)

del = (d1 * 0.36 + d2 * 0.3 + d3 * 0.2 + d4 * 0.14) * (1.0 + flut)

fCut = clamp(tone / sr * 2.8, 0.015, 0.45)
lp = lp + fCut * (del - lp)

wet = (del * (1.0 - damping) + lp * damping) * 1.25
out = (in * (1.0 - mix) + wet * mix) * 1.35
"""
    },
    {
        "slug": "jungle-sub-bass-resonator-fx",
        "title": "Jungle Sub Bass Resonator FX",
        "description": "Heavy sound system sub-bass filter and harmonic resonator with tuned low-end boost, asymmetric saturation drive, and transient preservation.",
        "tags": ["effect", "jungle", "sub", "resonator", "filter", "audio"],
        "params": {
            "centerFreq": 68.0,
            "boost": 2.2,
            "drive": 1.8,
            "tone": 2800.0,
            "mix": 0.75
        },
        "code": """param float centerFreq = 68.0 [40.0, 160.0]
param float boost = 2.2 [1.0, 4.5]
param float drive = 1.8 [0.8, 4.0]
param float tone = 2800.0 [500.0, 6000.0]
param float mix = 0.75 [0.0, 1.0]
state float subLp1 = 0
state float subLp2 = 0
state float toneLp = 0

fSub = clamp(centerFreq / sr * 3.14159, 0.003, 0.1)
subLp1 = subLp1 + fSub * (in - subLp1)
subLp2 = subLp2 + fSub * (subLp1 - subLp2)
subBoost = subLp2 * boost

highPart = in - subLp2
u = (highPart + subBoost * 0.5) * drive
sat = clamp(u - 0.24 * u * u * u, -0.95, 0.95)

fTone = clamp(tone / sr * 2.8, 0.02, 0.45)
toneLp = toneLp + fTone * (sat - toneLp)

wet = subBoost * 0.55 + toneLp * 0.6
out = (in * (1.0 - mix) + wet * mix) * 1.3
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embeds (Theme: glitch_datamosh)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "glitch-datamosh-buffer-tear",
        "title": "Glitch Datamosh Buffer Tear",
        "description": "Algorithmic video datamosh compression tearing shader with chromatic RGB channel separation, horizontal block motion vectors, and scanline glitch persistence.",
        "embed_desc": "Algorithmic video datamosh compression tearing shader with chromatic RGB channel separation, horizontal block motion vectors, and scanline glitch persistence. Bass drives macro compression slice tearing, mids distort chromatic RGB offset, highs illuminate sparkling bit-shift noise.",
        "tags": ["pixel", "glitch", "datamosh", "rgb", "compression", "shader"],
        "params": {
            "sliceDensity": 24.0,
            "tearIntensity": 1.4,
            "rgbOffset": 0.08,
            "scanlineDarkness": 0.35,
            "persistence": 1.1
        },
        "code": """param float sliceDensity = 24.0 [8.0, 48.0]
param float tearIntensity = 1.4 [0.2, 3.0]
param float rgbOffset = 0.08 [0.01, 0.25]
param float scanlineDarkness = 0.35 [0.0, 0.8]
param float persistence = 1.1 [0.4, 2.5]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2
time = t * 0.7

slice = floor(uv.y * sliceDensity)
sliceNoise = sin(slice * 17.3 + floor(time * 8.0) * 4.1)

tear = if(sin(sliceNoise * 5.5 + time * 2.0) > 0.4, sin(slice * 8.9) * 0.28 * tearIntensity, 0.0)
pTorn = vec2(p.x + tear, p.y)

grid = sin(pTorn.x * 12.0) * cos(pTorn.y * 12.0)
rings = sin(length(pTorn) * 16.0 - time * 3.0)

rChan = clamp(sin(grid * 4.0 + rings * 2.0 + pTorn.x * 2.0 + rgbOffset * 6.0) * 0.5 + 0.5, 0.0, 1.0)
gChan = clamp(sin(grid * 4.0 + rings * 2.0 + pTorn.y * 2.0) * 0.5 + 0.5, 0.0, 1.0)
bChan = clamp(sin(grid * 4.0 + rings * 2.0 - pTorn.x * 2.0 - rgbOffset * 6.0) * 0.5 + 0.5, 0.0, 1.0)

cBg = vec3(0.06, 0.04, 0.10)
cR = vec3(1.0, 0.08, 0.35)
cG = vec3(0.05, 0.95, 0.55)
cB = vec3(0.15, 0.65, 1.0)

col = cBg + cR * rChan * 0.65 + cG * gChan * 0.55 + cB * bChan * 0.65

scan = clamp(sin(uv.y * 540.0) * 0.5 + 0.5, 0.0, 1.0)
col = col * (1.0 - scan * scanlineDarkness)

alpha = 1.0
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
  float time = t * 0.7;

  float bass = audio_bass * 1.5;
  float mid = audio_mid * 1.3;
  float high = audio_high * 1.8;

  float sliceDensity = 24.0 + mid * 12.0;
  float slice = floor(uv.y * sliceDensity);
  float sliceNoise = sin(slice * 17.3 + floor(time * 8.0) * 4.1);

  float tear = 0.0;
  if (sin(sliceNoise * 5.5 + time * 2.0) > 0.35) {
    tear = sin(slice * 8.9) * (0.28 + bass * 0.45);
  }
  vec2 pTorn = vec2(p.x + tear, p.y);

  float grid = sin(pTorn.x * (12.0 + bass * 4.0)) * cos(pTorn.y * 12.0);
  float rings = sin(length(pTorn) * 16.0 - time * 3.0 + mid * 4.0);

  float rgbOffset = 0.08 + mid * 0.12;
  float rChan = clamp(sin(grid * 4.0 + rings * 2.0 + pTorn.x * 2.0 + rgbOffset * 6.0) * 0.5 + 0.5, 0.0, 1.0);
  float gChan = clamp(sin(grid * 4.0 + rings * 2.0 + pTorn.y * 2.0) * 0.5 + 0.5, 0.0, 1.0);
  float bChan = clamp(sin(grid * 4.0 + rings * 2.0 - pTorn.x * 2.0 - rgbOffset * 6.0) * 0.5 + 0.5, 0.0, 1.0);

  vec3 cBg = vec3(0.06, 0.04, 0.10);
  vec3 cR = vec3(1.0, 0.08, 0.35);
  vec3 cG = vec3(0.05, 0.95, 0.55);
  vec3 cB = vec3(0.15, 0.65, 1.0);

  vec3 col = cBg + cR * rChan * 0.65 + cG * gChan * 0.55 + cB * bChan * 0.65;

  float scan = clamp(sin(uv.y * 540.0) * 0.5 + 0.5, 0.0, 1.0);
  col = col * (1.0 - scan * 0.35);

  float sparkle = pow(max(0.0, sin(pTorn.x * 60.0 + pTorn.y * 60.0 + time * 12.0)), 16.0) * high * 1.5;
  col += vec3(sparkle);

  gl_FragColor = vec4(col, 1.0);
}
"""
    },
    {
        "slug": "glitch-datamosh-pixel-displace",
        "title": "Glitch Datamosh Pixel Displace",
        "description": "Digital compression artifact displacement shader with quantized mosaic buffer slices, feedback edge bleeding, and dynamic scanline wave distortion.",
        "embed_desc": "Digital compression artifact displacement shader with quantized mosaic buffer slices, feedback edge bleeding, and dynamic scanline wave distortion. Bass modulates quantized pixel block fragmentation, mids warp scanline wave shearing, highs trigger phosphor edge luminance flares.",
        "tags": ["pixel", "glitch", "displace", "mosaic", "feedback", "shader"],
        "params": {
            "mosaicScale": 28.0,
            "waveSpeed": 0.6,
            "bleedAmount": 0.7,
            "scanlineDarkness": 0.35,
            "contrast": 1.25
        },
        "code": """param float mosaicScale = 28.0 [10.0, 50.0]
param float waveSpeed = 0.6 [0.1, 2.0]
param float bleedAmount = 0.7 [0.1, 1.5]
param float scanlineDarkness = 0.35 [0.0, 0.8]
param float contrast = 1.25 [0.5, 2.5]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.4
time = t * waveSpeed

mosaic = floor(p * mosaicScale) / mosaicScale
wave = sin(mosaic.y * 8.0 + time * 2.0)
gate = sin(mosaic.y * 14.0 + floor(time * 6.0))
disp = if(gate > 0.4, 0.18, 0.0)
pDisp = vec2(mosaic.x + disp, mosaic.y + wave * 0.08)

r = length(pDisp)
ang = atan2(pDisp.y, pDisp.x)

f1 = clamp(sin(r * 14.0 - time * 2.5 + ang * 3.0) * 0.5 + 0.5, 0.0, 1.0)
f2 = clamp(cos(pDisp.x * 10.0 + pDisp.y * 8.0 + time) * 0.5 + 0.5, 0.0, 1.0)
f3 = clamp(sin(r * 22.0 - time * 4.0) * 0.5 + 0.5, 0.0, 1.0)

cDark = vec3(0.04, 0.05, 0.09)
cCyan = vec3(0.0, 0.92, 0.98)
cYellow = vec3(1.0, 0.92, 0.15)
cViolet = vec3(0.85, 0.15, 0.95)

base = mix(cDark, cCyan, f1 * 0.7)
base = mix(base, cYellow, f2 * 0.6)
base = mix(base, cViolet, f3 * 0.5)

scan = clamp(sin(uv.y * 480.0) * 0.5 + 0.5, 0.0, 1.0)
col = base * (1.0 - scan * scanlineDarkness)

alpha = 1.0
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
  float time = t * 0.6;

  float bass = audio_bass * 1.4;
  float mid = audio_mid * 1.5;
  float high = audio_high * 1.8;

  float scale = 28.0 - bass * 10.0;
  vec2 mosaic = floor(p * scale) / scale;

  float wave = sin(mosaic.y * 8.0 + time * 2.0 + mid * 3.0);
  float disp = 0.0;
  if (sin(mosaic.y * 14.0 + floor(time * 6.0)) > 0.35) {
    disp = 0.18 * (1.0 + bass);
  }
  vec2 pDisp = mosaic + vec2(disp, wave * 0.08);

  float r = length(pDisp);
  float ang = atan(pDisp.y, pDisp.x);

  float f1 = clamp(sin(r * 14.0 - time * 2.5 + ang * 3.0) * 0.5 + 0.5, 0.0, 1.0);
  float f2 = clamp(cos(pDisp.x * 10.0 + pDisp.y * 8.0 + time) * 0.5 + 0.5, 0.0, 1.0);
  float f3 = clamp(sin(r * 22.0 - time * 4.0) * 0.5 + 0.5, 0.0, 1.0);

  vec3 cDark = vec3(0.04, 0.05, 0.09);
  vec3 cCyan = vec3(0.0, 0.92, 0.98);
  vec3 cYellow = vec3(1.0, 0.92, 0.15);
  vec3 cViolet = vec3(0.85, 0.15, 0.95);

  vec3 col = mix(cDark, cCyan, f1 * 0.7);
  col = mix(col, cYellow, f2 * 0.6);
  col = mix(col, cViolet, f3 * 0.5);

  float scan = clamp(sin(uv.y * 480.0) * 0.5 + 0.5, 0.0, 1.0);
  col = col * (1.0 - scan * 0.35);

  float flare = pow(max(0.0, sin(r * 28.0 - time * 6.0)), 10.0) * (0.5 + high * 1.5);
  col += vec3(flare * 0.8, flare * 0.9, flare * 1.0);

  gl_FragColor = vec4(col, 1.0);
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
    print("=== Creative Expansion Pack 20: Build and Gate Runner ===")
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
  c bg 0.16 0.10 0.24
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
  f rateBeats 0.25
  i steps 16
  i note0 {nr}
  i note1 {nr}
  i note2 {nr+12}
  i note3 {nr}
  i note4 {nr+3}
  i note5 {nr}
  i note6 {nr+7}
  i note7 {nr+10}
  i note8 {nr}
  i note9 {nr}
  i note10 {nr+12}
  i note11 {nr}
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
  c fillColor 0.22 0.12 0.40
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
  f cutoff 3200.0
  f resonance 0.35
  f decay 0.25
  f volume 0.8
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
  c fillColor 0.40 0.18 0.12
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
