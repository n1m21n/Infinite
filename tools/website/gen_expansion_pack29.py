#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 29.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (suprematist-blocks-kinetic-composition, suprematist-blocks-geometric-axis)
2. Sketch 3D (bauhaus-geometry-kinetic-mobile-3d, bauhaus-geometry-cantilever-monument-3d)
3. Field Synth (gabber-distorted-909-kick-synth, gabber-hoover-rave-stab-synth)
4. Field Effects (liquid-dnb-rhodes-lush-chorus-fx, liquid-dnb-sub-warmth-ladder-filter-fx)
5. Field Pixel (acid-graphics-iridescent-liquid-twirl, acid-graphics-kaleido-chroma-warp)
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

TMP_DIR = "/tmp/pack29_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: suprematist_blocks)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "suprematist-blocks-kinetic-composition",
        "title": "Suprematist Blocks Kinetic Composition",
        "description": "Russian avant-garde geometric abstraction study featuring floating cantilevered black quadrilaterals, dynamic diagonal red wedges, intersecting linear axes, and orbital tension points on warm canvas.",
        "tags": ["suprematist", "geometric", "constructivism", "abstract", "composition", "drawing"],
        "code": """// Suprematist Blocks Kinetic Composition - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Russian avant-garde geometric abstraction study featuring floating cantilevered black quadrilaterals, dynamic diagonal red wedges, intersecting linear axes, and orbital tension points on warm canvas.
param("angle", -0.32, -1.0, 1.0);
param("drift", 0.40, 0.1, 1.2);
param("scale_factor", 1.05, 0.5, 2.0);
param("tension", 0.75, 0.2, 1.5);

function draw(t) {
  // Russian avant-garde parchment ground (#f4efe6)
  background(0.957, 0.937, 0.902);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88 * scale_factor;
  const time = t * drift;

  push();
  translate(cx, cy);

  // 1. Dynamic background diagonal construction guideline (faint charcoal)
  stroke(0.2, 0.2, 0.2, 0.25);
  strokeWeight(1.8);
  line(-span * 0.55, span * 0.45, span * 0.55, -span * 0.45);
  line(-span * 0.45, -span * 0.52, span * 0.45, span * 0.52);

  // 2. Heavy black anchor quadrilateral (Malevich black square in motion)
  push();
  translate(sin(time * 0.5) * span * 0.03 - span * 0.08, cos(time * 0.4) * span * 0.02 - span * 0.05);
  rotate(angle + sin(time * 0.25) * 0.04);
  fill(0.067, 0.067, 0.067);
  noStroke();
  rect(-span * 0.24, -span * 0.22, span * 0.48, span * 0.44);

  // High-contrast interior cutout bar
  fill(0.957, 0.937, 0.902);
  rect(-span * 0.18, span * 0.12, span * 0.36, span * 0.04);
  pop();

  // 3. Revolutionary cadmium red wedge (El Lissitzky wedge penetrating space)
  push();
  translate(cos(time * 0.6) * span * 0.04 + span * 0.10, sin(time * 0.5) * span * 0.03 + span * 0.08);
  rotate(angle + 0.78 + sin(time * 0.3) * 0.06);
  fill(0.851, 0.137, 0.122);
  noStroke();
  beginShape();
  vertex(-span * 0.36, -span * 0.08);
  vertex(span * 0.38, -span * 0.02);
  vertex(span * 0.28, span * 0.12);
  vertex(-span * 0.32, span * 0.06);
  endShape();
  pop();

  // 4. Secondary floating black and cobalt counterweight bars
  push();
  translate(span * 0.18, -span * 0.26);
  rotate(angle - 0.45);
  fill(0.067, 0.067, 0.067);
  rect(-span * 0.18, -span * 0.035, span * 0.36, span * 0.07);

  // Cobalt accent dot
  fill(0.12, 0.25, 0.65);
  circle(span * 0.22, 0, span * 0.035);
  pop();

  push();
  translate(-span * 0.22, span * 0.28);
  rotate(angle + 1.2);
  fill(0.067, 0.067, 0.067);
  rect(-span * 0.15, -span * 0.025, span * 0.30, span * 0.05);
  pop();

  // 5. Cadmium red circular tension focal point & black orbital pip
  fill(0.851, 0.137, 0.122);
  circle(-span * 0.26, -span * 0.18, span * 0.13);

  fill(0.067, 0.067, 0.067);
  circle(-span * 0.26 + cos(time * 1.5) * span * 0.11, -span * 0.18 + sin(time * 1.5) * span * 0.11, span * 0.028);

  // 6. Fine linear vector rules anchoring canvas margins
  stroke(0.067, 0.067, 0.067, 0.85);
  strokeWeight(2.4);
  line(-span * 0.44, -span * 0.44, -span * 0.44, span * 0.44);
  stroke(0.851, 0.137, 0.122, 0.90);
  strokeWeight(3.0);
  line(span * 0.44, -span * 0.35, span * 0.44, span * 0.35);

  pop();
}
"""
    },
    {
        "slug": "suprematist-blocks-geometric-axis",
        "title": "Suprematist Blocks Geometric Axis",
        "description": "Dynamic constructivist balance of intersecting orthogonal coordinate bars, bold diagonal red cantilever beams, floating counterweight rectangles, and rhythmic cross-line markers.",
        "tags": ["suprematist", "constructivism", "axis", "geometric", "cantilever", "drawing"],
        "code": """// Suprematist Blocks Geometric Axis - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Dynamic constructivist balance of intersecting orthogonal coordinate bars, bold diagonal red cantilever beams, floating counterweight rectangles, and rhythmic cross-line markers.
param("axis_tilt", 0.18, -0.6, 0.6);
param("beam_width", 28.0, 12.0, 50.0);
param("drift", 0.42, 0.1, 1.2);
param("balance", 0.85, 0.3, 1.6);

function draw(t) {
  // Warm Russian constructivist canvas (#f4efe6)
  background(0.957, 0.937, 0.902);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;
  const time = t * drift;

  push();
  translate(cx, cy);

  // 1. Dominant vertical black pylon axis
  push();
  rotate(axis_tilt + sin(time * 0.2) * 0.02);
  fill(0.067, 0.067, 0.067);
  noStroke();
  rect(-beam_width * 0.5, -span * 0.48, beam_width, span * 0.96);

  // Horizontal intersecting black beam
  rect(-span * 0.38, -span * 0.08, span * 0.76, beam_width * 0.65);
  pop();

  // 2. Bold diagonal revolutionary red cantilever beam
  push();
  rotate(axis_tilt - 0.72 + cos(time * 0.25) * 0.03);
  fill(0.851, 0.137, 0.122);
  noStroke();
  rect(-span * 0.42, -beam_width * 0.8, span * 0.84, beam_width * 1.6);

  // White negative space slit
  fill(0.957, 0.937, 0.902);
  rect(-span * 0.20, -beam_width * 0.2, span * 0.40, beam_width * 0.4);
  pop();

  // 3. Floating geometric counterweights (asymmetric black & cobalt slabs)
  push();
  translate(-span * 0.25 + sin(time * 0.5) * span * 0.02, span * 0.22);
  rotate(axis_tilt + 0.35);
  fill(0.067, 0.067, 0.067);
  noStroke();
  rect(-span * 0.16, -span * 0.10, span * 0.32, span * 0.20);
  pop();

  push();
  translate(span * 0.24, -span * 0.26 + cos(time * 0.4) * span * 0.02);
  rotate(axis_tilt - 0.40);
  fill(0.851, 0.137, 0.122);
  noStroke();
  circle(0, 0, span * 0.16);

  // Black central pupil
  fill(0.067, 0.067, 0.067);
  circle(0, 0, span * 0.045);
  pop();

  // 4. Rhythmic tick marks and cross-axis calibration pips
  stroke(0.067, 0.067, 0.067, 0.85);
  strokeWeight(2.2);
  for (let k = -5; k <= 5; k++) {
    const p = (k / 5.0) * (span * 0.38);
    line(p, -span * 0.42, p, -span * 0.38);
    line(-span * 0.42, p, -span * 0.38, p);
  }

  // Accent cadmium yellow marker
  noStroke();
  fill(0.988, 0.749, 0.286);
  circle(span * 0.32, span * 0.28, span * 0.055);

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: bauhaus_geometry)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "bauhaus-geometry-kinetic-mobile-3d",
        "title": "Bauhaus Geometry Kinetic Mobile 3D",
        "description": "Constructivist kinetic 3D sculpture inspired by Moholy-Nagy and the Bauhaus Light-Space Modulator, featuring rotating primary color planar discs, cantilevered chrome balance arms, and orbital spheres.",
        "tags": ["bauhaus", "geometry", "constructivism", "kinetic", "sculpture", "3d"],
        "code": """// Bauhaus Geometry Kinetic Mobile 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Constructivist kinetic 3D sculpture inspired by Moholy-Nagy and the Bauhaus Light-Space Modulator, featuring rotating primary color planar discs, cantilevered chrome balance arms, and orbital spheres.
param("spin", 0.40, 0.1, 1.2);
param("scale_factor", 1.15, 0.5, 2.0);
param("arms", 4.0, 3.0, 8.0);
param("strut_width", 0.05, 0.02, 0.12);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.52 + sin(time * 0.16) * 0.04);
  rotateY(time * 0.32 + 0.65);

  // Stepped Prussian blue Bauhaus plinth (#1d3557)
  push();
  translate(0, 1.25 * scale_factor, 0);
  fill(0.114, 0.208, 0.341);
  box(2.2 * scale_factor, 0.12, 2.2 * scale_factor);
  // Upper brass plinth tier
  translate(0, -0.09, 0);
  fill(0.988, 0.749, 0.286);
  box(1.5 * scale_factor, 0.06, 1.5 * scale_factor);
  pop();

  // 1. Central vertical brass axle arbor
  fill(0.988, 0.749, 0.286);
  box(strut_width * 1.5 * scale_factor, 2.4 * scale_factor, strut_width * 1.5 * scale_factor);

  const nArms = floor(arms);

  // 2. Counter-rotating horizontal cantilever balance arms with primary color elements
  for (let i = 0; i < nArms; i++) {
    const frac = i / nArms;
    const yOff = (-0.75 + frac * 1.5) * scale_factor;
    const armLength = (0.75 + sin(i * 1.8) * 0.25) * scale_factor;
    const speedMult = (i % 2 === 0 ? 1.0 : -0.85) * (0.35 + frac * 0.25);

    push();
    translate(0, yOff, 0);
    rotateY(time * speedMult + frac * Math.PI * 2.0);

    // Cantilever balance beam (plaster cream #f2e8cf)
    fill(0.949, 0.910, 0.812);
    box(armLength * 2.0, strut_width * scale_factor, strut_width * scale_factor);

    // Tip terminal shapes: alternating primary forms
    // Left tip
    push();
    translate(-armLength, 0, 0);
    if (i % 3 === 0) {
      // Crimson red planar disc/box
      fill(0.839, 0.157, 0.157);
      box(0.28 * scale_factor, 0.28 * scale_factor, 0.06 * scale_factor);
    } else if (i % 3 === 1) {
      // Cadmium yellow cube
      fill(0.988, 0.749, 0.286);
      box(0.24 * scale_factor, 0.24 * scale_factor, 0.24 * scale_factor);
    } else {
      // Prussian blue upright slab
      fill(0.114, 0.208, 0.341);
      box(0.12 * scale_factor, 0.34 * scale_factor, 0.18 * scale_factor);
    }
    pop();

    // Right tip counterweight
    push();
    translate(armLength, 0, 0);
    fill(0.949, 0.910, 0.812);
    box(0.16 * scale_factor, 0.16 * scale_factor, 0.16 * scale_factor);
    // Suspended crimson counter-pip
    translate(0, 0.18 * scale_factor, 0);
    fill(0.839, 0.157, 0.157);
    box(0.08 * scale_factor, 0.22 * scale_factor, 0.08 * scale_factor);
    pop();

    pop();
  }

  // 3. Central orbital kinetic ring
  push();
  rotateY(-time * 0.6);
  rotateZ(0.42);
  const ringSegs = 20;
  const ringR = 0.95 * scale_factor;
  for (let s = 0; s < ringSegs; s++) {
    const th = (s / ringSegs) * Math.PI * 2.0;
    push();
    translate(cos(th) * ringR, 0, sin(th) * ringR);
    rotateY(-th);
    if (s % 5 === 0) {
      fill(0.839, 0.157, 0.157); // Crimson red accent markers
    } else {
      fill(0.949, 0.910, 0.812); // Plaster cream ring
    }
    box(0.05 * scale_factor, 0.05 * scale_factor, (2.0 * Math.PI * ringR / ringSegs) * 0.95);
    pop();
  }
  pop();

  pop();
}
"""
    },
    {
        "slug": "bauhaus-geometry-cantilever-monument-3d",
        "title": "Bauhaus Geometry Cantilever Monument 3D",
        "description": "Monumental architectural 3D study combining asymmetric stepped cantilever slabs, intersecting geometric pylons, cylindrical structural pivots, and bold primary color blocking.",
        "tags": ["bauhaus", "geometry", "architecture", "cantilever", "monument", "3d"],
        "code": """// Bauhaus Geometry Cantilever Monument 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Monumental architectural 3D study combining asymmetric stepped cantilever slabs, intersecting geometric pylons, cylindrical structural pivots, and bold primary color blocking.
