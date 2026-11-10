#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 36.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: technical_blueprint -> technical-blueprint-isometric-gear-schematic, technical-blueprint-aerospace-telemetry-grid)
2. Sketch 3D (Theme: pixel_art_8bit -> pixel-art-8bit-voxel-monolith-citadel-3d, pixel-art-8bit-isometric-arcade-ziggurat-3d)
3. Field Synth (Theme: uk_garage -> uk-garage-warped-sub-bass-synth, uk-garage-2step-organ-stab-lead-synth)
4. Field Effects (Theme: detroit_techno -> detroit-techno-motor-city-tape-delay-fx, detroit-techno-warehouse-chorus-diffusion-fx)
5. Field Pixel (Theme: riso_manga_zine -> riso-manga-zine-halftone-dot-matrix, riso-manga-zine-chromatic-ink-misregistration)
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

TMP_DIR = "/tmp/pack36_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: technical_blueprint)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "technical-blueprint-isometric-gear-schematic",
        "title": "Technical Blueprint Isometric Gear Schematic",
        "description": "Procedural mathematical drafting drawing rendering interlocking epicyclic gear trains, concentric pitch circles, involute gear teeth profiles, and dimensional tolerances on a Prussian blue drafting plane.",
        "tags": ["technical", "blueprint", "schematic", "gear", "isometric", "drawing"],
        "code": """// Technical Blueprint Isometric Gear Schematic - Infinite Library (Sketch 2D)
param("teeth", 16.0, 8.0, 32.0);
param("ring_scale", 0.85, 0.5, 1.5);
param("precision_ticks", 48.0, 24.0, 96.0);
param("draft_opacity", 0.88, 0.4, 1.0);

function draw(t) {
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;

  // 1. Deep Prussian Blue Blueprint Ground (#001a30 -> #002b49)
  const bgSteps = 60;
  for (let i = 0; i < bgSteps; i++) {
    const frac = i / bgSteps;
    const y1 = frac * h;
    const y2 = ((i + 1) / bgSteps) * h;
    const r = 0.0 + frac * 0.01;
    const g = 0.10 + frac * 0.07;
    const b = 0.19 + frac * 0.10;
    fill(r, g, b);
    noStroke();
    rect(0, y1, w, y2 - y1 + 1.0);
  }

  // 2. Fine Drafting Coordinate Grid
  stroke(0.0, 0.83, 1.0, 0.14 * draft_opacity);
  strokeWeight(0.8);
  const minorGrid = 24.0;
  for (let x = 0; x <= w; x += minorGrid) {
    line(x, 0, x, h);
  }
  for (let y = 0; y <= h; y += minorGrid) {
    line(0, y, w, y);
  }

  // Major Coordinate Grid with Axis Accents
  stroke(0.0, 0.83, 1.0, 0.32 * draft_opacity);
  strokeWeight(1.2);
  const majorGrid = 120.0;
  for (let x = 0; x <= w; x += majorGrid) {
    line(x, 0, x, h);
  }
  for (let y = 0; y <= h; y += majorGrid) {
    line(0, y, w, y);
  }

  // Precision Outer Technical Border & Datum Registration Corners
  stroke(1.0, 1.0, 1.0, 0.75 * draft_opacity);
  strokeWeight(1.8);
  rect(36, 36, w - 72, h - 72);
  rect(44, 44, w - 88, h - 88);

  // Corner Crosshairs & Calibration Ticks
  const cornerMarks = [[40, 40], [w - 40, 40], [40, h - 40], [w - 40, h - 40]];
  for (let i = 0; i < cornerMarks.length; i++) {
    const pt = cornerMarks[i];
    line(pt[0] - 16, pt[1], pt[0] + 16, pt[1]);
    line(pt[0], pt[1] - 16, pt[0], pt[1] + 16);
    noFill();
    circle(pt[0], pt[1], 14);
  }

  // 3. Central Planetary Gear Mechanism
  const time = t * 0.35;
  const baseR = min(w, h) * 0.34 * ring_scale;

  // Outer Annular Ring Gear Pitch Circle
  stroke(0.0, 0.83, 1.0, 0.85 * draft_opacity);
  strokeWeight(2.0);
  noFill();
  circle(cx, cy, baseR * 2.0);
  stroke(1.0, 1.0, 1.0, 0.45 * draft_opacity);
  strokeWeight(1.0);
  circle(cx, cy, baseR * 2.14);
  circle(cx, cy, baseR * 1.86);

  // Outer Circular Vernier Angle Scale
  const nTicks = floor(precision_ticks);
  for (let k = 0; k < nTicks; k++) {
    const ang = (k / nTicks) * 6.2831853;
    const isMajor = (k % 4 === 0);
    const rIn = baseR * 2.0;
    const rOut = baseR * 2.0 + (isMajor ? 14 : 7);
    stroke(isMajor ? 1.0 : 0.0, isMajor ? 1.0 : 0.83, 1.0, isMajor ? 0.9 : 0.5);
    strokeWeight(isMajor ? 1.4 : 0.9);
    line(cx + cos(ang) * rIn, cy + sin(ang) * rIn, cx + cos(ang) * rOut, cy + sin(ang) * rOut);
  }

  // Central Sun Gear
  const sunR = baseR * 0.38;
  const nSunTeeth = floor(teeth);
  stroke(0.0, 0.83, 1.0, 0.95);
  strokeWeight(1.8);
  fill(0.0, 0.16, 0.28, 0.75);
  circle(cx, cy, sunR * 1.7);

  // Sun Gear Teeth Profiles
  for (let i = 0; i < nSunTeeth; i++) {
    const a = (i / nSunTeeth) * 6.2831853 + time;
    const a1 = a - 0.08;
    const a2 = a + 0.08;
    const r0 = sunR * 0.88;
    const r1 = sunR * 1.15;
    fill(0.0, 0.83, 1.0, 0.85);
    noStroke();
    quad(
      cx + cos(a1) * r0, cy + sin(a1) * r0,
      cx + cos(a1) * r1, cy + sin(a1) * r1,
      cx + cos(a2) * r1, cy + sin(a2) * r1,
      cx + cos(a2) * r0, cy + sin(a2) * r0
    );
  }

  // Sun Gear Center Bore & Keyway
  fill(0.0, 0.10, 0.20);
  stroke(1.0, 1.0, 1.0, 0.85);
  strokeWeight(1.5);
  circle(cx, cy, sunR * 0.45);
  fill(0.0, 0.83, 1.0, 0.9);
  rect(cx - 4, cy - sunR * 0.32, 8, 12);

  // 4. Three Interlocking Planet Gears
  const nPlanets = 3;
  const orbitR = baseR * 0.62;
  const planetR = baseR * 0.26;
  const nPlanetTeeth = floor(teeth * 0.68);

  for (let p = 0; p < nPlanets; p++) {
    const orbitAng = (p / nPlanets) * 6.2831853 - time * 0.6;
    const px = cx + cos(orbitAng) * orbitR;
    const py = cy + sin(orbitAng) * orbitR;

    // Pitch Circle & Spokes
    stroke(0.0, 0.83, 1.0, 0.85);
    strokeWeight(1.5);
    fill(0.0, 0.15, 0.26, 0.6);
    circle(px, py, planetR * 1.8);

    // Planet Gear Teeth
    const spinAng = time * 1.8 + p * 2.1;
    for (let k = 0; k < nPlanetTeeth; k++) {
      const a = (k / nPlanetTeeth) * 6.2831853 + spinAng;
      const a1 = a - 0.10;
      const a2 = a + 0.10;
      const r0 = planetR * 0.85;
      const r1 = planetR * 1.18;
      fill(1.0, 1.0, 1.0, 0.75);
      noStroke();
      quad(
        px + cos(a1) * r0, py + sin(a1) * r0,
        px + cos(a1) * r1, py + sin(a1) * r1,
        px + cos(a2) * r1, py + sin(a2) * r1,
        px + cos(a2) * r0, py + sin(a2) * r0
      );
    }

    // Planet Hub & Bolt Circle
    stroke(0.0, 0.83, 1.0, 0.9);
    strokeWeight(1.4);
    fill(0.0, 0.12, 0.22);
    circle(px, py, planetR * 0.55);
    for (let b = 0; b < 4; b++) {
      const ba = (b / 4) * 6.2831853;
      fill(1.0, 1.0, 1.0, 0.9);
      noStroke();
      circle(px + cos(ba) * planetR * 0.32, py + sin(ba) * planetR * 0.32, 4);
    }

    // Carrier Arm Lines
    stroke(1.0, 1.0, 1.0, 0.40);
    strokeWeight(1.0);
    line(cx, cy, px, py);
  }

  // 5. Technical Dimension Lines & Dimension Text Annotation
  stroke(1.0, 1.0, 1.0, 0.65 * draft_opacity);
  strokeWeight(1.2);

  // Horizontal Centerline
  line(cx - baseR * 1.25, cy, cx + baseR * 1.25, cy);
  // Vertical Centerline
  line(cx, cy - baseR * 1.25, cx, cy + baseR * 1.25);

  // Diagonal Dimension Leader Lines with Arrowheads
  const dimAng = -0.785398;
  const leadX1 = cx + cos(dimAng) * baseR;
  const leadY1 = cy + sin(dimAng) * baseR;
  const leadX2 = leadX1 + 75;
  const leadY2 = leadY1 - 55;
  line(leadX1, leadY1, leadX2, leadY2);
  line(leadX2, leadY2, leadX2 + 85, leadY2);

  // Tolerance Callout Box
  stroke(0.0, 0.83, 1.0, 0.75);
  strokeWeight(1.0);
  fill(0.0, 0.12, 0.22, 0.85);
  rect(leadX2 + 4, leadY2 - 22, 78, 18);

  // Technical Specification Stamp Block (Bottom Right)
  const stampX = w - 240;
  const stampY = h - 110;
  stroke(0.0, 0.83, 1.0, 0.80);
  strokeWeight(1.2);
  fill(0.0, 0.10, 0.20, 0.90);
  rect(stampX, stampY, 196, 66);
  line(stampX, stampY + 22, stampX + 196, stampY + 22);
  line(stampX, stampY + 44, stampX + 196, stampY + 44);
  line(stampX + 98, stampY + 22, stampX + 98, stampY + 66);

  // Stamp Block Internal Detail Lines
  stroke(1.0, 1.0, 1.0, 0.50);
  strokeWeight(1.0);
  line(stampX + 10, stampY + 11, stampX + 180, stampY + 11);
  line(stampX + 10, stampY + 33, stampX + 88, stampY + 33);
  line(stampX + 108, stampY + 33, stampX + 186, stampY + 33);
  line(stampX + 10, stampY + 55, stampX + 88, stampY + 55);
}
"""
    },
    {
        "slug": "technical-blueprint-aerospace-telemetry-grid",
        "title": "Technical Blueprint Aerospace Telemetry Grid",
        "description": "Mathematical architectural schematic drawing depicting multi-axis polar coordinate radar grids, orbital trajectory ellipses, telemetry vernier scales, and precision graticule crosshairs.",
        "tags": ["technical", "blueprint", "telemetry", "radar", "orbital", "drawing"],
        "code": """// Technical Blueprint Aerospace Telemetry Grid - Infinite Library (Sketch 2D)
param("radar_rings", 7.0, 4.0, 12.0);
param("sweep_angle", 0.45, 0.1, 1.0);
param("trajectory_tilt", 0.38, 0.1, 0.8);
param("reticle_size", 0.80, 0.4, 1.2);

function draw(t) {
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;

  // 1. Deep Midnight Prussian Cyan Ground (#001528 -> #002b49)
  const bgSteps = 60;
  for (let i = 0; i < bgSteps; i++) {
    const frac = i / bgSteps;
    const y1 = frac * h;
    const y2 = ((i + 1) / bgSteps) * h;
    const r = 0.0 + frac * 0.01;
    const g = 0.08 + frac * 0.08;
    const b = 0.16 + frac * 0.12;
    fill(r, g, b);
    noStroke();
    rect(0, y1, w, y2 - y1 + 1.0);
  }

  // 2. Cartesian Precision Graticule Lines
  stroke(0.0, 0.83, 1.0, 0.12);
  strokeWeight(0.8);
  const minorStep = 20.0;
  for (let x = 0; x <= w; x += minorStep) {
    line(x, 0, x, h);
  }
  for (let y = 0; y <= h; y += minorStep) {
    line(0, y, w, y);
  }

  // Major Coordinate Grids with Subdivided Hash Marks
  stroke(0.0, 0.83, 1.0, 0.28);
  strokeWeight(1.2);
  const majorStep = 100.0;
  for (let x = 0; x <= w; x += majorStep) {
    line(x, 0, x, h);
    for (let y = 0; y <= h; y += 20) {
      line(x - 3, y, x + 3, y);
    }
  }
  for (let y = 0; y <= h; y += majorStep) {
    line(0, y, w, y);
    for (let x = 0; x <= w; x += 20) {
      line(x, y - 3, x, y + 3);
    }
  }

  // 3. Polar Coordinate Radar Rings
  const nRings = floor(radar_rings);
  const maxR = min(w, h) * 0.42 * reticle_size;

  for (let r = 1; r <= nRings; r++) {
    const rad = (r / nRings) * maxR;
    const isMajor = (r % 2 === 1);
    stroke(0.0, 0.83, 1.0, isMajor ? 0.70 : 0.35);
    strokeWeight(isMajor ? 1.6 : 0.9);
    noFill();
    circle(cx, cy, rad * 2.0);

    // Range Marker Ticks
    fill(1.0, 1.0, 1.0, 0.65);
    noStroke();
    rect(cx + rad - 3, cy - 2, 6, 4);
    rect(cx - rad - 3, cy - 2, 6, 4);
    rect(cx - 2, cy + rad - 3, 4, 6);
    rect(cx - 2, cy - rad - 3, 4, 6);
  }

  // Radial Azimuth Spokes (24 Radial Sectors)
  const nSectors = 24;
  for (let s = 0; s < nSectors; s++) {
    const ang = (s / nSectors) * 6.2831853;
    const isQuarter = (s % 6 === 0);
    stroke(isQuarter ? 1.0 : 0.0, isQuarter ? 1.0 : 0.83, 1.0, isQuarter ? 0.75 : 0.30);
    strokeWeight(isQuarter ? 1.4 : 0.8);
    line(cx, cy, cx + cos(ang) * maxR, cy + sin(ang) * maxR);

    // Outer Graticule Perimeter Compass Ticks
    const outerR = maxR * 1.04;
    const tickLen = isQuarter ? 12 : 6;
    line(
      cx + cos(ang) * maxR, cy + sin(ang) * maxR,
      cx + cos(ang) * (maxR + tickLen), cy + sin(ang) * (maxR + tickLen)
    );
  }

  // 4. Nested Orbital Trajectory Ellipses
  const time = t * sweep_angle;
  stroke(1.0, 1.0, 1.0, 0.65);
  strokeWeight(1.4);
  noFill();

  // Primary Orbital Ellipse (rotated)
  for (let e = 0; e < 3; e++) {
    const rot = trajectory_tilt + e * 0.62;
    const rx = maxR * (0.85 - e * 0.15);
    const ry = maxR * (0.42 - e * 0.08);

    stroke(e === 0 ? 0.0 : 1.0, 0.83, 1.0, 0.75 - e * 0.15);
    strokeWeight(e === 0 ? 1.6 : 1.1);

    const steps = 72;
    beginShape();
    for (let k = 0; k <= steps; k++) {
      const th = (k / steps) * 6.2831853;
      const ex = cos(th) * rx;
      const ey = sin(th) * ry;
      const rxRot = cx + ex * cos(rot) - ey * sin(rot);
      const ryRot = cy + ex * sin(rot) + ey * cos(rot);
      vertex(rxRot, ryRot);
    }
    endShape();

    // Satellite / Telemetry Node Position
    const satTh = time * (1.2 + e * 0.5) + e * 2.0;
    const satX0 = cos(satTh) * rx;
    const satY0 = sin(satTh) * ry;
    const satX = cx + satX0 * cos(rot) - satY0 * sin(rot);
    const satY = cy + satX0 * sin(rot) + satY0 * cos(rot);

    fill(1.0, 1.0, 1.0, 0.95);
    noStroke();
    circle(satX, satY, 8);
    stroke(0.0, 0.83, 1.0, 0.85);
    strokeWeight(1.2);
    noFill();
    circle(satX, satY, 16);

    // Telemetry Vector Line
    stroke(1.0, 0.3, 0.3, 0.75);
    line(satX, satY, satX + cos(satTh + 1.57) * 28, satY + sin(satTh + 1.57) * 28);
  }

  // 5. Central Telemetry Reticle & Crosshairs
  stroke(1.0, 1.0, 1.0, 0.90);
  strokeWeight(1.8);
  noFill();
  circle(cx, cy, 32);
  circle(cx, cy, 64);
  line(cx - 48, cy, cx + 48, cy);
  line(cx, cy - 48, cx, cy + 48);

  // 6. Corner Vernier Arc Scales (Top Left & Bottom Right)
  const arcCorners = [[80, 80, 0], [w - 80, h - 80, 3.14159]];
  for (let c = 0; c < arcCorners.length; c++) {
    const ac = arcCorners[c];
    stroke(0.0, 0.83, 1.0, 0.65);
    strokeWeight(1.2);
    noFill();
    const arcR = 54;
    for (let k = 0; k <= 18; k++) {
      const a = ac[2] + (k / 18) * 1.570796;
      const l1 = arcR;
      const l2 = arcR + (k % 3 === 0 ? 10 : 5);
      line(ac[0] + cos(a) * l1, ac[1] + sin(a) * l1, ac[0] + cos(a) * l2, ac[1] + sin(a) * l2);
    }
  }

  // 7. Outer Technical Frame with Graticule Coordinates
  stroke(0.0, 0.83, 1.0, 0.85);
  strokeWeight(2.0);
  rect(32, 32, w - 64, h - 64);
  stroke(1.0, 1.0, 1.0, 0.40);
  strokeWeight(1.0);
  rect(40, 40, w - 80, h - 80);
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: pixel_art_8bit)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "pixel-art-8bit-voxel-monolith-citadel-3d",
        "title": "Pixel Art 8-Bit Voxel Monolith Citadel 3D",
        "description": "Three-dimensional procedural voxel citadel featuring stepped modular pixel pylons, cantilevered arcade cuboids, and relief battlements rendered in classic four-shade dot-matrix phosphor green.",
        "tags": ["pixelart", "voxel", "citadel", "8bit", "retro", "3d"],
        "code": """// Pixel Art 8-Bit Voxel Monolith Citadel 3D - Infinite Library (Sketch 3D)
param("spin", 0.35, 0.1, 1.2);
param("scale_factor", 1.15, 0.6, 2.0);
param("tower_height", 1.25, 0.6, 2.0);
param("cantilever", 0.95, 0.4, 1.6);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.46 + sin(time * 0.12) * 0.04);
  rotateY(time * 0.35 + 0.68);

  // Authentic Game Boy DMG 4-Tone Green Palette
  // Shade 0 (darkest shadow):  #0f380f -> rgb(0.059, 0.220, 0.059)
  // Shade 1 (dark moss):       #306230 -> rgb(0.188, 0.384, 0.188)
  // Shade 2 (phosphor olive):  #8bac0f -> rgb(0.545, 0.675, 0.059)
  // Shade 3 (brightest lime):  #9bbc0f -> rgb(0.608, 0.737, 0.059)

  // 1. Massive Foundation Voxel Plinth & Ground Tier
  push();
  translate(0, 1.15 * scale_factor, 0);
  fill(0.059, 0.220, 0.059);
  box(2.8 * scale_factor, 0.22 * scale_factor, 2.8 * scale_factor);

  translate(0, -0.16 * scale_factor, 0);
  fill(0.188, 0.384, 0.188);
  box(2.3 * scale_factor, 0.18 * scale_factor, 2.3 * scale_factor);

  translate(0, -0.15 * scale_factor, 0);
  fill(0.545, 0.675, 0.059);
  box(1.9 * scale_factor, 0.14 * scale_factor, 1.9 * scale_factor);
  pop();

  // 2. Central Stepped Voxel Monolith Tower
  const nSegments = 7;
  for (let s = 0; s < nSegments; s++) {
    const frac = s / nSegments;
    const yPos = (0.75 - s * 0.30 * tower_height) * scale_factor;
    const segW = (1.55 - frac * 0.75) * scale_factor;
    const segD = (1.55 - frac * 0.75) * scale_factor;
    const segH = 0.24 * scale_factor;

    push();
    translate(0, yPos, 0);

    if (s % 3 === 0) {
      fill(0.188, 0.384, 0.188);
    } else if (s % 3 === 1) {
      fill(0.545, 0.675, 0.059);
    } else {
      fill(0.608, 0.737, 0.059);
    }
    box(segW, segH, segD);

    // Relief Voxel Cornice Trim
    translate(0, -segH * 0.45, 0);
    fill(0.059, 0.220, 0.059);
    box(segW * 1.08, segH * 0.25, segD * 1.08);
    pop();
  }

  // 3. Four Symmetrical Corner Bastion Pylons
  const cornerDist = 0.95 * cantilever * scale_factor;
  const bastionCorners = [
    [-cornerDist, -cornerDist],
    [cornerDist, -cornerDist],
    [cornerDist, cornerDist],
    [-cornerDist, cornerDist]
  ];

  for (let i = 0; i < bastionCorners.length; i++) {
    const bx = bastionCorners[i][0];
    const bz = bastionCorners[i][1];

    push();
    translate(bx, 0.35 * scale_factor, bz);
    fill(0.188, 0.384, 0.188);
    box(0.55 * scale_factor, 1.45 * scale_factor, 0.55 * scale_factor);

    // Cantilevered Upper Arcade Pod
    translate(0, -0.75 * scale_factor, 0);
    fill(0.545, 0.675, 0.059);
    box(0.72 * scale_factor, 0.30 * scale_factor, 0.72 * scale_factor);

    // Crenellation Turret Teeth
    translate(0, -0.18 * scale_factor, 0);
    fill(0.608, 0.737, 0.059);
    box(0.35 * scale_factor, 0.18 * scale_factor, 0.35 * scale_factor);
    pop();
  }

  // 4. Cantilevered Connecting Skybridges
  push();
  translate(0, 0.05 * scale_factor, 0);
  fill(0.545, 0.675, 0.059);
  box(cornerDist * 2.1, 0.12 * scale_factor, 0.28 * scale_factor);
  box(0.28 * scale_factor, 0.12 * scale_factor, cornerDist * 2.1);
  pop();

  // 5. Crown Finial Spire & Apex Voxel Crystal
  push();
  const crownY = (-0.75 - 0.28 * tower_height) * scale_factor;
  translate(0, crownY, 0);
  fill(0.608, 0.737, 0.059);
  box(0.42 * scale_factor, 0.45 * scale_factor, 0.42 * scale_factor);

  translate(0, -0.32 * scale_factor, 0);
  fill(0.545, 0.675, 0.059);
  box(0.22 * scale_factor, 0.38 * scale_factor, 0.22 * scale_factor);

  translate(0, -0.24 * scale_factor, 0);
  fill(0.608, 0.737, 0.059);
  box(0.12 * scale_factor, 0.22 * scale_factor, 0.12 * scale_factor);
  pop();

  pop();
}
"""
    },
    {
        "slug": "pixel-art-8bit-isometric-arcade-ziggurat-3d",
        "title": "Pixel Art 8-Bit Isometric Arcade Ziggurat 3D",
        "description": "Three-dimensional stepped isometric arcade ziggurat with cascading voxel terraces, ceremonial portal archways, and floating modular pixel satellites in classic four-tone dot-matrix palette.",
        "tags": ["pixelart", "ziggurat", "isometric", "8bit", "arcade", "3d"],
        "code": """// Pixel Art 8-Bit Isometric Arcade Ziggurat 3D - Infinite Library (Sketch 3D)
