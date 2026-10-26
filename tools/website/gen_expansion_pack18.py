#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 18.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (riso-manga-panel-grid, riso-manga-action-burst)
2. Sketch 3D (lilliputian-surreal-monolith-sphere-3d, lilliputian-surreal-archway-colonnade-3d)
3. Field Synth (electro-robotic-pulse-bass-synth, electro-cybernetic-sync-lead-synth)
4. Field Effects (future-garage-ghost-reverb-fx, future-garage-pitch-drift-delay-fx)
5. Field Pixel (pixel-art-8bit-dither-matrix, pixel-art-8bit-scanline-scroller)
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

TMP_DIR = "/tmp/pack18_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: riso_manga_zine)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "riso-manga-panel-grid",
        "title": "Riso Manga Panel Grid",
        "description": "Risograph manga layout with offset halftone dots, dynamic screentone hatchings, and fluorescent misregistered panel borders.",
        "tags": ["riso", "manga", "halftone", "screentone", "drawing", "print"],
        "code": """// Riso Manga Panel Grid - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Risograph manga layout with offset halftone dots, dynamic screentone hatchings, and fluorescent misregistered panel borders.
param("zoom", 1.0, 0.5, 2.0);
param("halftone_density", 24.0, 8.0, 48.0);
param("misregister", 3.5, 0.0, 10.0);
param("line_weight", 3.0, 1.0, 8.0);

function draw(t) {
  // Warm riso newsprint background
  background(0.88, 0.86, 0.80);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.86 * zoom;
  const half = span * 0.5;
  const off = misregister;

  push();
  translate(cx - half, cy - half);

  // Outer border with cyan & pink misregistration shadow
  noFill();
  stroke(0.0, 0.941, 1.0, 0.7); // Process cyan plate
  strokeWeight(line_weight);
  rect(-off, off * 0.5, span, span);

  stroke(1.0, 0.0, 0.467, 0.7); // Fluorescent pink plate
  strokeWeight(line_weight);
  rect(off * 0.5, -off, span, span);

  stroke(0.067, 0.067, 0.067, 1.0); // Carbon black keyline
  strokeWeight(line_weight + 1.0);
  rect(0, 0, span, span);

  // Panel division: Top large panel, bottom two split panels
  const splitY = span * 0.55;
  const splitX = span * 0.5;

  // Horizontal divider
  stroke(0.067, 0.067, 0.067, 1.0);
  strokeWeight(line_weight);
  line(0, splitY, span, splitY);

  // Vertical divider in bottom half
  line(splitX, splitY, splitX, span);

  // Top Panel: Halftone dot screen matrix with dynamic radii
  const cols = floor(halftone_density);
  const rows = floor(cols * 0.5);
  const cellW = span / cols;
  const cellH = splitY / rows;

  noStroke();
  for (let j = 1; j < rows; j++) {
    for (let i = 1; i < cols; i++) {
      const px = i * cellW;
      const py = j * cellH;
      const d = dist(px, py, span * 0.5, splitY * 0.5);
      const r = (sin(d * 0.015 - t * 0.8) * 0.5 + 0.5) * (cellW * 0.42);

      // Pink dot plate with offset
      fill(1.0, 0.0, 0.467, 0.65);
      circle(px + off * 0.4, py - off * 0.4, r);

      // Black core dot
      fill(0.067, 0.067, 0.067, 0.9);
      circle(px, py, r * 0.75);
    }
  }

  // Bottom-Left Panel: Diagonal screentone speedlines
  push();
  stroke(0.0, 0.941, 1.0, 0.85); // Cyan screentone lines
  strokeWeight(line_weight * 0.6);
  const step = 14.0;
  for (let k = -span; k < span; k += step) {
    const x1 = max(0, k);
    const y1 = splitY + max(0, -k * 0.5);
    const x2 = min(splitX, k + splitY);
    const y2 = min(span, splitY + (splitX - k));
    if (x1 < splitX && y1 < span) {
      line(x1, y1, x2, y2);
    }
  }
  // Misregistered pink cross-hatch
  stroke(1.0, 0.0, 0.467, 0.5);
  for (let k = 0; k < splitX + (span - splitY); k += step * 1.5) {
    line(k + off, splitY, k - (span - splitY) + off, span);
  }
  pop();

  // Bottom-Right Panel: Bold geometric manga action starburst
  push();
  translate(splitX + (span - splitX) * 0.5, splitY + (span - splitY) * 0.5);
  noStroke();
  fill(1.0, 0.933, 0.0, 0.95); // Bright yellow impact burst
  const rays = 16;
  const innerR = span * 0.06;
  const outerR = span * 0.18;
  beginShape();
  for (let a = 0; a < rays; a++) {
    const ang1 = (a / rays) * TAU;
    const ang2 = ((a + 0.5) / rays) * TAU;
    const r1 = outerR * (0.8 + 0.4 * sin(a * 2.3 + t));
    const r2 = innerR;
    vertex(cos(ang1) * r1, sin(ang1) * r1);
    vertex(cos(ang2) * r2, sin(ang2) * r2);
  }
  endShape();

  // Black impact core
  fill(0.067, 0.067, 0.067);
  circle(0, 0, innerR * 0.8);
  fill(1.0, 0.0, 0.467);
  circle(off, -off, innerR * 0.4);
  pop();

  pop();
}
"""
    },
    {
        "slug": "riso-manga-action-burst",
        "title": "Riso Manga Action Burst",
        "description": "High-energy manga speedline radial explosion with fluorescent pink dot screens and cyan kinetic ink accents.",
        "tags": ["riso", "manga", "burst", "speedlines", "action", "drawing"],
        "code": """// Riso Manga Action Burst - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// High-energy manga speedline radial explosion with fluorescent pink dot screens and cyan kinetic ink accents.
param("burst_intensity", 1.0, 0.2, 2.5);
param("ray_count", 36.0, 12.0, 72.0);
param("misregister", 4.0, 0.0, 12.0);
param("focus_x", 0.45, 0.2, 0.8);
param("focus_y", 0.48, 0.2, 0.8);

function draw(t) {
  // Warm riso newsprint cream background
  background(0.89, 0.86, 0.80);

  const cx = width * focus_x;
  const cy = height * focus_y;
  const maxR = min(width, height) * 0.75 * burst_intensity;
  const rays = floor(ray_count);
  const off = misregister;

  // Background concentric shockwave rings in Fluorescent Pink
  noFill();
  stroke(1.0, 0.0, 0.467, 0.55);
  strokeWeight(3.0);
  for (let r = 80; r < maxR; r += 70) {
    const rad = r + sin(t * 1.5 + r * 0.02) * 12.0;
    circle(cx + off, cy - off, rad);
  }

  // Cyan misregistration ghost rays
  noStroke();
  fill(0.0, 0.941, 1.0, 0.45);
  for (let i = 0; i < rays; i++) {
    if (i % 2 === 0) {
      const a1 = (i / rays) * TAU;
      const a2 = ((i + 0.6) / rays) * TAU;
      const len = maxR * (0.85 + 0.3 * sin(i * 1.7 + t));
      beginShape();
      vertex(cx - off, cy + off);
      vertex(cx - off + cos(a1) * len, cy + off + sin(a1) * len);
      vertex(cx - off + cos(a2) * len, cy + off + sin(a2) * len);
      endShape();
    }
  }

  // Carbon black keyline kinetic speedline triangles
  fill(0.067, 0.067, 0.067, 0.95);
  for (let i = 0; i < rays; i++) {
    const a1 = (i / rays) * TAU;
    const a2 = ((i + 0.45) / rays) * TAU;
    const len = maxR * (0.9 + 0.25 * cos(i * 2.3 + t * 0.8));
    beginShape();
    vertex(cx, cy);
    vertex(cx + cos(a1) * len, cy + sin(a1) * len);
    vertex(cx + cos(a2) * len, cy + sin(a2) * len);
    endShape();
  }

  // Central impact core: pure white flash aperture with yellow starburst
  fill(0.98, 0.96, 0.90);
  circle(cx, cy, 140.0);

  fill(1.0, 0.933, 0.0); // Process Yellow impact star
  beginShape();
  const starPts = 12;
  for (let s = 0; s < starPts; s++) {
    const sa1 = (s / starPts) * TAU;
    const sa2 = ((s + 0.5) / starPts) * TAU;
    const sr1 = 65.0;
    const sr2 = 24.0;
    vertex(cx + cos(sa1) * sr1, cy + sin(sa1) * sr1);
    vertex(cx + cos(sa2) * sr2, cy + sin(sa2) * sr2);
  }
  endShape();

  // Central black pinpoint
  fill(0.067, 0.067, 0.067);
  circle(cx, cy, 18.0);
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: lilliputian_surreal)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "lilliputian-surreal-monolith-sphere-3d",
        "title": "Lilliputian Surreal Monolith Sphere 3D",
        "description": "Colossal floating bone-ivory sphere hovering over slate indigo stepped plinths with miniature terracotta sentinel pedestals.",
        "tags": ["surreal", "lilliputian", "scale", "monolith", "3d", "geometry"],
        "code": """// Lilliputian Surreal Monolith Sphere 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Colossal floating bone-ivory sphere hovering over slate indigo stepped plinths with miniature terracotta sentinel pedestals.
param("elevation", 0.25, -1.0, 1.5);
param("pedestal_scale", 1.0, 0.4, 2.0);
param("ring_radius", 0.85, 0.4, 1.6);
param("rotation_speed", 0.4, 0.1, 1.2);
param("sentinel_count", 8.0, 4.0, 16.0);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.42 + sin(time * 0.25) * 0.06);
  rotateY(time * 0.3);

  // Deep slate indigo base plinths (stepped square podiums)
  fill(0.102, 0.165, 0.227);
  for (let s = 0; s < 3; s++) {
    const pw = (1.4 - s * 0.3) * pedestal_scale;
    const py = -0.7 + s * 0.08;
    push();
    translate(0, py, 0);
    box(pw, 0.08, pw);
    pop();
  }

  // Ring of miniature sentinel pedestals around the podium
  const n = floor(sentinel_count);
  for (let i = 0; i < n; i++) {
    const a = (i / n) * TAU;
    const rx = cos(a) * 0.95 * pedestal_scale;
    const rz = sin(a) * 0.95 * pedestal_scale;
    push();
    translate(rx, -0.46, rz);
    fill(0.102, 0.165, 0.227); // Slate column
    box(0.04, 0.22, 0.04);

    translate(0, 0.14, 0);
    fill(0.722, 0.290, 0.224); // Terracotta miniature beacon head
    sphere(0.035, 12);
    pop();
  }

  // Colossal floating bone-ivory sphere
  push();
  translate(0, elevation + sin(time * 0.6) * 0.08, 0);
  fill(0.910, 0.847, 0.784); // Warm bone ivory
  sphere(0.48, 36);

  // Terracotta red equatorial celestial ring
  fill(0.722, 0.290, 0.224);
  rotateX(HALF_PI + sin(time * 0.4) * 0.2);
  rotateZ(time * 0.2);
  torus(ring_radius, 0.025, 28);
  pop();

  pop();
}
"""
    },
    {
        "slug": "lilliputian-surreal-archway-colonnade-3d",
        "title": "Lilliputian Surreal Archway Colonnade 3D",
        "description": "Vast quiet architectural colonnade with towering slate pillars framing tiny floating terracotta beacon orbs.",
        "tags": ["surreal", "colonnade", "architecture", "scale", "3d", "geometry"],
        "code": """// Lilliputian Surreal Archway Colonnade 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Vast quiet architectural colonnade with towering slate pillars framing tiny floating terracotta beacon orbs.
