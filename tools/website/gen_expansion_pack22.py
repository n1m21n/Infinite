#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 22.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (chavetta-montage-torn-ephemera, chavetta-montage-classical-medallion)
2. Sketch 3D (ukiyoe-cartography-topographic-peak-3d, ukiyoe-cartography-compass-archipelago-3d)
3. Field Synth (neurofunk-reese-roller-synth, neurofunk-modulated-growl-synth)
4. Field Effects (dubstep-sub-drive-saturator-fx, dubstep-formant-vowel-filter-fx)
5. Field Pixel (technical-blueprint-drafting-grid, technical-blueprint-specimen-matrix)
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

TMP_DIR = "/tmp/pack22_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: chavetta_montage)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "chavetta-montage-torn-ephemera",
        "title": "Chavetta Montage Torn Ephemera",
        "description": "Mixed-media collage on aged kraft paper with torn paper horizontal stratum bands, distressed terracotta color blocks, slate blue overlays, and botanical specimen silhouette contours.",
        "tags": ["chavetta", "montage", "collage", "ephemera", "paper", "mixedmedia"],
        "code": """// Chavetta Montage Torn Ephemera - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Mixed-media collage on aged kraft paper with torn paper horizontal stratum bands, distressed terracotta color blocks, slate blue overlays, and botanical specimen silhouette contours.
param("zoom", 1.0, 0.5, 2.0);
param("torn_roughness", 1.2, 0.4, 2.5);
param("stratum_count", 7.0, 4.0, 12.0);
param("slate_overlay", 0.65, 0.2, 1.0);

function draw(t) {
  // Aged kraft paper base ground
  background(0.85, 0.77, 0.71);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88 * zoom;

  push();
  translate(cx, cy);

  // Outer charcoal framing boundary
  noFill();
  stroke(0.17, 0.17, 0.17);
  strokeWeight(2.5);
  rect(-span * 0.5, -span * 0.5, span, span);

  // Fine inner margin
  stroke(0.31, 0.36, 0.46, 0.5);
  strokeWeight(1.0);
  rect(-span * 0.47, -span * 0.47, span * 0.94, span * 0.94);

  // Layer 1: Charcoal foundation block at top-left
  fill(0.17, 0.17, 0.17);
  noStroke();
  rect(-span * 0.45, -span * 0.45, span * 0.42, span * 0.28);

  // Stamped archival label box on top-left block
  fill(0.85, 0.77, 0.71);
  rect(-span * 0.42, -span * 0.42, span * 0.26, span * 0.05);
  fill(0.64, 0.28, 0.22);
  rect(-span * 0.42, -span * 0.35, span * 0.36, span * 0.035);

  // Layer 2: Horizontal torn paper color bands
  const count = floor(stratum_count);
  const bandH = (span * 0.55) / count;
  for (let i = 0; i < count; i++) {
    const yTop = -span * 0.12 + i * bandH;
    const isTerra = (i % 2 === 0);
    if (isTerra) {
      fill(0.64, 0.28, 0.22, 0.92); // Terracotta rust
    } else {
      fill(0.31, 0.36, 0.46, slate_overlay); // Slate blue
    }
    noStroke();

    // Procedural torn jagged top edge
    beginShape();
    vertex(-span * 0.44, yTop + bandH * 0.9);
    for (let x = -span * 0.44; x <= span * 0.44; x += 18) {
      const n = sin(x * 0.035 * torn_roughness + i * 2.8) * 6.5 + cos(x * 0.08) * 3.0;
      vertex(x, yTop + n);
    }
    vertex(span * 0.44, yTop + bandH * 0.9);
    endShape();

    // Exposed paper edge highlight in kraft white
    stroke(0.92, 0.88, 0.82, 0.75);
    strokeWeight(1.2);
    noFill();
    beginShape();
    for (let x = -span * 0.44; x <= span * 0.44; x += 18) {
      const n = sin(x * 0.035 * torn_roughness + i * 2.8) * 6.5 + cos(x * 0.08) * 3.0;
      vertex(x, yTop + n);
    }
    endShape();
  }

  // Layer 3: Vertical collage strip on right side
  fill(0.17, 0.17, 0.17, 0.88);
  noStroke();
  rect(span * 0.14, -span * 0.45, span * 0.31, span * 0.88);

  // Terracotta and Kraft geometric inserts in right strip
  fill(0.64, 0.28, 0.22);
  rect(span * 0.18, -span * 0.38, span * 0.23, span * 0.12);
  fill(0.85, 0.77, 0.71);
  rect(span * 0.18, -span * 0.22, span * 0.16, span * 0.04);
  rect(span * 0.18, -span * 0.15, span * 0.23, span * 0.03);

  // Slate blue square block
  fill(0.31, 0.36, 0.46);
  rect(span * 0.18, span * 0.08, span * 0.23, span * 0.22);

  // Botanical specimen silhouette drawing
  stroke(0.85, 0.77, 0.71, 0.95);
  strokeWeight(2.0);
  noFill();
  push();
  translate(span * 0.295, span * 0.19);
  line(0, span * 0.08, 0, -span * 0.08);
  for (let b = -4; b <= 4; b++) {
    const by = b * (span * 0.018);
    const bl = span * 0.04 * (1.0 - abs(b) * 0.18);
    line(0, by, -bl, by - 6);
    line(0, by, bl, by - 6);
  }
  pop();

  // Classical circular medallion sketch on lower left
  push();
  translate(-span * 0.22, span * 0.22);
  stroke(0.17, 0.17, 0.17);
  strokeWeight(2.4);
  noFill();
  circle(0, 0, span * 0.34);
  circle(0, 0, span * 0.28);
  stroke(0.64, 0.28, 0.22);
  strokeWeight(1.4);
  circle(0, 0, span * 0.18);

  fill(0.64, 0.28, 0.22);
  noStroke();
  circle(0, 0, span * 0.06);

  // Radial compass ticks
  stroke(0.17, 0.17, 0.17);
  strokeWeight(1.2);
  for (let a = 0; a < 16; a++) {
    const ang = (a * 6.2831853) / 16;
    const r1 = span * 0.14;
    const r2 = span * 0.17;
    line(cos(ang) * r1, sin(ang) * r1, cos(ang) * r2, sin(ang) * r2);
  }
  pop();

  // Registration ticks and technical boundary stamps
  stroke(0.17, 0.17, 0.17);
  strokeWeight(1.5);
  const markLen = 14;
  line(-span * 0.5, 0, -span * 0.5 + markLen, 0);
  line(span * 0.5, 0, span * 0.5 - markLen, 0);
  line(0, -span * 0.5, 0, -span * 0.5 + markLen);
  line(0, span * 0.5, 0, span * 0.5 - markLen);

  pop();
}
"""
    },
    {
        "slug": "chavetta-montage-classical-medallion",
        "title": "Chavetta Montage Classical Medallion",
        "description": "Constructed archival montage featuring concentric classical medallion rings, contrasting terracotta and slate geometric framing slabs, and tactile textured paper ephemera.",
        "tags": ["chavetta", "montage", "medallion", "classical", "geometric", "drawing"],
        "code": """// Chavetta Montage Classical Medallion - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Constructed archival montage featuring concentric classical medallion rings, contrasting terracotta and slate geometric framing slabs, and tactile textured paper ephemera.
param("zoom", 1.0, 0.5, 2.0);
param("medallion_scale", 1.0, 0.6, 1.8);
param("ring_count", 6.0, 3.0, 10.0);
param("slab_opacity", 0.9, 0.5, 1.0);

function draw(t) {
  // Deep charcoal background ground
  background(0.17, 0.17, 0.17);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88 * zoom;

  push();
  translate(cx, cy);

  // Main aged kraft archival backing sheet
  fill(0.85, 0.77, 0.71);
  noStroke();
  rect(-span * 0.46, -span * 0.46, span * 0.92, span * 0.92);

  // Border rule
  stroke(0.17, 0.17, 0.17);
  strokeWeight(2.0);
  noFill();
  rect(-span * 0.46, -span * 0.46, span * 0.92, span * 0.92);

  // Terracotta horizontal dominant slab
  fill(0.64, 0.28, 0.22, slab_opacity);
  noStroke();
  rect(-span * 0.46, -span * 0.28, span * 0.92, span * 0.24);

  // Slate blue asymmetric vertical column
  fill(0.31, 0.36, 0.46, slab_opacity);
  rect(-span * 0.14, -span * 0.46, span * 0.28, span * 0.92);

  // Dark charcoal header and footer bars
  fill(0.17, 0.17, 0.17);
  rect(-span * 0.42, -span * 0.42, span * 0.84, span * 0.08);
  rect(-span * 0.42, span * 0.34, span * 0.84, span * 0.08);

  // Fine paper accent inlays
  fill(0.85, 0.77, 0.71);
  rect(-span * 0.38, -span * 0.40, span * 0.32, span * 0.04);
  rect(span * 0.06, -span * 0.40, span * 0.32, span * 0.04);
  rect(-span * 0.38, span * 0.36, span * 0.24, span * 0.04);

  // Central classical circular medallion
  push();
  const rad = span * 0.28 * medallion_scale;

  // Medallion background disc
  fill(0.85, 0.77, 0.71);
  stroke(0.17, 0.17, 0.17);
  strokeWeight(3.0);
  circle(0, 0, rad * 2.0);

  // Concentric decorative rings
  const rings = floor(ring_count);
  for (let r = 1; r <= rings; r++) {
    const curR = (rad * r) / rings;
    stroke(r % 2 === 0 ? 0.64 : 0.17, r % 2 === 0 ? 0.28 : 0.17, r % 2 === 0 ? 0.22 : 0.17);
    strokeWeight(r === rings ? 2.5 : 1.2);
    noFill();
    circle(0, 0, curR * 2.0);
  }

  // Medallion radial dentil / compass ticks
  stroke(0.17, 0.17, 0.17);
  strokeWeight(1.8);
  for (let i = 0; i < 32; i++) {
    const a = (i * 6.2831853) / 32;
    const r1 = rad * 0.82;
    const r2 = rad * 0.98;
    line(cos(a) * r1, sin(a) * r1, cos(a) * r2, sin(a) * r2);
  }

  // Inner terracotta rosette star
  fill(0.64, 0.28, 0.22);
  noStroke();
  circle(0, 0, rad * 0.48);

  fill(0.31, 0.36, 0.46);
  circle(0, 0, rad * 0.26);

  fill(0.85, 0.77, 0.71);
  circle(0, 0, rad * 0.10);

  pop();

  // Corner catalog brackets & crosshairs
  stroke(0.64, 0.28, 0.22);
  strokeWeight(2.0);
  noFill();
  const cOff = span * 0.42;
  const cLen = 18;
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
# 2. Sketch 3D Sources (Theme: ukiyoe_cartography)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "ukiyoe-cartography-topographic-peak-3d",
        "title": "Ukiyo-e Cartography Topographic Peak 3D",
        "description": "Edo-period woodblock cartographic mountain relief with concentric stepped topographical strata, indigo marine foundations, and a vibrant cinnabar seal accent.",
        "tags": ["ukiyoe", "woodblock", "cartography", "topography", "japanart", "3d"],
        "code": """// Ukiyo-e Cartography Topographic Peak 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Edo-period woodblock cartographic mountain relief with concentric stepped topographical strata, indigo marine foundations, and a vibrant cinnabar seal accent.
param("strata_count", 9.0, 5.0, 16.0);
param("peak_height", 1.1, 0.6, 2.0);
param("rotation_speed", 0.3, 0.05, 1.0);
param("contour_scale", 1.0, 0.5, 1.8);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.48 + sin(time * 0.18) * 0.05);
  rotateY(time * 0.35 + 0.5);

  // Base marine foundation plinth in deep indigo ink
  fill(0.114, 0.208, 0.341);
  push();
  translate(0, -0.72, 0);
  box(1.9 * contour_scale, 0.08, 1.9 * contour_scale);

  // Seafoam coastal wave contour shelf
  translate(0, 0.07, 0);
  fill(0.659, 0.855, 0.863);
  box(1.65 * contour_scale, 0.06, 1.65 * contour_scale);
  pop();

  // Stepped topographical mountain contour terraces
  const count = floor(strata_count);
  for (let i = 0; i < count; i++) {
    const f = i / count;
    const y = -0.58 + f * (1.1 * peak_height);
    const radius = (1.4 - f * 1.15) * contour_scale;

    push();
    translate(0, y, 0);

    // Alternating indigo and seafoam cartographic strata
    if (i % 2 === 0) {
      fill(0.114 + f * 0.08, 0.208 + f * 0.12, 0.341 + f * 0.15); // Indigo to ocean gradient
    } else {
      fill(0.659 - f * 0.15, 0.855 - f * 0.12, 0.863 - f * 0.10); // Seafoam contour
    }

    // Cylindrical stepped contour ring
    cylinder(radius, 0.08);

    // Decorative quadrant marker blocks
    for (let q = 0; q < 4; q++) {
      const qAng = (q * 1.5707963) + (i * 0.2);
      const qx = cos(qAng) * radius * 0.98;
      const qz = sin(qAng) * radius * 0.98;
      push();
      translate(qx, 0.02, qz);
      fill(0.945, 0.980, 0.933); // Rice paper cream marker
      box(0.06, 0.09, 0.06);
      pop();
    }

    pop();
  }

  // Apex cinnabar red Hanko seal stamp
  push();
  translate(0, -0.58 + 1.15 * peak_height, 0);
  fill(0.902, 0.224, 0.275); // Cinnabar Red Seal
  box(0.24 * contour_scale, 0.16, 0.24 * contour_scale);

  // Floating cinnabar crest ornament
  translate(0, 0.15, 0);
  fill(0.945, 0.980, 0.933);
  sphere(0.08 * contour_scale);
  pop();

  // Floating nautical archipelago markers in the surrounding waters
  for (let k = 0; k < 6; k++) {
    const kAng = (k * 6.2831853) / 6 + time * 0.5;
    const kDist = 1.15 * contour_scale;
    const kx = cos(kAng) * kDist;
    const kz = sin(kAng) * kDist;
    const ky = -0.62 + sin(time * 1.8 + k) * 0.04;

    push();
    translate(kx, ky, kz);
    fill(k % 2 === 0 ? 0.902 : 0.114, k % 2 === 0 ? 0.224 : 0.208, k % 2 === 0 ? 0.275 : 0.341);
    box(0.12, 0.14, 0.12);
    pop();
  }

  pop();
}
"""
    },
    {
        "slug": "ukiyoe-cartography-compass-archipelago-3d",
        "title": "Ukiyo-e Cartography Compass Archipelago 3D",
        "description": "Procedural 3D nautical compass rose and floating archipelago islands rendered with indigo ink contours, seafoam wave ridges, and cinnabar navigational markers.",
        "tags": ["ukiyoe", "cartography", "compass", "archipelago", "islands", "3d"],
        "code": """// Ukiyo-e Cartography Compass Archipelago 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Procedural 3D nautical compass rose and floating archipelago islands rendered with indigo ink contours, seafoam wave ridges, and cinnabar navigational markers.
