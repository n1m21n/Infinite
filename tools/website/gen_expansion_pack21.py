#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 21.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (thundr-brutalist-hazard-crosshair, thundr-brutalist-technical-slab)
2. Sketch 3D (synthwave-grid-perspective-horizon-3d, synthwave-grid-neon-monolith-3d)
3. Field Synth (industrial-techno-warehouse-rumble-synth, industrial-techno-metallic-fm-synth)
4. Field Effects (classic-trance-euphoric-delay-fx, classic-trance-resonant-sweep-fx)
5. Field Pixel (cyberpunk-neon-rain-reflections, cyberpunk-neon-hologram-grid)
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

TMP_DIR = "/tmp/pack21_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: thundr_brutalist)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "thundr-brutalist-hazard-crosshair",
        "title": "Thundr Brutalist Hazard Crosshair",
        "description": "Postmodern brutalist technical poster with high-contrast diagonal hazard stripes, bold black monolithic slabs, neon warning accents, and precision reticle crosshairs.",
        "tags": ["brutalist", "poster", "hazard", "typography", "technical", "drawing"],
        "code": """// Thundr Brutalist Hazard Crosshair - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Postmodern brutalist technical poster with high-contrast diagonal hazard stripes, bold black monolithic slabs, neon warning accents, and precision reticle crosshairs.
param("zoom", 1.0, 0.5, 2.0);
param("line_weight", 2.2, 1.0, 6.0);
param("stripe_count", 12.0, 6.0, 24.0);
param("slab_width", 1.0, 0.6, 1.8);

function draw(t) {
  background(0.07, 0.07, 0.08);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88 * zoom;

  push();
  translate(cx, cy);

  // Outer framing boundary
  noFill();
  stroke(1.0, 0.8, 0.0); // Hazard yellow
  strokeWeight(line_weight * 1.5);
  rect(-span * 0.5, -span * 0.5, span, span);

  // Inner margin outline
  stroke(0.28, 0.28, 0.32);
  strokeWeight(1.0);
  rect(-span * 0.47, -span * 0.47, span * 0.94, span * 0.94);

  // Top heavy black monolithic header slab
  fill(0.12, 0.12, 0.14);
  noStroke();
  rect(-span * 0.45, -span * 0.45, span * 0.9, span * 0.18);

  // Signal red accent blocks
  fill(1.0, 0.0, 0.2); // Signal Red
  rect(-span * 0.45, -span * 0.45, span * 0.06, span * 0.18);
  rect(-span * 0.36, -span * 0.42, span * 0.38 * slab_width, span * 0.04);
  rect(-span * 0.36, -span * 0.35, span * 0.55 * slab_width, span * 0.05);

  // High-contrast diagonal hazard stripe block
  push();
  translate(0, span * 0.04);
  const hW = span * 0.85;
  const hH = span * 0.22;
  fill(0.04, 0.04, 0.05);
  rect(-hW * 0.5, -hH * 0.5, hW, hH);

  // Diagonal stripes in hazard yellow
  const count = floor(stripe_count);
  const step = hW / count;
  for (let i = -count; i <= count * 2; i++) {
    const x0 = -hW * 0.5 + i * step;
    fill(1.0, 0.8, 0.0);
    noStroke();
    beginShape();
    vertex(max(-hW * 0.5, min(hW * 0.5, x0)), -hH * 0.5);
    vertex(max(-hW * 0.5, min(hW * 0.5, x0 + step * 0.5)), -hH * 0.5);
    vertex(max(-hW * 0.5, min(hW * 0.5, x0 + step * 0.5 - hH)), hH * 0.5);
    vertex(max(-hW * 0.5, min(hW * 0.5, x0 - hH)), hH * 0.5);
    endShape();
  }

  // Border around hazard block
  noFill();
  stroke(1.0, 0.8, 0.0);
  strokeWeight(line_weight);
  rect(-hW * 0.5, -hH * 0.5, hW, hH);
  pop();

  // Central precision reticle crosshair target
  const rOut = span * 0.14;
  noFill();
  stroke(0.95, 0.95, 0.98); // Stark white
  strokeWeight(line_weight);
  circle(0, span * 0.04, rOut * 2.0);
  circle(0, span * 0.04, rOut * 0.8);

  fill(1.0, 0.0, 0.2);
  circle(0, span * 0.04, rOut * 0.25);

  stroke(1.0, 0.8, 0.0);
  line(-rOut * 1.5, span * 0.04, rOut * 1.5, span * 0.04);
  line(0, span * 0.04 - rOut * 1.5, 0, span * 0.04 + rOut * 1.5);

  // Bottom constructivist rectangular data panels
  fill(0.92, 0.92, 0.95);
  noStroke();
  rect(-span * 0.45, span * 0.24, span * 0.42 * slab_width, span * 0.18);

  fill(0.12, 0.12, 0.14);
  rect(-span * 0.42, span * 0.27, span * 0.36 * slab_width, span * 0.05);
  rect(-span * 0.42, span * 0.34, span * 0.24 * slab_width, span * 0.04);

  fill(1.0, 0.8, 0.0);
  rect(span * 0.02, span * 0.24, span * 0.43, span * 0.18);

  fill(0.08, 0.08, 0.09);
  rect(span * 0.06, span * 0.27, span * 0.35, span * 0.12);

  // Technical edge measurement calibration ticks
  stroke(0.95, 0.95, 0.98);
  strokeWeight(1.5);
  for (let d = -span * 0.45; d <= span * 0.45; d += span * 0.05) {
    line(d, -span * 0.5, d, -span * 0.5 + 10);
    line(d, span * 0.5 - 10, d, span * 0.5);
    line(-span * 0.5, d, -span * 0.5 + 10, d);
    line(span * 0.5 - 10, d, span * 0.5, d);
  }

  pop();
}
"""
    },
    {
        "slug": "thundr-brutalist-technical-slab",
        "title": "Thundr Brutalist Technical Slab",
        "description": "Asymmetric constructivist techwear layout with segmented monolithic bars, stark warning red callouts, dense coordinate matrix rules, and calibrated technical markings.",
        "tags": ["brutalist", "techwear", "constructivist", "grid", "geometric", "drawing"],
        "code": """// Thundr Brutalist Technical Slab - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Asymmetric constructivist techwear layout with segmented monolithic bars, stark warning red callouts, dense coordinate matrix rules, and calibrated technical markings.
param("zoom", 1.0, 0.5, 2.0);
param("line_weight", 2.2, 1.0, 6.0);
param("grid_cols", 10.0, 4.0, 20.0);
param("slab_offset", 0.15, 0.0, 0.4);

function draw(t) {
  background(0.065, 0.065, 0.075);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88 * zoom;

  push();
  translate(cx, cy);

  // Outer framing boundary
  noFill();
  stroke(0.92, 0.92, 0.95); // White frame
  strokeWeight(line_weight * 1.5);
  rect(-span * 0.5, -span * 0.5, span, span);

  // Background coordinate grid lines
  stroke(0.22, 0.22, 0.26);
  strokeWeight(1.0);
  const cols = floor(grid_cols);
  const step = span / cols;
  for (let i = -span * 0.5; i <= span * 0.5; i += step) {
    line(i, -span * 0.5, i, span * 0.5);
    line(-span * 0.5, i, span * 0.5, i);
  }

  // Heavy left monolithic black slab with warning yellow stripe
  fill(0.14, 0.14, 0.16);
  noStroke();
  rect(-span * 0.46, -span * 0.44, span * 0.32, span * 0.88);

  fill(1.0, 0.8, 0.0); // Hazard Yellow
  rect(-span * 0.46, -span * 0.44, span * 0.04, span * 0.88);

  // White data segments inside left slab
  fill(0.95, 0.95, 0.98);
  for (let k = 0; k < 6; k++) {
    const yk = -span * 0.36 + k * span * 0.12;
    rect(-span * 0.39, yk, span * 0.22, span * 0.035);
  }

  // Dominant cantilevered center slab in signal red
  fill(1.0, 0.0, 0.2); // Signal Red
  rect(-span * 0.10, -span * 0.32 + slab_offset * span, span * 0.54, span * 0.26);

  fill(0.07, 0.07, 0.08);
  rect(-span * 0.06, -span * 0.28 + slab_offset * span, span * 0.46, span * 0.08);

  fill(0.95, 0.95, 0.98);
  rect(-span * 0.06, -span * 0.16 + slab_offset * span, span * 0.30, span * 0.04);
  rect(span * 0.28, -span * 0.16 + slab_offset * span, span * 0.12, span * 0.04);

  // Bottom right technical grid box with diagonal cross-hatch
  push();
  translate(span * 0.17, span * 0.18);
  const bW = span * 0.27;
  const bH = span * 0.24;

  fill(0.10, 0.10, 0.12);
  stroke(1.0, 0.8, 0.0);
  strokeWeight(line_weight);
  rect(-bW * 0.5, -bH * 0.5, bW, bH);

  stroke(1.0, 0.8, 0.0, 0.7);
  strokeWeight(1.2);
  for (let x = -bW * 0.5; x <= bW * 0.5; x += 14) {
    line(x, -bH * 0.5, x + 16, bH * 0.5);
  }
  pop();

  // Registration crosshairs & corner brackets
  stroke(0.95, 0.95, 0.98);
  strokeWeight(line_weight);
  noFill();

  // Corner marks
  const cOff = span * 0.46;
  const cLen = 16;
  line(-cOff, -cOff, -cOff + cLen, -cOff);
  line(-cOff, -cOff, -cOff, -cOff + cLen);
  line(cOff, -cOff, cOff - cLen, -cOff);
  line(cOff, -cOff, cOff, -cOff + cLen);
  line(-cOff, cOff, -cOff + cLen, cOff);
  line(-cOff, cOff, -cOff, cOff - cLen);
  line(cOff, cOff, cOff - cLen, cOff);
  line(cOff, cOff, cOff, cOff - cLen);

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: synthwave_grid)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "synthwave-grid-perspective-horizon-3d",
        "title": "Synthwave Grid Perspective Horizon 3D",
        "description": "Retro-futuristic 3D perspective grid terrain with segmented glowing sun discs, neon geometric pylons, wireframe horizon mesh, and floating platonic octahedra.",
        "tags": ["synthwave", "grid", "retro", "horizon", "wireframe", "3d"],
        "code": """// Synthwave Grid Perspective Horizon 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Retro-futuristic 3D perspective grid terrain with segmented glowing sun discs, neon geometric pylons, wireframe horizon mesh, and floating platonic octahedra.
param("road_width", 1.0, 0.5, 2.0);
param("sun_scale", 1.1, 0.5, 2.0);
param("rotation_speed", 0.35, 0.1, 1.2);
param("pylon_height", 1.0, 0.5, 2.0);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.42 + sin(time * 0.2) * 0.04);
  rotateY(time * 0.28 + 0.35);

  // Receding ground grid floor slabs
  fill(0.12, 0.04, 0.22); // Deep midnight violet
  push();
  translate(0, -0.85, 0);
  box(2.2 * road_width, 0.08, 2.2);
  pop();

  // Grid transversal road slats (perspective stripes)
  const slats = 9;
  for (let s = 0; s < slats; s++) {
    const z = -0.9 + (s / (slats - 1)) * 1.8;
    fill(0.02, 0.85, 0.91); // Electric cyan
    push();
    translate(0, -0.80, z);
    box(1.8 * road_width, 0.03, 0.06);
    pop();
  }

  // Flanking neon highway light pylons along the road
  const pylons = 5;
  for (let p = 0; p < pylons; p++) {
    const z = -0.8 + (p / (pylons - 1)) * 1.6;
    const xL = -0.95 * road_width;
    const xR = 0.95 * road_width;

    // Left pylon
    fill(0.20, 0.08, 0.35);
    push();
    translate(xL, -0.4 * pylon_height, z);
    box(0.08, 0.8 * pylon_height, 0.08);
    // Neon beacon top
    fill(1.0, 0.165, 0.427); // Hot Magenta
    translate(0, 0.45 * pylon_height, 0);
    box(0.14, 0.10, 0.14);
    pop();

    // Right pylon
    fill(0.20, 0.08, 0.35);
    push();
    translate(xR, -0.4 * pylon_height, z);
    box(0.08, 0.8 * pylon_height, 0.08);
    // Neon beacon top
    fill(0.02, 0.85, 0.91); // Electric Cyan
    translate(0, 0.45 * pylon_height, 0);
    box(0.14, 0.10, 0.14);
    pop();
  }

  // Horizon tiered sunset disc
  push();
  translate(0, 0.35, -1.0);
  fill(1.0, 0.827, 0.098); // Amber yellow
  sphere(0.48 * sun_scale, 16, 16);

  fill(1.0, 0.165, 0.427); // Magenta lower ring
  translate(0, -0.22 * sun_scale, 0);
  box(1.2 * sun_scale, 0.06, 0.2);
  translate(0, -0.12 * sun_scale, 0);
  box(1.0 * sun_scale, 0.05, 0.2);
  pop();

  // Floating central rotating diamond crystal
  fill(0.02, 0.85, 0.91); // Cyan
  push();
  translate(0, 0.05 + sin(time * 1.5) * 0.08, 0);
  rotateY(time * 1.2);
  rotateZ(0.785398);
  box(0.26, 0.26, 0.26);

  fill(1.0, 0.827, 0.098); // Amber inner core
  box(0.12, 0.12, 0.12);
  pop();

  pop();
}
"""
    },
    {
        "slug": "synthwave-grid-neon-monolith-3d",
        "title": "Synthwave Grid Neon Monolith 3D",
        "description": "Low-poly synthwave cyber-temple with fluted neon obelisks, stepped luminous plinths, floating concentric rings, and vibrant chrome retro-futuristic volumes.",
        "tags": ["synthwave", "monolith", "obelisk", "cyberpunk", "retro", "3d"],
        "code": """// Synthwave Grid Neon Monolith 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Low-poly synthwave cyber-temple with fluted neon obelisks, stepped luminous plinths, floating concentric rings, and vibrant chrome retro-futuristic volumes.
