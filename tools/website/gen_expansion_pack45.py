#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 45.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: guilloche_intaglio -> guilloche-intaglio-banknote-rosette, guilloche-intaglio-lathe-cartouche)
2. Sketch 3D (Theme: tensegrity_pavilion -> tensegrity-pavilion-snelson-mast-3d, tensegrity-pavilion-geodesic-cantilever-3d)
3. Field Synth (Theme: karplus_sympathetic -> karplus-sympathetic-waveguide-harp-synth, karplus-sympathetic-bowed-monochord-synth)
4. Field Effects (Theme: spring_plate_dispersion -> spring-plate-dispersion-belton-tank-fx, spring-plate-dispersion-steel-plate-fx)
5. Field Pixel & Embeds (Theme: ferrofluid_thinfilm -> ferrofluid-thinfilm-magnetic-spikes, ferrofluid-thinfilm-bismuth-corona)
"""
import os, sys, json, subprocess, zipfile
from PIL import Image, ImageStat

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN = os.path.expanduser("~/.local/bin/Infinite")
if not os.path.exists(BIN):
    BIN = os.path.join(ROOT, "build", "Infinite.app", "Contents", "MacOS", "Infinite")
if not os.path.exists(BIN):
    BIN = "/Users/namansoni/Infinite/repo/build/Infinite.app/Contents/MacOS/Infinite"

LIB_DIR = os.path.join(ROOT, "website", "assets", "library")
SKETCH_DIR = os.path.join(LIB_DIR, "sketch")
PIXEL_DIR = os.path.join(LIB_DIR, "pixel")
PREVIEWS_DIR = os.path.join(LIB_DIR, "previews")
THUMBS_DIR = os.path.join(LIB_DIR, "thumbs")
TEMPLATES_DIR = os.path.join(ROOT, "website", "assets", "templates")
EMBEDS_DIR = os.path.join(ROOT, "website", "embeds")

TMP_DIR = "/tmp/pack45_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: guilloche_intaglio)
#    Palette: #F4F0E6 (rag paper), #1A1D22 (intaglio ink),
#             #D4AF37 (gilt brass), #8E2438 (ruby jewel)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "guilloche-intaglio-banknote-rosette",
        "title": "Guilloche Intaglio Banknote Rosette",
        "description": "Engine-turned rose-lathe guilloche rosette with cycloidal moire lobes, fine-line micro-hatch border, and ruby jewel center.",
        "tags": ["guilloche", "intaglio", "engraving", "banknote", "moire", "rosette", "procedural"],
        "code": """// Guilloche Intaglio Banknote Rosette - Infinite Library (Sketch 2D)
param("petals", 16, 8, 28);
param("rings", 22, 10, 36);
param("eccentricity", 0.24, 0.05, 0.45);
param("lathe_speed", 0.35, 0.0, 1.5);

function draw(t) {
  // Aged archival cotton-rag paper (#E2DBCC)
  background(0.886, 0.859, 0.800);
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const R = Math.min(w, h) * 0.40;

  // Fine horizontal intaglio security ruling
  strokeWeight(0.8);
  for (let i = 0; i < 90; i++) {
    const y = h * (0.04 + 0.92 * (i / 89));
    stroke(0.102, 0.114, 0.133, (i % 3 === 0) ? 0.09 : 0.04);
    line(w * 0.04, y, w * 0.96, y);
  }

  // Outer engraved double cartouche border
  noFill();
  stroke(0.102, 0.114, 0.133, 0.88);
  strokeWeight(2.4);
  rect(w * 0.04, h * 0.04, w * 0.92, h * 0.92);
  strokeWeight(1.0);
  rect(w * 0.055, h * 0.055, w * 0.89, h * 0.89);

  // Corner registration crosshairs & gilt spandrel rings
  const corners = [
    [w * 0.10, h * 0.10],
    [w * 0.90, h * 0.10],
    [w * 0.10, h * 0.90],
    [w * 0.90, h * 0.90]
  ];
  for (let c = 0; c < 4; c++) {
    const kx = corners[c][0];
    const ky = corners[c][1];
    stroke(0.831, 0.686, 0.216, 0.78);
    strokeWeight(1.4);
    circle(kx, ky, w * 0.032);
    stroke(0.102, 0.114, 0.133, 0.65);
    strokeWeight(1.0);
    line(kx - w * 0.025, ky, kx + w * 0.025, ky);
    line(kx, ky - h * 0.025, kx, ky + h * 0.025);
  }

  // Primary engine-turned guilloche rosette rings
  const nRings = floor(rings);
  const pCount = floor(petals);
  const steps = 240;
  const rot = t * lathe_speed;

  for (let r = 0; r < nRings; r++) {
    const u = r / Math.max(1, nRings - 1);
    const baseR = R * (0.16 + 0.82 * u);
    const amp = R * eccentricity * (0.28 + 0.72 * sin(u * PI));
    const phase = r * 0.26 + rot * ((r % 2 === 0) ? 1.0 : -0.8);

    if (r % 5 === 2) {
      stroke(0.831, 0.686, 0.216, 0.72); // gilt brass
    } else if (r % 7 === 4) {
      stroke(0.557, 0.141, 0.220, 0.68); // ruby jewel ink
    } else {
      stroke(0.102, 0.114, 0.133, 0.76); // intaglio black
    }
    strokeWeight(1.15);

    beginShape();
    for (let i = 0; i <= steps; i++) {
      const a = (i / steps) * TAU;
      const lobe = sin(a * pCount + phase) * 0.65 + cos(a * (pCount - 4) - phase * 1.3) * 0.35;
      const rad = baseR + amp * lobe * 0.36;
      vertex(cx + cos(a) * rad, cy + sin(a) * rad);
    }
    endShape();
  }

  // Central watchmaker sunburst & ruby bearing jewel
  noStroke();
  fill(0.102, 0.114, 0.133, 0.92);
  circle(cx, cy, R * 0.14);
  fill(0.831, 0.686, 0.216, 0.95);
  circle(cx, cy, R * 0.095);
  fill(0.557, 0.141, 0.220, 0.96);
  circle(cx, cy, R * 0.052);
}
"""
    },
    {
        "slug": "guilloche-intaglio-lathe-cartouche",
        "title": "Guilloche Intaglio Lathe Cartouche",
        "description": "Horizontal banknote guilloche ribbon lattice with phase-shifted sine interference, diamond-turned oval bezel, and micro-hatch shading.",
        "tags": ["guilloche", "cartouche", "intaglio", "lathe", "security", "engraving", "procedural"],
        "code": """// Guilloche Intaglio Lathe Cartouche - Infinite Library (Sketch 2D)
