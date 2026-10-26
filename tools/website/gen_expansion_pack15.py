#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 15.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (generative-plotter-contour-flow, generative-plotter-spiral-lattice)
2. Sketch 3D (vector-wireframe-geodesic-sphere-3d, vector-wireframe-torus-knot-3d)
3. Field Synth (dub-techno-chord-stab-synth, dub-techno-sub-rumble-synth)
4. Field Effects (drone-kosmische-space-delay-fx, drone-kosmische-resonator-filter-fx)
5. Field Pixel (cymatics-sine-chladni-resonance, cymatics-sine-harmonic-ripples)
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

TMP_DIR = "/tmp/pack15_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library

# -------------------------------------------------------------
# 1. Sketch 2D Sources
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "generative-plotter-contour-flow",
        "title": "Generative Plotter Contour Flow",
        "description": "Pen plotter topographic contour isolines with undulating vector flow fields drawn on archival cream stock.",
        "tags": ["plotter", "generative", "procedural", "drawing", "contours"],
        "code": """// Generative Plotter Contour Flow - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Pen plotter topographic contour isolines with undulating vector flow fields drawn on archival cream stock.
param("line_density", 42.0, 16.0, 80.0);
param("flow_scale", 3.2, 0.5, 8.0);
param("stream_warp", 0.65, 0.1, 1.5);
param("pen_weight", 1.8, 0.5, 4.0);
param("slate_density", 0.28, 0.0, 0.8);

function draw(t) {
  // Warm cream archival stock #F4EEDD
  background(0.957, 0.933, 0.867);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const numLines = floor(line_density);
  const time = t * 0.4;

  noFill();

  // Corner plotter registration marks
  stroke(0.549, 0.114, 0.251, 0.7);
  strokeWeight(1.2);
  const pad = 40.0;
  const rlen = 20.0;
  line(pad, pad, pad + rlen, pad);
  line(pad, pad, pad, pad + rlen);
  line(width - pad, pad, width - pad - rlen, pad);
  line(width - pad, pad, width - pad, pad + rlen);
  line(pad, height - pad, pad + rlen, height - pad);
  line(pad, height - pad, pad, height - pad - rlen);
  line(width - pad, height - pad, width - pad - rlen, height - pad);
  line(width - pad, height - pad, width - pad, height - pad - rlen);

  // Topographic contour flow field
  const ptsPerLine = 140;
  const stepX = (width - pad * 2.0) / ptsPerLine;
  const stepY = (height - pad * 2.0) / (numLines + 1);

  for (let j = 0; j < numLines; j++) {
    const baseY = pad + (j + 1) * stepY;
    const normY = j / numLines;

    const isVermilion = (j % 11 === 3);
    const isSlate = (j % 5 === 1) && (slate_density > 0.1);

    if (isVermilion) {
      stroke(0.549, 0.114, 0.251, 0.92);
      strokeWeight(pen_weight * 1.3);
    } else if (isSlate) {
      stroke(0.157, 0.325, 0.420, 0.85);
      strokeWeight(pen_weight * 1.1);
    } else {
      stroke(0.129, 0.129, 0.129, 0.88);
      strokeWeight(pen_weight);
    }

    beginShape();
    for (let i = 0; i <= ptsPerLine; i++) {
      const x = pad + i * stepX;
      const u = (x - pad) / (width - pad * 2.0);

      const d1 = sin(u * flow_scale * 6.2831853 + time + normY * 3.5);
      const d2 = cos(u * flow_scale * 3.1415926 - time * 0.7 + normY * 5.0) * 0.5;
      const d3 = sin((u + normY) * flow_scale * 9.4247779 + time * 1.2) * 0.25;

      const envelope = sin(u * 3.14159265);
      const dispY = (d1 + d2 + d3) * (stepY * 2.2 * stream_warp) * envelope;

      vertex(x, baseY + dispY);
    }
    endShape();

    if (j % 7 === 0) {
      stroke(0.129, 0.129, 0.129, 0.45);
      strokeWeight(0.8);
      for (let i = 4; i < ptsPerLine; i += 6) {
        const x = pad + i * stepX;
        const u = (x - pad) / (width - pad * 2.0);
        const d1 = sin(u * flow_scale * 6.2831853 + time + normY * 3.5);
        const envelope = sin(u * 3.14159265);
        const y = baseY + d1 * (stepY * 2.2 * stream_warp) * envelope;
        line(x - 2.0, y - 5.0, x + 2.0, y + 5.0);
      }
    }
  }

  stroke(0.549, 0.114, 0.251, 0.8);
  strokeWeight(pen_weight * 1.1);
  ellipse(cx, cy, 180.0, 180.0);
}
"""
    },
    {
        "slug": "generative-plotter-spiral-lattice",
        "title": "Generative Plotter Spiral Lattice",
        "description": "Concentric logarithmic Archimedean spiral lattice with geometric pen hatching and harmonic angle offsets.",
        "tags": ["plotter", "generative", "spiral", "lattice", "hatching"],
        "code": """// Generative Plotter Spiral Lattice - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Concentric logarithmic Archimedean spiral lattice with geometric pen hatching and harmonic angle offsets.
param("spiral_turns", 8.0, 3.0, 16.0);
param("spoke_chords", 32.0, 12.0, 64.0);
param("twist_warp", 0.55, 0.1, 1.5);
param("pen_weight", 1.8, 0.5, 4.0);
param("vermilion_density", 0.35, 0.0, 1.0);

function draw(t) {
  // Archival cream stock #F4EEDD
  background(0.957, 0.933, 0.867);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const time = t * 0.3;
  const maxR = min(width, height) * 0.44;

  noFill();

  // Calibration graticule border
  stroke(0.129, 0.129, 0.129, 0.4);
  strokeWeight(1.0);
  rect(36.0, 36.0, width - 72.0, height - 72.0);
  rect(44.0, 44.0, width - 88.0, height - 88.0);

  // Outer concentric tick ring
  const numTicks = 72;
  for (let k = 0; k < numTicks; k++) {
    const a = (k / numTicks) * 6.2831853;
    const r1 = maxR * 0.98;
    const r2 = maxR * 1.03;
    line(cx + cos(a) * r1, cy + sin(a) * r1, cx + cos(a) * r2, cy + sin(a) * r2);
  }

  // Dual interleaved logarithmic/Archimedean spirals
  const turns = floor(spiral_turns);
  const steps = 600;

  for (let s = 0; s < 3; s++) {
    const angleOffset = (s / 3.0) * 6.2831853 + time * 0.2;
    if (s === 1) {
      stroke(0.549, 0.114, 0.251, 0.92);
      strokeWeight(pen_weight * 1.2);
    } else if (s === 2) {
      stroke(0.157, 0.325, 0.420, 0.88);
      strokeWeight(pen_weight * 1.1);
    } else {
      stroke(0.129, 0.129, 0.129, 0.92);
      strokeWeight(pen_weight);
    }

    beginShape();
    for (let i = 0; i <= steps; i++) {
      const frac = i / steps;
      const theta = frac * turns * 6.2831853 + angleOffset;
      const r = pow(frac, 0.85) * maxR * (1.0 + sin(theta * 3.0 + time) * 0.05 * twist_warp);
      vertex(cx + cos(theta) * r, cy + sin(theta) * r);
    }
    endShape();
  }

  // Geometric cross-hatch chords connecting spiral arms
  const chords = floor(spoke_chords);
  strokeWeight(0.9);
  for (let i = 0; i < chords; i++) {
    const f1 = (i + 1) / (chords + 2);
    const th1 = f1 * turns * 6.2831853 + time * 0.2;
    const r1 = pow(f1, 0.85) * maxR;

    const f2 = min(1.0, f1 + 0.08);
    const th2 = f2 * turns * 6.2831853 + (2.0 / 3.0) * 6.2831853 + time * 0.2;
    const r2 = pow(f2, 0.85) * maxR;

    const isVerm = (i % 5 === 2) && (vermilion_density > 0.2);
    if (isVerm) {
      stroke(0.549, 0.114, 0.251, 0.7);
    } else {
      stroke(0.129, 0.129, 0.129, 0.45);
    }

    line(cx + cos(th1) * r1, cy + sin(th1) * r1, cx + cos(th2) * r2, cy + sin(th2) * r2);
  }

  // Central hub compass rings
  stroke(0.549, 0.114, 0.251, 0.85);
  strokeWeight(1.4);
  ellipse(cx, cy, 32.0, 32.0);
  stroke(0.129, 0.129, 0.129, 0.7);
  ellipse(cx, cy, 64.0, 64.0);
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "vector-wireframe-geodesic-sphere-3d",
        "title": "Vector Wireframe Geodesic Sphere 3D",
        "description": "Rotating vector display geodesic icosahedral sphere with luminescent green strut edges and pulsing nodal vertices.",
        "tags": ["vector", "wireframe", "3d", "geodesic", "geometry"],
        "code": """// Vector Wireframe Geodesic Sphere 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Rotating vector display geodesic icosahedral sphere with luminescent green strut edges and pulsing nodal vertices.
param("strut_thickness", 0.024, 0.008, 0.06);
param("pulse_rate", 0.8, 0.2, 2.5);
param("tumble_speed", 0.6, 0.1, 2.0);
param("vertex_scale", 0.055, 0.02, 0.12);
param("latitude_rings", 5.0, 1.0, 10.0);

function draw(t) {
  push();
  const time = t * tumble_speed;

  rotateY(time * 0.7);
  rotateX(0.55 + sin(time * 0.5) * 0.25);
  rotateZ(time * 0.3);

  const R = 1.15;
  const phi = (1.0 + sqrt(5.0)) * 0.5;
  const norm = R / sqrt(1.0 + phi * phi);
  const a = 1.0 * norm;
  const b = phi * norm;

  const V = [
    [-a,  b,  0], [ a,  b,  0], [-a, -b,  0], [ a, -b,  0],
    [ 0, -a,  b], [ 0,  a,  b], [ 0, -a, -b], [ 0,  a, -b],
    [ b,  0, -a], [ b,  0,  a], [-b,  0, -a], [-b,  0,  a]
  ];

  const E = [
    [0, 1], [0, 5], [0, 7], [0, 10], [0, 11],
    [1, 5], [1, 7], [1, 8], [1, 9],
    [2, 3], [2, 4], [2, 6], [2, 10], [2, 11],
    [3, 4], [3, 6], [3, 8], [3, 9],
    [4, 5], [4, 9], [4, 11],
    [5, 9], [5, 11],
    [6, 7], [6, 8], [6, 10],
    [7, 8], [7, 10],
    [8, 9], [10, 11]
  ];

  for (let i = 0; i < E.length; i++) {
    const p1 = V[E[i][0]];
    const p2 = V[E[i][1]];
    fill(0.0, 1.0, 0.4);
    tube(p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], strut_thickness, 6);
  }

  const pulse = sin(t * pulse_rate * 3.14159) * 0.25 + 0.75;
  for (let i = 0; i < V.length; i++) {
    push();
    translate(V[i][0], V[i][1], V[i][2]);
    fill(0.502, 1.0, 0.698);
    sphere(vertex_scale * pulse, 8, 8);
    pop();
  }

  const numRings = floor(latitude_rings);
  const ringSteps = 32;
  for (let r = 1; r <= numRings; r++) {
    const lat = ((r / (numRings + 1)) - 0.5) * 3.1415926 * 0.7;
    const ringY = sin(lat) * R;
    const ringR = cos(lat) * R;

    fill(0.0, 0.56, 0.22);
    for (let s = 0; s < ringSteps; s++) {
      const th1 = (s / ringSteps) * 6.2831853;
      const th2 = ((s + 1) / ringSteps) * 6.2831853;
      tube(cos(th1) * ringR, ringY, sin(th1) * ringR,
           cos(th2) * ringR, ringY, sin(th2) * ringR,
           strut_thickness * 0.6, 6);
    }
  }

  pop();
}
"""
    },
    {
        "slug": "vector-wireframe-torus-knot-3d",
        "title": "Vector Wireframe Torus Knot 3D",
        "description": "Orthographic vector wireframe trefoil torus knot with segmented glowing green rib cages and parametric twists.",
        "tags": ["vector", "wireframe", "3d", "torus", "knot"],
        "code": """// Vector Wireframe Torus Knot 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Orthographic vector wireframe trefoil torus knot with segmented glowing green rib cages and parametric twists.