param("monolith_scale", 1.0, 0.5, 2.0);
param("height_scale", 1.1, 0.6, 2.0);
param("rotation_speed", 0.35, 0.1, 1.2);
param("orbit_radius", 0.65, 0.3, 1.2);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.40 + sin(time * 0.2) * 0.04);
  rotateY(time * 0.32 + 0.48);

  // Stepped podium base plinth
  fill(0.14, 0.05, 0.25);
  push();
  translate(0, -0.88, 0);
  box(1.8 * monolith_scale, 0.12, 1.8 * monolith_scale);
  translate(0, 0.10, 0);
  fill(0.22, 0.08, 0.38);
  box(1.5 * monolith_scale, 0.10, 1.5 * monolith_scale);
  translate(0, 0.09, 0);
  fill(0.02, 0.85, 0.91); // Cyan trim
  box(1.25 * monolith_scale, 0.06, 1.25 * monolith_scale);
  pop();

  // Central monolith tower
  push();
  translate(0, -0.05, 0);

  // Dark obsidian monolith core
  fill(0.10, 0.04, 0.18);
  box(0.38 * monolith_scale, 1.2 * height_scale, 0.38 * monolith_scale);

  // Monolith neon side ribs
  fill(1.0, 0.165, 0.427); // Magenta ribs
  box(0.42 * monolith_scale, 1.1 * height_scale, 0.06);
  box(0.06, 1.1 * height_scale, 0.42 * monolith_scale);

  // Monolith apex capstone
  fill(1.0, 0.827, 0.098); // Luminous Amber Capstone
  translate(0, 0.68 * height_scale, 0);
  box(0.26 * monolith_scale, 0.16, 0.26 * monolith_scale);
  pop();

  // 4 Corner neon obelisks
  for (let c = 0; c < 4; c++) {
    const ang = c * 1.5707963 + 0.785398;
    const px = cos(ang) * 0.68 * monolith_scale;
    const pz = sin(ang) * 0.68 * monolith_scale;

    push();
    translate(px, -0.25, pz);
    fill(0.20, 0.08, 0.35);
    box(0.12, 0.85 * height_scale, 0.12);

    fill(0.02, 0.85, 0.91); // Cyan beacon
    translate(0, 0.45 * height_scale, 0);
    box(0.16, 0.08, 0.16);
    pop();
  }

  // Floating orbiting neon satellites
  const sCount = 3;
  for (let s = 0; s < sCount; s++) {
    const sAng = time * 1.4 + (s * 6.2831853) / sCount;
    const sx = cos(sAng) * orbit_radius;
    const sz = sin(sAng) * orbit_radius;
    const sy = 0.25 + sin(time * 2.0 + s) * 0.12;

    fill(s % 2 === 0 ? 1.0 : 0.02, s % 2 === 0 ? 0.165 : 0.85, s % 2 === 0 ? 0.427 : 0.91);
    push();
    translate(sx, sy, sz);
    rotateY(time * 2.0);
    box(0.10, 0.10, 0.10);
    pop();
  }

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: industrial_techno)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "industrial-techno-warehouse-rumble-synth",
        "title": "Industrial Techno Warehouse Rumble Synth",
        "description": "Heavily saturated industrial warehouse techno kick-rumble synthesizer with pitch-decay transient thump, wavefolded low-end resonance, and asymmetric drive.",
        "tags": ["synth", "industrial-techno", "kick", "rumble", "distortion", "audio"],
        "note_root": 36,
        "params": {
            "modRatio": 1.0,
            "modIndex": 1.6,
            "cutoff": 1800.0,
            "decay": 0.42,
            "drive": 1.8
        },
        "code": """param float modRatio = 1.0 [0.5, 4.0]
param float modIndex = 1.6 [0.0, 4.0]
param float cutoff = 1800.0 [200.0, 5000.0]
param float decay = 0.42 [0.1, 1.5]
param float drive = 1.8 [0.5, 3.5]
state float pKick = 0
state float pRumble = 0
state float env = 0
state float pEnv = 0
state float lp1 = 0
state float lp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decay), 0.00008, 0.02)
env = if(target > env, env + (target - env) * 0.35, env - env * decayRate)
pEnv = if(target > pEnv, pEnv + (target - pEnv) * 0.5, pEnv - pEnv * 0.0035)

fBase = clamp(freq, 32.0, 800.0)
fPitch = fBase * (1.0 + pEnv * 2.8)

pKick = (pKick + fPitch / sr) % 1.0
kickSine = sin(pKick * 6.2831853)

pRumble = (pRumble + (fBase * modRatio) / sr) % 1.0
rumbleSaw = (pRumble * 2.0 - 1.0)

raw = (kickSine * 0.8 + rumbleSaw * 0.4) * env

u = raw * drive
sat = clamp(u - 0.28 * u * u * u, -0.95, 0.95)

fCut = clamp((cutoff + env * 1400.0) / sr * 2.2, 0.01, 0.45)
lp1 = lp1 + fCut * (sat - lp1 - 0.3 * lp2)
lp2 = lp2 + fCut * (lp1 - lp2)

out = lp2 * 0.38
"""
    },
    {
        "slug": "industrial-techno-metallic-fm-synth",
        "title": "Industrial Techno Metallic FM Synth",
        "description": "Raw industrial FM percussion synthesizer with metallic inharmonic operator modulation, snappy noise-burst transient, and gritty wavefolded decay.",
        "tags": ["synth", "industrial-techno", "fm", "metallic", "percussion", "audio"],
        "note_root": 48,
        "params": {
            "modRatio": 2.76,
            "modIndex": 2.2,
            "cutoff": 2400.0,
            "decay": 0.32,
            "punch": 0.65
        },
        "code": """param float modRatio = 2.76 [1.0, 6.0]
param float modIndex = 2.2 [0.0, 5.0]
param float cutoff = 2400.0 [400.0, 6000.0]
param float decay = 0.32 [0.08, 1.2]
param float punch = 0.65 [0.0, 1.0]
state float pCar = 0
state float pMod = 0
state float env = 0
state float pEnv = 0
state float lp1 = 0
state float lp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decay), 0.00009, 0.02)
env = if(target > env, env + (target - env) * 0.38, env - env * decayRate)
pEnv = if(target > pEnv, pEnv + (target - pEnv) * 0.6, pEnv - pEnv * 0.005)

fBase = clamp(freq, 40.0, 2200.0)
fMod = fBase * modRatio

pMod = (pMod + fMod / sr) % 1.0
modSine = sin(pMod * 6.2831853) * modIndex * (env + pEnv * punch)

fCar = fBase + modSine * fBase
pCar = (pCar + fCar / sr) % 1.0
carSine = sin(pCar * 6.2831853)

raw = carSine * env

u = raw * 1.5
sat = clamp(u - 0.22 * u * u * u, -0.92, 0.92)

fCut = clamp((cutoff + env * 1800.0) / sr * 2.2, 0.015, 0.45)
lp1 = lp1 + fCut * (sat - lp1 - 0.25 * lp2)
lp2 = lp2 + fCut * (lp1 - lp2)

out = lp2 * 0.36
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: classic_trance)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "classic-trance-euphoric-delay-fx",
        "title": "Classic Trance Euphoric Delay FX",
        "description": "Anthemic trance stereo delay line with synchronized 16th-note repeats, progressive high-frequency damping, gentle flutter modulation, and lush stereo width.",
        "tags": ["effect", "classic-trance", "delay", "echo", "euphoric", "audio"],
        "params": {
            "feedback": 0.58,
            "damping": 0.45,
            "tone": 2800.0,
            "mix": 0.55,
            "flutter": 0.22
        },
        "code": """param float feedback = 0.58 [0.1, 0.88]
param float damping = 0.45 [0.05, 0.9]
param float tone = 2800.0 [500.0, 6000.0]
param float mix = 0.55 [0.0, 1.0]
param float flutter = 0.22 [0.0, 0.8]
state float d1 = 0
state float d2 = 0
state float d3 = 0
state float d4 = 0
state float lp = 0
state float lfo = 0

lfo = (lfo + 0.35 / sr) % 1.0
flut = sin(lfo * 6.2831853) * flutter * 0.04

sig = in + d4 * feedback

d1 = d1 + 0.26 * (sig - d1)
d2 = d2 + 0.20 * (d1 - d2)
d3 = d3 + 0.14 * (d2 - d3)
d4 = d4 + 0.10 * (d3 - d4)

del = (d1 * 0.34 + d2 * 0.30 + d3 * 0.22 + d4 * 0.14) * (1.0 + flut)

fCut = clamp(tone / sr * 2.6, 0.015, 0.45)
lp = lp + fCut * (del - lp)

wet = (del * (1.0 - damping) + lp * damping) * 1.25
out = (in * (1.0 - mix) + wet * mix) * 1.32
"""
    },
    {
        "slug": "classic-trance-resonant-sweep-fx",
        "title": "Classic Trance Resonant Sweep FX",
        "description": "Euphoric trance resonant state-variable filter and harmonic exciter with modulated peak sweep, warm analog saturation, and dynamic stereo presence.",
        "tags": ["effect", "classic-trance", "filter", "sweep", "resonance", "audio"],
        "params": {
            "centerFreq": 1600.0,
            "resonance": 0.48,
            "sweepDepth": 0.65,
            "sweepRate": 0.28,
            "mix": 0.65
        },
        "code": """param float centerFreq = 1600.0 [300.0, 4500.0]
param float resonance = 0.48 [0.1, 0.88]
param float sweepDepth = 0.65 [0.0, 1.0]
param float sweepRate = 0.28 [0.05, 1.5]
param float mix = 0.65 [0.0, 1.0]
state float lp1 = 0
state float lp2 = 0
state float lfo = 0

lfo = (lfo + sweepRate / sr) % 1.0
mod = (sin(lfo * 6.2831853) * 0.5 + 0.5) * sweepDepth

fMod = centerFreq * (1.0 + mod * 1.8)
fCut = clamp(fMod / sr * 2.4, 0.012, 0.45)

lp1 = lp1 + fCut * (in - lp1 - resonance * lp2)
lp2 = lp2 + fCut * (lp1 - lp2)

u = lp2 * 1.45
sat = clamp(u - 0.22 * u * u * u, -0.95, 0.95)

out = (in * (1.0 - mix) + sat * mix) * 1.32
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embeds (Theme: cyberpunk_neon)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "cyberpunk-neon-rain-reflections",
        "title": "Cyberpunk Neon Rain Reflections",
        "description": "Wet asphalt neon reflection shader with vertical rain streak refraction, glowing magenta-cyan billboard bloom, and dynamic chromatic aberration.",
        "embed_desc": "Wet asphalt neon reflection shader with vertical rain streak refraction, glowing magenta-cyan billboard bloom, and dynamic chromatic aberration. Bass pulses ground-level wet reflection waves, mids shift holographic sign distortions, highs spark luminous rain droplet flares.",
        "tags": ["pixel", "cyberpunk", "neon", "rain", "reflections", "shader"],
        "params": {
            "rainSpeed": 1.2,
            "puddleRipple": 0.85,
            "neonIntensity": 1.4,
            "chromaticOffset": 0.06,
            "scanlineDarkness": 0.3
        },
        "code": """param float rainSpeed = 1.2 [0.2, 3.0]
param float puddleRipple = 0.85 [0.1, 2.0]
param float neonIntensity = 1.4 [0.5, 3.0]
param float chromaticOffset = 0.06 [0.01, 0.2]
param float scanlineDarkness = 0.3 [0.0, 0.8]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2
time = t * 0.8

rain = sin(p.x * 32.0 + time * 14.0 * rainSpeed) * 0.05
puddle = sin(length(p) * 14.0 - time * 3.0) * 0.08 * puddleRipple
pDist = vec2(p.x + rain * (p.y + 1.0), p.y + puddle)

beamM = clamp(sin(pDist.x * 4.0 - time * 1.2) * 0.5 + 0.5, 0.0, 1.0)
beamC = clamp(cos(pDist.x * 4.0 + time * 1.5) * 0.5 + 0.5, 0.0, 1.0)
bloom = clamp(1.0 - length(pDist) * 0.55, 0.0, 1.0)

ground = clamp((0.1 - p.y) * 2.0, 0.0, 1.0)

cBg = vec3(0.04, 0.01, 0.08)
cMagenta = vec3(1.0, 0.05, 0.56)
cCyan = vec3(0.0, 0.94, 1.0)
cYellow = vec3(0.99, 0.93, 0.04)

col = cBg + cMagenta * beamM * 0.65 * neonIntensity + cCyan * beamC * 0.65 * neonIntensity
col = col + cYellow * bloom * ground * 0.45

scan = clamp(sin(uv.y * 520.0) * 0.5 + 0.5, 0.0, 1.0)
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
  float time = t * 0.8;

  float bass = audio_bass * 1.5;
  float mid = audio_mid * 1.3;
  float high = audio_high * 1.8;

  float rainSpeed = 1.2 + mid * 0.8;
  float rain = sin(p.x * 32.0 + time * 14.0 * rainSpeed) * (0.05 + high * 0.08);
  float puddle = sin(length(p) * 14.0 - time * 3.0 + bass * 4.0) * (0.08 + bass * 0.12);
  vec2 pDist = vec2(p.x + rain * (p.y + 1.0), p.y + puddle);

  float beamM = clamp(sin(pDist.x * (4.0 + bass * 2.0) - time * 1.2) * 0.5 + 0.5, 0.0, 1.0);
  float beamC = clamp(cos(pDist.x * 4.0 + time * 1.5 + mid * 2.0) * 0.5 + 0.5, 0.0, 1.0);
  float bloom = clamp(1.0 - length(pDist) * 0.55, 0.0, 1.0);

  float ground = clamp((0.1 - p.y) * 2.0, 0.0, 1.0);

  vec3 cBg = vec3(0.04, 0.01, 0.08);
  vec3 cMagenta = vec3(1.0, 0.05, 0.56);
  vec3 cCyan = vec3(0.0, 0.94, 1.0);
  vec3 cYellow = vec3(0.99, 0.93, 0.04);

  float neonIntensity = 1.4 + bass * 0.8;
  vec3 col = cBg + cMagenta * beamM * 0.65 * neonIntensity + cCyan * beamC * 0.65 * neonIntensity;
  col += cYellow * bloom * ground * (0.45 + mid * 0.4);

  float scan = clamp(sin(uv.y * 520.0) * 0.5 + 0.5, 0.0, 1.0);
  col = col * (1.0 - scan * 0.3);

  float glint = pow(max(0.0, sin(pDist.x * 80.0 + time * 20.0)), 16.0) * high * 1.6;
  col += vec3(glint * 0.8, glint * 0.9, glint);

  gl_FragColor = vec4(col, 1.0);
}
"""
    },
    {
        "slug": "cyberpunk-neon-hologram-grid",
        "title": "Cyberpunk Neon Hologram Grid",
        "description": "Volumetric neon city hologram shader with segmented cyberpunk signage matrices, phosphor scanline bleeding, and radiant chromatic light corridors.",
        "embed_desc": "Volumetric neon city hologram shader with segmented cyberpunk signage matrices, phosphor scanline bleeding, and radiant chromatic light corridors. Bass triggers volumetric hologram grid surges, mids warp matrix column alignment, highs trigger phosphor beam luminance spikes.",
        "tags": ["pixel", "cyberpunk", "hologram", "matrix", "neon", "shader"],
        "params": {
            "gridDensity": 18.0,
            "glitchSpeed": 1.1,
            "bloomSpread": 1.2,
            "phosphorGlow": 0.75,
            "scanlineDarkness": 0.35
        },
        "code": """param float gridDensity = 18.0 [8.0, 36.0]
param float glitchSpeed = 1.1 [0.2, 3.0]
param float bloomSpread = 1.2 [0.4, 2.5]
param float phosphorGlow = 0.75 [0.1, 1.8]
param float scanlineDarkness = 0.35 [0.0, 0.8]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.4
time = t * 0.75

gridP = floor(p * gridDensity) / gridDensity
wave = sin(gridP.y * 10.0 + time * 2.5 * glitchSpeed)

gate = sin(gridP.x * 12.0 + floor(time * 6.0))
disp = if(gate > 0.4, 0.14, 0.0)
pG = vec2(gridP.x + disp, gridP.y + wave * 0.06)

r = length(pG)
ang = atan2(pG.y, pG.x)

f1 = clamp(sin(r * 12.0 - time * 2.2 + ang * 2.0) * 0.5 + 0.5, 0.0, 1.0)
f2 = clamp(cos(pG.x * 8.0 + pG.y * 6.0 + time) * 0.5 + 0.5, 0.0, 1.0)
f3 = clamp(sin(r * 20.0 - time * 3.5) * 0.5 + 0.5, 0.0, 1.0)

cDark = vec3(0.03, 0.01, 0.07)
cMagenta = vec3(1.0, 0.02, 0.55)
cCyan = vec3(0.0, 0.95, 0.98)
cYellow = vec3(1.0, 0.90, 0.08)

base = mix(cDark, cMagenta, f1 * 0.75)
base = mix(base, cCyan, f2 * 0.65)
base = mix(base, cYellow, f3 * 0.45 * phosphorGlow)

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
  float time = t * 0.75;

  float bass = audio_bass * 1.4;
  float mid = audio_mid * 1.5;
  float high = audio_high * 1.8;

  float density = 18.0 - bass * 6.0;
  vec2 gridP = floor(p * density) / density;

  float wave = sin(gridP.y * 10.0 + time * 2.5 + mid * 3.0);
  float disp = 0.0;
  if (sin(gridP.x * 12.0 + floor(time * 6.0)) > 0.35) {
    disp = 0.14 * (1.0 + bass);
  }
  vec2 pG = gridP + vec2(disp, wave * 0.06);

  float r = length(pG);
  float ang = atan(pG.y, pG.x);

  float f1 = clamp(sin(r * 12.0 - time * 2.2 + ang * 2.0 + mid * 2.0) * 0.5 + 0.5, 0.0, 1.0);
  float f2 = clamp(cos(pG.x * 8.0 + pG.y * 6.0 + time) * 0.5 + 0.5, 0.0, 1.0);
  float f3 = clamp(sin(r * 20.0 - time * 3.5 + bass * 4.0) * 0.5 + 0.5, 0.0, 1.0);

  vec3 cDark = vec3(0.03, 0.01, 0.07);
  vec3 cMagenta = vec3(1.0, 0.02, 0.55);
  vec3 cCyan = vec3(0.0, 0.95, 0.98);
  vec3 cYellow = vec3(1.0, 0.90, 0.08);

  vec3 col = mix(cDark, cMagenta, f1 * 0.75);
  col = mix(col, cCyan, f2 * 0.65);
  col = mix(col, cYellow, f3 * (0.45 + high * 0.6));

  float scan = clamp(sin(uv.y * 480.0) * 0.5 + 0.5, 0.0, 1.0);
  col = col * (1.0 - scan * 0.35);

  float flare = pow(max(0.0, sin(r * 24.0 - time * 5.0)), 12.0) * (0.4 + high * 1.5);
  col += vec3(flare * 0.9, flare * 0.2, flare * 0.6);

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
    print("=== Creative Expansion Pack 21: Build and Gate Runner ===")
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
  c bg 0.12 0.04 0.22
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
  c fillColor 0.22 0.08 0.32
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
  c fillColor 0.16 0.28 0.45
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
    # Register in token_baseline.json
    # ---------------------------------------------------------
    tb_file = os.path.join(ROOT, "tools", "website", "token_baseline.json")
    if os.path.exists(tb_file):
        tb_data = json.load(open(tb_file))
        for it in PIXEL_ITEMS:
            key = f"embeds/{it['slug']}.html"
            if key not in tb_data:
                tb_data[key] = {
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