param("spin", 0.38, 0.1, 1.2);
param("scale_factor", 1.15, 0.5, 2.0);
param("tiers", 5.0, 3.0, 8.0);
param("cantilever_depth", 0.45, 0.1, 1.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.55 + sin(time * 0.14) * 0.04);
  rotateY(time * 0.28 + 0.55);

  // Deep architectural plinth (#1d3557)
  push();
  translate(0, 1.25 * scale_factor, 0);
  fill(0.114, 0.208, 0.341);
  box(2.4 * scale_factor, 0.12, 2.4 * scale_factor);
  pop();

  const nTiers = floor(tiers);

  // 1. Asymmetric stepped cantilever tower blocks
  for (let i = 0; i < nTiers; i++) {
    const frac = i / (nTiers - 1);
    const yPos = (0.85 - frac * 1.8) * scale_factor;
    const w = (1.4 - frac * 0.6) * scale_factor;
    const h = (0.24) * scale_factor;
    const d = (1.2 - frac * 0.4) * scale_factor;

    const xOffset = sin(i * 1.6 + 0.4) * cantilever_depth * scale_factor;
    const zOffset = cos(i * 1.4) * cantilever_depth * 0.7 * scale_factor;

    push();
    translate(xOffset, yPos, zOffset);

    // Primary slab color: alternating Bauhaus palette
    if (i % 3 === 0) {
      fill(0.839, 0.157, 0.157); // Crimson red cantilever (#d62828)
    } else if (i % 3 === 1) {
      fill(0.949, 0.910, 0.812); // Plaster cream (#f2e8cf)
    } else {
      fill(0.988, 0.749, 0.286); // Brass cadmium yellow (#fcbf49)
    }
    box(w, h, d);

    // Cantilever overhang balcony railing
    push();
    translate(0, -h * 0.6, d * 0.45);
    fill(0.114, 0.208, 0.341); // Prussian blue
    box(w * 0.95, h * 0.35, 0.04 * scale_factor);
    pop();

    pop();
  }

  // 2. Intersecting vertical structural pylon in Prussian blue
  push();
  translate(-0.35 * scale_factor, 0, -0.25 * scale_factor);
  fill(0.114, 0.208, 0.341);
  box(0.28 * scale_factor, 2.2 * scale_factor, 0.28 * scale_factor);

  // Vertical cream accent fin
  translate(0.18 * scale_factor, 0, 0);
  fill(0.949, 0.910, 0.812);
  box(0.04 * scale_factor, 2.0 * scale_factor, 0.24 * scale_factor);
  pop();

  // 3. Cadmium yellow sun finial cube floating at peak
  push();
  translate(0.20 * scale_factor, -1.25 * scale_factor, 0.15 * scale_factor);
  rotateY(-time * 0.6);
  fill(0.988, 0.749, 0.286);
  box(0.32 * scale_factor, 0.32 * scale_factor, 0.32 * scale_factor);

  // Crimson core
  fill(0.839, 0.157, 0.157);
  box(0.14 * scale_factor, 0.14 * scale_factor, 0.14 * scale_factor);
  pop();

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: gabber)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "gabber-distorted-909-kick-synth",
        "title": "Gabber Distorted 909 Kick Synth",
        "description": "Rotterdam hardcore 909 kick drum synthesizer with steep exponential pitch drop, heavy wavefolded tanh saturation, punchy transient click, and tuned sub-harmonic body.",
        "tags": ["synth", "gabber", "hardcore", "kick", "distortion", "audio"],
        "note_root": 36,
        "params": {
            "pitchDecay": 0.045,
            "drive": 0.65,
            "punch": 0.45,
            "decayTime": 0.28,
            "fold": 0.35
        },
        "code": """param float pitchDecay = 0.045 [0.015, 0.15]
param float drive = 0.65 [0.1, 1.0]
param float punch = 0.45 [0.0, 1.0]
param float decayTime = 0.28 [0.08, 0.8]
param float fold = 0.35 [0.0, 0.8]
state float p1 = 0
state float pClick = 0
state float env = 0
state float pEnv = 0
state float lp1 = 0
state float lp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decayTime), 0.0001, 0.06)
env = if(target > env, env + (target - env) * 0.95, env - env * decayRate)

pRate = clamp(1.0 / (sr * pitchDecay), 0.0005, 0.12)
pEnv = if(target > pEnv, target, pEnv - pEnv * pRate)

fBase = clamp(freq * 0.5 * (1.0 + pEnv * 5.5), 35.0, 650.0)

dp = fBase / sr
p1 = p1 + dp
p1 = if(p1 >= 1.0, p1 - 1.0, p1)
sineCore = sin(p1 * 6.2831853)

dpC = 2200.0 / sr
pClick = pClick + dpC
pClick = if(pClick >= 1.0, pClick - 1.0, pClick)
click = sin(pClick * 6.2831853) * pEnv * punch * 0.6

raw = (sineCore + click) * (1.0 + drive * 2.8)
sat = sin(raw * (1.0 + fold * 0.8)) * env

fCut = clamp(0.18 + pEnv * 0.25, 0.03, 0.45)
lp1 = lp1 + fCut * (sat - lp1)
lp2 = lp2 + fCut * (lp1 - lp2)

out = lp2 * 0.42
"""
    },
    {
        "slug": "gabber-hoover-rave-stab-synth",
        "title": "Gabber Hoover Rave Stab Synth",
        "description": "Iconic rave hoover chord stab synthesizer featuring multi-phase detuned pulse-saw oscillators, aggressive pitch glide envelope, resonant lowpass sweep, and saturated harmonic bite.",
        "tags": ["synth", "gabber", "hoover", "rave", "stab", "audio"],
        "note_root": 48,
        "params": {
            "detune": 0.35,
            "stabDecay": 0.22,
            "cutoff": 0.70,
            "drive": 0.40,
            "pitchGlide": 0.25
        },
        "code": """param float detune = 0.35 [0.05, 0.8]
param float stabDecay = 0.22 [0.05, 0.7]
param float cutoff = 0.70 [0.1, 1.0]
param float drive = 0.40 [0.0, 1.0]
param float pitchGlide = 0.25 [0.0, 0.8]
state float p1 = 0
state float p2 = 0
state float env = 0
state float pEnv = 0
state float lp1 = 0
state float lp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * stabDecay), 0.0001, 0.06)
env = if(target > env, env + (target - env) * 0.92, env - env * decayRate)

pRate = clamp(1.0 / (sr * 0.08), 0.0005, 0.10)
pEnv = if(target > pEnv, target, pEnv - pEnv * pRate)

fBase = clamp(freq * (1.0 + pEnv * pitchGlide * 0.8), 50.0, 1800.0)

dp1 = fBase / sr
p1 = p1 + dp1
p1 = if(p1 >= 1.0, p1 - 1.0, p1)
saw1 = p1 * 2.0 - 1.0

dp2 = (fBase * (1.0 + detune * 0.03)) / sr
p2 = p2 + dp2
p2 = if(p2 >= 1.0, p2 - 1.0, p2)
saw2 = p2 * 2.0 - 1.0

sig = (saw1 + saw2) * (0.5 + drive * 0.8)
sat = (sig / (1.0 + abs(sig * 0.5))) * env

fCut = clamp(cutoff * 0.35 + env * 0.25, 0.03, 0.45)
lp1 = lp1 + fCut * (sat - lp1)
lp2 = lp2 + fCut * (lp1 - lp2)

out = lp2 * 0.42
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: liquid_dnb)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "liquid-dnb-rhodes-lush-chorus-fx",
        "title": "Liquid DnB Rhodes Lush Chorus FX",
        "description": "Velvety multi-voice stereo chorus and spatial shimmer effect tailored for liquid drum & bass electric piano chords and pad textures, with dual quadrature LFOs and soft top-end roll-off.",
        "tags": ["effect", "liquid_dnb", "chorus", "rhodes", "spatial", "audio"],
        "params": {
            "rate": 0.45,
            "depth": 0.55,
            "warmth": 0.40,
            "feedback": 0.25,
            "mix": 0.55
        },
        "code": """param float rate = 0.45 [0.1, 2.5]
param float depth = 0.55 [0.1, 1.0]
param float warmth = 0.40 [0.0, 1.0]
param float feedback = 0.25 [0.0, 0.6]
param float mix = 0.55 [0.0, 1.0]
state float pLfo = 0
state float c1 = 0
state float c2 = 0
state float lp = 0

dp = rate / sr
pLfo = pLfo + dp
pLfo = if(pLfo >= 1.0, pLfo - 1.0, pLfo)
lfo = sin(pLfo * 6.2831853)

fDamp = clamp(3800.0 / sr * 2.0, 0.02, 0.45)
lp = lp + fDamp * (in - lp)

modRate = clamp(0.12 + depth * 0.08 * lfo, 0.02, 0.35)
c1 = c1 + modRate * (lp + c2 * feedback * 0.4 - c1)
c2 = c2 + modRate * 0.85 * (c1 - c2)

chorusSig = (c1 - c2) * 1.6
sat = (chorusSig / (1.0 + abs(chorusSig * warmth * 0.6)))

processed = in * (1.0 - mix) + sat * mix
out = processed * 1.65
"""
    },
    {
        "slug": "liquid-dnb-sub-warmth-ladder-filter-fx",
        "title": "Liquid DnB Sub Warmth Ladder Filter FX",
        "description": "Resonant 4-pole ladder lowpass filter with low-end resonance compensation, analog saturation warmth, and envelope following designed for warm rolling drum & bass sub-lines.",
        "tags": ["effect", "liquid_dnb", "filter", "ladder", "sub", "audio"],
        "params": {
            "cutoff": 2400.0,
            "resonance": 0.38,
            "drive": 0.45,
            "bassComp": 0.50,
            "mix": 0.70
        },
        "code": """param float cutoff = 2400.0 [400.0, 8000.0]
param float resonance = 0.38 [0.0, 0.85]
param float drive = 0.45 [0.0, 1.0]
param float bassComp = 0.50 [0.0, 1.0]
param float mix = 0.70 [0.0, 1.0]
state float s1 = 0
state float s2 = 0
state float s3 = 0
state float s4 = 0
state float lpBass = 0

fCut = clamp(cutoff / sr * 2.0, 0.02, 0.42)
fBass = clamp(160.0 / sr * 2.0, 0.005, 0.12)
lpBass = lpBass + fBass * (in - lpBass)

fb = resonance * 3.6
inp = in - fb * s4 + lpBass * bassComp * 0.4

drv = inp * (1.0 + drive * 1.8)
sat = drv / (1.0 + abs(drv * 0.5))

s1 = s1 + fCut * (sat - s1)
s2 = s2 + fCut * (s1 - s2)
s3 = s3 + fCut * (s2 - s3)
s4 = s4 + fCut * (s3 - s4)

processed = in * (1.0 - mix) + s4 * mix
out = processed * 1.30
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embeds (Theme: acid_graphics)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "acid-graphics-iridescent-liquid-twirl",
        "title": "Acid Graphics Iridescent Liquid Twirl",
        "description": "Audio-reactive liquid chrome vortex with logarithmic polar spiral coordinates, iridescent thin-film chromatic interference, and razor-sharp acid green and ultraviolet contours.",
        "embed_desc": "Audio-reactive acid graphics liquid simulation featuring a logarithmic spiral chrome vortex. Bass pulses expand and contract the central vortex aperture, mid frequencies modulate concentric liquid interference ripples, and high frequencies trigger vibrant chromatic aberration flares across electric lime and ultraviolet gradients.",
        "tags": ["pixel", "acid_graphics", "liquid", "iridescent", "twirl", "audioreactive"],
        "params": {
            "speed": 0.40,
            "twirl": 2.4,
            "scale": 14.0,
            "contrast": 1.15,
            "dispersion": 0.50
        },
        "code": """param float speed = 0.40 [0.1, 1.5]
param float twirl = 2.4 [0.5, 5.0]
param float scale = 14.0 [5.0, 30.0]
param float contrast = 1.15 [0.6, 2.0]
param float dispersion = 0.50 [0.1, 1.0]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2
time = t * speed

r = length(p)
theta = atan2(p.y, p.x) + (twirl / (r + 0.18))

w1 = sin(r * scale - time * 2.8 + theta * 3.0)
w2 = cos(p.x * scale * 0.7 + time * 1.8) * sin(p.y * scale * 0.7 - time * 1.4)
wave = (w1 + w2) * 0.5

rDisp = wave * dispersion * 0.08
dispP = p + vec2(rDisp, -rDisp)
waveDisp = sin(length(dispP) * scale - time * 2.5 + theta * 2.0)

cVoid = vec3(0.04, 0.01, 0.08)
cLime = vec3(0.80, 1.0, 0.0)
cMagenta = vec3(1.0, 0.0, 0.95)
cCyan = vec3(0.0, 0.90, 1.0)

base = mix(cVoid, cMagenta, clamp(wave * 0.5 + 0.5, 0.0, 1.0))
col = mix(base, cLime, clamp(waveDisp * 0.6 + 0.4, 0.0, 1.0))

rim = 1.0 - smoothstep(0.35, 0.70, abs(wave))
col = mix(col, cCyan, rim * 0.65)

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
  float time = t * 0.40;

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  float r = length(p);
  float theta = atan(p.y, p.x) + ((2.4 + b * 1.2) / (r + 0.18));

  float scale = 14.0 + m * 4.0;
  float w1 = sin(r * scale - time * 2.8 + theta * 3.0);
  float w2 = cos(p.x * scale * 0.7 + time * 1.8) * sin(p.y * scale * 0.7 - time * 1.4);
  float wave = (w1 + w2) * 0.5 * (1.0 + b * 0.3);

  float rDisp = wave * (0.50 + h * 0.25) * 0.08;
  vec2 dispP = p + vec2(rDisp, -rDisp);
  float waveDisp = sin(length(dispP) * scale - time * 2.5 + theta * 2.0);

  vec3 cVoid = vec3(0.04, 0.01, 0.08);
  vec3 cLime = vec3(0.80, 1.0, 0.0);
  vec3 cMagenta = vec3(1.0, 0.0, 0.95);
  vec3 cCyan = vec3(0.0, 0.90, 1.0);

  vec3 base = mix(cVoid, cMagenta, clamp(wave * 0.5 + 0.5, 0.0, 1.0));
  vec3 col = mix(base, cLime, clamp(waveDisp * 0.6 + 0.4, 0.0, 1.0));

  float rim = 1.0 - smoothstep(0.35, 0.70, abs(wave));
  col = mix(col, cCyan, rim * (0.65 + h * 0.35));

  gl_FragColor = vec4(clamp(col * 1.15, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "acid-graphics-kaleido-chroma-warp",
        "title": "Acid Graphics Kaleido Chroma Warp",
        "description": "Audio-reactive 8-fold kaleidoscopic chromatic warp featuring undulating starburst lattices, acid typography contours, and high-voltage neon edge glow.",
        "embed_desc": "Audio-reactive kaleidoscopic acid graphics visualizer featuring an 8-fold symmetric starburst field. Bass frequencies distort and swell the central radial lattice, mid frequencies ripple recursive polygonal contours across electric lime and neon magenta bands, and high frequencies ignite luminous iridescent perimeter caustics.",
        "tags": ["pixel", "acid_graphics", "kaleidoscope", "chroma", "starburst", "audioreactive"],
        "params": {
            "speed": 0.38,
            "folds": 8.0,
            "rippleScale": 16.0,
            "contrast": 1.18,
            "glow": 0.60
        },
        "code": """param float speed = 0.38 [0.1, 1.5]
param float folds = 8.0 [4.0, 16.0]
param float rippleScale = 16.0 [6.0, 32.0]
param float contrast = 1.18 [0.6, 2.0]
param float glow = 0.60 [0.1, 1.2]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.4
time = t * speed

r = length(p)
theta = atan2(p.y, p.x)
sector = 6.2831853 / folds
modTheta = abs(mod(theta + time * 0.25, sector) - sector * 0.5)

kp = vec2(cos(modTheta), sin(modTheta)) * r

w = sin(kp.x * rippleScale - time * 2.2) * cos(kp.y * rippleScale + time * 1.8)
ring = sin(r * 20.0 - time * 3.2)
sig = (w + ring) * 0.5

cDark = vec3(0.03, 0.02, 0.06)
cLime = vec3(0.80, 1.0, 0.0)
cPink = vec3(1.0, 0.10, 0.70)
cWhite = vec3(0.95, 0.98, 1.0)

col = mix(cDark, cPink, clamp(sig * 0.5 + 0.5, 0.0, 1.0))
col = mix(col, cLime, clamp(w * 0.7 + 0.3, 0.0, 1.0))

star = exp(-abs(sig) * (5.0 - glow * 2.0))
col = mix(col, cWhite, star * 0.75)

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
  float time = t * 0.38;

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  float r = length(p);
  float theta = atan(p.y, p.x);
  float folds = 8.0;
  float sector = 6.2831853 / folds;
  float modTheta = abs(mod(theta + time * 0.25, sector) - sector * 0.5);

  vec2 kp = vec2(cos(modTheta), sin(modTheta)) * r;

  float rippleScale = 16.0 + m * 6.0;
  float w = sin(kp.x * rippleScale - time * 2.2) * cos(kp.y * rippleScale + time * 1.8);
  float ring = sin(r * (20.0 + b * 6.0) - time * 3.2);
  float sig = (w + ring) * 0.5 * (1.0 + b * 0.3);

  vec3 cDark = vec3(0.03, 0.02, 0.06);
  vec3 cLime = vec3(0.80, 1.0, 0.0);
  vec3 cPink = vec3(1.0, 0.10, 0.70);
  vec3 cWhite = vec3(0.95, 0.98, 1.0);

  vec3 col = mix(cDark, cPink, clamp(sig * 0.5 + 0.5, 0.0, 1.0));
  col = mix(col, cLime, clamp(w * 0.7 + 0.3, 0.0, 1.0));

  float star = exp(-abs(sig) * (4.2 - b * 1.0));
  col = mix(col, cWhite, star * (0.75 + h * 0.35));

  gl_FragColor = vec4(clamp(col * 1.18, 0.0, 1.0), 1.0);
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
  <meta property="og:image" content="https://infinitedevices.org/assets/og/embed-{item['slug']}.jpg">
  <meta name="twitter:card" content="summary_large_image">
  <meta name="twitter:title" content="{item['title']}: audio-reactive visual">
  <meta name="twitter:description" content="{item['embed_desc']}">
  <meta name="twitter:image" content="https://infinitedevices.org/assets/og/embed-{item['slug']}.jpg">
  <style>
    * {{ box-sizing: border-box; margin: 0; padding: 0; }}
    html, body {{
      width: 100%;
      height: 100%;
      overflow: hidden;
      background: #0b132b;
      color: #f4efe6;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    }}
    canvas {{
      display: block;
      width: 100vw;
      height: 100vh;
      width: 100dvw;
      height: 100dvh;
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
    print("=== Creative Expansion Pack 29: Build and Gate Runner ===")
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
  c bg 0.065 0.085 0.125
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
  i note2 {nr+5}
  i note3 {nr+7}
  i note4 {nr}
  i note5 {nr+3}
  i note6 {nr+7}
  i note7 {nr+10}
  i note8 {nr}
  i note9 {nr}
  i note10 {nr+5}
  i note11 {nr+12}
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
  c fillColor 0.16 0.24 0.35
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
node 5 Modulators Audio Analyze
  id ana
  pos 0 2200
end
node 6 Source Shape
  id pic
  pos 0 162
  c fillColor 0.14 0.20 0.30
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
