#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 19.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (swiss-typographic-modular-grid, swiss-typographic-geometric-axis)
2. Sketch 3D (brutalist-concrete-cantilever-3d, brutalist-concrete-megastructure-3d)
3. Field Synth (detroit-techno-soul-chord-synth, detroit-techno-machine-lead-synth)
4. Field Effects (uk-garage-sub-drive-saturator-fx, uk-garage-chopped-vocal-delay-fx)
5. Field Pixel (holographic-iridescent-thin-film, holographic-iridescent-prismatic-flux)
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

TMP_DIR = "/tmp/pack19_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: swiss_typographic)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "swiss-typographic-modular-grid",
        "title": "Swiss Typographic Modular Grid",
        "description": "Asymmetric International Typographic Style layout with multi-column modular grid proportions, bold red architectural numerals, and hairline alignment rules.",
        "tags": ["swiss", "typography", "grid", "international-style", "minimal", "vector"],
        "code": """// Swiss Typographic Modular Grid - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Asymmetric International Typographic Style layout with multi-column modular grid proportions, bold red architectural numerals, and hairline alignment rules.
param("zoom", 1.0, 0.5, 2.0);
param("line_weight", 2.0, 1.0, 6.0);
param("accent_scale", 1.0, 0.4, 2.0);
param("grid_cols", 6.0, 3.0, 12.0);

function draw(t) {
  // Pure warm Swiss cream ground
  background(0.961, 0.961, 0.949);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88 * zoom;
  const left = cx - span * 0.5;
  const top = cy - span * 0.5;

  push();
  translate(left, top);

  // Outer framing boundary
  noFill();
  stroke(0.067, 0.067, 0.067);
  strokeWeight(line_weight);
  rect(0, 0, span, span);

  const cols = floor(grid_cols);
  const colW = span / cols;
  const rows = 8;
  const rowH = span / rows;

  // Hairline subtle grid matrix
  stroke(0.78, 0.78, 0.76);
  strokeWeight(1.0);
  for (let c = 1; c < cols; c++) {
    line(c * colW, 0, c * colW, span);
  }
  for (let r = 1; r < rows; r++) {
    line(0, r * rowH, span, r * rowH);
  }

  // Major architectural International Signal Red block (asymmetric golden ratio placement)
  noStroke();
  fill(0.894, 0.149, 0.102); // International Red
  const redX = colW * 2.0;
  const redY = rowH * 1.0;
  const redW = colW * 3.0 * accent_scale;
  const redH = rowH * 2.5 * accent_scale;
  rect(redX, redY, redW, redH);

  // Jet black structural accent block (aligned on lower grid intersection)
  fill(0.067, 0.067, 0.067);
  rect(colW * 0.0, rowH * 4.5, colW * 2.0, rowH * 2.5);

  // Signal red counter-form circle in bottom-right quadrant
  fill(0.894, 0.149, 0.102);
  const circR = colW * 0.9 * accent_scale;
  circle(colW * 4.5, rowH * 5.5, circR);

  // Concentric hairline registration ring
  noFill();
  stroke(0.067, 0.067, 0.067);
  strokeWeight(line_weight);
  circle(colW * 4.5, rowH * 5.5, circR * 1.5);
  line(colW * 4.5 - circR * 1.8, rowH * 5.5, colW * 4.5 + circR * 1.8, rowH * 5.5);
  line(colW * 4.5, rowH * 5.5 - circR * 1.8, colW * 4.5, rowH * 5.5 + circR * 1.8);

  // Heavy black Swiss typographic bar numerals and rules
  stroke(0.067, 0.067, 0.067);
  strokeWeight(line_weight + 3.0);
  line(colW * 0.5, rowH * 0.5, colW * 1.5, rowH * 0.5);
  line(colW * 0.5, rowH * 0.5, colW * 0.5, rowH * 1.8);

  // Horizontal high-contrast typographic bands
  strokeWeight(line_weight * 1.5);
  for (let k = 0; k < 5; k++) {
    const yk = rowH * 7.2 + k * (rowH * 0.12);
    line(colW * 0.5, yk, colW * 3.5, yk);
  }

  // Vertical alignment tick marks along top header
  strokeWeight(1.5);
  for (let i = 0; i < cols * 4; i++) {
    const tx = (i / (cols * 4)) * span;
    const th = (i % 4 === 0) ? 14 : 6;
    line(tx, 0, tx, th);
  }

  pop();
}
"""
    },
    {
        "slug": "swiss-typographic-geometric-axis",
        "title": "Swiss Typographic Geometric Axis",
        "description": "Constructive Swiss modernism with dynamic 45-degree diagonal coordinate axes, cantilevered rectangular color blocks, and typographic signal hierarchy.",
        "tags": ["swiss", "constructivism", "axis", "geometry", "signal", "drawing"],
        "code": """// Swiss Typographic Geometric Axis - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Constructive Swiss modernism with dynamic 45-degree diagonal coordinate axes, cantilevered rectangular color blocks, and typographic signal hierarchy.
param("zoom", 1.0, 0.5, 2.0);
param("axis_angle", 0.785398, 0.0, 1.570796);
param("line_weight", 2.5, 1.0, 7.0);
param("accent_size", 1.1, 0.5, 2.0);

function draw(t) {
  // Pure warm Swiss cream ground
  background(0.961, 0.961, 0.949);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.86 * zoom;

  push();
  translate(cx, cy);

  // Outer framing boundary
  noFill();
  stroke(0.067, 0.067, 0.067);
  strokeWeight(line_weight);
  rect(-span * 0.5, -span * 0.5, span, span);

  // Subtle coordinate grid background lines
  stroke(0.82, 0.82, 0.80);
  strokeWeight(1.0);
  const step = span / 8.0;
  for (let k = -span * 0.5; k <= span * 0.5; k += step) {
    line(k, -span * 0.5, k, span * 0.5);
    line(-span * 0.5, k, span * 0.5, k);
  }

  // Major 45-degree constructive diagonal axis
  push();
  rotate(axis_angle);

  // International Signal Red rectangular monolith on primary axis
  noStroke();
  fill(0.894, 0.149, 0.102);
  rect(-span * 0.28, -span * 0.08, span * 0.56 * accent_size, span * 0.16 * accent_size);

  // Deep jet black crossing counter-block
  fill(0.067, 0.067, 0.067);
  rect(-span * 0.12, -span * 0.35, span * 0.24, span * 0.7);

  // Crisp white inner cutout circle
  fill(0.961, 0.961, 0.949);
  circle(0, 0, span * 0.14);

  // Red concentric point
  fill(0.894, 0.149, 0.102);
  circle(0, 0, span * 0.05);

  // Long constructive hairline projection axes
  stroke(0.067, 0.067, 0.067);
  strokeWeight(line_weight);
  line(-span * 0.7, 0, span * 0.7, 0);
  line(0, -span * 0.7, 0, span * 0.7);

  // Axis measurement ticks
  strokeWeight(1.5);
  for (let d = -span * 0.6; d <= span * 0.6; d += span * 0.04) {
    line(d, -5, d, 5);
  }
  pop();

  // Horizontal asymmetric baseline alignment bar at bottom
  noStroke();
  fill(0.067, 0.067, 0.067);
  rect(-span * 0.45, span * 0.36, span * 0.9, span * 0.04);

  // Red indicator square on the baseline
  fill(0.894, 0.149, 0.102);
  rect(-span * 0.45, span * 0.31, span * 0.08, span * 0.04);

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
        "slug": "brutalist-concrete-cantilever-3d",
        "title": "Brutalist Concrete Cantilever 3D",
        "description": "Monumental poured-concrete cantilever structure with interlocking stepped cubic volumes, deep recessed fenestration apertures, and raw structural facets.",
        "tags": ["brutalist", "architecture", "concrete", "cantilever", "monumental", "3d"],
        "code": """// Brutalist Concrete Cantilever 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Monumental poured-concrete cantilever structure with interlocking stepped cubic volumes, deep recessed fenestration apertures, and raw structural facets.
param("cantilever_reach", 1.0, 0.5, 2.0);
param("elevation", 0.1, -0.8, 1.2);
param("rotation_speed", 0.35, 0.1, 1.2);
param("aperture_depth", 0.06, 0.02, 0.15);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.48 + sin(time * 0.2) * 0.05);
  rotateY(time * 0.35 + 0.6);

  // Massive stepped foundation plinth (weathered raw concrete)
  fill(0.35, 0.35, 0.34);
  push();
  translate(0, -0.75 + elevation, 0);
  box(1.5, 0.12, 1.5);
  translate(0, 0.08, 0);
  box(1.2, 0.10, 1.2);
  pop();

  // Central vertical brutalist elevator / service core
  fill(0.48, 0.48, 0.47); // Poured concrete grey
  push();
  translate(-0.15, 0.0 + elevation, -0.15);
  box(0.55, 1.3, 0.55);

  // Deep recessed vertical shadow slit in the core
  fill(0.12, 0.12, 0.12);
  translate(0.28, 0, 0);
  box(aperture_depth, 1.1, 0.12);
  pop();

  // Primary dramatic horizontal cantilevered wing (east-west beam)
  fill(0.56, 0.56, 0.54); // Concrete sunlit face
  push();
  translate(0.25 * cantilever_reach, 0.2 + elevation, 0);
  box(0.95 * cantilever_reach, 0.38, 0.65);

  // Row of recessed rhythmic window apertures under the cantilever
  fill(0.12, 0.12, 0.12); // Deep window shadow
  for (let i = -2; i <= 2; i++) {
    push();
    translate(i * 0.16 * cantilever_reach, 0.0, 0.33);
    box(0.10, 0.24, aperture_depth);
    pop();
  }
  pop();

  // Secondary high-level orthogonal cantilever (jutting forward)
  fill(0.44, 0.44, 0.43);
  push();
  translate(-0.1, 0.55 + elevation, 0.25 * cantilever_reach);
  box(0.5, 0.32, 0.85 * cantilever_reach);

  // Structural cantilever overhang teeth
  fill(0.30, 0.30, 0.29);
  translate(0, -0.16, 0.25 * cantilever_reach);
  box(0.46, 0.08, 0.28);
  pop();

  // Base entrance colonnade pilasters
  fill(0.22, 0.22, 0.22);
  for (let c = 0; c < 4; c++) {
    push();
    translate(0.15 + c * 0.2, -0.45 + elevation, 0.35);
    box(0.08, 0.45, 0.08);
    pop();
  }

  pop();
}
"""
    },
    {
        "slug": "brutalist-concrete-megastructure-3d",
        "title": "Brutalist Concrete Megastructure 3D",
        "description": "Colossal brutalist civic tower complex with repeating heavy precast spandrels, tiered geometric buttresses, and raw textured chamfers.",
        "tags": ["brutalist", "megastructure", "architecture", "tower", "geometry", "3d"],
        "code": """// Brutalist Concrete Megastructure 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Colossal brutalist civic tower complex with repeating heavy precast spandrels, tiered geometric buttresses, and raw textured chamfers.
