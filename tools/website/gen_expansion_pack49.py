#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 49.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: cosmatesque_opus_sectile -> cosmatesque-quincunx-pavement, cosmatesque-octagram-medallion)
2. Sketch 3D (Theme: candela_hypar_shell -> candela-manantiales-flower-3d, candela-umbrella-cantilever-3d)
3. Field Synth (Theme: casio_cz_phase_distortion -> cz-phase-distortion-brass-synth, glottal-formant-choir-synth)
4. Field Effects (Theme: six_stage_allpass_phaser -> six-stage-allpass-phaser-fx, resonant-vowel-formant-fx)
5. Field Pixel & Embeds (Themes: turrell_ganzfeld_aperture & pevsner_gabo_ruled -> turrell-skyspace-ganzfeld, gabo-ruled-perspex-waves)
"""
import os
import sys
import json
import subprocess
import zipfile
from PIL import Image, ImageStat

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN = os.path.join(ROOT, "build", "Infinite.app", "Contents", "MacOS", "Infinite")
if not os.path.exists(BIN):
    BIN = os.path.expanduser("~/.local/bin/Infinite")
    CMD = [BIN]
else:
    CMD = [BIN, "--cli"]

LIB_DIR = os.path.join(ROOT, "website", "assets", "library")
SKETCH_DIR = os.path.join(LIB_DIR, "sketch")
PIXEL_DIR = os.path.join(LIB_DIR, "pixel")
PREVIEWS_DIR = os.path.join(LIB_DIR, "previews")
THUMBS_DIR = os.path.join(LIB_DIR, "thumbs")
TEMPLATES_DIR = os.path.join(ROOT, "website", "assets", "templates")
EMBEDS_DIR = os.path.join(ROOT, "website", "embeds")

TMP_DIR = "/tmp/pack49_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview


# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: cosmatesque_opus_sectile)
#    Palette: #2E2D2B (slate), #6E2429 (porphyry),
#             #244235 (serpentine), #DCD2BC (travertine), #B8924A (ochre)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "cosmatesque-quincunx-pavement",
        "title": "Cosmatesque Quincunx Pavement",
        "description": "Roman opus sectile marble pavement with a five-roundel quincunx, braided guilloche stone bands, porphyry and serpentine triangles, and travertine border inlays.",
        "tags": ["cosmatesque", "mosaic", "opus-sectile", "marble", "tessellation", "procedural", "sketch"],
        "code": """// Cosmatesque Quincunx Pavement - Infinite Library (Sketch 2D)
param("tess_density", 14, 8, 22);
param("ring_braids", 24, 12, 40);
param("rot_speed", 0.08, 0.0, 0.4);
param("inlay_scale", 0.84, 0.65, 0.95);

function draw(t) {
  // Slate marble matrix (#2E2D2B)
  background(0.180, 0.176, 0.169);
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const S = min(w, h) * inlay_scale;

  // Outer travertine frame (#DCD2BC)
  noFill();
  stroke(0.863, 0.824, 0.737, 0.92);
  strokeWeight(4.0);
  rect(cx - S * 0.5, cy - S * 0.5, S, S);
  stroke(0.722, 0.573, 0.290, 0.75);
  strokeWeight(1.8);
  rect(cx - S * 0.46, cy - S * 0.46, S * 0.92, S * 0.92);

  // Opus sectile triangular spandrel tessellation
  const grid = floor(tess_density);
  const cell = (S * 0.88) / grid;
  const x0 = cx - S * 0.44;
  const y0 = cy - S * 0.44;
  for (let iy = 0; iy < grid; iy++) {
    for (let ix = 0; ix < grid; ix++) {
      const px = x0 + ix * cell;
      const py = y0 + iy * cell;
      const dx = (px + cell * 0.5 - cx) / (S * 0.5);
      const dy = (py + cell * 0.5 - cy) / (S * 0.5);
      if (sqrt(dx * dx + dy * dy) < 0.36) continue;

      const parity = (ix + iy) % 3;
      if (parity === 0) fill(0.431, 0.141, 0.161, 0.88);
      else if (parity === 1) fill(0.141, 0.259, 0.208, 0.88);
      else fill(0.863, 0.824, 0.737, 0.82);

      stroke(0.12, 0.11, 0.10, 0.85);
      strokeWeight(1.2);
      beginShape();
      vertex(px, py);
      vertex(px + cell, py);
      vertex(px + cell * 0.5, py + cell * 0.5);
      endShape();

      fill(0.722, 0.573, 0.290, 0.65);
      beginShape();
      vertex(px, py + cell);
      vertex(px + cell, py + cell);
      vertex(px + cell * 0.5, py + cell * 0.5);
      endShape();
    }
  }

  // Diagonal porphyry lozenge frame
  push();
  translate(cx, cy);
  rotate(PI * 0.25);
  fill(0.180, 0.176, 0.169, 0.94);
  stroke(0.863, 0.824, 0.737, 0.95);
  strokeWeight(5.0);
  const dSize = S * 0.54;
  rect(-dSize * 0.5, -dSize * 0.5, dSize, dSize);
  pop();

  // Five-roundel quincunx (4 satellites + 1 central porphyry disc)
  const satOff = S * 0.235;
  const satRad = S * 0.115;
  const braids = floor(ring_braids);
  for (let q = 0; q < 4; q++) {
    const a = (q / 4.0) * TAU + PI * 0.25 + t * rot_speed * 0.25;
    const rx = cx + cos(a) * satOff;
    const ry = cy + sin(a) * satOff;

    fill(0.180, 0.176, 0.169, 0.98);
    stroke(0.863, 0.824, 0.737, 0.92);
    strokeWeight(3.2);
    circle(rx, ry, satRad * 2.15);

    // Serpentine/porphyry alternating inner disc
    fill(q % 2 === 0 ? 0.141 : 0.431, q % 2 === 0 ? 0.259 : 0.141, q % 2 === 0 ? 0.208 : 0.161, 0.96);
    stroke(0.722, 0.573, 0.290, 0.90);
    strokeWeight(2.0);
    circle(rx, ry, satRad * 1.32);

    // 6-point stone star inlay
    fill(0.863, 0.824, 0.737, 0.85);
    noStroke();
    beginShape();
    for (let k = 0; k <= 12; k++) {
      const sa = (k / 12.0) * TAU + a;
      const sr = k % 2 === 0 ? satRad * 0.56 : satRad * 0.22;
      vertex(rx + cos(sa) * sr, ry + sin(sa) * sr);
    }
    endShape();
  }

  // Central imperial porphyry roundel with guilloche braid ring
  const cRad = S * 0.165;
  fill(0.180, 0.176, 0.169, 0.98);
  stroke(0.863, 0.824, 0.737, 0.96);
  strokeWeight(4.0);
  circle(cx, cy, cRad * 2.24);

  for (let i = 0; i < braids; i++) {
    const a = (i / braids) * TAU - t * rot_speed;
    const bx = cx + cos(a) * cRad * 0.95;
    const by = cy + sin(a) * cRad * 0.95;
    fill(i % 2 === 0 ? 0.722 : 0.863, i % 2 === 0 ? 0.573 : 0.824, i % 2 === 0 ? 0.290 : 0.737, 0.90);
    noStroke();
    circle(bx, by, cRad * 0.16);
  }

  fill(0.431, 0.141, 0.161, 0.98);
  stroke(0.722, 0.573, 0.290, 0.95);
  strokeWeight(2.5);
  circle(cx, cy, cRad * 1.48);

  // Central travertine rosette
  fill(0.863, 0.824, 0.737, 0.92);
  noStroke();
  beginShape();
  for (let k = 0; k <= 16; k++) {
    const ra = (k / 16.0) * TAU + t * rot_speed * 0.5;
    const rr = k % 2 === 0 ? cRad * 0.56 : cRad * 0.24;
    vertex(cx + cos(ra) * rr, cy + sin(ra) * rr);
  }
  endShape();
}"""
    },
    {
        "slug": "cosmatesque-octagram-medallion",
        "title": "Cosmatesque Octagram Medallion",
        "description": "Concentric Roman Cosmati eight-pointed marble star medallion framed by stepped chevron stone friezes, serpentine chevrons, and golden ochre tesserae.",
        "tags": ["cosmatesque", "octagram", "mosaic", "marble", "geometry", "procedural", "sketch"],
        "code": """// Cosmatesque Octagram Medallion - Infinite Library (Sketch 2D)