param("corridor_length", 5.0, 3.0, 8.0);
param("arch_spacing", 0.55, 0.25, 1.0);
param("rotation_speed", 0.35, 0.1, 1.0);
param("pillar_height", 1.35, 0.6, 2.2);
param("orb_float", 0.2, 0.0, 0.6);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.38 + sin(time * 0.2) * 0.05);
  rotateY(0.45 + cos(time * 0.25) * 0.08);

  const count = floor(corridor_length);
  const spacing = arch_spacing;

  // Ground plinth corridor slab
  fill(0.102, 0.165, 0.227); // Slate indigo
  push();
  translate(0, -pillar_height * 0.52, 0);
  box(1.6, 0.08, count * spacing * 1.4);
  pop();

  // Colonnade arches
  for (let i = 0; i < count; i++) {
    const z = (i - count * 0.5) * spacing;
    push();
    translate(0, 0, z);

    // Left pillar
    fill(0.102, 0.165, 0.227);
    push();
    translate(-0.55, 0, 0);
    box(0.1, pillar_height, 0.1);
    pop();

    // Right pillar
    push();
    translate(0.55, 0, 0);
    box(0.1, pillar_height, 0.1);
    pop();

    // Archway lintel beam in warm bone ivory
    fill(0.910, 0.847, 0.784);
    push();
    translate(0, pillar_height * 0.5, 0);
    box(1.3, 0.1, 0.12);
    pop();

    // Tiny floating terracotta sentinel beacon orb
    fill(0.722, 0.290, 0.224);
    push();
    const yOff = sin(time * 1.2 + i * 0.8) * orb_float;
    translate(0, -0.1 + yOff, 0);
    sphere(0.045, 16);

    // Tiny pedestal
    fill(0.910, 0.847, 0.784);
    translate(0, -pillar_height * 0.45, 0);
    cylinder(0.03, 0.12, 12);
    pop();

    pop();
  }

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: electro)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "electro-robotic-pulse-bass-synth",
        "title": "Electro Robotic Pulse Bass Synth",
        "description": "Syncopated Detroit electro bass synthesizer with variable pulse-width bite, sub-octave snap, and fast resonant cutoff punch.",
        "tags": ["synth", "electro", "bass", "pulse", "detroit", "audio"],
        "note_root": 36,
        "params": {
            "pulseWidth": 0.35,
            "cutoff": 1400.0,
            "resonance": 0.4,
            "subLevel": 0.55,
            "decay": 0.38
        },
        "code": """param float pulseWidth = 0.35 [0.05, 0.95]
param float cutoff = 1400.0 [100.0, 6000.0]
param float resonance = 0.4 [0.0, 0.85]
param float subLevel = 0.55 [0.0, 1.0]
param float decay = 0.38 [0.05, 1.5]
state float phase1 = 0
state float subPhase = 0
state float env = 0
state float lp1 = 0
state float lp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decay), 0.00005, 0.02)
env = if(target > env, env + (target - env) * 0.25, env - env * decayRate)

fBase = clamp(freq, 25.0, 3000.0)
step1 = fBase / sr
stepSub = (fBase * 0.5) / sr

phase1 = (phase1 + step1) % 1.0
subPhase = (subPhase + stepSub) % 1.0

sq1 = if(phase1 < pulseWidth, 1.0, -1.0)
sqSub = if(subPhase < 0.5, 1.0, -1.0) * subLevel

raw = (sq1 * 0.65 + sqSub) * env

fCut = clamp((cutoff + env * 1800.0) / sr * 2.5, 0.008, 0.45)
q = clamp(resonance * 1.6, 0.0, 1.4)
lp1 = lp1 + fCut * (raw - lp1 - q * lp2)
lp2 = lp2 + fCut * (lp1 - lp2)

u = lp2 * 1.4
sat = clamp(u - 0.22 * u * u * u, -0.9, 0.9)

out = sat * 0.32
"""
    },
    {
        "slug": "electro-cybernetic-sync-lead-synth",
        "title": "Electro Cybernetic Sync Lead Synth",
        "description": "Sharp hard-sync machine lead synthesizer with metallic harmonic bite, glide articulation, and punchy stereo width.",
        "tags": ["synth", "electro", "lead", "sync", "metallic", "audio"],
        "note_root": 48,
        "params": {
            "syncRatio": 2.4,
            "cutoff": 2200.0,
            "decay": 0.42,
            "resonance": 0.45,
            "drive": 1.35
        },
        "code": """param float syncRatio = 2.4 [1.0, 5.0]
param float cutoff = 2200.0 [200.0, 7500.0]
param float decay = 0.42 [0.05, 1.8]
param float resonance = 0.45 [0.0, 0.85]
param float drive = 1.35 [0.5, 3.0]
state float masterPhase = 0
state float slavePhase = 0
state float env = 0
state float lp1 = 0
state float lp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decay), 0.00004, 0.02)
env = if(target > env, env + (target - env) * 0.2, env - env * decayRate)

fBase = clamp(freq, 40.0, 4000.0)
stepMaster = fBase / sr
stepSlave = (fBase * (syncRatio + env * 1.5)) / sr

newMaster = masterPhase + stepMaster
masterPhase = newMaster % 1.0

slavePhase = if(newMaster >= 1.0, 0.0, (slavePhase + stepSlave) % 1.0)

sawSlave = slavePhase * 2.0 - 1.0
squareMaster = if(masterPhase < 0.5, 0.4, -0.4)

raw = (sawSlave * 0.75 + squareMaster * 0.25) * env

u = raw * drive
sat = clamp(u - 0.2 * u * u * u, -0.95, 0.95)

fCut = clamp(cutoff / sr * 2.8, 0.01, 0.45)
q = clamp(resonance * 1.4, 0.0, 1.3)
lp1 = lp1 + fCut * (sat - lp1 - q * lp2)
lp2 = lp2 + fCut * (lp1 - lp2)

out = lp2 * 0.35
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: future_garage)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "future-garage-ghost-reverb-fx",
        "title": "Future Garage Ghost Reverb FX",
        "description": "Subterranean cavernous ghost reverb with pitch-drift detune diffusion, vinyl warmth saturation, and high-frequency dampening.",
        "tags": ["effect", "future-garage", "reverb", "ghost", "burial", "audio"],
        "params": {
            "decay": 0.65,
            "damping": 0.45,
            "mix": 0.55,
            "drift": 0.35,
            "tone": 1800.0
        },
        "code": """param float decay = 0.65 [0.1, 0.95]
param float damping = 0.45 [0.05, 0.9]
param float mix = 0.55 [0.0, 1.0]
param float drift = 0.35 [0.0, 1.0]
param float tone = 1800.0 [300.0, 6000.0]
state float d1 = 0
state float d2 = 0
state float d3 = 0
state float d4 = 0
state float lp = 0
state float lfo = 0

lfo = (lfo + 0.35 / sr) % 1.0
modSine = sin(lfo * 6.2831853) * drift * 0.08

d1 = d1 + 0.18 * (in - d1)
d2 = d2 + 0.12 * (d1 + in * 0.5 - d2)
d3 = d3 + 0.08 * (d2 - d3)
d4 = d4 + 0.05 * (d3 - d4)

diffused = (d1 * 0.4 + d2 * 0.3 + d3 * 0.2 + d4 * 0.1) * (1.0 + modSine)

fCut = clamp(tone / sr * 2.8, 0.01, 0.45)
lp = lp + fCut * (diffused - lp)

wet = (diffused * 0.6 + lp * 0.4) * decay
out = (in * (1.0 - mix) + wet * mix) * 1.5
"""
    },
    {
        "slug": "future-garage-pitch-drift-delay-fx",
        "title": "Future Garage Pitch Drift Delay FX",
        "description": "Syncopated stereo tape delay with flutter modulation, resonant low-shelf filtering, and warm analog cross-feedback.",
        "tags": ["effect", "future-garage", "delay", "tape", "flutter", "audio"],
        "params": {
            "delayTime": 0.32,
            "feedback": 0.55,
            "flutter": 0.28,
            "tone": 2200.0,
            "mix": 0.5
        },
        "code": """param float delayTime = 0.32 [0.05, 1.0]
param float feedback = 0.55 [0.1, 0.9]
param float flutter = 0.28 [0.0, 1.0]
param float tone = 2200.0 [200.0, 7000.0]
param float mix = 0.5 [0.0, 1.0]
state float buf1 = 0
state float buf2 = 0
state float lp = 0
state float lfo = 0

lfo = (lfo + 0.42 / sr) % 1.0
wobble = sin(lfo * 6.2831853) * flutter * 0.06

u = in * 1.2
sat = clamp(u - 0.18 * u * u * u, -0.95, 0.95)

fCut = clamp(tone / sr * 2.5, 0.01, 0.45)
sig = sat + buf2 * feedback * (1.0 + wobble)
lp = lp + fCut * (sig - lp)

buf1 = buf1 + 0.25 * (lp - buf1)
buf2 = buf2 + 0.18 * (buf1 - buf2)

wet = (buf1 * 0.6 + buf2 * 0.4)
out = (in * (1.0 - mix) + wet * mix) * 1.4
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel Sources (Theme: pixel_art_8bit)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "pixel-art-8bit-dither-matrix",
        "title": "Pixel Art 8-Bit Dither Matrix",
        "description": "Classic four-tone green Game Boy shader with Bayer ordered matrix dithering, block quantization, and CRT pixel grid scanlines.",
        "embed_desc": "Classic four-tone green Game Boy shader with Bayer ordered matrix dithering, block quantization, and CRT pixel grid scanlines. Bass drives pixel grid stepping and wave ripple, mids expand dynamic dither threshold patterns, highs illuminate brightest phosphor greens.",
        "tags": ["pixel", "8bit", "gameboy", "dither", "retro", "shader"],
        "params": {
            "pixelSize": 14.0,
            "waveSpeed": 0.65,
            "ditherDepth": 0.45,
            "scanlineDark": 0.15,
            "contrast": 1.4
        },
        "code": """param float pixelSize = 14.0 [4.0, 32.0];
param float waveSpeed = 0.65 [0.1, 2.0];
param float ditherDepth = 0.45 [0.1, 1.0];
param float scanlineDark = 0.15 [0.0, 0.5];
param float contrast = 1.4 [0.5, 2.5];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.4;
time = t * waveSpeed;

blocks = 1080.0 / pixelSize;
bx = floor(uv.x * blocks) / blocks;
by = floor(uv.y * blocks) / blocks;
bp = vec2((bx - 0.5) * aspect, by - 0.5) * 2.4;

d = length(bp);
ang = atan2(bp.y, bp.x);
w1 = sin(bp.x * 4.5 + time * 1.8);
w2 = cos(bp.y * 4.0 - time * 1.4);
w3 = sin(d * 8.0 - time * 2.2 + ang * 2.0);
raw = (w1 * 0.35 + w2 * 0.35 + w3 * 0.3) * 0.5 + 0.5;

px4 = floor(uv.x * 1080.0 / (pixelSize * 0.25)) % 4.0;
py4 = floor(uv.y * 1080.0 / (pixelSize * 0.25)) % 4.0;

bayer = if(py4 < 1.0, if(px4 < 1.0, 0.0, if(px4 < 2.0, 0.5, if(px4 < 3.0, 0.125, 0.625))), if(py4 < 2.0, if(px4 < 1.0, 0.75, if(px4 < 2.0, 0.25, if(px4 < 3.0, 0.875, 0.375))), if(py4 < 3.0, if(px4 < 1.0, 0.1875, if(px4 < 2.0, 0.6875, if(px4 < 3.0, 0.0625, 0.5625))), if(px4 < 1.0, 0.9375, if(px4 < 2.0, 0.4375, if(px4 < 3.0, 0.8125, 0.3125))))));

v = clamp((raw - 0.5) * contrast + 0.5 + (bayer - 0.5) * ditherDepth, 0.0, 1.0);

c0 = vec3(0.059, 0.220, 0.059);
c1 = vec3(0.188, 0.384, 0.188);
c2 = vec3(0.545, 0.675, 0.059);
c3 = vec3(0.608, 0.737, 0.059);

col = if(v < 0.25, c0, if(v < 0.50, c1, if(v < 0.75, c2, c3)));

scanline = sin(uv.y * 1080.0 * 3.14159) * 0.5 + 0.5;
col = col * (1.0 - scanline * scanlineDark);
alpha = 1.0;
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
  float pixelSize = 14.0 + audio_bass * 8.0;
  float waveSpeed = 0.65;
  float ditherDepth = 0.45 + audio_mid * 0.35;
  float scanlineDark = 0.15;
  float contrast = 1.4;

  float time = t * waveSpeed;

  float blocks = 1080.0 / pixelSize;
  float bx = floor(uv.x * blocks) / blocks;
  float by = floor(uv.y * blocks) / blocks;
  vec2 bp = vec2((bx - 0.5) * aspect, by - 0.5) * 2.4;

  float d = length(bp);
  float ang = atan(bp.y, bp.x);
  float w1 = sin(bp.x * 4.5 + time * 1.8 + audio_bass * 1.2);
  float w2 = cos(bp.y * 4.0 - time * 1.4);
  float w3 = sin(d * (8.0 + audio_mid * 4.0) - time * 2.2 + ang * 2.0);
  float raw = (w1 * 0.35 + w2 * 0.35 + w3 * 0.3) * 0.5 + 0.5;

  float px4 = mod(floor(gl_FragCoord.x / (pixelSize * 0.25)), 4.0);
  float py4 = mod(floor(gl_FragCoord.y / (pixelSize * 0.25)), 4.0);

  float bayer = 0.0;
  if (py4 < 1.0) {
    bayer = (px4 < 1.0) ? 0.0 : ((px4 < 2.0) ? 0.5 : ((px4 < 3.0) ? 0.125 : 0.625));
  } else if (py4 < 2.0) {
    bayer = (px4 < 1.0) ? 0.75 : ((px4 < 2.0) ? 0.25 : ((px4 < 3.0) ? 0.875 : 0.375));
  } else if (py4 < 3.0) {
    bayer = (px4 < 1.0) ? 0.1875 : ((px4 < 2.0) ? 0.6875 : ((px4 < 3.0) ? 0.0625 : 0.5625));
  } else {
    bayer = (px4 < 1.0) ? 0.9375 : ((px4 < 2.0) ? 0.4375 : ((px4 < 3.0) ? 0.8125 : 0.3125));
  }

  float v = clamp((raw - 0.5) * contrast + 0.5 + (bayer - 0.5) * ditherDepth + audio_high * 0.25, 0.0, 1.0);

  vec3 c0 = vec3(0.059, 0.220, 0.059);
  vec3 c1 = vec3(0.188, 0.384, 0.188);
  vec3 c2 = vec3(0.545, 0.675, 0.059);
  vec3 c3 = vec3(0.608, 0.737, 0.059) + vec3(audio_high * 0.25);

  vec3 col = (v < 0.25) ? c0 : ((v < 0.50) ? c1 : ((v < 0.75) ? c2 : c3));

  float scanline = sin(uv.y * res.y * 3.14159) * 0.5 + 0.5;
  col = col * (1.0 - scanline * scanlineDark);

  gl_FragColor = vec4(col, 1.0);
}
"""
    },
    {
        "slug": "pixel-art-8bit-scanline-scroller",
        "title": "Pixel Art 8-Bit Scanline Scroller",
        "description": "Retro chiptune raster parallax scroller with 4-level olive green palette quantization, scanline raster bars, and sprite mosaic cells.",
        "embed_desc": "Retro chiptune raster parallax scroller with 4-level olive green palette quantization, scanline raster bars, and sprite mosaic cells. Bass modulates raster bar amplitude and ripple displacement, mids control parallax layer interference, highs trigger radiant phosphor strobe pulses.",
        "tags": ["pixel", "8bit", "scanline", "raster", "chiptune", "shader"],
        "params": {
            "scrollSpeed": 0.85,
            "pixelBlock": 10.0,
            "scanlineDepth": 0.16,
            "barDensity": 6.0,
            "contrast": 1.3
        },
        "code": """param float scrollSpeed = 0.85 [0.1, 3.0];
param float pixelBlock = 10.0 [4.0, 24.0];
param float scanlineDepth = 0.16 [0.0, 0.4];
param float barDensity = 6.0 [2.0, 16.0];
param float contrast = 1.3 [0.5, 2.5];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.4;
time = t * scrollSpeed;

blocks = 1080.0 / pixelBlock;
bx = floor(uv.x * blocks) / blocks;
by = floor(uv.y * blocks) / blocks;
bp = vec2((bx - 0.5) * aspect, by - 0.5) * 2.4;

hill1 = sin(bp.x * 3.0 + time * 1.5) * 0.25 - 0.2;
hill2 = sin(bp.x * 6.0 - time * 0.8) * 0.15 - 0.5;
hill3 = cos(bp.x * 12.0 + time * 2.2) * 0.08 - 0.7;

rasterBar = sin(bp.y * barDensity * 3.14159 - time * 4.0) * 0.5 + 0.5;
stars = sin(floor(bp.x * 40.0) * 123.45 + floor(bp.y * 40.0) * 678.9) * 0.5 + 0.5;
starBrite = if(stars > 0.94, 0.85, 0.1);

sky = rasterBar * 0.35 + starBrite * 0.4;
ground = if(bp.y < hill3, 0.85, if(bp.y < hill2, 0.6, if(bp.y < hill1, 0.4, sky)));

v = clamp((ground - 0.5) * contrast + 0.5, 0.0, 1.0);

c0 = vec3(0.059, 0.220, 0.059);
c1 = vec3(0.188, 0.384, 0.188);
c2 = vec3(0.545, 0.675, 0.059);
c3 = vec3(0.608, 0.737, 0.059);

col = if(v < 0.25, c0, if(v < 0.50, c1, if(v < 0.75, c2, c3)));

scan = sin(uv.y * 1080.0 * 3.14159) * 0.5 + 0.5;
col = col * (1.0 - scan * scanlineDepth);
alpha = 1.0;
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
  float scrollSpeed = 0.85;
  float pixelBlock = 10.0 + audio_bass * 6.0;
  float scanlineDepth = 0.16;
  float barDensity = 6.0;
  float contrast = 1.3;

  float time = t * scrollSpeed;

  float blocks = 1080.0 / pixelBlock;
  float bx = floor(uv.x * blocks) / blocks;
  float by = floor(uv.y * blocks) / blocks;
  vec2 bp = vec2((bx - 0.5) * aspect, by - 0.5) * 2.4;

  float hill1 = sin(bp.x * 3.0 + time * 1.5) * 0.25 - 0.2;
  float hill2 = sin(bp.x * 6.0 - time * 0.8 + audio_mid * 0.5) * 0.15 - 0.5;
  float hill3 = cos(bp.x * 12.0 + time * 2.2) * 0.08 - 0.7;

  float rasterBar = sin(bp.y * barDensity * 3.14159 - time * 4.0 + audio_bass * 2.0) * 0.5 + 0.5;
  float stars = sin(floor(bp.x * 40.0) * 123.45 + floor(bp.y * 40.0) * 678.9) * 0.5 + 0.5;
  float starBrite = (stars > 0.94) ? (0.85 + audio_high * 0.5) : 0.1;

  float sky = rasterBar * (0.35 + audio_bass * 0.2) + starBrite * 0.4;
  float ground = (bp.y < hill3) ? 0.85 : ((bp.y < hill2) ? 0.6 : ((bp.y < hill1) ? 0.4 : sky));

  float v = clamp((ground - 0.5) * contrast + 0.5, 0.0, 1.0);

  vec3 c0 = vec3(0.059, 0.220, 0.059);
  vec3 c1 = vec3(0.188, 0.384, 0.188);
  vec3 c2 = vec3(0.545, 0.675, 0.059);
  vec3 c3 = vec3(0.608, 0.737, 0.059) + vec3(audio_high * 0.2);

  vec3 col = (v < 0.25) ? c0 : ((v < 0.50) ? c1 : ((v < 0.75) ? c2 : c3));

  float scan = sin(uv.y * res.y * 3.14159) * 0.5 + 0.5;
  col = col * (1.0 - scan * scanlineDepth);

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
</html>"""

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
  c bg 0.86 0.84 0.80
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
  pos 1020 1024
end
node 4 Modulators Audio Analyze
  id ana
  pos 0 2200
end
node 5 Source Shape
  id pic
  pos 0 162
  c fillColor 0.12 0.35 0.58
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