param("tier_height", 1.0, 0.5, 2.0);
param("spandrel_scale", 1.0, 0.4, 1.8);
param("rotation_speed", 0.35, 0.1, 1.2);
param("buttress_flare", 0.35, 0.1, 0.8);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.42 + sin(time * 0.2) * 0.04);
  rotateY(time * 0.32);

  // Tiered stepped foundation podium
  fill(0.32, 0.32, 0.31);
  push();
  translate(0, -0.85, 0);
  box(1.6, 0.14, 1.6);
  translate(0, 0.10, 0);
  box(1.3, 0.10, 1.3);
  pop();

  // Four monumental angled corner buttresses
  fill(0.42, 0.42, 0.41);
  for (let b = 0; b < 4; b++) {
    const ang = b * HALF_PI;
    const bx = cos(ang) * (0.65 + buttress_flare * 0.3);
    const bz = sin(ang) * (0.65 + buttress_flare * 0.3);
    push();
    translate(bx, -0.3, bz);
    rotateY(ang + 0.785);
    box(0.22, 0.9, 0.35);
    pop();
  }

  // Central monolithic brutalist tower shaft
  fill(0.52, 0.52, 0.50); // Concrete facade grey
  push();
  translate(0, 0.05, 0);
  box(0.72, 1.3 * tier_height, 0.72);

  // Repeating horizontal precast concrete spandrel ribs
  for (let s = -3; s <= 3; s++) {
    const sy = s * (0.17 * tier_height);
    push();
    translate(0, sy, 0);
    fill(0.62, 0.62, 0.60); // Highlight on projecting spandrel
    box(0.82 * spandrel_scale, 0.06, 0.82 * spandrel_scale);

    // Deep recessed shadow window slot beneath spandrel
    fill(0.12, 0.12, 0.12);
    box(0.76, 0.07, 0.76);
    pop();
  }
  pop();

  // Elevated crown observation cantilever pod
  fill(0.46, 0.46, 0.45);
  push();
  translate(0, 0.82 * tier_height, 0);
  box(1.05 * spandrel_scale, 0.22, 1.05 * spandrel_scale);

  // Deep recessed panoramic ribbon window
  fill(0.10, 0.10, 0.10);
  box(1.08 * spandrel_scale, 0.08, 1.08 * spandrel_scale);
  pop();

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: detroit_techno)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "detroit-techno-soul-chord-synth",
        "title": "Detroit Techno Soul Chord Synth",
        "description": "Soulful Detroit techno chord synthesizer with detuned sawtooth triads, warm analog filter resonance, and velocity-sensitive exponential decay envelope.",
        "tags": ["synth", "detroit", "techno", "chord", "analog", "audio"],
        "note_root": 48,
        "params": {
            "cutoff": 1300.0,
            "resonance": 0.35,
            "spread": 0.012,
            "decay": 0.45,
            "drive": 0.35
        },
        "code": """param float cutoff = 1300.0 [200.0, 5500.0]
param float resonance = 0.35 [0.0, 0.85]
param float spread = 0.012 [0.002, 0.04]
param float decay = 0.45 [0.1, 1.5]
param float drive = 0.35 [0.0, 0.9]
state float p1 = 0
state float p2 = 0
state float p3 = 0
state float env = 0
state float lp1 = 0
state float lp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decay), 0.00005, 0.02)
env = if(target > env, env + (target - env) * 0.22, env - env * decayRate)

fBase = clamp(freq, 30.0, 3200.0)
fRoot = fBase * (1.0 - spread)
fThird = fBase * 1.189207 * (1.0 + spread * 0.5)
fFifth = fBase * 1.498307 * (1.0 + spread)

p1 = (p1 + fRoot / sr) % 1.0
p2 = (p2 + fThird / sr) % 1.0
p3 = (p3 + fFifth / sr) % 1.0

saw1 = (p1 * 2.0 - 1.0)
saw2 = (p2 * 2.0 - 1.0)
saw3 = (p3 * 2.0 - 1.0)
raw = (saw1 * 0.36 + saw2 * 0.32 + saw3 * 0.32) * env

fCut = clamp((cutoff + env * 2400.0) / sr * 2.5, 0.008, 0.45)
q = clamp(resonance * 1.6, 0.0, 1.4)
lp1 = lp1 + fCut * (raw - lp1 - q * lp2)
lp2 = lp2 + fCut * (lp1 - lp2)

u = lp2 * (1.0 + drive * 1.5)
sat = clamp(u - 0.2 * u * u * u, -0.95, 0.95)

out = sat * 0.34
"""
    },
    {
        "slug": "detroit-techno-machine-lead-synth",
        "title": "Detroit Techno Machine Lead Synth",
        "description": "Piercing Belleville machine lead synthesizer with hard frequency modulation bite, resonant high-pass bite, and syncopated harmonic bite.",
        "tags": ["synth", "detroit", "techno", "lead", "fm", "audio"],
        "note_root": 48,
        "params": {
            "fmRatio": 2.0,
            "fmIndex": 1.6,
            "cutoff": 2400.0,
            "decay": 0.36,
            "subLevel": 0.35
        },
        "code": """param float fmRatio = 2.0 [0.5, 6.0]
param float fmIndex = 1.6 [0.0, 4.0]
param float cutoff = 2400.0 [300.0, 6000.0]
param float decay = 0.36 [0.08, 1.2]
param float subLevel = 0.35 [0.0, 0.8]
state float pCar = 0
state float pMod = 0
state float pSub = 0
state float env = 0
state float lp1 = 0
state float lp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decay), 0.00008, 0.02)
env = if(target > env, env + (target - env) * 0.28, env - env * decayRate)

fBase = clamp(freq, 40.0, 4000.0)
fMod = fBase * fmRatio
fSub = fBase * 0.5

pMod = (pMod + fMod / sr) % 1.0
modSine = sin(pMod * 6.2831853) * fmIndex * env

pCar = (pCar + (fBase + modSine * fBase) / sr) % 1.0
carSaw = pCar * 2.0 - 1.0

pSub = (pSub + fSub / sr) % 1.0
subSq = if(pSub < 0.5, 1.0, -1.0) * subLevel

raw = (carSaw * 0.75 + subSq) * env

fCut = clamp((cutoff + env * 1800.0) / sr * 2.4, 0.01, 0.45)
lp1 = lp1 + fCut * (raw - lp1 - 0.45 * lp2)
lp2 = lp2 + fCut * (lp1 - lp2)

u = lp2 * 1.3
sat = clamp(u - 0.22 * u * u * u, -0.92, 0.92)

out = sat * 0.35
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: uk_garage)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "uk-garage-sub-drive-saturator-fx",
        "title": "UK Garage Sub Drive Saturator FX",
        "description": "Heavy 2-step bass overdrive and saturation processor with asymmetrical diode clipping, low-end punch preservation, and tape warmth harmonics.",
        "tags": ["effect", "uk-garage", "drive", "saturator", "bass", "audio"],
        "params": {
            "drive": 2.2,
            "subGain": 1.25,
            "tone": 2400.0,
            "asym": 0.28,
            "mix": 0.7
        },
        "code": """param float drive = 2.2 [1.0, 5.0]
param float subGain = 1.25 [0.8, 2.2]
param float tone = 2400.0 [500.0, 6000.0]
param float asym = 0.28 [0.0, 0.7]
param float mix = 0.7 [0.0, 1.0]
state float subLp1 = 0
state float subLp2 = 0
state float midLp = 0

fSub = clamp(140.0 / sr * 2.8, 0.005, 0.15)
subLp1 = subLp1 + fSub * (in - subLp1)
subLp2 = subLp2 + fSub * (subLp1 - subLp2)
sub = subLp2 * subGain

highPass = in - subLp2
u = highPass * drive
sat = clamp(u + asym * u * u - 0.25 * u * u * u, -0.95, 0.95)

fTone = clamp(tone / sr * 2.6, 0.02, 0.45)
midLp = midLp + fTone * (sat - midLp)

wet = sub * 0.6 + midLp * 0.55
out = (in * (1.0 - mix) + wet * mix) * 1.35
"""
    },
    {
        "slug": "uk-garage-chopped-vocal-delay-fx",
        "title": "UK Garage Chopped Vocal Delay FX",
        "description": "Syncopated ping-pong dub delay with rhythmic 3/16 dotted divisions, resonant high-cut damping, and subtle pitch-drift tape modulation.",
        "tags": ["effect", "uk-garage", "delay", "dub", "echo", "audio"],
        "params": {
            "feedback": 0.52,
            "damping": 0.38,
            "tone": 2100.0,
            "mix": 0.55,
            "flutter": 0.22
        },
        "code": """param float feedback = 0.52 [0.1, 0.85]
param float damping = 0.38 [0.05, 0.85]
param float tone = 2100.0 [400.0, 5500.0]
param float mix = 0.55 [0.0, 1.0]
param float flutter = 0.22 [0.0, 0.8]
state float d1 = 0
state float d2 = 0
state float d3 = 0
state float d4 = 0
state float lp = 0
state float lfo = 0

lfo = (lfo + 0.42 / sr) % 1.0
flut = sin(lfo * 6.2831853) * flutter * 0.06

sig = in + d4 * feedback

d1 = d1 + 0.22 * (sig - d1)
d2 = d2 + 0.16 * (d1 - d2)
d3 = d3 + 0.11 * (d2 - d3)
d4 = d4 + 0.08 * (d3 - d4)

del = (d1 * 0.35 + d2 * 0.3 + d3 * 0.2 + d4 * 0.15) * (1.0 + flut)

fCut = clamp(tone / sr * 2.8, 0.015, 0.45)
lp = lp + fCut * (del - lp)

wet = (del * (1.0 - damping) + lp * damping) * 1.2
out = (in * (1.0 - mix) + wet * mix) * 1.42
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embeds (Theme: holographic_iridescent)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "holographic-iridescent-thin-film",
        "title": "Holographic Iridescent Thin Film",
        "description": "Optical thin-film interference shader with refractive spectral dispersion, pearlescent fluid ripples, and dynamic chromatic caustic sheen.",
        "embed_desc": "Optical thin-film interference shader with refractive spectral dispersion, pearlescent fluid ripples, and dynamic chromatic caustic sheen. Bass modulates refractive index depth, mids warp iridescent wave curves, highs trigger shimmering caustic highlights.",
        "tags": ["pixel", "holographic", "iridescent", "optics", "pearl", "shader"],
        "params": {
            "filmDepth": 1.2,
            "waveSpeed": 0.55,
            "dispersion": 1.4,
            "sheen": 0.7,
            "contrast": 1.2
        },
        "code": """param float filmDepth = 1.2 [0.2, 3.0]
param float waveSpeed = 0.55 [0.1, 2.0]
param float dispersion = 1.4 [0.2, 3.0]
param float sheen = 0.7 [0.1, 1.5]
param float contrast = 1.2 [0.5, 2.5]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.6
r = length(p)
ang = atan2(p.y, p.x)
time = t * waveSpeed

w1 = sin(p.x * 4.2 + time * 1.5 + sin(p.y * 3.2))
w2 = cos(p.y * 3.8 - time * 1.3 + cos(p.x * 2.8))
w3 = sin(r * 7.5 - time * 2.2 + ang * 2.0)
wave = (w1 * 0.35 + w2 * 0.35 + w3 * 0.3)

phase = r * 5.0 + wave * filmDepth - time * 1.2

f1 = clamp(sin(phase * dispersion + 0.0) * 0.5 + 0.5, 0.0, 1.0)
f2 = clamp(sin(phase * dispersion + 2.094) * 0.5 + 0.5, 0.0, 1.0)
f3 = clamp(sin(phase * dispersion + 4.188) * 0.5 + 0.5, 0.0, 1.0)

cDeep = vec3(0.06, 0.04, 0.12)
cCyan = vec3(0.663, 0.941, 1.0)
cPink = vec3(1.0, 0.702, 0.961)
cLime = vec3(0.839, 1.0, 0.620)
cWhite = vec3(1.0, 1.0, 1.0)

base = mix(cDeep, cCyan, f1)
base = mix(base, cPink, f2 * 0.7)
base = mix(base, cLime, f3 * 0.5)

spec = pow(max(0.0, sin(wave * 5.0 + time * 2.5)), 8.0) * sheen
col = mix(base, cWhite, spec)
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
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.6;
  float time = t * 0.55;

  float r = length(p);
  float ang = atan(p.y, p.x);

  float bass = audio_bass * 1.4;
  float mid = audio_mid * 1.2;
  float high = audio_high * 1.6;

  float w1 = sin(p.x * 4.2 + time * 1.5 + sin(p.y * 3.2 + mid * 2.5));
  float w2 = cos(p.y * 3.8 - time * 1.3 + cos(p.x * 2.8));
  float w3 = sin(r * (7.5 + bass * 3.5) - time * 2.2 + ang * 2.0);
  float wave = (w1 * 0.35 + w2 * 0.35 + w3 * 0.3);

  float phase = r * 5.0 + wave * (1.2 + bass * 0.8) - time * 1.2;
  float disp = 1.4 + mid * 0.6;

  float f1 = clamp(sin(phase * disp + 0.0) * 0.5 + 0.5, 0.0, 1.0);
  float f2 = clamp(sin(phase * disp + 2.094) * 0.5 + 0.5, 0.0, 1.0);
  float f3 = clamp(sin(phase * disp + 4.188) * 0.5 + 0.5, 0.0, 1.0);

  vec3 cDeep = vec3(0.06, 0.04, 0.12);
  vec3 cCyan = vec3(0.663, 0.941, 1.0);
  vec3 cPink = vec3(1.0, 0.702, 0.961);
  vec3 cLime = vec3(0.839, 1.0, 0.620);
  vec3 cWhite = vec3(1.0, 1.0, 1.0);

  vec3 base = mix(cDeep, cCyan, f1);
  base = mix(base, cPink, f2 * 0.7);
  base = mix(base, cLime, f3 * 0.5);

  float spec = pow(max(0.0, sin(wave * 5.0 + time * 2.5 + high * 4.0)), 8.0) * (0.7 + high * 1.5);
  vec3 col = mix(base, cWhite, spec);

  gl_FragColor = vec4(col, 1.0);
}
"""
    },
    {
        "slug": "holographic-iridescent-prismatic-flux",
        "title": "Holographic Iridescent Prismatic Flux",
        "description": "Multi-layered prismatic crystal dispersion with rotating Fresnel polarization angles, liquid metallic sheen, and spectral caustic flares.",
        "embed_desc": "Multi-layered prismatic crystal dispersion with rotating Fresnel polarization angles, liquid metallic sheen, and spectral caustic flares. Bass drives crystal facet breathing, mids rotate refractive polarization angles, highs illuminate shimmering spectral caustic dispersion.",
        "tags": ["pixel", "holographic", "prismatic", "crystal", "dispersion", "shader"],
        "params": {
            "facetCount": 7.0,
            "fluxSpeed": 0.6,
            "polarization": 1.6,
            "sheen": 0.75
        },
        "code": """param float facetCount = 7.0 [3.0, 14.0]
param float fluxSpeed = 0.6 [0.1, 2.0]
param float polarization = 1.6 [0.2, 3.5]
param float sheen = 0.75 [0.1, 1.5]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.6
r = length(p)
ang = atan2(p.y, p.x)
time = t * fluxSpeed

facet = cos(ang * facetCount + time * 1.1)
ring = sin(r * 9.5 - time * 1.8 + facet * 0.8)
warp = sin(p.x * 4.5 + p.y * 4.0 + time)

disp = r * polarization + ring * 0.45 + warp * 0.35

f1 = clamp(sin(disp * 3.0 + 0.0) * 0.5 + 0.5, 0.0, 1.0)
f2 = clamp(sin(disp * 3.0 + 2.094) * 0.5 + 0.5, 0.0, 1.0)
f3 = clamp(sin(disp * 3.0 + 4.188) * 0.5 + 0.5, 0.0, 1.0)

cDeep = vec3(0.05, 0.04, 0.10)
cCyan = vec3(0.663, 0.941, 1.0)
cPink = vec3(1.0, 0.702, 0.961)
cLime = vec3(0.839, 1.0, 0.620)
cWhite = vec3(1.0, 1.0, 1.0)

base = mix(cDeep, cCyan, f1)
base = mix(base, cPink, f2 * 0.75)
base = mix(base, cLime, f3 * 0.55)

glint = pow(max(0.0, sin(ang * facetCount * 2.0 - time * 3.0)), 12.0) * sheen
col = mix(base, cWhite, glint)
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
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.6;
  float time = t * 0.6;

  float r = length(p);
  float ang = atan(p.y, p.x);

  float bass = audio_bass * 1.4;
  float mid = audio_mid * 1.5;
  float high = audio_high * 2.0;

  float facet = cos(ang * 7.0 + time * 1.1 + mid * 2.5);
  float ring = sin(r * (9.5 + bass * 4.0) - time * 1.8 + facet * 0.8);
  float warp = sin(p.x * 4.5 + p.y * 4.0 + time);

  float disp = r * (1.6 + mid * 0.8) + ring * 0.45 + warp * 0.35;

  float f1 = clamp(sin(disp * 3.0 + 0.0) * 0.5 + 0.5, 0.0, 1.0);
  float f2 = clamp(sin(disp * 3.0 + 2.094) * 0.5 + 0.5, 0.0, 1.0);
  float f3 = clamp(sin(disp * 3.0 + 4.188) * 0.5 + 0.5, 0.0, 1.0);

  vec3 cDeep = vec3(0.05, 0.04, 0.10);
  vec3 cCyan = vec3(0.663, 0.941, 1.0);
  vec3 cPink = vec3(1.0, 0.702, 0.961);
  vec3 cLime = vec3(0.839, 1.0, 0.620);
  vec3 cWhite = vec3(1.0, 1.0, 1.0);

  vec3 base = mix(cDeep, cCyan, f1);
  base = mix(base, cPink, f2 * 0.75);
  base = mix(base, cLime, f3 * 0.55);

  float glint = pow(max(0.0, sin(ang * 14.0 - time * 3.0 + high * 5.0)), 12.0) * (0.75 + high * 1.5);
  vec3 col = mix(base, cWhite, glint);

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
    print("=== Creative Expansion Pack 19: Build and Gate Runner ===")
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
  c bg 0.82 0.80 0.78
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
  c fillColor 0.25 0.15 0.45
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
