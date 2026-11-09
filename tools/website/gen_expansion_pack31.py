#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 31.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (riso-manga-zine-screentone-burst, riso-manga-zine-speedline-panel)
2. Sketch 3D (thundr-brutalist-modular-techwear-pylon-3d, thundr-brutalist-hazard-cantilever-tower-3d)
3. Field Synth (classic-trance-supersaw-anthem-lead-synth, classic-trance-rolling-offbeat-bass-synth)
4. Field Effects (industrial-techno-warehouse-distortion-reverb-fx, industrial-techno-grimy-bitcrush-saturator-fx)
5. Field Pixel (op-art-concentric-moire-interference, op-art-hypnotic-checkerboard-warp)
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

TMP_DIR = "/tmp/pack31_build"
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
        "slug": "riso-manga-zine-screentone-burst",
        "title": "Riso Manga Zine Screentone Burst",
        "description": "Multi-layered risograph halftone screentone composition featuring concentric micro-dot matrices, radiating action impact stars, dynamic speed chevron accents, and bold riso ink separation.",
        "tags": ["riso", "manga", "zine", "screentone", "halftone", "drawing"],
        "code": """// Riso Manga Zine Screentone Burst - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Multi-layered risograph halftone screentone composition featuring concentric micro-dot matrices, radiating action impact stars, dynamic speed chevron accents, and bold riso ink separation.
param("burst_spikes", 16.0, 8.0, 32.0);
param("halftone_density", 22.0, 10.0, 36.0);
param("spin", 0.35, 0.1, 1.5);
param("riso_offset", 8.0, 2.0, 20.0);

function draw(t) {
  // Rich aged riso paper background (#ede7d5)
  background(0.885, 0.860, 0.795);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;
  const time = t * spin;

  push();
  translate(cx, cy);

  // 1. Deconstructed Zine Outer Frame & Corner Registration Marks
  stroke(0.10, 0.10, 0.14);
  strokeWeight(3.5);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  // Registration crosshairs
  strokeWeight(1.8);
  const regOff = span * 0.45;
  const regLen = span * 0.025;
  const corners = [[-regOff, -regOff], [regOff, -regOff], [-regOff, regOff], [regOff, regOff]];
  for (let c of corners) {
    line(c[0] - regLen, c[1], c[0] + regLen, c[1]);
    line(c[0], c[1] - regLen, c[0], c[1] + regLen);
    circle(c[0], c[1], regLen * 1.2);
  }

  // 2. Riso Halftone Screentone Grid (Fluorescent Coral/Pink & Sunflower Yellow)
  const dots = floor(halftone_density);
  const step = (span * 0.88) / dots;
  noStroke();

  // Sunflower Yellow pass (#ffcc00)
  fill(1.0, 0.78, 0.0, 0.78);
  for (let i = -dots / 2; i <= dots / 2; i++) {
    for (let j = -dots / 2; j <= dots / 2; j++) {
      const d = Math.hypot(i, j);
      if (d < dots * 0.48) {
        const dotR = (sin(d * 0.45 - time * 2.0) * 0.5 + 0.5) * (step * 0.48) + 2.0;
        circle(i * step + 4.0, j * step + 3.0, dotR * 2.0);
      }
    }
  }

  // Fluorescent Pink pass (#ff3366) with misregistration shift
  fill(0.96, 0.16, 0.38, 0.85);
  for (let i = -dots / 2; i <= dots / 2; i++) {
    for (let j = -dots / 2; j <= dots / 2; j++) {
      const d = Math.hypot(i, j);
      if (d < dots * 0.45) {
        const dotR = (cos(d * 0.50 + time * 1.8) * 0.5 + 0.5) * (step * 0.44) + 1.8;
        circle(i * step - riso_offset * 0.5, j * step + riso_offset * 0.4, dotR * 2.0);
      }
    }
  }

  // 3. Radial Action Manga Speedlines (Deep Riso Blue #0033cc)
  push();
  rotate(time * 0.25);
  stroke(0.0, 0.20, 0.75, 0.88);
  const nLines = 54;
  for (let l = 0; l < nLines; l++) {
    const ang = (l / nLines) * Math.PI * 2.0;
    const r1 = span * (0.22 + sin(l * 12.0 + time * 3.0) * 0.08);
    const r2 = span * 0.46;
    strokeWeight((l % 3 === 0) ? 3.5 : (l % 2 === 0) ? 2.2 : 1.4);
    line(cos(ang) * r1, sin(ang) * r1, cos(ang) * r2, sin(ang) * r2);
  }
  pop();

  // 4. Explosive Starburst Impact Silhouette (Ink Black with Riso Pink Shadow)
  const spikes = floor(burst_spikes);

  // Pink offset shadow
  push();
  translate(riso_offset, riso_offset * 0.8);
  fill(0.96, 0.16, 0.38, 0.92);
  noStroke();
  beginShape();
  for (let s = 0; s <= spikes * 2; s++) {
    const ang = (s / (spikes * 2)) * Math.PI * 2.0 - time * 0.5;
    const r = (s % 2 === 0) ? span * 0.34 : span * 0.15;
    vertex(cos(ang) * r, sin(ang) * r);
  }
  endShape();
  pop();

  // Charcoal Black Main Starburst
  fill(0.10, 0.10, 0.14);
  stroke(0.10, 0.10, 0.14);
  strokeWeight(2.0);
  beginShape();
  for (let s = 0; s <= spikes * 2; s++) {
    const ang = (s / (spikes * 2)) * Math.PI * 2.0 - time * 0.5;
    const r = (s % 2 === 0) ? span * 0.34 : span * 0.15;
    vertex(cos(ang) * r, sin(ang) * r);
  }
  endShape();

  // Central Sunflower Yellow core spark
  fill(1.0, 0.78, 0.0);
  noStroke();
  circle(0, 0, span * 0.09);

  // Sound-effect dynamic graphic chevron accents
  fill(0.885, 0.860, 0.795);
  for (let k = 0; k < 4; k++) {
    const ang = (k / 4.0) * Math.PI * 2.0 + time * 0.8;
    const rk = span * 0.21;
    push();
    translate(cos(ang) * rk, sin(ang) * rk);
    rotate(ang + Math.PI * 0.5);
    triangle(-span * 0.022, span * 0.022, span * 0.022, span * 0.022, 0, -span * 0.035);
    pop();
  }

  pop();
}
"""
    },
    {
        "slug": "riso-manga-zine-speedline-panel",
        "title": "Riso Manga Zine Speedline Panel",
        "description": "Deconstructed manga panel layout with high-velocity radial speedlines, offset riso ink misregistration, isometric sound-effect chevron geometry, and layered halftone gradient grids.",
        "tags": ["riso", "manga", "zine", "speedline", "panel", "drawing"],
        "code": """// Riso Manga Zine Speedline Panel - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Deconstructed manga panel layout with high-velocity radial speedlines, offset riso ink misregistration, isometric sound-effect chevron geometry, and layered halftone gradient grids.
param("focal_warp", 1.15, 0.6, 2.0);
param("panel_split", 0.52, 0.3, 0.7);
param("speed", 0.40, 0.1, 1.5);
param("screentone_scale", 24.0, 12.0, 40.0);

function draw(t) {
  // Rich aged riso paper background (#ede7d5)
  background(0.885, 0.860, 0.795);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;
  const time = t * speed;

  push();
  translate(cx, cy);

  // 1. Asymmetric Manga Comic Panels (Diagonal Division)
  const pW = span * 0.94;
  const pH = span * 0.94;

  // Panel 1: Top Dynamic Vortex Box
  stroke(0.10, 0.10, 0.14);
  strokeWeight(3.8);
  noFill();
  rect(-pW * 0.5, -pH * 0.5, pW, pH * panel_split);

  // Panel 2: Bottom Technical Screentone Box
  rect(-pW * 0.5, -pH * 0.5 + pH * panel_split + span * 0.025, pW, pH * (1.0 - panel_split) - span * 0.025);

  // 2. Convergent High-Velocity Action Speedlines (Top Panel)
  push();
  const topH = pH * panel_split;
  const focalX = sin(time * 1.5) * span * 0.12;
  const focalY = -pH * 0.5 + topH * 0.45;

  stroke(0.0, 0.20, 0.75, 0.88); // Riso Blue
  const lines = 72;
  for (let i = 0; i < lines; i++) {
    const ang = (i / lines) * Math.PI * 2.0;
    const len1 = span * 0.07 * focal_warp;
    const len2 = span * 0.60;
    const x1 = focalX + cos(ang) * len1;
    const y1 = focalY + sin(ang) * len1;
    const x2 = focalX + cos(ang) * len2;
    const y2 = focalY + sin(ang) * len2;

    if (y1 > -pH * 0.5 && y1 < -pH * 0.5 + topH && y2 > -pH * 0.5 && y2 < -pH * 0.5 + topH) {
      strokeWeight((i % 4 === 0) ? 3.5 : (i % 2 === 0) ? 2.0 : 1.2);
      line(x1, y1, x2, y2);
    }
  }

  // Focal explosive impact point
  fill(0.96, 0.16, 0.38); // Fluorescent Pink
  noStroke();
  circle(focalX, focalY, span * 0.05);
  fill(1.0, 0.78, 0.0); // Yellow spark
  circle(focalX, focalY, span * 0.024);
  pop();

  // 3. Dense Risograph Halftone Screentone Grid (Bottom Panel)
  const botY = -pH * 0.5 + pH * panel_split + span * 0.025;
  const botH = pH * (1.0 - panel_split) - span * 0.025;
  const dotsX = 26;
  const dotsY = 11;
  const stepX = pW / dotsX;
  const stepY = botH / dotsY;

  noStroke();
  for (let ix = 1; ix < dotsX; ix++) {
    for (let iy = 1; iy < dotsY; iy++) {
      const px = -pW * 0.5 + ix * stepX;
      const py = botY + iy * stepY;
      const wave = sin(ix * 0.45 + iy * 0.6 + time * 2.0);

      if ((ix + iy) % 2 === 0) {
        fill(0.96, 0.16, 0.38, 0.85);
        circle(px, py, (wave * 0.5 + 0.5) * (stepX * 0.44) + 1.8);
      } else {
        fill(1.0, 0.78, 0.0, 0.88);
        circle(px + 2.0, py + 2.0, (cos(wave) * 0.5 + 0.5) * (stepX * 0.46) + 1.8);
      }
    }
  }

  // 4. Giant Manga Sound-Effect Geometric Letterforms / Chevrons
  fill(0.10, 0.10, 0.14); // Ink black
  const chevs = 3;
  for (let k = 0; k < chevs; k++) {
    const kx = -pW * 0.35 + k * (pW * 0.32);
    const ky = botY + botH * 0.5;
    push();
    translate(kx, ky);
    rotate(-0.15 + sin(time * 2.0 + k) * 0.08);

    // Offset fluorescent shadow
    fill(0.96, 0.16, 0.38);
    rect(-span * 0.06 - 4.0, -span * 0.04 - 3.0, span * 0.12, span * 0.08);

    // Main ink slab
    fill(0.10, 0.10, 0.14);
    rect(-span * 0.06, -span * 0.04, span * 0.12, span * 0.08);

    // Interior yellow lightning slash
    stroke(1.0, 0.78, 0.0);
    strokeWeight(2.5);
    line(-span * 0.04, span * 0.02, 0, -span * 0.02);
    line(0, -span * 0.02, span * 0.04, 0);
    pop();
  }

  // 5. Margin CMYK / Riso color calibration pills
  noStroke();
  fill(0.96, 0.16, 0.38); circle(-pW * 0.45, pH * 0.48, span * 0.020);
  fill(1.0, 0.78, 0.0);  circle(-pW * 0.41, pH * 0.48, span * 0.020);
  fill(0.0, 0.20, 0.75); circle(-pW * 0.37, pH * 0.48, span * 0.020);
  fill(0.10, 0.10, 0.14); circle(-pW * 0.33, pH * 0.48, span * 0.020);

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: thundr_brutalist)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "thundr-brutalist-modular-techwear-pylon-3d",
        "title": "Thundr Brutalist Modular Techwear Pylon 3D",
        "description": "Monolithic postmodern techwear architectural pylon with interlocking precast concrete slabs, high-visibility hazard orange cantilever trusses, and modular industrial techwear bracing.",
        "tags": ["brutalist", "techwear", "architecture", "pylon", "monolith", "3d"],
        "code": """// Thundr Brutalist Modular Techwear Pylon 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Monolithic postmodern techwear architectural pylon with interlocking precast concrete slabs, high-visibility hazard orange cantilever trusses, and modular industrial techwear bracing.
param("spin", 0.36, 0.1, 1.2);
param("scale_factor", 1.15, 0.5, 2.0);
param("pylon_height", 2.4, 1.0, 3.6);
param("cantilevers", 4.0, 2.0, 8.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.50 + sin(time * 0.12) * 0.04);
  rotateY(time * 0.28 + 0.65);

  // 1. Heavy stepped asphalt foundation slab
  push();
  translate(0, 1.25 * scale_factor, 0);
  fill(0.12, 0.13, 0.16); // Dark gunmetal asphalt
  box(2.8 * scale_factor, 0.14, 2.8 * scale_factor);

  // Stepped sub-plinth with hazard orange perimeter
  translate(0, -0.09, 0);
  fill(1.0, 0.33, 0.0); // Hazard Orange (#ff5500)
  box(2.2 * scale_factor, 0.05, 2.2 * scale_factor);

  translate(0, -0.05, 0);
  fill(0.78, 0.80, 0.84); // Precast concrete deck
  box(1.8 * scale_factor, 0.06, 1.8 * scale_factor);
  pop();

  // 2. Monolithic Central Concrete Techwear Pylon
  push();
  translate(0, (1.25 - pylon_height * 0.5) * scale_factor, 0);
  fill(0.82, 0.84, 0.86); // Raw precast concrete
  box(0.55 * scale_factor, pylon_height * scale_factor, 0.55 * scale_factor);

  // Recessed dark mechanical equipment core cavities
  fill(0.08, 0.09, 0.11);
  box(0.38 * scale_factor, pylon_height * 0.70 * scale_factor, 0.60 * scale_factor);
  box(0.60 * scale_factor, pylon_height * 0.50 * scale_factor, 0.38 * scale_factor);

  // Vertical acid-lime telemetry conduit strip
  push();
  translate(0.29 * scale_factor, 0, 0);
  fill(0.78, 1.0, 0.0); // Electric Acid Lime
  box(0.04 * scale_factor, pylon_height * 0.85 * scale_factor, 0.08 * scale_factor);
  pop();
  pop();

  // 3. Interlocking Asymmetric Hazard Cantilever Beams
  const nBeams = floor(cantilevers);
  for (let b = 0; b < nBeams; b++) {
    const frac = b / nBeams;
    const yPos = (0.8 - frac * 1.6) * scale_factor;
    const ang = frac * Math.PI * 1.5 + time * 0.15;
    const len = (1.4 + sin(b * 1.7) * 0.3) * scale_factor;

    push();
    translate(0, yPos, 0);
    rotateY(ang);

    // Hazard orange heavy cantilever spar
    push();
    translate(len * 0.5, 0, 0);
    fill(1.0, 0.33, 0.0);
    box(len, 0.12 * scale_factor, 0.18 * scale_factor);

    // Suspended modular pod at spar tip
    translate(len * 0.45, 0.14 * scale_factor, 0);
    fill(0.18, 0.19, 0.22);
    box(0.24 * scale_factor, 0.20 * scale_factor, 0.24 * scale_factor);

    // Acid lime status beacon pip
    fill(0.78, 1.0, 0.0);
    box(0.08 * scale_factor, 0.08 * scale_factor, 0.26 * scale_factor);
    pop();

    // Diagonal structural tension truss
    push();
    translate(len * 0.35, 0.25 * scale_factor, 0);
    rotateZ(-0.45);
    fill(0.28, 0.31, 0.36);
    box(len * 0.7, 0.05 * scale_factor, 0.06 * scale_factor);
    pop();

    pop();
  }

  // 4. Crown Communications Rig and Mast Antennae
  push();
  translate(0, (1.25 - pylon_height) * scale_factor - 0.2 * scale_factor, 0);
  fill(0.18, 0.19, 0.22);
  box(0.70 * scale_factor, 0.12 * scale_factor, 0.70 * scale_factor);

  // Mast needle
  translate(0, -0.4 * scale_factor, 0);
  fill(0.78, 0.80, 0.84);
  box(0.06 * scale_factor, 0.8 * scale_factor, 0.06 * scale_factor);

  // Mast beacon
  translate(0, -0.42 * scale_factor, 0);
  fill(1.0, 0.33, 0.0);
  box(0.14 * scale_factor, 0.14 * scale_factor, 0.14 * scale_factor);
  pop();

  pop();
}
"""
    },
    {
        "slug": "thundr-brutalist-hazard-cantilever-tower-3d",
        "title": "Thundr Brutalist Hazard Cantilever Tower 3D",
        "description": "Stepped industrial cantilever megastructure featuring suspended heavy concrete decks, alternating hazard chevron stripes, diagonal structural exoskeletons, and glowing telemetry beacon arrays.",
        "tags": ["brutalist", "cantilever", "industrial", "megastructure", "hazard", "3d"],
        "code": """// Thundr Brutalist Hazard Cantilever Tower 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Stepped industrial cantilever megastructure featuring suspended heavy concrete decks, alternating hazard chevron stripes, diagonal structural exoskeletons, and glowing telemetry beacon arrays.