param("star_rings", 5, 3, 8);
param("tessera_count", 32, 16, 48);
param("drift_speed", 0.06, 0.0, 0.35);
param("chevron_teeth", 16, 8, 28);

function draw(t) {
  // Deep travertine-slate ground (#242321)
  background(0.141, 0.137, 0.129);
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const R = min(w, h) * 0.43;

  // Outer stepped chevron marble frieze
  const teeth = floor(chevron_teeth);
  for (let i = 0; i < teeth; i++) {
    const a0 = (i / teeth) * TAU;
    const a1 = ((i + 0.5) / teeth) * TAU;
    const a2 = ((i + 1.0) / teeth) * TAU;
    fill(i % 2 === 0 ? 0.431 : 0.141, i % 2 === 0 ? 0.141 : 0.259, i % 2 === 0 ? 0.161 : 0.208, 0.92);
    stroke(0.863, 0.824, 0.737, 0.85);
    strokeWeight(1.5);
    beginShape();
    vertex(cx + cos(a0) * R * 0.88, cy + sin(a0) * R * 0.88);
    vertex(cx + cos(a1) * R * 1.02, cy + sin(a1) * R * 1.02);
    vertex(cx + cos(a2) * R * 0.88, cy + sin(a2) * R * 0.88);
    endShape();
  }

  // Concentric marble opus sectile rings
  const rings = floor(star_rings);
  for (let r = rings; r >= 1; r--) {
    const u = r / rings;
    const rad = R * (0.20 + u * 0.66);
    const spin = t * drift_speed * (r % 2 === 0 ? 1 : -1);

    // Octagram star polygon
    if (r % 2 === 1) fill(0.863, 0.824, 0.737, 0.88);
    else fill(0.180, 0.176, 0.169, 0.96);
    stroke(0.722, 0.573, 0.290, 0.92);
    strokeWeight(2.4);

    beginShape();
    const pts = 16;
    for (let k = 0; k <= pts; k++) {
      const a = (k / pts) * TAU + spin;
      const rr = k % 2 === 0 ? rad : rad * 0.76;
      vertex(cx + cos(a) * rr, cy + sin(a) * rr);
    }
    endShape();

    // Ring of triangular marble tesserae
    const tc = floor(tessera_count * u) + 8;
    for (let j = 0; j < tc; j++) {
      const a = (j / tc) * TAU + spin * 0.5;
      const tr = rad * 0.84;
      const tx = cx + cos(a) * tr;
      const ty = cy + sin(a) * tr;
      const sz = (R / rings) * 0.22;
      fill(j % 3 === 0 ? 0.431 : (j % 3 === 1 ? 0.141 : 0.722),
           j % 3 === 0 ? 0.141 : (j % 3 === 1 ? 0.259 : 0.573),
           j % 3 === 0 ? 0.161 : (j % 3 === 1 ? 0.208 : 0.290), 0.90);
      noStroke();
      push();
      translate(tx, ty);
      rotate(a + PI * 0.25);
      rect(-sz * 0.5, -sz * 0.5, sz, sz);
      pop();
    }
  }

  // Central porphyry omphalos disc (#6E2429)
  fill(0.431, 0.141, 0.161, 0.98);
  stroke(0.863, 0.824, 0.737, 0.96);
  strokeWeight(3.2);
  circle(cx, cy, R * 0.30);

  fill(0.722, 0.573, 0.290, 0.95);
  noStroke();
  circle(cx, cy, R * 0.10);
}"""
    }
]


# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: candela_hypar_shell)
#    Palette: #141820 (night sky), #ECE8DF (thin-shell concrete),
#             #3A4659 (shadow crease), #2C5AA0 (cobalt curtain glass)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "candela-manantiales-flower-3d",
        "title": "Candela Manantiales Flower 3D",
        "description": "Eight-petal intersecting hyperbolic paraboloid concrete vault inspired by Felix Candela's Los Manantiales, with doubly-ruled ribs and cobalt curtain mullions.",
        "tags": ["candela", "hypar", "architecture", "concrete", "shell", "procedural", "3d"],
        "code": """// Candela Manantiales Flower 3D - Infinite Library (Sketch 3D)