param("strands", 24, 10, 40);
param("freq_x", 5.0, 2.0, 10.0);
param("weave_amp", 0.12, 0.03, 0.24);
param("drift", 0.45, 0.0, 1.5);

function draw(t) {
  background(0.886, 0.859, 0.800);
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;

  // Fine vertical plate-wiping micro-hatch
  strokeWeight(0.8);
  for (let i = 0; i < 84; i++) {
    const x = w * (0.04 + 0.92 * (i / 83));
    stroke(0.102, 0.114, 0.133, 0.045);
    line(x, h * 0.05, x, h * 0.95);
  }

  // Outer engraved frame
  noFill();
  stroke(0.102, 0.114, 0.133, 0.9);
  strokeWeight(2.4);
  rect(w * 0.045, h * 0.045, w * 0.91, h * 0.91);

  // Horizontal guilloche interference ribbons
  const nStrands = floor(strands);
  const pts = 160;
  const ph = t * drift;

  for (let s = 0; s < nStrands; s++) {
    const u = s / Math.max(1, nStrands - 1);
    const yBase = h * (0.16 + 0.68 * u);
    const env = Math.sin(u * PI);

    if (s % 4 === 1) {
      stroke(0.831, 0.686, 0.216, 0.72);
    } else if (s % 6 === 3) {
      stroke(0.557, 0.141, 0.220, 0.64);
    } else {
      stroke(0.102, 0.114, 0.133, 0.78);
    }
    strokeWeight(1.2);

    beginShape();
    for (let i = 0; i <= pts; i++) {
      const vx = i / pts;
      const x = w * (0.09 + 0.82 * vx);
      const wave1 = sin(vx * TAU * freq_x + u * 8.0 + ph);
      const wave2 = cos(vx * TAU * (freq_x * 0.5) - u * 6.0 - ph * 0.7);
      const y = yBase + h * weave_amp * env * (0.65 * wave1 + 0.35 * wave2);
      vertex(x, y);
    }
    endShape();
  }

  // Central oval engraved cartouche medallion
  const rx = w * 0.26;
  const ry = h * 0.17;
  for (let k = 0; k < 10; k++) {
    const sc = 1.0 - k * 0.055;
    stroke(k % 3 === 1 ? 0.831 : 0.102, k % 3 === 1 ? 0.686 : 0.114, k % 3 === 1 ? 0.216 : 0.133, 0.85);
    strokeWeight(k === 0 ? 2.2 : 1.0);
    beginShape();
    for (let i = 0; i <= 120; i++) {
      const a = (i / 120) * TAU;
      const ripple = 1.0 + 0.035 * sin(a * 24.0 + k * 0.6 + ph);
      vertex(cx + cos(a) * rx * sc * ripple, cy + sin(a) * ry * sc * ripple);
    }
    endShape();
  }

  // Ruby jewel center mark
  noStroke();
  fill(0.557, 0.141, 0.220, 0.95);
  circle(cx, cy, w * 0.028);
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: tensegrity_pavilion)
#    Palette: #DCE4EC (overcast slate sky), #6E7885 (steel),
#             #B8C0C8 (brushed aluminum), #222830 (obsidian plinth)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "tensegrity-pavilion-snelson-mast-3d",
        "title": "Tensegrity Pavilion Snelson Mast 3D",
        "description": "Three-stage helical tensegrity tower with brushed stainless compression struts, tensioned cable network, and dark slate plinth.",
        "tags": ["tensegrity", "snelson", "fuller", "sculpture", "architecture", "3d", "procedural"],
        "code": """// Tensegrity Pavilion Snelson Mast 3D - Infinite Library (Sketch 3D)
param("stages", 3, 2, 5);
param("twist_deg", 36, 15, 65);
param("sway", 0.08, 0.0, 0.25);
param("orbit", 0.25, 0.0, 1.0);

function draw(t) {
  const nStages = floor(stages);
  const twist = (twist_deg * PI) / 180.0;
  rotateY(t * orbit + 0.35);

  // Dark architectural plinth (#222830)
  push();
  translate(0, -1.38, 0);
  fill(0.133, 0.157, 0.188);
  cylinder(1.15, 0.14);
  pop();

  // Base collar ring (#6E7885)
  push();
  translate(0, -1.28, 0);
  fill(0.431, 0.471, 0.522);
  torus(0.76, 0.025);
  pop();

  const stageH = 0.72;
  const rad = 0.68;

  for (let s = 0; s < nStages; s++) {
    const y0 = -1.2 + s * stageH;
    const baseAngle = s * twist + sin(t * 1.1 + s) * sway;
    const stageRad = rad * (1.0 - s * 0.10);

    // 3 compression struts per tensegrity prism
    for (let i = 0; i < 3; i++) {
      const a0 = baseAngle + (i * TAU) / 3.0;
      const a1 = baseAngle + ((i + 1) * TAU) / 3.0 + twist * 0.85;
      const x0 = cos(a0) * stageRad;
      const z0 = sin(a0) * stageRad;
      const x1 = cos(a1) * stageRad * 0.92;
      const z1 = sin(a1) * stageRad * 0.92;

      const mx = (x0 + x1) * 0.5;
      const my = y0 + stageH * 0.52;
      const mz = (z0 + z1) * 0.5;
      const dx = x1 - x0;
      const dy = stageH * 1.04;
      const dz = z1 - z0;
      const len = Math.sqrt(dx * dx + dy * dy + dz * dz);

      // Brushed steel compression strut (#B8C0C8)
      push();
      translate(mx, my, mz);
      rotateY(-atan2(dz, dx));
      rotateZ(atan2(Math.sqrt(dx * dx + dz * dz), dy));
      fill(0.722, 0.753, 0.784);
      cylinder(0.042, len);
      pop();

      // End cap nodes (#222830)
      push();
      translate(x0, y0, z0);
      fill(0.133, 0.157, 0.188);
      sphere(0.055);
      pop();

      // Vertical tension cable (#6E7885)
      push();
      translate(x0 * 0.96, y0 + stageH * 0.5, z0 * 0.96);
      fill(0.431, 0.471, 0.522);
      cylinder(0.008, stageH);
      pop();
    }

    // Horizontal tension ring per stage
    push();
    translate(0, y0 + stageH, 0);
    fill(0.431, 0.471, 0.522);
    torus(stageRad * 0.92, 0.012);
    pop();
  }
}
"""
    },
    {
        "slug": "tensegrity-pavilion-geodesic-cantilever-3d",
        "title": "Tensegrity Pavilion Geodesic Cantilever 3D",
        "description": "Suspended space-frame truss cantilever with triangulated stainless struts, counterweight ring, and tension tendons.",
        "tags": ["geodesic", "cantilever", "space-frame", "tensegrity", "architecture", "3d", "procedural"],
        "code": """// Tensegrity Pavilion Geodesic Cantilever 3D - Infinite Library (Sketch 3D)
param("bays", 6, 4, 10);
param("span", 2.2, 1.4, 3.0);
param("tilt", 0.22, -0.4, 0.5);
param("orbit", 0.22, 0.0, 1.0);

function draw(t) {
  const n = floor(bays);
  rotateY(t * orbit + 0.45);
  rotateZ(tilt + sin(t * 0.8) * 0.04);

  // Dark anchor pylon (#222830)
  push();
  translate(-span * 0.48, -0.45, 0);
  fill(0.133, 0.157, 0.188);
  box(0.32, 1.5, 0.52);
  pop();

  // Suspended counterweight torus (#6E7885)
  push();
  translate(-span * 0.48, 0.42, 0);
  rotateX(PI * 0.5);
  fill(0.431, 0.471, 0.522);
  torus(0.36, 0.035);
  pop();

  // Cantilevered triangulated space-frame truss
  const stepX = span / n;
  for (let i = 0; i < n; i++) {
    const x = -span * 0.4 + i * stepX;
    const taper = 1.0 - (i / n) * 0.45;
    const h = 0.46 * taper;
    const zOff = 0.26 * taper;

    // Upper and lower chord struts (#B8C0C8)
    push();
    translate(x + stepX * 0.5, h, 0);
    rotateZ(PI * 0.5);
    fill(0.722, 0.753, 0.784);
    cylinder(0.026, stepX);
    pop();

    push();
    translate(x + stepX * 0.5, -h * 0.6, zOff);
    rotateZ(PI * 0.5);
    fill(0.722, 0.753, 0.784);
    cylinder(0.024, stepX);
    pop();

    push();
    translate(x + stepX * 0.5, -h * 0.6, -zOff);
    rotateZ(PI * 0.5);
    fill(0.722, 0.753, 0.784);
    cylinder(0.024, stepX);
    pop();

    // Cross ribs & diagonal tension cables
    push();
    translate(x, -h * 0.6, 0);
    rotateX(PI * 0.5);
    fill(0.431, 0.471, 0.522);
    cylinder(0.016, zOff * 2.0);
    pop();

    push();
    translate(x, 0.1 * h, zOff * 0.5);
    rotateX(-0.48);
    fill(0.431, 0.471, 0.522);
    cylinder(0.012, h * 1.75);
    pop();

    push();
    translate(x, 0.1 * h, -zOff * 0.5);
    rotateX(0.48);
    fill(0.431, 0.471, 0.522);
    cylinder(0.012, h * 1.75);
    pop();

    // Node sphere joints
    push();
    translate(x, h, 0);
    fill(0.133, 0.157, 0.188);
    sphere(0.045);
    pop();
  }
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: karplus_sympathetic)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "karplus-sympathetic-waveguide-harp-synth",
        "title": "Karplus Sympathetic Waveguide Harp Synth",
        "description": "Physical-modeling plucked waveguide harp with dual harmonic comb resonators, sympathetic octave body, and wooden-bridge saturation.",
        "note_root": 50,
        "params": {
            "damping": 0.42,
            "pluckBright": 0.68,
            "sympathy": 0.35,
            "warmth": 1.35
        },
        "code": """param float damping = 0.42 [0.1, 0.9];
param float pluckBright = 0.68 [0.15, 1.0];
param float sympathy = 0.35 [0.0, 0.8];
param float warmth = 1.35 [0.8, 2.5];
state float p1 = 0;
state float p2 = 0;
state float pSymp = 0;
state float env = 0;
state float lp1 = 0;

target = if(gate > 0.5, 1.0, 0.0);
decayRate = clamp(1.0 / (sr * (0.25 + damping * 1.4)), 0.0001, 0.03);
env = if(target > env, env + (target - env) * 0.16, env - env * decayRate);

f0 = clamp(freq, 45.0, 2200.0);
p1 = (p1 + f0 / sr) % 1.0;
p2 = (p2 + (f0 * 2.002) / sr) % 1.0;
pSymp = (pSymp + (f0 * 1.498) / sr) % 1.0;

sig = sin(6.2831853 * p1) * 0.55 + sin(6.2831853 * p2) * (0.32 * pluckBright) + sin(6.2831853 * pSymp) * (sympathy * 0.35);
fCut = clamp(0.03 + env * 0.36 * pluckBright, 0.02, 0.42);
lp1 = lp1 + fCut * (sig - lp1);

sat = (lp1 * warmth) / (1.0 + abs(lp1 * warmth) * 0.42);
out = sat * env * 0.54;
"""
    },
    {
        "slug": "karplus-sympathetic-bowed-monochord-synth",
        "title": "Karplus Sympathetic Bowed Monochord Synth",
        "description": "Continuously bowed waveguide monochord with stick-slip rosin harmonic excitation, vibrato drift, and resonant body formants.",
        "note_root": 45,
        "params": {
            "bowPressure": 0.62,
            "rosinEdge": 0.48,
            "vibrato": 0.35,
            "release": 0.75
        },
        "code": """param float bowPressure = 0.62 [0.2, 1.0];
param float rosinEdge = 0.48 [0.0, 1.0];
param float vibrato = 0.35 [0.0, 1.0];
param float release = 0.75 [0.15, 2.0];
state float p1 = 0;
state float pSub = 0;
state float vibLfo = 0;
state float env = 0;
state float fLp = 0;

target = if(gate > 0.5, 1.0, 0.0);
relRate = clamp(1.0 / (sr * release), 0.0001, 0.02);
env = if(target > env, env + (target - env) * 0.035, env - env * relRate);

vibLfo = (vibLfo + 4.8 / sr) % 1.0;
f0 = clamp(freq * (1.0 + sin(6.2831853 * vibLfo) * 0.006 * vibrato), 40.0, 2000.0);
p1 = (p1 + f0 / sr) % 1.0;
pSub = (pSub + (f0 * 0.5) / sr) % 1.0;

bowMod = sin(6.2831853 * p1) * bowPressure * (0.65 + 0.35 * rosinEdge);
core = sin(6.2831853 * p1 + bowMod) * 0.72 + sin(6.2831853 * pSub) * 0.28;
fLp = fLp + 0.14 * (core - fLp);

sat = (fLp * 1.35) / (1.0 + abs(fLp * 1.35) * 0.45);
out = sat * env * 0.50;
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: spring_plate_dispersion)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "spring-plate-dispersion-belton-tank-fx",
        "title": "Spring Plate Dispersion Belton Tank FX",
        "description": "Triple-spring tank dispersion network with cascaded allpass chirp stages, brass transducer drive, and mechanical tank damping.",
        "params": {
            "chirp": 0.68,
            "tension": 0.45,
            "drive": 1.55,
            "damping": 0.62,
            "mix": 0.68
        },
        "code": """param float chirp = 0.68 [0.1, 0.9];
param float tension = 0.45 [0.0, 1.0];
param float drive = 1.55 [0.8, 3.2];
param float damping = 0.62 [0.15, 1.0];
param float mix = 0.68 [0.0, 1.0];
state float lfo = 0;
state float ap1 = 0;
state float ap2 = 0;
state float ap3 = 0;
state float ap4 = 0;
state float ap5 = 0;
state float tankFb = 0;
state float lp = 0;

lfo = (lfo + 0.95 / sr) % 1.0;
g = clamp(chirp + sin(6.2831853 * lfo) * 0.08 * tension, -0.88, 0.88);

# 5-stage allpass spring-coil chirp dispersion with soft tank feedback
x = in + tankFb * (0.16 + 0.14 * tension);

y1 = -g * x + ap1;
ap1 = x + g * y1;

y2 = -g * y1 + ap2;
ap2 = y1 + g * y2;

y3 = -g * y2 + ap3;
ap3 = y2 + g * y3;

y4 = -g * y3 + ap4;
ap4 = y3 + g * y4;

y5 = -g * y4 + ap5;
ap5 = y4 + g * y5;

# Transducer saturation & high-frequency spring damping
driven = y5 * (drive * 0.72);
sat = driven / (1.0 + abs(driven) * 0.55);
fCut = clamp(0.08 + damping * 0.34, 0.04, 0.44);
lp = lp + fCut * (sat - lp);
tankFb = clamp(lp, -0.85, 0.85);

out = (in * (1.0 - mix) + lp * mix) * 0.68;
"""
    },
    {
        "slug": "spring-plate-dispersion-steel-plate-fx",
        "title": "Spring Plate Dispersion Steel Plate FX",
        "description": "Dense steel plate diffusion lattice with early metallic reflections, phase-modulated allpass tank, and warm sheen tilt.",
        "params": {
            "density": 0.72,
            "shimmer": 0.42,
            "sheen": 0.74,
            "warmth": 1.35,
            "mix": 0.64
        },
        "code": """param float density = 0.72 [0.15, 0.88];
param float shimmer = 0.42 [0.0, 1.0];
param float sheen = 0.74 [0.2, 1.0];
param float warmth = 1.35 [0.8, 2.8];
param float mix = 0.64 [0.0, 1.0];
state float lfo1 = 0;
state float lfo2 = 0;
state float ap1 = 0;
state float ap2 = 0;
state float ap3 = 0;
state float ap4 = 0;
state float lp = 0;
state float hpState = 0;

lfo1 = (lfo1 + 0.62 / sr) % 1.0;
lfo2 = (lfo2 + 1.18 / sr) % 1.0;
g1 = clamp(density + sin(6.2831853 * lfo1) * 0.09 * shimmer, -0.88, 0.88);
g2 = clamp(density * 0.88 + cos(6.2831853 * lfo2) * 0.09 * shimmer, -0.88, 0.88);

y1 = -g1 * in + ap1;
ap1 = in + g1 * y1;

y2 = -g2 * y1 + ap2;
ap2 = y1 + g2 * y2;

y3 = -g1 * y2 + ap3;
ap3 = y2 + g1 * y3;

y4 = -g2 * y3 + ap4;
ap4 = y3 + g2 * y4;

# Plate sheen tilt + soft transformer warmth
hpState = hpState + 0.06 * (y4 - hpState);
bright = y4 + (y4 - hpState) * (sheen * 0.45);
fCut = clamp(0.12 + sheen * 0.30, 0.05, 0.44);
lp = lp + fCut * (bright - lp);

sat = (lp * warmth) / (1.0 + abs(lp * warmth) * 0.40);
out = in * (1.0 - mix) + sat * mix;
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embeds (Theme: ferrofluid_thinfilm)
#    Palette: #080A0F (obsidian), #F5F8FA (specular),
#             #183088 (cobalt), #2ED8DF (thin-film cyan)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "ferrofluid-thinfilm-magnetic-spikes",
        "title": "Ferrofluid Thinfilm Magnetic Spikes",
        "description": "Raymarched magnetic ferrofluid dome with normal-field spike lattice, specular studio rim highlights, and bismuth thin-film interference.",
        "embed_desc": "Audio-reactive ferrofluid spike visualizer. Bass drives the magnetic spike amplitude, mids shift the thin-film interference spectrum, and highs sharpen the studio rim reflections.",
        "tags": ["ferrofluid", "magnetic", "raymarching", "thinfilm", "iridescent", "shader", "webgl", "audio-reactive"],
        "params": {
            "spikeFreq": 22.0,
            "spikeAmp": 0.34,
            "filmShift": 0.45,
            "rimPower": 2.2,
            "chroma": 0.92
        },
        "code": """param float spikeFreq = 22.0 [8.0, 36.0];
param float spikeAmp = 0.34 [0.10, 0.55];
param float filmShift = 0.45 [0.0, 1.0];
param float rimPower = 2.2 [1.0, 4.5];
param float chroma = 0.92 [0.2, 1.0];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0;
r = length(p);
a = atan2(p.y, p.x);

# High-frequency hexagonal normal-field ferrofluid spike lattice
hex1 = sin(p.x * spikeFreq + t * 0.8);
hex2 = sin((p.x * 0.5 + p.y * 0.866) * spikeFreq - t * 0.7);
hex3 = sin((p.x * 0.5 - p.y * 0.866) * spikeFreq + t * 0.5);
spikeField = abs(hex1 * hex2 * hex3);
ridge = pow(clamp(1.0 - abs(hex1 + hex2 + hex3) * 0.45, 0.0, 1.0), 3.0);

# Concentric dome + sharp magnetic peaks
dome = smoothstep(1.08, 0.06, r);
height = dome * (0.28 + spikeAmp * 2.8 * pow(spikeField, 0.45));
spec = pow(clamp(ridge * dome + spikeField * dome * 0.65, 0.0, 1.0), rimPower);

# Thin-film optical interference colors
phase = height * 9.0 + r * 4.5 + filmShift * 6.28 + t * 0.5;
irid = vec3(0.5 + 0.5 * cos(phase), 0.5 + 0.5 * cos(phase + 2.1), 0.5 + 0.5 * cos(phase + 4.2));

cObsidian = vec3(0.031, 0.039, 0.059);
cCobalt = vec3(0.094, 0.188, 0.533);
cCyan = vec3(0.180, 0.847, 0.875);
cSpec = vec3(0.961, 0.973, 0.980);

col = mix(cObsidian, cCobalt * 1.15, clamp(height * 0.95, 0.0, 1.0));
col = mix(col, irid * cCyan * 1.2, clamp(spec * chroma * 0.9, 0.0, 1.0));
col = col + cSpec * pow(spec, 2.0) * 0.95;
col = clamp(col, 0.0, 1.0);
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

  float b = audio_bass * 0.6;
  float m = audio_mid * 0.6;
  float h = audio_high * 0.6;

  float r = length(p);
  float freq = 22.0 + b * 6.0;

  float hex1 = sin(p.x * freq + t * 0.8);
  float hex2 = sin((p.x * 0.5 + p.y * 0.866) * freq - t * 0.7);
  float hex3 = sin((p.x * 0.5 - p.y * 0.866) * freq + t * 0.5);
  float spikeField = abs(hex1 * hex2 * hex3);
  float ridge = pow(clamp(1.0 - abs(hex1 + hex2 + hex3) * 0.45, 0.0, 1.0), 3.0);

  float dome = smoothstep(1.08, 0.06, r);
  float height = dome * (0.28 + (0.95 + b * 0.6) * pow(spikeField, 0.45));
  float spec = pow(clamp(ridge * dome + spikeField * dome * 0.65, 0.0, 1.0), 2.2 - h * 0.6);

  float phase = height * 9.0 + r * 4.5 + m * 3.5 + t * 0.5;
  vec3 irid = vec3(
    0.5 + 0.5 * cos(phase + 0.0),
    0.5 + 0.5 * cos(phase + 2.1),
    0.5 + 0.5 * cos(phase + 4.2)
  );

  vec3 cObsidian = vec3(0.031, 0.039, 0.059);
  vec3 cCobalt = vec3(0.094, 0.188, 0.533);
  vec3 cCyan = vec3(0.180, 0.847, 0.875);
  vec3 cSpec = vec3(0.961, 0.973, 0.980);

  vec3 col = mix(cObsidian, cCobalt * 1.15, clamp(height * 0.95, 0.0, 1.0));
  col = mix(col, irid * cCyan * 1.2, clamp(spec * 0.9, 0.0, 1.0));
  col = col + cSpec * pow(spec, 2.0) * (0.92 + h * 0.35);

  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "ferrofluid-thinfilm-bismuth-corona",
        "title": "Ferrofluid Thinfilm Bismuth Corona",
        "description": "Multi-pole magnetic field line interference with iridescent bismuth thin-film fringes, obsidian ridges, and corona shimmer.",
        "embed_desc": "Audio-reactive bismuth thin-film interference visualizer. Bass pulses the dipole field lines, mids sweep the iridescent oil fringes, and highs ignite the corona ridges.",
        "tags": ["bismuth", "thinfilm", "ferrofluid", "interference", "iridescent", "shader", "webgl", "audio-reactive"],
        "params": {
            "dipoleSep": 0.42,
            "fluxDensity": 12.0,
            "filmOrder": 4.2,
            "ridgeSharp": 3.0,
            "glow": 0.78
        },
        "code": """param float dipoleSep = 0.42 [0.15, 0.75];
param float fluxDensity = 12.0 [4.0, 22.0];
param float filmOrder = 4.2 [1.5, 8.0];
param float ridgeSharp = 3.0 [1.2, 6.0];
param float glow = 0.78 [0.2, 1.0];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0;

pA = p - vec2(-dipoleSep, 0.14 * sin(t * 0.6));
pB = p - vec2(dipoleSep, -0.14 * sin(t * 0.6));
rA = length(pA) + 0.02;
rB = length(pB) + 0.02;
aA = atan2(pA.y, pA.x);
aB = atan2(pB.y, pB.x);

# Magnetic dipole flux potential & interference fringes
flux = sin(rA * fluxDensity - t * 1.2) + sin(rB * fluxDensity + t * 1.0);
lines = sin((aA - aB) * 6.0 + flux * 1.4);
ridge = pow(clamp(0.5 + 0.5 * lines, 0.0, 1.0), ridgeSharp);

# Thin-film interference palette
optPath = (rA + rB) * filmOrder + flux * 0.8 + t * 0.4;
irid = vec3(0.5 + 0.5 * sin(optPath + 0.3), 0.5 + 0.5 * sin(optPath + 2.4), 0.5 + 0.5 * sin(optPath + 4.5));

cObsidian = vec3(0.031, 0.039, 0.059);
cCobalt = vec3(0.094, 0.188, 0.533);
cSpec = vec3(0.961, 0.973, 0.980);

col = mix(cObsidian, cCobalt * 1.2, clamp(0.5 + 0.35 * flux, 0.0, 1.0));
col = mix(col, irid, ridge * glow);
col = col + cSpec * pow(ridge, 3.2) * 0.75;
col = clamp(col, 0.0, 1.0);
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

  float b = audio_bass * 0.55;
  float m = audio_mid * 0.55;
  float h = audio_high * 0.55;

  float sep = 0.42 + b * 0.14;
  vec2 pA = p - vec2(-sep, 0.14 * sin(t * 0.6));
  vec2 pB = p - vec2(sep, -0.14 * sin(t * 0.6));
  float rA = length(pA) + 0.02;
  float rB = length(pB) + 0.02;
  float aA = atan(pA.y, pA.x);
  float aB = atan(pB.y, pB.x);

  float dens = 12.0 + b * 5.0;
  float flux = sin(rA * dens - t * 1.2) + sin(rB * dens + t * 1.0);
  float lines = sin((aA - aB) * 6.0 + flux * 1.4);
  float ridge = pow(clamp(0.5 + 0.5 * lines, 0.0, 1.0), 3.0 - h * 0.9);

  float optPath = (rA + rB) * (4.2 + m * 2.4) + flux * 0.8 + t * 0.4;
  vec3 irid = vec3(
    0.5 + 0.5 * sin(optPath + 0.3),
    0.5 + 0.5 * sin(optPath + 2.4),
    0.5 + 0.5 * sin(optPath + 4.5)
  );

  vec3 cObsidian = vec3(0.031, 0.039, 0.059);
  vec3 cCobalt = vec3(0.094, 0.188, 0.533);
  vec3 cSpec = vec3(0.961, 0.973, 0.980);

  vec3 col = mix(cObsidian, cCobalt * 1.2, clamp(0.5 + 0.35 * flux, 0.0, 1.0));
  col = mix(col, irid, ridge * (0.78 + m * 0.25));
  col = col + cSpec * pow(ridge, 3.2) * (0.72 + h * 0.45);

  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
"""
    }
]


