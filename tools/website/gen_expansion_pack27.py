#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 27.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (generative-plotter-curved-flow-lines, generative-plotter-nested-polygon-spirals)
2. Sketch 3D (vector-wireframe-gyroscope-icosahedron-3d, vector-wireframe-topological-hypercube-3d)
3. Field Synth (tech-house-rolling-sub-groove-synth, tech-house-percussive-chord-stab-synth)
4. Field Effects (dub-techno-tape-delay-diffusion-fx, dub-techno-resonant-space-reverb-fx)
5. Field Pixel (cymatics-sine-nodal-frequency-matrix, cymatics-sine-harmonic-quadrupole-field)
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

TMP_DIR = "/tmp/pack27_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: generative_plotter)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "generative-plotter-curved-flow-lines",
        "title": "Generative Plotter Curved Flow Lines",
        "description": "Algorithmic pen plotter study of continuous vector streamlines deflected through a trigonometric vector force field in carbon ink with vermilion accents on archival paper.",
        "tags": ["generative", "plotter", "flowfield", "lines", "mathematical", "drawing"],
        "code": """// Generative Plotter Curved Flow Lines - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Algorithmic pen plotter study of continuous vector streamlines deflected through a trigonometric vector force field in carbon ink with vermilion accents on archival paper.
param("density", 36.0, 16.0, 64.0);
param("curvature", 1.35, 0.5, 2.5);
param("speed", 0.35, 0.1, 1.2);
param("line_width", 2.2, 1.0, 4.0);

function draw(t) {
  // Archival tinted cream paper ground (#e8e0d0)
  background(0.89, 0.86, 0.80);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;

  push();
  translate(cx, cy);

  // Precision mechanical framing boundary (#111111)
  stroke(0.067, 0.067, 0.067);
  strokeWeight(2.6);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  const time = t * speed;
  const count = floor(density);

  // 1. Carbon ink vector streamlines
  for (let i = 0; i < count; i++) {
    const norm = (i / (count - 1)) * 2.0 - 1.0;
    const y0 = norm * span * 0.44;

    const isAccent = (i % 6 === 0);
    if (isAccent) {
      stroke(0.820, 0.188, 0.122, 0.95); // Vermilion red (#d1301f)
      strokeWeight(line_width * 1.5);
    } else {
      stroke(0.067, 0.067, 0.067, 0.88); // Carbon black (#111111)
      strokeWeight(line_width * 0.9);
    }
    noFill();

    beginShape();
    const steps = 44;
    for (let s = 0; s <= steps; s++) {
      const u = (s / steps) * 2.0 - 1.0;
      const x = u * span * 0.44;
      const angle = sin(u * 2.5 * curvature + time * 1.4) * cos(norm * 3.0 + time * 0.8);
      const warp = sin(angle * Math.PI) * (span * 0.08 * curvature);
      const y = y0 + warp;
      vertex(x, y);
    }
    endShape();
  }

  // 2. Central vermilion plotter focus rosette
  push();
  rotate(time * 0.5);
  stroke(0.820, 0.188, 0.122, 0.92);
  strokeWeight(2.2);
  noFill();
  for (let r = 0; r < 8; r++) {
    const ang = (r / 8.0) * Math.PI * 2.0;
    push();
    rotate(ang);
    line(0, span * 0.08, 0, span * 0.22);
    circle(0, span * 0.24, 7.0);
    pop();
  }
  pop();

  // 3. Precision registration tick marks
  stroke(0.067, 0.067, 0.067);
  strokeWeight(1.6);
  for (let k = -8; k <= 8; k++) {
    const p = (k / 8.0) * (span * 0.44);
    line(p, -span * 0.48, p, -span * 0.46);
    line(p, span * 0.46, p, span * 0.48);
    line(-span * 0.48, p, -span * 0.46, p);
    line(span * 0.46, p, span * 0.48, p);
  }

  pop();
}
"""
    },
    {
        "slug": "generative-plotter-nested-polygon-spirals",
        "title": "Generative Plotter Nested Polygon Spirals",
        "description": "Mechanical plotter drawing featuring concentric nested polygon spirals, progressive angular rotation offsets, and precision vermilion geometric crosshairs on warm cream parchment.",
        "tags": ["generative", "plotter", "polygons", "spiral", "algorithmic", "drawing"],
        "code": """// Generative Plotter Nested Polygon Spirals - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Mechanical plotter drawing featuring concentric nested polygon spirals, progressive angular rotation offsets, and precision vermilion geometric crosshairs on warm cream parchment.
param("rings", 22.0, 10.0, 40.0);
param("sides", 6.0, 3.0, 10.0);
param("rotation_delta", 0.08, 0.02, 0.25);
param("speed", 0.35, 0.1, 1.2);

function draw(t) {
  // Warm cream archival parchment (#ede5d2)
  background(0.88, 0.85, 0.79);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;

  push();
  translate(cx, cy);

  // Precision caliper outer frame (#111111)
  stroke(0.067, 0.067, 0.067);
  strokeWeight(2.6);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  const time = t * speed;
  const ringCount = floor(rings);
  const nSides = floor(sides);

  // 1. Concentric nested polygons with angular progression
  for (let r = ringCount; r >= 1; r--) {
    const fr = r / ringCount;
    const rad = fr * span * 0.44;
    const angOffset = r * rotation_delta + time * 0.4;

    push();
    rotate(angOffset);

    const isVermilion = (r % 5 === 0);
    if (isVermilion) {
      stroke(0.820, 0.188, 0.122, 0.95); // Vermilion red (#d1301f)
      strokeWeight(2.4);
    } else {
      stroke(0.067, 0.067, 0.067, 0.85); // Carbon ink (#111111)
      strokeWeight(1.8);
    }
    noFill();

    beginShape();
    for (let s = 0; s <= nSides; s++) {
      const a = (s / nSides) * Math.PI * 2.0;
      vertex(cos(a) * rad, sin(a) * rad);
    }
    endShape();
    pop();
  }

  // 2. Vermilion precision axial crosshairs
  stroke(0.820, 0.188, 0.122, 0.9);
  strokeWeight(2.2);
  line(-span * 0.46, 0, span * 0.46, 0);
  line(0, -span * 0.46, 0, span * 0.46);

  // 3. Central solid carbon ink core with vermilion registration ring
  fill(0.067, 0.067, 0.067);
  noStroke();
  circle(0, 0, span * 0.08);

  stroke(0.820, 0.188, 0.122);
  strokeWeight(2.4);
  noFill();
  circle(0, 0, span * 0.13);

  // 4. Corner mechanical registration brackets
  stroke(0.067, 0.067, 0.067);
  strokeWeight(2.0);
  const bracket = span * 0.06;
  const d = span * 0.48;
  // Top-left
  line(-d, -d + bracket, -d, -d);
  line(-d, -d, -d + bracket, -d);
  // Top-right
  line(d - bracket, -d, d, -d);
  line(d, -d, d, -d + bracket);
  // Bottom-left
  line(-d, d - bracket, -d, d);
  line(-d, d, -d + bracket, d);
  // Bottom-right
  line(d - bracket, d, d, d);
  line(d, d, d, d - bracket);

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: vector_wireframe)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "vector-wireframe-gyroscope-icosahedron-3d",
        "title": "Vector Wireframe Gyroscope Icosahedron 3D",
        "description": "Gimballed 3D vector wireframe gyroscope with concentric rotating orbital rings, nested icosahedral geometric cages, and high-luminance phosphor green vector beams.",
        "tags": ["vector", "wireframe", "gyroscope", "icosahedron", "geometry", "3d"],
        "code": """// Vector Wireframe Gyroscope Icosahedron 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Gimballed 3D vector wireframe gyroscope with concentric rotating orbital rings, nested icosahedral geometric cages, and high-luminance phosphor green vector beams.
param("rings", 6.0, 3.0, 12.0);
param("spin", 0.45, 0.1, 1.2);
param("scale", 1.15, 0.5, 2.0);
param("cage_density", 8.0, 4.0, 16.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.48 + sin(time * 0.2) * 0.06);
  rotateY(time * 0.35 + 0.5);

  // Deep CRT vector monitor plinth (#08140c)
  push();
  translate(0, 1.15, 0);
  fill(0.031, 0.078, 0.047);
  box(2.4 * scale, 0.14, 2.4 * scale);
  pop();

  // 1. Concentric rotating gimbal rings
  const ringCount = floor(rings);
  for (let r = 0; r < ringCount; r++) {
    const rad = (0.7 + r * 0.16) * scale;
    const ang = r * Math.PI / ringCount;
    push();
    rotateY(time * (0.3 + r * 0.1) + ang);
    rotateX(ang * 0.6);

    const segments = 24;
    for (let s = 0; s < segments; s++) {
      const theta = (s / segments) * Math.PI * 2.0;
      push();
      translate(cos(theta) * rad, 0, sin(theta) * rad);
      rotateY(-theta);
      // Phosphor green beam segment (#00ff6a)
      fill(0.0, 1.0, 0.416);
      box(0.04 * scale, 0.04 * scale, (2.0 * Math.PI * rad / segments) * 0.95);
      pop();
    }
    pop();
  }

  // 2. Central rotating nested geometric vector cage
  push();
  rotateY(-time * 0.6);
  rotateZ(time * 0.4);

  // Central white phosphor core junction (#ffffff)
  fill(1.0, 1.0, 1.0);
  box(0.35 * scale, 0.35 * scale, 0.35 * scale);

  // Radiating icosahedral vector cage struts
  const cageCount = floor(cage_density);
  for (let c = 0; c < cageCount; c++) {
    const phi = (c / cageCount) * Math.PI * 2.0;
    push();
    rotateY(phi);
    rotateX(0.55);

    // Green vector strut
    fill(0.0, 1.0, 0.416);
    translate(0, 0.35 * scale, 0);
    box(0.06 * scale, 0.7 * scale, 0.06 * scale);

    // White apex vector pip
    translate(0, 0.38 * scale, 0);
    fill(1.0, 1.0, 1.0);
    box(0.12 * scale, 0.12 * scale, 0.12 * scale);
    pop();
  }
  pop();

  // 3. Axial phosphor calibration antennas
  for (let a = -1; a <= 1; a += 2) {
    push();
    translate(0, a * 0.95 * scale, 0);
    fill(0.0, 1.0, 0.416);
    box(0.05, 0.35 * scale, 0.05);
    pop();
  }

  pop();
}
"""
    },
    {
        "slug": "vector-wireframe-topological-hypercube-3d",
        "title": "Vector Wireframe Topological Hypercube 3D",
        "description": "Four-dimensional tesseract hypercube projection rotating in 3D vector space with inner and outer concentric vertex lattices, phosphor green connection struts, and bright beam junctions.",
        "tags": ["vector", "wireframe", "hypercube", "tesseract", "topology", "3d"],
        "code": """// Vector Wireframe Topological Hypercube 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Four-dimensional tesseract hypercube projection rotating in 3D vector space with inner and outer concentric vertex lattices, phosphor green connection struts, and bright beam junctions.