param("petals", 8, 5, 12);
param("rib_steps", 12, 6, 18);
param("cam_tilt", 0.38, 0.15, 0.70);
param("spin", 0.18, 0.0, 0.45);

function draw(t) {
  rotateX(cam_tilt + 0.05 * sin(t * 0.3));
  rotateY(t * spin + 0.35);

  const nP = floor(petals);
  const nR = floor(rib_steps);

  // Travertine reflecting pool plinth
  fill(0.11, 0.14, 0.20);
  push();
  translate(0, -0.95, 0);
  box(3.8, 0.12, 3.8);
  pop();

  // Cobalt glass curtain core (#2C5AA0)
  fill(0.173, 0.353, 0.627);
  for (let p = 0; p < nP; p++) {
    const a = (p / nP) * TAU;
    push();
    rotateY(a);
    translate(0, -0.45, 0.85);
    box(0.68, 0.90, 0.04);
    pop();
  }

  // Eight intersecting hyperbolic paraboloid concrete shell petals (#ECE8DF)
  for (let p = 0; p < nP; p++) {
    const a = (p / nP) * TAU;
    push();
    rotateY(a);
    for (let u = 0; u < nR; u++) {
      const fu = u / max(1, nR - 1);
      const r = 0.25 + fu * 1.45;
      const archY = -0.78 + sin(fu * PI * 0.85) * 1.15;
      const ribW = 0.14 + fu * 0.56;
      const shade = 0.925 - fu * 0.14;
      fill(shade, shade * 0.985, shade * 0.945);

      push();
      translate(0, archY, r);
      rotateX(-0.35 + fu * 0.65);
      box(ribW, 0.06, 0.16);
      pop();
    }
    // Diagonal valley groin crease (#3A4659)
    fill(0.227, 0.275, 0.349);
    rotateY(PI / nP);
    push();
    translate(0, -0.35, 0.92);
    rotateX(0.55);
    box(0.06, 0.08, 1.55);
    pop();
    pop();
  }
}"""
    },
    {
        "slug": "candela-umbrella-cantilever-3d",
        "title": "Candela Umbrella Cantilever 3D",
        "description": "Clustered inverted hyperbolic paraboloid concrete umbrella columns with ruled timber-formwork striations and cantilevered saddle canopies.",
        "tags": ["candela", "umbrella", "cantilever", "concrete", "architecture", "procedural", "3d"],
        "code": """// Candela Umbrella Cantilever 3D - Infinite Library (Sketch 3D)
param("clusters", 4, 2, 6);
param("ruled_ribs", 10, 6, 16);
param("cam_tilt", 0.34, 0.15, 0.65);
param("spin", 0.15, 0.0, 0.45);

function draw(t) {
  rotateX(cam_tilt + 0.04 * cos(t * 0.28));
  rotateY(t * spin - 0.45);

  const nC = floor(clusters);
  const nR = floor(ruled_ribs);

  // Dark concrete plaza slab
  fill(0.14, 0.16, 0.19);
  push();
  translate(0, -1.02, 0);
  box(4.0, 0.10, 4.0);
  pop();

  for (let c = 0; c < nC; c++) {
    const ca = (c / nC) * TAU + PI * 0.25;
    const rad = nC > 1 ? 0.95 : 0.0;
    const cx = cos(ca) * rad;
    const cz = sin(ca) * rad;
    const hOff = (c % 2) * 0.22;

    push();
    translate(cx, 0, cz);

    // Tapered central concrete column
    fill(0.52, 0.54, 0.58);
    push();
    translate(0, -0.45 + hOff * 0.5, 0);
    box(0.14, 1.15 + hOff, 0.14);
    pop();

    // 4-quadrant inverted hypar umbrella canopy (#ECE8DF)
    for (let q = 0; q < 4; q++) {
      push();
      rotateY((q / 4.0) * TAU);
      for (let i = 0; i < nR; i++) {
        const u = i / max(1, nR - 1);
        const span = 0.12 + u * 0.72;
        const y = 0.10 + hOff + u * 0.36;
        const shade = 0.92 - u * 0.10;
        fill(shade, shade * 0.98, shade * 0.94);
        push();
        translate(span * 0.5, y, span * 0.5);
        rotateY(PI * 0.25);
        rotateX(-0.28);
        box(0.08 + u * 0.65, 0.045, 0.08);
        pop();
      }
      // Ruled steel edge rib (#2C5AA0)
      fill(0.173, 0.353, 0.627);
      push();
      translate(0.45, 0.28 + hOff, 0);
      rotateZ(0.42);
      box(0.95, 0.04, 0.05);
      pop();
      pop();
    }
    pop();
  }
}"""
    }
]


# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: casio_cz_phase_distortion)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "cz-phase-distortion-brass-synth",
        "title": "CZ Phase Distortion Brass Synth",
        "description": "Casio CZ-inspired cosine-windowed phase distortion synthesizer with envelope-swept resonant knee, sub-harmonic reinforcement, and warm saturation.",
        "note_root": 48,
        "params": {
            "pdDepth": 0.68,
            "resHarm": 3.5,
            "subMix": 0.24,
            "envDecay": 0.9994,
            "drive": 1.35
        },
        "code": """param float pdDepth = 0.68 [0.05, 0.94];
