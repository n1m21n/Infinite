#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 43.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: vector_wireframe -> vector-wireframe-trench-horizon, vector-wireframe-icosahedron-radar)
2. Sketch 3D (Theme: generative_plotter -> generative-plotter-molnar-cubes-3d, generative-plotter-mohr-hypercube-3d)
3. Field Synth (Theme: future_garage -> future-garage-sub-reese-bass-synth, future-garage-glass-rhodes-chord-synth)
4. Field Effects (Theme: acid_house -> acid-house-303-resonant-squelch-fx, acid-house-tape-dub-delay-fx)
5. Field Pixel & Embeds (Theme: ukiyoe_cartography -> ukiyoe-cartography-archipelago-contours, ukiyoe-cartography-compass-tide-chart)
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

TMP_DIR = "/tmp/pack43_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: vector_wireframe)
#    Palette: #000000 (phosphor void), #00ff6a (Vectrex CRT green), #ffffff (beam white)
#    Refs: Elite (1984), Battlezone, Tempest, Vectrex CRT
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "vector-wireframe-trench-horizon",
        "title": "Vector Wireframe Trench Horizon",
        "description": "Perspective CRT vector trench corridor with receding horizon grid, targeting reticle, and phosphor scanlines.",
        "tags": ["vector-wireframe", "vectrex", "crt", "wireframe", "perspective", "retro", "procedural"],
        "code": """// Vector Wireframe Trench Horizon - Infinite Library (Sketch 2D)
param("grid_depth", 14, 6, 24);
param("trench_width", 0.42, 0.2, 0.7);
param("reticle_size", 0.16, 0.06, 0.3);
param("scroll_speed", 0.85, 0.0, 2.5);

function draw(t) {
  background(0.015, 0.048, 0.028);

  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.48;
  const nd = floor(grid_depth);

  // Subtle CRT phosphor horizon band
  noStroke();
  fill(0.0, 0.35, 0.16, 0.22);
  rect(w * 0.04, cy - h * 0.08, w * 0.92, h * 0.16);
  fill(0.0, 0.55, 0.25, 0.16);
  circle(cx, cy, min(w, h) * 0.42);

  // Outer CRT bezel frame
  noFill();
  stroke(0.0, 1.0, 0.416, 0.7);
  strokeWeight(3.5);
  rect(w * 0.04, h * 0.04, w * 0.92, h * 0.92);

  // Horizon line
  stroke(1.0, 1.0, 1.0, 0.85);
  strokeWeight(2.0);
  line(w * 0.04, cy, w * 0.96, cy);

  // Receding perspective floor & ceiling rays
  const rays = 12;
  for (let i = -rays; i <= rays; i++) {
    const u = i / rays;
    const xEnd = cx + u * w * 0.72;
    const alpha = Math.abs(i) <= 2 ? 0.9 : 0.45;
    stroke(0.0, 1.0, 0.416, alpha);
    strokeWeight(Math.abs(i) % 3 === 0 ? 2.2 : 1.3);
    line(cx + u * w * 0.03, cy, xEnd, h * 0.96);
    line(cx + u * w * 0.03, cy, xEnd, h * 0.04);
  }

  // Scrolling perspective depth ribs
  const phase = (t * scroll_speed) % 1.0;
  for (let d = 0; d < nd; d++) {
    const u = (d + phase) / nd;
    const z = Math.pow(u, 2.1);
    const yOff = z * h * 0.48;
    const xHalf = (0.04 + z * trench_width) * w;

    stroke(0.0, 1.0, 0.416, 0.25 + z * 0.72);
    strokeWeight(1.2 + z * 2.2);

    // Lower floor rib & upper ceiling rib
    line(w * 0.04, cy + yOff, w * 0.96, cy + yOff);
    line(w * 0.04, cy - yOff, w * 0.96, cy - yOff);

    // Trench wall vertical struts
    stroke(1.0, 1.0, 1.0, 0.2 + z * 0.65);
    line(cx - xHalf, cy - yOff, cx - xHalf, cy + yOff);
    line(cx + xHalf, cy - yOff, cx + xHalf, cy + yOff);
  }

  // Central vector targeting reticle
  const rr = min(w, h) * reticle_size;
  noFill();
  stroke(1.0, 1.0, 1.0, 0.95);
  strokeWeight(2.4);
  circle(cx, cy, rr * 2.0);
  stroke(0.0, 1.0, 0.416, 0.95);
  strokeWeight(2.0);
  circle(cx, cy, rr * 1.15);

  // Diamond crosshairs
  const dOff = rr * 1.35;
  line(cx - dOff, cy, cx - rr * 0.45, cy);
  line(cx + rr * 0.45, cy, cx + dOff, cy);
  line(cx, cy - dOff, cx, cy - rr * 0.45);
  line(cx, cy + rr * 0.45, cx, cy + dOff);

  // Corner telemetry ticks
  stroke(0.0, 1.0, 0.416, 0.75);
  strokeWeight(2.0);
  for (let k = 0; k < 6; k++) {
    const ty = h * 0.12 + k * 24;
    const barW = 28 + ((k * 17) % 38);
    line(w * 0.07, ty, w * 0.07 + barW, ty);
    line(w * 0.93 - barW, ty, w * 0.93, ty);
  }
}
"""
    },
    {
        "slug": "vector-wireframe-icosahedron-radar",
        "title": "Vector Wireframe Icosahedron Radar",
        "description": "Rotating 3D-projected vector wireframe icosahedron inside concentric Vectrex radar rings and cardinal azimuth ticks.",
        "tags": ["vector-wireframe", "icosahedron", "radar", "crt", "vectrex", "geometry", "procedural"],
        "code": """// Vector Wireframe Icosahedron Radar - Infinite Library (Sketch 2D)
param("ring_count", 6, 3, 10);
param("spin_speed", 0.55, 0.0, 2.0);
param("poly_scale", 0.28, 0.14, 0.42);
param("tick_density", 36, 12, 72);

function draw(t) {
  background(0.015, 0.048, 0.028);

  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const maxR = min(w, h) * 0.44;
  const nr = floor(ring_count);
  const nt = floor(tick_density);

  // Subtle CRT phosphor radar screen glow
  noStroke();
  fill(0.0, 0.32, 0.15, 0.22);
  circle(cx, cy, maxR * 2.0);
  fill(0.0, 0.55, 0.24, 0.18);
  circle(cx, cy, maxR * 1.15);

  // Concentric radar range rings
  noFill();
  for (let i = 1; i <= nr; i++) {
    const r = (i / nr) * maxR;
    stroke(0.0, 1.0, 0.416, i === nr ? 0.92 : 0.48);
    strokeWeight(i === nr ? 3.2 : 2.0);
    circle(cx, cy, r * 2.0);
  }

  // Azimuthal ticks around outer ring
  for (let i = 0; i < nt; i++) {
    const ang = (i / nt) * Math.PI * 2.0;
    const isMajor = (i % 3 === 0);
    const r0 = maxR * (isMajor ? 0.92 : 0.96);
    const r1 = maxR * 1.02;
    stroke(isMajor ? 1.0 : 0.0, 1.0, isMajor ? 1.0 : 0.416, isMajor ? 0.9 : 0.5);
    strokeWeight(isMajor ? 2.2 : 1.2);
    line(cx + Math.cos(ang) * r0, cy + Math.sin(ang) * r0, cx + Math.cos(ang) * r1, cy + Math.sin(ang) * r1);
  }

  // Rotating radar sweep line
  const sweepAng = t * spin_speed * 1.4;
  stroke(0.0, 1.0, 0.416, 0.75);
  strokeWeight(2.0);
  line(cx, cy, cx + Math.cos(sweepAng) * maxR, cy + Math.sin(sweepAng) * maxR);

  // 3D-projected icosahedron vertices
  const phi = 1.6180339887;
  const rawVerts = [
    [-1, phi, 0], [1, phi, 0], [-1, -phi, 0], [1, -phi, 0],
    [0, -1, phi], [0, 1, phi], [0, -1, -phi], [0, 1, -phi],
    [phi, 0, -1], [phi, 0, 1], [-phi, 0, -1], [-phi, 0, 1]
  ];
  const edges = [
    [0,11],[0,5],[0,1],[0,7],[0,10],[1,5],[5,11],[11,10],[10,7],[7,1],
    [3,9],[3,4],[3,2],[3,6],[3,8],[4,9],[2,4],[6,2],[8,6],[9,8],
    [1,9],[5,4],[11,2],[10,6],[7,8],[9,5],[4,11],[2,10],[6,7],[8,1]
  ];

  const ay = t * spin_speed + 0.45;
  const ax = t * spin_speed * 0.62 + 0.35;
  const cyA = Math.cos(ay), syA = Math.sin(ay);
  const cxA = Math.cos(ax), sxA = Math.sin(ax);
  const sc = min(w, h) * poly_scale * 0.58;

  const proj = [];
  for (let i = 0; i < rawVerts.length; i++) {
    const vx = rawVerts[i][0], vy = rawVerts[i][1], vz = rawVerts[i][2];
    const x1 = vx * cyA - vz * syA;
    const z1 = vx * syA + vz * cyA;
    const y2 = vy * cxA - z1 * sxA;
    const z2 = vy * sxA + z1 * cxA;
    const persp = 4.2 / (4.2 - z2 * 0.45);
    proj.push([cx + x1 * sc * persp, cy + y2 * sc * persp, z2]);
  }

  // Draw wireframe edges with depth-weighted phosphor intensity
  for (let e = 0; e < edges.length; e++) {
    const a = proj[edges[e][0]];
    const b = proj[edges[e][1]];
    const zAvg = (a[2] + b[2]) * 0.5;
    const front = zAvg > 0.0;
    if (front) {
      stroke(1.0, 1.0, 1.0, 0.95);
      strokeWeight(2.8);
    } else {
      stroke(0.0, 1.0, 0.416, 0.55);
      strokeWeight(1.8);
    }
    line(a[0], a[1], b[0], b[1]);
  }

  // Vertex phosphor nodes
  noStroke();
  for (let i = 0; i < proj.length; i++) {
    const p = proj[i];
    fill(p[2] > 0 ? 1.0 : 0.0, 1.0, p[2] > 0 ? 1.0 : 0.416, 0.95);
    circle(p[0], p[1], p[2] > 0 ? 8 : 5);
  }
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: generative_plotter)
#    Palette: #f5f1e6 (archival cream), #111111 (plotter ink), #d1301f (vermilion)
#    Refs: Vera Molnar, Manfred Mohr, Frieder Nake
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "generative-plotter-molnar-cubes-3d",
        "title": "Generative Plotter Molnar Cubes 3D",
        "description": "Vera Molnar-inspired 3D grid of stepped cubic frames in technical ink and vermilion accent on archival cream paper.",
        "tags": ["generative-plotter", "molnar", "plotter", "3d", "cubes", "algorithmic", "procedural"],
        "code": """// Generative Plotter Molnar Cubes 3D - Infinite Library (Sketch 3D)
param("grid_size", 4, 3, 6);
param("jitter_amt", 0.16, 0.0, 0.35);
param("tilt_angle", 0.52, 0.1, 1.1);
param("spin_speed", 0.25, 0.0, 1.0);

function draw(t) {
  rotateX(tilt_angle);
  rotateY(t * spin_speed + 0.62);

  const ng = floor(grid_size);
  const spacing = 0.62;
  const offset = (ng - 1) * spacing * 0.5;

  // Outer plotter registration frame
  push();
  fill(0.067, 0.067, 0.067);
  torus(1.55, 0.018, 48, 8);
  pop();

  for (let ix = 0; ix < ng; ix++) {
    for (let iz = 0; iz < ng; iz++) {
      const seed = ix * 17.3 + iz * 31.7;
      const jx = Math.sin(seed + t * 0.3) * jitter_amt;
      const jz = Math.cos(seed * 1.4 - t * 0.25) * jitter_amt;
      const hStep = 0.28 + ((ix + iz * 2) % 3) * 0.24;
      const isAccent = ((ix + iz) % 4 === 1);

      push();
      translate(ix * spacing - offset + jx, 0, iz * spacing - offset + jz);
      rotateY(Math.sin(seed) * 0.28);

      if (isAccent) {
        fill(0.820, 0.188, 0.122); // #d1301f vermilion
      } else {
        fill(0.067, 0.067, 0.067); // #111111 technical ink
      }

      // Stepped plotter frame bars
      box(0.42, 0.05, 0.42);
      translate(0, hStep * 0.5, 0);
      box(0.06, hStep, 0.06);
      translate(0, hStep * 0.5, 0);
      box(0.32, 0.05, 0.32);
      pop();
    }
  }
}
"""
    },
    {
        "slug": "generative-plotter-mohr-hypercube-3d",
        "title": "Generative Plotter Mohr Hypercube 3D",
        "description": "Manfred Mohr-inspired algorithmic hypercube lattice of intersecting orthogonal beams and vermilion node markers.",
        "tags": ["generative-plotter", "mohr", "hypercube", "lattice", "3d", "plotter", "procedural"],
        "code": """// Generative Plotter Mohr Hypercube 3D - Infinite Library (Sketch 3D)
param("subdivisions", 3, 2, 5);
param("beam_thick", 0.045, 0.02, 0.09);
param("phase_shift", 0.45, 0.0, 1.5);
param("spin_speed", 0.30, 0.0, 1.0);

function draw(t) {
  rotateX(0.48 + Math.sin(t * 0.3) * 0.12);
  rotateY(t * spin_speed + 0.78);

  const ns = floor(subdivisions);
  const span = 1.35;
  const bt = beam_thick;

  // Outer hypercube skeleton
  for (let sx = -1; sx <= 1; sx += 2) {
    for (let sy = -1; sy <= 1; sy += 2) {
      push();
      fill(0.067, 0.067, 0.067);
      translate(sx * span * 0.5, sy * span * 0.5, 0);
      box(bt, bt, span);
      pop();

      push();
      fill(0.067, 0.067, 0.067);
      translate(sx * span * 0.5, 0, sy * span * 0.5);
      box(bt, span, bt);
      pop();

      push();
      fill(0.067, 0.067, 0.067);
      translate(0, sx * span * 0.5, sy * span * 0.5);
      box(span, bt, bt);
      pop();
    }
  }

  // Inner algorithmic subdivisions & vermilion nodes
  for (let i = 0; i < ns; i++) {
    const u = (i + 0.5) / ns - 0.5;
    const p = u * span * 0.85;
    const rot = phase_shift * (i + 1) + t * 0.2;

    push();
    rotateZ(rot * 0.35);
    fill(0.067, 0.067, 0.067);
    translate(p, 0, 0);
    box(bt * 0.85, span * 0.75, bt * 0.85);
    pop();

    push();
    fill(0.820, 0.188, 0.122); // #d1301f vermilion node
    translate(p, Math.sin(rot) * 0.32, Math.cos(rot) * 0.32);
    sphere(0.11, 16, 12);
    pop();
  }

  // Central diagonal axis strut
  push();
  rotateZ(0.785);
  rotateX(0.615);
  fill(0.820, 0.188, 0.122);
  cylinder(0.032, span * 1.45, 16);
  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: future_garage)
#    130-135 BPM, swing 0.3, scale [1, 4], Refs: Burial, Mount Kimbie, Four Tet
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "future-garage-sub-reese-bass-synth",
        "title": "Future Garage Sub Reese Bass Synth",
        "description": "Detuned dual-saw sub-bass synthesizer with warm lowpass filter drift and subtle saturation for UK garage basslines.",
        "note_root": 44,
        "params": {
            "detune": 0.016,
            "cutoff": 520.0,
            "subLevel": 0.65,
            "drive": 1.35,
            "decay": 0.65
        },
        "code": """param float detune = 0.016 [0.002, 0.045];
param float cutoff = 520.0 [120.0, 2200.0];
param float subLevel = 0.65 [0.0, 1.0];
param float drive = 1.35 [1.0, 3.0];
param float decay = 0.65 [0.15, 1.8];
state float p1 = 0;
state float p2 = 0;
state float pSub = 0;
state float lfo = 0;
state float env = 0;
state float lp1 = 0;
state float lp2 = 0;

target = if(gate > 0.5, 1.0, 0.0);
decayRate = clamp(1.0 / (sr * decay), 0.0001, 0.03);
env = if(target > env, env + (target - env) * 0.09, env - env * decayRate);

lfo = (lfo + 0.65 / sr) % 1.0;
drift = 1.0 + sin(6.2831853 * lfo) * 0.18;

f0 = clamp(freq, 30.0, 1600.0);
p1 = (p1 + f0 * (1.0 - detune) / sr) % 1.0;
p2 = (p2 + f0 * (1.0 + detune) / sr) % 1.0;
pSub = (pSub + (f0 * 0.5) / sr) % 1.0;

saw1 = 2.0 * p1 - 1.0;
saw2 = 2.0 * p2 - 1.0;
sub = sin(6.2831853 * pSub);
raw = (saw1 + saw2) * 0.36 + sub * subLevel;

fCut = clamp((cutoff * drift + env * 340.0) / sr * 3.14, 0.008, 0.42);
lp1 = lp1 + fCut * (raw - lp1);
lp2 = lp2 + fCut * (lp1 - lp2);

sat = (lp2 * drive) / (1.0 + abs(lp2 * drive) * 0.4);
out = sat * env * 0.45;
"""
    },
    {
        "slug": "future-garage-glass-rhodes-chord-synth",
        "title": "Future Garage Glass Rhodes Chord Synth",
        "description": "Shimmering bell-harmonic electric piano synthesizer with soft stereo chorus drift and gentle envelope decay.",
        "note_root": 56,
        "params": {
            "bell": 0.52,
            "detune": 0.009,
            "warmth": 1850.0,
            "shimmer": 0.40,
            "decay": 0.90
        },
        "code": """param float bell = 0.52 [0.0, 1.2];
param float detune = 0.009 [0.0, 0.03];
param float warmth = 1850.0 [350.0, 4500.0];
param float shimmer = 0.40 [0.0, 1.0];
param float decay = 0.90 [0.2, 2.2];
state float p1 = 0;
state float p2 = 0;
state float pBell = 0;
state float env = 0;
state float lp = 0;

target = if(gate > 0.5, 1.0, 0.0);
decayRate = clamp(1.0 / (sr * decay), 0.0001, 0.03);
env = if(target > env, env + (target - env) * 0.08, env - env * decayRate);

f0 = clamp(freq, 40.0, 2800.0);
p1 = (p1 + f0 * (1.0 - detune) / sr) % 1.0;
p2 = (p2 + f0 * (1.0 + detune) / sr) % 1.0;
pBell = (pBell + (f0 * 4.0) / sr) % 1.0;

modSig = sin(6.2831853 * pBell) * bell * env * env;
v1 = sin(6.2831853 * p1 + modSig);
v2 = sin(6.2831853 * p2 + modSig * shimmer);
raw = (v1 + v2) * 0.48;

fCut = clamp(warmth / sr * 3.14, 0.01, 0.44);
lp = lp + fCut * (raw - lp);

out = lp * env * 1.15;
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: acid_house)
#    120-130 BPM, Refs: Phuture (Acid Tracks 1987), Roland TB-303, Hardfloor
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "acid-house-303-resonant-squelch-fx",
        "title": "Acid House 303 Resonant Squelch FX",
        "description": "Resonant 2-pole diode-style lowpass filter with LFO cutoff sweep and acid overdrive saturation.",
        "params": {
            "cutoff": 0.24,
            "resonance": 0.68,
            "sweepRate": 0.65,
            "drive": 1.6,
            "mix": 0.78
        },
        "code": """param float cutoff = 0.24 [0.04, 0.55];
param float resonance = 0.68 [0.0, 0.88];
param float sweepRate = 0.65 [0.1, 4.0];
param float drive = 1.6 [1.0, 3.5];
param float mix = 0.78 [0.0, 1.0];
state float lfo = 0;
state float lp1 = 0;
state float lp2 = 0;

lfo = (lfo + sweepRate / sr) % 1.0;
modCut = clamp(cutoff + sin(6.2831853 * lfo) * 0.14, 0.03, 0.52);

fb = clamp(resonance * 0.82, 0.0, 0.78);
sig = in - lp2 * fb;
satIn = (sig * drive) / (1.0 + abs(sig * drive) * 0.45);

lp1 = lp1 + modCut * (satIn - lp1);
lp2 = lp2 + modCut * (lp1 - lp2);

processed = in * (1.0 - mix) + lp2 * mix;
out = processed * 0.92;
"""
    },
    {
        "slug": "acid-house-tape-dub-delay-fx",
        "title": "Acid House Tape Dub Delay FX",
        "description": "Multi-tap acid dub delay with resonant feedback, high-frequency damping, and warm drive.",
        "params": {
            "feedback": 0.54,
            "damping": 0.44,
            "drive": 1.45,
            "mix": 0.52
        },
        "code": """param float feedback = 0.54 [0.0, 0.82];
param float damping = 0.44 [0.1, 0.92];
param float drive = 1.45 [1.0, 3.5];
param float mix = 0.52 [0.0, 1.0];
state float tap1 = 0;
state float tap2 = 0;
state float tap3 = 0;
state float lp = 0;

fCut = clamp(0.12 + (1.0 - damping) * 0.28, 0.03, 0.42);
lp = lp + fCut * (in - lp);

tap1 = tap1 + 0.072 * (lp + tap3 * feedback * 0.52 - tap1);
tap2 = tap2 + 0.056 * (tap1 - tap2);
tap3 = tap3 + 0.046 * (tap2 - tap3);

echo = (tap1 * 0.45 + tap2 * 0.35 + tap3 * 0.25) * drive;
sat = echo / (1.0 + abs(echo) * 0.38);

processed = in * (1.0 - mix) + sat * mix;
out = processed * 0.94;
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel & Embeds Sources (Theme: ukiyoe_cartography)
#    Palette: #f1faee (washi paper), #1d3557 (indigo ink), #a8dadc (cerulean sea), #e63946 (hanko vermilion)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "ukiyoe-cartography-archipelago-contours",
        "title": "Ukiyoe Cartography Archipelago Contours",
        "description": "Topographical Edo woodblock archipelago map with animated contour ripples, rhumb lines, and a vermilion hanko seal.",
        "embed_desc": "Audio-reactive Ukiyo-e woodblock cartography map. Bass swells the island elevation contours, mids drift the sea currents, and highs illuminate the rhumb lines and hanko seal.",
        "tags": ["ukiyoe", "cartography", "woodblock", "contours", "map", "shader", "webgl", "audio-reactive"],
        "params": {
            "contourFreq": 18.0,
            "islandScale": 1.45,
            "rhumbCount": 8.0,
            "grainAmt": 0.04,
            "seaTint": 0.55
        },
        "code": """param float contourFreq = 18.0 [8.0, 32.0];
param float islandScale = 1.45 [0.8, 2.5];
param float rhumbCount = 8.0 [4.0, 16.0];
param float grainAmt = 0.04 [0.0, 0.12];
param float seaTint = 0.55 [0.1, 1.0];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0;

# Ukiyo-e cartography palette
cPaper = vec3(0.945, 0.922, 0.855);
cIndigo = vec3(0.114, 0.208, 0.341);
cSea = vec3(0.659, 0.855, 0.863);
cSeal = vec3(0.902, 0.224, 0.275);

# Topographical elevation field
q = p * islandScale;
h1 = sin(q.x * 2.1 + sin(q.y * 1.7 + t * 0.25)) * cos(q.y * 2.0 - t * 0.15);
h2 = 0.5 * sin(length(q - vec2(0.35, -0.25)) * 3.8 - t * 0.3);
elev = h1 + h2;

# Woodblock contour bands and fine ink lines
band = smoothstep(-0.08, 0.08, elev);
contourWave = abs(fract(elev * contourFreq * 0.25) - 0.5);
inkLine = 1.0 - smoothstep(0.03, 0.09, contourWave);

# Cartographic rhumb lines from map center
ang = atan2(p.y, p.x);
rhumb = 1.0 - smoothstep(0.015, 0.045, abs(sin(ang * rhumbCount)));

# Paper fibre grain
grain = (fract(sin(dot(uv * res, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * grainAmt;

baseCol = mix(mix(cPaper, cSea, seaTint * 0.72), cPaper, band);
baseCol = mix(baseCol, cIndigo, inkLine * 0.78);
baseCol = mix(baseCol, cIndigo, rhumb * 0.24);

# Vermilion hanko seal stamp in lower-right quadrant
sealPos = abs(p - vec2(0.56, -0.56));
inSeal = step(max(sealPos.x, sealPos.y), 0.14);
sealInner = step(max(sealPos.x, sealPos.y), 0.105) * step(0.025, abs(sealPos.x - 0.03));
baseCol = mix(baseCol, cSeal, inSeal * 0.92);
baseCol = mix(baseCol, cPaper, sealInner * 0.75);

col = clamp(baseCol + grain, 0.0, 1.0);
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

  float b = audio_bass * 0.45;
  float m = audio_mid * 0.45;
  float h = audio_high * 0.45;

  vec3 cPaper = vec3(0.945, 0.922, 0.855);
  vec3 cIndigo = vec3(0.114, 0.208, 0.341);
  vec3 cSea = vec3(0.659, 0.855, 0.863);
  vec3 cSeal = vec3(0.902, 0.224, 0.275);

  vec2 q = p * (1.45 - b * 0.22);
  float h1 = sin(q.x * 2.1 + sin(q.y * 1.7 + t * 0.25 + m)) * cos(q.y * 2.0 - t * 0.15);
  float h2 = 0.5 * sin(length(q - vec2(0.35, -0.25)) * 3.8 - t * 0.3);
  float elev = h1 + h2 + b * 0.25;

  float band = smoothstep(-0.08, 0.08, elev);
  float contourWave = abs(fract(elev * (18.0 + h * 6.0) * 0.25) - 0.5);
  float inkLine = 1.0 - smoothstep(0.03, 0.09, contourWave);

  float ang = atan(p.y, p.x);
  float rhumb = 1.0 - smoothstep(0.015, 0.045, abs(sin(ang * 8.0)));

  float grain = (fract(sin(dot(uv * res, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * 0.04;

  vec3 baseCol = mix(mix(cPaper, cSea, 0.55 * 0.72), cPaper, band);
  baseCol = mix(baseCol, cIndigo, inkLine * 0.78);
  baseCol = mix(baseCol, cIndigo, rhumb * (0.24 + h * 0.3));

  vec2 sealPos = abs(p - vec2(0.56, -0.56));
  float inSeal = step(max(sealPos.x, sealPos.y), 0.14 + b * 0.02);
  float sealInner = step(max(sealPos.x, sealPos.y), 0.105) * step(0.025, abs(sealPos.x - 0.03));
  baseCol = mix(baseCol, cSeal, inSeal * 0.92);
  baseCol = mix(baseCol, cPaper, sealInner * 0.75);

  gl_FragColor = vec4(clamp(baseCol + grain, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "ukiyoe-cartography-compass-tide-chart",
        "title": "Ukiyoe Cartography Compass Tide Chart",
        "description": "Indigo woodblock nautical tide chart with a 16-point compass rose, Great Wave arc bands, and vermilion markers.",
        "embed_desc": "Audio-reactive Ukiyo-e nautical tide chart. Bass pulses the compass rose, mids roll the Great Wave arc bands, and highs sharpen the woodblock degree ticks.",
        "tags": ["ukiyoe", "compass", "cartography", "woodblock", "tide", "shader", "webgl", "audio-reactive"],
        "params": {
            "rosePoints": 16.0,
            "waveBands": 9.0,
            "ringRadius": 0.62,
            "grainAmt": 0.04,
            "inkWeight": 0.85
        },
        "code": """param float rosePoints = 16.0 [8.0, 32.0];
param float waveBands = 9.0 [4.0, 16.0];
param float ringRadius = 0.62 [0.35, 0.85];
param float grainAmt = 0.04 [0.0, 0.12];
param float inkWeight = 0.85 [0.4, 1.0];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0;

# Ukiyo-e nautical palette
cPaper = vec3(0.945, 0.922, 0.855);
cIndigo = vec3(0.114, 0.208, 0.341);
cSea = vec3(0.659, 0.855, 0.863);
cSeal = vec3(0.902, 0.224, 0.275);

r = length(p);
ang = atan2(p.y, p.x);

# Woodblock Great Wave scalloped tide bands
tide = sin(p.x * 5.2 + t * 0.45) * 0.12 + p.y;
wavePattern = abs(fract(tide * waveBands * 0.35) - 0.5);
waveMask = 1.0 - smoothstep(0.06, 0.12, wavePattern);
seaFill = smoothstep(0.0, 0.5, fract(tide * waveBands * 0.35));

# Compass rose radial star and degree rings
star = abs(cos(ang * rosePoints * 0.5 + t * 0.08));
roseShape = 1.0 - smoothstep(0.02, 0.06, r - (0.18 + 0.32 * pow(star, 3.0)));
outerRing = 1.0 - smoothstep(0.012, 0.032, abs(r - ringRadius));
innerRing = 1.0 - smoothstep(0.010, 0.028, abs(r - ringRadius * 0.58));

# Vermilion cardinal markers
cardinal = 1.0 - smoothstep(0.02, 0.04, abs(r - ringRadius)) * (1.0 - smoothstep(0.96, 0.99, abs(cos(ang * 2.0))));

grain = (fract(sin(dot(uv * res, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * grainAmt;

baseCol = mix(cPaper, cSea, seaFill * 0.55);
baseCol = mix(baseCol, cIndigo, waveMask * 0.42 * inkWeight);
baseCol = mix(baseCol, cPaper, smoothstep(ringRadius + 0.02, ringRadius - 0.02, r) * 0.62);
baseCol = mix(baseCol, cIndigo, (outerRing + innerRing) * inkWeight);
baseCol = mix(baseCol, cIndigo, roseShape * 0.85);
baseCol = mix(baseCol, cSeal, step(r, 0.11));

# Cardinal vermilion pips
pipN = step(length(p - vec2(0.0, ringRadius)), 0.055);
pipS = step(length(p - vec2(0.0, -ringRadius)), 0.055);
pipE = step(length(p - vec2(ringRadius, 0.0)), 0.055);
pipW = step(length(p - vec2(-ringRadius, 0.0)), 0.055);
baseCol = mix(baseCol, cSeal, clamp(pipN + pipS + pipE + pipW, 0.0, 1.0));

col = clamp(baseCol + grain, 0.0, 1.0);
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

  float b = audio_bass * 0.45;
  float m = audio_mid * 0.45;
  float h = audio_high * 0.45;

  vec3 cPaper = vec3(0.945, 0.922, 0.855);
  vec3 cIndigo = vec3(0.114, 0.208, 0.341);
  vec3 cSea = vec3(0.659, 0.855, 0.863);
  vec3 cSeal = vec3(0.902, 0.224, 0.275);

  float r = length(p);
  float ang = atan(p.y, p.x);

  float tide = sin(p.x * 5.2 + t * 0.45 + m * 2.0) * 0.12 + p.y;
  float wavePattern = abs(fract(tide * 9.0 * 0.35) - 0.5);
  float waveMask = 1.0 - smoothstep(0.06, 0.12, wavePattern);
  float seaFill = smoothstep(0.0, 0.5, fract(tide * 9.0 * 0.35));

  float ringRadius = 0.62 + b * 0.06;
  float star = abs(cos(ang * 8.0 + t * 0.08 + h * 0.3));
  float roseShape = 1.0 - smoothstep(0.02, 0.06, r - (0.18 + (0.32 + b * 0.1) * pow(star, 3.0)));
  float outerRing = 1.0 - smoothstep(0.012, 0.032, abs(r - ringRadius));
  float innerRing = 1.0 - smoothstep(0.010, 0.028, abs(r - ringRadius * 0.58));

  float grain = (fract(sin(dot(uv * res, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * 0.04;

  vec3 baseCol = mix(cPaper, cSea, seaFill * 0.55);
  baseCol = mix(baseCol, cIndigo, waveMask * 0.42 * 0.85);
  baseCol = mix(baseCol, cPaper, smoothstep(ringRadius + 0.02, ringRadius - 0.02, r) * 0.62);
  baseCol = mix(baseCol, cIndigo, (outerRing + innerRing) * 0.85);
  baseCol = mix(baseCol, cIndigo, roseShape * 0.85);
  baseCol = mix(baseCol, cSeal, step(r, 0.11 + b * 0.03));

  float pipN = step(length(p - vec2(0.0, ringRadius)), 0.055);
  float pipS = step(length(p - vec2(0.0, -ringRadius)), 0.055);
  float pipE = step(length(p - vec2(ringRadius, 0.0)), 0.055);
  float pipW = step(length(p - vec2(-ringRadius, 0.0)), 0.055);
  baseCol = mix(baseCol, cSeal, clamp(pipN + pipS + pipE + pipW, 0.0, 1.0));

  gl_FragColor = vec4(clamp(baseCol + grain, 0.0, 1.0), 1.0);
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
  c bg 0.9608 0.9451 0.9020
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
  i note2 {nr+3}
  i note3 {nr+7}
  i note4 {nr}
  i note5 {nr+3}
  i note6 {nr+10}
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
  f rateBeats 0.25
  i steps 16
  i note0 41
  i note1 41
  i note2 53
  i note3 48
  i note4 41
  i note5 53
  i note6 46
  i note7 48
  i note8 41
  i note9 41
  i note10 53
  i note11 48
  i note12 41
  i note13 53
  i note14 46
  i note15 51
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
                "tags": ["synth", "future-garage", "audio", "dsp", "bass", "rhodes", "uk-garage"],
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
                "tags": ["effect", "acid-house", "audio", "dsp", "filter", "delay", "303"],
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