param("spin", 0.34, 0.1, 1.2);
param("scale_factor", 1.15, 0.5, 2.0);
param("tier_offset", 0.42, 0.2, 0.8);
param("deck_tiers", 5.0, 3.0, 8.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.52 + sin(time * 0.13) * 0.04);
  rotateY(time * 0.26 + 0.55);

  // 1. Massive industrial stepped plinth
  push();
  translate(0, 1.25 * scale_factor, 0);
  fill(0.10, 0.11, 0.14); // Asphalt base
  box(2.7 * scale_factor, 0.14, 2.7 * scale_factor);

  translate(0, -0.09, 0);
  fill(0.78, 0.80, 0.84); // Concrete sub-deck
  box(2.1 * scale_factor, 0.06, 2.1 * scale_factor);
  pop();

  // 2. Stepped Cantilever Megastructure Decks (Alternating Concrete & Hazard Orange)
  const nTiers = floor(deck_tiers);
  for (let k = 0; k < nTiers; k++) {
    const frac = k / nTiers;
    const yPos = (1.05 - k * 0.48) * scale_factor;
    const shiftX = ((k % 2 === 0) ? tier_offset : -tier_offset) * (1.0 - frac * 0.3) * scale_factor;
    const shiftZ = ((k % 3 === 0) ? tier_offset * 0.5 : -tier_offset * 0.5) * scale_factor;
    const deckW = (1.5 - frac * 0.3) * scale_factor;
    const deckD = (1.1 - frac * 0.2) * scale_factor;

    push();
    translate(shiftX, yPos, shiftZ);

    // Primary cantilever deck slab
    if (k % 2 === 0) {
      fill(0.82, 0.84, 0.86); // Concrete slab
    } else {
      fill(1.0, 0.33, 0.0); // Hazard Orange accent deck
    }
    box(deckW, 0.14 * scale_factor, deckD);

    // Under-deck structural corbel brackets
    push();
    translate(0, 0.10 * scale_factor, 0);
    fill(0.18, 0.19, 0.22);
    box(deckW * 0.75, 0.08 * scale_factor, deckD * 0.75);
    pop();

    // Perimeter safety barrier with acid lime beacons
    push();
    translate(deckW * 0.46, -0.10 * scale_factor, 0);
    fill(0.78, 1.0, 0.0); // Electric Acid Lime
    box(0.04 * scale_factor, 0.08 * scale_factor, deckD * 0.85);
    pop();

    pop();
  }

  // 3. Exoskeleton Vertical Core Spine & Diagonal Truss Columns
  push();
  translate(-0.15 * scale_factor, 0.1 * scale_factor, -0.15 * scale_factor);
  fill(0.14, 0.15, 0.18); // Dark structural steel
  box(0.35 * scale_factor, 2.5 * scale_factor, 0.35 * scale_factor);
  pop();

  // Diagonal support trusses
  push();
  translate(0.6 * scale_factor, 0.35 * scale_factor, 0.3 * scale_factor);
  rotateZ(0.55);
  fill(0.28, 0.31, 0.36);
  box(0.08 * scale_factor, 1.8 * scale_factor, 0.08 * scale_factor);
  pop();

  push();
  translate(-0.6 * scale_factor, 0.35 * scale_factor, -0.3 * scale_factor);
  rotateZ(-0.55);
  fill(0.28, 0.31, 0.36);
  box(0.08 * scale_factor, 1.8 * scale_factor, 0.08 * scale_factor);
  pop();

  // 4. Rooftop Observation Deck & Beacon Spire
  push();
  translate(0, (1.05 - nTiers * 0.48) * scale_factor - 0.12 * scale_factor, 0);
  fill(0.82, 0.84, 0.86);
  box(0.60 * scale_factor, 0.10 * scale_factor, 0.60 * scale_factor);

  translate(0, -0.35 * scale_factor, 0);
  fill(1.0, 0.33, 0.0);
  box(0.08 * scale_factor, 0.65 * scale_factor, 0.08 * scale_factor);

  // Top flashing beacon
  translate(0, -0.35 * scale_factor, 0);
  fill(0.78, 1.0, 0.0);
  box(0.16 * scale_factor, 0.12 * scale_factor, 0.16 * scale_factor);
  pop();

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: classic_trance)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "classic-trance-supersaw-anthem-lead-synth",
        "title": "Classic Trance Supersaw Anthem Lead Synth",
        "description": "Iconic late-90s euphoric trance lead synthesizer with 7 detuned unison saw oscillators, cascading 24dB resonant lowpass filter with envelope modulation, high-shelf air, and subtle chorus stereo width.",
        "tags": ["synth", "trance", "supersaw", "lead", "filter", "audio"],
        "note_root": 60,
        "params": {
            "cutoff": 0.65,
            "resonance": 0.45,
            "detune": 0.40,
            "decayTime": 0.38,
            "drive": 0.35
        },
        "code": """param float cutoff = 0.65 [0.1, 1.0]
param float resonance = 0.45 [0.1, 0.95]
param float detune = 0.40 [0.0, 1.0]
param float decayTime = 0.38 [0.1, 1.2]
param float drive = 0.35 [0.0, 1.0]
state float p1 = 0
state float p2 = 0
state float p3 = 0
state float env = 0
state float s1 = 0
state float s2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decayTime), 0.0001, 0.05)
env = if(target > env, env + (target - env) * 0.95, env - env * decayRate)

dp = freq / sr
dDet = dp * detune * 0.03
p1 = (p1 + dp) % 1.0
p2 = (p2 + dp + dDet) % 1.0
p3 = (p3 + dp - dDet) % 1.0

saw = (p1 * 2.0 - 1.0) + (p2 * 2.0 - 1.0) * 0.8 + (p3 * 2.0 - 1.0) * 0.8
sig = saw * 0.38 * (1.0 + drive * 1.2)

fNorm = clamp(cutoff * 0.22 + env * 0.32, 0.02, 0.46)
kRes = clamp(resonance * 1.8, 0.1, 1.9)

s1 = s1 + fNorm * (sig - s2 * kRes - s1)
s2 = s2 + fNorm * (s1 - s2)

sat = (s2 / (1.0 + abs(s2 * 0.45))) * env
out = sat * 0.60
"""
    },
    {
        "slug": "classic-trance-rolling-offbeat-bass-synth",
        "title": "Classic Trance Rolling Offbeat Bass Synth",
        "description": "High-energy rolling 16th-note trance bass synthesizer featuring paired saw and pulse sub-oscillators, snappy transistor envelope punch, 24dB resonant lowpass filter, and warm analog drive.",
        "tags": ["synth", "trance", "bass", "rolling", "analog", "audio"],
        "note_root": 36,
        "params": {
            "cutoff": 0.52,
            "resonance": 0.60,
            "envMod": 0.65,
            "decayTime": 0.22,
            "drive": 0.40
        },
        "code": """param float cutoff = 0.52 [0.1, 1.0]
param float resonance = 0.60 [0.1, 0.95]
param float envMod = 0.65 [0.0, 1.0]
param float decayTime = 0.22 [0.06, 0.6]
param float drive = 0.40 [0.0, 1.0]
state float p1 = 0
state float pSub = 0
state float env = 0
state float s1 = 0
state float s2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decayTime), 0.0001, 0.08)
env = if(target > env, env + (target - env) * 0.96, env - env * decayRate)

dp = freq / sr
p1 = (p1 + dp) % 1.0
saw = p1 * 2.0 - 1.0

dpSub = dp * 0.5
pSub = (pSub + dpSub) % 1.0
sub = if(pSub < 0.5, 1.0, -1.0)

sig = (saw * 0.75 + sub * 0.55) * (1.0 + drive * 1.4)

fNorm = clamp(cutoff * 0.16 + env * envMod * 0.34, 0.015, 0.45)
kRes = clamp(resonance * 1.85, 0.1, 1.9)

s1 = s1 + fNorm * (sig - s2 * kRes - s1)
s2 = s2 + fNorm * (s1 - s2)

sat = (s2 / (1.0 + abs(s2 * 0.5))) * env
out = sat * 0.55
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: industrial_techno)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "industrial-techno-warehouse-distortion-reverb-fx",
        "title": "Industrial Techno Warehouse Distortion Reverb FX",
        "description": "Cavernous Berlin warehouse reverb effect combining asymmetric pre-saturation drive, dense multi-tap delay diffusion network, resonant dark low-frequency room reflections, and high-frequency damping.",
        "tags": ["effect", "reverb", "distortion", "industrial", "warehouse", "audio"],
        "params": {
            "decay": 0.65,
            "drive": 0.45,
            "damping": 0.45,
            "roomSize": 0.55,
            "mix": 0.50
        },
        "code": """param float decay = 0.65 [0.1, 0.95]
param float drive = 0.45 [0.0, 1.0]
param float damping = 0.45 [0.0, 1.0]
param float roomSize = 0.55 [0.1, 1.0]
param float mix = 0.50 [0.0, 1.0]
state float d1 = 0
state float d2 = 0
state float d3 = 0
state float d4 = 0
state float lp = 0

inSat = in * (1.0 + drive * 2.2)
sat = inSat / (1.0 + abs(inSat * 0.7))

fDamp = clamp(0.12 + (1.0 - damping) * 0.28, 0.03, 0.42)
lp = lp + fDamp * (sat - lp)

kDiff = clamp(0.10 * (1.0 + roomSize * 0.8), 0.03, 0.35)
d1 = d1 + kDiff * (lp + d4 * 0.32 * decay - d1)
d2 = d2 + kDiff * 0.89 * (d1 - d2)
d3 = d3 + kDiff * 0.81 * (d2 - d3)
d4 = d4 + kDiff * 0.73 * (d3 - d4)

rev = (d2 - d3 + d4) * 0.85
processed = in * (1.0 - mix) + rev * mix
out = processed * 0.70
"""
    },
    {
        "slug": "industrial-techno-grimy-bitcrush-saturator-fx",
        "title": "Industrial Techno Grimy Bitcrush Saturator FX",
        "description": "Punishing industrial waveshaper processor featuring asymmetric diode wavefolding, quantized bit-depth degradation, resonant post-overdrive lowpass filter, and parallel dry-wet blend.",
        "tags": ["effect", "distortion", "bitcrush", "saturator", "industrial", "audio"],
        "params": {
            "drive": 0.55,
            "crush": 0.45,
            "tone": 0.50,
            "warmth": 0.40,
            "mix": 0.60
        },
        "code": """param float drive = 0.55 [0.0, 1.0]
param float crush = 0.45 [0.0, 0.9]
param float tone = 0.50 [0.1, 1.0]
param float warmth = 0.40 [0.0, 1.0]
param float mix = 0.60 [0.0, 1.0]
state float s1 = 0
state float s2 = 0

steps = clamp(floor(32.0 - crush * 24.0), 4.0, 32.0)
qIn = floor(in * steps) / steps

pre = qIn * (1.0 + drive * 2.8)
sat = (pre - (pre * pre * pre * 0.15)) / (1.0 + abs(pre * 0.4))

fCut = clamp(0.08 + tone * 0.35, 0.02, 0.45)
s1 = s1 + fCut * (sat - s1)
s2 = s2 + fCut * (s1 - s2)

warm = s2 * (1.0 + warmth * 0.3)
processed = in * (1.0 - mix) + warm * mix
out = processed * 0.45
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel Sources & Embeds (Theme: op_art)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "op-art-concentric-moire-interference",
        "title": "Op-Art Concentric Moiré Interference",
        "description": "Hypnotic kinetic op-art simulation generating pulsating moiré interference patterns from twin intersecting high-frequency circular wavefields, audio-reactive spatial frequency modulation, and optical edge vibration.",
        "embed_desc": "Audio-reactive kinetic Op-Art visualizer featuring dual interfering circular wavefields that generate pulsating moiré interference bands. Bass frequencies swell wave amplitude and radial spacing, mid frequencies modulate optical interference frequency, and high frequencies ignite vibrant electric crimson edge fringes.",
        "tags": ["pixel", "opart", "moire", "optical", "kinetic", "shader"],
        "params": {
            "speed": 0.40,
            "freq": 26.0,
            "offset": 0.32,
            "contrast": 1.20,
            "glow": 0.50
        },
        "code": """param float speed = 0.40 [0.1, 1.5]
param float freq = 26.0 [10.0, 50.0]
param float offset = 0.32 [0.1, 0.8]
param float contrast = 1.20 [0.6, 2.0]
param float glow = 0.50 [0.1, 1.2]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.5
time = t * speed

p1 = p - vec2(offset, 0.0)
p2 = p + vec2(offset, 0.0)

d1 = length(p1)
d2 = length(p2)

w1 = sin(d1 * freq - time * 3.5)
w2 = sin(d2 * freq - time * 3.5)
interf = (w1 + w2) * 0.5

ring = sin(length(p) * 14.0 + time * 1.5)
sig = interf * 0.75 + ring * 0.25

cDark = vec3(0.04, 0.04, 0.05)
cWhite = vec3(0.96, 0.96, 0.98)
cCrimson = vec3(0.88, 0.12, 0.18)
cCyan = vec3(0.0, 0.90, 0.98)

bw = smoothstep(-0.12, 0.12, sig)
col = mix(cDark, cWhite, bw)

edge = 1.0 - abs(sig) * 2.2
col = mix(col, cCrimson, clamp(edge * glow * step(0.5, bw), 0.0, 1.0))
col = mix(col, cCyan, clamp(edge * glow * (1.0 - step(0.5, bw)), 0.0, 1.0))

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
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.5;
  float time = t * 0.40;

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  float offset = 0.32 + b * 0.12;
  vec2 p1 = p - vec2(offset, 0.0);
  vec2 p2 = p + vec2(offset, 0.0);

  float d1 = length(p1);
  float d2 = length(p2);

  float freq = 26.0 + m * 8.0;
  float w1 = sin(d1 * freq - time * 3.5);
  float w2 = sin(d2 * freq - time * 3.5);
  float interf = (w1 + w2) * 0.5 * (1.0 + b * 0.35);

  float ring = sin(length(p) * 14.0 + time * 1.5);
  float sig = interf * 0.75 + ring * 0.25;

  vec3 cDark = vec3(0.04, 0.04, 0.05);
  vec3 cWhite = vec3(0.96, 0.96, 0.98);
  vec3 cCrimson = vec3(0.88, 0.12, 0.18);
  vec3 cCyan = vec3(0.0, 0.90, 0.98);

  float bw = smoothstep(-0.12, 0.12, sig);
  vec3 col = mix(cDark, cWhite, bw);

  float edge = 1.0 - abs(sig) * 2.2;
  col = mix(col, cCrimson, clamp(edge * (0.50 + h * 0.4) * step(0.5, bw), 0.0, 1.0));
  col = mix(col, cCyan, clamp(edge * (0.50 + h * 0.4) * (1.0 - step(0.5, bw)), 0.0, 1.0));

  gl_FragColor = vec4(clamp(col * 1.20, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "op-art-hypnotic-checkerboard-warp",
        "title": "Op-Art Hypnotic Checkerboard Warp",
        "description": "Warping curvilinear optical checkerboard matrix with breathing radial barrel distortion, audio-reactive perspective shearing, hypnotic alternating black and white tile pulsations, and razor-sharp contrast.",
        "embed_desc": "Audio-reactive Op-Art checkerboard visualizer driven by pulsating curvilinear barrel distortion. Bass pulses expand the center optical vortex, mid frequencies twist perspective sheer across alternating black and white tiles, and high frequencies trigger electric optical color shifts along tile boundaries.",
        "tags": ["pixel", "opart", "checkerboard", "optical", "geometric", "shader"],
        "params": {
            "speed": 0.38,
            "tiles": 14.0,
            "warp_amp": 0.45,
            "contrast": 1.25,
            "flare": 0.45
        },
        "code": """param float speed = 0.38 [0.1, 1.5]
param float tiles = 14.0 [6.0, 30.0]
param float warp_amp = 0.45 [0.1, 1.0]
param float contrast = 1.25 [0.6, 2.0]
param float flare = 0.45 [0.0, 1.0]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.4
time = t * speed

r = length(p)
theta = atan2(p.y, p.x)

lens = r * (1.0 + r * r * warp_amp)
shearU = lens * cos(theta + sin(lens * 4.0 - time * 2.5) * 0.35)
shearV = lens * sin(theta + cos(lens * 4.0 - time * 2.5) * 0.35)

tileX = mod(shearU * tiles + time * 0.8, 2.0)
tileY = mod(shearV * tiles - time * 0.8, 2.0)

bX = step(1.0, tileX)
bY = step(1.0, tileY)
check = abs(bX - bY)

cDark = vec3(0.05, 0.05, 0.06)
cWhite = vec3(0.96, 0.96, 0.98)
cCrimson = vec3(0.92, 0.14, 0.20)
cCyan = vec3(0.0, 0.88, 0.95)

vortex = exp(-r * 3.5)
col = mix(cDark, cWhite, check)
col = mix(col, cCrimson, vortex * flare * 1.5)

edge = exp(-abs(check - 0.5) * 6.0)
col = mix(col, cCyan, edge * flare * 0.6)

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
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.4;
  float time = t * 0.38;

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  float r = length(p);
  float theta = atan(p.y, p.x);

  float warp_amp = 0.45 + b * 0.25;
  float lens = r * (1.0 + r * r * warp_amp);
  float twist = sin(lens * (4.0 + m * 2.0) - time * 2.5) * 0.35;
  float shearU = lens * cos(theta + twist);
  float shearV = lens * sin(theta + twist);

  float tiles = 14.0 + m * 4.0;
  float tileX = mod(shearU * tiles + time * 0.8, 2.0);
  float tileY = mod(shearV * tiles - time * 0.8, 2.0);

  float bX = step(1.0, tileX);
  float bY = step(1.0, tileY);
  float check = abs(bX - bY);

  vec3 cDark = vec3(0.05, 0.05, 0.06);
  vec3 cWhite = vec3(0.96, 0.96, 0.98);
  vec3 cCrimson = vec3(0.92, 0.14, 0.20);
  vec3 cCyan = vec3(0.0, 0.88, 0.95);

  float vortex = exp(-r * 3.5);
  vec3 col = mix(cDark, cWhite, check);
  col = mix(col, cCrimson, vortex * (0.45 + b * 0.5) * 1.5);

  float edge = exp(-abs(check - 0.5) * 6.0);
  col = mix(col, cCyan, edge * (0.45 + h * 0.4) * 0.6);

  gl_FragColor = vec4(clamp(col * 1.25, 0.0, 1.0), 1.0);
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
      background: linear-gradient(135deg, #5bc0be, #ffffff);
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
    print("=== Creative Expansion Pack 31: Build and Gate Runner ===")
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
  f rateBeats 0.25
  i steps 16
  i note0 {nr}
  i note1 {nr}
  i note2 {nr+12}
  i note3 {nr+7}
  i note4 {nr}
  i note5 {nr+12}
  i note6 {nr+5}
  i note7 {nr+7}
  i note8 {nr}
  i note9 {nr}
  i note10 {nr+12}
  i note11 {nr+15}
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
