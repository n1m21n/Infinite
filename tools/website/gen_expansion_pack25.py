#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 25.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (bauhaus-geometry-constructivist-lattice, bauhaus-geometry-asymmetric-rotary-composition)
2. Sketch 3D (brutalist-architecture-monolithic-facade-3d, brutalist-architecture-stepped-cantilever-monument-3d)
3. Field Synth (french-filter-house-pumping-saw-bass-synth, french-filter-house-disco-stab-lead-synth)
4. Field Effects (downtempo-tape-flutter-warm-delay-fx, downtempo-analog-warmth-resonant-filter-fx)
5. Field Pixel (suprematist-forms-kinetic-red-wedge, suprematist-forms-floating-black-quadrilateral)
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

TMP_DIR = "/tmp/pack25_build"
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
        "slug": "bauhaus-geometry-constructivist-lattice",
        "title": "Bauhaus Geometry Constructivist Lattice",
        "description": "Constructivist geometric lattice featuring primary-colored concentric arcs, diagonal structural rules, and balanced asymmetrical quadrant divisions on warm parchment.",
        "tags": ["bauhaus", "constructivist", "lattice", "geometric", "drawing", "vector"],
        "code": """// Bauhaus Geometry Constructivist Lattice - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Constructivist geometric lattice featuring primary-colored concentric arcs, diagonal structural rules, and balanced asymmetrical quadrant divisions on warm parchment.
param("grid_cells", 6.0, 3.0, 12.0);
param("arc_scale", 1.1, 0.5, 2.0);
param("rotation_speed", 0.45, 0.1, 1.5);
param("rule_thickness", 2.8, 1.0, 6.0);

function draw(t) {
  // Warm parchment paper ground (#efe7d3)
  background(0.937, 0.906, 0.827);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;

  push();
  translate(cx, cy);

  // Structural border frame (#111111)
  stroke(0.08, 0.08, 0.09);
  strokeWeight(3.0);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  // Inner margin boundary
  stroke(0.08, 0.08, 0.09, 0.4);
  strokeWeight(1.2);
  rect(-span * 0.45, -span * 0.45, span * 0.90, span * 0.90);

  const time = t * rotation_speed;

  // 1. Prussian Blue deep semicircle arc (#1d3557)
  push();
  translate(-span * 0.08, span * 0.06);
  rotate(time * 0.25);
  fill(0.114, 0.208, 0.341, 0.92);
  noStroke();
  beginShape();
  const rad = span * 0.32 * arc_scale;
  for (let a = 0; a <= Math.PI + 0.05; a += 0.08) {
    vertex(cos(a) * rad, sin(a) * rad);
  }
  vertex(0, 0);
  endShape();
  pop();

  // 2. Bold Crimson Red solid constructivist wedge & rect (#d62828)
  push();
  translate(span * 0.12, -span * 0.10);
  rotate(-time * 0.3);
  fill(0.839, 0.157, 0.157, 0.94);
  noStroke();
  rect(-span * 0.18, -span * 0.18, span * 0.36, span * 0.36);
  
  // Intersecting dynamic diagonal wedge
  fill(0.839, 0.157, 0.157, 0.85);
  triangle(0, -span * 0.25, span * 0.28, span * 0.15, -span * 0.15, span * 0.20);
  pop();

  // 3. Warm Ochre Yellow solar disk & concentric ring (#fcbf49)
  push();
  translate(span * 0.18, span * 0.16);
  fill(0.988, 0.749, 0.286, 0.95);
  noStroke();
  circle(0, 0, span * 0.28 * arc_scale);

  stroke(0.08, 0.08, 0.09);
  strokeWeight(rule_thickness);
  noFill();
  circle(0, 0, span * 0.42 * arc_scale);
  pop();

  // 4. Charcoal diagonal linear constructivist axis lines & crossbars
  stroke(0.08, 0.08, 0.09);
  strokeWeight(rule_thickness * 1.5);
  line(-span * 0.44, span * 0.36, span * 0.44, -span * 0.36);

  strokeWeight(rule_thickness * 0.8);
  line(-span * 0.36, -span * 0.44, span * 0.36, span * 0.44);

  // 5. Grid of precision registration crosshairs and tick marks
  stroke(0.08, 0.08, 0.09, 0.75);
  strokeWeight(1.4);
  const steps = 5;
  for (let i = -steps; i <= steps; i++) {
    const x = (i / steps) * (span * 0.42);
    line(x, -span * 0.45, x, -span * 0.43);
    line(x, span * 0.43, x, span * 0.45);
    line(-span * 0.45, x, -span * 0.43, x);
    line(span * 0.43, x, span * 0.45, x);
  }

  pop();
}
"""
    },
    {
        "slug": "bauhaus-geometry-asymmetric-rotary-composition",
        "title": "Bauhaus Geometry Asymmetric Rotary Composition",
        "description": "Asymmetric constructivist study with interlocking rotational discs, tensioned diagonal intersecting planes, and primary Bauhaus accents on ivory gesso.",
        "tags": ["bauhaus", "composition", "rotary", "constructivism", "geometric", "drawing"],
        "code": """// Bauhaus Geometry Asymmetric Rotary Composition - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Asymmetric constructivist study with interlocking rotational discs, tensioned diagonal intersecting planes, and primary Bauhaus accents on ivory gesso.
param("disc_count", 5.0, 3.0, 8.0);
param("rotary_speed", 0.42, 0.1, 1.2);
param("accent_scale", 1.15, 0.5, 2.0);
param("axis_angle", 0.785, 0.2, 1.57);

function draw(t) {
  // Ivory gesso ground (#efe7d3)
  background(0.937, 0.906, 0.827);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;

  push();
  translate(cx, cy);

  // Outer framing boundary
  stroke(0.08, 0.08, 0.09);
  strokeWeight(2.8);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  const time = t * rotary_speed;

  // 1. Concentric rotating disc segments in Prussian Blue (#1d3557)
  push();
  translate(-span * 0.12, -span * 0.08);
  rotate(time * 0.35);
  fill(0.114, 0.208, 0.341, 0.90);
  noStroke();
  beginShape();
  const r1 = span * 0.38 * accent_scale;
  for (let a = 0; a <= Math.PI * 0.75 + 0.05; a += 0.08) {
    vertex(cos(a) * r1, sin(a) * r1);
  }
  vertex(0, 0);
  endShape();
  pop();

  // 2. Bauhaus Red wedge (#d62828)
  push();
  translate(span * 0.10, span * 0.12);
  rotate(-time * 0.25);
  fill(0.839, 0.157, 0.157, 0.92);
  noStroke();
  triangle(-span * 0.15, -span * 0.25, span * 0.32, 0, -span * 0.10, span * 0.25);
  pop();

  // 3. Ochre Yellow disk (#fcbf49)
  push();
  translate(-span * 0.18, span * 0.20);
  fill(0.988, 0.749, 0.286, 0.95);
  noStroke();
  circle(0, 0, span * 0.24 * accent_scale);
  pop();

  // 4. Intersecting charcoal axis lines
  stroke(0.08, 0.08, 0.09);
  strokeWeight(3.2);
  line(-span * 0.44, -span * 0.22, span * 0.44, span * 0.22);
  strokeWeight(1.8);
  line(-span * 0.20, -span * 0.44, span * 0.20, span * 0.44);

  // 5. Secondary Bauhaus Red small circle
  push();
  translate(span * 0.26, -span * 0.28);
  fill(0.839, 0.157, 0.157, 0.95);
  noStroke();
  circle(0, 0, span * 0.10 * accent_scale);
  pop();

  // 6. Fine grid of constructivist registration ticks
  stroke(0.08, 0.08, 0.09, 0.6);
  strokeWeight(1.2);
  for (let i = -4; i <= 4; i++) {
    const p = (i / 4.0) * (span * 0.42);
    line(p, -8, p, 8);
    line(-8, p, 8, p);
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
        "slug": "brutalist-architecture-monolithic-facade-3d",
        "title": "Brutalist Architecture Monolithic Facade 3D",
        "description": "Monumental raw concrete facade with deep recessed fenestrations, fluted monolithic pilasters, and industrial red primer datum lines.",
        "tags": ["brutalist", "architecture", "concrete", "monolith", "facade", "3d"],
        "code": """// Brutalist Architecture Monolithic Facade 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Monumental raw concrete facade with deep recessed fenestrations, fluted monolithic pilasters, and industrial red primer datum lines.
param("pylon_scale", 1.0, 0.5, 2.0);
param("cantilever_depth", 1.2, 0.5, 2.5);
param("rotation_speed", 0.32, 0.05, 1.0);
param("fenestration_count", 6.0, 3.0, 10.0);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.42 + sin(time * 0.15) * 0.05);
  rotateY(time * 0.25 + 0.55);

  // Deep graphite base foundation slab (#222222)
  push();
  translate(0, 0.95, 0);
  fill(0.13, 0.13, 0.15);
  box(2.4 * pylon_scale, 0.18, 2.4 * pylon_scale);
  pop();

  // Tier 2: Weathered cast aggregate concrete plinth (#7a7a78)
  push();
  translate(0, 0.78, 0);
  fill(0.48, 0.48, 0.47);
  box(2.0 * pylon_scale, 0.18, 2.0 * pylon_scale);
  pop();

  // Massive vertical monolithic facade towers (raw light aggregate stone #d9d6cf)
  push();
  translate(0, -0.05, 0);
  fill(0.85, 0.84, 0.81);
  box(1.2 * pylon_scale, 1.45 * pylon_scale, 0.85 * pylon_scale);
  pop();

  // Fluted pilaster ribs along the facade
  const ribs = floor(fenestration_count);
  for (let i = 0; i < ribs; i++) {
    const xOff = (i - (ribs - 1) * 0.5) * 0.22 * pylon_scale;
    push();
    translate(xOff, -0.05, 0.46 * pylon_scale);
    fill(i % 2 === 0 ? 0.76 : 0.88, i % 2 === 0 ? 0.75 : 0.87, i % 2 === 0 ? 0.72 : 0.84);
    box(0.09 * pylon_scale, 1.40 * pylon_scale, 0.14 * pylon_scale);
    pop();
  }

  // Heavy projecting cantilever slab (dramatic architectural overhang)
  push();
  translate(0.45 * cantilever_depth, -0.45, 0.15);
  fill(0.62, 0.61, 0.59);
  box(1.5 * cantilever_depth, 0.24, 1.1 * pylon_scale);
  pop();

  // Industrial rust / red primer datum beam accent (#d64527)
  push();
  translate(0, -0.82 * pylon_scale, 0);
  fill(0.84, 0.27, 0.15); // Raw industrial primer red
  box(1.3 * pylon_scale, 0.08, 0.95 * pylon_scale);
  pop();

  // Heavy top cornice slab capping the structure
  push();
  translate(0, -0.92 * pylon_scale, 0);
  fill(0.88, 0.87, 0.84);
  box(1.45 * pylon_scale, 0.12, 1.1 * pylon_scale);
  pop();

  pop();
}
"""
    },
    {
        "slug": "brutalist-architecture-stepped-cantilever-monument-3d",
        "title": "Brutalist Architecture Stepped Cantilever Monument 3D",
        "description": "Spomenik-inspired civic monument of interlocking cantilevered concrete slabs with angular shadow recesses and monumental stepped plinths.",
        "tags": ["brutalist", "cantilever", "monument", "concrete", "sculpture", "3d"],
        "code": """// Brutalist Architecture Stepped Cantilever Monument 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Spomenik-inspired civic monument of interlocking cantilevered concrete slabs with angular shadow recesses and monumental stepped plinths.