param("rib_count", 48.0, 16.0, 96.0);
param("rib_radius", 0.22, 0.08, 0.45);
param("spin_rate", 0.5, 0.1, 2.0);
param("knot_scale", 0.95, 0.4, 1.8);
param("wire_thickness", 0.022, 0.008, 0.05);

function draw(t) {
  push();
  const time = t * spin_rate;

  rotateY(time * 0.75);
  rotateX(0.65 + sin(time * 0.4) * 0.25);
  rotateZ(time * 0.25);

  const N = floor(rib_count);
  const p = 2.0;
  const q = 3.0;
  const R = 0.85 * knot_scale;
  const r = 0.38 * knot_scale;
  const ribR = rib_radius * knot_scale;

  function knotPoint(phi) {
    const rCurrent = R + r * cos(q * phi);
    return [
      rCurrent * cos(p * phi),
      rCurrent * sin(p * phi),
      -sin(q * phi) * r * 1.5
    ];
  }

  const spine = [];
  for (let i = 0; i < N; i++) {
    const phi = (i / N) * 6.2831853;
    spine.push(knotPoint(phi));
  }

  fill(0.0, 1.0, 0.4);
  for (let i = 0; i < N; i++) {
    const p1 = spine[i];
    const p2 = spine[(i + 1) % N];
    tube(p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], wire_thickness * 1.4, 6);
  }

  const ribSegments = 16;
  for (let i = 0; i < N; i++) {
    const pCenter = spine[i];
    const pNext = spine[(i + 1) % N];

    const tx = pNext[0] - pCenter[0];
    const ty = pNext[1] - pCenter[1];
    const tz = pNext[2] - pCenter[2];
    const tLen = sqrt(tx * tx + ty * ty + tz * tz) || 1.0;

    let nx = -ty / tLen;
    let ny = tx / tLen;
    let nz = 0;
    if (abs(nx) < 0.001 && abs(ny) < 0.001) {
      nx = 1.0; ny = 0; nz = 0;
    }

    const bx = ty * nz - tz * ny;
    const by = tz * nx - tx * nz;
    const bz = tx * ny - ty * nx;

    if (i % 2 === 0) {
      fill(0.502, 1.0, 0.698);
    } else {
      fill(0.0, 0.56, 0.22);
    }

    for (let s = 0; s < ribSegments; s++) {
      const th1 = (s / ribSegments) * 6.2831853;
      const th2 = ((s + 1) / ribSegments) * 6.2831853;

      const x1 = pCenter[0] + (cos(th1) * nx + sin(th1) * bx) * ribR;
      const y1 = pCenter[1] + (cos(th1) * ny + sin(th1) * by) * ribR;
      const z1 = pCenter[2] + (cos(th1) * nz + sin(th1) * bz) * ribR;

      const x2 = pCenter[0] + (cos(th2) * nx + sin(th2) * bx) * ribR;
      const y2 = pCenter[1] + (cos(th2) * ny + sin(th2) * by) * ribR;
      const z2 = pCenter[2] + (cos(th2) * nz + sin(th2) * bz) * ribR;

      tube(x1, y1, z1, x2, y2, z2, wire_thickness * 0.7, 4);
    }

    if (i % 4 === 0) {
      push();
      translate(pCenter[0] + nx * ribR, pCenter[1] + ny * ribR, pCenter[2] + nz * ribR);
      fill(0.502, 1.0, 0.698);
      sphere(0.038, 6, 6);
      pop();
    }
  }

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "dub-techno-chord-stab-synth",
        "title": "Dub Techno Chord Stab Synth",
        "description": "Minor triad chord stab synthesizer with resonant lowpass filter sweep, noise breath, and exponential envelope decay.",
        "tags": ["synth", "dub", "techno", "chord", "filter"],
        "params": {
            "cutoff": 1500.0,
            "res": 0.65,
            "chordSpread": 0.5,
            "decay": 0.45,
            "noiseLevel": 0.15
        },
        "code": """param float cutoff = 1500.0 [80.0, 8000.0]
param float res = 0.65 [0.0, 0.95]
param float chordSpread = 0.5 [0.0, 1.0]
param float decay = 0.45 [0.05, 1.5]
param float noiseLevel = 0.15 [0.0, 0.5]
state float p1 = 0
state float p2 = 0
state float env = 0
state float s1 = 0
state float s2 = 0
state float noiseState = 0.5

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decay), 0.00005, 0.01)
env = if(target > env, env + (target - env) * 0.1, env - env * decayRate)

pStep = clamp(freq, 20.0, 4000.0) / sr
p1 = (p1 + pStep) % 1.0
p2 = (p2 + pStep * (1.0 + 0.1892 * chordSpread)) % 1.0

saw1 = 2.0 * p1 - 1.0
saw2 = 2.0 * p2 - 1.0

noiseState = (noiseState * 105.13 + 0.137) % 1.0
noise = (noiseState * 2.0 - 1.0) * noiseLevel

raw = (saw1 * 0.55 + saw2 * 0.45 + noise) * env

fCut = clamp((cutoff + env * 4500.0) / sr * 2.4, 0.004, 0.45)
feedback = (s2 * res * 2.8) / (1.0 + abs(s2))
u = raw - feedback
sat = u / (1.0 + abs(u) * 0.4)
s1 = s1 + fCut * (sat - s1)
s2 = s2 + fCut * (s1 - s2)

out = s2 * 0.22
"""
    },
    {
        "slug": "dub-techno-sub-rumble-synth",
        "title": "Dub Techno Sub Rumble Synth",
        "description": "Sub-octave sinusoidal bass synthesizer with warm soft saturation, pitch glide envelope, and deep low-end punch.",
        "tags": ["synth", "dub", "techno", "sub", "bass"],
        "params": {
            "subDrive": 1.6,
            "subTone": 0.45,
            "punch": 0.55,
            "glideRate": 0.12,
            "decay": 0.55
        },
        "code": """param float subDrive = 1.6 [1.0, 4.0]
param float subTone = 0.45 [0.0, 1.0]
param float punch = 0.55 [0.0, 1.0]
param float glideRate = 0.12 [0.02, 0.5]
param float decay = 0.55 [0.1, 2.0]
state float phase = 0
state float curFreq = 55
state float env = 0
state float pEnv = 0
state float s1 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decay), 0.00005, 0.01)
env = if(target > env, env + (target - env) * 0.15, env - env * decayRate)

pEnv = if(target > pEnv, 1.0, pEnv * 0.998)
curFreq = curFreq + (freq * 0.5 - curFreq) * glideRate
f0 = clamp(curFreq + pEnv * punch * 40.0, 20.0, 600.0)

pStep = f0 / sr
phase = (phase + pStep) % 1.0

sin1 = sin(phase * 6.2831853)
sin2 = sin((phase * 2.0) % 1.0 * 6.2831853) * subTone * 0.5
raw = (sin1 + sin2) * env

u = raw * subDrive
sat = u / (1.0 + abs(u) * 0.5)

fCut = clamp(240.0 / sr * 2.4, 0.005, 0.35)
s1 = s1 + fCut * (sat - s1)

out = s1 * 0.28
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "drone-kosmische-space-delay-fx",
        "title": "Drone Kosmische Space Delay FX",
        "description": "Dual tape space echo with drifting motor wow, damping lowpass filter in feedback loop, and stereo cross-reflections.",
        "tags": ["effect", "delay", "kosmische", "drone", "space"],
        "params": {
            "delayTime": 0.28,
            "feedback": 0.55,
            "wowRate": 0.45,
            "damping": 0.45,
            "mix": 0.42
        },
        "code": """param float delayTime = 0.28 [0.02, 1.0]
param float feedback = 0.55 [0.0, 0.9]
param float wowRate = 0.45 [0.05, 2.0]
param float damping = 0.45 [0.0, 1.0]
param float mix = 0.42 [0.0, 1.0]
state float lfo = 0
state float d1 = 0
state float d2 = 0
state float lp = 0
state float prev = 0

pStep = clamp(wowRate, 0.05, 5.0) / sr
lfo = (lfo + pStep) % 1.0
wobble = sin(lfo * 6.2831853) * 0.08

drive = in + prev * feedback
sat = drive / (1.0 + abs(drive) * 0.5)

dCoeff = clamp(0.35 + wobble * 0.15, 0.05, 0.85)
ap1 = d1 + dCoeff * (sat - d1)
d1 = sat
ap2 = d2 + dCoeff * (ap1 - d2)
d2 = ap1

fDamp = clamp((1.0 - damping * 0.65) * 4000.0 / sr * 2.4, 0.01, 0.45)
lp = lp + fDamp * (ap2 - lp)
prev = lp

out = in * (1.0 - mix) + prev * mix * 0.85
"""
    },
    {
        "slug": "drone-kosmische-resonator-filter-fx",
        "title": "Drone Kosmische Resonator Filter FX",
        "description": "Four-pole resonant state-variable filter with slow LFO phase modulation, harmonic comb resonance, and soft drive.",
        "tags": ["effect", "filter", "resonator", "kosmische", "drone"],
        "params": {
            "cutoff": 1200.0,
            "resonance": 0.72,
            "sweepRate": 0.25,
            "drive": 1.5,
            "mix": 0.45
        },
        "code": """param float cutoff = 1200.0 [80.0, 6000.0]
param float resonance = 0.72 [0.1, 0.95]
param float sweepRate = 0.25 [0.02, 1.5]
param float drive = 1.5 [1.0, 4.0]
param float mix = 0.45 [0.0, 1.0]
state float lfo = 0
state float s1 = 0
state float s2 = 0

pStep = clamp(sweepRate, 0.02, 3.0) / sr
lfo = (lfo + pStep) % 1.0
sweep = sin(lfo * 6.2831853) * 0.45 + 0.55

u = in * drive
satIn = u / (1.0 + abs(u) * 0.4)

fCut = clamp((cutoff * sweep) / sr * 2.4, 0.005, 0.45)
feedback = (s2 * resonance * 2.8) / (1.0 + abs(s2))
diff = satIn - feedback - s1
s1 = s1 + fCut * diff
s2 = s2 + fCut * s1

out = in * (1.0 - mix) + s2 * mix * 0.55
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel Sources
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "cymatics-sine-chladni-resonance",
        "title": "Cymatics Sine Chladni Resonance",
        "description": "2D Chladni acoustic plate resonance showing nodal vibration lines where sand grains collect into geometric harmonic patterns.",
        "embed_desc": "2D acoustic Chladni plate resonance showing nodal vibration geometry where sand grains collect into harmonic standing wave figures. Bass drives vibrational energy and plate rim pulsation, mids shift modal integer ratios, highs scatter micro-grain sand particles.",
        "tags": ["pixel", "cymatics", "resonance", "chladni", "acoustics"],
        "params": {
            "modalM": 3.0,
            "modalN": 5.0,
            "nodalWidth": 0.08,
            "vibrationRate": 0.8,
            "amberGlow": 0.65
        },
        "code": """param float modalM = 3.0 [1.0, 8.0]
param float modalN = 5.0 [1.0, 8.0]
param float nodalWidth = 0.08 [0.01, 0.3]
param float vibrationRate = 0.8 [0.1, 3.0]
param float amberGlow = 0.65 [0.0, 1.0]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0
r = length(p)

pi = 3.14159265
kx = p.x * pi
ky = p.y * pi
time = t * vibrationRate

m1 = sin(modalN * kx + time * 0.2) * sin(modalM * ky)
m2 = sin(modalM * kx) * sin(modalN * ky - time * 0.2)
chladni = m1 - m2

distNode = abs(chladni)
sand = smoothstep(nodalWidth, 0.0, distNode)

ripple = sin(r * 18.0 - time * 3.0) * 0.5 + 0.5

cGround = vec3(0.043, 0.055, 0.078)
cSand = vec3(0.878, 0.902, 0.929)
cNode = vec3(0.220, 0.741, 0.973)
cAmber = vec3(0.961, 0.620, 0.043)

plateRim = smoothstep(0.98, 0.92, r)
boundaryRing = smoothstep(0.04, 0.0, abs(r - 0.92))

vibrationField = mix(cGround, cNode * 0.7, ripple * (1.0 - distNode * 0.5))
colPlate = mix(vibrationField, cSand, sand) + boundaryRing * cAmber * amberGlow

col = colPlate * plateRim
alpha = 1.0
""",
        "glsl": """
      precision highp float;
      uniform vec2 res;
      uniform float t;
      uniform float aspect;
      uniform float audio_bass;
      uniform float audio_mid;
      uniform float audio_high;

      void main() {
        vec2 uv = gl_FragCoord.xy / res;
        vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0;
        float r = length(p);

        float pi = 3.14159265;
        float kx = p.x * pi;
        float ky = p.y * pi;
        float time = t * (0.8 + audio_bass * 1.5);

        float mM = 3.0 + floor(audio_mid * 3.0);
        float mN = 5.0 + floor(audio_mid * 2.0);

        float m1 = sin(mN * kx + time * 0.25) * sin(mM * ky);
        float m2 = sin(mM * kx) * sin(mN * ky - time * 0.25);
        float chladni = m1 - m2;

        float distNode = abs(chladni);
        float sandW = 0.08 + audio_high * 0.06;
        float sand = smoothstep(sandW, 0.0, distNode);

        float ripple = sin(r * (18.0 + audio_bass * 12.0) - time * 3.5) * 0.5 + 0.5;

        vec3 cGround = vec3(0.043, 0.055, 0.078);
        vec3 cSand = vec3(0.878, 0.902, 0.929);
        vec3 cNode = vec3(0.220, 0.741, 0.973);
        vec3 cAmber = vec3(0.961, 0.620, 0.043);

        float plateRim = smoothstep(0.98, 0.92, r);
        float boundaryRing = smoothstep(0.04, 0.0, abs(r - 0.92));

        vec3 vibrationField = mix(cGround, cNode * (0.7 + audio_mid * 0.5), ripple * (1.0 - distNode * 0.5));
        vec3 colPlate = mix(vibrationField, cSand, sand) + boundaryRing * cAmber * (0.65 + audio_high * 0.8);

        vec3 col = colPlate * plateRim;
        gl_FragColor = vec4(col, 1.0);
      }
"""
    },
    {
        "slug": "cymatics-sine-harmonic-ripples",
        "title": "Cymatics Sine Harmonic Ripples",
        "description": "Intersecting Bessel standing wave rings with audio-reactive frequency nodal clusters, kinetic wavefront displacement, and metallic plate highlights.",
        "embed_desc": "Intersecting circular acoustic Bessel standing wave rings with audio-reactive nodal interference clusters and metallic plate highlights. Bass deepens the central wavefront displacement, mids modulate multi-focus ripple frequencies, highs ignite luminous amber nodal boundaries.",
        "tags": ["pixel", "cymatics", "harmonic", "ripples", "waves"],
        "params": {
            "rippleFrequency": 32.0,
            "waveVelocity": 1.2,
            "interferenceNodes": 4.0,
            "amberAccent": 0.7,
            "sandGathering": 0.65
        },
        "code": """param float rippleFrequency = 32.0 [8.0, 64.0]
param float waveVelocity = 1.2 [0.2, 4.0]
param float interferenceNodes = 4.0 [2.0, 8.0]
param float amberAccent = 0.7 [0.0, 1.0]
param float sandGathering = 0.65 [0.1, 1.0]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0
r = length(p)
ang = atan2(p.y, p.x)
time = t * waveVelocity

d1 = length(p - vec2(cos(time * 0.4) * 0.35, sin(time * 0.4) * 0.35))
d2 = length(p + vec2(cos(time * 0.4) * 0.35, sin(time * 0.4) * 0.35))
d3 = length(p - vec2(-sin(time * 0.3) * 0.25, cos(time * 0.3) * 0.25))

w1 = sin(d1 * rippleFrequency - time * 2.5)
w2 = sin(d2 * rippleFrequency - time * 2.5)
w3 = sin(d3 * rippleFrequency * 0.75 + time * 1.8)

waveSum = (w1 + w2 + w3) * 0.3333

nodeSand = smoothstep(0.12 * sandGathering, 0.0, abs(waveSum))
crestGlow = smoothstep(0.4, 0.9, waveSum)

angularBeats = sin(ang * interferenceNodes + time) * 0.5 + 0.5

cGround = vec3(0.043, 0.055, 0.078)
cSand = vec3(0.878, 0.902, 0.929)
cNode = vec3(0.220, 0.741, 0.973)
cAmber = vec3(0.961, 0.620, 0.043)

plateRim = smoothstep(0.98, 0.92, r)
boundaryGlow = smoothstep(0.06, 0.0, abs(r - 0.92)) * amberAccent

basePlate = mix(cGround, cNode * 0.65, crestGlow * angularBeats)
withSand = mix(basePlate, cSand, nodeSand) + boundaryGlow * cAmber

col = withSand * plateRim
alpha = 1.0
""",
        "glsl": """
      precision highp float;
      uniform vec2 res;
      uniform float t;
      uniform float aspect;
      uniform float audio_bass;
      uniform float audio_mid;
      uniform float audio_high;

      void main() {
        vec2 uv = gl_FragCoord.xy / res;
        vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0;
        float r = length(p);
        float ang = atan(p.y, p.x);
        float time = t * (1.2 + audio_bass * 1.8);

        float rFreq = 32.0 + audio_mid * 16.0;

        float orbitR = 0.35 + audio_bass * 0.25;
        vec2 c1 = vec2(cos(time * 0.4) * orbitR, sin(time * 0.4) * orbitR);
        vec2 c2 = -c1;
        vec2 c3 = vec2(-sin(time * 0.3) * orbitR * 0.8, cos(time * 0.3) * orbitR * 0.8);

        float d1 = length(p - c1);
        float d2 = length(p - c2);
        float d3 = length(p - c3);

        float w1 = sin(d1 * rFreq - time * 2.5);
        float w2 = sin(d2 * rFreq - time * 2.5);
        float w3 = sin(d3 * rFreq * 0.75 + time * 1.8);

        float waveSum = (w1 + w2 + w3) * 0.3333;

        float nodeSand = smoothstep(0.12 * (0.65 + audio_high * 0.5), 0.0, abs(waveSum));
        float crestGlow = smoothstep(0.35, 0.85, waveSum);

        float angularBeats = sin(ang * 4.0 + time) * 0.5 + 0.5;

        vec3 cGround = vec3(0.043, 0.055, 0.078);
        vec3 cSand = vec3(0.878, 0.902, 0.929);
        vec3 cNode = vec3(0.220, 0.741, 0.973);
        vec3 cAmber = vec3(0.961, 0.620, 0.043);

        float plateRim = smoothstep(0.98, 0.92, r);
        float boundaryGlow = smoothstep(0.06, 0.0, abs(r - 0.92)) * (0.7 + audio_high * 0.8);

        vec3 basePlate = mix(cGround, cNode * (0.65 + audio_mid * 0.4), crestGlow * angularBeats);
        vec3 withSand = mix(basePlate, cSand, nodeSand) + boundaryGlow * cAmber;

        vec3 col = withSand * plateRim;
        gl_FragColor = vec4(col, 1.0);
      }
"""
    }
]