param float resHarm = 3.5 [1.0, 9.0];
param float subMix = 0.24 [0.0, 0.6];
param float envDecay = 0.9994 [0.997, 0.9998];
param float drive = 1.35 [0.6, 2.8];
state float p = 0; state float pSub = 0; state float env = 0; state float dc = 0;

env = if(gate > 0.5, env + (1.0 - env) * 0.035, env * envDecay);
p = (p + clamp(freq, 30.0, 5200.0) / sr) % 1.0;
pSub = (pSub + clamp(freq * 0.5, 20.0, 2600.0) / sr) % 1.0;

# Cosine-windowed phase distortion with dynamic knee
knee = clamp(0.5 - pdDepth * 0.45 * env, 0.04, 0.50);
pw = if(p < knee, p * 0.5 / knee, 0.5 + (p - knee) * 0.5 / (1.0 - knee));
win = 0.5 - 0.5 * cos(6.2831853 * p);
resOsc = cos(6.2831853 * resHarm * pw) * win;
fund = cos(6.2831853 * pw);
sub = sin(6.2831853 * pSub);

raw = (fund * 0.55 + resOsc * 0.55 + sub * subMix) * env;
dc = dc + 0.002 * (raw - dc);
clean = (raw - dc) * drive;
out = (clean / (1.0 + abs(clean) * 0.72)) * 0.56;
"""
    },
    {
        "slug": "glottal-formant-choir-synth",
        "title": "Glottal Formant Choir Synth",
        "description": "Pitch-tracked glottal pulse and three-band parallel state-variable vowel formant synthesizer with subtle vibrato and warm saturation.",
        "note_root": 50,
        "params": {
            "vowel": 0.42,
            "formantQ": 0.28,
            "vibDepth": 0.006,
            "brilliance": 0.65,
            "drive": 1.4
        },
        "code": """param float vowel = 0.42 [0.0, 1.0];
param float formantQ = 0.28 [0.08, 0.65];
param float vibDepth = 0.006 [0.0, 0.025];
param float brilliance = 0.65 [0.1, 1.0];
param float drive = 1.4 [0.6, 2.8];
state float p = 0; state float lfo = 0; state float env = 0;
state float bp = 0; state float lp = 0;

env = if(gate > 0.5, env + (1.0 - env) * 0.025, env * 0.9994);
lfo = (lfo + 5.2 / sr) % 1.0;
p = (p + clamp(freq * (1.0 + sin(6.2831853 * lfo) * vibDepth), 40.0, 4200.0) / sr) % 1.0;

# FOF-style raised-cosine glottal burst + harmonic formant carrier
win = 0.5 - 0.5 * cos(6.2831853 * p);
fof = sin(6.2831853 * (3.0 + vowel * 5.0) * p) * win;
src = (sin(6.2831853 * p) * 0.5 + fof * brilliance) * env;

g = clamp(2.0 * sin(3.14159265 * (550.0 + vowel * 1350.0) / sr), 0.02, 0.85);
hp = src - lp - formantQ * bp;
bp = bp + g * hp;
lp = lp + g * bp;

mixSig = (bp * 0.68 + lp * 0.42) * drive;
out = (mixSig / (1.0 + abs(mixSig) * 0.72)) * 0.70;
"""
    }
]


# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: six_stage_allpass_phaser)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "six-stage-allpass-phaser-fx",
        "title": "Six-Stage Allpass Phaser FX",
        "description": "Six-stage cascaded first-order allpass phaser with LFO-swept notch combs, asymmetric diode feedback saturation, and lush phase dispersion.",
        "params": {
            "lfoRate": 0.42,
            "centerHz": 920.0,
            "sweepOct": 2.4,
            "feedback": 0.62,
            "mix": 0.75
        },
        "code": """param float lfoRate = 0.42 [0.05, 4.0];
param float centerHz = 920.0 [220.0, 3200.0];
param float sweepOct = 2.4 [0.2, 4.0];
param float feedback = 0.62 [0.0, 0.88];
param float mix = 0.75 [0.0, 1.0];
state float lfo = 0; state float fb = 0;
state float ap1 = 0; state float ap2 = 0; state float ap3 = 0;
state float ap4 = 0; state float ap5 = 0; state float ap6 = 0;

lfo = (lfo + lfoRate / sr) % 1.0;
mod = sin(6.2831853 * lfo);
hz = clamp(centerHz * pow(2.0, mod * sweepOct * 0.5), 90.0, 9500.0);
w = clamp(3.14159265 * hz / sr, 0.01, 1.25);
c = (1.0 - w) / (1.0 + w);

x = in + fb * feedback;
sat = x / (1.0 + abs(x) * 0.45);

