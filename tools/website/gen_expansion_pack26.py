#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 26.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (demoscene-plasma-rotozoom-raster-bars, demoscene-plasma-sine-wave-interference)
2. Sketch 3D (holographic-iridescent-faceted-polytope-3d, holographic-iridescent-prismatic-gem-3d)
3. Field Synth (progressive-house-euphoric-pluck-lead-synth, progressive-house-driving-sub-bass-synth)
4. Field Effects (lofi-beats-tape-flutter-cassette-warmth-fx, lofi-beats-nostalgic-vinyl-crackle-filter-fx)
5. Field Pixel (data-landscape-topographic-frequency-contour, data-landscape-bathymetric-elevation-grid)
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

TMP_DIR = "/tmp/pack26_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: demoscene_plasma)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "demoscene-plasma-rotozoom-raster-bars",
        "title": "Demoscene Plasma Rotozoom Raster Bars",
        "description": "Amiga demoscene study featuring interlocking sinusoidal copper raster bars, rotating geometric rosettes, and electric cyan and flame orange palettes.",
        "tags": ["demoscene", "plasma", "rotozoom", "copperbars", "mathematical", "drawing"],
        "code": """// Demoscene Plasma Rotozoom Raster Bars - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Amiga demoscene study featuring interlocking sinusoidal copper raster bars, rotating geometric rosettes, and electric cyan and flame orange palettes.
param("bars", 9.0, 4.0, 16.0);
param("rosette_petals", 6.0, 3.0, 12.0);
param("speed", 0.5, 0.1, 1.5);
param("bar_spread", 1.1, 0.5, 2.0);

function draw(t) {
  // Deep obsidian void (#07080c)
  background(0.027, 0.031, 0.047);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;

  push();
  translate(cx, cy);

  // Precision framing boundary (#1a2035)
  stroke(0.102, 0.125, 0.208);
  strokeWeight(2.4);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  const time = t * speed;

  // 1. Interlocking multi-frequency copper raster bars
  const barCount = floor(bars);
  for (let i = 0; i < barCount; i++) {
    const norm = (i / (barCount - 1)) * 2.0 - 1.0;
    const yBase = norm * span * 0.38 * bar_spread;
    const wave = sin(time * 2.5 + i * 0.65) * (span * 0.06);
    const y = yBase + wave;
    const barH = span * 0.045 * (1.0 - abs(norm) * 0.3);

    // Dynamic Amiga gradient: Flame Orange (#ff3b00) transitioning to Electric Cyan (#00e0ff)
    const blend = sin(time * 1.8 + i * 0.8) * 0.5 + 0.5;
    const r = 1.0 * (1.0 - blend) + 0.0 * blend;
    const g = 0.231 * (1.0 - blend) + 0.878 * blend;
    const b = 0.0 * (1.0 - blend) + 1.0 * blend;

    fill(r, g, b, 0.88);
    noStroke();
    rect(-span * 0.44, y - barH * 0.5, span * 0.88, barH);

    // High-luminance white core line (#ffffff)
    stroke(1.0, 1.0, 1.0, 0.92);
    strokeWeight(1.8);
    line(-span * 0.44, y, span * 0.44, y);
  }

  // 2. Rotating geometric rosette moire petals at center
  push();
  rotate(time * 0.7);
  const petals = floor(rosette_petals);
  const rad = span * 0.28;
  stroke(0.0, 0.878, 1.0, 0.85); // Electric Cyan
  strokeWeight(2.2);
  noFill();
  for (let p = 0; p < petals; p++) {
    const ang = (p / petals) * Math.PI * 2.0;
    push();
    rotate(ang);
    ellipse(rad * 0.4, 0, rad * 0.8, rad * 0.35);
    pop();
  }

  // 3. Central flame orange solar disk with white registration cross
  fill(1.0, 0.231, 0.0, 0.95);
  noStroke();
  circle(0, 0, span * 0.12);

  stroke(1.0, 1.0, 1.0);
  strokeWeight(2.0);
  noFill();
  circle(0, 0, span * 0.16);
  pop();

  // 4. Outer precision registration crosshairs & scan ticks
  stroke(0.0, 0.878, 1.0, 0.65);
  strokeWeight(1.4);
  const ticks = 6;
  for (let k = -ticks; k <= ticks; k++) {
    const x = (k / ticks) * (span * 0.42);
    line(x, -span * 0.46, x, -span * 0.44);
    line(x, span * 0.44, x, span * 0.46);
    line(-span * 0.46, x, -span * 0.44, x);
    line(span * 0.44, x, span * 0.46, x);
  }

  pop();
}
"""
    },
    {
        "slug": "demoscene-plasma-sine-wave-interference",
        "title": "Demoscene Plasma Sine Wave Interference",
        "description": "Mathematical wave interference study with concentric interference rings, dynamic polar rosettes, and vivid Amiga demoscene copper gradient bars on pitch black ground.",
        "tags": ["demoscene", "plasma", "interference", "moire", "generative", "drawing"],
        "code": """// Demoscene Plasma Sine Wave Interference - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Mathematical wave interference study with concentric interference rings, dynamic polar rosettes, and vivid Amiga demoscene copper gradient bars on pitch black ground.
param("frequencies", 5.0, 2.0, 10.0);
param("ring_density", 12.0, 6.0, 24.0);
param("speed", 0.45, 0.1, 1.2);
param("amplitude", 1.15, 0.5, 2.0);

function draw(t) {
  // Deep obsidian void (#0c0e18)
  background(0.048, 0.055, 0.088);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;

  push();
  translate(cx, cy);

  // Outer framing boundary
  stroke(0.12, 0.16, 0.28);
  strokeWeight(2.8);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  const time = t * speed;

  // 1. Concentric elliptical wave interference rings
  const rings = floor(ring_density);
  for (let r = rings; r >= 1; r--) {
    const fr = r / rings;
    const radX = fr * span * 0.44 * amplitude;
    const radY = fr * span * 0.32 * amplitude;
    const warp = sin(time * 3.0 + r * 0.7) * (span * 0.03);

    const isCyan = (r % 2 === 0);
    if (isCyan) {
      stroke(0.0, 0.878, 1.0, 0.85); // Electric Cyan
      strokeWeight(2.4);
    } else {
      stroke(1.0, 0.231, 0.0, 0.90); // Flame Orange
      strokeWeight(2.2);
    }
    noFill();
    ellipse(0, 0, radX * 2.0 + warp, radY * 2.0 - warp);
  }

  // 2. Rotating polar harmonic rose curve
  push();
  rotate(time * 0.6);
  stroke(1.0, 1.0, 1.0, 0.95);
  strokeWeight(2.6);
  noFill();
  beginShape();
  const k = floor(frequencies);
  const steps = 180;
  for (let s = 0; s <= steps; s++) {
    const theta = (s / steps) * Math.PI * 2.0;
    const radius = span * 0.26 * cos(k * theta) * amplitude;
    vertex(cos(theta) * radius, sin(theta) * radius);
  }
  endShape();
  pop();

  // 3. Counter-rotating copper starburst axes
  push();
  rotate(-time * 0.45);
  stroke(1.0, 0.231, 0.0, 0.92);
  strokeWeight(3.0);
  line(-span * 0.38, 0, span * 0.38, 0);
  line(0, -span * 0.38, 0, span * 0.38);
  stroke(0.0, 0.878, 1.0, 0.92);
  strokeWeight(2.0);
  line(-span * 0.28, -span * 0.28, span * 0.28, span * 0.28);
  line(-span * 0.28, span * 0.28, span * 0.28, -span * 0.28);
  pop();

  // 4. Center luminescent core
  fill(1.0, 1.0, 1.0, 0.98);
  noStroke();
  circle(0, 0, span * 0.09);

  // 5. Registration grid
  stroke(0.0, 0.878, 1.0, 0.7);
  strokeWeight(1.4);
  for (let i = -5; i <= 5; i++) {
    const p = (i / 5.0) * (span * 0.42);
    line(p, -8, p, 8);
    line(-8, p, 8, p);
  }

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: holographic_iridescent)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "holographic-iridescent-faceted-polytope-3d",
        "title": "Holographic Iridescent Faceted Polytope 3D",
        "description": "Complex rotating 3D polytope with crystalline multifaceted geometry, chromatic spectral dispersion facets, and iridescent pastel gradient layers on deep obsidian.",
        "tags": ["holographic", "iridescent", "polytope", "crystal", "geometry", "3d"],
        "code": """// Holographic Iridescent Faceted Polytope 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Complex rotating 3D polytope with crystalline multifaceted geometry, chromatic spectral dispersion facets, and iridescent pastel gradient layers on deep obsidian.
param("facets", 10.0, 6.0, 16.0);
param("rotation_speed", 0.4, 0.1, 1.2);
param("dispersion_scale", 1.2, 0.5, 2.2);
param("facet_depth", 0.85, 0.3, 1.8);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.48 + sin(time * 0.16) * 0.05);
  rotateY(time * 0.32 + 0.6);

  // Deep obsidian pedestal base slab (#0c0c14)
  push();
  translate(0, 1.1, 0);
  fill(0.047, 0.047, 0.078);
  box(2.2 * dispersion_scale, 0.15, 2.2 * dispersion_scale);
  pop();

  // Primary central crystalline dodecahedral core (iridescent white-cyan #e8f9ff)
  push();
  translate(0, 0.05, 0);
  fill(0.91, 0.97, 1.0);
  box(0.95 * dispersion_scale, 0.95 * dispersion_scale, 0.95 * dispersion_scale);
  pop();

  // Multi-faceted chromatic dispersion satellite planes
  const facetCount = floor(facets);
  for (let i = 0; i < facetCount; i++) {
    const ang = (i / facetCount) * Math.PI * 2.0;
    const r = 1.15 * dispersion_scale;
    const yOff = sin(time * 0.8 + i * 0.6) * 0.28;

    push();
    translate(cos(ang) * r, yOff, sin(ang) * r);
    rotateY(-ang + time * 0.4);
    rotateZ(0.35 * sin(time + i));

    // Pastel iridescent spectrum rotation:
    // Cyan (#a9f0ff), Magenta-Pink (#ffb3f5), Lime (#d6ff9e), Lavender (#d4c5ff)
    const phase = (i % 4);
    if (phase === 0) fill(0.663, 0.941, 1.0);
    else if (phase === 1) fill(1.0, 0.702, 0.961);
    else if (phase === 2) fill(0.839, 1.0, 0.620);
    else fill(0.831, 0.773, 1.0);

    box(0.18 * facet_depth, 0.75 * dispersion_scale, 0.48 * dispersion_scale);
    pop();
  }

  // Floating crystalline top crown pyramid
  push();
  translate(0, -0.85 * dispersion_scale, 0);
  rotateY(-time * 0.5);
  fill(1.0, 0.85, 0.95);
  box(0.55 * dispersion_scale, 0.55 * dispersion_scale, 0.55 * dispersion_scale);
  pop();

  // Radiating prismatic edge struts
  for (let j = 0; j < 4; j++) {
    const a = j * Math.PI * 0.5 + time * 0.2;
    push();
    translate(cos(a) * 0.7, 0.05, sin(a) * 0.7);
    rotateY(a);
    fill(0.663, 0.941, 1.0);
    box(0.08, 1.25 * dispersion_scale, 0.08);
    pop();
  }

  pop();
}
"""
    },
    {
        "slug": "holographic-iridescent-prismatic-gem-3d",
        "title": "Holographic Iridescent Prismatic Gem 3D",
        "description": "Floating prismatic crystal cluster with interlocking geometric pyramids, radiant chromatic dispersion planes, and luminescent pastel edge boundaries.",
        "tags": ["holographic", "prismatic", "gem", "crystal", "sculpture", "3d"],
        "code": """// Holographic Iridescent Prismatic Gem 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Floating prismatic crystal cluster with interlocking geometric pyramids, radiant chromatic dispersion planes, and luminescent pastel edge boundaries.