param("spin", 0.32, 0.1, 1.2);
param("scale_factor", 1.12, 0.6, 2.0);
param("terrace_steps", 8.0, 4.0, 12.0);
param("satellite_orbit", 1.05, 0.5, 1.8);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.50 + sin(time * 0.14) * 0.03);
  rotateY(time * 0.32 + 0.78);

  // Game Boy DMG 4-Tone Green Palette
  // Shade 0: rgb(0.059, 0.220, 0.059)
  // Shade 1: rgb(0.188, 0.384, 0.188)
  // Shade 2: rgb(0.545, 0.675, 0.059)
  // Shade 3: rgb(0.608, 0.737, 0.059)

  // 1. Massive Terraced Voxel Ziggurat Pyramid
  const nSteps = floor(terrace_steps);
  const baseSize = 2.6 * scale_factor;
  const stepH = (1.8 / nSteps) * scale_factor;

  for (let k = 0; k < nSteps; k++) {
    const frac = k / nSteps;
    const yPos = (0.90 - k * stepH) * scale_factor;
    const curW = baseSize * (1.0 - frac * 0.78);
    const curD = curW;

    push();
    translate(0, yPos, 0);

    // Alternate shading through the 4-tone palette
    if (k % 4 === 0) {
      fill(0.059, 0.220, 0.059);
    } else if (k % 4 === 1) {
      fill(0.188, 0.384, 0.188);
    } else if (k % 4 === 2) {
      fill(0.545, 0.675, 0.059);
    } else {
      fill(0.608, 0.737, 0.059);
    }
    box(curW, stepH * 0.95, curD);

    // Stepped Axial Grand Staircase on North/South Faces
    fill(0.608, 0.737, 0.059);
    const stairW = 0.32 * scale_factor;
    translate(0, 0, curD * 0.51);
    box(stairW, stepH * 0.7, 0.12 * scale_factor);
    translate(0, 0, -curD * 1.02);
    box(stairW, stepH * 0.7, 0.12 * scale_factor);
    pop();
  }

  // 2. Summit Altar & Ceremonial Portal Sanctuary
  const summitY = (0.90 - nSteps * stepH - 0.18) * scale_factor;
  push();
  translate(0, summitY, 0);
  fill(0.188, 0.384, 0.188);
  box(0.72 * scale_factor, 0.32 * scale_factor, 0.72 * scale_factor);

  // Portal Pillars
  translate(-0.24 * scale_factor, -0.25 * scale_factor, 0);
  fill(0.545, 0.675, 0.059);
  box(0.14 * scale_factor, 0.40 * scale_factor, 0.14 * scale_factor);
  translate(0.48 * scale_factor, 0, 0);
  box(0.14 * scale_factor, 0.40 * scale_factor, 0.14 * scale_factor);

  // Lintel Beam
  translate(-0.24 * scale_factor, -0.22 * scale_factor, 0);
  fill(0.608, 0.737, 0.059);
  box(0.65 * scale_factor, 0.12 * scale_factor, 0.22 * scale_factor);

  // Floating Apex Voxel Beacon
  translate(0, -0.25 * scale_factor, 0);
  fill(0.608, 0.737, 0.059);
  box(0.18 * scale_factor, 0.18 * scale_factor, 0.18 * scale_factor);
  pop();

  // 3. Four Orbiting Modular Voxel Satellites
  const orbitRad = 1.65 * satellite_orbit * scale_factor;
  for (let s = 0; s < 4; s++) {
    const ang = time * 0.85 + (s / 4) * 6.2831853;
    const sx = cos(ang) * orbitRad;
    const sz = sin(ang) * orbitRad;
    const sy = (sin(time * 1.2 + s * 1.5) * 0.25 - 0.1) * scale_factor;

    push();
    translate(sx, sy, sz);
    rotateY(ang * 2.0);
    fill(0.608, 0.737, 0.059);
    box(0.24 * scale_factor, 0.24 * scale_factor, 0.24 * scale_factor);

    translate(0, 0.18 * scale_factor, 0);
    fill(0.188, 0.384, 0.188);
    box(0.12 * scale_factor, 0.18 * scale_factor, 0.12 * scale_factor);
    pop();
  }

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: uk_garage)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "uk-garage-warped-sub-bass-synth",
        "title": "UK Garage Warped Sub Bass Synth",
        "description": "Dual-oscillator speed garage warped sub synthesizer combining heavy sine fundamental with detuned modulated saw glide, saturation drive, and envelope-swept low-pass filter.",
        "tags": ["ukgarage", "subbass", "warped", "speedgarage", "bass", "synth"],
        "note_root": 38,
        "params": {
            "cutoff": 0.52,
            "resonance": 0.30,
            "subLevel": 0.75,
            "sawWarp": 0.42,
            "drive": 0.35
        },
        "code": """param float cutoff = 0.52 [0.1, 1.0]
param float resonance = 0.30 [0.1, 0.88]
param float subLevel = 0.75 [0.0, 1.0]
param float sawWarp = 0.42 [0.0, 1.0]
param float drive = 0.35 [0.0, 1.0]
state float p1 = 0
state float p2 = 0
state float env = 0
state float s1 = 0
state float s2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * 0.55), 0.0001, 0.05)
env = if(target > env, env + (target - env) * 0.95, env - env * decayRate)

dp = freq / sr
p1 = (p1 + dp) % 1.0
subSine = sin(p1 * 6.2831853)

p2 = (p2 + dp * (1.004 + sawWarp * 0.008)) % 1.0
saw = p2 * 2.0 - 1.0

sig = subSine * subLevel * 0.95 + saw * (1.0 - subLevel * 0.5) * 0.65

fNorm = clamp(cutoff * 0.14 + env * 0.20, 0.015, 0.42)
kRes = clamp(resonance * 1.45, 0.1, 1.75)

s1 = s1 + fNorm * (sig - s2 * kRes - s1)
s2 = s2 + fNorm * (s1 - s2)

sat = (s2 / (1.0 + abs(s2 * (0.22 + drive * 0.28)))) * env
out = sat * 0.68
"""
    },
    {
        "slug": "uk-garage-2step-organ-stab-lead-synth",
        "title": "UK Garage 2-Step Organ Stab Lead Synth",
        "description": "Punchy percussive digital organ and chord stab synthesizer with multi-harmonic drawbar overtone partials, click attack transient, and warm analog chorus diffusion.",
        "tags": ["ukgarage", "organ", "2step", "stab", "lead", "synth"],
        "note_root": 50,
        "params": {
            "cutoff": 0.65,
            "resonance": 0.28,
            "percussion": 0.60,
            "drawbar": 0.70,
            "drive": 0.30
        },
        "code": """param float cutoff = 0.65 [0.1, 1.0]
param float resonance = 0.28 [0.1, 0.88]
param float percussion = 0.60 [0.0, 1.0]
param float drawbar = 0.70 [0.0, 1.0]
param float drive = 0.30 [0.0, 1.0]
state float p1 = 0
state float p2 = 0
state float p3 = 0
state float env = 0
state float clickEnv = 0
state float s1 = 0
state float s2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * 0.38), 0.0001, 0.05)
clickDecay = clamp(1.0 / (sr * 0.025), 0.001, 0.2)
env = if(target > env, env + (target - env) * 0.98, env - env * decayRate)
clickEnv = if(target > clickEnv, target, clickEnv - clickEnv * clickDecay)

dp = freq / sr
p1 = (p1 + dp) % 1.0
p2 = (p2 + dp * 2.0) % 1.0
p3 = (p3 + dp * 3.0) % 1.0

h1 = sin(p1 * 6.2831853)
h2 = sin(p2 * 6.2831853)
h3 = sin(p3 * 6.2831853)

organ = (h1 * 0.65 + h2 * drawbar * 0.45 + h3 * drawbar * 0.30)
click = sin(p1 * 12.56637) * clickEnv * percussion * 0.45
sig = organ + click

fNorm = clamp(cutoff * 0.22 + env * 0.16, 0.02, 0.45)
kRes = clamp(resonance * 1.35, 0.1, 1.65)

s1 = s1 + fNorm * (sig - s2 * kRes - s1)
s2 = s2 + fNorm * (s1 - s2)

sat = (s2 / (1.0 + abs(s2 * (0.20 + drive * 0.25)))) * env
out = sat * 0.62
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: detroit_techno)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "detroit-techno-motor-city-tape-delay-fx",
        "title": "Detroit Techno Motor City Tape Delay FX",
        "description": "Analog tape echo processor featuring tempo-sync feedback diffusion, head flutter modulation, non-linear tape saturation warmth, and high-frequency damping roll-off.",
        "tags": ["detroit", "techno", "delay", "tape", "echo", "effect"],
        "params": {
            "rate": 0.38,
            "feedback": 0.48,
            "tone": 0.45,
            "warmth": 0.45,
            "mix": 0.48
        },
        "code": """param float rate = 0.38 [0.05, 1.0]
param float feedback = 0.48 [0.1, 0.85]
param float tone = 0.45 [0.1, 1.0]
param float warmth = 0.45 [0.0, 1.0]
param float mix = 0.48 [0.0, 1.0]
state float d1 = 0
state float d2 = 0
state float d3 = 0
state float fb = 0
state float lp = 0
state float lfo = 0

lfo = (lfo + rate * 0.0018) % 1.0
flutter = sin(lfo * 6.2831853) * 0.025

fTone = clamp(0.05 + tone * 0.32, 0.02, 0.42)
inSat = in / (1.0 + abs(in * (0.18 + warmth * 0.22)))

kRate = clamp(0.08 + flutter, 0.02, 0.28)
d1 = d1 + kRate * ((inSat + fb * feedback) - d1)
d2 = d2 + kRate * 0.88 * (d1 - d2)
d3 = d3 + kRate * 0.75 * (d2 - d3)

lp = lp + fTone * (d3 - lp)
fb = lp / (1.0 + abs(lp * 0.25))

sat = d2 / (1.0 + abs(d2 * 0.22))
processed = in * (1.0 - mix) + sat * mix * 1.25
out = processed * 1.10
"""
    },
    {
        "slug": "detroit-techno-warehouse-chorus-diffusion-fx",
        "title": "Detroit Techno Warehouse Chorus Diffusion FX",
        "description": "Multi-voice analog bucket-brigade chorus and spatial diffusion effect providing wide stereo chorus modulation, lush harmonic depth, and subtle early-reflection warmth.",
        "tags": ["detroit", "techno", "chorus", "diffusion", "spatial", "effect"],
        "params": {
            "rate": 0.42,
            "depth": 0.52,
            "tone": 0.48,
            "warmth": 0.42,
            "mix": 0.52
        },
        "code": """param float rate = 0.42 [0.05, 1.0]
param float depth = 0.52 [0.0, 1.0]
param float tone = 0.48 [0.1, 1.0]
param float warmth = 0.42 [0.0, 1.0]
param float mix = 0.52 [0.0, 1.0]
state float c1 = 0
state float c2 = 0
state float c3 = 0
state float lp = 0
state float lfo1 = 0
state float lfo2 = 0

lfo1 = (lfo1 + rate * 0.0022) % 1.0
lfo2 = (lfo2 + rate * 0.0031 + 0.25) % 1.0
mod1 = sin(lfo1 * 6.2831853) * depth * 0.035
mod2 = cos(lfo2 * 6.2831853) * depth * 0.028

fTone = clamp(0.04 + tone * 0.36, 0.015, 0.44)
inSat = in / (1.0 + abs(in * (0.16 + warmth * 0.20)))
lp = lp + fTone * (inSat - lp)

kRate1 = clamp(0.12 + mod1, 0.03, 0.35)
kRate2 = clamp(0.10 + mod2, 0.02, 0.32)
c1 = c1 + kRate1 * (lp - c1)
c2 = c2 + kRate2 * (c1 - c2)
c3 = c3 + kRate1 * 0.8 * (c2 - c3)

chorusSig = (c1 * 0.45 + c2 * 0.42 - c3 * 0.32) * 1.35
sat = chorusSig / (1.0 + abs(chorusSig * 0.22))
processed = in * (1.0 - mix) + sat * mix
out = processed * 1.12
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel Sources (Theme: riso_manga_zine)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "riso-manga-zine-halftone-dot-matrix",
        "title": "Riso Manga Zine Halftone Dot Matrix",
        "description": "Audio-reactive risograph print shader rendering rotating screen halftone dot lattices, spot color ink overlapping, and bass-driven dot radius pulsation in fluorescent pink, cyan, and yellow inks.",
        "embed_desc": "Interactive audio-reactive risograph print shader rendering screen halftone dot matrices in fluorescent spot inks. Bass pulses halftone dot scale and ink spread, mids rotate print angle separation, and highs modulate fluorescent ink misregistration.",
        "tags": ["risograph", "manga", "halftone", "screentone", "zine", "shader"],
        "params": {
            "dotScale": 36.0,
            "inkDensity": 0.82,
            "contrast": 1.18,
            "paperWarmth": 0.88,
            "bloom": 0.45
        },
        "code": """param float dotScale = 36.0 [12.0, 72.0]
param float inkDensity = 0.82 [0.2, 1.4]
param float contrast = 1.18 [0.8, 1.6]
param float paperWarmth = 0.88 [0.5, 1.0]
param float bloom = 0.45 [0.0, 1.0]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2
time = t * 0.35

r = length(p)
theta = atan2(p.y, p.x)

rot1 = theta + 0.26 + time * 0.05
pPink = vec2(cos(rot1) * r, sin(rot1) * r) * dotScale

rot2 = theta + 1.31 + time * 0.07
pCyan = vec2(cos(rot2) * r, sin(rot2) * r) * dotScale

rot3 = theta + 0.78 + time * 0.04
pYellow = vec2(cos(rot3) * r, sin(rot3) * r) * dotScale

gridPink = fract(pPink) - 0.5
gridCyan = fract(pCyan) - 0.5
gridYellow = fract(pYellow) - 0.5

distPink = length(gridPink)
distCyan = length(gridCyan)
distYellow = length(gridYellow)

targetPink = 0.36 + sin(r * 4.0 - time * 2.0) * 0.14
targetCyan = 0.34 + cos(r * 5.0 + time * 1.6) * 0.12
targetYellow = 0.32 + sin(theta * 3.0 + time) * 0.10

dotPink = smoothstep(targetPink, targetPink - 0.08, distPink) * inkDensity
dotCyan = smoothstep(targetCyan, targetCyan - 0.08, distCyan) * inkDensity
dotYellow = smoothstep(targetYellow, targetYellow - 0.08, distYellow) * inkDensity

paper = vec3(0.98 * paperWarmth, 0.96 * paperWarmth, 0.91 * paperWarmth)

pinkInk = vec3(1.0, 0.0, 0.47)
cyanInk = vec3(0.0, 0.84, 1.0)
yellowInk = vec3(1.0, 0.92, 0.0)

col = paper * (1.0 - dotPink * (1.0 - pinkInk)) * (1.0 - dotCyan * (1.0 - cyanInk)) * (1.0 - dotYellow * (1.0 - yellowInk))

vignette = smoothstep(1.7, 0.6, r)
col = col * vignette * contrast
out = vec4(col, 1.0)
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
  float time = t * 0.35;

  float r = length(p);
  float theta = atan(p.y, p.x);

  float bBoost = audio_bass * 0.35;
  float mBoost = audio_mid * 0.25;
  float hBoost = audio_high * 0.30;

  float rot1 = theta + 0.26 + time * 0.05 + mBoost;
  vec2 pPink = vec2(cos(rot1) * r, sin(rot1) * r) * (36.0 + bBoost * 8.0);

  float rot2 = theta + 1.31 + time * 0.07;
  vec2 pCyan = vec2(cos(rot2) * r, sin(rot2) * r) * (36.0 + bBoost * 8.0) + vec2(hBoost * 0.8, -hBoost * 0.8);

  float rot3 = theta + 0.78 + time * 0.04;
  vec2 pYellow = vec2(cos(rot3) * r, sin(rot3) * r) * (36.0 + bBoost * 8.0);

  vec2 gridPink = fract(pPink) - 0.5;
  vec2 gridCyan = fract(pCyan) - 0.5;
  vec2 gridYellow = fract(pYellow) - 0.5;

  float distPink = length(gridPink);
  float distCyan = length(gridCyan);
  float distYellow = length(gridYellow);

  float targetPink = 0.34 + sin(r * 4.0 - time * 2.0) * 0.14 + bBoost * 0.18;
  float targetCyan = 0.32 + cos(r * 5.0 + time * 1.6) * 0.12 + mBoost * 0.14;
  float targetYellow = 0.30 + sin(theta * 3.0 + time) * 0.10 + hBoost * 0.12;

  float dotPink = smoothstep(targetPink, targetPink - 0.08, distPink) * 0.85;
  float dotCyan = smoothstep(targetCyan, targetCyan - 0.08, distCyan) * 0.85;
  float dotYellow = smoothstep(targetYellow, targetYellow - 0.08, distYellow) * 0.85;

  vec3 paper = vec3(0.98, 0.96, 0.91);
  vec3 pinkInk = vec3(1.0, 0.0, 0.47);
  vec3 cyanInk = vec3(0.0, 0.84, 1.0);
  vec3 yellowInk = vec3(1.0, 0.92, 0.0);

  vec3 col = paper * (1.0 - dotPink * (1.0 - pinkInk)) * (1.0 - dotCyan * (1.0 - cyanInk)) * (1.0 - dotYellow * (1.0 - yellowInk));
  float vignette = smoothstep(1.7, 0.6, r);
  gl_FragColor = vec4(col * vignette * 1.15, 1.0);
}
"""
    },
    {
        "slug": "riso-manga-zine-chromatic-ink-misregistration",
        "title": "Riso Manga Zine Chromatic Ink Misregistration",
        "description": "Audio-reactive risograph manga shader featuring high-frequency ink plate misregistration, dynamic radial action speedlines, and tactile paper grain texture under spot-color inks.",
        "embed_desc": "Audio-reactive risograph manga shader featuring speedline dynamics and spot-color ink misregistration. Bass commands ink saturation depth, mids pulse dynamic speedline convergence, and highs trigger physical riso print plate misregistration.",
        "tags": ["risograph", "manga", "speedlines", "misregistration", "print", "shader"],
        "params": {
            "speed": 0.38,
            "lineDensity": 52.0,
            "misreg": 0.42,
            "contrast": 1.20,
            "paperWarmth": 0.88
        },
        "code": """param float speed = 0.38 [0.1, 1.0]
param float lineDensity = 52.0 [20.0, 96.0]
param float misreg = 0.42 [0.1, 1.0]
param float contrast = 1.20 [0.8, 1.6]
param float paperWarmth = 0.88 [0.5, 1.0]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2
time = t * speed

r = length(p)
theta = atan2(p.y, p.x)

spokePink = abs(sin(theta * lineDensity * 0.5 + time * 1.5 + 0.1))
spokeCyan = abs(sin(theta * lineDensity * 0.5 - time * 1.2 - 0.1))
spokeYellow = abs(sin(theta * lineDensity * 0.5 + time * 0.8 + 0.2))

burst = smoothstep(0.18, 0.95, r)

linePink = smoothstep(0.45, 0.82, spokePink) * burst * 0.88
lineCyan = smoothstep(0.42, 0.80, spokeCyan) * burst * 0.85
lineYellow = smoothstep(0.40, 0.78, spokeYellow) * burst * 0.82

paper = vec3(0.97 * paperWarmth, 0.95 * paperWarmth, 0.90 * paperWarmth)

pinkInk = vec3(1.0, 0.05, 0.45)
cyanInk = vec3(0.0, 0.82, 0.98)
yellowInk = vec3(1.0, 0.90, 0.02)

col = paper * (1.0 - linePink * (1.0 - pinkInk)) * (1.0 - lineCyan * (1.0 - cyanInk)) * (1.0 - lineYellow * (1.0 - yellowInk))

centerDot = smoothstep(0.18, 0.15, r) * 0.85
col = mix(col, vec3(0.08, 0.08, 0.10), centerDot)

vignette = smoothstep(1.7, 0.65, r)
out = vec4(col * vignette * contrast, 1.0)
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
  float time = t * 0.38;

  float r = length(p);
  float theta = atan(p.y, p.x);

  float bBoost = audio_bass * 0.40;
  float mBoost = audio_mid * 0.30;
  float hBoost = audio_high * 0.35;

  float misregP = 0.1 + hBoost * 0.3;
  float misregC = -0.1 - hBoost * 0.3;

  float spokePink = abs(sin(theta * 26.0 + time * 1.5 + misregP));
  float spokeCyan = abs(sin(theta * 26.0 - time * 1.2 + misregC));
  float spokeYellow = abs(sin(theta * 26.0 + time * 0.8 + 0.2));

  float burst = smoothstep(0.15 - bBoost * 0.08, 0.95, r);

  float linePink = smoothstep(0.45, 0.82, spokePink) * burst * (0.85 + bBoost * 0.15);
  float lineCyan = smoothstep(0.42, 0.80, spokeCyan) * burst * (0.85 + mBoost * 0.15);
  float lineYellow = smoothstep(0.40, 0.78, spokeYellow) * burst * (0.80 + hBoost * 0.20);

  vec3 paper = vec3(0.97, 0.95, 0.90);
  vec3 pinkInk = vec3(1.0, 0.05, 0.45);
  vec3 cyanInk = vec3(0.0, 0.82, 0.98);
  vec3 yellowInk = vec3(1.0, 0.90, 0.02);

  vec3 col = paper * (1.0 - linePink * (1.0 - pinkInk)) * (1.0 - lineCyan * (1.0 - cyanInk)) * (1.0 - lineYellow * (1.0 - yellowInk));
  float centerDot = smoothstep(0.18 + bBoost * 0.06, 0.14, r) * 0.85;
  col = mix(col, vec3(0.08, 0.08, 0.10), centerDot);

  float vignette = smoothstep(1.7, 0.65, r);
  gl_FragColor = vec4(col * vignette * 1.20, 1.0);
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
    print("=== Creative Expansion Pack 36 Generator & Verification Gate Runner ===")
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
  c bg 0.08 0.14 0.08
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
  i note3 {nr+5}
  i note4 {nr}
  i note5 {nr+7}
  i note6 {nr+3}
  i note7 {nr+5}
  i note8 {nr-5}
  i note9 {nr-5}
  i note10 {nr+2}
  i note11 {nr}
  i note12 {nr-7}
  i note13 {nr-3}
  i note14 {nr+2}
  i note15 {nr+3}
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
