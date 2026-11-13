#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 37.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: data_landscape -> data-landscape-pulsar-elevation-profile, data-landscape-bathymetric-isoline-survey)
2. Sketch 3D (Theme: demoscene_plasma -> demoscene-plasma-rotozoom-helix-twister-3d, demoscene-plasma-amiga-copper-polyhedron-3d)
3. Field Synth (Theme: psytrance -> psytrance-rolling-saw-bass-synth, psytrance-acid-squelch-lead-synth)
4. Field Effects (Theme: electro -> electro-robotic-vocoder-comb-fx, electro-cybernetic-bbd-flanger-delay-fx)
5. Field Pixel & Embeds (Theme: minimal_signal -> minimal-signal-datamatics-barcode-matrix, minimal-signal-raster-oscillogram-sweep)
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

TMP_DIR = "/tmp/pack37_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: data_landscape)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "data-landscape-pulsar-elevation-profile",
        "title": "Data Landscape Pulsar Elevation Profile",
        "description": "Procedural mathematical drafting rendering successive radio-astronomy pulsar transit traces with Gaussian signal peaks, hidden-line occlusion, and phosphorescent cyan ridge lines.",
        "tags": ["data", "landscape", "pulsar", "topographic", "radio-astronomy", "elevation"],
        "code": """// Data Landscape Pulsar Elevation Profile - Infinite Library (Sketch 2D)
param("ridges", 46.0, 24.0, 72.0);
param("peak_height", 85.0, 40.0, 160.0);
param("noise_scale", 1.25, 0.5, 2.5);
param("glow_intensity", 0.90, 0.4, 1.0);

function clamp(x, a, b) {
  return Math.max(a, Math.min(b, x));
}

function draw(t) {
  const w = width;
  const h = height;

  // 1. Midnight Oceanic Gradient Background (#0b132b -> #1c2541)
  const bgSteps = 48;
  for (let i = 0; i < bgSteps; i++) {
    const frac = i / bgSteps;
    const y1 = frac * h;
    const y2 = ((i + 1) / bgSteps) * h;
    const r = 0.043 + frac * 0.067;
    const g = 0.075 + frac * 0.070;
    const b = 0.169 + frac * 0.086;
    fill(r, g, b);
    noStroke();
    rect(0, y1, w, y2 - y1 + 1.0);
  }

  // 2. Telemetry and Survey Coordinate Frame
  stroke(0.357, 0.753, 0.745, 0.22 * glow_intensity);
  strokeWeight(0.9);
  rect(48, 48, w - 96, h - 96);
  rect(56, 56, w - 112, h - 112);

  // Calibration tick marks on margins
  stroke(1.0, 1.0, 1.0, 0.55 * glow_intensity);
  strokeWeight(1.2);
  for (let x = 80; x <= w - 80; x += 40) {
    line(x, 48, x, 53);
    line(x, h - 48, x, h - 53);
  }
  for (let y = 80; y <= h - 80; y += 40) {
    line(48, y, 53, y);
    line(w - 48, y, w - 53, y);
  }

  // 3. Stacked Pulsar Ridge Profiles with Hidden-Line Occlusion
  const numRidges = floor(ridges);
  const startY = 120.0;
  const endY = h - 120.0;
  const ridgeSpacing = (endY - startY) / numRidges;

  const leftX = 100.0;
  const rightX = w - 100.0;
  const numSamples = 130;
  const dx = (rightX - leftX) / (numSamples - 1);

  for (let r = 0; r < numRidges; r++) {
    const fracR = r / numRidges;
    const baseY = startY + r * ridgeSpacing;

    // Build the elevation profile points for this ridge
    const pts = [];
    for (let j = 0; j < numSamples; j++) {
      const curX = leftX + j * dx;
      const nx = (curX - (w * 0.5)) / 190.0;

      // Dual Gaussian bell envelopes centered around the midsection
      const env1 = exp(-nx * nx * 1.6);
      const env2 = exp(-(nx - 0.45) * (nx - 0.45) * 4.5) * 0.65;
      const env3 = exp(-(nx + 0.50) * (nx + 0.50) * 3.8) * 0.55;
      const combinedEnv = env1 + env2 + env3;

      // Multi-frequency harmonic perturbation
      const s1 = sin(nx * 7.5 + r * 0.48 + t * 0.75);
      const s2 = cos(nx * 15.0 - r * 0.32 + t * 1.1) * 0.42;
      const s3 = sin(nx * 32.0 + r * 1.1 + t * 1.8) * 0.22;
      const wave = (s1 + s2 + s3) * combinedEnv * peak_height * noise_scale;

      const curY = baseY - abs(wave);
      pts.push([curX, curY]);
    }

    // A. Occlusion Under-Polygon (Mask background so front lines occlude rear lines)
    fill(0.043, 0.075, 0.169);
    noStroke();
    beginShape();
    vertex(leftX, baseY + ridgeSpacing * 1.5);
    vertex(leftX, pts[0][1]);
    for (let j = 0; j < pts.length; j++) {
      vertex(pts[j][0], pts[j][1]);
    }
    vertex(rightX, pts[pts.length - 1][1]);
    vertex(rightX, baseY + ridgeSpacing * 1.5);
    endShape(CLOSE);

    // B. Phosphorescent Vector Contour Line
    // Color transitions from deep cyan at base to crisp white at crest peaks
    for (let j = 0; j < pts.length - 1; j++) {
      const p1 = pts[j];
      const p2 = pts[j + 1];
      const elevFrac = clamp((baseY - (p1[1] + p2[1]) * 0.5) / peak_height, 0.0, 1.0);

      const colR = 0.357 + elevFrac * 0.643;
      const colG = 0.753 + elevFrac * 0.247;
      const colB = 0.745 + elevFrac * 0.255;
      const alpha = (0.70 + elevFrac * 0.30) * glow_intensity;

      stroke(colR, colG, colB, alpha);
      strokeWeight(elevFrac > 0.4 ? 1.8 : 1.2);
      line(p1[0], p1[1], p2[0], p2[1]);
    }
  }

  // 4. Center Axis Crosshairs & Title Datum
  stroke(1.0, 1.0, 1.0, 0.65 * glow_intensity);
  strokeWeight(1.0);
  line(w * 0.5 - 24, h - 32, w * 0.5 + 24, h - 32);
  line(w * 0.5, h - 44, w * 0.5, h - 20);
}
"""
    },
    {
        "slug": "data-landscape-bathymetric-isoline-survey",
        "title": "Data Landscape Bathymetric Isoline Survey",
        "description": "Procedural topographic contour cartography visualizing multi-tiered bathymetric elevation isolines, submarine trench abyssal gradients, and nautical depth coordinate soundings.",
        "tags": ["data", "landscape", "bathymetric", "isoline", "contour", "cartography"],
        "code": """// Data Landscape Bathymetric Isoline Survey - Infinite Library (Sketch 2D)
param("isolines", 32.0, 16.0, 48.0);
param("depth_warp", 1.15, 0.4, 2.2);
param("harmonic_peaks", 4.0, 2.0, 8.0);
param("contour_opacity", 0.92, 0.5, 1.0);

function clamp(x, a, b) {
  return Math.max(a, Math.min(b, x));
}

function draw(t) {
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;

  // 1. Deep Abyssal Midnight Ground (#070d1e -> #101c38)
  const bgSteps = 50;
  for (let i = 0; i < bgSteps; i++) {
    const frac = i / bgSteps;
    const y1 = frac * h;
    const y2 = ((i + 1) / bgSteps) * h;
    const r = 0.027 + frac * 0.035;
    const g = 0.051 + frac * 0.058;
    const b = 0.118 + frac * 0.102;
    fill(r, g, b);
    noStroke();
    rect(0, y1, w, y2 - y1 + 1.0);
  }

  // 2. Nautical Bathymetry Graticule Lines
  stroke(0.357, 0.753, 0.745, 0.14 * contour_opacity);
  strokeWeight(0.8);
  for (let x = 60; x <= w - 60; x += 60) {
    line(x, 60, x, h - 60);
  }
  for (let y = 60; y <= h - 60; y += 60) {
    line(60, y, w - 60, y);
  }

  // Outer Survey Border
  stroke(0.357, 0.753, 0.745, 0.45 * contour_opacity);
  strokeWeight(1.4);
  rect(48, 48, w - 96, h - 96);
  rect(54, 54, w - 108, h - 108);

  // 3. Concentric Bathymetric Elevation Isolines
  const count = floor(isolines);
  const maxRadius = 450.0;
  const numAngles = 180;
  const dTheta = (PI * 2.0) / numAngles;

  for (let k = count - 1; k >= 0; k--) {
    const depthFrac = k / count; // 0 = summit, 1 = deep sea
    const baseR = maxRadius * (0.08 + 0.92 * depthFrac);

    // Isoline color gradient: deep oceanic blue -> bright turquoise cyan -> pure white summit
    const rCol = 0.10 + (1.0 - depthFrac) * 0.85;
    const gCol = 0.25 + (1.0 - depthFrac) * 0.73;
    const bCol = 0.48 + (1.0 - depthFrac) * 0.52;

    // Fill ring body with subtle depth shading
    fill(0.043 + (1.0 - depthFrac) * 0.05, 0.075 + (1.0 - depthFrac) * 0.08, 0.169 + (1.0 - depthFrac) * 0.12, 0.35);
    stroke(rCol, gCol, bCol, (0.65 + (1.0 - depthFrac) * 0.35) * contour_opacity);
    strokeWeight(k % 4 === 0 ? 2.2 : 1.1);

    beginShape();
    for (let i = 0; i < numAngles; i++) {
      const th = i * dTheta;

      // Complex multi-harmonic ocean trench & seamount contour perturbations
      const h1 = sin(th * 3.0 + t * 0.35 + depthFrac * 2.2) * 36.0;
      const h2 = cos(th * 5.0 - t * 0.50 + depthFrac * 1.5) * 22.0;
      const h3 = sin(th * 7.0 + t * 0.70) * 14.0;
      const h4 = cos(th * 2.0 + t * 0.20) * 28.0;

      const r = baseR + (h1 + h2 + h3 + h4) * depth_warp;
      const px = cx + cos(th) * r;
      const py = cy + sin(th) * r;
      vertex(px, py);
    }
    endShape(CLOSE);
  }

  // 4. Nautical Sounding Crosshairs & Depth Markers
  stroke(1.0, 1.0, 1.0, 0.85 * contour_opacity);
  strokeWeight(1.4);
  line(cx - 18, cy, cx + 18, cy);
  line(cx, cy - 18, cx, cy + 18);

  const quadrants = [
    [cx - 240, cy - 240], [cx + 240, cy - 240],
    [cx - 240, cy + 240], [cx + 240, cy + 240]
  ];
  for (let i = 0; i < quadrants.length; i++) {
    const qx = quadrants[i][0];
    const qy = quadrants[i][1];
    line(qx - 10, qy, qx + 10, qy);
    line(qx, qy - 10, qx, qy + 10);
    noFill();
    rect(qx - 16, qy - 16, 32, 32);
  }
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: demoscene_plasma)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "demoscene-plasma-rotozoom-helix-twister-3d",
        "title": "Demoscene Plasma Rotozoom Helix Twister 3D",
        "description": "Classic 1990s demoscene 3D twister column rendering interweaving helical copper ribbons with sine-modulated cross-sections and dual-tone electric cyan and plasma orange illumination.",
        "tags": ["demoscene", "plasma", "3d", "twister", "helix", "amiga", "rotozoom"],
        "code": """// Demoscene Plasma Rotozoom Helix Twister 3D - Infinite Library (Sketch 3D)
param("twist_speed", 0.75, 0.2, 2.0);
param("ribbon_scale", 0.85, 0.4, 1.5);
param("color_cycle", 1.25, 0.4, 3.0);
param("column_height", 2.5, 1.5, 3.8);

function clamp(x, a, b) {
  return Math.max(a, Math.min(b, x));
}

function draw(t) {
  push();
  const time = t * twist_speed;

  rotateY(time * 0.85);
  rotateX(0.40 + sin(time * 0.45) * 0.18);
  rotateZ(sin(time * 0.3) * 0.15);

  const slices = 52;
  const dy = column_height / slices;
  const halfH = column_height * 0.5;

  // Interweaving 4-ribbon classic Amiga twister
  for (let i = 0; i < slices; i++) {
    const y0 = -halfH + i * dy;
    const y1 = y0 + dy;

    const twist0 = y0 * 3.4 + time * 1.8;
    const twist1 = y1 * 3.4 + time * 1.8;

    const r0 = (0.52 + 0.16 * sin(y0 * 4.2 - time * 2.2)) * ribbon_scale;
    const r1 = (0.52 + 0.16 * sin(y1 * 4.2 - time * 2.2)) * ribbon_scale;

    for (let ribbon = 0; ribbon < 4; ribbon++) {
      const phiOffset = ribbon * (PI * 0.5);
      const a0 = twist0 + phiOffset;
      const a1 = twist1 + phiOffset;

      const x0A = cos(a0) * r0;
      const z0A = sin(a0) * r0;
      const x0B = cos(a0 + 0.35) * (r0 * 0.95);
      const z0B = sin(a0 + 0.35) * (r0 * 0.95);

      const x1A = cos(a1) * r1;
      const z1A = sin(a1) * r1;
      const x1B = cos(a1 + 0.35) * (r1 * 0.95);
      const z1B = sin(a1 + 0.35) * (r1 * 0.95);

      // Ribbon color: alternating fiery orange/copper (#ff3b00) and electric cyan (#00e0ff)
      if (ribbon % 2 === 0) {
        const fire = 0.5 + 0.5 * sin(y0 * color_cycle * 2.0 + time * 2.5);
        fill(1.0, 0.22 + fire * 0.26, 0.0);
      } else {
        const cyan = 0.5 + 0.5 * cos(y0 * color_cycle * 2.0 - time * 2.0);
        fill(0.0, 0.85 + cyan * 0.15, 1.0);
      }

      // Render quad strip facet
      beginShape();
      vertex(x0A, y0, z0A);
      vertex(x0B, y0, z0B);
      vertex(x1B, y1, z1B);
      vertex(x1A, y1, z1A);
      endShape();
    }
  }

  // Outer orbiting plasma particle rings
  const numParticles = 24;
  for (let p = 0; p < numParticles; p++) {
    const pAng = (p / numParticles) * PI * 2.0 + time * 1.4;
    const pr = 1.25 + 0.15 * sin(pAng * 3.0 + time);
    const py = sin(pAng * 2.0 - time) * 0.85;
    const px = cos(pAng) * pr;
    const pz = sin(pAng) * pr;

    push();
    translate(px, py, pz);
    fill(0.0, 0.88, 1.0);
    sphere(0.045, 6, 6);
    pop();
  }

  pop();
}
"""
    },
    {
        "slug": "demoscene-plasma-amiga-copper-polyhedron-3d",
        "title": "Demoscene Plasma Amiga Copper Polyhedron 3D",
        "description": "Faceted 3D stellated icosahedron rotating along dual axes with copper-bar plasma gradient vertex highlights and retro Amiga vector wireframe struts.",
        "tags": ["demoscene", "amiga", "3d", "polyhedron", "copper", "plasma", "geometry"],
        "code": """// Demoscene Plasma Amiga Copper Polyhedron 3D - Infinite Library (Sketch 3D)
param("rot_speed", 0.65, 0.1, 2.0);
param("strut_thickness", 0.026, 0.01, 0.06);
param("plasma_warp", 1.10, 0.3, 2.5);
param("star_radius", 1.18, 0.6, 1.8);

function clamp(x, a, b) {
  return Math.max(a, Math.min(b, x));
}

function draw(t) {
  push();
  const time = t * rot_speed;

  rotateY(time * 0.8);
  rotateX(time * 0.6 + 0.4);
  rotateZ(sin(time * 0.45) * 0.25);

  const phi = (1.0 + sqrt(5.0)) * 0.5;
  const invNorm = 1.0 / sqrt(1.0 + phi * phi);
  const a = 1.0 * invNorm * star_radius * 0.62;
  const b = phi * invNorm * star_radius * 0.62;

  // 12 base icosahedral vertices
  const V = [
    [-a,  b,  0], [ a,  b,  0], [-a, -b,  0], [ a, -b,  0],
    [ 0, -a,  b], [ 0,  a,  b], [ 0, -a, -b], [ 0,  a, -b],
    [ b,  0, -a], [ b,  0,  a], [-b,  0, -a], [-b,  0,  a]
  ];

  // 20 Icosahedral triangular faces
  const F = [
    [0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11],
    [1, 5, 9], [5, 11, 4], [11, 10, 2], [10, 7, 6], [7, 1, 8],
    [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9],
    [4, 9, 5], [2, 4, 11], [6, 2, 10], [8, 6, 7], [9, 8, 1]
  ];

  // Draw stellation apex pyramids on each face
  for (let i = 0; i < F.length; i++) {
    const f = F[i];
    const v0 = V[f[0]];
    const v1 = V[f[1]];
    const v2 = V[f[2]];

    // Face centroid normal
    const cx = (v0[0] + v1[0] + v2[0]) / 3.0;
    const cy = (v0[1] + v1[1] + v2[1]) / 3.0;
    const cz = (v0[2] + v1[2] + v2[2]) / 3.0;
    const len = sqrt(cx * cx + cy * cy + cz * cz);

    const spikeH = star_radius * (0.95 + 0.18 * sin(time * 2.2 * plasma_warp + i * 0.35));
    const apexX = (cx / len) * spikeH;
    const apexY = (cy / len) * spikeH;
    const apexZ = (cz / len) * spikeH;

    // Alternating copper flame and electric cyan coloring
    if (i % 2 === 0) {
      fill(1.0, 0.25 + 0.20 * sin(i * 0.5 + time), 0.0);
    } else {
      fill(0.0, 0.80, 1.0);
    }

    // Three facets of the stellation pyramid
    beginShape();
    vertex(v0[0], v0[1], v0[2]);
    vertex(v1[0], v1[1], v1[2]);
    vertex(apexX, apexY, apexZ);
    endShape();

    beginShape();
    vertex(v1[0], v1[1], v1[2]);
    vertex(v2[0], v2[1], v2[2]);
    vertex(apexX, apexY, apexZ);
    endShape();

    beginShape();
    vertex(v2[0], v2[1], v2[2]);
    vertex(v0[0], v0[1], v0[2]);
    vertex(apexX, apexY, apexZ);
    endShape();
  }

  // Dual equatorial gimbal plasma rings
  push();
  rotateX(HALF_PI);
  fill(1.0, 0.35 + 0.3 * sin(time * 3.0), 0.0);
  torus(star_radius * 1.35, 0.024, 24);
  pop();

  push();
  rotateY(HALF_PI);
  fill(0.0, 0.90, 1.0);
  torus(star_radius * 1.35, 0.024, 24);
  pop();

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: psytrance)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "psytrance-rolling-saw-bass-synth",
        "title": "Psytrance Rolling Saw Bass Synth",
        "description": "High-octane Goa and full-on psytrance bassline synthesizer featuring sharp transient click shaping, exponential envelope decay, resonant 24dB ladder filter cutoff, and sub-octave saturation.",
        "note_root": 38, # D1 (note 38)
        "params": {
            "cutoff": 1450.0,
            "res": 0.65,
            "envMod": 0.72,
            "decay": 0.14,
            "drive": 2.1,
            "sub": 0.38,
            "click": 0.45
        },
        "code": """param float cutoff = 1450.0 [80.0, 8000.0];
param float res = 0.65 [0.0, 0.95];
param float envMod = 0.72 [0.0, 1.0];
param float decay = 0.14 [0.03, 0.5];
param float drive = 2.1 [1.0, 5.0];
param float sub = 0.38 [0.0, 1.0];
param float click = 0.45 [0.0, 1.0];
state float phase = 0;
state float subPhase = 0;
state float env = 0;
state float clickEnv = 0;
state float lastGate = 0;
state float s1 = 0;
state float s2 = 0;
state float s3 = 0;
state float s4 = 0;

trig = if(gate > 0.5 && lastGate <= 0.5, 1.0, 0.0);
lastGate = gate;

clickEnv = if(trig > 0.5, 1.0, clickEnv - clickEnv * 0.12);
decayRate = clamp(1.0 / (sr * decay), 0.0001, 0.05);
env = if(trig > 0.5, 1.0, env - env * decayRate);

pStep = clamp(freq, 20.0, 2000.0) / sr;
phase = (phase + pStep) % 1.0;
subPhase = (subPhase + pStep * 0.5) % 1.0;

saw = 2.0 * phase - 1.0;
subOsc = sin(6.2831853 * subPhase) * sub;
raw = saw + subOsc + clickEnv * click * 0.75;

fCut = clamp((cutoff + env * envMod * 5200.0) / sr * 2.2, 0.005, 0.45);
fb = (s4 * res * 3.6) / (1.0 + abs(s4) * 0.85);
u = (raw * drive) - fb;
sat = u / (1.0 + abs(u) * 0.4);

s1 = s1 + fCut * (sat - s1);
s2 = s2 + fCut * (s1 - s2);
s3 = s3 + fCut * (s2 - s3);
s4 = s4 + fCut * (s3 - s4);

out = s4 * (0.35 + env * 0.65) * 0.38;
"""
    },
    {
        "slug": "psytrance-acid-squelch-lead-synth",
        "title": "Psytrance Acid Squelch Lead Synth",
        "description": "Psychedelic FM acid lead synthesizer featuring dual cascaded oscillators, extreme resonance squelch filter sweeps, tempo-synced LFO filter modulation, and warm diode clipping.",
        "note_root": 62, # D3 (note 62)
        "params": {
            "cutoff": 2400.0,
            "res": 0.82,
            "envMod": 0.75,
            "decay": 0.25,
            "drive": 2.2,
            "lfoRate": 4.0
        },
        "code": """param float cutoff = 2400.0 [100.0, 10000.0];
param float res = 0.82 [0.0, 0.98];
param float envMod = 0.75 [0.0, 1.0];
param float decay = 0.25 [0.05, 0.8];
param float drive = 2.2 [1.0, 6.0];
param float lfoRate = 4.0 [0.5, 12.0];
state float phase = 0;
state float lfo = 0;
state float env = 0;
state float s1 = 0;
state float s2 = 0;
state float s3 = 0;
state float s4 = 0;

target = if(gate > 0.5, 1.0, 0.0);
decayRate = clamp(1.0 / (sr * decay), 0.0001, 0.02);
env = if(target > env, env + (target - env) * 0.12, env - env * decayRate);

lfo = (lfo + lfoRate / sr) % 1.0;
lfoSin = sin(6.2831853 * lfo);

pStep = clamp(freq, 20.0, 4000.0) / sr;
phase = (phase + pStep) % 1.0;
saw = 2.0 * phase - 1.0;
sq = if(phase > 0.5, 0.8, -0.8);
raw = saw * 0.7 + sq * 0.3;

fCut = clamp((cutoff + env * envMod * 6000.0 + lfoSin * 800.0) / sr * 2.2, 0.005, 0.45);
feedback = (s4 * res * 3.8) / (1.0 + abs(s4) * 0.9);
u = (raw * drive) - feedback;
sat = u / (1.0 + abs(u) * 0.35);

s1 = s1 + fCut * (sat - s1);
s2 = s2 + fCut * (s1 - s2);
s3 = s3 + fCut * (s2 - s3);
s4 = s4 + fCut * (s3 - s4);

out = s4 * (0.30 + env * 0.70) * 0.38;
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: electro)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "electro-robotic-vocoder-comb-fx",
        "title": "Electro Robotic Vocoder Comb FX",
        "description": "Resonant robotic comb filter and formant vocoder effect featuring pitch-tracked harmonic comb peaks, metallic sideband modulation, non-linear feedback drive, and crisp transient shaping.",
        "params": {
            "pitch": 0.42,
            "feedback": 0.62,
            "formant": 0.55,
            "drive": 0.35,
            "mix": 0.50
        },
        "code": """param float pitch = 0.42 [0.1, 0.95];
param float feedback = 0.62 [0.0, 0.92];
param float formant = 0.55 [0.0, 1.0];
param float drive = 0.35 [0.0, 1.0];
param float mix = 0.50 [0.0, 1.0];
state float d1 = 0;
state float d2 = 0;
state float d3 = 0;
state float d4 = 0;
state float fb = 0;

kRate = clamp(0.04 + pitch * 0.28, 0.02, 0.45);
d1 = d1 + kRate * ((in + fb * feedback) - d1);
d2 = d2 + kRate * 0.92 * (d1 - d2);
d3 = d3 + kRate * 0.85 * (d2 - d3);
d4 = d4 + kRate * 0.78 * (d3 - d4);

comb = (d1 - d2 * 0.70 + d3 * 0.50 - d4 * 0.30) * (1.0 + formant * 0.50);
fb = comb / (1.0 + abs(comb * (0.20 + drive * 0.40)));

processed = in * (1.0 - mix) + fb * mix * 1.35;
out = processed * 0.95;
"""
    },
    {
        "slug": "electro-cybernetic-bbd-flanger-delay-fx",
        "title": "Electro Cybernetic BBD Flanger Delay FX",
        "description": "Vintage bucket-brigade style short-time flanger and cross-feedback delay processor with triangular LFO sweep modulation, inverted feedback notches, and warm analog diode saturation.",
        "params": {
            "rate": 0.32,
            "depth": 0.45,
            "feedback": 0.52,
            "damping": 0.40,
            "mix": 0.48
        },
        "code": """param float rate = 0.32 [0.05, 1.0];
param float depth = 0.45 [0.05, 0.9];
param float feedback = 0.52 [0.0, 0.88];
param float damping = 0.40 [0.05, 0.9];
param float mix = 0.48 [0.0, 1.0];
state float lfoPhase = 0;
state float d1 = 0;
state float d2 = 0;
state float d3 = 0;
state float lp = 0;
state float fb = 0;

lfoPhase = (lfoPhase + rate * 0.0016) % 1.0;
tri = 2.0 * abs(2.0 * lfoPhase - 1.0) - 1.0;
flangeMod = tri * depth * 0.035;

kRate = clamp(0.10 + flangeMod, 0.03, 0.35);
d1 = d1 + kRate * ((in + fb * feedback) - d1);
d2 = d2 + kRate * 0.88 * (d1 - d2);
d3 = d3 + kRate * 0.76 * (d2 - d3);

fDamp = clamp(0.12 + (1.0 - damping) * 0.28, 0.04, 0.45);
lp = lp + fDamp * (d3 - lp);
fb = lp / (1.0 + abs(lp * 0.22));

flanged = (in - d2 * 0.95) * 1.15;
processed = in * (1.0 - mix) + flanged * mix;
out = processed * 1.05;
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embeds (Theme: minimal_signal)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "minimal-signal-datamatics-barcode-matrix",
        "title": "Minimal Signal Datamatics Barcode Matrix",
        "description": "High-contrast binary data matrix and barcode visualizer inspired by Ryoji Ikeda, with audio-reactive vertical scanlines, horizontal bitstreams, and stark signal orange impulses.",
        "embed_desc": "Audio-reactive binary data matrix and barcode visualizer inspired by Ryoji Ikeda. Bass pulses the high-density bitstream, mids shift vertical scanlines, and highs ignite vermilion signal impulses.",
        "tags": ["minimal", "signal", "datamatics", "binary", "matrix", "barcode", "ryoji-ikeda"],
        "params": {
            "speed": 0.35,
            "dataDensity": 16.0,
            "barcodeScale": 1.2,
            "strobeIntensity": 0.8,
            "contrast": 1.25
        },
        "code": """param float speed = 0.35 [0.05, 1.0];
param float dataDensity = 16.0 [6.0, 32.0];
param float barcodeScale = 1.2 [0.5, 2.5];
param float strobeIntensity = 0.8 [0.2, 1.0];
param float contrast = 1.25 [0.8, 1.8];

p = uv;
time = t * speed;

lane = floor(p.y * 14.0);
laneSpeed = sin(lane * 2.3) * 1.2;

xCoord = p.x * (dataDensity * barcodeScale) + time * laneSpeed;
bit = step(0.48, fract(sin(floor(xCoord) * 43758.5453 + lane * 13.37)));

centerMask = step(0.36, p.y) * step(p.y, 0.64);
fineBit = step(0.5, fract(p.x * 64.0 + time * 2.0));
denseData = mix(bit, fineBit, centerMask * 0.75);

isPulse = step(0.92, fract(sin(lane * 71.1 + floor(xCoord * 0.25)) * 12345.67));

graticuleY = step(0.94, fract(p.y * 14.0));
graticuleX = step(0.96, fract(p.x * 28.0));
grid = max(graticuleY, graticuleX) * 0.35;

cBg = vec3(0.063, 0.063, 0.063);
cWhite = vec3(0.94, 0.94, 0.94);
cRed = vec3(1.0, 0.20, 0.0);

col = cBg;
col = mix(col, cWhite, denseData * 0.88);
col = mix(col, cWhite, grid);
col = mix(col, cRed, isPulse * strobeIntensity);

out = clamp(col * contrast, 0.0, 1.0);
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
  vec2 p = uv;
  float time = t * 0.35;

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  float lane = floor(p.y * 14.0);
  float laneSpeed = sin(lane * 2.3) * (1.2 + b * 0.8);

  float density = 16.0 * (1.0 + b * 0.5);
  float xCoord = p.x * density + (time + m * 0.4) * laneSpeed;
  float bit = step(0.48, fract(sin(floor(xCoord) * 43758.5453 + lane * 13.37)));

  float centerMask = step(0.36, p.y) * step(p.y, 0.64);
  float fineBit = step(0.5, fract(p.x * (64.0 + b * 32.0) + time * 2.0));
  float denseData = mix(bit, fineBit, centerMask * 0.75);

  float isPulse = step(0.92 - h * 0.15, fract(sin(lane * 71.1 + floor(xCoord * 0.25)) * 12345.67));

  float graticuleY = step(0.94, fract(p.y * 14.0));
  float graticuleX = step(0.96, fract(p.x * 28.0));
  float grid = max(graticuleY, graticuleX) * (0.35 + m * 0.25);

  vec3 cBg = vec3(0.063, 0.063, 0.063);
  vec3 cWhite = vec3(0.94, 0.94, 0.94);
  vec3 cRed = vec3(1.0, 0.20, 0.0);

  vec3 col = cBg;
  col = mix(col, cWhite, denseData * 0.88);
  col = mix(col, cWhite, grid);
  col = mix(col, cRed, isPulse * (0.8 + h * 0.4));

  gl_FragColor = vec4(clamp(col * 1.25, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "minimal-signal-raster-oscillogram-sweep",
        "title": "Minimal Signal Raster Oscillogram Sweep",
        "description": "Precision audio-reactive oscilloscope raster grid rendering multi-channel sine-sweep waveforms, graticule coordinate graduations, and Alva Noto-style pulse-code impulses.",
        "embed_desc": "Precision audio-reactive oscilloscope raster grid rendering multi-channel sine-sweep waveforms and Alva Noto pulse-code impulses. Bass drives beam deflection amplitude, mids modulate raster lines, and highs trigger transient beam bursts.",
        "tags": ["minimal", "signal", "oscilloscope", "raster", "graticule", "waveform", "alva-noto"],
        "params": {
            "sweepSpeed": 0.40,
            "beamGlow": 0.85,
            "graticuleOpacity": 0.35,
            "harmonicFrequency": 5.0,
            "contrast": 1.20
        },
        "code": """param float sweepSpeed = 0.40 [0.05, 1.2];
param float beamGlow = 0.85 [0.3, 1.5];
param float graticuleOpacity = 0.35 [0.1, 0.8];
param float harmonicFrequency = 5.0 [2.0, 12.0];
param float contrast = 1.20 [0.8, 1.6];

p = uv;
time = t * sweepSpeed;

# CRT Oscilloscope 10x10 Graticule Grid
gx = smoothstep(0.06, 0.01, abs(fract(p.x * 10.0) - 0.5));
gy = smoothstep(0.06, 0.01, abs(fract(p.y * 10.0) - 0.5));
grid = max(gx, gy) * graticuleOpacity;

# Center Axis Calibration Ticks
centerAxisX = smoothstep(0.008, 0.002, abs(p.y - 0.5));
centerAxisY = smoothstep(0.008, 0.002, abs(p.x - 0.5));
subTicks = (step(0.85, fract(p.x * 50.0)) * centerAxisX) + (step(0.85, fract(p.y * 50.0)) * centerAxisY);

# Primary White Oscilloscope Beam Trace
wave1 = sin(p.x * 14.0 + time * 3.5) * 0.16 + sin(p.x * 38.0 - time * 2.0) * 0.04;
beam1 = smoothstep(0.018, 0.002, abs(p.y - (0.52 + wave1))) * beamGlow;

# Secondary Vermilion Pulse Waveform
wave2 = cos(p.x * harmonicFrequency * 3.0 - time * 4.2) * 0.12;
pulseGate = step(0.3, fract(p.x * 8.0 - time * 1.5));
beam2 = smoothstep(0.014, 0.002, abs(p.y - (0.46 + wave2))) * pulseGate * 1.1;

# Bottom Telemetry Spectrum Bars
barIndex = floor(p.x * 16.0);
barHeight = 0.04 + 0.12 * abs(sin(barIndex * 1.7 + time * 2.5));
barMask = step(p.y, barHeight) * step(0.04, p.y) * step(0.18, fract(p.x * 16.0));

cBg = vec3(0.063, 0.063, 0.063);
cWhite = vec3(0.95, 0.95, 0.95);
cRed = vec3(1.0, 0.20, 0.0);
cGrid = vec3(0.30, 0.32, 0.36);

col = cBg;
col = mix(col, cGrid, grid);
col = mix(col, cWhite * 0.65, subTicks * 0.6);
col = mix(col, cWhite, barMask * 0.75);
col = mix(col, cRed, beam2);
col = mix(col, cWhite, beam1);

out = clamp(col * contrast, 0.0, 1.0);
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
  vec2 p = uv;
  float time = t * 0.40;

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  float gx = smoothstep(0.06, 0.01, abs(fract(p.x * 10.0) - 0.5));
  float gy = smoothstep(0.06, 0.01, abs(fract(p.y * 10.0) - 0.5));
  float grid = max(gx, gy) * (0.35 + m * 0.2);

  float centerAxisX = smoothstep(0.008, 0.002, abs(p.y - 0.5));
  float centerAxisY = smoothstep(0.008, 0.002, abs(p.x - 0.5));
  float subTicks = (step(0.85, fract(p.x * 50.0)) * centerAxisX) + (step(0.85, fract(p.y * 50.0)) * centerAxisY);

  float wave1 = sin(p.x * 14.0 + (time + m * 0.3) * 3.5) * (0.16 + b * 0.14) + sin(p.x * 38.0 - time * 2.0) * 0.04;
  float beam1 = smoothstep(0.018 + b * 0.01, 0.002, abs(p.y - (0.52 + wave1))) * (0.85 + b * 0.3);

  float wave2 = cos(p.x * 15.0 - (time + b * 0.5) * 4.2) * 0.12;
  float pulseGate = step(0.3, fract(p.x * 8.0 - time * 1.5));
  float beam2 = smoothstep(0.014 + h * 0.01, 0.002, abs(p.y - (0.46 + wave2))) * pulseGate * (1.1 + h * 0.5);

  float barIndex = floor(p.x * 16.0);
  float barHeight = 0.04 + (0.10 + b * 0.12) * abs(sin(barIndex * 1.7 + time * 2.5));
  float barMask = step(p.y, barHeight) * step(0.04, p.y) * step(0.18, fract(p.x * 16.0));

  vec3 cBg = vec3(0.063, 0.063, 0.063);
  vec3 cWhite = vec3(0.95, 0.95, 0.95);
  vec3 cRed = vec3(1.0, 0.20, 0.0);
  vec3 cGrid = vec3(0.30, 0.32, 0.36);

  vec3 col = cBg;
  col = mix(col, cGrid, grid);
  col = mix(col, cWhite * 0.65, subTicks * 0.6);
  col = mix(col, cWhite, barMask * 0.75);
  col = mix(col, cRed, beam2);
  col = mix(col, cWhite, beam1);

  gl_FragColor = vec4(clamp(col * 1.20, 0.0, 1.0), 1.0);
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
    print("=== Creative Expansion Pack 37 Generator & Verification Gate Runner ===")
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
            print(f"FAIL validate: {slug}\\n{v.stdout}")
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        frame_png = os.path.join(out_frame_dir, "frame.png")
        f_proc = subprocess.run([BIN, "frame", test_inf, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        if '"ok":true' not in f_proc.stdout or not os.path.exists(frame_png):
            print(f"FAIL frame: {slug}\\n{f_proc.stdout}")
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
  c bg 0.04 0.05 0.09
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
            print(f"FAIL validate: {slug}\\n{v.stdout}")
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        frame_png = os.path.join(out_frame_dir, "frame.png")
        f_proc = subprocess.run([BIN, "frame", test_inf, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        if '"ok":true' not in f_proc.stdout or not os.path.exists(frame_png):
            print(f"FAIL frame: {slug}\\n{f_proc.stdout}")
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
  i note2 {nr}
  i note3 {nr}
  i note4 {nr}
  i note5 {nr}
  i note6 {nr}
  i note7 {nr+12}
  i note8 {nr}
  i note9 {nr}
  i note10 {nr}
  i note11 {nr}
  i note12 {nr}
  i note13 {nr}
  i note14 {nr+3}
  i note15 {nr+12}
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
            print(f"FAIL validate: {slug}\\n{v.stdout}")
            failed = True
            continue

        sum_json = os.path.join(TMP_DIR, f"{slug}_sum.json")
        s = subprocess.run([BIN, "audio", inf_file, sum_json, "--duration", "6"], capture_output=True, text=True)
        if '"ok":true' not in s.stdout or not os.path.exists(sum_json):
            print(f"FAIL audio command: {slug}\\n{s.stdout}")
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
            print(f"FAIL validate: {slug}\\n{v.stdout}")
            failed = True
            continue

        sum_json = os.path.join(TMP_DIR, f"{slug}_sum.json")
        s = subprocess.run([BIN, "audio", inf_file, sum_json, "--duration", "6"], capture_output=True, text=True)
        if '"ok":true' not in s.stdout or not os.path.exists(sum_json):
            print(f"FAIL audio command: {slug}\\n{s.stdout}")
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
            print(f"FAIL validate: {slug}\\n{v.stdout}")
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        frame_png = os.path.join(out_frame_dir, "frame.png")
        f_proc = subprocess.run([BIN, "frame", test_inf, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        if '"ok":true' not in f_proc.stdout or not os.path.exists(frame_png):
            print(f"FAIL frame: {slug}\\n{f_proc.stdout}")
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
                "tags": ["synth", "psytrance", "audio", "dsp", "lead", "bass"],
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
                "tags": ["effect", "electro", "audio", "dsp", "filter", "flanger"],
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