param("cluster_count", 8.0, 4.0, 14.0);
param("spread", 1.15, 0.5, 2.0);
param("spin", 0.38, 0.1, 1.0);
param("gem_aspect", 1.3, 0.5, 2.5);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.44 + sin(time * 0.18) * 0.04);
  rotateY(time * 0.28 + 0.8);

  // Obsidian foundation plinth (#090910)
  push();
  translate(0, 1.15, 0);
  fill(0.035, 0.035, 0.063);
  box(2.4 * spread, 0.14, 2.4 * spread);
  pop();

  // Central monolithic crystal spire
  push();
  translate(0, 0.0, 0);
  rotateY(time * 0.4);
  fill(0.94, 0.96, 1.0);
  box(0.65 * spread, 1.6 * gem_aspect, 0.65 * spread);
  pop();

  // Orbiting crystalline satellite prisms with pastel iridescent dispersion
  const count = floor(cluster_count);
  for (let i = 0; i < count; i++) {
    const ang = (i / count) * Math.PI * 2.0;
    const dist = (0.85 + (i % 2) * 0.35) * spread;
    const yTier = 0.45 - (i / count) * 0.9;

    push();
    translate(cos(ang + time * 0.3) * dist, yTier, sin(ang + time * 0.3) * dist);
    rotateY(ang + time * 0.6);
    rotateZ(0.25 * (i % 2 === 0 ? 1 : -1));

    // Chromatic dispersion tones
    if (i % 3 === 0) fill(0.663, 0.941, 1.0);
    else if (i % 3 === 1) fill(1.0, 0.702, 0.961);
    else fill(0.839, 1.0, 0.620);

    box(0.35 * spread, 0.85 * gem_aspect, 0.35 * spread);
    pop();
  }

  // Interlocking upper cantilever gem cap
  push();
  translate(0, -1.0 * gem_aspect, 0);
  rotateY(-time * 0.6);
  fill(1.0, 0.702, 0.961);
  box(0.85 * spread, 0.22, 0.85 * spread);
  pop();

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: progressive_house)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "progressive-house-euphoric-pluck-lead-synth",
        "title": "Progressive House Euphoric Pluck Lead Synth",
        "description": "Euphoric progressive house pluck lead with multi-saw unison detune, snappy resonant filter envelope sweep, and analog saturation.",
        "tags": ["synth", "progressive-house", "pluck", "lead", "saw", "audio"],
        "note_root": 58,
        "params": {
            "cutoff": 0.58,
            "resonance": 0.42,
            "detuneSpread": 0.35,
            "pluckDecay": 0.22,
            "drive": 0.28
        },
        "code": """param float cutoff = 0.58 [0.1, 1.0]
param float resonance = 0.42 [0.0, 0.85]
param float detuneSpread = 0.35 [0.0, 1.0]
param float pluckDecay = 0.22 [0.05, 0.8]
param float drive = 0.28 [0.0, 1.0]
state float p1 = 0
state float p2 = 0
state float p3 = 0
state float env = 0
state float lp1 = 0
state float lp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * pluckDecay), 0.0001, 0.04)
env = if(target > env, env + (target - env) * 0.75, env - env * decayRate)

fBase = clamp(freq, 110.0, 1200.0)

dp1 = fBase / sr
p1 = p1 + dp1
p1 = if(p1 >= 1.0, p1 - 1.0, p1)
s1 = p1 * 2.0 - 1.0

dp2 = (fBase * (1.0 + detuneSpread * 0.007)) / sr
p2 = p2 + dp2
p2 = if(p2 >= 1.0, p2 - 1.0, p2)
s2 = p2 * 2.0 - 1.0

dp3 = (fBase * (1.0 - detuneSpread * 0.007)) / sr
p3 = p3 + dp3
p3 = if(p3 >= 1.0, p3 - 1.0, p3)
s3 = p3 * 2.0 - 1.0

raw = (s1 + s2 * 0.8 + s3 * 0.8) * 0.40

drv = raw * (1.0 + drive * 1.5)
sat = (drv / (1.0 + abs(drv))) * env

fCut = clamp(cutoff * (0.10 + env * 0.55), 0.015, 0.44)
lp1 = lp1 + fCut * (sat + lp1 * resonance * 0.45 - lp1)
lp2 = lp2 + fCut * (lp1 - lp2)

out = lp2 * 0.50
"""
    },
    {
        "slug": "progressive-house-driving-sub-bass-synth",
        "title": "Progressive House Driving Sub Bass Synth",
        "description": "Driving rolling progressive house bass with layered saw core, sub-octave square reinforcement, and punchy resonant low-pass filter envelope.",
        "tags": ["synth", "progressive-house", "bass", "sub", "rolling", "audio"],
        "note_root": 46,
        "params": {
            "cutoff": 0.48,
            "resonance": 0.38,
            "subGain": 0.45,
            "decayTime": 0.24,
            "drive": 0.32
        },
        "code": """param float cutoff = 0.48 [0.1, 1.0]
param float resonance = 0.38 [0.0, 0.85]
param float subGain = 0.45 [0.0, 1.0]
param float decayTime = 0.24 [0.05, 0.8]
param float drive = 0.32 [0.0, 1.0]
state float p1 = 0
state float pSub = 0
state float env = 0
state float lp1 = 0
state float lp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decayTime), 0.0001, 0.035)
env = if(target > env, env + (target - env) * 0.65, env - env * decayRate)

fBase = clamp(freq, 35.0, 440.0)

dp1 = fBase / sr
p1 = p1 + dp1
p1 = if(p1 >= 1.0, p1 - 1.0, p1)
saw1 = p1 * 2.0 - 1.0

dpSub = (fBase * 0.5) / sr
pSub = pSub + dpSub
pSub = if(pSub >= 1.0, pSub - 1.0, pSub)
subSquare = if(pSub < 0.5, 0.48, -0.48)

raw = saw1 * 0.62 + subSquare * subGain

drv = raw * (1.0 + drive * 1.6)
sat = (drv / (1.0 + abs(drv))) * env

fCut = clamp(cutoff * (0.12 + env * 0.42), 0.01, 0.42)
lp1 = lp1 + fCut * (sat + lp1 * resonance * 0.45 - lp1)
lp2 = lp2 + fCut * (lp1 - lp2)

out = lp2 * 0.55
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: lofi_beats)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "lofi-beats-tape-flutter-cassette-warmth-fx",
        "title": "Lo-Fi Beats Tape Flutter Cassette Warmth FX",
        "description": "Vintage lo-fi cassette tape emulation with gentle wow and flutter pitch modulation, magnetic head high-frequency damping, and soft asymmetric tape saturation.",
        "tags": ["effect", "lofi", "tape", "flutter", "cassette", "audio"],
        "params": {
            "flutterSpeed": 1.4,
            "flutterDepth": 0.32,
            "tapeDamping": 3200.0,
            "drive": 0.40,
            "mix": 0.60
        },
        "code": """param float flutterSpeed = 1.4 [0.2, 5.0]
param float flutterDepth = 0.32 [0.0, 1.0]
param float tapeDamping = 3200.0 [800.0, 8000.0]
param float drive = 0.40 [0.0, 1.0]
param float mix = 0.60 [0.0, 1.0]
state float ph = 0
state float d1 = 0
state float d2 = 0
state float lp = 0

ph = ph + flutterSpeed / sr
ph = if(ph >= 1.0, ph - 1.0, ph)
flutter = sin(ph * 6.2831853) * flutterDepth * 0.06

fDamp = clamp(tapeDamping / sr * 2.0, 0.02, 0.44)
lp = lp + fDamp * (in - lp)

fb = 0.45

d1 = d1 + (0.16 + flutter) * (lp + d2 * fb - d1)
d2 = d2 + (0.12 - flutter) * (d1 * 0.85 - d2)

wet = (d1 * 0.65 + d2 * 0.5) * 1.15
drv = wet * (1.0 + drive * 1.4)
sat = clamp(drv / (1.0 + abs(drv * 0.5)), -0.85, 0.85)

processed = in * (1.0 - mix) + sat * mix
out = processed * 0.58
"""
    },
    {
        "slug": "lofi-beats-nostalgic-vinyl-crackle-filter-fx",
        "title": "Lo-Fi Beats Nostalgic Vinyl Crackle Filter FX",
        "description": "Nostalgic lo-fi resonant telephone bandpass filter with vinyl grit texture, subtle pitch drift, and warm harmonic drive.",
        "tags": ["effect", "lofi", "filter", "vinyl", "warmth", "audio"],
        "params": {
            "centerFreq": 1400.0,
            "bandWidth": 0.55,
            "warmth": 0.42,
            "vinylGrit": 0.30,
            "mix": 0.65
        },
        "code": """param float centerFreq = 1400.0 [400.0, 4000.0]
param float bandWidth = 0.55 [0.1, 0.9]
param float warmth = 0.42 [0.0, 1.0]
param float vinylGrit = 0.30 [0.0, 1.0]
param float mix = 0.65 [0.0, 1.0]
state float envFollower = 0
state float lp1 = 0
state float lp2 = 0
state float noisePhase = 0

absIn = abs(in)
envFollower = envFollower + 0.04 * (absIn - envFollower)

fEff = clamp((centerFreq / sr) * (0.35 + envFollower * 0.5), 0.02, 0.44)
fb = clamp(bandWidth * 0.75, 0.0, 0.85)

lp1 = lp1 + fEff * (in + lp1 * fb - lp1)
lp2 = lp2 + fEff * (lp1 - lp2)
bp = (lp1 - lp2) * 1.5

noisePhase = noisePhase + 0.37
noisePhase = if(noisePhase > 1.0, noisePhase - 1.0, noisePhase)
noise = sin(noisePhase * 7919.0) * vinylGrit * 0.012

sig = bp + noise
drv = sig * (1.0 + warmth * 1.8)
sat = drv / (1.0 + abs(drv))

processed = in * (1.0 - mix) + sat * mix
out = processed * 1.45
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embeds (Theme: data_landscape)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "data-landscape-topographic-frequency-contour",
        "title": "Data Landscape Topographic Frequency Contour",
        "description": "Audio-reactive topological landscape with dense horizontal elevation contours, audio-displaced frequency ridges, and glowing cyan signal peaks on midnight obsidian.",
        "embed_desc": "Audio-reactive topological data landscape featuring stacked horizontal elevation contours that warp and crest to the sound. Bass drives the monumental central ridge peaks, mids displace lateral frequency modulations, and highs trigger glowing cyan contour scanlines across a deep oceanic abyssal ground.",
        "tags": ["pixel", "datalandscape", "topography", "contour", "shader", "audioreactive"],
        "params": {
            "ridgeScale": 1.05,
            "flowSpeed": 0.40,
            "contourDensity": 16.0,
            "contrast": 1.15,
            "peakElevation": 0.55
        },
        "code": """param float ridgeScale = 1.05 [0.5, 2.0]
param float flowSpeed = 0.40 [0.1, 1.5]
param float contourDensity = 16.0 [8.0, 32.0]
param float contrast = 1.15 [0.6, 2.0]
param float peakElevation = 0.55 [0.2, 1.2]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0
time = t * flowSpeed

distX = abs(p.x)
ridgeEnvelope = exp(-distX * distX * 3.5)

wave1 = sin(distX * 18.0 - time * 3.2) * 0.08
wave2 = sin(distX * 32.0 + time * 2.1) * 0.04
elevation = (wave1 + wave2 + 0.12) * ridgeEnvelope * ridgeScale * peakElevation

elevP = p.y - elevation + 0.1

contourVal = elevP * contourDensity
contourFrac = abs(fract(contourVal) - 0.5)
contourMask = 1.0 - smoothstep(0.04, 0.12, contourFrac)

peakMask = smoothstep(0.18, 0.42, elevation) * contourMask

cAbyss = vec3(0.043, 0.075, 0.169)
cNavy = vec3(0.110, 0.145, 0.255)
cCyan = vec3(0.357, 0.753, 0.745)
cWhite = vec3(0.96, 0.98, 1.0)

baseCol = mix(cAbyss, cNavy, clamp((p.y + 1.0) * 0.5, 0.0, 1.0))
col = mix(baseCol, cCyan, contourMask * 0.85)
col = mix(col, cWhite, peakMask)

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
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0;
  float time = t * 0.40;

  float b = audio_bass * 1.6;
  float m = audio_mid * 1.5;
  float h = audio_high * 1.8;

  float distX = abs(p.x);
  float ridgeEnvelope = exp(-distX * distX * 3.5);

  float wave1 = sin(distX * (18.0 + m * 6.0) - time * 3.2) * (0.08 + b * 0.06);
  float wave2 = sin(distX * 32.0 + time * 2.1) * (0.04 + h * 0.03);
  float elevation = (wave1 + wave2 + 0.12 + b * 0.18) * ridgeEnvelope * 1.05 * 0.55;

  float elevP = p.y - elevation + 0.1;
  float contourVal = elevP * 16.0;
  float contourFrac = abs(fract(contourVal) - 0.5);
  float contourMask = 1.0 - smoothstep(0.04, 0.12, contourFrac);

  float peakMask = smoothstep(0.18, 0.42, elevation) * contourMask;

  vec3 cAbyss = vec3(0.043, 0.075, 0.169);
  vec3 cNavy = vec3(0.110, 0.145, 0.255);
  vec3 cCyan = vec3(0.357, 0.753, 0.745);
  vec3 cWhite = vec3(0.96, 0.98, 1.0);

  vec3 baseCol = mix(cAbyss, cNavy, clamp((p.y + 1.0) * 0.5, 0.0, 1.0));
  vec3 col = mix(baseCol, cCyan, contourMask * (0.85 + h * 0.15));
  col = mix(col, cWhite, peakMask);

  gl_FragColor = vec4(clamp(col * 1.15, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "data-landscape-bathymetric-elevation-grid",
        "title": "Data Landscape Bathymetric Elevation Grid",
        "description": "Isometric bathymetric data grid with pulsating harmonic contour lines, deep navy oceanic terrain troughs, and luminescent white elevation peaks.",
        "embed_desc": "Isometric bathymetric elevation grid visualizing subterranean data topographies with audio-reactive contour fields. Bass pulses the terrain amplitude and deep trench depths, mids ripple undulating wave currents along the grid, and highs ignite crisp topographic contour markers on deep navy.",
        "tags": ["pixel", "bathymetric", "elevation", "grid", "shader", "audioreactive"],
        "params": {
            "gridScale": 1.10,
            "flowSpeed": 0.38,
            "bathymetryTiers": 14.0,
            "contrast": 1.12,
            "waveAmplitude": 0.45
        },
        "code": """param float gridScale = 1.10 [0.5, 2.0]
param float flowSpeed = 0.38 [0.1, 1.5]
param float bathymetryTiers = 14.0 [6.0, 28.0]
param float contrast = 1.12 [0.6, 2.0]
param float waveAmplitude = 0.45 [0.1, 1.0]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2
time = t * flowSpeed

rotP = vec2(p.x * 0.866 - p.y * 0.5, p.x * 0.5 + p.y * 0.866)

shelf = sin(rotP.x * 5.0 + time * 2.0) * cos(rotP.y * 4.0 - time * 1.5) * waveAmplitude * 0.25
depthVal = length(rotP) * 1.2 - shelf

tierCount = floor(bathymetryTiers)
steppedDepth = floor(depthVal * tierCount) / tierCount
tierFrac = abs(fract(depthVal * tierCount) - 0.5)
tierLine = 1.0 - smoothstep(0.03, 0.10, tierFrac)

cAbyss = vec3(0.043, 0.075, 0.169)
cNavy = vec3(0.110, 0.145, 0.255)
cCyan = vec3(0.357, 0.753, 0.745)
cWhite = vec3(0.96, 0.98, 1.0)

col = mix(cAbyss, cNavy, clamp(depthVal * 0.7, 0.0, 1.0))
col = mix(col, cCyan, tierLine * 0.85)

crestMask = tierLine * step(0.65, 1.0 - depthVal * 0.4)
col = mix(col, cWhite, crestMask * 0.9)

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
  float time = t * 0.38;

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  vec2 rotP = vec2(p.x * 0.866 - p.y * 0.5, p.x * 0.5 + p.y * 0.866);

  float shelf = sin(rotP.x * (5.0 + m * 2.0) + time * 2.0) * cos(rotP.y * 4.0 - time * 1.5) * (0.45 + b * 0.25) * 0.25;
  float depthVal = length(rotP) * (1.2 + b * 0.15) - shelf;

  float tierCount = 14.0;
  float tierFrac = abs(fract(depthVal * tierCount) - 0.5);
  float tierLine = 1.0 - smoothstep(0.03, 0.10, tierFrac);

  vec3 cAbyss = vec3(0.043, 0.075, 0.169);
  vec3 cNavy = vec3(0.110, 0.145, 0.255);
  vec3 cCyan = vec3(0.357, 0.753, 0.745);
  vec3 cWhite = vec3(0.96, 0.98, 1.0);

  vec3 col = mix(cAbyss, cNavy, clamp(depthVal * 0.7, 0.0, 1.0));
  col = mix(col, cCyan, tierLine * (0.85 + h * 0.15));

  float crestMask = tierLine * step(0.65, 1.0 - depthVal * 0.4);
  col = mix(col, cWhite, crestMask * 0.9);

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
    print("=== Creative Expansion Pack 26: Build and Gate Runner ===")
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
  c bg 0.047 0.047 0.078
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
  i note2 {nr+7}
  i note3 {nr+12}
  i note4 {nr}
  i note5 {nr+3}
  i note6 {nr+7}
  i note7 {nr+10}
  i note8 {nr}
  i note9 {nr}
  i note10 {nr+7}
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
  c fillColor 0.15 0.22 0.38
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
  c fillColor 0.12 0.18 0.28
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