y1 = -c * sat + ap1; ap1 = sat + c * y1;
y2 = -c * y1 + ap2; ap2 = y1 + c * y2;
y3 = -c * y2 + ap3; ap3 = y2 + c * y3;
y4 = -c * y3 + ap4; ap4 = y3 + c * y4;
y5 = -c * y4 + ap5; ap5 = y4 + c * y5;
y6 = -c * y5 + ap6; ap6 = y5 + c * y6;

fb = clamp(y6, -1.0, 1.0);
phased = (sat + y6) * 0.55;
out = (in * (1.0 - mix) + phased * mix) * 0.58;
"""
    },
    {
        "slug": "resonant-vowel-formant-fx",
        "title": "Resonant Vowel Formant FX",
        "description": "Three-band parallel state-variable formant filter bank that morphs between A-E-I-O-U vocal resonances with LFO sweep and warm tube saturation.",
        "params": {
            "vowelRate": 0.35,
            "vowelSpan": 0.82,
            "resonance": 0.24,
            "drive": 1.45,
            "mix": 0.82
        },
        "code": """param float vowelRate = 0.35 [0.05, 3.5];
param float vowelSpan = 0.82 [0.1, 1.0];
param float resonance = 0.24 [0.08, 0.65];
param float drive = 1.45 [0.6, 2.8];
param float mix = 0.82 [0.0, 1.0];
state float lfo = 0;
state float b1 = 0; state float l1 = 0;
state float b2 = 0; state float l2 = 0;
state float b3 = 0; state float l3 = 0;

lfo = (lfo + vowelRate / sr) % 1.0;
v = 0.5 + 0.5 * sin(6.2831853 * lfo) * vowelSpan;

f1Hz = 310.0 + v * 460.0;
f2Hz = 840.0 + (1.0 - v) * 1380.0;
f3Hz = 2350.0 + v * 580.0;

g1 = clamp(2.0 * sin(3.14159265 * f1Hz / sr), 0.02, 0.85);
g2 = clamp(2.0 * sin(3.14159265 * f2Hz / sr), 0.02, 0.85);
g3 = clamp(2.0 * sin(3.14159265 * f3Hz / sr), 0.02, 0.85);

sat = (in * drive) / (1.0 + abs(in * drive) * 0.65);

h1 = sat - l1 - resonance * b1; b1 = b1 + g1 * h1; l1 = l1 + g1 * b1;
h2 = sat - l2 - resonance * b2; b2 = b2 + g2 * h2; l2 = l2 + g2 * b2;
h3 = sat - l3 - resonance * b3; b3 = b3 + g3 * h3; l3 = l3 + g3 * b3;

formant = (b1 * 0.58 + b2 * 0.42 + b3 * 0.24);
out = (in * (1.0 - mix) + formant * mix) * 0.65;
"""
    }
]


# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embed Sources
#    (turrell_ganzfeld_aperture & pevsner_gabo_ruled)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "turrell-skyspace-ganzfeld",
        "title": "Turrell Skyspace Ganzfeld",
        "description": "Concentric elliptical Ganzfeld light apertures in velvety crimson, luminous amber-gold, and warm ivory with atmospheric grain.",
        "embed_desc": "Luminous concentric Ganzfeld elliptical light apertures inspired by James Turrell. Bass expands the inner core, mids shift chromatic warmth, highs shimmer the rim.",
        "tags": ["turrell", "ganzfeld", "light", "aperture", "minimal", "shader", "pixel"],
        "params": {
            "ringCount": 6.0,
            "apertureEcc": 0.72,
            "glowSoftness": 0.045,
            "warmShift": 0.52,
            "grainAmt": 0.035
        },
        "code": """param float ringCount = 6.0 [3.0, 10.0];
param float apertureEcc = 0.72 [0.5, 1.0];
param float glowSoftness = 0.045 [0.01, 0.12];
param float warmShift = 0.52 [0.0, 1.0];
param float grainAmt = 0.035 [0.0, 0.10];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
pe = vec2(p.x / apertureEcc, p.y);
r = length(pe);

# Concentric stepped Ganzfeld light rings
phase = sin(t * 0.35) * 0.03;
rNorm = clamp(r * 2.15 + phase, 0.0, 1.25);

# Smooth stepped aperture bands
ringIdx = floor(rNorm * ringCount);
ringFrac = fract(rNorm * ringCount);
stepEdge = smoothstep(0.0, glowSoftness * 4.0, ringFrac);
bandVal = (ringIdx + stepEdge) / ringCount;

# Palette: ivory core -> amber gold -> coral crimson -> obsidian room
cIvory = vec3(1.000, 0.949, 0.776);
cAmber = vec3(0.957, 0.722, 0.376);
cCrimson = vec3(0.831, 0.165, 0.196);
cVoid = vec3(0.071, 0.039, 0.031);

col = mix(cIvory, cAmber, smoothstep(0.05, 0.42, bandVal + warmShift * 0.12));
col = mix(col, cCrimson, smoothstep(0.36, 0.78, bandVal));
col = mix(col, cVoid, smoothstep(0.76, 1.05, rNorm));

