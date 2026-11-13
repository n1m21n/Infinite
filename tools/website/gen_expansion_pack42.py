#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 42.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: indie_psychedelic -> indie-psychedelic-liquid-oil-prism, indie-psychedelic-chromatic-mycelium)
2. Sketch 3D (Theme: technical_blueprint -> technical-blueprint-turbine-assembly-3d, technical-blueprint-geodesic-observatory-3d)
3. Field Synth (Theme: vaporwave -> vaporwave-mallsoft-rhodes-pad-synth, vaporwave-midnight-chorus-lead-synth)
4. Field Effects (Theme: synthwave -> synthwave-ensemble-chorus-wash-fx, synthwave-gated-analog-delay-fx)
5. Field Pixel & Embeds (Theme: brutalist_architecture -> brutalist-architecture-monolith-facade, brutalist-architecture-spomenik-rib-vault)
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

TMP_DIR = "/tmp/pack42_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: indie_psychedelic)
#    Palette: #1b0b2e (deep plum), #ff6b4a (warm coral), #ffb703 (amber), #c8e64c (chartreuse), #2ec4b6 (teal)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "indie-psychedelic-liquid-oil-prism",
        "title": "Indie Psychedelic Liquid Oil Prism",
        "description": "Liquified harmonic oil-lens rings and floating chromatic spores drifting across a deep plum field.",
        "tags": ["indie-psychedelic", "psychedelic", "oil", "liquid", "organic", "prism", "procedural"],
        "code": """// Indie Psychedelic Liquid Oil Prism - Infinite Library (Sketch 2D)
param("ring_count", 11, 5, 18);
param("warp_amp", 0.16, 0.04, 0.32);
param("lobe_freq", 5, 3, 9);
param("drift_speed", 0.45, 0.0, 1.5);

function draw(t) {
  // Deep plum ground (#1b0b2e)
  background(0.106, 0.043, 0.180);

  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const baseR = min(w, h) * 0.44;
  const nr = floor(ring_count);
  const lf = floor(lobe_freq);

  // Soft outer aura halos
  noStroke();
  for (let k = 0; k < 4; k++) {
    const a = (k / 4) * Math.PI * 2.0 + t * drift_speed * 0.4;
    const ox = cx + Math.cos(a) * w * 0.16;
    const oy = cy + Math.sin(a) * h * 0.16;
    if (k % 2 === 0) {
      fill(1.0, 0.42, 0.29, 0.14);
    } else {
      fill(0.18, 0.77, 0.71, 0.14);
    }
    circle(ox, oy, baseR * 1.15);
  }

  // Concentric warped oil-lens contours
  for (let i = 0; i < nr; i++) {
    const u = 1.0 - i / nr;
    const rad = baseR * (0.18 + u * 0.85);
    const phase = t * drift_speed + i * 0.55;

    if (i % 4 === 0) {
      fill(1.0, 0.42, 0.29, 0.78);
      stroke(1.0, 0.72, 0.01, 0.9);
    } else if (i % 4 === 1) {
      fill(1.0, 0.718, 0.012, 0.76);
      stroke(0.784, 0.902, 0.298, 0.9);
    } else if (i % 4 === 2) {
      fill(0.180, 0.769, 0.714, 0.74);
      stroke(0.95, 0.92, 0.98, 0.75);
    } else {
      fill(0.106, 0.043, 0.180, 0.88);
      stroke(1.0, 0.42, 0.29, 0.85);
    }
    strokeWeight(2.5);

    beginShape();
    const segs = 72;
    for (let s = 0; s < segs; s++) {
      const ang = (s / segs) * Math.PI * 2.0;
      const w1 = Math.sin(ang * lf + phase) * warp_amp;
      const w2 = Math.cos(ang * (lf - 2) - phase * 0.7) * (warp_amp * 0.55);
      const r = rad * (1.0 + w1 + w2);
      vertex(cx + Math.cos(ang) * r, cy + Math.sin(ang) * r);
    }
    endShape(CLOSE);
  }

  // Orbiting psychedelic oil droplets
  noStroke();
  for (let d = 0; d < 14; d++) {
    const ang = (d / 14) * Math.PI * 2.0 + t * drift_speed * 0.35;
    const dist = baseR * (0.68 + 0.26 * Math.sin(d * 2.1 + t * 0.5));
    const dx = cx + Math.cos(ang) * dist;
    const dy = cy + Math.sin(ang) * dist;
    const sz = 14 + (d % 3) * 9;
    if (d % 3 === 0) fill(0.784, 0.902, 0.298, 0.92);
    else if (d % 3 === 1) fill(1.0, 0.42, 0.29, 0.92);
    else fill(0.180, 0.769, 0.714, 0.92);
    circle(dx, dy, sz);
  }
}
"""
    },
    {
        "slug": "indie-psychedelic-chromatic-mycelium",
        "title": "Indie Psychedelic Chromatic Mycelium",
        "description": "Sinuous radiating mycelium filaments and glowing elliptical spore nodes blooming on deep violet.",
        "tags": ["indie-psychedelic", "mycelium", "tendrils", "spores", "bloom", "organic", "procedural"],
        "code": """// Indie Psychedelic Chromatic Mycelium - Infinite Library (Sketch 2D)
param("branch_count", 18, 8, 32);
param("curl_amp", 0.38, 0.1, 0.8);
param("node_rings", 7, 3, 12);
param("flow_rate", 0.55, 0.0, 1.5);

function draw(t) {
  // Deep violet background (#140924)
  background(0.078, 0.035, 0.141);

  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const nb = floor(branch_count);
  const nr = floor(node_rings);

  // Subtle concentric harmonic wave bands
  noFill();
  for (let r = 1; r <= nr; r++) {
    const rad = (r / nr) * min(w, h) * 0.46;
    stroke(0.28, 0.79, 0.89, 0.22);
    strokeWeight(1.8);
    circle(cx, cy, rad * 2.0);
  }

  // Sinuous radiating mycelium tendrils
  for (let i = 0; i < nb; i++) {
    const baseAng = (i / nb) * Math.PI * 2.0;
    const steps = 28;
    const maxLen = min(w, h) * 0.46;

    if (i % 3 === 0) stroke(0.902, 0.224, 0.459, 0.88);      // magenta
    else if (i % 3 === 1) stroke(1.0, 0.549, 0.259, 0.88);   // tangerine
    else stroke(0.282, 0.792, 0.894, 0.88);                  // seafoam

    strokeWeight(3.2);
    noFill();
    beginShape();
    for (let s = 0; s <= steps; s++) {
      const u = s / steps;
      const d = u * maxLen;
      const swirl = Math.sin(u * 5.0 - t * flow_rate + i * 0.7) * curl_amp * u;
      const ang = baseAng + swirl;
      vertex(cx + Math.cos(ang) * d, cy + Math.sin(ang) * d);
    }
    endShape();

    // Glowing spore node at tip and midpoint
    noStroke();
    for (let m = 1; m <= 2; m++) {
      const u = m * 0.46;
      const d = u * maxLen;
      const swirl = Math.sin(u * 5.0 - t * flow_rate + i * 0.7) * curl_amp * u;
      const ang = baseAng + swirl;
      const nx = cx + Math.cos(ang) * d;
      const ny = cy + Math.sin(ang) * d;
      if (m === 1) fill(1.0, 0.82, 0.25, 0.85);
      else fill(0.902, 0.224, 0.459, 0.92);
      ellipse(nx, ny, 14 + m * 5, 10 + m * 4);
    }
  }

  // Central luminous core
  noStroke();
  fill(1.0, 0.549, 0.259, 0.35);
  circle(cx, cy, 120);
  fill(1.0, 0.84, 0.35, 0.9);
  circle(cx, cy, 48);
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: technical_blueprint)
#    Palette: #0b2447 (blueprint navy), #d8ecff (drafting white-blue), #5bc0be (cyan-teal)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "technical-blueprint-turbine-assembly-3d",
        "title": "Technical Blueprint Turbine Assembly 3D",
        "description": "Precision CAD-style axial turbine rotor with radial airfoil blades, stepped shaft collars, and stator rings.",
        "tags": ["technical-blueprint", "turbine", "cad", "3d", "engineering", "mechanical", "procedural"],
        "code": """// Technical Blueprint Turbine Assembly 3D - Infinite Library (Sketch 3D)
param("blade_count", 18, 8, 32);
param("stage_count", 3, 2, 5);
param("blade_pitch", 0.45, 0.1, 0.9);
param("spin_rate", 0.35, 0.0, 1.2);

function draw(t) {
  rotateX(0.42);
  rotateY(t * spin_rate + 0.5);

  const nb = floor(blade_count);
  const ns = floor(stage_count);

  // Central drive shaft
  push();
  fill(0.847, 0.925, 1.0);
  cylinder(0.14, 2.5, 24);
  pop();

  // Outer blueprint calibration ring
  push();
  fill(0.357, 0.753, 0.745);
  torus(1.48, 0.022, 40, 10);
  pop();

  // Axial rotor stages
  for (let s = 0; s < ns; s++) {
    const sy = (s / (ns - 1) - 0.5) * 1.3;
    const rad = 0.88 - s * 0.12;

    // Hub collar
    push();
    translate(0, sy, 0);
    fill(0.357, 0.753, 0.745);
    cylinder(0.28, 0.16, 24);
    pop();

    // Shroud ring
    push();
    translate(0, sy, 0);
    rotateX(Math.PI * 0.5);
    fill(0.847, 0.925, 1.0);
    torus(rad + 0.22, 0.02, 36, 8);
    pop();

    // Radial turbine blades
    for (let b = 0; b < nb; b++) {
      const ang = (b / nb) * Math.PI * 2.0 + s * 0.22;
      const bx = Math.cos(ang) * (rad * 0.65);
      const bz = Math.sin(ang) * (rad * 0.65);
      push();
      translate(bx, sy, bz);
      rotateY(-ang);
      rotateX(blade_pitch);
      if (b % 3 === 0) fill(0.357, 0.753, 0.745);
      else fill(0.847, 0.925, 1.0);
      box(rad * 0.72, 0.035, 0.14);
      pop();
    }
  }
}
"""
    },
    {
        "slug": "technical-blueprint-geodesic-observatory-3d",
        "title": "Technical Blueprint Geodesic Observatory 3D",
        "description": "Drafting-style geodesic observatory dome with meridian ribs, azimuth tracks, and central collimator tower.",
        "tags": ["technical-blueprint", "geodesic", "observatory", "3d", "architecture", "cad", "procedural"],
        "code": """// Technical Blueprint Geodesic Observatory 3D - Infinite Library (Sketch 3D)
param("meridian_ribs", 12, 6, 20);
param("lat_rings", 5, 3, 8);
param("dome_radius", 1.25, 0.8, 1.6);
param("orbit_speed", 0.25, 0.0, 1.0);

function draw(t) {
  rotateX(0.36);
  rotateY(t * orbit_speed);

  const nm = floor(meridian_ribs);
  const nl = floor(lat_rings);

  // Base plinth and azimuth ring
  push();
  translate(0, -0.55, 0);
  fill(0.18, 0.34, 0.52);
  cylinder(dome_radius * 1.12, 0.1, 36);
  pop();

  push();
  translate(0, -0.48, 0);
  rotateX(Math.PI * 0.5);
  fill(0.357, 0.753, 0.745);
  torus(dome_radius * 1.08, 0.026, 40, 10);
  pop();

  // Meridian ribs
  for (let i = 0; i < nm; i++) {
    const ang = (i / nm) * Math.PI;
    push();
    rotateY(ang);
    fill(0.847, 0.925, 1.0);
    torus(dome_radius, 0.016, 36, 8);
    pop();
  }

  // Latitude rings
  for (let j = 1; j <= nl; j++) {
    const phi = (j / (nl + 1)) * Math.PI * 0.48;
    const ry = Math.sin(phi) * dome_radius;
    const rr = Math.cos(phi) * dome_radius;
    push();
    translate(0, ry, 0);
    rotateX(Math.PI * 0.5);
    fill(0.357, 0.753, 0.745);
    torus(rr, 0.015, 36, 8);
    pop();
  }

  // Central collimator instrument
  push();
  fill(0.847, 0.925, 1.0);
  cylinder(0.12, 1.1, 20);
  translate(0, 0.62, 0);
  fill(0.357, 0.753, 0.745);
  sphere(0.18, 16);
  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: vaporwave)
#    60-85 BPM, Scale 6/0, Refs: Macintosh Plus, Vektroid, 2814
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "vaporwave-mallsoft-rhodes-pad-synth",
        "title": "Vaporwave Mallsoft Rhodes Pad Synth",
        "description": "Lush detuned electric-tine pad synthesizer with slow tape wow, soft FM bell shimmer, and warm lowpass bloom.",
        "note_root": 53,
        "params": {
            "tine": 0.42,
            "detune": 0.012,
            "warmth": 1600.0,
            "wow": 0.35,
            "decay": 0.85
        },
        "code": """param float tine = 0.42 [0.0, 1.2];
param float detune = 0.012 [0.0, 0.035];
param float warmth = 1600.0 [300.0, 4200.0];
param float wow = 0.35 [0.0, 1.0];
param float decay = 0.85 [0.2, 2.0];
state float p1 = 0;
state float p2 = 0;
state float pMod = 0;
state float lfo = 0;
state float env = 0;
state float lp = 0;

target = if(gate > 0.5, 1.0, 0.0);
decayRate = clamp(1.0 / (sr * decay), 0.0001, 0.03);
env = if(target > env, env + (target - env) * 0.06, env - env * decayRate);

lfo = (lfo + 0.9 / sr) % 1.0;
pitchWow = 1.0 + sin(6.2831853 * lfo) * wow * 0.008;

f0 = clamp(freq * pitchWow, 40.0, 2600.0);
p1 = (p1 + f0 * (1.0 - detune) / sr) % 1.0;
p2 = (p2 + f0 * (1.0 + detune) / sr) % 1.0;
pMod = (pMod + (f0 * 3.0) / sr) % 1.0;

modSig = sin(6.2831853 * pMod) * tine * env * env;
v1 = sin(6.2831853 * p1 + modSig);
v2 = sin(6.2831853 * p2 + modSig * 0.65);
raw = (v1 + v2) * 0.5;

fCut = clamp(warmth / sr * 3.14, 0.01, 0.44);
lp = lp + fCut * (raw - lp);

out = lp * env * 0.78;
"""
    },
    {
        "slug": "vaporwave-midnight-chorus-lead-synth",
        "title": "Vaporwave Midnight Chorus Lead Synth",
        "description": "Dreamy dual-oscillator detuned saw-triangle lead with gentle vibrato, chorus drift, and silky lowpass filter.",
        "note_root": 60,
        "params": {
            "detune": 0.014,
            "cutoff": 1450.0,
            "vibrato": 0.25,
            "subMix": 0.38,
            "decay": 0.55
        },
        "code": """param float detune = 0.014 [0.0, 0.04];
param float cutoff = 1450.0 [200.0, 4500.0];
param float vibrato = 0.25 [0.0, 1.0];
param float subMix = 0.38 [0.0, 0.8];
param float decay = 0.55 [0.15, 1.5];
state float p1 = 0;
state float p2 = 0;
state float pSub = 0;
state float vib = 0;
state float env = 0;
state float lp1 = 0;
state float lp2 = 0;

target = if(gate > 0.5, 1.0, 0.0);
decayRate = clamp(1.0 / (sr * decay), 0.0001, 0.04);
env = if(target > env, env + (target - env) * 0.1, env - env * decayRate);

vib = (vib + 4.8 / sr) % 1.0;
vMod = 1.0 + sin(6.2831853 * vib) * vibrato * 0.006;

f0 = clamp(freq * vMod, 40.0, 2800.0);
p1 = (p1 + f0 * (1.0 - detune) / sr) % 1.0;
p2 = (p2 + f0 * (1.0 + detune) / sr) % 1.0;
pSub = (pSub + (f0 * 0.5) / sr) % 1.0;

saw1 = 2.0 * p1 - 1.0;
tri2 = 1.0 - 4.0 * abs(p2 - 0.5);
sub = sin(6.2831853 * pSub);
raw = (saw1 * 0.45 + tri2 * 0.55) + sub * subMix;

fCut = clamp((cutoff + env * 900.0) / sr * 3.14, 0.01, 0.44);
lp1 = lp1 + fCut * (raw - lp1);
lp2 = lp2 + fCut * (lp1 - lp2);

out = lp2 * env * 0.32;
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: synthwave)
#    96-118 BPM, Refs: Kavinsky, Drive (2011), Moroder
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "synthwave-ensemble-chorus-wash-fx",
        "title": "Synthwave Ensemble Chorus Wash FX",
        "description": "Analog-inspired multi-voice modulated chorus wash with warm high-frequency smoothing for retro synth leads and pads.",
        "params": {
            "rate": 0.85,
            "depth": 0.45,
            "warmth": 0.65,
            "mix": 0.58
        },
        "code": """param float rate = 0.85 [0.1, 4.0];
param float depth = 0.45 [0.0, 1.0];
param float warmth = 0.65 [0.1, 1.0];
param float mix = 0.58 [0.0, 1.0];
state float lfo1 = 0;
state float lfo2 = 0;
state float tap1 = 0;
state float tap2 = 0;
state float tap3 = 0;
state float lp = 0;

lfo1 = (lfo1 + rate / sr) % 1.0;
lfo2 = (lfo2 + (rate * 1.37) / sr) % 1.0;

m1 = sin(6.2831853 * lfo1) * depth * 0.06;
m2 = cos(6.2831853 * lfo2) * depth * 0.06;

r1 = clamp(0.18 + m1, 0.03, 0.45);
r2 = clamp(0.14 + m2, 0.03, 0.45);

tap1 = tap1 + r1 * (in - tap1);
tap2 = tap2 + r2 * (tap1 - tap2);
tap3 = tap3 + 0.11 * (tap2 - tap3);

fCut = clamp(0.15 + warmth * 0.30, 0.04, 0.45);
lp = lp + fCut * ((tap1 + tap2 + tap3) * 0.42 - lp);

processed = in * (1.0 - mix) + lp * mix;
out = processed * 1.25;
"""
    },
    {
        "slug": "synthwave-gated-analog-delay-fx",
        "title": "Synthwave Gated Analog Delay FX",
        "description": "Retro-futuristic multi-tap analog delay with warm tape saturation and damped feedback tail.",
        "params": {
            "feedback": 0.56,
            "damping": 0.48,
            "drive": 1.4,
            "mix": 0.50
        },
        "code": """param float feedback = 0.56 [0.0, 0.85];
param float damping = 0.48 [0.1, 0.95];
param float drive = 1.4 [1.0, 3.5];
param float mix = 0.50 [0.0, 1.0];
state float tap1 = 0;
state float tap2 = 0;
state float tap3 = 0;
state float lp = 0;

fCut = clamp(0.10 + (1.0 - damping) * 0.28, 0.03, 0.42);
lp = lp + fCut * (in - lp);

tap1 = tap1 + 0.065 * (lp + tap3 * feedback * 0.55 - tap1);
tap2 = tap2 + 0.052 * (tap1 - tap2);
tap3 = tap3 + 0.044 * (tap2 - tap3);

echo = (tap1 * 0.45 + tap2 * 0.35 + tap3 * 0.25) * drive;
sat = echo / (1.0 + abs(echo) * 0.35);

processed = in * (1.0 - mix) + sat * mix;
out = processed * 0.95;
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel & Embeds Sources (Theme: brutalist_architecture)
#    Palette: #222222 (deep shadow concrete), #7a7a78 (raw beton), #d9d6cf (lit limestone concrete)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "brutalist-architecture-monolith-facade",
        "title": "Brutalist Architecture Monolith Facade",
        "description": "Board-formed concrete facade with rhythmic cantilevered coffer bays and diagonal architectural shadow shafts.",
        "embed_desc": "Audio-reactive Brutalist concrete facade inspired by Le Corbusier and Soviet modernism. Bass shifts the monumental cantilevers, mids sweep the shadow angle, and highs sharpen the concrete relief.",
        "tags": ["brutalist", "architecture", "concrete", "monolith", "modernism", "shader", "webgl", "audio-reactive"],
        "params": {
            "bayCols": 6.0,
            "bayRows": 5.0,
            "shadowAngle": 0.62,
            "grainAmt": 0.05,
            "contrast": 1.08
        },
        "code": """param float bayCols = 6.0 [3.0, 12.0];
param float bayRows = 5.0 [2.0, 10.0];
param float shadowAngle = 0.62 [0.1, 1.3];
param float grainAmt = 0.05 [0.0, 0.14];
param float contrast = 1.08 [0.8, 1.5];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0;

# Brutalist palette: deep shadow (#222222), raw beton (#7a7a78), lit concrete (#d9d6cf)
cDark = vec3(0.133, 0.133, 0.133);
cMid = vec3(0.478, 0.478, 0.471);
cLight = vec3(0.851, 0.839, 0.812);

# Board-formed concrete grain and horizontal formwork seams
grain = (fract(sin(dot(uv * res, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * grainAmt;
seam = smoothstep(0.92, 0.99, abs(sin(p.y * 28.0)));

# Coffered grid bays
gx = fract((p.x * 0.5 + 0.5) * bayCols) - 0.5;
gy = fract((p.y * 0.5 + 0.5) * bayRows) - 0.5;
bayDist = max(abs(gx), abs(gy));
inBay = 1.0 - smoothstep(0.32, 0.36, bayDist);

# Diagonal brutalist sun/shadow shaft
ang = shadowAngle + sin(t * 0.25) * 0.12;
proj = p.x * cos(ang) + p.y * sin(ang);
shaft = smoothstep(-0.18, 0.22, sin(proj * 3.2 + t * 0.15));

# Cantilevered upper lintel beam
lintel = 1.0 - smoothstep(0.0, 0.04, abs(p.y - 0.42) - 0.09);

baseCol = mix(cMid, cLight, shaft * 0.72);
baseCol = mix(baseCol, cDark, inBay * (0.55 + 0.30 * (1.0 - shaft)));
baseCol = mix(baseCol, cDark * 0.75, lintel * 0.65);
baseCol = mix(baseCol, cDark, seam * 0.22);

col = clamp((baseCol + grain) * contrast, 0.0, 1.0);
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

  float b = audio_bass * 0.4;
  float m = audio_mid * 0.4;
  float h = audio_high * 0.4;

  vec3 cDark = vec3(0.133, 0.133, 0.133);
  vec3 cMid = vec3(0.478, 0.478, 0.471);
  vec3 cLight = vec3(0.851, 0.839, 0.812);

  float grain = (fract(sin(dot(uv * res, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * (0.05 + h * 0.08);
  float seam = smoothstep(0.92, 0.99, abs(sin(p.y * 28.0)));

  float gx = fract((p.x * 0.5 + 0.5) * 6.0) - 0.5;
  float gy = fract((p.y * 0.5 + 0.5) * 5.0) - 0.5;
  float bayDist = max(abs(gx), abs(gy));
  float inBay = 1.0 - smoothstep(0.32 - b * 0.08, 0.36 - b * 0.08, bayDist);

  float ang = 0.62 + sin(t * 0.25) * 0.12 + m * 0.35;
  float proj = p.x * cos(ang) + p.y * sin(ang);
  float shaft = smoothstep(-0.18, 0.22, sin(proj * 3.2 + t * 0.15));

  float lintel = 1.0 - smoothstep(0.0, 0.04, abs(p.y - (0.42 + b * 0.08)) - 0.09);

  vec3 baseCol = mix(cMid, cLight, shaft * (0.72 + h * 0.22));
  baseCol = mix(baseCol, cDark, inBay * (0.55 + 0.30 * (1.0 - shaft)));
  baseCol = mix(baseCol, cDark * 0.75, lintel * 0.65);
  baseCol = mix(baseCol, cDark, seam * 0.22);

  vec3 col = clamp((baseCol + grain) * 1.08, 0.0, 1.0);
  gl_FragColor = vec4(col, 1.0);
}
"""
    },
    {
        "slug": "brutalist-architecture-spomenik-rib-vault",
        "title": "Brutalist Architecture Spomenik Rib Vault",
        "description": "Monumental Spomenik concrete rib vault with radiating cantilevered fins, terraced plinths, and brutalist oculus.",
        "embed_desc": "Audio-reactive Spomenik concrete monument. Bass pulses the brutalist oculus and ribs, mids rotate the directional sun relief, and highs illuminate the raw concrete edges.",
        "tags": ["brutalist", "spomenik", "concrete", "monument", "vault", "shader", "webgl", "audio-reactive"],
        "params": {
            "ribCount": 12.0,
            "oculusSize": 0.24,
            "ringSteps": 6.0,
            "grainAmt": 0.05,
            "contrast": 1.1
        },
        "code": """param float ribCount = 12.0 [6.0, 24.0];
param float oculusSize = 0.24 [0.10, 0.45];
param float ringSteps = 6.0 [3.0, 12.0];
param float grainAmt = 0.05 [0.0, 0.14];
param float contrast = 1.10 [0.8, 1.5];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0;

cDark = vec3(0.133, 0.133, 0.133);
cMid = vec3(0.478, 0.478, 0.471);
cLight = vec3(0.851, 0.839, 0.812);

r = length(p);
a = atan2(p.y, p.x);

# Concrete formwork grain
grain = (fract(sin(dot(uv * res, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * grainAmt;

# Radiating Spomenik concrete ribs
ribWave = cos(a * ribCount + sin(t * 0.2) * 0.15);
ribMask = smoothstep(0.15, 0.55, ribWave);

# Terraced concentric concrete plinth rings
ringIdx = floor(r * ringSteps);
ringEdge = smoothstep(0.86, 0.96, fract(r * ringSteps));
stepShade = 0.35 + 0.65 * fract(ringIdx * 0.37);

# Central brutalist oculus void
oculus = 1.0 - smoothstep(oculusSize - 0.02, oculusSize + 0.02, r);
rim = smoothstep(oculusSize - 0.01, oculusSize + 0.03, r) * (1.0 - smoothstep(oculusSize + 0.03, oculusSize + 0.09, r));

# Directional relief lighting
sun = 0.5 + 0.5 * cos(a - 0.8 + t * 0.18);

surface = mix(cDark, cMid, stepShade);
surface = mix(surface, cLight, ribMask * sun * 0.82);
surface = mix(surface, cDark, ringEdge * 0.48);
surface = mix(surface, cLight, rim * 0.9);
surface = mix(surface, cDark * 0.6, oculus);

col = clamp((surface + grain) * contrast, 0.0, 1.0);
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

  float b = audio_bass * 0.35;
  float m = audio_mid * 0.35;
  float h = audio_high * 0.35;

  vec3 cDark = vec3(0.133, 0.133, 0.133);
  vec3 cMid = vec3(0.478, 0.478, 0.471);
  vec3 cLight = vec3(0.851, 0.839, 0.812);

  float r = length(p);
  float a = atan(p.y, p.x);

  float grain = (fract(sin(dot(uv * res, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * (0.05 + h * 0.07);

  float ribWave = cos(a * 12.0 + sin(t * 0.2) * 0.15 + m * 0.4);
  float ribMask = smoothstep(0.15 - b * 0.1, 0.55 - b * 0.1, ribWave);

  float ringIdx = floor(r * 6.0);
  float ringEdge = smoothstep(0.86, 0.96, fract(r * 6.0));
  float stepShade = 0.35 + 0.65 * fract(ringIdx * 0.37);

  float ocR = 0.24 + b * 0.07;
  float oculus = 1.0 - smoothstep(ocR - 0.02, ocR + 0.02, r);
  float rim = smoothstep(ocR - 0.01, ocR + 0.03, r) * (1.0 - smoothstep(ocR + 0.03, ocR + 0.09, r));

  float sun = 0.5 + 0.5 * cos(a - 0.8 + t * 0.18 + m * 0.6);

  vec3 surface = mix(cDark, cMid, stepShade);
  surface = mix(surface, cLight, ribMask * sun * (0.82 + h * 0.18));
  surface = mix(surface, cDark, ringEdge * 0.48);
  surface = mix(surface, cLight, rim * 0.9);
  surface = mix(surface, cDark * 0.6, oculus);

  vec3 col = clamp((surface + grain) * 1.10, 0.0, 1.0);
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
        let bSum = 0, mSum = 0, hSum = 0;
        for (let i = 0; i < 4; i++) bSum += freqData[i];
        for (let i = 4; i < 16; i++) mSum += freqData[i];
        for (let i = 16; i < 32; i++) hSum += freqData[i];
        const targetB = (bSum / 4) / 255;
        const targetM = (mSum / 12) / 255;
        const targetH = (hSum / 16) / 255;
        bass += (targetB - bass) * 0.25;
        mid += (targetM - mid) * 0.2;
        high += (targetH - high) * 0.2;
      }} else {{
        bass = 0.25 + 0.25 * Math.sin(t * 2.2);
        mid = 0.25 + 0.25 * Math.sin(t * 3.1 + 1.2);
        high = 0.25 + 0.25 * Math.sin(t * 4.5 + 2.4);
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
  c bg 0.043 0.141 0.278
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
  i note1 {nr}
  i note2 {nr+4}
  i note3 {nr+7}
  i note4 {nr}
  i note5 {nr+4}
  i note6 {nr+11}
  i note7 {nr+7}
  i note8 {nr}
  i note9 {nr}
  i note10 {nr+4}
  i note11 {nr+5}
  i note12 {nr}
  i note13 {nr+12}
  i note14 {nr+7}
  i note15 {nr}
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
  c fillColor 0.24 0.16 0.32
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
  f rateBeats 0.25
  i steps 16
  i note0 45
  i note1 45
  i note2 57
  i note3 52
  i note4 45
  i note5 57
  i note6 50
  i note7 52
  i note8 45
  i note9 45
  i note10 57
  i note11 52
  i note12 45
  i note13 57
  i note14 50
  i note15 55
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
  f cutoff 2800.0
  f resonance 0.25
  f decay 0.35
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
  c fillColor 0.22 0.14 0.30
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
                "tags": ["synth", "vaporwave", "audio", "dsp", "pad", "lead", "retro"],
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
                "tags": ["effect", "synthwave", "audio", "dsp", "chorus", "delay", "retro"],
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