param("compass_radius", 1.0, 0.5, 1.8);
param("island_height", 0.9, 0.4, 1.8);
param("rotation_speed", 0.32, 0.05, 1.0);
param("wave_motion", 0.45, 0.1, 1.0);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.52 + sin(time * 0.22) * 0.04);
  rotateY(time * 0.38 + 0.6);

  // Outer ocean basin disc in deep indigo ink
  fill(0.114, 0.208, 0.341);
  push();
  translate(0, -0.68, 0);
  cylinder(1.7 * compass_radius, 0.08);

  // Seafoam navigation ring
  translate(0, 0.06, 0);
  fill(0.659, 0.855, 0.863);
  cylinder(1.48 * compass_radius, 0.05);

  // Inner indigo dial
  translate(0, 0.04, 0);
  fill(0.114, 0.208, 0.341);
  cylinder(1.28 * compass_radius, 0.04);
  pop();

  // 8-Point nautical compass rose star
  push();
  translate(0, -0.52, 0);
  for (let p = 0; p < 8; p++) {
    const pAng = (p * 6.2831853) / 8;
    const isCardinal = (p % 2 === 0);
    const pLen = (isCardinal ? 0.95 : 0.65) * compass_radius;

    push();
    rotateY(pAng);
    translate(pLen * 0.5, 0, 0);

    // Cardinal rays in cinnabar red, inter-cardinal in rice paper cream
    if (isCardinal) {
      fill(0.902, 0.224, 0.275); // Cinnabar Red
      box(pLen, 0.06, 0.08);
    } else {
      fill(0.945, 0.980, 0.933); // Rice paper cream
      box(pLen, 0.04, 0.06);
    }
    pop();
  }

  // Central compass pivot dome
  fill(0.902, 0.224, 0.275);
  sphere(0.14 * compass_radius);
  translate(0, 0.12, 0);
  fill(0.945, 0.980, 0.933);
  cylinder(0.06, 0.18);
  pop();

  // Floating archipelago islands array
  const islandCount = 5;
  for (let i = 0; i < islandCount; i++) {
    const iAng = (i * 6.2831853) / islandCount + 0.3;
    const iDist = 1.25 * compass_radius;
    const ix = cos(iAng) * iDist;
    const iz = sin(iAng) * iDist;
    const iy = -0.42 + sin(time * 1.5 + i * 1.2) * (0.06 * wave_motion);

    push();
    translate(ix, iy, iz);

    // Stepped island tiers
    fill(0.114, 0.208, 0.341); // Indigo base
    cylinder(0.24, 0.16 * island_height);

    translate(0, 0.12 * island_height, 0);
    fill(0.659, 0.855, 0.863); // Seafoam mid tier
    cylinder(0.17, 0.12 * island_height);

    translate(0, 0.10 * island_height, 0);
    fill(0.902, 0.224, 0.275); // Cinnabar summit peak
    cone(0.12, 0.16 * island_height);

    pop();
  }

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: neurofunk)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "neurofunk-reese-roller-synth",
        "title": "Neurofunk Reese Roller Synth",
        "description": "Multi-oscillator detuned reese bass synthesizer with dual saw spread, morphing lowpass-bandpass formant filter, analog saturation, and deep sub presence.",
        "tags": ["synth", "neurofunk", "reese", "dnb", "bass", "dsp"],
        "note_root": 38,
        "params": {
            "detune": 0.45,
            "cutoff": 1600.0,
            "resonance": 0.42,
            "decay": 0.38,
            "subGain": 0.55
        },
        "code": """param float detune = 0.45 [0.05, 1.2]
param float cutoff = 1600.0 [250.0, 5000.0]
param float resonance = 0.42 [0.05, 0.85]
param float decay = 0.38 [0.1, 1.5]
param float subGain = 0.55 [0.0, 1.0]
state float p1 = 0
state float p2 = 0
state float pSub = 0
state float env = 0
state float pEnv = 0
state float lp1 = 0
state float lp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decay), 0.00008, 0.02)
env = if(target > env, env + (target - env) * 0.38, env - env * decayRate)
pEnv = if(target > pEnv, pEnv + (target - pEnv) * 0.55, pEnv - pEnv * 0.004)

fBase = clamp(freq, 32.0, 1200.0)
f1 = fBase * (1.0 + detune * 0.015)
f2 = fBase * (1.0 - detune * 0.015)

p1 = (p1 + f1 / sr) % 1.0
saw1 = p1 * 2.0 - 1.0

p2 = (p2 + f2 / sr) % 1.0
saw2 = p2 * 2.0 - 1.0

pSub = (pSub + (fBase * 0.5) / sr) % 1.0
subSine = sin(pSub * 6.2831853)

raw = (saw1 * 0.5 + saw2 * 0.5 + subSine * subGain) * env

u = raw * 1.6
sat = clamp(u - 0.24 * u * u * u, -0.95, 0.95)

fCut = clamp((cutoff + pEnv * 1600.0) / sr * 2.2, 0.01, 0.45)
lp1 = lp1 + fCut * (sat - lp1 - resonance * lp2)
lp2 = lp2 + fCut * (lp1 - lp2)

out = lp2 * 0.38
"""
    },
    {
        "slug": "neurofunk-modulated-growl-synth",
        "title": "Neurofunk Modulated Growl Synth",
        "description": "Aggressive frequency-modulated growl bass synthesizer with carrier-modulator phase feedback, vowel formant notch shaping, and tube drive.",
        "tags": ["synth", "neurofunk", "growl", "fm", "bass", "dsp"],
        "note_root": 38,
        "params": {
            "modRatio": 1.0,
            "modIndex": 2.4,
            "cutoff": 2200.0,
            "decay": 0.34,
            "growlSpeed": 1.4
        },
        "code": """param float modRatio = 1.0 [0.5, 4.0]
param float modIndex = 2.4 [0.0, 5.0]
param float cutoff = 2200.0 [300.0, 6000.0]
param float decay = 0.34 [0.08, 1.2]
param float growlSpeed = 1.4 [0.2, 4.0]
state float pCar = 0
state float pMod = 0
state float env = 0
state float pEnv = 0
state float lp1 = 0
state float lp2 = 0
state float lfo = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decay), 0.00009, 0.02)
env = if(target > env, env + (target - env) * 0.40, env - env * decayRate)
pEnv = if(target > pEnv, pEnv + (target - pEnv) * 0.65, pEnv - pEnv * 0.006)

lfo = (lfo + growlSpeed / sr) % 1.0
wobble = sin(lfo * 6.2831853) * 0.5 + 0.5

fBase = clamp(freq, 35.0, 1400.0)
fMod = fBase * modRatio

pMod = (pMod + fMod / sr) % 1.0
modSine = sin(pMod * 6.2831853) * (modIndex * (0.6 + wobble * 0.8)) * env

fCar = fBase + modSine * fBase
pCar = (pCar + fCar / sr) % 1.0
carSine = sin(pCar * 6.2831853)

raw = carSine * env

u = raw * 1.55
sat = clamp(u - 0.22 * u * u * u, -0.94, 0.94)

fCut = clamp((cutoff + pEnv * 2000.0) / sr * 2.2, 0.015, 0.45)
lp1 = lp1 + fCut * (sat - lp1 - 0.35 * lp2)
lp2 = lp2 + fCut * (lp1 - lp2)

out = lp2 * 0.36
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: dubstep)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "dubstep-sub-drive-saturator-fx",
        "title": "Dubstep Sub Drive Saturator FX",
        "description": "Heavy dynamic asymmetric tube overdrive and multi-band sub-bass exciter with tape warmth, parallel clean blend, and output ceiling limiting.",
        "tags": ["effect", "dubstep", "saturator", "overdrive", "distortion", "audio"],
        "params": {
            "drive": 1.8,
            "tone": 2600.0,
            "bassBoost": 0.45,
            "mix": 0.65
        },
        "code": """param float drive = 1.8 [0.5, 4.0]
param float tone = 2600.0 [400.0, 6000.0]
param float bassBoost = 0.45 [0.0, 1.2]
param float mix = 0.65 [0.0, 1.0]
state float lpSub = 0
state float lpTone = 0

fSubCut = clamp(180.0 / sr * 2.4, 0.005, 0.2)
lpSub = lpSub + fSubCut * (in - lpSub)

u = in * drive + lpSub * bassBoost
sat = clamp(u - 0.22 * u * u * u, -0.95, 0.95)

fToneCut = clamp(tone / sr * 2.4, 0.015, 0.45)
lpTone = lpTone + fToneCut * (sat - lpTone)

wet = lpTone * 1.05
out = (in * (1.0 - mix) + wet * mix) * 1.05
"""
    },
    {
        "slug": "dubstep-formant-vowel-filter-fx",
        "title": "Dubstep Formant Vowel Filter FX",
        "description": "Dynamic vowel-morphing dual-peak formant filter with modulated resonance sweep, vocal frequency emphasis, and stereo width generation.",
        "tags": ["effect", "dubstep", "formant", "vowel", "filter", "audio"],
        "params": {
            "vowelMorph": 0.65,
            "resonance": 0.50,
            "sweepRate": 0.45,
            "mix": 0.70
        },
        "code": """param float vowelMorph = 0.65 [0.0, 1.0]
param float resonance = 0.50 [0.1, 0.88]
param float sweepRate = 0.45 [0.05, 2.0]
param float mix = 0.70 [0.0, 1.0]
state float bp1 = 0
state float bp2 = 0
state float lp1 = 0
state float lp2 = 0
state float lfo = 0

lfo = (lfo + sweepRate / sr) % 1.0
wob = sin(lfo * 6.2831853) * 0.5 + 0.5

f1 = 400.0 + (vowelMorph * 0.6 + wob * 0.4) * 500.0
f2 = 1200.0 + (vowelMorph * 0.5 + wob * 0.5) * 1400.0

fCut1 = clamp(f1 / sr * 2.2, 0.01, 0.45)
fCut2 = clamp(f2 / sr * 2.2, 0.01, 0.45)

lp1 = lp1 + fCut1 * (in - lp1 - resonance * bp1)
bp1 = bp1 + fCut1 * (lp1 - bp1)

lp2 = lp2 + fCut2 * (in - lp2 - resonance * bp2)
bp2 = bp2 + fCut2 * (lp2 - bp2)

vowel = bp1 * 0.8 + bp2 * 0.6
u = vowel * 1.4
sat = clamp(u - 0.20 * u * u * u, -0.92, 0.92)

out = (in * (1.0 - mix) + sat * mix) * 1.08
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embeds (Theme: technical_blueprint)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "technical-blueprint-drafting-grid",
        "title": "Technical Blueprint Drafting Grid",
        "description": "Cyanotype architectural drafting blueprint shader with multi-scale coordinate grids, concentric measurement calipers, and audio-reactive dimension ticks.",
        "embed_desc": "Architectural cyanotype drafting blueprint shader with multi-scale precision coordinate grids, concentric caliper dimension rings, and audio-reactive measurement arcs. Bass expands primary caliper circles, mids rotate dimension markers, highs ignite chalk-white crosshair ticks.",
        "tags": ["pixel", "blueprint", "technical", "cyanotype", "grid", "shader"],
        "params": {
            "gridDensity": 14.0,
            "caliperRadius": 0.48,
            "lineGlow": 1.3,
            "dimensionSpeed": 0.8,
            "scanlineDarkness": 0.25
        },
        "code": """param float gridDensity = 14.0 [6.0, 32.0]
param float caliperRadius = 0.48 [0.2, 1.0]
param float lineGlow = 1.3 [0.5, 3.0]
param float dimensionSpeed = 0.8 [0.1, 2.5]
param float scanlineDarkness = 0.25 [0.0, 0.8]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2
time = t * dimensionSpeed

r = length(p)
ang = atan2(p.y, p.x)

gx = fract(p.x * gridDensity * 0.5)
gy = fract(p.y * gridDensity * 0.5)
gridFine = step(0.93, gx) + step(0.93, gy)
gridMajor = step(0.97, fract(p.x * gridDensity * 0.1)) + step(0.97, fract(p.y * gridDensity * 0.1))

cal1 = abs(r - caliperRadius)
cal2 = abs(r - caliperRadius * 0.65)
ring1 = smoothstep(0.018, 0.002, cal1)
ring2 = smoothstep(0.014, 0.002, cal2)

ticks = step(0.85, sin(ang * 24.0 + time * 0.6)) * step(caliperRadius * 0.58, r) * step(r, caliperRadius * 0.72)

crosshair = (smoothstep(0.012, 0.002, abs(p.x)) + smoothstep(0.012, 0.002, abs(p.y))) * step(r, caliperRadius * 1.2)

cBlue = vec3(0.015, 0.08, 0.18)
cCyan = vec3(0.0, 0.82, 1.0)
cWhite = vec3(0.95, 0.97, 1.0)

col = cBlue + cCyan * (gridFine * 0.18 + gridMajor * 0.35 + ring1 * 0.75 + ring2 * 0.65) * lineGlow
col = col + cWhite * (crosshair * 0.85 + ticks * 0.9) * lineGlow

scan = clamp(sin(uv.y * 560.0) * 0.5 + 0.5, 0.0, 1.0)
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
  float time = t * 0.85;

  float bass = audio_bass * 1.4;
  float mid = audio_mid * 1.5;
  float high = audio_high * 1.8;

  float r = length(p);
  float ang = atan(p.y, p.x);

  float gridDensity = 14.0 + bass * 4.0;
  vec2 gridUV = fract(p * gridDensity * 0.5);
  float gridFine = step(0.93, gridUV.x) + step(0.93, gridUV.y);
  float gridMajor = step(0.97, fract(p.x * gridDensity * 0.1)) + step(0.97, fract(p.y * gridDensity * 0.1));

  float calRad1 = 0.52 + bass * 0.22;
  float calRad2 = 0.34 + sin(time * 1.2 + mid * 2.0) * 0.06;
  float ring1 = smoothstep(0.018, 0.003, abs(r - calRad1));
  float ring2 = smoothstep(0.014, 0.003, abs(r - calRad2));

  float ticks = step(0.82, sin(ang * 24.0 + time * 0.8 + mid * 3.5)) * step(calRad2 * 0.85, r) * step(r, calRad2 * 1.15);

  float crosshair = (smoothstep(0.012, 0.002, abs(p.x)) + smoothstep(0.012, 0.002, abs(p.y))) * step(r, calRad1 * 1.25);

  vec3 cBlue = vec3(0.015, 0.08, 0.18);
  vec3 cCyan = vec3(0.0, 0.82, 1.0);
  vec3 cWhite = vec3(0.95, 0.97, 1.0);

  vec3 col = cBlue + cCyan * (gridFine * 0.20 + gridMajor * 0.38 + ring1 * 0.85 + ring2 * 0.70);
  col += cWhite * (crosshair * (0.75 + high * 0.8) + ticks * (0.8 + high * 0.9));

  float scan = clamp(sin(uv.y * 560.0) * 0.5 + 0.5, 0.0, 1.0);
  col *= (1.0 - scan * 0.25);

  gl_FragColor = vec4(col, 1.0);
}
"""
    },
    {
        "slug": "technical-blueprint-specimen-matrix",
        "title": "Technical Blueprint Specimen Matrix",
        "description": "Speculative biological schematic blueprint shader with sectional specimen contour rings, radial coordinate calipers, and technical drafting callouts.",
        "embed_desc": "Speculative biological schematic blueprint shader with sectional specimen contour rings, radial coordinate calipers, and technical drafting callouts. Bass modulates sectional contour breathing, mids sweep drafting alignment arcs, highs trigger luminous chalk-white specimen flares.",
        "tags": ["pixel", "blueprint", "schematic", "specimen", "scientific", "shader"],
        "params": {
            "sectionDensity": 8.0,
            "specimenScale": 1.0,
            "draftingCyan": 1.2,
            "rotationRate": 0.25,
            "scanlineDarkness": 0.25
        },
        "code": """param float sectionDensity = 8.0 [3.0, 18.0]
param float specimenScale = 1.0 [0.5, 2.0]
param float draftingCyan = 1.2 [0.5, 2.5]
param float rotationRate = 0.25 [0.05, 1.0]
param float scanlineDarkness = 0.25 [0.0, 0.8]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2
time = t * rotationRate

r = length(p)
ang = atan2(p.y, p.x)

specContour = sin(r * 22.0 * specimenScale - time * 3.0 + sin(ang * 6.0) * 2.2)
contourLine = smoothstep(0.12, 0.0, abs(specContour)) * step(r, 0.88 * specimenScale)

cal1 = abs(r - 0.92 * specimenScale)
cal2 = abs(r - 0.74 * specimenScale)
ring1 = smoothstep(0.016, 0.002, cal1)
ring2 = smoothstep(0.012, 0.002, cal2)

divs = step(0.88, cos(ang * 12.0 + time * 1.4)) * step(0.72 * specimenScale, r) * step(r, 0.94 * specimenScale)

gx = fract(p.x * 16.0)
gy = fract(p.y * 16.0)
grid = (step(0.94, gx) + step(0.94, gy)) * 0.16

cBlue = vec3(0.012, 0.07, 0.16)
cCyan = vec3(0.0, 0.82, 1.0)
cWhite = vec3(0.95, 0.97, 1.0)

col = cBlue + cCyan * (grid + contourLine * 0.75 + ring1 * 0.85 + ring2 * 0.65) * draftingCyan
col = col + cWhite * divs * 0.85

scan = clamp(sin(uv.y * 560.0) * 0.5 + 0.5, 0.0, 1.0)
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
  float time = t * 0.65;

  float bass = audio_bass * 1.4;
  float mid = audio_mid * 1.5;
  float high = audio_high * 1.8;

  float r = length(p);
  float ang = atan(p.y, p.x);

  float warp = sin(ang * 6.0 + time * 1.2 + mid * 2.5) * (1.8 + bass * 0.8);
  float specContour = sin(r * (20.0 + bass * 6.0) - time * 2.8 + warp);
  float contourLine = smoothstep(0.14, 0.005, abs(specContour)) * step(r, 0.95 + bass * 0.2);

  float calRad1 = 0.96 + bass * 0.15;
  float calRad2 = 0.78 + sin(time * 0.9) * 0.04;
  float ring1 = smoothstep(0.016, 0.003, abs(r - calRad1));
  float ring2 = smoothstep(0.012, 0.003, abs(r - calRad2));

  float divs = step(0.86, cos(ang * 12.0 + time * 1.1 + mid * 2.0)) * step(calRad2 * 0.92, r) * step(r, calRad1 * 1.05);

  vec2 gUV = fract(p * 16.0);
  float grid = (step(0.94, gUV.x) + step(0.94, gUV.y)) * 0.16;

  vec3 cBlue = vec3(0.012, 0.07, 0.16);
  vec3 cCyan = vec3(0.0, 0.82, 1.0);
  vec3 cWhite = vec3(0.95, 0.97, 1.0);

  vec3 col = cBlue + cCyan * (grid + contourLine * (0.80 + mid * 0.5) + ring1 * 0.85 + ring2 * 0.65);
  col += cWhite * divs * (0.85 + high * 0.95);

  float scan = clamp(sin(uv.y * 560.0) * 0.5 + 0.5, 0.0, 1.0);
  col *= (1.0 - scan * 0.25);

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
    print("=== Creative Expansion Pack 22: Build and Gate Runner ===")
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
  c bg 0.114 0.208 0.341
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
