#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 41.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: lilliputian_surreal -> lilliputian-surreal-colossal-orb-procession, lilliputian-surreal-titanic-staircase-pilgrims)
2. Sketch 3D (Theme: cymatics_sine -> cymatics-sine-chladni-nodal-shell-3d, cymatics-sine-harmonic-toroid-resonator-3d)
3. Field Synth (Theme: downtempo -> downtempo-warm-sub-tape-bass-synth, downtempo-dusty-rhodes-bell-pad-synth)
4. Field Effects (Theme: minimal_techno -> minimal-techno-dub-chord-echo-fx, minimal-techno-sub-harmonic-shaper-fx)
5. Field Pixel & Embeds (Theme: suprematist_blocks -> suprematist-blocks-kinetic-red-wedge, suprematist-blocks-malevich-floating-plane)
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

TMP_DIR = "/tmp/pack41_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: lilliputian_surreal)
#    Palette: #1a2a3a (deep slate), #e8d8c8 (warm alabaster), #b84a39 (terracotta)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "lilliputian-surreal-colossal-orb-procession",
        "title": "Lilliputian Surreal Colossal Orb Procession",
        "description": "Miniature vermilion figures crossing a suspended causeway beneath a titanic alabaster sphere in a vaulted slate chamber.",
        "tags": ["lilliputian", "surreal", "scale", "miniature", "procession", "architectural", "procedural"],
        "code": """// Lilliputian Surreal Colossal Orb Procession - Infinite Library (Sketch 2D)
param("pilgrim_count", 16, 6, 32);
param("orb_scale", 0.56, 0.35, 0.72);
param("arch_ribs", 10, 4, 18);
param("drift_speed", 0.04, 0.0, 0.2);

function draw(t) {
  // Deep slate navy background (#1a2a3a)
  background(0.102, 0.165, 0.227);

  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.43;

  // Vaulted architectural perspective ribs in muted slate-ivory
  noFill();
  stroke(0.24, 0.33, 0.42, 0.85);
  strokeWeight(2.0);
  const ribs = floor(arch_ribs);
  for (let i = 0; i < ribs; i++) {
    const r = (i + 1) / ribs;
    const rw = w * (0.28 + r * 0.68);
    const rh = h * (0.28 + r * 0.66);
    rect(cx - rw * 0.5, cy - rh * 0.48, rw, rh);
  }

  // Horizon perspective rays converging behind the colossal relic
  stroke(0.28, 0.37, 0.46, 0.55);
  strokeWeight(1.5);
  for (let a = 0; a < 16; a++) {
    const ang = (a / 16) * Math.PI * 2.0;
    line(cx + Math.cos(ang) * 90, cy + Math.sin(ang) * 90,
         cx + Math.cos(ang) * w * 0.75, cy + Math.sin(ang) * h * 0.75);
  }

  // Shadow halo behind the titanic alabaster sphere
  noStroke();
  fill(0.06, 0.10, 0.15, 0.85);
  circle(cx + 18, cy + 22, min(w, h) * orb_scale * 1.06);

  // Colossal alabaster sphere (#e8d8c8)
  fill(0.910, 0.847, 0.784);
  circle(cx, cy, min(w, h) * orb_scale);

  // Subtle crescent shadow and meridian rings on the sphere
  fill(0.76, 0.68, 0.61, 0.45);
  circle(cx - w * 0.04, cy - h * 0.03, min(w, h) * orb_scale * 0.78);
  noFill();
  stroke(0.102, 0.165, 0.227, 0.35);
  strokeWeight(2.0);
  ellipse(cx, cy, min(w, h) * orb_scale * 0.92, min(w, h) * orb_scale * 0.32);

  // Suspended causeway deck below the sphere
  const deckY = h * 0.78;
  noStroke();
  fill(0.910, 0.847, 0.784);
  rect(0, deckY, w, 16);
  fill(0.06, 0.10, 0.15);
  rect(0, deckY + 16, w, h - deckY - 16);

  // Support piers under causeway
  fill(0.22, 0.31, 0.40);
  for (let p = 1; p < 8; p++) {
    const px = (p / 8) * w;
    rect(px - 6, deckY + 16, 12, h - deckY - 16);
  }

  // Miniature vermilion pilgrims (#b84a39) traversing the causeway
  const n = floor(pilgrim_count);
  for (let i = 0; i < n; i++) {
    const prog = (i / n + t * drift_speed) % 1.0;
    const fx = 60 + prog * (w - 120);
    const bob = Math.abs(Math.sin(t * 2.5 + i * 1.7)) * 3.0;
    const fy = deckY - bob;

    // Long surrealist shadow cast onto the deck
    fill(0.08, 0.12, 0.18, 0.55);
    rect(fx - 14, deckY + 2, 16, 4);

    // Tiny terracotta body and head
    fill(0.722, 0.290, 0.224);
    rect(fx - 3, fy - 18, 6, 18);
    circle(fx, fy - 23, 7);
  }
}
"""
    },
    {
        "slug": "lilliputian-surreal-titanic-staircase-pilgrims",
        "title": "Lilliputian Surreal Titanic Staircase Pilgrims",
        "description": "Monumental stepped alabaster monoliths and sweeping arches traversed by miniature terracotta figures in quiet scale contrast.",
        "tags": ["lilliputian", "surreal", "staircase", "monolith", "scale", "miniature", "procedural"],
        "code": """// Lilliputian Surreal Titanic Staircase Pilgrims - Infinite Library (Sketch 2D)
param("steps", 12, 6, 20);
param("travelers", 14, 4, 28);
param("arch_span", 0.62, 0.4, 0.8);
param("pace", 0.05, 0.0, 0.2);

function draw(t) {
  // Deep slate navy interior (#1a2a3a)
  background(0.102, 0.165, 0.227);

  const w = width;
  const h = height;

  // Giant background portal arch (#e8d8c8)
  noStroke();
  fill(0.18, 0.26, 0.35);
  const archW = w * arch_span;
  const archX = (w - archW) * 0.5;
  rect(archX, h * 0.12, archW, h * 0.88);
  circle(w * 0.5, h * 0.30, archW);

  // Inner warm alabaster celestial disc
  fill(0.910, 0.847, 0.784);
  circle(w * 0.5, h * 0.26, archW * 0.46);

  // Monumental diagonal stepped staircase rising left to right
  const nSteps = floor(steps);
  const stepW = w / nSteps;
  const baseH = h * 0.88;

  for (let i = 0; i < nSteps; i++) {
    const sx = i * stepW;
    const stepTop = baseH - (i + 1) * (h * 0.56 / nSteps);

    // Step riser in warm alabaster
    fill(0.910, 0.847, 0.784);
    rect(sx, stepTop, stepW + 1, h - stepTop);

    // Deep slate shadow side on each riser
    fill(0.14, 0.21, 0.29);
    rect(sx, stepTop + 14, stepW * 0.35, h - stepTop - 14);

    // Crisp horizontal tread highlight
    fill(0.96, 0.92, 0.87);
    rect(sx, stepTop, stepW + 1, 6);
  }

  // Framing colossal foreground columns
  fill(0.07, 0.12, 0.17);
  rect(0, 0, 56, h);
  rect(w - 56, 0, 56, h);

  // Miniature terracotta pilgrims ascending the titanic steps (#b84a39)
  const nTrav = floor(travelers);
  for (let k = 0; k < nTrav; k++) {
    const u = (k / nTrav + t * pace) % 1.0;
    const stepIdx = Math.min(nSteps - 1, floor(u * nSteps));
    const localU = (u * nSteps) - stepIdx;
    const px = stepIdx * stepW + localU * stepW;
    const py = baseH - (stepIdx + 1) * (h * 0.56 / nSteps);

    // Tiny figure
    fill(0.722, 0.290, 0.224);
    rect(px - 3, py - 16, 6, 16);
    circle(px, py - 21, 6.5);
  }
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: cymatics_sine)
#    Palette: #000000, #ffffff, #7df9ff (Cymatics & Chladni)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "cymatics-sine-chladni-nodal-shell-3d",
        "title": "Cymatics Sine Chladni Nodal Shell 3D",
        "description": "3D standing-wave Chladni plate lattice where crisp white and cyan-ice nodal pillars trace resonant harmonic modes.",
        "tags": ["cymatics", "chladni", "3d", "standing-wave", "nodal", "cyan", "procedural"],
        "code": """// Cymatics Sine Chladni Nodal Shell 3D - Infinite Library (Sketch 3D)
param("mode_m", 3.0, 1.0, 7.0);
param("mode_n", 5.0, 2.0, 9.0);
param("grid_res", 20, 10, 28);
param("wave_amp", 0.35, 0.1, 0.7);

function draw(t) {
  rotateY(t * 0.22);
  rotateX(0.48);

  const g = floor(grid_res);
  const span = 2.4;
  const step = span / g;

  // Outer resonant containment ring in electric cyan-ice (#7df9ff)
  push();
  fill(0.49, 0.976, 1.0);
  torus(1.42, 0.025, 36, 10);
  pop();

  // Inner reference ring in crisp white
  push();
  fill(0.95, 0.98, 1.0);
  torus(0.95, 0.018, 32, 8);
  pop();

  // Chladni standing-wave nodal field
  for (let ix = 0; ix < g; ix++) {
    const x = (ix / (g - 1) - 0.5) * span;
    for (let iz = 0; iz < g; iz++) {
      const z = (iz / (g - 1) - 0.5) * span;
      const r = Math.sqrt(x * x + z * z);
      if (r > 1.32) continue;

      const c1 = Math.cos(mode_m * x * 2.2) * Math.cos(mode_n * z * 2.2);
      const c2 = Math.cos(mode_n * x * 2.2) * Math.cos(mode_m * z * 2.2);
      const nodal = c1 - c2;
      const h = Math.abs(nodal) * wave_amp + 0.04;
      const isNode = Math.abs(nodal) < 0.28;

      push();
      translate(x, nodal * wave_amp * 0.5, z);
      if (isNode) {
        fill(0.49, 0.976, 1.0);
        box(step * 0.52, 0.05, step * 0.52);
      } else {
        fill(0.94, 0.96, 0.98);
        box(step * 0.36, h, step * 0.36);
      }
      pop();
    }
  }
}
"""
    },
    {
        "slug": "cymatics-sine-harmonic-toroid-resonator-3d",
        "title": "Cymatics Sine Harmonic Toroid Resonator 3D",
        "description": "Nested cymatic torus rings and radial harmonic spokes oscillating around a central acoustic focal core.",
        "tags": ["cymatics", "toroid", "harmonic", "3d", "resonance", "cyan", "procedural"],
        "code": """// Cymatics Sine Harmonic Toroid Resonator 3D - Infinite Library (Sketch 3D)
param("ring_count", 9, 4, 16);
param("spoke_count", 24, 8, 40);
param("twist_rate", 0.35, 0.0, 1.0);
param("spread", 1.25, 0.7, 1.8);

function draw(t) {
  rotateY(t * 0.25);
  rotateX(0.38 + Math.sin(t * 0.4) * 0.08);

  // Central resonant sphere
  push();
  fill(0.49, 0.976, 1.0);
  sphere(0.24, 18);
  pop();

  // Nested harmonic torus shells
  const nr = floor(ring_count);
  for (let i = 0; i < nr; i++) {
    const u = i / nr;
    const rad = 0.42 + u * spread;
    const tilt = Math.sin(u * Math.PI * 2.0 + t * twist_rate) * 0.65;
    push();
    rotateX(tilt);
    rotateZ(u * Math.PI * 0.5);
    if (i % 2 === 0) {
      fill(0.96, 0.98, 1.0);
    } else {
      fill(0.49, 0.976, 1.0);
    }
    torus(rad, 0.022, 32, 8);
    pop();
  }

  // Radial standing-wave nodal spokes
  const ns = floor(spoke_count);
  for (let k = 0; k < ns; k++) {
    const a = (k / ns) * Math.PI * 2.0;
    const amp = 0.55 + 0.45 * Math.sin(a * 4.0 + t * 0.8);
    const rx = Math.cos(a) * 1.15;
    const rz = Math.sin(a) * 1.15;
    push();
    translate(rx, 0, rz);
    rotateY(-a);
    fill(0.92, 0.95, 0.98);
    box(0.45 * amp, 0.04, 0.04);
    pop();
  }
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: downtempo)
#    92 BPM, Scale 4, Refs: Massive Attack, Portishead, Bonobo
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "downtempo-warm-sub-tape-bass-synth",
        "title": "Downtempo Warm Sub Tape Bass Synth",
        "description": "Trip-hop sub-weighted analog bass synthesizer with tape saturation, subtle pitch drift, and resonant lowpass envelope.",
        "note_root": 36, # C1
        "params": {
            "cutoff": 680.0,
            "resonance": 0.25,
            "tapeDrive": 1.85,
            "drift": 0.22,
            "decay": 0.45
        },
        "code": """param float cutoff = 680.0 [80.0, 2800.0];
param float resonance = 0.25 [0.0, 0.75];
param float tapeDrive = 1.85 [1.0, 4.5];
param float drift = 0.22 [0.0, 1.0];
param float decay = 0.45 [0.1, 1.2];
state float p = 0;
state float pSub = 0;
state float lfo = 0;
state float env = 0;
state float lp1 = 0;
state float lp2 = 0;

target = if(gate > 0.5, 1.0, 0.0);
decayRate = clamp(1.0 / (sr * decay), 0.0001, 0.04);
env = if(target > env, env + (target - env) * 0.14, env - env * decayRate);

lfo = (lfo + 1.8 / sr) % 1.0;
wow = sin(6.2831853 * lfo) * drift * 0.005;

f0 = clamp(freq * (1.0 + wow), 24.0, 900.0);
p = (p + f0 / sr) % 1.0;
pSub = (pSub + (f0 * 0.5) / sr) % 1.0;

saw = 2.0 * p - 1.0;
tri = 1.0 - 4.0 * abs(p - 0.5);
sub = sin(6.2831853 * pSub);

raw = saw * 0.35 + tri * 0.35 + sub * 0.55;
u = raw * tapeDrive;
sat = u / (1.0 + abs(u) * 0.42);

fCut = clamp((cutoff + env * 720.0) / sr * 3.14, 0.01, 0.42);
fb = clamp(resonance * 1.4, 0.0, 1.1);
lp1 = lp1 + fCut * (sat - lp2 * fb * 0.35 - lp1);
lp2 = lp2 + fCut * (lp1 - lp2);

out = lp2 * (0.28 + env * 0.72) * 0.28;
"""
    },
    {
        "slug": "downtempo-dusty-rhodes-bell-pad-synth",
        "title": "Downtempo Dusty Rhodes Bell Pad Synth",
        "description": "Warm dusty electric-tine and bell pad synthesizer with soft FM overtone bloom, gentle chorus detune, and mellow decay.",
        "note_root": 51, # Eb2
        "params": {
            "tine": 0.45,
            "warmth": 1400.0,
            "detune": 0.008,
            "tremolo": 0.25,
            "decay": 0.65
        },
        "code": """param float tine = 0.45 [0.0, 1.5];
param float warmth = 1400.0 [200.0, 4000.0];
param float detune = 0.008 [0.0, 0.03];
param float tremolo = 0.25 [0.0, 0.8];
param float decay = 0.65 [0.15, 1.5];
state float p1 = 0;
state float p2 = 0;
state float pMod = 0;
state float trem = 0;
state float env = 0;
state float lp = 0;

target = if(gate > 0.5, 1.0, 0.0);
decayRate = clamp(1.0 / (sr * decay), 0.0001, 0.03);
env = if(target > env, env + (target - env) * 0.08, env - env * decayRate);

f0 = clamp(freq, 40.0, 2400.0);
p1 = (p1 + f0 * (1.0 - detune) / sr) % 1.0;
p2 = (p2 + f0 * (1.0 + detune) / sr) % 1.0;
pMod = (pMod + (f0 * 4.0) / sr) % 1.0;

trem = (trem + 3.4 / sr) % 1.0;
tremAmp = 1.0 - tremolo * (0.5 + 0.5 * sin(6.2831853 * trem));

modSig = sin(6.2831853 * pMod) * tine * env * env;
v1 = sin(6.2831853 * p1 + modSig);
v2 = sin(6.2831853 * p2 + modSig * 0.7);
raw = (v1 + v2) * 0.5;

fCut = clamp(warmth / sr * 3.14, 0.01, 0.45);
lp = lp + fCut * (raw - lp);

out = lp * env * tremAmp * 0.82;
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: minimal_techno)
#    125 BPM, Scale 1, Refs: Basic Channel, Plastikman, Perlon
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "minimal-techno-dub-chord-echo-fx",
        "title": "Minimal Techno Dub Chord Echo FX",
        "description": "Basic Channel-inspired damped stereo dub delay network with dark feedback filtering and subtle tape pitch drift.",
        "params": {
            "feedback": 0.62,
            "damping": 0.55,
            "drift": 0.25,
            "mix": 0.52
        },
        "code": """param float feedback = 0.62 [0.0, 0.88];
param float damping = 0.55 [0.1, 0.95];
param float drift = 0.25 [0.0, 1.0];
param float mix = 0.52 [0.0, 1.0];
state float tap1 = 0;
state float tap2 = 0;
state float tap3 = 0;
state float tap4 = 0;
state float lp = 0;
state float lfo = 0;

lfo = (lfo + 1.1 / sr) % 1.0;
modRate = sin(6.2831853 * lfo) * drift * 0.015;

fCut = clamp(0.08 + (1.0 - damping) * 0.26, 0.02, 0.40);
lp = lp + fCut * (in - lp);

r = clamp(0.075 + modRate, 0.02, 0.30);
tap1 = tap1 + r * (lp + tap4 * feedback * 0.45 - tap1);
tap2 = tap2 + r * 0.84 * (tap1 - tap2);
tap3 = tap3 + r * 0.72 * (tap2 - tap3);
tap4 = tap4 + r * 0.61 * (tap3 - tap4);

echo = (tap2 * 0.42 + tap3 * 0.34 + tap4 * 0.24) * 1.15;
sat = echo / (1.0 + abs(echo) * 0.28);

processed = in * (1.0 - mix) + sat * mix;
out = processed * 1.45;
"""
    },
    {
        "slug": "minimal-techno-sub-harmonic-shaper-fx",
        "title": "Minimal Techno Sub Harmonic Shaper FX",
        "description": "Plastikman-inspired resonant lowpass sculpt and asymmetric soft-clipper for tight minimal techno basslines and percussive clicks.",
        "params": {
            "drive": 2.4,
            "cutoff": 0.42,
            "resonance": 0.45,
            "mix": 0.72
        },
        "code": """param float drive = 2.4 [1.0, 6.0];
param float cutoff = 0.42 [0.05, 0.95];
param float resonance = 0.45 [0.0, 0.85];
param float mix = 0.72 [0.0, 1.0];
state float lp1 = 0;
state float lp2 = 0;

u = in * drive + 0.06;
sat = (u / (1.0 + abs(u) * 0.55)) - 0.04;

fCut = clamp(0.04 + cutoff * 0.38, 0.02, 0.44);
fb = clamp(resonance * 1.35, 0.0, 1.15);
lp1 = lp1 + fCut * (sat - lp2 * fb * 0.35 - lp1);
lp2 = lp2 + fCut * (lp1 - lp2);

processed = in * (1.0 - mix) + lp2 * mix;
out = processed * 1.35;
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel & Embeds Sources (Theme: suprematist_blocks)
#    Palette: #f4efe6 (warm ivory), #111111 (charcoal ink), #d9231f (vermilion)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "suprematist-blocks-kinetic-red-wedge",
        "title": "Suprematist Blocks Kinetic Red Wedge",
        "description": "Constructivist composition of a vermilion wedge piercing a charcoal disc and diagonal beams on warm ivory paper.",
        "embed_desc": "Audio-reactive Suprematist composition inspired by El Lissitzky. Bass drives the vermilion wedge thrust, mids rotate the constructivist beams, and highs pulse the geometric accents.",
        "tags": ["suprematist", "constructivist", "lissitzky", "malevich", "geometric", "shader", "webgl", "audio-reactive"],
        "params": {
            "wedgeThrust": 0.18,
            "beamAngle": 0.58,
            "discRadius": 0.32,
            "paperGrain": 0.04,
            "contrast": 1.05
        },
        "code": """param float wedgeThrust = 0.18 [-0.2, 0.5];
param float beamAngle = 0.58 [0.1, 1.4];
param float discRadius = 0.32 [0.15, 0.5];
param float paperGrain = 0.04 [0.0, 0.12];
param float contrast = 1.05 [0.8, 1.4];

p = (uv - 0.5) * 2.0;

# Warm paper ivory (#f4efe6), charcoal ink (#111111), constructivist vermilion (#d9231f)
cPaper = vec3(0.957, 0.937, 0.902);
cInk = vec3(0.067, 0.067, 0.067);
cRed = vec3(0.851, 0.137, 0.122);

# Subtle paper grain
grain = (fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * paperGrain;
bg = cPaper + grain;

# Charcoal disc slightly right of center
dDisc = length(p - vec2(0.18, -0.05)) - discRadius;
mDisc = 1.0 - smoothstep(-0.008, 0.008, dDisc);

# Kinetic red wedge piercing from upper-left
ang = beamAngle + sin(t * 0.35) * 0.05;
ca = cos(ang);
sa = sin(ang);
q = vec2(p.x * ca + p.y * sa, -p.x * sa + p.y * ca);
shiftX = q.x - wedgeThrust + 0.15 * sin(t * 0.5);
halfW = clamp(0.28 - shiftX * 0.38, 0.0, 0.45);
inWedgeX = step(-0.75, shiftX) * step(shiftX, 0.55);
mWedge = inWedgeX * (1.0 - smoothstep(halfW - 0.008, halfW + 0.008, abs(q.y)));

# Diagonal charcoal structural bar
q2 = vec2(p.x * cos(-0.42) + p.y * sin(-0.42), -p.x * sin(-0.42) + p.y * cos(-0.42));
mBar = (1.0 - smoothstep(0.045, 0.053, abs(q2.y + 0.36))) * (1.0 - smoothstep(0.65, 0.66, abs(q2.x)));

# Small floating vermilion square accent
dSq = max(abs(p.x - 0.52), abs(p.y - 0.46)) - 0.09;
mSq = 1.0 - smoothstep(-0.008, 0.008, dSq);

resCol = mix(bg, cInk, mDisc);
resCol = mix(resCol, cInk, mBar);
resCol = mix(resCol, cRed, mWedge);
resCol = mix(resCol, cRed, mSq);

col = clamp(resCol * contrast, 0.0, 1.0);
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
  vec2 p = (uv - 0.5) * 2.0;
  p.x *= aspect;

  float b = audio_bass * 0.35;
  float m = audio_mid * 0.35;
  float h = audio_high * 0.35;

  vec3 cPaper = vec3(0.957, 0.937, 0.902);
  vec3 cInk = vec3(0.067, 0.067, 0.067);
  vec3 cRed = vec3(0.851, 0.137, 0.122);

  float grain = (fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * 0.04;
  vec3 bg = cPaper + grain;

  float dDisc = length(p - vec2(0.18, -0.05)) - (0.32 + b * 0.12);
  float mDisc = 1.0 - smoothstep(-0.008, 0.008, dDisc);

  float ang = 0.58 + sin(t * 0.35) * 0.05 + m * 0.18;
  float ca = cos(ang);
  float sa = sin(ang);
  vec2 q = vec2(p.x * ca + p.y * sa, -p.x * sa + p.y * ca);
  float shiftX = q.x - (0.18 + b * 0.25) + 0.15 * sin(t * 0.5);
  float halfW = clamp(0.28 - shiftX * 0.38, 0.0, 0.45);
  float inWedgeX = step(-0.75, shiftX) * step(shiftX, 0.55);
  float mWedge = inWedgeX * (1.0 - smoothstep(halfW - 0.008, halfW + 0.008, abs(q.y)));

  vec2 q2 = vec2(p.x * cos(-0.42) + p.y * sin(-0.42), -p.x * sin(-0.42) + p.y * cos(-0.42));
  float mBar = (1.0 - smoothstep(0.045, 0.053, abs(q2.y + 0.36))) * (1.0 - smoothstep(0.65, 0.66, abs(q2.x)));

  float dSq = max(abs(p.x - 0.52), abs(p.y - 0.46)) - (0.09 + h * 0.06);
  float mSq = 1.0 - smoothstep(-0.008, 0.008, dSq);

  vec3 resCol = mix(bg, cInk, mDisc);
  resCol = mix(resCol, cInk, mBar);
  resCol = mix(resCol, cRed, mWedge);
  resCol = mix(resCol, cRed, mSq);

  gl_FragColor = vec4(clamp(resCol * 1.05, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "suprematist-blocks-malevich-floating-plane",
        "title": "Suprematist Blocks Malevich Floating Plane",
        "description": "Balanced asymmetric field of a rotated black quadrilateral and vermilion satellite bars suspended over warm paper ivory.",
        "embed_desc": "Audio-reactive Malevich-inspired Suprematist planes. Bass pulses the central black quadrilateral, mids drift the vermilion satellite bars, and highs sharpen the edge tension.",
        "tags": ["suprematist", "malevich", "geometric", "minimal", "constructivist", "shader", "webgl", "audio-reactive"],
        "params": {
            "planeSize": 0.42,
            "tilt": 0.32,
            "barSpread": 0.52,
            "paperGrain": 0.04,
            "contrast": 1.05
        },
        "code": """param float planeSize = 0.42 [0.2, 0.65];
param float tilt = 0.32 [-0.8, 0.8];
param float barSpread = 0.52 [0.2, 0.8];
param float paperGrain = 0.04 [0.0, 0.12];
param float contrast = 1.05 [0.8, 1.4];

p = (uv - 0.5) * 2.0;

cPaper = vec3(0.957, 0.937, 0.902);
cInk = vec3(0.067, 0.067, 0.067);
cRed = vec3(0.851, 0.137, 0.122);

grain = (fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * paperGrain;
bg = cPaper + grain;

# Central tilted black quadrilateral
a = tilt + sin(t * 0.25) * 0.04;
ca = cos(a);
sa = sin(a);
q = vec2(p.x * ca + p.y * sa, -p.x * sa + p.y * ca);
dQuad = max(abs(q.x), abs(q.y)) - planeSize;
mQuad = 1.0 - smoothstep(-0.008, 0.008, dQuad);

# Vermilion floating bar above-left
b1 = vec2(p.x + barSpread * 0.72, p.y - barSpread * 0.85);
r1 = vec2(b1.x * ca + b1.y * sa, -b1.x * sa + b1.y * ca);
mBar1 = (1.0 - smoothstep(-0.008, 0.008, max(abs(r1.x) - 0.28, abs(r1.y) - 0.055)));

# Charcoal thin cross-bar below-right
b2 = vec2(p.x - barSpread * 0.68, p.y + barSpread * 0.75);
r2 = vec2(b2.x * ca + b2.y * sa, -b2.x * sa + b2.y * ca);
mBar2 = (1.0 - smoothstep(-0.008, 0.008, max(abs(r2.x) - 0.36, abs(r2.y) - 0.032)));

# Small vermilion circle orbit
dOrb = length(p - vec2(0.56, 0.22 + 0.06 * cos(t * 0.4))) - 0.09;
mOrb = 1.0 - smoothstep(-0.008, 0.008, dOrb);

resCol = mix(bg, cInk, mQuad);
resCol = mix(resCol, cInk, mBar2);
resCol = mix(resCol, cRed, mBar1);
resCol = mix(resCol, cRed, mOrb);

col = clamp(resCol * contrast, 0.0, 1.0);
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
  vec2 p = (uv - 0.5) * 2.0;
  p.x *= aspect;

  float b = audio_bass * 0.25;
  float m = audio_mid * 0.25;
  float h = audio_high * 0.25;

  vec3 cPaper = vec3(0.957, 0.937, 0.902);
  vec3 cInk = vec3(0.067, 0.067, 0.067);
  vec3 cRed = vec3(0.851, 0.137, 0.122);

  float grain = (fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * 0.04;
  vec3 bg = cPaper + grain;

  float a = 0.32 + sin(t * 0.25) * 0.04 + m * 0.12;
  float ca = cos(a);
  float sa = sin(a);
  vec2 q = vec2(p.x * ca + p.y * sa, -p.x * sa + p.y * ca);
  float dQuad = max(abs(q.x), abs(q.y)) - (0.42 + b * 0.12);
  float mQuad = 1.0 - smoothstep(-0.008, 0.008, dQuad);

  vec2 b1 = vec2(p.x + 0.52 * 0.72, p.y - 0.52 * 0.85);
  vec2 r1 = vec2(b1.x * ca + b1.y * sa, -b1.x * sa + b1.y * ca);
  float mBar1 = (1.0 - smoothstep(-0.008, 0.008, max(abs(r1.x) - (0.28 + m * 0.14), abs(r1.y) - 0.055)));

  vec2 b2 = vec2(p.x - 0.52 * 0.68, p.y + 0.52 * 0.75);
  vec2 r2 = vec2(b2.x * ca + b2.y * sa, -b2.x * sa + b2.y * ca);
  float mBar2 = (1.0 - smoothstep(-0.008, 0.008, max(abs(r2.x) - 0.36, abs(r2.y) - 0.032)));

  float dOrb = length(p - vec2(0.56, 0.22 + 0.06 * cos(t * 0.4))) - (0.09 + h * 0.06);
  float mOrb = 1.0 - smoothstep(-0.008, 0.008, dOrb);

  vec3 resCol = mix(bg, cInk, mQuad);
  resCol = mix(resCol, cInk, mBar2);
  resCol = mix(resCol, cRed, mBar1);
  resCol = mix(resCol, cRed, mOrb);

  gl_FragColor = vec4(clamp(resCol * 1.05, 0.0, 1.0), 1.0);
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
  c bg 0.03 0.04 0.06
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
  i note2 {nr+3}
  i note3 {nr+7}
  i note4 {nr}
  i note5 {nr+3}
  i note6 {nr+12}
  i note7 {nr+7}
  i note8 {nr}
  i note9 {nr}
  i note10 {nr+3}
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
                "tags": ["synth", "downtempo", "audio", "dsp", "bass", "rhodes", "pad"],
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
                "tags": ["effect", "minimal-techno", "audio", "dsp", "delay", "dub", "shaper"],
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