param("tesseract_scale", 1.15, 0.5, 2.0);
param("rotation_speed", 0.42, 0.1, 1.2);
param("strut_thickness", 0.05, 0.02, 0.12);
param("inner_ratio", 0.52, 0.3, 0.8);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.42 + sin(time * 0.18) * 0.05);
  rotateY(time * 0.32 + 0.7);

  // Deep CRT vector phosphor plinth (#06120a)
  push();
  translate(0, 1.15, 0);
  fill(0.024, 0.071, 0.039);
  box(2.3 * tesseract_scale, 0.14, 2.3 * tesseract_scale);
  pop();

  const outerSize = 0.95 * tesseract_scale;
  const innerSize = outerSize * inner_ratio;

  // 1. Inner rotating tesseract core (phosphor green with bright white vertices)
  push();
  rotateY(-time * 0.5);
  rotateX(time * 0.35);

  // Inner cube vertices
  for (let x = -1; x <= 1; x += 2) {
    for (let y = -1; y <= 1; y += 2) {
      for (let z = -1; z <= 1; z += 2) {
        push();
        translate(x * innerSize, y * innerSize, z * innerSize);
        fill(1.0, 1.0, 1.0); // Bright beam vertex
        box(0.12 * tesseract_scale, 0.12 * tesseract_scale, 0.12 * tesseract_scale);
        pop();
      }
    }
  }

  // Inner cube struts
  fill(0.0, 1.0, 0.416); // Phosphor green
  for (let x = -1; x <= 1; x += 2) {
    for (let y = -1; y <= 1; y += 2) {
      push();
      translate(x * innerSize, y * innerSize, 0);
      box(strut_thickness * 1.2, strut_thickness * 1.2, innerSize * 2.0);
      pop();
      push();
      translate(x * innerSize, 0, y * innerSize);
      box(strut_thickness * 1.2, innerSize * 2.0, strut_thickness * 1.2);
      pop();
      push();
      translate(0, x * innerSize, y * innerSize);
      box(innerSize * 2.0, strut_thickness * 1.2, strut_thickness * 1.2);
      pop();
    }
  }
  pop();

  // 2. Outer tesseract boundary frame
  push();
  rotateY(time * 0.25);

  // Outer cube struts
  fill(0.0, 0.85, 0.35);
  for (let x = -1; x <= 1; x += 2) {
    for (let y = -1; y <= 1; y += 2) {
      push();
      translate(x * outerSize, y * outerSize, 0);
      box(strut_thickness, strut_thickness, outerSize * 2.0);
      pop();
      push();
      translate(x * outerSize, 0, y * outerSize);
      box(strut_thickness, outerSize * 2.0, strut_thickness);
      pop();
      push();
      translate(0, x * outerSize, y * outerSize);
      box(outerSize * 2.0, strut_thickness, strut_thickness);
      pop();
    }
  }

  // Outer corner vertex nodes
  for (let x = -1; x <= 1; x += 2) {
    for (let y = -1; y <= 1; y += 2) {
      for (let z = -1; z <= 1; z += 2) {
        push();
        translate(x * outerSize, y * outerSize, z * outerSize);
        fill(0.7, 1.0, 0.85);
        box(0.14 * tesseract_scale, 0.14 * tesseract_scale, 0.14 * tesseract_scale);
        pop();
      }
    }
  }

  // 3. Diagonal 4D projection interconnects (from inner to outer vertices)
  fill(0.0, 1.0, 0.416);
  for (let x = -1; x <= 1; x += 2) {
    for (let y = -1; y <= 1; y += 2) {
      for (let z = -1; z <= 1; z += 2) {
        const midX = (x * outerSize + x * innerSize) * 0.5;
        const midY = (y * outerSize + y * innerSize) * 0.5;
        const midZ = (z * outerSize + z * innerSize) * 0.5;
        push();
        translate(midX, midY, midZ);
        box(strut_thickness * 0.9, strut_thickness * 0.9, (outerSize - innerSize) * 1.4);
        pop();
      }
    }
  }

  pop();
  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: tech_house)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "tech-house-rolling-sub-groove-synth",
        "title": "Tech House Rolling Sub Groove Synth",
        "description": "Punchy rolling tech house sub bass synth with snappy pitch envelope decay, warm saturated sine-triangle core, and tight low-pass cutoff filter.",
        "tags": ["synth", "tech-house", "bass", "sub", "rolling", "audio"],
        "note_root": 43,
        "params": {
            "cutoff": 0.52,
            "resonance": 0.40,
            "subGain": 0.48,
            "decayTime": 0.22,
            "drive": 0.30
        },
        "code": """param float cutoff = 0.52 [0.1, 1.0]
param float resonance = 0.40 [0.0, 0.85]
param float subGain = 0.48 [0.0, 1.0]
param float decayTime = 0.22 [0.05, 0.8]
param float drive = 0.30 [0.0, 1.0]
state float p1 = 0
state float pSub = 0
state float env = 0
state float lp1 = 0
state float lp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decayTime), 0.0001, 0.04)
env = if(target > env, env + (target - env) * 0.70, env - env * decayRate)

fBase = clamp(freq, 35.0, 350.0)

dp1 = fBase / sr
p1 = p1 + dp1
p1 = if(p1 >= 1.0, p1 - 1.0, p1)
saw1 = p1 * 2.0 - 1.0

dpSub = (fBase * 0.5) / sr
pSub = pSub + dpSub
pSub = if(pSub >= 1.0, pSub - 1.0, pSub)
subSine = sin(pSub * 6.2831853)

raw = saw1 * 0.55 + subSine * subGain

drv = raw * (1.0 + drive * 1.5)
sat = (drv / (1.0 + abs(drv))) * env

fCut = clamp(cutoff * (0.10 + env * 0.45), 0.015, 0.44)
lp1 = lp1 + fCut * (sat + lp1 * resonance * 0.45 - lp1)
lp2 = lp2 + fCut * (lp1 - lp2)

out = lp2 * 0.52
"""
    },
    {
        "slug": "tech-house-percussive-chord-stab-synth",
        "title": "Tech House Percussive Chord Stab Synth",
        "description": "Snappy tech house chord stab synthesizer with multi-oscillator minor triad tuning, crisp resonant bandpass envelope filter, and punchy transient drive.",
        "tags": ["synth", "tech-house", "stab", "chord", "percussive", "audio"],
        "note_root": 55,
        "params": {
            "cutoff": 0.62,
            "resonance": 0.35,
            "chordDecay": 0.18,
            "detune": 0.25,
            "drive": 0.35
        },
        "code": """param float cutoff = 0.62 [0.1, 1.0]
param float resonance = 0.35 [0.0, 0.85]
param float chordDecay = 0.18 [0.04, 0.8]
param float detune = 0.25 [0.0, 1.0]
param float drive = 0.35 [0.0, 1.0]
state float pRoot = 0
state float pThird = 0
state float pFifth = 0
state float env = 0
state float lp1 = 0
state float lp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * chordDecay), 0.0001, 0.05)
env = if(target > env, env + (target - env) * 0.85, env - env * decayRate)

fBase = clamp(freq, 110.0, 900.0)

dpR = fBase / sr
pRoot = pRoot + dpR
pRoot = if(pRoot >= 1.0, pRoot - 1.0, pRoot)
sR = pRoot * 2.0 - 1.0

dp3 = (fBase * (1.1892 + detune * 0.004)) / sr
pThird = pThird + dp3
pThird = if(pThird >= 1.0, pThird - 1.0, pThird)
s3 = pThird * 2.0 - 1.0

dp5 = (fBase * (1.4983 - detune * 0.004)) / sr
pFifth = pFifth + dp5
pFifth = if(pFifth >= 1.0, pFifth - 1.0, pFifth)
s5 = pFifth * 2.0 - 1.0

raw = (sR * 0.5 + s3 * 0.42 + s5 * 0.38)

drv = raw * (1.0 + drive * 1.6)
sat = (drv / (1.0 + abs(drv))) * env

fCut = clamp(cutoff * (0.08 + env * 0.60), 0.02, 0.45)
lp1 = lp1 + fCut * (sat + lp1 * resonance * 0.45 - lp1)
lp2 = lp2 + fCut * (lp1 - lp2)

out = lp2 * 0.48
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: dub_techno)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "dub-techno-tape-delay-diffusion-fx",
        "title": "Dub Techno Tape Delay Diffusion FX",
        "description": "Endless dub techno tape delay with multi-tap cross-feedback loops, high-frequency tape damping, warm analog saturation, and deep spatial diffusion.",
        "tags": ["effect", "dub-techno", "delay", "tape", "diffusion", "audio"],
        "params": {
            "delayTime": 0.28,
            "feedback": 0.58,
            "damping": 2600.0,
            "drive": 0.35,
            "mix": 0.55
        },
        "code": """param float delayTime = 0.28 [0.05, 0.8]
param float feedback = 0.58 [0.0, 0.9]
param float damping = 2600.0 [600.0, 8000.0]
param float drive = 0.35 [0.0, 1.0]
param float mix = 0.55 [0.0, 1.0]
state float d1 = 0
state float d2 = 0
state float lp = 0

fDamp = clamp(damping / sr * 2.0, 0.02, 0.44)
lp = lp + fDamp * (in - lp)

rate1 = clamp(1.0 / (sr * delayTime), 0.02, 0.35)
rate2 = clamp(1.0 / (sr * delayTime * 1.48), 0.015, 0.30)

d1 = d1 + rate1 * (lp + d2 * feedback * 0.85 - d1)
d2 = d2 + rate2 * (d1 * feedback * 0.90 - d2)

wet = (d1 * 0.65 + d2 * 0.55)
drv = wet * (1.0 + drive * 1.5)
sat = clamp(drv / (1.0 + abs(drv * 0.6)), -0.85, 0.85)

processed = in * (1.0 - mix) + sat * mix
out = processed * 0.85
"""
    },
    {
        "slug": "dub-techno-resonant-space-reverb-fx",
        "title": "Dub Techno Resonant Space Reverb FX",
        "description": "Expansive dub techno resonant space wash with interlocking comb filter delays, lush high-frequency diffusion, and warm atmospheric tail resonance.",
        "tags": ["effect", "dub-techno", "reverb", "space", "resonance", "audio"],
        "params": {
            "roomSize": 0.55,
            "decayTime": 0.42,
            "damping": 3200.0,
            "resonance": 0.38,
            "mix": 0.50
        },
        "code": """param float roomSize = 0.55 [0.1, 0.9]
param float decayTime = 0.42 [0.1, 0.8]
param float damping = 3200.0 [800.0, 8000.0]
param float resonance = 0.38 [0.0, 0.8]
param float mix = 0.50 [0.0, 1.0]
state float c1 = 0
state float c2 = 0
state float c3 = 0
state float lp = 0

fDamp = clamp(damping / sr * 2.0, 0.03, 0.45)
lp = lp + fDamp * (in - lp)

fb = clamp(roomSize * 0.85, 0.1, 0.85)
dRate = clamp(decayTime * 0.35, 0.04, 0.35)

c1 = c1 + dRate * 1.05 * (lp + c3 * fb * 0.5 - c1)
c2 = c2 + dRate * 0.78 * (c1 * fb * 0.75 - c2)
c3 = c3 + dRate * 0.54 * (c2 * fb * 0.70 - c3)

resBand = (c1 - c2) * resonance * 1.8
wet = (c1 * 0.4 + c2 * 0.35 + c3 * 0.3 + resBand)
sat = clamp(wet / (1.0 + abs(wet * 0.5)), -0.85, 0.85)

processed = in * (1.0 - mix) + sat * mix
out = processed * 1.15
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embeds (Theme: cymatics_sine)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "cymatics-sine-nodal-frequency-matrix",
        "title": "Cymatics Sine Nodal Frequency Matrix",
        "description": "Audio-reactive Chladni standing wave resonance matrix with high-frequency nodal lines, modal harmonic interference, and radiant electric cyan wave crests.",
        "embed_desc": "Audio-reactive Chladni cymatics visualization modeling two-dimensional acoustic plate vibrations. Bass frequencies flex modal vibration antinodes, mid frequencies modulate secondary harmonic nodal geometry, and high frequencies ignite luminous white standing-wave grain boundaries over deep oceanic obsidian.",
        "tags": ["pixel", "cymatics", "chladni", "standingwave", "shader", "audioreactive"],
        "params": {
            "frequency": 5.0,
            "speed": 0.45,
            "modalScale": 1.35,
            "contrast": 1.15,
            "resonance": 0.65
        },
        "code": """param float frequency = 5.0 [2.0, 16.0]
param float speed = 0.45 [0.1, 1.5]
param float modalScale = 1.35 [0.5, 2.5]
param float contrast = 1.15 [0.6, 2.0]
param float resonance = 0.65 [0.1, 1.2]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2
time = t * speed

r = length(p)

n = frequency
m = frequency * 1.6

k1 = cos(p.x * n * modalScale + time * 1.5) * cos(p.y * m * modalScale - time * 0.8)
k2 = cos(p.x * m * modalScale - time * 1.2) * cos(p.y * n * modalScale + time * 1.1)
chladni = abs(k1 - k2)

nodalMask = 1.0 - smoothstep(0.04, 0.22, chladni)
ripple = sin(r * 22.0 - time * 3.5) * 0.08

radialVibe = cos(r * 12.0 - time * 2.0) * resonance * 0.2

cVoid = vec3(0.015, 0.031, 0.063)
cCyan = vec3(0.490, 0.976, 1.0)
cWhite = vec3(1.0, 1.0, 1.0)

col = mix(cVoid, cCyan, clamp(nodalMask * 0.85 + ripple + radialVibe, 0.0, 1.0))
col = mix(col, cWhite, clamp(nodalMask * nodalMask, 0.0, 1.0))

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

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  float r = length(p);

  float n = 5.0 + m * 3.0;
  float modalM = (5.0 + m * 3.0) * 1.6;

  float k1 = cos(p.x * n * (1.35 + b * 0.25) + time * 1.5) * cos(p.y * modalM * 1.35 - time * 0.8);
  float k2 = cos(p.x * modalM * 1.35 - time * 1.2) * cos(p.y * n * 1.35 + time * 1.1);
  float chladni = abs(k1 - k2);

  float nodalMask = 1.0 - smoothstep(0.04, 0.22 + b * 0.08, chladni);
  float ripple = sin(r * 22.0 - time * 3.5) * (0.08 + b * 0.05);

  float radialVibe = cos(r * 12.0 - time * 2.0) * (0.65 + h * 0.35) * 0.2;

  vec3 cVoid = vec3(0.015, 0.031, 0.063);
  vec3 cCyan = vec3(0.490, 0.976, 1.0);
  vec3 cWhite = vec3(1.0, 1.0, 1.0);

  vec3 col = mix(cVoid, cCyan, clamp(nodalMask * (0.85 + h * 0.15) + ripple + radialVibe, 0.0, 1.0));
  col = mix(col, cWhite, clamp(nodalMask * nodalMask * (1.0 + h * 0.4), 0.0, 1.0));

  gl_FragColor = vec4(clamp(col * 1.15, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "cymatics-sine-harmonic-quadrupole-field",
        "title": "Cymatics Sine Harmonic Quadrupole Field",
        "description": "Acoustic quadrupole standing wave field visualizing polar interference lobes, acoustic levitation nodes, and concentric harmonic resonance ripples.",
        "embed_desc": "Audio-reactive acoustic quadrupole field simulation featuring rotating quadrupolar wave interference lobes and concentric acoustic trap nodes. Bass pulses quadrupole amplitude and radial antinodes, mids modulate multipole harmonic deformation, and highs spark electric cyan phase boundaries.",
        "tags": ["pixel", "cymatics", "quadrupole", "acoustic", "shader", "audioreactive"],
        "params": {
            "speed": 0.40,
            "acousticScale": 1.25,
            "contrast": 1.12,
            "harmonicDecay": 0.85
        },
        "code": """param float speed = 0.40 [0.1, 1.5]
param float acousticScale = 1.25 [0.5, 2.0]
param float contrast = 1.12 [0.6, 2.0]
param float harmonicDecay = 0.85 [0.2, 1.5]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2
time = t * speed

r = length(p)
rSafe = max(r, 0.001)

quad = (p.x * p.x - p.y * p.y) / (rSafe * rSafe)
wave = sin(r * 14.0 * acousticScale - time * 2.6)
quadLobe = quad * wave

harmonicRing = cos(r * 24.0 * acousticScale + time * 1.6) * 0.5 + 0.5

trap = abs(quadLobe) * exp(-r * harmonicDecay)
nodalLobe = 1.0 - smoothstep(0.05, 0.28, trap)

cVoid = vec3(0.020, 0.035, 0.071)
cDeepBlue = vec3(0.08, 0.16, 0.35)
cCyan = vec3(0.490, 0.976, 1.0)
cWhite = vec3(0.96, 0.98, 1.0)

base = mix(cVoid, cDeepBlue, clamp(harmonicRing * 0.6, 0.0, 1.0))
col = mix(base, cCyan, clamp(trap * 1.5, 0.0, 1.0))
col = mix(col, cWhite, clamp(nodalLobe * (1.0 - smoothstep(0.0, 0.7, r)), 0.0, 1.0))

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
  float rSafe = max(r, 0.001);

  float quad = (p.x * p.x - p.y * p.y) / (rSafe * rSafe);
  float wave = sin(r * (14.0 + m * 4.0) * 1.25 - time * 2.6);
  float quadLobe = quad * wave * (1.0 + b * 0.4);

  float harmonicRing = cos(r * 24.0 * 1.25 + time * 1.6) * 0.5 + 0.5;

  float trap = abs(quadLobe) * exp(-r * 0.85);
  float nodalLobe = 1.0 - smoothstep(0.05, 0.28, trap);

  vec3 cVoid = vec3(0.020, 0.035, 0.071);
  vec3 cDeepBlue = vec3(0.08, 0.16, 0.35);
  vec3 cCyan = vec3(0.490, 0.976, 1.0);
  vec3 cWhite = vec3(0.96, 0.98, 1.0);

  vec3 base = mix(cVoid, cDeepBlue, clamp(harmonicRing * 0.6, 0.0, 1.0));
  vec3 col = mix(base, cCyan, clamp(trap * (1.5 + h * 0.3), 0.0, 1.0));
  col = mix(col, cWhite, clamp(nodalLobe * (1.0 - smoothstep(0.0, 0.7, r)) * (1.0 + h * 0.5), 0.0, 1.0));

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
    print("=== Creative Expansion Pack 27: Build and Gate Runner ===")
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
  c bg 0.020 0.039 0.024
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