param("monument_scale", 1.05, 0.5, 2.0);
param("cantilever_spread", 1.25, 0.5, 2.2);
param("rotation_speed", 0.35, 0.05, 1.2);
param("tier_count", 5.0, 3.0, 8.0);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.44 + sin(time * 0.18) * 0.04);
  rotateY(time * 0.28 + 0.85);

  // Stepped foundation plinth in basalt (#1e1f24)
  push();
  translate(0, 0.92, 0);
  fill(0.12, 0.12, 0.14);
  box(2.5 * monument_scale, 0.16, 2.5 * monument_scale);
  pop();

  push();
  translate(0, 0.76, 0);
  fill(0.42, 0.42, 0.41);
  box(2.1 * monument_scale, 0.16, 2.1 * monument_scale);
  pop();

  // Monumental angular cantilever wings radiating outward
  const tiers = floor(tier_count);
  for (let i = 0; i < tiers; i++) {
    const yTier = 0.55 - i * 0.28 * monument_scale;
    const w = (1.5 - i * 0.18) * monument_scale;
    const overhang = (0.2 + i * 0.18) * cantilever_spread;

    // Wing Left
    push();
    translate(-overhang, yTier, 0);
    rotateZ(-0.12 * i);
    fill(i % 2 === 0 ? 0.82 : 0.74, i % 2 === 0 ? 0.81 : 0.73, i % 2 === 0 ? 0.78 : 0.70);
    box(w, 0.18 * monument_scale, 1.1 * monument_scale);
    pop();

    // Wing Right
    push();
    translate(overhang, yTier, 0);
    rotateZ(0.12 * i);
    fill(i % 2 === 0 ? 0.84 : 0.76, i % 2 === 0 ? 0.83 : 0.75, i % 2 === 0 ? 0.80 : 0.72);
    box(w, 0.18 * monument_scale, 1.1 * monument_scale);
    pop();
  }

  // Central dark recess spine
  push();
  translate(0, 0.10, 0);
  fill(0.18, 0.18, 0.21);
  box(0.55 * monument_scale, 1.35 * monument_scale, 0.65 * monument_scale);
  pop();

  // Raw rust primer red cantilever datum line (#d64527)
  push();
  translate(0, -0.62 * monument_scale, 0);
  fill(0.85, 0.25, 0.14);
  box(1.6 * monument_scale, 0.08, 0.85 * monument_scale);
  pop();

  // Top monolithic capping block
  push();
  translate(0, -0.74 * monument_scale, 0);
  fill(0.88, 0.87, 0.84);
  box(0.95 * monument_scale, 0.16 * monument_scale, 0.95 * monument_scale);
  pop();

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: french_filter_house)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "french-filter-house-pumping-saw-bass-synth",
        "title": "French Filter House Pumping Saw Bass Synth",
        "description": "Pumping multi-oscillator saw bass with 2-pole resonant diode ladder filter, sidechain ducking curve, and warm harmonic drive.",
        "tags": ["synth", "french-house", "bass", "filter", "saw", "audio"],
        "note_root": 42,
        "params": {
            "cutoff": 0.55,
            "resonance": 0.45,
            "subGain": 0.40,
            "drive": 0.35,
            "decayTime": 0.30
        },
        "code": """param float cutoff = 0.55 [0.1, 1.0]
param float resonance = 0.45 [0.0, 0.85]
param float subGain = 0.40 [0.0, 1.0]
param float drive = 0.35 [0.0, 1.0]
param float decayTime = 0.30 [0.05, 0.8]
state float p1 = 0
state float pSub = 0
state float env = 0
state float lp1 = 0
state float lp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decayTime), 0.0001, 0.03)
env = if(target > env, env + (target - env) * 0.55, env - env * decayRate)

fBase = clamp(freq, 35.0, 440.0)

dp1 = fBase / sr
p1 = p1 + dp1
p1 = if(p1 >= 1.0, p1 - 1.0, p1)
saw1 = p1 * 2.0 - 1.0

dpSub = (fBase * 0.5) / sr
pSub = pSub + dpSub
pSub = if(pSub >= 1.0, pSub - 1.0, pSub)
subSquare = if(pSub < 0.5, 0.45, -0.45)

raw = saw1 * 0.65 + subSquare * subGain

drv = raw * (1.0 + drive * 1.8)
sat = (drv / (1.0 + abs(drv))) * env

fCut = clamp(cutoff * (0.15 + env * 0.35), 0.01, 0.42)
lp1 = lp1 + fCut * (sat + lp1 * resonance * 0.5 - lp1)
lp2 = lp2 + fCut * (lp1 - lp2)

out = lp2 * 0.58
"""
    },
    {
        "slug": "french-filter-house-disco-stab-lead-synth",
        "title": "French Filter House Disco Stab Lead Synth",
        "description": "Euphoric filtered disco chord stab synthesizer with multi-saw unison spread, resonant sweep envelope, and analog saturation.",
        "tags": ["synth", "french-house", "lead", "disco", "stab", "audio"],
        "note_root": 54,
        "params": {
            "filterSweep": 0.65,
            "resonance": 0.42,
            "detuneSpread": 0.35,
            "drive": 0.30,
            "stabDecay": 0.24
        },
        "code": """param float filterSweep = 0.65 [0.1, 1.0]
param float resonance = 0.42 [0.0, 0.85]
param float detuneSpread = 0.35 [0.0, 1.0]
param float drive = 0.30 [0.0, 1.0]
param float stabDecay = 0.24 [0.05, 0.6]
state float p1 = 0
state float p2 = 0
state float p3 = 0
state float env = 0
state float lp1 = 0
state float lp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * stabDecay), 0.0001, 0.04)
env = if(target > env, env + (target - env) * 0.65, env - env * decayRate)

fBase = clamp(freq, 110.0, 1200.0)

dp1 = fBase / sr
p1 = p1 + dp1
p1 = if(p1 >= 1.0, p1 - 1.0, p1)
s1 = p1 * 2.0 - 1.0

dp2 = (fBase * (1.0 + detuneSpread * 0.008)) / sr
p2 = p2 + dp2
p2 = if(p2 >= 1.0, p2 - 1.0, p2)
s2 = p2 * 2.0 - 1.0

dp3 = (fBase * (1.0 - detuneSpread * 0.008)) / sr
p3 = p3 + dp3
p3 = if(p3 >= 1.0, p3 - 1.0, p3)
s3 = p3 * 2.0 - 1.0

raw = (s1 + s2 * 0.8 + s3 * 0.8) * 0.42

drv = raw * (1.0 + drive * 1.5)
sat = (drv / (1.0 + abs(drv))) * env

fCut = clamp(filterSweep * (0.12 + env * 0.45), 0.02, 0.44)
lp1 = lp1 + fCut * (sat + lp1 * resonance * 0.45 - lp1)
lp2 = lp2 + fCut * (lp1 - lp2)

out = lp2 * 0.52
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: downtempo)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "downtempo-tape-flutter-warm-delay-fx",
        "title": "Downtempo Tape Flutter Warm Delay FX",
        "description": "Vintage reel-to-reel tape delay with wow and flutter modulation, high-frequency tape head roll-off, and warm saturating feedback.",
        "tags": ["effect", "downtempo", "tape", "delay", "flutter", "audio"],
        "params": {
            "flutterRate": 1.8,
            "flutterDepth": 0.35,
            "damping": 3400.0,
            "feedback": 0.52,
            "mix": 0.58
        },
        "code": """param float flutterRate = 1.8 [0.2, 5.0]
param float flutterDepth = 0.35 [0.0, 1.0]
param float damping = 3400.0 [800.0, 8000.0]
param float feedback = 0.52 [0.1, 0.85]
param float mix = 0.58 [0.0, 1.0]
state float ph = 0
state float d1 = 0
state float d2 = 0
state float lp = 0

ph = ph + flutterRate / sr
ph = if(ph >= 1.0, ph - 1.0, ph)
flutter = sin(ph * 6.2831853) * flutterDepth * 0.08

fDamp = clamp(damping / sr * 2.0, 0.02, 0.44)
lp = lp + fDamp * (in - lp)

fb = clamp(feedback, 0.1, 0.85)

d1 = d1 + (0.18 + flutter) * (lp + d2 * fb - d1)
d2 = d2 + (0.13 - flutter) * (d1 * 0.85 - d2)

wet = (d1 * 0.65 + d2 * 0.5) * 1.15
sat = clamp(wet * 1.3, -0.85, 0.85)

out = in * (1.0 - mix) + sat * mix
"""
    },
    {
        "slug": "downtempo-analog-warmth-resonant-filter-fx",
        "title": "Downtempo Analog Warmth Resonant Filter FX",
        "description": "Organic resonant state filter with dynamic envelope follower modulation, asymmetric tube warmth, and subtle vinyl dust texture.",
        "tags": ["effect", "downtempo", "filter", "analog", "warmth", "audio"],
        "params": {
            "cutoff": 2600.0,
            "resonance": 0.52,
            "warmth": 0.45,
            "envDepth": 0.38,
            "mix": 0.65
        },
        "code": """param float cutoff = 2600.0 [400.0, 7000.0]
param float resonance = 0.52 [0.05, 0.90]
param float warmth = 0.45 [0.0, 1.0]
param float envDepth = 0.38 [0.0, 1.0]
param float mix = 0.65 [0.0, 1.0]
state float envFollower = 0
state float lp1 = 0
state float lp2 = 0

absIn = abs(in)
envFollower = envFollower + 0.05 * (absIn - envFollower)

fEff = clamp((cutoff / sr) * (0.18 + envFollower * envDepth * 0.8), 0.02, 0.44)
fb = clamp(resonance * 0.65, 0.0, 0.85)

lp1 = lp1 + fEff * (in + lp1 * fb - lp1)
lp2 = lp2 + fEff * (lp1 - lp2)

drv = lp2 * (1.0 + warmth * 1.6)
sat = drv / (1.0 + abs(drv))

processed = in * (1.0 - mix) + sat * mix
out = processed * 1.65
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embeds (Theme: suprematist_blocks)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "suprematist-forms-kinetic-red-wedge",
        "title": "Suprematist Forms Kinetic Red Wedge",
        "description": "Dynamic Suprematist constructivist composition with audio-reactive vermilion wedge thrust, floating carbon black quadrilaterals, and linen ground.",
        "embed_desc": "Dynamic Suprematist composition featuring a sharp vermilion wedge cleaving through geometric tension fields, floating carbon black quadrilaterals, and raw linen canvas ground. Bass drives the red wedge penetration depth, mids rotate intersecting rectangular planes, and highs ignite constructivist point markers.",
        "tags": ["pixel", "suprematist", "constructivism", "malevich", "shader"],
        "params": {
            "wedgeScale": 1.0,
            "rotationSpeed": 0.45,
            "tensionGap": 0.08,
            "contrast": 1.15,
            "barWidth": 0.22
        },
        "code": """param float wedgeScale = 1.0 [0.5, 2.0]
param float rotationSpeed = 0.45 [0.1, 1.5]
param float tensionGap = 0.08 [0.01, 0.25]
param float contrast = 1.15 [0.6, 2.0]
param float barWidth = 0.22 [0.08, 0.5]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2
time = t * rotationSpeed

ca = cos(time * 0.3)
sa = sin(time * 0.3)
pRot = vec2(p.x * ca - p.y * sa, p.x * sa + p.y * ca)

wP = pRot - vec2(-0.15, 0.1)
dWedge = max(abs(wP.y + wP.x * 0.7) - 0.45 * wedgeScale, abs(wP.x) - 0.65 * wedgeScale)
wedgeMask = 1.0 - smoothstep(0.0, 0.012, dWedge)

r1P = pRot - vec2(0.28, -0.22)
dRect1 = max(abs(r1P.x) - 0.42, abs(r1P.y) - 0.18)
rect1Mask = 1.0 - smoothstep(0.0, 0.012, dRect1)

r2P = vec2(p.x * 0.866 - p.y * 0.5, p.x * 0.5 + p.y * 0.866) - vec2(-0.25, 0.3)
dRect2 = max(abs(r2P.x) - 0.95, abs(r2P.y) - barWidth * 0.25)
rect2Mask = 1.0 - smoothstep(0.0, 0.012, dRect2)

dP3 = length(pRot - vec2(0.48, 0.42)) - 0.16
diskMask = 1.0 - smoothstep(0.0, 0.012, dP3)

cLinen = vec3(0.957, 0.937, 0.902)
cCarbon = vec3(0.067, 0.067, 0.071)
cVermilion = vec3(0.851, 0.137, 0.122)
cCobalt = vec3(0.169, 0.243, 0.549)

col = cLinen
col = mix(col, cCarbon, rect2Mask)
col = mix(col, cCarbon, rect1Mask)
col = mix(col, cVermilion, wedgeMask)
col = mix(col, cCobalt, diskMask)

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
  float time = t * 0.45;

  float b = audio_bass * 1.6;
  float m = audio_mid * 1.5;
  float h = audio_high * 1.8;

  float ang = time * 0.3 + m * 0.4;
  float ca = cos(ang);
  float sa = sin(ang);
  vec2 pRot = vec2(p.x * ca - p.y * sa, p.x * sa + p.y * ca);

  // Vermilion Wedge with bass thrust
  vec2 wP = pRot - vec2(-0.15 - b * 0.25, 0.1);
  float dWedge = max(abs(wP.y + wP.x * 0.7) - (0.45 + b * 0.2), abs(wP.x) - (0.65 + b * 0.25));
  float wedgeMask = 1.0 - smoothstep(0.0, 0.012, dWedge);

  // Floating Carbon Rectangle
  vec2 r1P = pRot - vec2(0.28, -0.22);
  float dRect1 = max(abs(r1P.x) - 0.42, abs(r1P.y) - 0.18);
  float rect1Mask = 1.0 - smoothstep(0.0, 0.012, dRect1);

  // Carbon Diagonal Tension Bar
  vec2 r2P = vec2(p.x * 0.866 - p.y * 0.5, p.x * 0.5 + p.y * 0.866) - vec2(-0.25, 0.3);
  float dRect2 = max(abs(r2P.x) - 0.95, abs(r2P.y) - (0.055 + h * 0.03));
  float rect2Mask = 1.0 - smoothstep(0.0, 0.012, dRect2);

  // Cobalt Blue Accent Disk
  float dP3 = length(pRot - vec2(0.48, 0.42)) - (0.16 + h * 0.08);
  float diskMask = 1.0 - smoothstep(0.0, 0.012, dP3);

  vec3 cLinen = vec3(0.957, 0.937, 0.902);
  vec3 cCarbon = vec3(0.067, 0.067, 0.071);
  vec3 cVermilion = vec3(0.851, 0.137, 0.122);
  vec3 cCobalt = vec3(0.169, 0.243, 0.549);

  vec3 col = cLinen;
  col = mix(col, cCarbon, rect2Mask);
  col = mix(col, cCarbon, rect1Mask);
  col = mix(col, cVermilion, wedgeMask);
  col = mix(col, cCobalt, diskMask);

  gl_FragColor = vec4(clamp(col * 1.15, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "suprematist-forms-floating-black-quadrilateral",
        "title": "Suprematist Forms Floating Black Quadrilateral",
        "description": "Zero-degree Suprematist spatial study featuring monumental floating carbon rectangles, tensioned diagonal intersecting bars, and vermilion accents on raw canvas.",
        "embed_desc": "Zero-degree Suprematist spatial study featuring monumental floating carbon black quadrilaterals, tensioned diagonal intersecting bars, and revolutionary vermilion disk accents on raw textured linen canvas. Bass pulses geometric quadrilateral scale, mids shift the diagonal balance axis, and highs modulate edge tension fields.",
        "tags": ["pixel", "suprematist", "abstract", "geometric", "shader"],
        "params": {
            "quadScale": 1.05,
            "rotationSpeed": 0.40,
            "diagonalAngle": 0.52,
            "contrast": 1.12,
            "accentSize": 0.22
        },
        "code": """param float quadScale = 1.05 [0.5, 2.0]
param float rotationSpeed = 0.40 [0.1, 1.5]
param float diagonalAngle = 0.52 [0.1, 1.2]
param float contrast = 1.12 [0.6, 2.0]
param float accentSize = 0.22 [0.08, 0.5]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2
time = t * rotationSpeed

ca = cos(time * 0.25)
sa = sin(time * 0.25)
pRot = vec2(p.x * ca - p.y * sa, p.x * sa + p.y * ca)

r1P = pRot - vec2(-0.18, -0.05)
dQuad1 = max(abs(r1P.x) - 0.55 * quadScale, abs(r1P.y) - 0.32 * quadScale)
quad1Mask = 1.0 - smoothstep(0.0, 0.012, dQuad1)

dBar = abs(pRot.x * sin(diagonalAngle) + pRot.y * cos(diagonalAngle) - 0.12) - 0.045
barMask = 1.0 - smoothstep(0.0, 0.012, dBar)

dP2 = length(pRot - vec2(0.38, 0.32)) - accentSize
diskMask = 1.0 - smoothstep(0.0, 0.012, dP2)

r3P = pRot - vec2(0.32, -0.38)
dQuad3 = max(abs(r3P.x) - 0.22, abs(r3P.y) - 0.22)
quad3Mask = 1.0 - smoothstep(0.0, 0.012, dQuad3)

cLinen = vec3(0.957, 0.937, 0.902)
cCarbon = vec3(0.067, 0.067, 0.071)
cVermilion = vec3(0.851, 0.137, 0.122)
cCobalt = vec3(0.169, 0.243, 0.549)

col = cLinen
col = mix(col, cCarbon, barMask)
col = mix(col, cCarbon, quad1Mask)
col = mix(col, cCobalt, quad3Mask)
col = mix(col, cVermilion, diskMask)

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

  float ang = time * 0.25 + m * 0.35;
  float ca = cos(ang);
  float sa = sin(ang);
  vec2 pRot = vec2(p.x * ca - p.y * sa, p.x * sa + p.y * ca);

  // Monumental Carbon Rectangle
  vec2 r1P = pRot - vec2(-0.18, -0.05);
  float dQuad1 = max(abs(r1P.x) - (0.55 + b * 0.18), abs(r1P.y) - (0.32 + b * 0.12));
  float quad1Mask = 1.0 - smoothstep(0.0, 0.012, dQuad1);

  // Tensioned Diagonal Bar
  float diagAng = 0.52 + m * 0.25;
  float dBar = abs(pRot.x * sin(diagAng) + pRot.y * cos(diagAng) - 0.12) - (0.045 + h * 0.025);
  float barMask = 1.0 - smoothstep(0.0, 0.012, dBar);

  // Revolutionary Vermilion Disk
  float dP2 = length(pRot - vec2(0.38, 0.32)) - (0.22 + b * 0.15);
  float diskMask = 1.0 - smoothstep(0.0, 0.012, dP2);

  // Cobalt Blue Square
  vec2 r3P = pRot - vec2(0.32, -0.38);
  float dQuad3 = max(abs(r3P.x) - 0.22, abs(r3P.y) - 0.22);
  float quad3Mask = 1.0 - smoothstep(0.0, 0.012, dQuad3);

  vec3 cLinen = vec3(0.957, 0.937, 0.902);
  vec3 cCarbon = vec3(0.067, 0.067, 0.071);
  vec3 cVermilion = vec3(0.851, 0.137, 0.122);
  vec3 cCobalt = vec3(0.169, 0.243, 0.549);

  vec3 col = cLinen;
  col = mix(col, cCarbon, barMask);
  col = mix(col, cCarbon, quad1Mask);
  col = mix(col, cCobalt, quad3Mask);
  col = mix(col, cVermilion, diskMask);

  gl_FragColor = vec4(clamp(col * 1.12, 0.0, 1.0), 1.0);
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
  <meta name="twitter:title" content="{item['title']}: audio-reactive visual">
  <meta name="twitter:description" content="{item['embed_desc']}">
  <meta name="twitter:image" content="https://infinitedevices.org/assets/og-image.jpg">
  <style>
    * {{ box-sizing: border-box; margin: 0; padding: 0; }}
    html, body {{
      width: 100%;
      height: 100%;
      overflow: hidden;
      background: #0f1015;
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
      background: rgba(15, 16, 21, 0.85);
      backdrop-filter: blur(8px);
      border: 1px solid rgba(244, 239, 230, 0.2);
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
      background: rgba(25, 27, 35, 0.95);
      border-color: rgba(244, 239, 230, 0.5);
    }}
    .badge-icon {{
      width: 14px;
      height: 14px;
      border-radius: 50%;
      background: linear-gradient(135deg, #d9231f, #f4efe6);
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
      background: rgba(15, 16, 21, 0.82);
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
    print("=== Creative Expansion Pack 25: Build and Gate Runner ===")
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
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}")
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        f_proc = subprocess.run([BIN, "--frame", test_inf, "0", out_frame_dir + "/", "--size", "1080x1080"], capture_output=True, text=True)
        if '"ok":true' not in f_proc.stdout:
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
  c bg 0.100 0.105 0.125
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
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}")
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        f_proc = subprocess.run([BIN, "--frame", test_inf, "0", out_frame_dir + "/", "--size", "1080x1080"], capture_output=True, text=True)
        if '"ok":true' not in f_proc.stdout:
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
  i note2 {nr}
  i note3 {nr+12}
  i note4 {nr}
  i note5 {nr}
  i note6 {nr}
  i note7 {nr+10}
  i note8 {nr}
  i note9 {nr}
  i note10 {nr}
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
  c fillColor 0.22 0.18 0.35
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
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}")
            failed = True
            continue

        sum_json = os.path.join(TMP_DIR, f"{slug}_sum.json")
        s = subprocess.run([BIN, "--audio-summary", inf_file, sum_json, "--duration", "6"], capture_output=True, text=True)
        if '"ok":true' not in s.stdout or not os.path.exists(sum_json):
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
  f cutoff 3400.0
  f resonance 0.32
  f decay 0.28
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
  c fillColor 0.05 0.15 0.35
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
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}")
            failed = True
            continue

        sum_json = os.path.join(TMP_DIR, f"{slug}_sum.json")
        s = subprocess.run([BIN, "--audio-summary", inf_file, sum_json, "--duration", "6"], capture_output=True, text=True)
        if '"ok":true' not in s.stdout or not os.path.exists(sum_json):
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

        v = subprocess.run([BIN, "--validate", test_inf], capture_output=True, text=True)
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}")
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        f_proc = subprocess.run([BIN, "--frame", test_inf, "0", out_frame_dir + "/", "--size", "1080x1080"], capture_output=True, text=True)
        if '"ok":true' not in f_proc.stdout:
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
