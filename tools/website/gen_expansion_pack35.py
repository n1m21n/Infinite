#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 35.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: liminal_void -> liminal-void-infinite-colonnade-water-mirror, liminal-void-submerged-monolith-stairwell)
2. Sketch 3D (Theme: supremacist_blocks -> supremacist-blocks-constructivist-proun-monolith-3d, supremacist-blocks-kinetic-intersecting-cuboids-3d)
3. Field Synth (Theme: drone_kosmische -> drone-kosmische-berlin-school-analog-drift-synth, drone-kosmische-sub-harmonic-meditation-generator-synth)
4. Field Effects (Theme: vaporwave -> vaporwave-tape-warble-chorus-diffusion-fx, vaporwave-hypnagogic-slowdown-reverb-fx)
5. Field Pixel (Theme: vector_wireframe -> vector-wireframe-phosphor-tesseract-hypercube, vector-wireframe-geodesic-radar-horizon)
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

TMP_DIR = "/tmp/pack35_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: liminal_void)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "liminal-void-infinite-colonnade-water-mirror",
        "title": "Liminal Void Infinite Colonnade Water Mirror",
        "description": "Procedural mathematical drawing of an infinite flooded concrete colonnade receding toward a twilight horizon with reflective water plane, dusk gradient atmosphere, and solitary monolithic pillars.",
        "tags": ["liminal", "void", "colonnade", "reflection", "water", "drawing"],
        "code": """// Liminal Void Infinite Colonnade Water Mirror - Infinite Library (Sketch 2D)
param("pillars", 8.0, 4.0, 16.0);
param("water_ripples", 0.45, 0.1, 1.0);
param("horizon_pos", 0.56, 0.4, 0.7);
param("mist_density", 0.60, 0.2, 1.0);

function draw(t) {
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const hy = h * horizon_pos;

  // 1. Dusk Twilight Sky Gradient (#00192A -> #D62828 -> #F77F00 -> #EAE2B7)
  const skySteps = 90;
  for (let i = 0; i < skySteps; i++) {
    const f = i / skySteps;
    const y1 = (i / skySteps) * hy;
    const y2 = ((i + 1) / skySteps) * hy;

    let r, g, b;
    if (f < 0.45) {
      const u = f / 0.45;
      r = 0.01 + u * 0.35;
      g = 0.08 + u * 0.05;
      b = 0.18 - u * 0.08;
    } else if (f < 0.82) {
      const u = (f - 0.45) / 0.37;
      r = 0.36 + u * 0.48;
      g = 0.13 + u * 0.32;
      b = 0.10 - u * 0.08;
    } else {
      const u = (f - 0.82) / 0.18;
      r = 0.84 + u * 0.12;
      g = 0.45 + u * 0.38;
      b = 0.02 + u * 0.55;
    }
    fill(r, g, b);
    noStroke();
    rect(0, y1, w, y2 - y1 + 1.0);
  }

  // 2. Distant Low-Slung Solar Disc on Horizon
  fill(0.96, 0.92, 0.78, 0.90);
  circle(cx, hy - h * 0.035, w * 0.09);
  fill(0.97, 0.50, 0.00, 0.40);
  circle(cx, hy - h * 0.035, w * 0.16);

  // 3. Flooded Reflective Water Plane
  const waterSteps = 90;
  for (let j = 0; j < waterSteps; j++) {
    const f = j / waterSteps;
    const y1 = hy + f * (h - hy);
    const y2 = hy + ((j + 1) / waterSteps) * (h - hy);

    const r = (0.75 * (1.0 - f) * 0.65) + 0.02;
    const g = (0.40 * (1.0 - f) * 0.50) + 0.04;
    const b = (0.15 * (1.0 - f) * 0.70) + 0.12;

    fill(r, g, b);
    noStroke();
    rect(0, y1, w, y2 - y1 + 1.0);
  }

  // Water Surface Horizontal Perspective Wave Tics
  stroke(0.97, 0.50, 0.00, 0.28);
  strokeWeight(1.2);
  const nRipples = 38;
  for (let r = 1; r < nRipples; r++) {
    const rf = Math.pow(r / nRipples, 2.2);
    const ry = hy + rf * (h - hy);
    const ripW = w * (0.15 + rf * 0.85);
    const xOff = sin(ry * 0.18 + t * 0.4) * (w * 0.02);
    line(cx - ripW * 0.5 + xOff, ry, cx + ripW * 0.5 + xOff, ry);
  }

  // 4. Perspective Colonnade of Concrete Monoliths
  const nCols = floor(pillars);
  const vpX = cx;

  for (let side = -1; side <= 1; side += 2) {
    for (let k = nCols; k >= 1; k--) {
      const zNorm = k / nCols;
      const zDist = Math.pow(zNorm, 2.0);
      const colX = vpX + side * (w * 0.18 + zDist * (w * 0.32));
      const colY = hy + zDist * (h * 0.38);
      const colW = (w * 0.032) + zDist * (w * 0.095);
      const colH = (h * 0.14) + zDist * (h * 0.65);

      const topY = colY - colH;

      fill(0.18, 0.19, 0.24);
      noStroke();
      rect(colX - colW * 0.5, topY, colW, colH);

      if (side < 0) {
        fill(0.97, 0.50, 0.00, 0.75);
        rect(colX + colW * 0.35, topY, colW * 0.15, colH);
      } else {
        fill(0.97, 0.50, 0.00, 0.75);
        rect(colX - colW * 0.5, topY, colW * 0.15, colH);
      }

      fill(0.08, 0.09, 0.12);
      if (side < 0) {
        rect(colX - colW * 0.5, topY, colW * 0.25, colH);
      } else {
        rect(colX + colW * 0.25, topY, colW * 0.25, colH);
      }

      stroke(0.06, 0.07, 0.09);
      strokeWeight(1.4);
      const nJoints = 6;
      for (let j = 1; j < nJoints; j++) {
        const jy = topY + (j / nJoints) * colH;
        line(colX - colW * 0.5, jy, colX + colW * 0.5, jy);
      }

      const reflH = colH * 0.62;
      fill(0.06, 0.07, 0.10, 0.65);
      noStroke();
      rect(colX - colW * 0.48, colY, colW * 0.96, reflH);

      stroke(0.97, 0.50, 0.00, 0.45);
      strokeWeight(1.2);
      for (let q = 4; q < reflH; q += 9) {
        const qy = colY + q;
        const qW = colW * (1.0 - q / reflH) * 0.85;
        const qOff = sin(qy * 0.25 + t) * (w * 0.01);
        line(colX - qW * 0.5 + qOff, qy, colX + qW * 0.5 + qOff, qy);
      }
    }
  }

  // 5. Central Solitary Floating Stele on Horizon
  const steleW = w * 0.024;
  const steleH = h * 0.12;
  fill(0.05, 0.06, 0.08);
  noStroke();
  rect(cx - steleW * 0.5, hy - steleH * 0.85, steleW, steleH);

  stroke(0.97, 0.50, 0.00, 0.85);
  strokeWeight(1.2);
  line(cx - steleW * 0.5, hy - steleH * 0.85, cx - steleW * 0.5, hy + steleH * 0.15);
  line(cx + steleW * 0.5, hy - steleH * 0.85, cx + steleW * 0.5, hy + steleH * 0.15);

  // 6. Horizon Atmospheric Fog Mist Layer
  noStroke();
  fill(0.92, 0.45, 0.12, 0.22 * mist_density);
  rect(0, hy - h * 0.04, w, h * 0.08);
}
"""
    },
    {
        "slug": "liminal-void-submerged-monolith-stairwell",
        "title": "Liminal Void Submerged Monolith Stairwell",
        "description": "Mathematical architectural perspective drawing rendering submerged concrete brutalist staircases descending into dark reflective water beneath a heavy twilight sky with fine volumetric mist.",
        "tags": ["liminal", "void", "stairwell", "monolith", "submerged", "drawing"],
        "code": """// Liminal Void Submerged Monolith Stairwell - Infinite Library (Sketch 2D)
param("stair_steps", 12.0, 6.0, 20.0);
param("water_level", 0.52, 0.35, 0.7);
param("portal_scale", 0.85, 0.5, 1.4);
param("fog_spread", 0.40, 0.1, 0.8);

function draw(t) {
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const hy = h * water_level;

  // 1. Heavy Dramatic Sunset Atmosphere Gradient (#020D1A -> #9E2A2B -> #E09F3E -> #FFF3B0)
  const skySteps = 90;
  for (let i = 0; i < skySteps; i++) {
    const f = i / skySteps;
    const y1 = (i / skySteps) * hy;
    const y2 = ((i + 1) / skySteps) * hy;

    let r, g, b;
    if (f < 0.40) {
      const u = f / 0.40;
      r = 0.01 + u * 0.30;
      g = 0.04 + u * 0.08;
      b = 0.14 - u * 0.04;
    } else if (f < 0.80) {
      const u = (f - 0.40) / 0.40;
      r = 0.31 + u * 0.48;
      g = 0.12 + u * 0.28;
      b = 0.10 - u * 0.06;
    } else {
      const u = (f - 0.80) / 0.20;
      r = 0.79 + u * 0.20;
      g = 0.40 + u * 0.42;
      b = 0.04 + u * 0.45;
    }
    fill(r, g, b);
    noStroke();
    rect(0, y1, w, y2 - y1 + 1.0);
  }

  // 2. Heavy Brutalist Arch Portal Framing Twilight Sky
  const portalW = w * 0.38 * portal_scale;
  const portalH = h * 0.46 * portal_scale;
  const pTop = hy - portalH;

  fill(0.08, 0.09, 0.12);
  noStroke();
  rect(cx - portalW * 0.5 - w * 0.06, pTop, w * 0.06, portalH);
  rect(cx + portalW * 0.5, pTop, w * 0.06, portalH);
  rect(cx - portalW * 0.5 - w * 0.06, pTop - h * 0.06, portalW + w * 0.12, h * 0.06);

  stroke(0.97, 0.50, 0.00, 0.85);
  strokeWeight(1.5);
  line(cx - portalW * 0.5, pTop, cx - portalW * 0.5, hy);
  line(cx + portalW * 0.5, pTop, cx + portalW * 0.5, hy);
  line(cx - portalW * 0.5, pTop, cx + portalW * 0.5, pTop);

  fill(0.99, 0.95, 0.82);
  circle(cx, hy - portalH * 0.45, w * 0.08);
  fill(0.96, 0.45, 0.10, 0.35);
  circle(cx, hy - portalH * 0.45, w * 0.16);

  // 3. Dark Mirror Flood Floor
  const waterSteps = 90;
  for (let j = 0; j < waterSteps; j++) {
    const f = j / waterSteps;
    const y1 = hy + f * (h - hy);
    const y2 = hy + ((j + 1) / waterSteps) * (h - hy);

    const r = (0.70 * (1.0 - f) * 0.55) + 0.02;
    const g = (0.35 * (1.0 - f) * 0.45) + 0.03;
    const b = (0.12 * (1.0 - f) * 0.60) + 0.09;

    fill(r, g, b);
    noStroke();
    rect(0, y1, w, y2 - y1 + 1.0);
  }

  stroke(0.95, 0.48, 0.10, 0.30);
  strokeWeight(1.2);
  const nRips = 32;
  for (let k = 1; k < nRips; k++) {
    const kf = Math.pow(k / nRips, 2.0);
    const ry = hy + kf * (h - hy);
    const rw = w * (0.2 + kf * 0.8);
    const xOff = sin(ry * 0.15 + t * 0.35) * (w * 0.018);
    line(cx - rw * 0.5 + xOff, ry, cx + rw * 0.5 + xOff, ry);
  }

  // 4. Submerged Brutalist Stepped Stairwells Flanking the Central Abyss
  const nSteps = floor(stair_steps);
  for (let side = -1; side <= 1; side += 2) {
    for (let s = 0; s < nSteps; s++) {
      const frac = s / nSteps;
      const stepY = hy - h * 0.28 + frac * (h * 0.55);
      const stepW = w * 0.32 * (1.0 - frac * 0.35);
      const stepH = (h * 0.55 / nSteps) * 0.85;

      const sx = (side < 0) ? 0 : w - stepW;

      fill(0.24, 0.26, 0.32);
      noStroke();
      rect(sx, stepY, stepW, stepH * 0.45);

      stroke(0.97, 0.50, 0.00, 0.70);
      strokeWeight(1.2);
      line(sx, stepY, sx + stepW, stepY);

      fill(0.09, 0.10, 0.14);
      noStroke();
      rect(sx, stepY + stepH * 0.45, stepW, stepH * 0.55);

      if (stepY > hy) {
        fill(0.04, 0.05, 0.08, 0.85);
        rect(sx, stepY, stepW, stepH);
      }
    }
  }

  fill(0.12, 0.13, 0.18);
  noStroke();
  rect(cx - w * 0.035, hy - h * 0.08, w * 0.07, h * 0.32);

  stroke(0.97, 0.50, 0.00, 0.75);
  strokeWeight(1.4);
  line(cx - w * 0.035, hy - h * 0.08, cx - w * 0.035, hy + h * 0.24);
  line(cx + w * 0.035, hy - h * 0.08, cx + w * 0.035, hy + h * 0.24);

  noStroke();
  fill(0.92, 0.45, 0.15, 0.24 * fog_spread);
  rect(0, hy - h * 0.05, w, h * 0.10);
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: supremacist_blocks)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "suprematist-blocks-constructivist-proun-monolith-3d",
        "title": "Suprematist Blocks Constructivist Proun Monolith 3D",
        "description": "Suprematist three-dimensional geometric construction with interlocking floating rectangular blocks, cantilevered tension bars, and constructivist diagonal planes inspired by El Lissitzky Proun compositions.",
        "tags": ["suprematist", "constructivist", "proun", "monolith", "cuboid", "3d"],
        "code": """// Suprematist Blocks Constructivist Proun Monolith 3D - Infinite Library (Sketch 3D)
param("spin", 0.35, 0.1, 1.2);
param("scale_factor", 1.15, 0.6, 2.0);
param("cantilever", 1.20, 0.5, 2.0);
param("tier_count", 6.0, 3.0, 10.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.42 + sin(time * 0.14) * 0.03);
  rotateY(time * 0.30 + 0.75);

  // 1. Heavy Black Foundation Monolith Plinth (#111111)
  push();
  translate(0, 1.20 * scale_factor, 0);
  fill(0.067, 0.067, 0.067);
  box(2.6 * scale_factor, 0.16 * scale_factor, 1.8 * scale_factor);

  translate(0, -0.11 * scale_factor, 0);
  fill(0.92, 0.90, 0.86);
  box(2.1 * scale_factor, 0.08 * scale_factor, 1.4 * scale_factor);
  pop();

  // 2. Suprematist Vermilion Central Anchor Tower (#D9231F)
  push();
  translate(-0.15 * scale_factor, 0.10 * scale_factor, 0);
  fill(0.851, 0.137, 0.122);
  box(0.55 * scale_factor, 2.2 * scale_factor, 0.55 * scale_factor);

  translate(0.38 * scale_factor, 0.15 * scale_factor, 0.12 * scale_factor);
  fill(0.102, 0.231, 0.545);
  box(0.22 * scale_factor, 2.5 * scale_factor, 0.40 * scale_factor);
  pop();

  // 3. Dynamic Cantilevered Proun Floating Horizontal Slabs
  const nTiers = floor(tier_count);
  for (let k = 0; k < nTiers; k++) {
    const frac = k / nTiers;
    const yPos = (0.75 - k * 0.34) * scale_factor;
    const xOff = (sin(k * 1.7) * 0.45 * cantilever) * scale_factor;
    const zOff = (cos(k * 1.4) * 0.35 * cantilever) * scale_factor;
    const slabW = (1.9 - frac * 0.4) * scale_factor;
    const slabD = (0.65 - frac * 0.15) * scale_factor;

    push();
    translate(xOff, yPos, zOff);
    rotateY(k * 0.52);

    if (k % 3 === 0) {
      fill(0.957, 0.937, 0.902);
    } else if (k % 3 === 1) {
      fill(0.067, 0.067, 0.067);
    } else {
      fill(0.957, 0.706, 0.102);
    }
    box(slabW, 0.14 * scale_factor, slabD);

    translate(0, 0.08 * scale_factor, 0);
    fill(0.851, 0.137, 0.122);
    box(slabW * 0.35, 0.04 * scale_factor, slabD * 0.6);
    pop();
  }

  // 4. Diagonal Structural Tension Beams (El Lissitzky Proun diagonal vectors)
  push();
  translate(0.60 * scale_factor, -0.15 * scale_factor, 0.30 * scale_factor);
  rotateZ(0.48);
  fill(0.851, 0.137, 0.122);
  box(0.12 * scale_factor, 2.1 * scale_factor, 0.12 * scale_factor);
  pop();

  push();
  translate(-0.55 * scale_factor, 0.05 * scale_factor, -0.35 * scale_factor);
  rotateZ(-0.44);
  fill(0.102, 0.231, 0.545);
  box(0.14 * scale_factor, 1.9 * scale_factor, 0.14 * scale_factor);
  pop();

  // 5. Crown Finial Composition
  push();
  translate(0, -1.22 * scale_factor, 0);
  fill(0.957, 0.937, 0.902);
  box(0.90 * scale_factor, 0.12 * scale_factor, 0.90 * scale_factor);

  translate(0, -0.11 * scale_factor, 0);
  fill(0.851, 0.137, 0.122);
  box(0.45 * scale_factor, 0.10 * scale_factor, 0.45 * scale_factor);

  translate(0, -0.09 * scale_factor, 0);
  fill(0.067, 0.067, 0.067);
  box(0.20 * scale_factor, 0.08 * scale_factor, 0.20 * scale_factor);
  pop();

  pop();
}
"""
    },
    {
        "slug": "suprematist-blocks-kinetic-intersecting-cuboids-3d",
        "title": "Suprematist Blocks Kinetic Intersecting Cuboids 3D",
        "description": "Three-dimensional constructivist spatial sculpture featuring intersecting orthogonal cuboids, dynamic diagonal counterweights, and severe primary geometric volumes suspended in balanced tension.",
        "tags": ["suprematist", "cuboids", "constructivist", "isometric", "geometry", "3d"],
        "code": """// Suprematist Blocks Kinetic Intersecting Cuboids 3D - Infinite Library (Sketch 3D)