# -------------------------------------------------------------
# HTML Embed Template
# -------------------------------------------------------------
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

        # Build validation patch
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

        # Gate 1: Validate
        v = subprocess.run([BIN, "--validate", test_inf], capture_output=True, text=True)
        if "ok\":true" not in v.stdout:
            print(f"FAIL validate: {slug}")
            print(v.stdout)
            failed = True
            continue

        # Gate 2: Frame render
        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        f_proc = subprocess.run([BIN, "--frame", test_inf, "0", out_frame_dir + "/", "--size", "1080x1080"], capture_output=True, text=True)
        if "ok\":true" not in f_proc.stdout:
            print(f"FAIL frame: {slug}")
            print(f_proc.stdout)
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

        # Save real engine frame preview
        prev_jpg = os.path.join(PREVIEWS_DIR, f"{slug}.jpg")
        im.convert("RGB").save(prev_jpg, quality=94)

        # Save webp thumbnail
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
            print(f"FAIL validate: {slug}")
            print(v.stdout)
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        f_proc = subprocess.run([BIN, "--frame", test_inf, "0", out_frame_dir + "/", "--size", "1080x1080"], capture_output=True, text=True)
        if "ok\":true" not in f_proc.stdout:
            print(f"FAIL frame: {slug}")
            print(f_proc.stdout)
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
        # Save .field device
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

        # Save .inf template
        escaped = it["code"].replace("\\", "\\\\").replace("\n", "\\n")
        inf_content = f"""infinite-patch 1
# {it['title']}
node 1 Notes Random Note Generator
  id notes
  pos 0 1024
  f rateBeats 0.5
  i rangeLow 36
  i rangeHigh 60
end
node 2 Synths Field Synth
  id synth
  pos 480 1024
  s code {escaped}
end
node 3 Utility Audio Out
  id speakers
  pos 1020 1024
end
node 4 Modulators Audio Analyze
  id ana
  pos 0 2200
end
node 5 Source Shape
  id pic
  pos 0 162
  c fillColor 0.16 0.32 0.45
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

        # Gate 1: Validate
        v = subprocess.run([BIN, "--validate", inf_file], capture_output=True, text=True)
        if "ok\":true" not in v.stdout:
            print(f"FAIL validate: {slug}")
            print(v.stdout)
            failed = True
            continue

        # Gate 2: Audio summary
        sum_json = os.path.join(TMP_DIR, f"{slug}_sum.json")
        s = subprocess.run([BIN, "--audio-summary", inf_file, sum_json, "--duration", "6"], capture_output=True, text=True)
        if "ok\":true" not in s.stdout or not os.path.exists(sum_json):
            print(f"FAIL audio summary command: {slug}")
            print(s.stdout)
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

        # Gate 3: Render frame preview
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
        im.convert("RGB").resize((640, 360), Image.Resampling.LANCZOS).save(tmpl_webp, "WEBP", quality=85)

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
node 1 Notes Random Note Generator
  id notes
  pos 0 1024
  f rateBeats 0.5
  i rangeLow 36
  i rangeHigh 60
end
node 2 Synths Analog
  id osc
  pos 400 1024
  f cutoff 2400.0
end
node 3 AudioEffects Field Effect
  id fx
  pos 800 1024
  s code {escaped}
end
node 4 Effects Limiter
  id lim
  pos 1100 1024
end
node 5 Utility Audio Out
  id speakers
  pos 1400 1024
end
node 6 Modulators Audio Analyze
  id ana
  pos 0 2200
end
node 7 Source Shape
  id pic
  pos 0 162
  c fillColor 0.28 0.18 0.45
  f size 0.24
end
node 8 Utility Output
  id out
  pos 460 0
end
node 9 Compositing Comment
  pos 0 722
  s text {it['title']}. {it['description']}
  f width 340
  f height 120
end
note osc 0 notes
aud fx 0 osc
aud lim 0 fx
aud speakers 0 lim
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
            print(f"FAIL validate: {slug}")
            print(v.stdout)
            failed = True
            continue

        sum_json = os.path.join(TMP_DIR, f"{slug}_sum.json")
        s = subprocess.run([BIN, "--audio-summary", inf_file, sum_json, "--duration", "6"], capture_output=True, text=True)
        if "ok\":true" not in s.stdout or not os.path.exists(sum_json):
            print(f"FAIL audio summary command: {slug}")
            print(s.stdout)
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
        im.convert("RGB").resize((640, 360), Image.Resampling.LANCZOS).save(tmpl_webp, "WEBP", quality=85)

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
            print(f"FAIL validate: {slug}")
            print(v.stdout)
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        f_proc = subprocess.run([BIN, "--frame", test_inf, "0", out_frame_dir + "/", "--size", "1080x1080"], capture_output=True, text=True)
        if "ok\":true" not in f_proc.stdout:
            print(f"FAIL frame: {slug}")
            print(f_proc.stdout)
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
                "params": 5,
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
                "params": 5,
                "file": f"assets/library/sketch/{it['slug']}.js",
                "preview": f"assets/library/previews/{it['slug']}.jpg",
                "bytes": it["bytes"]
            })

    for it in SYNTH_ITEMS:
        if it["slug"] not in existing_lib_ids:
            lib_data["items"].append({
                "id": it["slug"],
                "name": it["title"],
                "kind": "Synth",
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
                "nodes": ["Random Note Generator", "Field Synth", "Audio Out"]
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
                "nodes": ["Random Note Generator", "Analog", "Field Effect", "Limiter", "Audio Out"]
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