# Subtle optical film grain
g = fract(sin(dot(uv * 313.7 + t * 0.1, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
col = clamp(col + g * grainAmt, 0.0, 1.0);
""",
        "webgl_fs": """precision mediump float;
uniform vec2 res;
uniform float t;
uniform float aspect;
uniform float audio_bass;
uniform float audio_mid;
uniform float audio_high;

void main() {
  vec2 uv = gl_FragCoord.xy / res;
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
  float ecc = 0.72 + audio_bass * 0.08;
  vec2 pe = vec2(p.x / ecc, p.y);
  float r = length(pe);

  float ringCount = 6.0;
  float glowSoftness = 0.045 + audio_high * 0.02;
  float phase = sin(t * 0.35) * 0.03 - audio_bass * 0.06;
  float rNorm = clamp(r * 2.15 + phase, 0.0, 1.25);

  float ringIdx = floor(rNorm * ringCount);
  float ringFrac = fract(rNorm * ringCount);
  float stepEdge = smoothstep(0.0, glowSoftness * 4.0, ringFrac);
  float bandVal = (ringIdx + stepEdge) / ringCount;

  vec3 cIvory = vec3(1.000, 0.949, 0.776);
  vec3 cAmber = vec3(0.957, 0.722, 0.376);
  vec3 cCrimson = vec3(0.831, 0.165, 0.196);
  vec3 cVoid = vec3(0.071, 0.039, 0.031);

  float warmShift = 0.52 + audio_mid * 0.25;
  vec3 col = mix(cIvory, cAmber, smoothstep(0.05, 0.42, bandVal + warmShift * 0.12));
  col = mix(col, cCrimson, smoothstep(0.36, 0.78, bandVal));
  col = mix(col, cVoid, smoothstep(0.76, 1.05, rNorm));

  float g = fract(sin(dot(uv * 313.7 + t * 0.1, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
  col = clamp(col + g * (0.035 + audio_high * 0.025), 0.0, 1.0);
  gl_FragColor = vec4(col, 1.0);
}"""
    },
    {
        "slug": "gabo-ruled-perspex-waves",
        "title": "Gabo Ruled Perspex Waves",
        "description": "Constructivist Naum Gabo and Antoine Pevsner ruled-surface tensioned string families in brushed steel, warm bronze, and deep gunmetal.",
        "embed_desc": "Constructivist ruled-surface string interference inspired by Naum Gabo and Antoine Pevsner. Bass flexes the saddle envelope, mids shift string tension, highs brighten specular filaments.",
        "tags": ["gabo", "pevsner", "constructivism", "ruled-surface", "strings", "shader", "pixel"],
        "params": {
            "stringFreq": 36.0,
            "twistAmp": 0.28,
            "envelopeRad": 0.44,
            "bronzeMix": 0.48,
            "speed": 0.45
        },
        "code": """param float stringFreq = 36.0 [18.0, 64.0];
param float twistAmp = 0.28 [0.05, 0.55];
param float envelopeRad = 0.44 [0.28, 0.48];
param float bronzeMix = 0.48 [0.0, 1.0];
param float speed = 0.45 [0.1, 1.5];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
r = length(p);
a = atan2(p.y, p.x);

# Two counter-twisted ruled hyperbolic surface families
u1 = p.x * cos(0.45 + sin(t * speed) * twistAmp) - p.y * sin(0.45 + sin(t * speed) * twistAmp);
v1 = p.x * sin(0.45) + p.y * cos(0.45);
s1 = abs(sin((u1 + v1 * v1 * 1.8) * stringFreq));

u2 = p.x * cos(-0.55 - cos(t * speed * 0.8) * twistAmp) - p.y * sin(-0.55);
v2 = p.x * sin(-0.55) + p.y * cos(-0.55);
s2 = abs(sin((u2 - v2 * v2 * 1.6) * stringFreq * 0.92));

# Crisp tensioned filament lines
line1 = 1.0 - smoothstep(0.0, 0.16, s1);
line2 = 1.0 - smoothstep(0.0, 0.16, s2);

# Sculptural parabolic silhouette mask
mask = 1.0 - smoothstep(envelopeRad - 0.04, envelopeRad + 0.02, r + 0.08 * cos(2.0 * a + t * speed * 0.5));
rim = smoothstep(envelopeRad - 0.02, envelopeRad, r) * (1.0 - smoothstep(envelopeRad, envelopeRad + 0.025, r));

cGunmetal = vec3(0.078, 0.086, 0.094);
cSteel = vec3(0.545, 0.573, 0.588);
cBronze = vec3(0.784, 0.604, 0.357);
cIvory = vec3(0.910, 0.920, 0.930);

col = cGunmetal;
col = mix(col, mix(cSteel, cBronze, bronzeMix), line1 * mask * 0.85);
col = mix(col, cIvory, line2 * mask * 0.65);
col = mix(col, cBronze, rim * 0.72);
col = clamp(col, 0.0, 1.0);
""",
        "webgl_fs": """precision mediump float;
uniform vec2 res;
uniform float t;
uniform float aspect;
uniform float audio_bass;
uniform float audio_mid;
uniform float audio_high;

void main() {
  vec2 uv = gl_FragCoord.xy / res;
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
  float r = length(p);
  float a = atan(p.y, p.x);

  float stringFreq = 36.0 + audio_mid * 10.0;
  float twistAmp = 0.28 + audio_bass * 0.16;
  float speed = 0.45;

  float th1 = 0.45 + sin(t * speed) * twistAmp;
  float u1 = p.x * cos(th1) - p.y * sin(th1);
  float v1 = p.x * sin(0.45) + p.y * cos(0.45);
  float s1 = abs(sin((u1 + v1 * v1 * 1.8) * stringFreq));

  float th2 = -0.55 - cos(t * speed * 0.8) * twistAmp;
  float u2 = p.x * cos(th2) - p.y * sin(th2);
  float v2 = p.x * sin(-0.55) + p.y * cos(-0.55);
  float s2 = abs(sin((u2 - v2 * v2 * 1.6) * stringFreq * 0.92));

  float line1 = 1.0 - smoothstep(0.0, 0.16, s1);
  float line2 = 1.0 - smoothstep(0.0, 0.16, s2);

  float envRad = 0.44 + audio_bass * 0.03;
  float mask = 1.0 - smoothstep(envRad - 0.04, envRad + 0.02, r + 0.08 * cos(2.0 * a + t * speed * 0.5));
  float rim = smoothstep(envRad - 0.02, envRad, r) * (1.0 - smoothstep(envRad, envRad + 0.025, r));

  vec3 cGunmetal = vec3(0.078, 0.086, 0.094);
  vec3 cSteel = vec3(0.545, 0.573, 0.588);
  vec3 cBronze = vec3(0.784, 0.604, 0.357);
  vec3 cIvory = vec3(0.910, 0.920, 0.930) * (1.0 + audio_high * 0.22);

  vec3 col = cGunmetal;
  col = mix(col, mix(cSteel, cBronze, 0.48), line1 * mask * 0.85);
  col = mix(col, cIvory, line2 * mask * 0.65);
  col = mix(col, cBronze, rim * 0.72);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}"""
    }
]


def make_embed_html(it):
    slug = it["slug"]
    title = it["title"]
    desc = it["embed_desc"]
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
  <meta name="theme-color" content="#151930">
  <title>{title} — Infinite</title>
  <meta name="description" content="{desc}">
  <style>
    :root {{ --ground: #151930; --glass: rgba(32, 38, 63, 0.78); --ink: #EEF1FA; --ember: #FF6B35; --signal: #A7B1FF; --shadow: rgba(4, 5, 14, 0.45); }}
    * {{ box-sizing: border-box; margin: 0; padding: 0; }}
    html, body {{ width: 100%; height: 100%; overflow: hidden; background: var(--ground); font-family: Geist, system-ui, -apple-system, "Segoe UI", sans-serif; }}
    canvas {{ width: 100%; height: 100%; display: block; }}
  </style>
</head>
<body>
  <main>
    <h1 class="sr-only">{title}</h1>
    <canvas id="gl" aria-label="{title} audio-reactive visual"></canvas>
  </main>
  <script>
    const canvas = document.getElementById("gl");
    const gl = canvas.getContext("webgl");

    const vsSource = `
      attribute vec2 position;
      void main() {{
        gl_Position = vec4(position, 0.0, 1.0);
      }}
    `;

    const fsSource = `
{it['webgl_fs']}
`;

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
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]), gl.STATIC_DRAW);

    const positionLoc = gl.getAttribLocation(program, "position");
    gl.enableVertexAttribArray(positionLoc);
    gl.vertexAttribPointer(positionLoc, 2, gl.FLOAT, false, 0, 0);

    const resLoc = gl.getUniformLocation(program, "res");
    const tLoc = gl.getUniformLocation(program, "t");
    const aspectLoc = gl.getUniformLocation(program, "aspect");
    const bassLoc = gl.getUniformLocation(program, "audio_bass");
    const midLoc = gl.getUniformLocation(program, "audio_mid");
    const highLoc = gl.getUniformLocation(program, "audio_high");

    let audioBass = 0.0, audioMid = 0.0, audioHigh = 0.0;
    window.addEventListener("message", (e) => {{
      if (e.data && e.data.type === "infinite_audio") {{
        audioBass = e.data.bass || 0.0;
        audioMid = e.data.mid || 0.0;
        audioHigh = e.data.high || 0.0;
      }}
    }});

    function resize() {{
      const dpr = Math.min(window.devicePixelRatio || 1, 2);
      canvas.width = Math.floor(window.innerWidth * dpr);
      canvas.height = Math.floor(window.innerHeight * dpr);
      gl.viewport(0, 0, canvas.width, canvas.height);
    }}
    window.addEventListener("resize", resize);
    resize();

    const start = performance.now();
    function render(now) {{
      const t = (now - start) * 0.001;
      gl.uniform2f(resLoc, canvas.width, canvas.height);
      gl.uniform1f(tLoc, t);
      gl.uniform1f(aspectLoc, canvas.width / Math.max(1.0, canvas.height));
      gl.uniform1f(bassLoc, audioBass);
      gl.uniform1f(midLoc, audioMid);
      gl.uniform1f(highLoc, audioHigh);
      gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
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

        v = subprocess.run(CMD + ["validate", test_inf], capture_output=True, text=True)
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}\n{v.stderr}")
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        frame_png = os.path.join(out_frame_dir, "frame.png")
        f_proc = subprocess.run(CMD + ["frame", test_inf, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        if '"ok":true' not in f_proc.stdout or not os.path.exists(frame_png):
            print(f"FAIL frame: {slug}\n{f_proc.stdout}\n{f_proc.stderr}")
            failed = True
            continue

        im = Image.open(frame_png)
        lum = ImageStat.Stat(im.convert("L")).mean[0]
        sc = library.interest(frame_png)

        if not (14.0 <= lum <= 228.0) or sc < 0.45:
            print(f"FAIL metrics for {slug}: lum={lum:.1f}, interest={sc:.3f}")
            failed = True
            continue

        im.convert("RGB").save(os.path.join(PREVIEWS_DIR, f"{slug}.jpg"), quality=94)
        im.convert("RGB").resize((540, 540), Image.Resampling.LANCZOS).save(os.path.join(THUMBS_DIR, f"{slug}.webp"), "WEBP", quality=85)
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
  c bg 0.08 0.095 0.125
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

        v = subprocess.run(CMD + ["validate", test_inf], capture_output=True, text=True)
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}\n{v.stderr}")
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        frame_png = os.path.join(out_frame_dir, "frame.png")
        f_proc = subprocess.run(CMD + ["frame", test_inf, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        if '"ok":true' not in f_proc.stdout or not os.path.exists(frame_png):
            print(f"FAIL frame: {slug}\n{f_proc.stdout}\n{f_proc.stderr}")
            failed = True
            continue

        im = Image.open(frame_png)
        lum = ImageStat.Stat(im.convert("L")).mean[0]
        sc = library.interest(frame_png)

        if not (14.0 <= lum <= 228.0) or sc < 0.45:
            print(f"FAIL metrics for {slug}: lum={lum:.1f}, interest={sc:.3f}")
            failed = True
            continue

        im.convert("RGB").save(os.path.join(PREVIEWS_DIR, f"{slug}.jpg"), quality=94)
        im.convert("RGB").resize((540, 540), Image.Resampling.LANCZOS).save(os.path.join(THUMBS_DIR, f"{slug}.webp"), "WEBP", quality=85)
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
  i note1 {nr+2}
  i note2 {nr+4}
  i note3 {nr+7}
  i note4 {nr+9}
  i note5 {nr+7}
  i note6 {nr+4}
  i note7 {nr+2}
  i note8 {nr}
  i note9 {nr+7}
  i note10 {nr+9}
  i note11 {nr+12}
  i note12 {nr+9}
  i note13 {nr+7}
  i note14 {nr+4}
  i note15 {nr+2}
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
  c fillColor 0.24 0.30 0.36
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

        v = subprocess.run(CMD + ["validate", inf_file], capture_output=True, text=True)
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}\n{v.stderr}")
            failed = True
            continue

        sum_json = os.path.join(TMP_DIR, f"{slug}_sum.json")
        s = subprocess.run(CMD + ["audio", inf_file, sum_json, "--duration", "6"], capture_output=True, text=True)
        if '"ok":true' not in s.stdout or not os.path.exists(sum_json):
            print(f"FAIL audio command: {slug}\n{s.stdout}\n{s.stderr}")
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
        subprocess.run(CMD + ["frame", inf_file, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        im = Image.open(frame_png)
        im.convert("RGB").save(os.path.join(PREVIEWS_DIR, f"{slug}.jpg"), quality=94)
        im.convert("RGB").resize((540, 540), Image.Resampling.LANCZOS).save(os.path.join(THUMBS_DIR, f"{slug}.webp"), "WEBP", quality=85)
        template_preview.render(inf_file, os.path.join(TEMPLATES_DIR, f"{slug}.webp"))
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
  i note4 55
  i note5 50
  i note6 48
  i note7 52
  i note8 48
  i note9 55
  i note10 57
  i note11 60
  i note12 57
  i note13 55
  i note14 52
  i note15 50
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
  pos 320 1024
  f cutoff 2400.0
  f resonance 0.25
  f decay 0.45
  f volume 0.85
end
node 3 AudioEffects Field Effect
  id fx
  pos 640 1024
  s code {escaped}
end
node 4 Utility Audio Out
  id speakers
  pos 960 1024
end
node 5 Source Shape
  id pic
  pos 0 162
  c fillColor 0.28 0.22 0.16
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

        v = subprocess.run(CMD + ["validate", inf_file], capture_output=True, text=True)
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}\n{v.stderr}")
            failed = True
            continue

        sum_json = os.path.join(TMP_DIR, f"{slug}_sum.json")
        s = subprocess.run(CMD + ["audio", inf_file, sum_json, "--duration", "6"], capture_output=True, text=True)
        if '"ok":true' not in s.stdout or not os.path.exists(sum_json):
            print(f"FAIL audio command: {slug}\n{s.stdout}\n{s.stderr}")
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
        subprocess.run(CMD + ["frame", inf_file, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        im = Image.open(frame_png)
        im.convert("RGB").save(os.path.join(PREVIEWS_DIR, f"{slug}.jpg"), quality=94)
        im.convert("RGB").resize((540, 540), Image.Resampling.LANCZOS).save(os.path.join(THUMBS_DIR, f"{slug}.webp"), "WEBP", quality=85)
        template_preview.render(inf_file, os.path.join(TEMPLATES_DIR, f"{slug}.webp"))
        report.append(f"PASS [Field FX] {slug}: lufs={lufs:.2f}, peak={peak:.2f}dBFS, silence={silence}")

    # 5. Field Pixel & WebGL Embed
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
                "params": it["params"]
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

        v = subprocess.run(CMD + ["validate", test_inf], capture_output=True, text=True)
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}\n{v.stderr}")
            failed = True
            continue

        out_frame_dir = os.path.join(TMP_DIR, f"{slug}_frame")
        os.makedirs(out_frame_dir, exist_ok=True)
        frame_png = os.path.join(out_frame_dir, "frame.png")
        f_proc = subprocess.run(CMD + ["frame", test_inf, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        if '"ok":true' not in f_proc.stdout or not os.path.exists(frame_png):
            print(f"FAIL frame: {slug}\n{f_proc.stdout}\n{f_proc.stderr}")
            failed = True
            continue

        im = Image.open(frame_png)
        lum = ImageStat.Stat(im.convert("L")).mean[0]
        sc = library.interest(frame_png)

        if not (14.0 <= lum <= 228.0) or sc < 0.45:
            print(f"FAIL metrics for {slug}: lum={lum:.1f}, interest={sc:.3f}")
            failed = True
            continue

        im.convert("RGB").save(os.path.join(PREVIEWS_DIR, f"{slug}.jpg"), quality=94)
        im.convert("RGB").resize((540, 540), Image.Resampling.LANCZOS).save(os.path.join(THUMBS_DIR, f"{slug}.webp"), "WEBP", quality=85)
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
                "tags": ["synth", "phase-distortion", "formant", "audio", "dsp"],
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
                "tags": ["effect", "phaser", "allpass", "formant", "audio", "dsp"],
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

    lib_data["count"] = len(lib_data["items"])
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

    tmpl_data["count"] = len(tmpl_data["items"])
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
