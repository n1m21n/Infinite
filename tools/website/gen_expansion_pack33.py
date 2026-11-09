#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 33.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: ukiyoe_cartography -> ukiyoe-cartography-archipelago-wave-contour, ukiyoe-cartography-mountain-strata-relief)
2. Sketch 3D (Theme: chavetta_montage -> chavetta-montage-assemblage-monolith-3d, chavetta-montage-cantilever-ephemera-3d)
3. Field Synth (Theme: neo_classical -> neo-classical-felt-piano-harmonic-synth, neo-classical-chamber-strings-cello-synth)
4. Field Effects (Theme: ambient_piano -> ambient-piano-cloud-reverb-diffusion-fx, ambient-piano-tape-flutter-delay-fx)
5. Field Pixel (Theme: retro_surreal -> retro-surreal-celestial-portal-matrix, retro-surreal-astronomical-clockwork-horizon)
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

TMP_DIR = "/tmp/pack33_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: ukiyoe_cartography)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "ukiyoe-cartography-archipelago-wave-contour",
        "title": "Ukiyo-e Cartography Archipelago Wave Contour",
        "description": "Traditional Edo woodblock cartography rendering archipelagic coastline contours with rhythmic indigo sea wave ripples, ochre topographic mountain ridges, and cinnabar seal marks.",
        "tags": ["ukiyoe", "cartography", "woodblock", "archipelago", "wave", "drawing"],
        "code": """// Ukiyo-e Cartography Archipelago Wave Contour - Infinite Library (Sketch)
param("wave_density", 18.0, 8.0, 36.0);
param("swell_height", 0.045, 0.01, 0.12);
param("gold_accents", 0.45, 0.1, 0.9);
param("speed", 0.25, 0.05, 0.8);

function draw(t) {
  // Archival handmade Japanese Washi paper ground (#EADEC9)
  background(0.918, 0.871, 0.788);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;
  const time = t * speed;

  push();
  translate(cx, cy);

  // 1. Traditional Cartographic Woodblock Border (Indigo & Cinnabar ruled frame)
  stroke(0.110, 0.176, 0.259); // Deep Prussian Indigo
  strokeWeight(3.5);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  stroke(0.639, 0.220, 0.153); // Lacquer Cinnabar Vermilion
  strokeWeight(1.5);
  rect(-span * 0.465, -span * 0.465, span * 0.93, span * 0.93);

  // 2. Archipelagic Island Landforms (Layered Kyoto Ochre Gold #C99E5C)
  fill(0.788, 0.620, 0.361);
  stroke(0.110, 0.176, 0.259);
  strokeWeight(2.0);

  // Main Honshu-inspired mountainous island silhouette
  beginShape();
  const nIslandPts = 60;
  for (let i = 0; i <= nIslandPts; i++) {
    const a = (i / nIslandPts) * Math.PI * 2.0;
    const rBase = span * (0.22 + 0.08 * sin(a * 3.0) + 0.04 * cos(a * 7.0));
    const ix = cos(a) * rBase * 1.35;
    const iy = sin(a) * rBase * 0.75 - span * 0.06;
    vertex(ix, iy);
  }
  endShape(1);

  // Secondary archipelagic islets
  const islets = [
    [-span * 0.30, span * 0.18, span * 0.07],
    [-span * 0.18, span * 0.26, span * 0.05],
    [span * 0.26, span * 0.15, span * 0.08],
    [span * 0.34, span * 0.05, span * 0.04]
  ];
  for (let isl of islets) {
    ellipse(isl[0], isl[1], isl[2] * 2.0, isl[2] * 1.2);
  }

  // 3. Topographic Mountain Ridge Lines inside the islands (Fine Indigo Strata)
  stroke(0.110, 0.176, 0.259, 0.75);
  strokeWeight(1.2);
  noFill();
  for (let r = 1; r <= 6; r++) {
    const scaleFrac = 1.0 - r * 0.14;
    beginShape();
    for (let i = 0; i <= nIslandPts; i++) {
      const a = (i / nIslandPts) * Math.PI * 2.0;
      const rBase = span * (0.22 + 0.08 * sin(a * 3.0) + 0.04 * cos(a * 7.0)) * scaleFrac;
      const ix = cos(a) * rBase * 1.35;
      const iy = sin(a) * rBase * 0.75 - span * 0.06;
      vertex(ix, iy);
    }
    endShape(1);
  }

  // 4. Rhythmic Great Wave Ocean Contours (Traditional Ukiyo-e Seigaiha / Okinami wave ridges)
  const nWaves = floor(wave_density);
  const waveSpanY = span * 0.40;
  for (let w = 0; w < nWaves; w++) {
    const wy = span * 0.05 + (w / nWaves) * waveSpanY;
    const wavePhase = w * 0.45 + time * 1.5;

    stroke(0.110, 0.176, 0.259);
    strokeWeight(1.8 - (w / nWaves) * 0.8);
    noFill();

    beginShape();
    for (let x = -span * 0.46; x <= span * 0.46; x += 10.0) {
      const xFrac = x / span;
      const crest = sin(xFrac * 14.0 + wavePhase) * span * swell_height
                  + cos(xFrac * 28.0 - wavePhase * 1.2) * span * (swell_height * 0.45);
      vertex(x, wy + crest);
    }
    endShape();

    // Occasional foam curl claw crests
    if (w % 3 === 0) {
      fill(0.918, 0.871, 0.788);
      stroke(0.110, 0.176, 0.259);
      strokeWeight(1.0);
      for (let fx = -span * 0.40; fx <= span * 0.40; fx += span * 0.16) {
        const curlY = wy + sin(fx / span * 14.0 + wavePhase) * span * swell_height;
        circle(fx + sin(w) * 12.0, curlY - 3.0, 3.5);
      }
    }
  }

  // 5. Traditional Japanese Cartographic Seal Marks (Hanko / Cinnabar Red Cartouche)
  noStroke();
  fill(0.639, 0.220, 0.153); // Vermilion red seal
  rect(span * 0.32, -span * 0.42, span * 0.11, span * 0.15, 3.0);

  // Seal interior character relief
  fill(0.918, 0.871, 0.788);
  rect(span * 0.34, -span * 0.39, span * 0.07, span * 0.015);
  rect(span * 0.365, -span * 0.40, span * 0.02, span * 0.09);
  rect(span * 0.34, -span * 0.33, span * 0.07, span * 0.015);

  // Compass Rose / Cartographic Cardinal Orientation Star in Indigo & Gold
  push();
  translate(-span * 0.34, -span * 0.34);
  stroke(0.110, 0.176, 0.259);
  strokeWeight(1.4);
  noFill();
  circle(0, 0, span * 0.14);
  circle(0, 0, span * 0.10);

  fill(0.639, 0.220, 0.153);
  for (let pt = 0; pt < 4; pt++) {
    rotate(Math.PI * 0.5);
    triangle(0, -span * 0.065, -span * 0.015, -span * 0.01, span * 0.015, -span * 0.01);
  }
  pop();

  pop();
}
"""
    },
    {
        "slug": "ukiyoe-cartography-mountain-strata-relief",
        "title": "Ukiyo-e Cartography Mountain Strata Relief",
        "description": "Procedural woodblock relief map featuring logarithmic elevation strata lines, fine indigo shadow slope hatching, sacred summit crater snow contours, and cinnabar waypoints.",
        "tags": ["ukiyoe", "cartography", "mountain", "strata", "woodblock", "drawing"],
        "code": """// Ukiyo-e Cartography Mountain Strata Relief - Infinite Library (Sketch)
param("contour_levels", 24.0, 10.0, 48.0);
param("steepness", 0.65, 0.2, 1.2);
param("hatch_density", 0.40, 0.1, 0.8);
param("drift", 0.20, 0.05, 0.6);

function draw(t) {
  // Archival washi paper background (#EADEC9)
  background(0.918, 0.871, 0.788);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;
  const time = t * drift;

  push();
  translate(cx, cy);

  // 1. Traditional Woodblock Outer Border
  stroke(0.110, 0.176, 0.259);
  strokeWeight(3.5);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  stroke(0.639, 0.220, 0.153);
  strokeWeight(1.4);
  rect(-span * 0.465, -span * 0.465, span * 0.93, span * 0.93);

  // 2. Base Mountain Ridge Polygon (Ochre Gold Ground #C99E5C)
  fill(0.788, 0.620, 0.361);
  stroke(0.110, 0.176, 0.259);
  strokeWeight(2.2);

  // Hero sacred peak silhouette (Mt. Fuji / Sacred volcanic ridge)
  beginShape();
  vertex(-span * 0.44, span * 0.44);
  vertex(-span * 0.44, span * 0.22);
  vertex(-span * 0.30, span * 0.16);
  vertex(-span * 0.18, span * 0.02);
  vertex(-span * 0.08, -span * 0.32);
  vertex(0.0, -span * 0.35);
  vertex(span * 0.08, -span * 0.32);
  vertex(span * 0.22, span * 0.08);
  vertex(span * 0.35, span * 0.18);
  vertex(span * 0.44, span * 0.26);
  vertex(span * 0.44, span * 0.44);
  endShape(1);

  // 3. Topographical Elevation Strata / Logarithmic Contour Contours
  const levels = floor(contour_levels);
  noFill();

  for (let lvl = 1; lvl < levels; lvl++) {
    const frac = lvl / levels;
    const yBaseline = span * (0.42 - frac * 0.74);
    const amp = span * 0.06 * (1.0 - frac * 0.45) * steepness;
    const peakSpread = span * (0.44 * (1.0 - frac * 0.72));

    stroke(0.110, 0.176, 0.259, 0.85);
    strokeWeight(1.1 + (1.0 - frac) * 0.8);

    beginShape();
    for (let x = -peakSpread; x <= peakSpread; x += 8.0) {
      const normX = x / peakSpread;
      const gaussian = exp(-normX * normX * 3.5);
      const ripple = sin(normX * 8.0 + frac * 2.5 + time) * amp * 0.25;
      const yContour = yBaseline - gaussian * amp * 2.8 + ripple;
      vertex(x, yContour);
    }
    endShape();

    // Woodblock fine slant-hatching on shadow slopes (West face)
    if (lvl % 2 === 0) {
      stroke(0.110, 0.176, 0.259, 0.45);
      strokeWeight(0.9);
      const hCount = floor(12 * (1.0 - frac));
      for (let h = 0; h < hCount; h++) {
        const hx = -peakSpread * (0.3 + 0.6 * (h / max(1, hCount)));
        const hy = yBaseline - exp(-(hx / peakSpread) * (hx / peakSpread) * 3.5) * amp * 2.8;
        line(hx, hy, hx + span * 0.018, hy + span * 0.022);
      }
    }
  }

  // 4. Summit Snow Cap / Sacred Crater in Indigo & Washi relief
  fill(0.918, 0.871, 0.788);
  stroke(0.110, 0.176, 0.259);
  strokeWeight(1.8);
  beginShape();
  vertex(-span * 0.08, -span * 0.32);
  vertex(-span * 0.05, -span * 0.24);
  vertex(-span * 0.02, -span * 0.28);
  vertex(span * 0.01, -span * 0.23);
  vertex(span * 0.04, -span * 0.27);
  vertex(span * 0.08, -span * 0.32);
  vertex(0.0, -span * 0.35);
  endShape(1);

  // 5. Traditional Red Torii / Shrine Waypoint & Cartographic Inscriptions
  noStroke();
  fill(0.639, 0.220, 0.153); // Vermilion red

  // Torii shrine icon on ridge
  const tx = span * 0.20;
  const ty = span * 0.10;
  rect(tx - 12.0, ty - 16.0, 24.0, 3.5);
  rect(tx - 10.0, ty - 11.0, 20.0, 2.5);
  rect(tx - 8.0, ty - 11.0, 3.0, 15.0);
  rect(tx + 5.0, ty - 11.0, 3.0, 15.0);

  // Vermilion signature cartouche in top-left corner
  rect(-span * 0.42, -span * 0.42, span * 0.09, span * 0.18, 2.0);
  fill(0.918, 0.871, 0.788);
  rect(-span * 0.40, -span * 0.38, span * 0.05, span * 0.016);
  rect(-span * 0.38, -span * 0.34, span * 0.016, span * 0.07);
  rect(-span * 0.40, -span * 0.28, span * 0.05, span * 0.016);

  // Ocean wave baseline ripples at foothills
  stroke(0.110, 0.176, 0.259);
  strokeWeight(1.4);
  noFill();
  for (let w = 0; w < 4; w++) {
    const wy = span * (0.34 + w * 0.025);
    beginShape();
    for (let wx = -span * 0.44; wx <= span * 0.44; wx += 12.0) {
      vertex(wx, wy + sin(wx * 0.04 + w * 0.8 + time) * 3.5);
    }
    endShape();
  }

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: chavetta_montage)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "chavetta-montage-assemblage-monolith-3d",
        "title": "Chavetta Montage Assemblage Monolith 3D",
        "description": "Dynamic 3D mixed-media assemblage sculpture featuring rotated corrugated cardboard cantilever slabs, burnt sienna oil-stick steles, muted teal xerox planes, and deep structural shadow relief.",
        "tags": ["chavetta", "montage", "assemblage", "mixedmedia", "cardboard", "3d"],
        "code": """// Chavetta Montage Assemblage Monolith 3D - Infinite Library (Sketch 3D)
param("spin", 0.32, 0.1, 1.2);
param("scale_factor", 1.15, 0.5, 2.0);
param("cantilever_reach", 1.4, 0.6, 2.4);
param("slab_count", 6.0, 3.0, 10.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.48 + sin(time * 0.11) * 0.03);
  rotateY(time * 0.25 + 0.65);

  // 1. Heavy Base Plinth in Raw Cardboard & Charcoal
  push();
  translate(0, 1.20 * scale_factor, 0);
  fill(0.106, 0.106, 0.114);
  box(2.6 * scale_factor, 0.12, 2.6 * scale_factor);

  translate(0, -0.08, 0);
  fill(0.824, 0.773, 0.690);
  box(2.1 * scale_factor, 0.06, 2.1 * scale_factor);
  pop();

  // 2. Central Monolithic Assemblage Spine (Burnt Sienna & Raw Board)
  push();
  translate(0, 0.15 * scale_factor, 0);
  fill(0.784, 0.353, 0.196);
  box(0.55 * scale_factor, 2.1 * scale_factor, 0.45 * scale_factor);

  translate(0.32 * scale_factor, 0, 0);
  fill(0.824, 0.773, 0.690);
  box(0.15 * scale_factor, 2.2 * scale_factor, 0.35 * scale_factor);

  translate(-0.64 * scale_factor, 0, 0);
  fill(0.290, 0.420, 0.424);
  box(0.15 * scale_factor, 1.8 * scale_factor, 0.35 * scale_factor);
  pop();

  // 3. Cantilevered Mixed-Media Slabs (Stepped & Angled Collage Plates)
  const nSlabs = floor(slab_count);
  for (let k = 0; k < nSlabs; k++) {
    const frac = k / nSlabs;
    const yPos = (0.90 - k * 0.42) * scale_factor;
    const shiftX = ((k % 2 === 0) ? 0.38 : -0.38) * cantilever_reach * scale_factor;
    const shiftZ = ((k % 3 === 0) ? 0.28 : -0.28) * scale_factor;
    const slabW = (1.5 - frac * 0.35) * scale_factor;
    const slabD = (0.95 - frac * 0.20) * scale_factor;

    push();
    translate(shiftX, yPos, shiftZ);
    rotateY((k * 0.35) - 0.2);

    if (k % 3 === 0) {
      fill(0.784, 0.353, 0.196);
    } else if (k % 3 === 1) {
      fill(0.290, 0.420, 0.424);
    } else {
      fill(0.824, 0.773, 0.690);
    }
    box(slabW, 0.14 * scale_factor, slabD);

    push();
    translate(0, 0.08 * scale_factor, 0);
    fill(0.106, 0.106, 0.114);
    box(slabW * 0.85, 0.04 * scale_factor, slabD * 0.85);
    pop();

    pop();
  }

  // 4. Diagonal Bracing & Asymmetrical Relief Steles
  push();
  translate(0.48 * scale_factor, -0.15 * scale_factor, 0.25 * scale_factor);
  rotateZ(0.42);
  fill(0.784, 0.353, 0.196);
  box(0.10 * scale_factor, 1.6 * scale_factor, 0.12 * scale_factor);
  pop();

  push();
  translate(-0.48 * scale_factor, -0.10 * scale_factor, -0.22 * scale_factor);
  rotateZ(-0.38);
  fill(0.290, 0.420, 0.424);
  box(0.12 * scale_factor, 1.5 * scale_factor, 0.10 * scale_factor);
  pop();

  // 5. Crown Ephemera Finial (Stacked Offset Rectangles)
  push();
  translate(0, -1.05 * scale_factor, 0);
  fill(0.824, 0.773, 0.690);
  box(0.75 * scale_factor, 0.12 * scale_factor, 0.75 * scale_factor);

  translate(0, -0.10 * scale_factor, 0);
  fill(0.784, 0.353, 0.196);
  box(0.45 * scale_factor, 0.08 * scale_factor, 0.45 * scale_factor);

  translate(0, -0.08 * scale_factor, 0);
  fill(0.106, 0.106, 0.114);
  box(0.22 * scale_factor, 0.06 * scale_factor, 0.22 * scale_factor);
  pop();

  pop();
}
"""
    },
    {
        "slug": "chavetta-montage-cantilever-ephemera-3d",
        "title": "Chavetta Montage Cantilever Ephemera 3D",
        "description": "Architectural 3D paperboard assemblage sculpture with interlocking offset rectilinear panels, raw burnt sienna geometric plates, and tactile paper layering.",
        "tags": ["chavetta", "montage", "cantilever", "ephemera", "cardboard", "3d"],
        "code": """// Chavetta Montage Cantilever Ephemera 3D - Infinite Library (Sketch 3D)