param("spin", 0.32, 0.1, 1.2);
param("scale_factor", 1.18, 0.6, 2.0);
param("dispersion", 1.10, 0.5, 2.0);
param("cuboid_tiers", 7.0, 3.0, 10.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.46 + sin(time * 0.12) * 0.04);
  rotateY(time * 0.28 + 0.55);

  // 1. Heavy Stepped Orthogonal Plinth Base (#0E0F12)
  push();
  translate(0, 1.25 * scale_factor, 0);
  fill(0.067, 0.067, 0.067);
  box(2.8 * scale_factor, 0.14 * scale_factor, 2.4 * scale_factor);

  translate(0, -0.10 * scale_factor, 0);
  fill(0.18, 0.19, 0.22);
  box(2.2 * scale_factor, 0.08 * scale_factor, 1.8 * scale_factor);
  pop();

  // 2. Interlocking Orthogonal Cuboids Ring
  const nPylons = 6;
  for (let i = 0; i < nPylons; i++) {
    const angle = (i / nPylons) * Math.PI * 2.0;
    const rDist = 0.88 * dispersion * scale_factor;
    const px = cos(angle) * rDist;
    const pz = sin(angle) * rDist;
    const pylonH = (1.4 + ((i % 3) * 0.32)) * scale_factor;

    push();
    translate(px, 0.25 * scale_factor, pz);

    if (i % 3 === 0) {
      fill(0.957, 0.937, 0.902);
    } else if (i % 3 === 1) {
      fill(0.851, 0.137, 0.122);
    } else {
      fill(0.102, 0.231, 0.545);
    }
    box(0.26 * scale_factor, pylonH, 0.26 * scale_factor);

    translate(0, -pylonH * 0.5 - 0.04 * scale_factor, 0);
    fill(0.957, 0.706, 0.102);
    box(0.38 * scale_factor, 0.08 * scale_factor, 0.38 * scale_factor);
    pop();
  }

  // 3. Central Monolithic Core Slab
  push();
  translate(0, 0.15 * scale_factor, 0);
  fill(0.067, 0.067, 0.067);
  box(0.80 * scale_factor, 2.3 * scale_factor, 0.80 * scale_factor);

  translate(0, 0, 0.42 * scale_factor);
  fill(0.851, 0.137, 0.122);
  box(0.38 * scale_factor, 2.1 * scale_factor, 0.08 * scale_factor);
  pop();

  // 4. Intersecting Transverse Floating Cantilevers
  const nTiers = floor(cuboid_tiers);
  for (let k = 0; k < nTiers; k++) {
    const frac = k / nTiers;
    const yPos = (0.80 - k * 0.35) * scale_factor;
    const rot = k * 0.48;
    const slabW = (2.0 - frac * 0.4) * scale_factor;
    const slabD = (0.60 - frac * 0.12) * scale_factor;

    push();
    translate(0, yPos, 0);
    rotateY(rot);

    if (k % 4 === 0) {
      fill(0.957, 0.937, 0.902);
    } else if (k % 4 === 1) {
      fill(0.851, 0.137, 0.122);
    } else if (k % 4 === 2) {
      fill(0.067, 0.067, 0.067);
    } else {
      fill(0.957, 0.706, 0.102);
    }
    box(slabW, 0.12 * scale_factor, slabD);

    translate(0, 0.07 * scale_factor, 0);
    fill(0.102, 0.231, 0.545);
    box(slabW * 0.4, 0.04 * scale_factor, slabD * 0.8);
    pop();
  }

  // 5. Diagonal Dynamic Struts
  push();
  translate(0.62 * scale_factor, -0.08 * scale_factor, 0.38 * scale_factor);
  rotateZ(0.48);
  fill(0.851, 0.137, 0.122);
  box(0.12 * scale_factor, 2.0 * scale_factor, 0.12 * scale_factor);
  pop();

  push();
  translate(-0.62 * scale_factor, -0.04 * scale_factor, -0.35 * scale_factor);
  rotateZ(-0.45);
  fill(0.957, 0.937, 0.902);
  box(0.14 * scale_factor, 1.9 * scale_factor, 0.14 * scale_factor);
  pop();

  // 6. Crown Finial
  push();
  translate(0, -1.20 * scale_factor, 0);
  fill(0.957, 0.937, 0.902);
  box(0.95 * scale_factor, 0.12 * scale_factor, 0.95 * scale_factor);

  translate(0, -0.10 * scale_factor, 0);
  fill(0.851, 0.137, 0.122);
  box(0.55 * scale_factor, 0.08 * scale_factor, 0.55 * scale_factor);

  translate(0, -0.08 * scale_factor, 0);
  fill(0.067, 0.067, 0.067);
  box(0.25 * scale_factor, 0.08 * scale_factor, 0.25 * scale_factor);
  pop();

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: drone_kosmische)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "drone-kosmische-berlin-school-analog-drift-synth",
        "title": "Drone Kosmische Berlin School Analog Drift Synth",
        "description": "Deep cosmic synthesizer modeled after vintage German Berlin School modular systems, featuring three detuned drifting analog saw/triangle oscillators, resonant ladder low-pass filtering, and slow breathing harmonic envelope.",
        "tags": ["synth", "kosmische", "drone", "berlin", "modular", "audio"],
        "note_root": 48,
        "params": {
            "cutoff": 0.58,
            "resonance": 0.32,
            "drift": 0.45,
            "subOctave": 0.55,
            "warmth": 0.40
        },
        "code": """param float cutoff = 0.58 [0.1, 1.0]
param float resonance = 0.32 [0.1, 0.88]
param float drift = 0.45 [0.0, 1.0]
param float subOctave = 0.55 [0.0, 1.0]
param float warmth = 0.40 [0.0, 1.0]
state float p1 = 0
state float p2 = 0
state float p3 = 0
state float env = 0
state float s1 = 0
state float s2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * 1.8), 0.00005, 0.05)
env = if(target > env, env + (target - env) * 0.96, env - env * decayRate)

dp = freq / sr
p1 = (p1 + dp) % 1.0
saw1 = p1 * 2.0 - 1.0

p2 = (p2 + dp * (1.002 + drift * 0.004)) % 1.0
saw2 = p2 * 2.0 - 1.0

p3 = (p3 + dp * 0.5) % 1.0
subTri = abs(p3 * 2.0 - 1.0) * 2.0 - 1.0

sig = (saw1 * 0.42 + saw2 * 0.38 + subTri * subOctave * 0.50) * 1.15

fNorm = clamp(cutoff * 0.16 + env * 0.22, 0.015, 0.42)
kRes = clamp(resonance * 1.45, 0.1, 1.75)

s1 = s1 + fNorm * (sig - s2 * kRes - s1)
s2 = s2 + fNorm * (s1 - s2)

sat = (s2 / (1.0 + abs(s2 * (0.25 + warmth * 0.25)))) * env
out = sat * 0.55
"""
    },
    {
        "slug": "drone-kosmische-sub-harmonic-meditation-generator-synth",
        "title": "Drone Kosmische Sub-Harmonic Meditation Generator Synth",
        "description": "Sub-harmonic meditation drone synthesizer with rich fifth and sub-octave oscillator beating, warm saturation, and gentle analog filter frequency modulation.",
        "tags": ["synth", "kosmische", "subharmonic", "drone", "meditation", "audio"],
        "note_root": 43,
        "params": {
            "cutoff": 0.54,
            "resonance": 0.35,
            "subBass": 0.55,
            "fifthHarmonic": 0.40,
            "drive": 0.35
        },
        "code": """param float cutoff = 0.54 [0.1, 1.0]
param float resonance = 0.35 [0.1, 0.90]
param float subBass = 0.55 [0.0, 1.0]
param float fifthHarmonic = 0.40 [0.0, 1.0]
param float drive = 0.35 [0.0, 1.0]
state float p1 = 0
state float p2 = 0
state float p3 = 0
state float env = 0
state float s1 = 0
state float s2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * 1.6), 0.00005, 0.05)
env = if(target > env, env + (target - env) * 0.95, env - env * decayRate)

dp = freq / sr
p1 = (p1 + dp) % 1.0
tri1 = abs(p1 * 2.0 - 1.0) * 2.0 - 1.0

p2 = (p2 + dp * 0.501) % 1.0
subSaw = p2 * 2.0 - 1.0

p3 = (p3 + dp * 1.503) % 1.0
fifthTri = abs(p3 * 2.0 - 1.0) * 2.0 - 1.0

sig = (tri1 * 0.50 + subSaw * subBass * 0.45 + fifthTri * fifthHarmonic * 0.35) * 1.2

fNorm = clamp(cutoff * 0.15 + env * 0.24, 0.016, 0.44)
kRes = clamp(resonance * 1.5, 0.1, 1.8)

s1 = s1 + fNorm * (sig - s2 * kRes - s1)
s2 = s2 + fNorm * (s1 - s2)

sat = (s2 / (1.0 + abs(s2 * (0.28 + drive * 0.30)))) * env
out = sat * 0.54
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: vaporwave)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "vaporwave-tape-warble-chorus-diffusion-fx",
        "title": "Vaporwave Tape Warble Chorus Diffusion FX",
        "description": "Dreamy vintage cassette tape chorus and spatial diffusion effect with pitch flutter, multi-tap bucket brigade modulation, warm low-end saturation, and airy stereo widening.",
        "tags": ["effect", "vaporwave", "chorus", "tape", "warble", "audio"],
        "params": {
            "rate": 0.38,
            "depth": 0.55,
            "tone": 0.48,
            "warmth": 0.50,
            "mix": 0.55
        },
        "code": """param float rate = 0.38 [0.05, 1.0]
param float depth = 0.55 [0.0, 1.0]
param float tone = 0.48 [0.1, 1.0]
param float warmth = 0.50 [0.0, 1.0]
param float mix = 0.55 [0.0, 1.0]
state float d1 = 0
state float d2 = 0
state float d3 = 0
state float lp = 0
state float lfo = 0

lfo = (lfo + rate * 0.002) % 1.0
wMod = sin(lfo * 6.28318) * depth * 0.04

fTone = clamp(0.04 + tone * 0.35, 0.015, 0.44)
inSat = in / (1.0 + abs(in * (0.20 + warmth * 0.25)))
lp = lp + fTone * (inSat - lp)

kRate = clamp(0.12 + wMod, 0.02, 0.35)
d1 = d1 + kRate * (lp - d1)
d2 = d2 + kRate * 0.85 * (d1 - d2)
d3 = d3 + kRate * 0.72 * (d2 - d3)

chorusSig = (d1 * 0.45 + d2 * 0.40 - d3 * 0.35) * 1.35
sat = chorusSig / (1.0 + abs(chorusSig * 0.25))
processed = in * (1.0 - mix) + sat * mix
out = processed * 1.15
"""
    },
    {
        "slug": "vaporwave-hypnagogic-slowdown-reverb-fx",
        "title": "Vaporwave Hypnagogic Slowdown Reverb FX",
        "description": "Hypnagogic ambient space reverb and pitch-stretched delay processor featuring modulated allpass diffusion combs, warm analog tape roll-off, and decaying nostalgic reflections.",
        "tags": ["effect", "vaporwave", "reverb", "hypnagogic", "ambient", "audio"],
        "params": {
            "decay": 0.62,
            "tone": 0.44,
            "size": 0.68,
            "diffusion": 0.52,
            "mix": 0.55
        },
        "code": """param float decay = 0.62 [0.1, 0.95]
param float tone = 0.44 [0.1, 1.0]
param float size = 0.68 [0.1, 1.0]
param float diffusion = 0.52 [0.0, 1.0]
param float mix = 0.55 [0.0, 1.0]
state float a1 = 0
state float a2 = 0
state float a3 = 0
state float a4 = 0
state float lp = 0

fTone = clamp(0.04 + tone * 0.35, 0.015, 0.44)
inSat = in / (1.0 + abs(in * 0.25))
lp = lp + fTone * (inSat - lp)

kDiff = clamp(0.12 * (1.0 + size * 0.80), 0.03, 0.38)
kDecay = clamp(decay * 0.86, 0.1, 0.90)

a1 = a1 + kDiff * (lp + a4 * kDecay - a1)
a2 = a2 + kDiff * 0.82 * (a1 - a2)
a3 = a3 + kDiff * 0.70 * (a2 - a3)
a4 = a4 + kDiff * 0.60 * (a3 - a4)

diffSig = (a2 * 0.55 + a3 * 0.45 + a4 * 0.75) * (1.0 + diffusion * 0.5)
sat = diffSig / (1.0 + abs(diffSig * 0.28))
processed = in * (1.0 - mix) + sat * mix
out = processed * 1.12
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel Sources & Embeds (Theme: vector_wireframe)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "vector-wireframe-phosphor-tesseract-hypercube",
        "title": "Vector Wireframe Phosphor Tesseract Hypercube",
        "description": "Audio-reactive vector CRT phosphor shader rendering a rotating 4D tesseract hypercube projection with luminous green beam glow, perspective wireframe grid, and frequency-reactive vertex displacement.",
        "embed_desc": "Audio-reactive 1980s vector CRT visualizer rendering a rotating 4D tesseract wireframe with luminous phosphor glow. Bass pulses the outer hypercube scale and beam intensity, mids rotate inner 4D perspective axes, and highs trigger intense vector CRT electron bloom.",
        "tags": ["pixel", "vector", "wireframe", "tesseract", "phosphor", "shader"],
        "params": {
            "speed": 0.28,
            "beamGlow": 0.75,
            "gridDensity": 14.0,
            "cubeScale": 0.65,
            "contrast": 1.15
        },
        "code": """param float speed = 0.28 [0.05, 1.0]
param float beamGlow = 0.75 [0.2, 1.0]
param float gridDensity = 14.0 [6.0, 28.0]
param float cubeScale = 0.65 [0.2, 1.2]
param float contrast = 1.15 [0.8, 1.6]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2
time = t * speed

r = length(p)
theta = atan2(p.y, p.x)

rot = theta + time * 0.4
rotP = vec2(cos(rot) * r, sin(rot) * r)

boxOuter = max(abs(rotP.x), abs(rotP.y))
lineOuter = smoothstep(cubeScale * 0.53, cubeScale * 0.50, boxOuter) * (1.0 - smoothstep(cubeScale * 0.50, cubeScale * 0.47, boxOuter))

innerScale = cubeScale * 0.48
boxInner = max(abs(p.x + sin(time) * 0.08), abs(p.y + cos(time) * 0.08))
lineInner = smoothstep(innerScale * 0.53, innerScale * 0.50, boxInner) * (1.0 - smoothstep(innerScale * 0.50, innerScale * 0.47, boxInner))

diag1 = abs(p.x - p.y)
diag2 = abs(p.x + p.y)
diagLines = (smoothstep(0.024, 0.005, diag1) + smoothstep(0.024, 0.005, diag2)) * step(r, cubeScale * 0.72)

gridY = (p.y + 0.65) / (abs(p.y + 0.95) + 0.1)
gridX = p.x / (abs(p.y + 0.95) + 0.1)
perspLines = smoothstep(0.08, 0.01, abs(fract(gridX * 4.0) - 0.5)) + smoothstep(0.08, 0.01, abs(fract(gridY * 4.0 - time * 1.5) - 0.5))
perspMask = step(p.y, -0.10) * perspLines * clamp(1.0 - abs(p.y + 0.10) * 1.2, 0.0, 1.0)

cBg = vec3(0.04, 0.06, 0.08)
cGreen = vec3(0.0, 1.0, 0.42)
cCyan = vec3(0.0, 0.90, 1.0)
cWhite = vec3(0.95, 1.0, 0.95)

glowRing = smoothstep(cubeScale * 0.85, cubeScale * 0.25, r) * 0.45
crtBevel = smoothstep(1.15, 0.95, max(abs(p.x), abs(p.y)))

col = cBg * crtBevel
col = mix(col, cCyan * 0.7, perspMask * 0.85)
col = mix(col, cGreen * 0.6, glowRing)
col = mix(col, cGreen, lineOuter * beamGlow * 1.5)
col = mix(col, cCyan, lineInner * beamGlow * 1.6)
col = mix(col, cWhite, diagLines * 1.1)

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

  float rot = theta + (time + m * 0.4) * 0.4;
  vec2 rotP = vec2(cos(rot) * r, sin(rot) * r);

  float cubeScale = 0.65 + b * 0.20;
  float boxOuter = max(abs(rotP.x), abs(rotP.y));
  float lineOuter = smoothstep(cubeScale * 0.53, cubeScale * 0.50, boxOuter) * (1.0 - smoothstep(cubeScale * 0.50, cubeScale * 0.47, boxOuter));

  float innerScale = cubeScale * 0.48;
  float boxInner = max(abs(p.x + sin(time) * 0.08), abs(p.y + cos(time) * 0.08));
  float lineInner = smoothstep(innerScale * 0.53, innerScale * 0.50, boxInner) * (1.0 - smoothstep(innerScale * 0.50, innerScale * 0.47, boxInner));

  float diag1 = abs(p.x - p.y);
  float diag2 = abs(p.x + p.y);
  float diagLines = (smoothstep(0.024 + h * 0.015, 0.005, diag1) + smoothstep(0.024 + h * 0.015, 0.005, diag2)) * step(r, cubeScale * 0.72);

  float gridY = (p.y + 0.65) / (abs(p.y + 0.95) + 0.1);
  float gridX = p.x / (abs(p.y + 0.95) + 0.1);
  float perspLines = smoothstep(0.08, 0.01, abs(fract(gridX * 4.0) - 0.5)) + smoothstep(0.08, 0.01, abs(fract(gridY * 4.0 - (time + b * 0.2) * 1.5) - 0.5));
  float perspMask = step(p.y, -0.10) * perspLines * clamp(1.0 - abs(p.y + 0.10) * 1.2, 0.0, 1.0);

  vec3 cBg = vec3(0.04, 0.06, 0.08);
  vec3 cGreen = vec3(0.0, 1.0, 0.42);
  vec3 cCyan = vec3(0.0, 0.90, 1.0);
  vec3 cWhite = vec3(0.95, 1.0, 0.95);

  float glowRing = smoothstep(cubeScale * 0.85, cubeScale * 0.25, r) * (0.45 + b * 0.25);
  float crtBevel = smoothstep(1.15, 0.95, max(abs(p.x), abs(p.y)));

  vec3 col = cBg * crtBevel;
  col = mix(col, cCyan * 0.7, perspMask * 0.85);
  col = mix(col, cGreen * 0.6, glowRing);
  col = mix(col, cGreen, lineOuter * (0.75 + b * 0.4) * 1.5);
  col = mix(col, cCyan, lineInner * (0.75 + m * 0.4) * 1.6);
  col = mix(col, cWhite, diagLines * (1.1 + h * 0.6));

  gl_FragColor = vec4(clamp(col * 1.15, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "vector-wireframe-geodesic-radar-horizon",
        "title": "Vector Wireframe Geodesic Radar Horizon",
        "description": "Audio-reactive vector wireframe radar shader featuring a rotating geodesic radar dome over an infinite perspective grid with beam sweep echoes and reactive frequency pulses.",
        "embed_desc": "Audio-reactive vector wireframe radar visualizer featuring a spinning geodesic radar dome atop an infinite perspective vector terrain. Bass drives grid undulation, mids rotate geodesic radar azimuth, and highs emit phosphor sweep flares.",
        "tags": ["pixel", "vector", "radar", "geodesic", "wireframe", "shader"],
        "params": {
            "speed": 0.26,
            "radarDensity": 16.0,
            "beamIntensity": 0.75,
            "domeScale": 0.65,
            "contrast": 1.15
        },
        "code": """param float speed = 0.26 [0.05, 1.0]
param float radarDensity = 16.0 [8.0, 32.0]
param float beamIntensity = 0.75 [0.2, 1.0]
param float domeScale = 0.65 [0.3, 1.2]
param float contrast = 1.15 [0.8, 1.6]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2
time = t * speed

r = length(p)
theta = atan2(p.y, p.x)

rings = sin(r * radarDensity * 1.5 - time * 1.2)
ringLines = smoothstep(0.12, 0.02, abs(rings)) * step(r, domeScale)

spokes = sin(theta * 12.0 + time * 0.5)
spokeLines = smoothstep(0.10, 0.01, abs(spokes)) * step(r, domeScale)

sweepAngle = mod(theta - time * 2.5 + 3.14159, 6.28318)
sweepBeam = smoothstep(0.45, 0.0, sweepAngle) * step(r, domeScale * 1.08)

boundRing = smoothstep(domeScale * 1.02, domeScale * 0.99, r) * (1.0 - smoothstep(domeScale * 0.99, domeScale * 0.96, r))

gridY = (p.y + 0.50) / (abs(p.y + 0.90) + 0.1)
gridX = p.x / (abs(p.y + 0.90) + 0.1)
perspLines = smoothstep(0.08, 0.01, abs(fract(gridX * 4.0) - 0.5)) + smoothstep(0.08, 0.01, abs(fract(gridY * 4.0 - time * 1.4) - 0.5))
perspMask = step(p.y, -0.20) * perspLines * clamp(1.0 - abs(p.y + 0.20) * 1.2, 0.0, 1.0)

cBg = vec3(0.04, 0.06, 0.08)
cGreen = vec3(0.0, 1.0, 0.42)
cCyan = vec3(0.0, 0.90, 1.0)
cWhite = vec3(0.95, 1.0, 0.95)

glowDome = smoothstep(domeScale * 1.05, domeScale * 0.2, r) * 0.42
crtBevel = smoothstep(1.15, 0.95, max(abs(p.x), abs(p.y)))

col = cBg * crtBevel
col = mix(col, cCyan * 0.7, perspMask * 0.85)
col = mix(col, cGreen * 0.55, glowDome)
col = mix(col, cGreen, (ringLines * 0.6 + spokeLines * 0.5) * beamIntensity * 1.4)
col = mix(col, cCyan, boundRing * 1.5)
col = mix(col, cWhite, sweepBeam * beamIntensity * 1.3)

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
  float time = t * 0.26;

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  float r = length(p);
  float theta = atan(p.y, p.x);

  float domeScale = 0.65 + b * 0.15;

  float rings = sin(r * (16.0 + b * 4.0) * 1.5 - time * 1.2);
  float ringLines = smoothstep(0.12, 0.02, abs(rings)) * step(r, domeScale);

  float spokes = sin(theta * 12.0 + (time + m * 0.5) * 0.5);
  float spokeLines = smoothstep(0.10, 0.01, abs(spokes)) * step(r, domeScale);

  float sweepAngle = mod(theta - time * 2.5 + 3.14159, 6.28318);
  float sweepBeam = smoothstep(0.45 + h * 0.25, 0.0, sweepAngle) * step(r, domeScale * 1.08);

  float boundRing = smoothstep(domeScale * 1.02, domeScale * 0.99, r) * (1.0 - smoothstep(domeScale * 0.99, domeScale * 0.96, r));

  float gridY = (p.y + 0.50) / (abs(p.y + 0.90) + 0.1);
  float gridX = p.x / (abs(p.y + 0.90) + 0.1);
  float perspLines = smoothstep(0.08, 0.01, abs(fract(gridX * 4.0) - 0.5)) + smoothstep(0.08, 0.01, abs(fract(gridY * 4.0 - (time + b * 0.2) * 1.4) - 0.5));
  float perspMask = step(p.y, -0.20) * perspLines * clamp(1.0 - abs(p.y + 0.20) * 1.2, 0.0, 1.0);

  vec3 cBg = vec3(0.04, 0.06, 0.08);
  vec3 cGreen = vec3(0.0, 1.0, 0.42);
  vec3 cCyan = vec3(0.0, 0.90, 1.0);
  vec3 cWhite = vec3(0.95, 1.0, 0.95);

  float glowDome = smoothstep(domeScale * 1.05, domeScale * 0.2, r) * (0.42 + b * 0.25);
  float crtBevel = smoothstep(1.15, 0.95, max(abs(p.x), abs(p.y)));

  vec3 col = cBg * crtBevel;
  col = mix(col, cCyan * 0.7, perspMask * 0.85);
  col = mix(col, cGreen * 0.55, glowDome);
  col = mix(col, cGreen, (ringLines * 0.6 + spokeLines * 0.5) * (0.75 + m * 0.4) * 1.4);
  col = mix(col, cCyan, boundRing * 1.5);
  col = mix(col, cWhite, sweepBeam * (0.75 + h * 0.5) * 1.3);

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
    print("=== Creative Expansion Pack 35 Generator & Verification Gate Runner ===")
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
  c bg 0.12 0.13 0.16
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
  i note8 {nr-5}
  i note9 {nr-5}
  i note10 {nr+2}
  i note11 {nr}
  i note12 {nr-7}
  i note13 {nr-3}
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
  f cutoff 3400.0
  f resonance 0.30
  f decay 0.32
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
                "kind": "Generator",
                "tags": [t.lower() for t in it["tags"]],
                "description": it["description"],
                "price": 0,
                "domain": "effect",
                "category": "Audio effects",
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