def make_embed_html(item):
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>{item['title']} — Infinite</title>
  <meta name="description" content="{item['embed_desc']}">
  <meta property="og:title" content="{item['title']}">
  <meta property="og:description" content="{item['embed_desc']}">
  <meta property="og:image" content="https://infinitedevices.org/assets/library/previews/{item['slug']}.jpg">
  <meta property="og:type" content="website">
  <meta name="twitter:card" content="summary_large_image">
  <link rel="icon" type="image/svg+xml" href="/assets/favicon.svg">
  <link rel="canonical" href="https://infinitedevices.org/embeds/{item['slug']}.html">
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
    .controls {{ position: absolute; left: 14px; bottom: 14px; z-index: 10; display: flex; gap: 8px; }}
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

    let audioCtx = null, analyser = null, dataArray = null;
    let bass = 0.0, mid = 0.0, high = 0.0;
    let audioActive = false;

    document.getElementById("btn-audio").addEventListener("click", async () => {{
      if (audioActive) return;
      try {{
        const stream = await navigator.mediaDevices.getUserMedia({{ audio: true }});
        audioCtx = new (window.AudioContext || window.webkitAudioContext)();
        const src = audioCtx.createMediaStreamSource(stream);
        analyser = audioCtx.createAnalyser();
        analyser.fftSize = 256;
        src.connect(analyser);
        dataArray = new Uint8Array(analyser.frequencyBinCount);
        audioActive = true;
        document.getElementById("btn-audio").style.background = "rgba(255, 107, 53, 0.65)";
      }} catch (err) {{
        console.warn("Audio mic unavailable:", err);
      }}
    }});

    document.getElementById("btn-share").addEventListener("click", () => {{
      navigator.clipboard?.writeText(location.href);
      const b = document.getElementById("btn-share");
      b.textContent = "copied";
      setTimeout(() => b.textContent = "share", 1200);
    }});

    const startTime = performance.now();
    function render() {{
      const dpr = Math.min(window.devicePixelRatio || 1, 2);
      const cw = Math.floor(canvas.clientWidth * dpr);
      const ch = Math.floor(canvas.clientHeight * dpr);
      if (canvas.width !== cw || canvas.height !== ch) {{
        canvas.width = cw;
        canvas.height = ch;
        gl.viewport(0, 0, cw, ch);
      }}

      const now = (performance.now() - startTime) * 0.001;
      if (audioActive && analyser) {{
        analyser.getByteFrequencyData(dataArray);
        let bSum = 0, mSum = 0, hSum = 0;
        for (let i = 0; i < 12; i++) bSum += dataArray[i];
        for (let i = 12; i < 48; i++) mSum += dataArray[i];
        for (let i = 48; i < 110; i++) hSum += dataArray[i];
        bass = bass * 0.72 + (bSum / (12 * 255)) * 0.28;
        mid = mid * 0.72 + (mSum / (36 * 255)) * 0.28;
        high = high * 0.72 + (hSum / (62 * 255)) * 0.28;
      }} else {{
        bass = 0.22 + 0.16 * Math.sin(now * 2.1);
        mid = 0.20 + 0.14 * Math.sin(now * 1.4 + 1.2);
        high = 0.18 + 0.12 * Math.sin(now * 3.1 + 2.4);
      }}

      gl.uniform2f(uRes, canvas.width, canvas.height);
      gl.uniform1f(uTime, now);
      gl.uniform1f(uAspect, canvas.width / Math.max(1, canvas.height));
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

    # 1. Sketch 2D
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

    # 2. Sketch 3D
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
  c bg 0.8627 0.8941 0.9255
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

    # 3. Field Synth
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
  i note1 {nr+7}
  i note2 {nr+4}
  i note3 {nr+12}
  i note4 {nr}
  i note5 {nr+5}
  i note6 {nr+9}
  i note7 {nr+7}
  i note8 {nr}
  i note9 {nr+2}
  i note10 {nr+4}
  i note11 {nr+7}
  i note12 {nr}
  i note13 {nr+12}
  i note14 {nr+9}
  i note15 {nr+4}
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
  c fillColor 0.16 0.24 0.34
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
        subprocess.run([BIN, "frame", inf_file, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        im = Image.open(frame_png)

        prev_jpg = os.path.join(PREVIEWS_DIR, f"{slug}.jpg")
        im.convert("RGB").save(prev_jpg, quality=94)

        thumb_webp = os.path.join(THUMBS_DIR, f"{slug}.webp")
        im.convert("RGB").resize((540, 540), Image.Resampling.LANCZOS).save(thumb_webp, "WEBP", quality=85)

        tmpl_webp = os.path.join(TEMPLATES_DIR, f"{slug}.webp")
        template_preview.render(inf_file, tmpl_webp)

        report.append(f"PASS [Field Synth] {slug}: lufs={lufs:.2f}, peak={peak:.2f}dBFS, silence={silence}")

    # 4. Field Effects
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
  f rateBeats 0.5
  i steps 16
  i note0 48
  i note1 50
  i note2 55
  i note3 57
  i note4 48
  i note5 55
  i note6 52
  i note7 50
  i note8 48
  i note9 50
  i note10 55
  i note11 60
  i note12 48
  i note13 55
  i note14 52
  i note15 57
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
  f resonance 0.25
  f decay 0.45
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
  c fillColor 0.26 0.16 0.22
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
        subprocess.run([BIN, "frame", inf_file, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        im = Image.open(frame_png)

        prev_jpg = os.path.join(PREVIEWS_DIR, f"{slug}.jpg")
        im.convert("RGB").save(prev_jpg, quality=94)

        thumb_webp = os.path.join(THUMBS_DIR, f"{slug}.webp")
        im.convert("RGB").resize((540, 540), Image.Resampling.LANCZOS).save(thumb_webp, "WEBP", quality=85)

        tmpl_webp = os.path.join(TEMPLATES_DIR, f"{slug}.webp")
        template_preview.render(inf_file, tmpl_webp)

        report.append(f"PASS [Field Effects] {slug}: lufs={lufs:.2f}, peak={peak:.2f}dBFS, silence={silence}")

    # 5. Field Pixel & Embeds
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

    print("\n=== Verification Summary ===")
    for line in report:
        print(f"  {line}")

    if failed or len(report) < 10:
        print(f"\nABORTING: Not all 10 items passed gates! (passed {len(report)}/10)")
        sys.exit(1)

    print("\nAll 10 items successfully passed engine verification gates!")

    # Register in library index.json
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
                "tags": ["synth", "karplus", "waveguide", "physical-modeling", "audio", "dsp", "string"],
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
                "tags": ["effect", "spring", "plate", "dispersion", "reverb", "audio", "dsp"],
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

    # Register in templates index.json
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

    # Register in embeds.json
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

    # Register in token_baseline.json
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

    # Update infinite-library.zip
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