param("spin", 0.35, 0.1, 1.2);
param("scale_factor", 1.35, 0.5, 2.0);
param("offset_angle", 0.45, 0.1, 1.0);
param("tiers", 6.0, 3.0, 8.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.48 + sin(time * 0.12) * 0.03);
  rotateY(time * 0.27 + 0.55);

  // 1. Asymmetrical Stepped Sub-Base
  push();
  translate(0, 1.25 * scale_factor, 0);
  fill(0.078, 0.078, 0.086);
  box(2.8 * scale_factor, 0.14, 2.8 * scale_factor);

  translate(0.15 * scale_factor, -0.09, 0.10 * scale_factor);
  fill(0.243, 0.447, 0.459);
  box(2.2 * scale_factor, 0.07, 2.0 * scale_factor);

  translate(-0.25 * scale_factor, -0.06, -0.15 * scale_factor);
  fill(0.929, 0.890, 0.816);
  box(1.8 * scale_factor, 0.06, 1.7 * scale_factor);
  pop();

  // 2. Central Core Stele Tower with Vertical Slit Cavities
  push();
  translate(0, 0.10 * scale_factor, 0);
  fill(0.929, 0.890, 0.816);
  box(0.65 * scale_factor, 2.4 * scale_factor, 0.65 * scale_factor);

  fill(0.078, 0.078, 0.086);
  box(0.45 * scale_factor, 2.3 * scale_factor, 0.72 * scale_factor);
  box(0.72 * scale_factor, 2.3 * scale_factor, 0.45 * scale_factor);
  pop();

  // 3. Cantilevered Ephemera Plates & Repeated Window Cutouts
  const nTiers = floor(tiers);
  for (let k = 0; k < nTiers; k++) {
    const frac = k / nTiers;
    const yPos = (0.95 - k * 0.42) * scale_factor;
    const rot = frac * Math.PI * 1.45 + time * 0.12;
    const len = (1.5 * (1.15 - frac * 0.20)) * scale_factor;

    push();
    translate(0, yPos, 0);
    rotateY(rot);

    push();
    translate(len * 0.5, 0, 0);
    if (k % 3 === 0) fill(0.816, 0.345, 0.157);
    else if (k % 3 === 1) fill(0.243, 0.447, 0.459);
    else fill(0.929, 0.890, 0.816);
    box(len, 0.15 * scale_factor, 0.32 * scale_factor);

    for (let w = 0; w < 3; w++) {
      push();
      translate(-len * 0.35 + w * (len * 0.32), 0, 0);
      fill(0.078, 0.078, 0.086);
      box(len * 0.18, 0.10 * scale_factor, 0.36 * scale_factor);
      pop();
    }

    translate(len * 0.44, 0.16 * scale_factor, 0);
    fill(0.929, 0.890, 0.816);
    box(0.35 * scale_factor, 0.25 * scale_factor, 0.35 * scale_factor);

    fill(0.078, 0.078, 0.086);
    box(0.20 * scale_factor, 0.15 * scale_factor, 0.38 * scale_factor);
    pop();

    push();
    translate(len * 0.30, 0.22 * scale_factor, 0);
    rotateZ(-0.45);
    fill(0.816, 0.345, 0.157);
    box(len * 0.65, 0.08 * scale_factor, 0.16 * scale_factor);
    pop();

    pop();
  }

  // 4. External Diagonal Exoskeleton Bracing Columns
  push();
  translate(0.65 * scale_factor, 0.20 * scale_factor, 0.40 * scale_factor);
  rotateZ(0.50);
  fill(0.243, 0.447, 0.459);
  box(0.12 * scale_factor, 2.3 * scale_factor, 0.12 * scale_factor);
  pop();

  push();
  translate(-0.65 * scale_factor, 0.20 * scale_factor, -0.40 * scale_factor);
  rotateZ(-0.50);
  fill(0.816, 0.345, 0.157);
  box(0.12 * scale_factor, 2.3 * scale_factor, 0.12 * scale_factor);
  pop();

  // 5. Crown Observation Deck
  push();
  translate(0, -1.25 * scale_factor, 0);
  fill(0.929, 0.890, 0.816);
  box(0.85 * scale_factor, 0.15 * scale_factor, 0.85 * scale_factor);

  translate(0, -0.12 * scale_factor, 0);
  fill(0.078, 0.078, 0.086);
  box(0.65 * scale_factor, 0.10 * scale_factor, 0.65 * scale_factor);
  pop();

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: neo_classical)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "neo-classical-felt-piano-harmonic-synth",
        "title": "Neo-Classical Felt Piano Harmonic Synth",
        "description": "Intimate felt upright piano synthesizer featuring dual detuned acoustic hammer harmonics, warm soundboard body resonance, and gentle felt damping.",
        "tags": ["synth", "neoclassical", "piano", "felt", "acoustic", "audio"],
        "note_root": 60,
        "params": {
            "cutoff": 0.54,
            "resonance": 0.32,
            "hardness": 0.40,
            "decayTime": 0.55,
            "felt": 0.45
        },
        "code": """param float cutoff = 0.54 [0.1, 1.0]
param float resonance = 0.32 [0.1, 0.9]
param float hardness = 0.40 [0.0, 1.0]
param float decayTime = 0.55 [0.1, 1.8]
param float felt = 0.45 [0.0, 1.0]
state float p1 = 0
state float p2 = 0
state float env = 0
state float s1 = 0
state float s2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decayTime), 0.0001, 0.05)
env = if(target > env, env + (target - env) * 0.95, env - env * decayRate)

dp = freq / sr
p1 = (p1 + dp) % 1.0
tri = abs(p1 * 2.0 - 1.0) * 2.0 - 1.0

p2 = (p2 + dp * 2.002) % 1.0
h2 = (p2 * 2.0 - 1.0) * 0.35 * hardness

sig = (tri * 0.75 + h2 * 0.45) * 1.2

fNorm = clamp(cutoff * 0.18 + env * (1.0 - felt * 0.6) * 0.25, 0.015, 0.44)
kRes = clamp(resonance * 1.5, 0.1, 1.8)

s1 = s1 + fNorm * (sig - s2 * kRes - s1)
s2 = s2 + fNorm * (s1 - s2)

sat = (s2 / (1.0 + abs(s2 * 0.38))) * env
out = sat * 0.56
"""
    },
    {
        "slug": "neo-classical-chamber-strings-cello-synth",
        "title": "Neo-Classical Chamber Strings Cello Synth",
        "description": "Expressive neo-classical solo cello synthesizer with slow bow attack envelope, warm resonant formant filtering, subtle vibrato modulation, and rich acoustic string undertones.",
        "tags": ["synth", "neoclassical", "strings", "cello", "acoustic", "audio"],
        "note_root": 48,
        "params": {
            "cutoff": 0.50,
            "resonance": 0.45,
            "bowSpeed": 0.40,
            "vibrato": 0.35,
            "warmth": 0.48
        },
        "code": """param float cutoff = 0.50 [0.1, 1.0]
param float resonance = 0.45 [0.1, 0.95]
param float bowSpeed = 0.40 [0.05, 1.0]
param float vibrato = 0.35 [0.0, 1.0]
param float warmth = 0.48 [0.0, 1.0]
state float p1 = 0
state float p2 = 0
state float lfo = 0
state float env = 0
state float s1 = 0
state float s2 = 0

target = if(gate > 0.5, 1.0, 0.0)
attackRate = clamp(1.0 / (sr * (0.04 + (1.0 - bowSpeed) * 0.12)), 0.0001, 0.1)
decayRate = clamp(1.0 / (sr * 0.45), 0.0001, 0.05)
env = if(target > env, env + attackRate * (target - env), env - env * decayRate)

lfo = (lfo + 5.2 / sr) % 1.0
vibMod = sin(lfo * 6.28318) * vibrato * 0.012

dp = (freq / sr) * (1.0 + vibMod)
p1 = (p1 + dp) % 1.0
saw = p1 * 2.0 - 1.0

p2 = (p2 + dp * 1.004) % 1.0
subSaw = p2 * 2.0 - 1.0

sig = (saw * 0.65 + subSaw * 0.55) * (1.0 + warmth * 0.8)

fNorm = clamp(cutoff * 0.16 + env * 0.22, 0.02, 0.45)
kRes = clamp(resonance * 1.6, 0.1, 1.85)

s1 = s1 + fNorm * (sig - s2 * kRes - s1)
s2 = s2 + fNorm * (s1 - s2)

sat = (s2 / (1.0 + abs(s2 * 0.40))) * env
out = sat * 0.54
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: ambient_piano)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "ambient-piano-cloud-reverb-diffusion-fx",
        "title": "Ambient Piano Cloud Reverb Diffusion FX",
        "description": "Lush ambient space reverberator featuring multi-stage allpass diffusion network, soft tape saturation in the feedback path, and gentle high-cut air damping.",
        "tags": ["effect", "ambient", "reverb", "diffusion", "piano", "audio"],
        "params": {
            "decay": 0.55,
            "tone": 0.50,
            "size": 0.60,
            "shimmer": 0.35,
            "mix": 0.55
        },
        "code": """param float decay = 0.55 [0.1, 0.9]
param float tone = 0.50 [0.1, 1.0]
param float size = 0.60 [0.1, 1.0]
param float shimmer = 0.35 [0.0, 1.0]
param float mix = 0.55 [0.0, 1.0]
state float a1 = 0
state float a2 = 0
state float a3 = 0
state float a4 = 0
state float lp = 0

fTone = clamp(0.06 + tone * 0.36, 0.02, 0.45)
lp = lp + fTone * (in - lp)

kDiff = clamp(0.12 * (1.0 + size * 0.8), 0.04, 0.38)
kDecay = clamp(decay * 0.82, 0.1, 0.88)

a1 = a1 + kDiff * (lp + a4 * kDecay - a1)
a2 = a2 + kDiff * 0.85 * (a1 - a2)
a3 = a3 + kDiff * 0.72 * (a2 - a3)
a4 = a4 + kDiff * 0.65 * (a3 - a4)

shim = (a1 - a3) * shimmer * 0.4
diff = (a2 + a4 * 0.8 + shim) * 0.92

sat = diff / (1.0 + abs(diff * 0.25))
wet = sat * 0.85
processed = in * (1.0 - mix) + wet * mix
out = processed * 0.70
"""
    },
    {
        "slug": "ambient-piano-tape-flutter-delay-fx",
        "title": "Ambient Piano Tape Flutter Delay FX",
        "description": "Warm vintage magnetic tape delay with slow capstan wow-and-flutter pitch modulation, resonant tone damping, and decaying tape saturation.",
        "tags": ["effect", "ambient", "tape", "delay", "flutter", "audio"],
        "params": {
            "delayTime": 0.50,
            "flutter": 0.40,
            "tone": 0.52,
            "feedback": 0.45,
            "mix": 0.55
        },
        "code": """param float delayTime = 0.50 [0.1, 1.0]
param float flutter = 0.40 [0.0, 1.0]
param float tone = 0.52 [0.1, 1.0]
param float feedback = 0.45 [0.0, 0.85]
param float mix = 0.55 [0.0, 1.0]
state float lfo = 0
state float d1 = 0
state float d2 = 0
state float d3 = 0
state float lp = 0

lfo = (lfo + 3.8 / sr) % 1.0
flutterMod = sin(lfo * 6.28318) * flutter * 0.08

kRate = clamp(0.10 * (1.0 + delayTime * 0.8) + flutterMod, 0.03, 0.35)
fTone = clamp(0.06 + tone * 0.35, 0.02, 0.45)

inSat = in / (1.0 + abs(in * 0.25))
fbIn = inSat + d3 * feedback * 0.75

lp = lp + fTone * (fbIn - lp)

d1 = d1 + kRate * (lp - d1)
d2 = d2 + kRate * 0.88 * (d1 - d2)
d3 = d3 + kRate * 0.78 * (d2 - d3)

delaySig = (d2 * 0.65 + d3 * 0.55)
processed = in * (1.0 - mix) + delaySig * mix
out = processed * 0.68
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel Sources & Embeds (Theme: retro_surreal)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "retro-surreal-celestial-portal-matrix",
        "title": "Retro-Surreal Celestial Portal Matrix",
        "description": "Audio-reactive retro-surrealist shader depicting an antique celestial sphere inside terracotta and teal nested archways with copperplate engraving hatching.",
        "embed_desc": "Audio-reactive retro-surrealist visualizer featuring antique celestial engraving rings inside terracotta and vintage teal portals. Bass pulses the central astral orb and cosmic ring radius, mids warp concentric engraving contour lines, and highs illuminate celestial starburst flares.",
        "tags": ["pixel", "retrosurreal", "portal", "celestial", "collage", "shader"],
        "params": {
            "speed": 0.30,
            "portalWarp": 0.45,
            "ringCount": 18.0,
            "celestialScale": 0.48,
            "contrast": 1.15
        },
        "code": """param float speed = 0.30 [0.05, 1.0]
param float portalWarp = 0.45 [0.1, 1.0]
param float ringCount = 18.0 [6.0, 32.0]
param float celestialScale = 0.48 [0.2, 0.9]
param float contrast = 1.15 [0.8, 1.6]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2
time = t * speed

r = length(p)
theta = atan2(p.y, p.x)

archDist = max(abs(p.x) * 1.15, p.y + abs(p.x) * 0.4)
arch = smoothstep(0.75, 0.72, archDist)

warp = sin(theta * 6.0 + time * 1.8) * cos(r * 10.0 - time * 1.4) * portalWarp * 0.22
rings = sin((r + warp) * ringCount * 2.5)
ringLines = smoothstep(0.22, -0.22, abs(rings) - 0.28)

sunR = length(p - vec2(0.0, -0.15))
sunDisc = smoothstep(celestialScale, celestialScale - 0.04, sunR)
rays = sin(atan2(p.y + 0.15, p.x) * 16.0 + time * 2.0)
sunRays = smoothstep(0.3, -0.3, abs(rays) - 0.4) * step(sunR, celestialScale * 1.6) * step(celestialScale * 0.8, sunR)

cross = sin(p.x * 32.0 + p.y * 18.0) * cos(p.y * 32.0 - p.x * 18.0)
hatch = step(0.60, cross) * (1.0 - arch) * 0.35

cTwilight = vec3(0.157, 0.188, 0.267)
cTeal = vec3(0.235, 0.431, 0.443)
cTerracotta = vec3(0.851, 0.420, 0.263)
cParchment = vec3(0.851, 0.706, 0.561)

col = mix(cTwilight, cTeal, ringLines * 0.75)
col = mix(col, cTerracotta, arch * 0.85)
col = mix(col, cParchment, sunDisc)
col = mix(col, cTerracotta, sunRays * 0.8)
col = mix(col, cParchment, hatch)

out = clamp(col * contrast, 0.0, 1.0)
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
  float time = t * 0.30;

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  float r = length(p);
  float theta = atan(p.y, p.x);

  float archDist = max(abs(p.x) * 1.15, p.y + abs(p.x) * 0.4);
  float arch = smoothstep(0.75 + b * 0.08, 0.72, archDist);

  float warp = sin(theta * (6.0 + m * 2.0) + time * 1.8) * cos(r * (10.0 + m * 2.0) - time * 1.4) * (0.45 + b * 0.25) * 0.22;
  float rings = sin((r + warp) * (18.0 + m * 6.0) * 2.5);
  float ringLines = smoothstep(0.22, -0.22, abs(rings) - (0.28 + b * 0.06));

  float sunR = length(p - vec2(0.0, -0.15));
  float sunScale = 0.48 + b * 0.12;
  float sunDisc = smoothstep(sunScale, sunScale - 0.04, sunR);
  float rays = sin(atan(p.y + 0.15, p.x) * 16.0 + time * 2.0);
  float sunRays = smoothstep(0.3, -0.3, abs(rays) - 0.4) * step(sunR, sunScale * 1.6) * step(sunScale * 0.8, sunR);

  float cross = sin(p.x * 32.0 + p.y * 18.0) * cos(p.y * 32.0 - p.x * 18.0);
  float hatch = step(0.60 - h * 0.15, cross) * (1.0 - arch) * 0.35;

  vec3 cTwilight = vec3(0.157, 0.188, 0.267);
  vec3 cTeal = vec3(0.235, 0.431, 0.443);
  vec3 cTerracotta = vec3(0.851, 0.420, 0.263);
  vec3 cParchment = vec3(0.851, 0.706, 0.561);

  vec3 col = mix(cTwilight, cTeal, ringLines * 0.75);
  col = mix(col, cTerracotta, arch * 0.85);
  col = mix(col, cParchment, sunDisc);
  col = mix(col, cTerracotta, sunRays * 0.8);
  col = mix(col, cParchment, hatch);

  gl_FragColor = vec4(clamp(col * 1.15, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "retro-surreal-astronomical-clockwork-horizon",
        "title": "Retro-Surreal Astronomical Clockwork Horizon",
        "description": "Metaphysical audio-reactive visualizer featuring intersecting astrolabe rings, antique lunar crescent geometry, and twilight atmospheric gradients.",
        "embed_desc": "Audio-reactive metaphysical collage visualizer featuring intersecting astronomical astrolabe rings and antique lunar disc geometry. Bass drives the gravitational warp, mids modulate fine copperplate engraving hatching density, and highs sweep ethereal atmospheric twilight gradients.",
        "tags": ["pixel", "retrosurreal", "astronomy", "clockwork", "lunar", "shader"],
        "params": {
            "speed": 0.28,
            "astrolabeRings": 16.0,
            "warpStrength": 0.40,
            "lunarPhase": 0.35,
            "contrast": 1.15
        },
        "code": """param float speed = 0.28 [0.05, 1.0]
param float astrolabeRings = 16.0 [6.0, 32.0]
param float warpStrength = 0.40 [0.1, 0.9]
param float lunarPhase = 0.35 [0.0, 1.0]
param float contrast = 1.15 [0.8, 1.6]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2
time = t * speed

r = length(p)
theta = atan2(p.y, p.x)

orbitWarp = sin(theta * 8.0 + time * 1.5) * cos(r * 12.0 - time * 1.2) * warpStrength * 0.24
lensR = r + orbitWarp

rings = sin(lensR * astrolabeRings * 2.2)
astrolabeLines = smoothstep(0.24, -0.24, abs(rings) - 0.26)

meridian = sin(p.x * 14.0 + sin(p.y * 6.0) * 1.2)
meridianLines = smoothstep(0.20, -0.20, abs(meridian) - 0.25) * step(r, 0.95)

lunarPos = p - vec2(0.25 * cos(time * 0.8), 0.20 * sin(time * 0.8))
lunarR = length(lunarPos)
lunarDisc = smoothstep(0.42, 0.38, lunarR)

lunarCrescentDist = length(lunarPos - vec2(0.14, 0.08))
lunarShadow = smoothstep(0.38, 0.34, lunarCrescentDist)
crescent = clamp(lunarDisc - lunarShadow, 0.0, 1.0)

horizon = p.y + sin(p.x * 6.0 + time * 0.5) * 0.08 + 0.35
horizonMask = step(horizon, 0.0)

cross = sin(p.x * 28.0) * cos(p.y * 28.0)
hatch = step(0.62, cross) * (1.0 - lunarDisc) * 0.32

cTwilight = vec3(0.157, 0.188, 0.267)
cTeal = vec3(0.235, 0.431, 0.443)
cTerracotta = vec3(0.851, 0.420, 0.263)
cParchment = vec3(0.851, 0.706, 0.561)

col = mix(cTwilight, cTeal, astrolabeLines * 0.70)
col = mix(col, cTerracotta, meridianLines * 0.65)
col = mix(col, cTerracotta, horizonMask * 0.55)
col = mix(col, cParchment, crescent * 0.95)
col = mix(col, cParchment, hatch)

out = clamp(col * contrast, 0.0, 1.0)
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
  float time = t * 0.28;

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  float r = length(p);
  float theta = atan(p.y, p.x);

  float orbitWarp = sin(theta * (8.0 + m * 2.0) + time * 1.5) * cos(r * (12.0 + m * 2.0) - time * 1.2) * (0.40 + b * 0.25) * 0.24;
  float lensR = r + orbitWarp;

  float rings = sin(lensR * (16.0 + m * 6.0) * 2.2);
  float astrolabeLines = smoothstep(0.24, -0.24, abs(rings) - (0.26 + b * 0.06));

  float meridian = sin(p.x * (14.0 + m * 4.0) + sin(p.y * 6.0) * 1.2);
  float meridianLines = smoothstep(0.20, -0.20, abs(meridian) - 0.25) * step(r, 0.95);

  vec2 lunarPos = p - vec2(0.25 * cos(time * 0.8), 0.20 * sin(time * 0.8));
  float lunarR = length(lunarPos);
  float lunarScale = 0.42 + b * 0.10;
  float lunarDisc = smoothstep(lunarScale, lunarScale - 0.04, lunarR);

  float lunarCrescentDist = length(lunarPos - vec2(0.14, 0.08));
  float lunarShadow = smoothstep(0.38, 0.34, lunarCrescentDist);
  float crescent = clamp(lunarDisc - lunarShadow, 0.0, 1.0);

  float horizon = p.y + sin(p.x * 6.0 + time * 0.5) * 0.08 + 0.35;
  float horizonMask = step(horizon, 0.0);

  float cross = sin(p.x * 28.0) * cos(p.y * 28.0);
  float hatch = step(0.62 - h * 0.15, cross) * (1.0 - lunarDisc) * 0.32;

  vec3 cTwilight = vec3(0.157, 0.188, 0.267);
  vec3 cTeal = vec3(0.235, 0.431, 0.443);
  vec3 cTerracotta = vec3(0.851, 0.420, 0.263);
  vec3 cParchment = vec3(0.851, 0.706, 0.561);

  vec3 col = mix(cTwilight, cTeal, astrolabeLines * 0.70);
  col = mix(col, cTerracotta, meridianLines * 0.65);
  col = mix(col, cTerracotta, horizonMask * 0.55);
  col = mix(col, cParchment, crescent * 0.95);
  col = mix(col, cParchment, hatch);

  gl_FragColor = vec4(clamp(col * 1.15, 0.0, 1.0), 1.0);
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
  <title>{item['title']} - Infinite WebGL Embed</title>
  <style>
    * {{ box-sizing: border-box; margin: 0; padding: 0; }}
    body {{
      background: #0b132b;
      overflow: hidden;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      color: #fff;
      display: flex;
      align-items: center;
      justify-content: center;
      min-height: 100vh;
      min-height: 100dvh;
    }}
    .badge {{
      position: absolute;
      top: 14px;
      right: 14px;
      padding: 6px 12px;
      background: rgba(11, 19, 43, 0.85);
      backdrop-filter: blur(8px);
      border: 1px solid rgba(91, 192, 190, 0.25);
      border-radius: 999px;
      color: #fff;
      font-size: 11px;
      font-weight: 500;
      text-decoration: none;
      display: flex;
      align-items: center;
      gap: 6px;
      box-shadow: 0 4px 12px rgba(0,0,0,0.3);
      transition: transform 0.2s, background 0.2s;
      z-index: 10;
    }}
    .badge:hover {{
      transform: translateY(-2px);
      background: rgba(28, 37, 65, 0.95);
      border-color: rgba(91, 192, 190, 0.6);
    }}
    .badge-icon {{
      width: 14px;
      height: 14px;
      border-radius: 50%;
      background: linear-gradient(135deg, #d96b43, #3c6e71);
      display: inline-block;
    }}
    .controls {{
      position: absolute;
      bottom: 14px;
      left: 14px;
      display: flex;
      gap: 8px;
      z-index: 10;
    }}
    .btn {{
      background: rgba(11, 19, 43, 0.82);
      backdrop-filter: blur(8px);
      border: 1px solid rgba(255, 255, 255, 0.15);
      color: #eaeaea;
      padding: 6px 10px;
      border-radius: 6px;
      font-size: 11px;
      cursor: pointer;
    }}
    .btn:hover {{ background: rgba(255, 255, 255, 0.15); }}
  </style>
</head>
<body>
  <main>
    <h1 class="sr-only">{item['title']}</h1>
    <canvas id="gl" role="img" aria-label="{item['title']}, an audio-reactive visual"></canvas>

  <div class="controls">
    <button class="btn" id="btn-audio">enable mic input</button>
  </div>

  <a href="https://infinitedevices.org" target="_blank" rel="noopener" class="badge" title="infinite: the open-source audiovisual daw">
    <span class="badge-icon"></span>
    <span>made in <strong>infinite</strong></span>
  </a>
  </main>

  <script>
    const canvas = document.getElementById("gl");
    const gl = canvas.getContext("webgl");
    if (!gl) {{ document.body.innerHTML = "webgl not supported"; }}

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
    print("=== Creative Expansion Pack 33: Build and Gate Runner ===")
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
  c bg 0.08 0.10 0.14
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
  f rateBeats 0.5
  i steps 16
  i note0 {nr}
  i note1 {nr}
  i note2 {nr+7}
  i note3 {nr+4}
  i note4 {nr}
  i note5 {nr+7}
  i note6 {nr+5}
  i note7 {nr+7}
  i note8 {nr-3}
  i note9 {nr-3}
  i note10 {nr+4}
  i note11 {nr}
  i note12 {nr-5}
  i note13 {nr-1}
  i note14 {nr+2}
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
  f resonance 0.30
  f decay 0.26
  f volume 0.75
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
                "domain": "synth",
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
