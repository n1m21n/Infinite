#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 23.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (retro-surreal-celestial-arch, retro-surreal-botanical-monolith)
2. Sketch 3D (liminal-void-floating-monolith-3d, liminal-void-dusk-colonnade-3d)
3. Field Synth (breakbeat-hardcore-sub-roller-synth, breakbeat-hoover-rave-stab-synth)
4. Field Effects (dark-ambient-abyssal-drone-reverb-fx, dark-ambient-spectral-filter-fx)
5. Field Pixel (y2k-acid-chrome-starburst, y2k-acid-liquid-matrix)
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

TMP_DIR = "/tmp/pack23_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: retro_surreal)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "retro-surreal-celestial-arch",
        "title": "Retro-Surreal Celestial Arch",
        "description": "Surrealist cutout composition on aged parchment with concentric celestial orbital rings, sage and terracotta archway portals, and midnight slate geometric plinths.",
        "tags": ["retro", "surreal", "collage", "celestial", "arch", "drawing"],
        "code": """// Retro-Surreal Celestial Arch - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Surrealist cutout composition on aged parchment with concentric celestial orbital rings, sage and terracotta archway portals, and midnight slate geometric plinths.
param("zoom", 1.0, 0.5, 2.0);
param("arch_width", 0.58, 0.3, 0.9);
param("orbital_rings", 5.0, 3.0, 9.0);
param("terracotta_opacity", 0.92, 0.4, 1.0);

function draw(t) {
  // Pale cream kraft ground
  background(0.996, 0.980, 0.878);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88 * zoom;

  push();
  translate(cx, cy);

  // Outer framing border in midnight slate
  stroke(0.169, 0.176, 0.259);
  strokeWeight(2.5);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  // Inner margin rule
  stroke(0.290, 0.486, 0.349, 0.45);
  strokeWeight(1.0);
  rect(-span * 0.45, -span * 0.45, span * 0.90, span * 0.90);

  // Foundational slate horizontal plinth at base
  fill(0.169, 0.176, 0.259);
  noStroke();
  rect(-span * 0.42, span * 0.32, span * 0.84, span * 0.12);

  // Stepped pediment block in sage green
  fill(0.290, 0.486, 0.349);
  rect(-span * 0.38, span * 0.26, span * 0.76, span * 0.06);

  // Terracotta central archway portal
  fill(0.831, 0.639, 0.451, terracotta_opacity);
  const wArch = span * arch_width;
  const hArch = span * 0.65;
  const yArch = -span * 0.38;

  beginShape();
  vertex(-wArch * 0.5, yArch + hArch);
  vertex(-wArch * 0.5, yArch + wArch * 0.5);
  // Rounded arch dome top
  for (let a = Math.PI; a <= Math.PI * 2.0; a += 0.1) {
    const rx = (wArch * 0.5) * cos(a);
    const ry = (wArch * 0.5) * sin(a);
    vertex(rx, yArch + wArch * 0.5 + ry);
  }
  vertex(wArch * 0.5, yArch + hArch);
  endShape();

  // Midnight slate inner portal void
  fill(0.169, 0.176, 0.259);
  const wInner = wArch * 0.74;
  const hInner = hArch * 0.84;
  const yInner = yArch + (hArch - hInner);

  beginShape();
  vertex(-wInner * 0.5, yInner + hInner);
  vertex(-wInner * 0.5, yInner + wInner * 0.5);
  for (let a = Math.PI; a <= Math.PI * 2.0; a += 0.1) {
    const rx = (wInner * 0.5) * cos(a);
    const ry = (wInner * 0.5) * sin(a);
    vertex(rx, yInner + wInner * 0.5 + ry);
  }
  vertex(wInner * 0.5, yInner + hInner);
  endShape();

  // Floating celestial sphere inside archway
  push();
  translate(0, -span * 0.08);

  // Concentric orbital rings in sage green and cream
  const rings = floor(orbital_rings);
  for (let r = 1; r <= rings; r++) {
    const rad = (span * 0.18 * r) / rings;
    stroke(r % 2 === 0 ? 0.290 : 0.831, r % 2 === 0 ? 0.486 : 0.639, r % 2 === 0 ? 0.349 : 0.451, 0.75);
    strokeWeight(1.2);
    noFill();
    ellipse(0, 0, rad * 2.0, rad * 1.1);
  }

  // Central glowing terracotta moon/sun disc
  fill(0.831, 0.639, 0.451);
  noStroke();
  circle(0, 0, span * 0.16);

  // Crescent shadow in midnight slate
  fill(0.169, 0.176, 0.259);
  circle(span * 0.035, -span * 0.02, span * 0.13);

  // Astrological radial ticks
  stroke(0.996, 0.980, 0.878, 0.85);
  strokeWeight(1.4);
  for (let i = 0; i < 16; i++) {
    const ang = (i * 6.2831853) / 16;
    const r1 = span * 0.09;
    const r2 = span * 0.11;
    line(cos(ang) * r1, sin(ang) * r1, cos(ang) * r2, sin(ang) * r2);
  }
  pop();

  // Flanking sage green botanical sprig silhouettes
  stroke(0.290, 0.486, 0.349);
  strokeWeight(2.0);
  noFill();

  // Left botanical branch
  push();
  translate(-span * 0.36, span * 0.02);
  line(0, span * 0.22, 0, -span * 0.22);
  for (let b = -4; b <= 4; b++) {
    const by = b * (span * 0.045);
    const bl = span * 0.05 * (1.0 - abs(b) * 0.15);
    line(0, by, -bl, by - span * 0.02);
    fill(0.290, 0.486, 0.349);
    noStroke();
    ellipse(-bl - 4, by - span * 0.02, 7, 12);
    stroke(0.290, 0.486, 0.349);
    strokeWeight(2.0);
    noFill();
  }
  pop();

  // Right botanical branch
  push();
  translate(span * 0.36, span * 0.02);
  line(0, span * 0.22, 0, -span * 0.22);
  for (let b = -4; b <= 4; b++) {
    const by = b * (span * 0.045);
    const bl = span * 0.05 * (1.0 - abs(b) * 0.15);
    line(0, by, bl, by - span * 0.02);
    fill(0.290, 0.486, 0.349);
    noStroke();
    ellipse(bl + 4, by - span * 0.02, 7, 12);
    stroke(0.290, 0.486, 0.349);
    strokeWeight(2.0);
    noFill();
  }
  pop();

  // Corner catalog registration brackets
  stroke(0.169, 0.176, 0.259);
  strokeWeight(2.0);
  noFill();
  const cOff = span * 0.44;
  const cLen = 16;
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
    },
    {
        "slug": "retro-surreal-botanical-monolith",
        "title": "Retro-Surreal Botanical Monolith",
        "description": "Constructed collage featuring floating slate monolith slabs, botanical specimen silhouettes in muted sage, and terracotta astrological disc alignments.",
        "tags": ["retro", "surreal", "botanical", "monolith", "collage", "geometric"],
        "code": """// Retro-Surreal Botanical Monolith - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Constructed collage featuring floating slate monolith slabs, botanical specimen silhouettes in muted sage, and terracotta astrological disc alignments.
param("zoom", 1.0, 0.5, 2.0);
param("monolith_width", 0.32, 0.15, 0.6);
param("sage_overlay", 0.88, 0.3, 1.0);
param("terracotta_split", 0.5, 0.2, 0.8);

function draw(t) {
  // Midnight slate background
  background(0.169, 0.176, 0.259);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88 * zoom;

  push();
  translate(cx, cy);

  // Primary aged cream kraft backing paper sheet
  fill(0.996, 0.980, 0.878);
  noStroke();
  rect(-span * 0.46, -span * 0.46, span * 0.92, span * 0.92);

  // Border outline rule
  stroke(0.169, 0.176, 0.259);
  strokeWeight(2.0);
  noFill();
  rect(-span * 0.46, -span * 0.46, span * 0.92, span * 0.92);

  // Terracotta geometric field block on top half
  fill(0.831, 0.639, 0.451);
  noStroke();
  rect(-span * 0.46, -span * 0.46, span * 0.92, span * 0.38);

  // Sage green horizontal horizon band
  fill(0.290, 0.486, 0.349, sage_overlay);
  rect(-span * 0.46, -span * 0.08, span * 0.92, span * 0.18);

  // Central floating monolith column in midnight slate
  const mW = span * monolith_width;
  fill(0.169, 0.176, 0.259);
  rect(-mW * 0.5, -span * 0.36, mW, span * 0.72);

  // Split terracotta and cream geometric inlays within the monolith
  fill(0.831, 0.639, 0.451);
  rect(-mW * 0.4, -span * 0.30, mW * 0.8, span * 0.16);
  fill(0.996, 0.980, 0.878);
  rect(-mW * 0.4, -span * 0.08, mW * 0.8, span * 0.04);
  rect(-mW * 0.4, span * 0.02, mW * 0.8, span * 0.18);

  // Astrological alignment disc hovering at top
  push();
  translate(0, -span * 0.22);
  stroke(0.996, 0.980, 0.878);
  strokeWeight(2.2);
  noFill();
  circle(0, 0, span * 0.24);

  // Concentric ring and radial spokes
  stroke(0.169, 0.176, 0.259);
  strokeWeight(1.4);
  circle(0, 0, span * 0.16);

  stroke(0.831, 0.639, 0.451);
  strokeWeight(1.2);
  for (let i = 0; i < 12; i++) {
    const a = (i * 6.2831853) / 12;
    line(cos(a) * span * 0.08, sin(a) * span * 0.08, cos(a) * span * 0.12, sin(a) * span * 0.12);
  }
  pop();

  // Botanical specimen fronds overlapping the monolith base
  stroke(0.290, 0.486, 0.349);
  strokeWeight(2.2);
  noFill();

  // Left curved botanical frond
  beginShape();
  for (let s = 0; s <= 20; s++) {
    const f = s / 20.0;
    const bx = -mW * 0.3 - sin(f * 2.4) * span * 0.12;
    const by = span * 0.30 - f * span * 0.36;
    vertex(bx, by);
  }
  endShape();

  // Leaf sprays along left frond
  for (let k = 0; k < 6; k++) {
    const f = (k + 1) / 7.0;
    const ly = span * 0.30 - f * span * 0.36;
    const lx = -mW * 0.3 - sin(f * 2.4) * span * 0.12;
    fill(0.290, 0.486, 0.349);
    noStroke();
    ellipse(lx - span * 0.035, ly - span * 0.015, span * 0.04, span * 0.018);
    stroke(0.290, 0.486, 0.349);
    strokeWeight(1.5);
    noFill();
    line(lx, ly, lx - span * 0.035, ly - span * 0.015);
  }

  // Right curved botanical frond
  stroke(0.290, 0.486, 0.349);
  strokeWeight(2.2);
  noFill();
  beginShape();
  for (let s = 0; s <= 20; s++) {
    const f = s / 20.0;
    const bx = mW * 0.3 + sin(f * 2.4) * span * 0.12;
    const by = span * 0.30 - f * span * 0.36;
    vertex(bx, by);
  }
  endShape();

  // Leaf sprays along right frond
  for (let k = 0; k < 6; k++) {
    const f = (k + 1) / 7.0;
    const ly = span * 0.30 - f * span * 0.36;
    const lx = mW * 0.3 + sin(f * 2.4) * span * 0.12;
    fill(0.290, 0.486, 0.349);
    noStroke();
    ellipse(lx + span * 0.035, ly - span * 0.015, span * 0.04, span * 0.018);
    stroke(0.290, 0.486, 0.349);
    strokeWeight(1.5);
    noFill();
    line(lx, ly, lx + span * 0.035, ly - span * 0.015);
  }

  // Archival stamps and dimension guidelines
  stroke(0.169, 0.176, 0.259);
  strokeWeight(1.2);
  line(-span * 0.42, span * 0.38, span * 0.42, span * 0.38);
  for (let x = -span * 0.40; x <= span * 0.40; x += span * 0.05) {
    line(x, span * 0.37, x, span * 0.39);
  }

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: liminal_void)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "liminal-void-floating-monolith-3d",
        "title": "Liminal Void Floating Monolith 3D",
        "description": "Atmospheric liminal anime landscape featuring floating geometric monolith blocks above an abyssal navy horizon plane with crimson dusk gradients.",
        "tags": ["liminal", "void", "anime", "monolith", "atmosphere", "3d"],
        "code": """// Liminal Void Floating Monolith 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Atmospheric liminal anime landscape featuring floating geometric monolith blocks above an abyssal navy horizon plane with crimson dusk gradients.
param("monolith_scale", 1.0, 0.5, 2.0);
param("float_height", 0.35, 0.1, 0.8);
param("rotation_speed", 0.4, 0.05, 1.2);
param("terrace_levels", 5.0, 3.0, 8.0);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.42 + sin(time * 0.2) * 0.06);
  rotateY(time * 0.3 + 0.6);

  // Stepped foundation floor terraces in abyssal navy and crimson
  const levels = floor(terrace_levels);
  for (let l = 0; l < levels; l++) {
    const fl = l / levels;
    const w = (2.2 - fl * 0.7) * monolith_scale;
    const y = -0.75 + l * 0.05;

    push();
    translate(0, y, 0);
    if (l % 2 === 0) {
      fill(0.000, 0.188, 0.286); // Abyssal navy
    } else {
      fill(0.839, 0.157, 0.157); // Crimson sunset
    }
    box(w, 0.045, w);
    pop();
  }

  // Floating central rectangular monolith
  push();
  const hoverY = 0.05 + sin(time * 0.8) * float_height * 0.2;
  translate(0, hoverY, 0);
  rotateY(time * 0.5);

  // Main monolith block in amber twilight
  fill(0.969, 0.498, 0.000);
  box(0.38 * monolith_scale, 1.35 * monolith_scale, 0.38 * monolith_scale);

  // Crimson core accent stripe
  translate(0, 0, 0);
  fill(0.839, 0.157, 0.157);
  box(0.42 * monolith_scale, 0.18 * monolith_scale, 0.42 * monolith_scale);

  // Pale warm haze top cap
  translate(0, 0.72 * monolith_scale, 0);
  fill(0.918, 0.886, 0.718);
  box(0.32 * monolith_scale, 0.10 * monolith_scale, 0.32 * monolith_scale);
  pop();

  // Flanking satellite monolith shards hovering in orbit
  const shards = 4;
  for (let s = 0; s < shards; s++) {
    const ang = (s * 6.2831853) / shards + time * 0.35;
    const dist = 0.95 * monolith_scale;
    const sx = cos(ang) * dist;
    const sz = sin(ang) * dist;
    const sy = -0.15 + sin(time * 1.1 + s * 1.5) * 0.14;

    push();
    translate(sx, sy, sz);
    rotateY(ang + 0.8);
    if (s % 2 === 0) {
      fill(0.839, 0.157, 0.157); // Crimson
      box(0.14 * monolith_scale, 0.75 * monolith_scale, 0.14 * monolith_scale);
    } else {
      fill(0.918, 0.886, 0.718); // Pale haze
      box(0.12 * monolith_scale, 0.55 * monolith_scale, 0.12 * monolith_scale);
    }
    pop();
  }

  // Outer orbital halo ring
  push();
  translate(0, 0.1, 0);
  rotateX(1.5707963);
  rotateZ(time * 0.4);
  fill(0.969, 0.498, 0.000);
  torus(1.15 * monolith_scale, 0.025);
  pop();

  pop();
}
"""
    },
    {
        "slug": "liminal-void-dusk-colonnade-3d",
        "title": "Liminal Void Dusk Colonnade 3D",
        "description": "Minimalist infinite architectural colonnade receding into a crimson twilight sky, featuring warm amber structural pillars and abyssal foundation terraces.",
        "tags": ["liminal", "void", "colonnade", "architecture", "dusk", "3d"],
        "code": """// Liminal Void Dusk Colonnade 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Minimalist infinite architectural colonnade receding into a crimson twilight sky, featuring warm amber structural pillars and abyssal foundation terraces.
param("colonnade_span", 1.0, 0.5, 1.8);
param("pillar_height", 1.2, 0.6, 2.0);
param("travel_speed", 0.35, 0.05, 1.0);
param("arch_spacing", 0.55, 0.3, 0.9);

function draw(t) {
  push();
  const time = t * travel_speed;

  rotateX(0.32 + sin(time * 0.15) * 0.04);
  rotateY(sin(time * 0.25) * 0.22);

  // Deep floor foundation platform in abyssal navy
  fill(0.000, 0.188, 0.286);
  push();
  translate(0, -0.72, 0);
  box(2.4 * colonnade_span, 0.08, 3.4 * colonnade_span);

  // Central runway pathway in crimson sunset
  fill(0.839, 0.157, 0.157);
  translate(0, 0.05, 0);
  box(0.68 * colonnade_span, 0.04, 3.4 * colonnade_span);

  // Stepped pathway center line in pale warm haze
  fill(0.918, 0.886, 0.718);
  translate(0, 0.03, 0);
  box(0.12 * colonnade_span, 0.03, 3.4 * colonnade_span);
  pop();

  // Colonnade pillars receding along Z-axis
  const count = 7;
  const colW = 0.75 * colonnade_span;
  const pH = pillar_height;

  for (let i = -count; i <= count; i++) {
    const z = i * arch_spacing;

    // Left pillar in amber twilight
    push();
    translate(-colW, -0.68 + pH * 0.5, z);
    fill(0.969, 0.498, 0.000);
    box(0.10 * colonnade_span, pH, 0.10 * colonnade_span);

    // Pillar capital top block in crimson
    translate(0, pH * 0.5 + 0.04, 0);
    fill(0.839, 0.157, 0.157);
    box(0.16 * colonnade_span, 0.08, 0.16 * colonnade_span);
    pop();

    // Right pillar in amber twilight
    push();
    translate(colW, -0.68 + pH * 0.5, z);
    fill(0.969, 0.498, 0.000);
    box(0.10 * colonnade_span, pH, 0.10 * colonnade_span);

    // Pillar capital top block in crimson
    translate(0, pH * 0.5 + 0.04, 0);
    fill(0.839, 0.157, 0.157);
    box(0.16 * colonnade_span, 0.08, 0.16 * colonnade_span);
    pop();

    // Connecting architrave lintel overhead
    push();
    translate(0, -0.68 + pH + 0.08, z);
    fill(0.918, 0.886, 0.718); // Pale warm haze
    box(colW * 2.1, 0.08, 0.12 * colonnade_span);
    pop();
  }

  // Floating focal orb at the far perspective horizon
  push();
  translate(0, -0.15, -1.8);
  fill(0.969, 0.498, 0.000);
  sphere(0.26 * colonnade_span);

  // Pale aura halo around beacon
  fill(0.918, 0.886, 0.718);
  torus(0.42 * colonnade_span, 0.02);
  pop();

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: breakbeat)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "breakbeat-hardcore-sub-roller-synth",
        "title": "Breakbeat Hardcore Sub Roller Synth",
        "description": "Heavy syncopated breakbeat sub-roller synthesizer with dual detuned saw-pulse oscillators, driven lowpass filter punch, and warm saturation.",
        "tags": ["synth", "breakbeat", "hardcore", "sub", "roller", "dsp"],
        "note_root": 36,
        "params": {
            "detune": 0.38,
            "cutoff": 1800.0,
            "resonance": 0.35,
            "decay": 0.32,
            "subLevel": 0.65
        },
        "code": """param float detune = 0.38 [0.05, 1.2]
param float cutoff = 1800.0 [200.0, 5000.0]
param float resonance = 0.35 [0.05, 0.85]
param float decay = 0.32 [0.08, 1.5]
param float subLevel = 0.65 [0.0, 1.2]
state float p1 = 0
state float p2 = 0
state float pSub = 0
state float env = 0
state float pEnv = 0
state float lp1 = 0
state float lp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decay), 0.00008, 0.02)
env = if(target > env, env + (target - env) * 0.42, env - env * decayRate)
pEnv = if(target > pEnv, pEnv + (target - pEnv) * 0.60, pEnv - pEnv * 0.005)

fBase = clamp(freq, 30.0, 1000.0)
f1 = fBase * (1.0 + detune * 0.012)
f2 = fBase * (1.0 - detune * 0.012)

p1 = (p1 + f1 / sr) % 1.0
saw1 = p1 * 2.0 - 1.0

p2 = (p2 + f2 / sr) % 1.0
pulse = if(p2 > 0.45, 1.0, -1.0)

pSub = (pSub + (fBase * 0.5) / sr) % 1.0
subSine = sin(pSub * 6.2831853)

raw = (saw1 * 0.45 + pulse * 0.40 + subSine * subLevel) * env

u = raw * 1.5
sat = clamp(u - 0.22 * u * u * u, -0.95, 0.95)

fCut = clamp((cutoff + pEnv * 1800.0) / sr * 2.2, 0.01, 0.45)
lp1 = lp1 + fCut * (sat - lp1 - resonance * lp2)
lp2 = lp2 + fCut * (lp1 - lp2)

out = lp2 * 0.38
"""
    },
    {
        "slug": "breakbeat-hoover-rave-stab-synth",
        "title": "Breakbeat Hoover Rave Stab Synth",
        "description": "Classic 90s rave hoover stab synthesizer with pitch-swept multi-pulse chorus width, biting resonant filter decay, and aggressive distortion.",
        "tags": ["synth", "breakbeat", "hoover", "rave", "stab", "dsp"],
        "note_root": 38,
        "params": {
            "detuneSpread": 0.65,
            "cutoff": 2400.0,
            "pitchDrop": 0.45,
            "decay": 0.28,
            "drive": 1.4
        },
        "code": """param float detuneSpread = 0.65 [0.1, 1.5]
param float cutoff = 2400.0 [300.0, 6000.0]
param float pitchDrop = 0.45 [0.0, 1.0]
param float decay = 0.28 [0.08, 1.2]
param float drive = 1.4 [0.5, 3.0]
state float p1 = 0
state float p2 = 0
state float env = 0
state float pEnv = 0
state float lp1 = 0
state float lp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decay), 0.0001, 0.02)
env = if(target > env, env + (target - env) * 0.45, env - env * decayRate)
pEnv = if(target > pEnv, pEnv + (target - pEnv) * 0.65, pEnv - pEnv * 0.006)

fBase = clamp(freq * (1.0 + pEnv * pitchDrop * 0.5), 40.0, 1600.0)
d = detuneSpread * 0.018
f1 = fBase * (1.0 + d)
f2 = fBase * (1.0 - d)

p1 = (p1 + f1 / sr) % 1.0
saw1 = p1 * 2.0 - 1.0

p2 = (p2 + f2 / sr) % 1.0
saw2 = p2 * 2.0 - 1.0

raw = (saw1 * 0.5 + saw2 * 0.5) * env

u = raw * drive
sat = clamp(u - 0.20 * u * u * u, -0.94, 0.94)

fCut = clamp((cutoff + pEnv * 2000.0) / sr * 2.2, 0.015, 0.45)
lp1 = lp1 + fCut * (sat - lp1 - 0.38 * lp2)
lp2 = lp2 + fCut * (lp1 - lp2)

out = lp2 * 0.36
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: dark_ambient)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "dark-ambient-abyssal-drone-reverb-fx",
        "title": "Dark Ambient Abyssal Drone Reverb FX",
        "description": "Deep subterranean cavern reverberator with endless low-frequency resonance, pitch dispersion feedback, and dense spectral decay diffusion.",
        "tags": ["effect", "ambient", "reverb", "drone", "abyssal", "audio"],
        "params": {
            "decayTime": 0.75,
            "damping": 2200.0,
            "diffusion": 0.55,
            "mix": 0.65
        },
        "code": """param float decayTime = 0.75 [0.1, 0.95]
param float damping = 2200.0 [500.0, 6000.0]
param float diffusion = 0.55 [0.1, 0.85]
param float mix = 0.65 [0.0, 1.0]
state float d1 = 0
state float d2 = 0
state float d3 = 0
state float d4 = 0
state float lpDamp = 0

fDamp = clamp(damping / sr * 2.2, 0.01, 0.4)
lpDamp = lpDamp + fDamp * (in - lpDamp)

fb = clamp(decayTime, 0.1, 0.88)
d1 = d1 + 0.14 * (lpDamp + d4 * fb - d1)
d2 = d2 + 0.18 * (d1 * diffusion + d3 * fb * 0.7 - d2)
d3 = d3 + 0.11 * (d2 * diffusion + d1 * fb * 0.6 - d3)
d4 = d4 + 0.09 * (d3 * diffusion - d4)

wet = (d1 * 0.3 + d2 * 0.3 + d3 * 0.25 + d4 * 0.25) * 1.4
u = wet * 1.3
sat = clamp(u - 0.18 * u * u * u, -0.92, 0.92)

out = (in * (1.0 - mix) + sat * mix) * 1.05
"""
    },
    {
        "slug": "dark-ambient-spectral-filter-fx",
        "title": "Dark Ambient Spectral Filter FX",
        "description": "Atmospheric multi-pole morphing lowpass and notch filter with slow drifting LFO modulation, sub-harmonic exciter, and soft tape clipping.",
        "tags": ["effect", "ambient", "filter", "spectral", "drone", "audio"],
        "params": {
            "lfoRate": 0.25,
            "centerFreq": 1400.0,
            "resonance": 0.45,
            "mix": 0.70
        },
        "code": """param float lfoRate = 0.25 [0.05, 1.5]
param float centerFreq = 1400.0 [250.0, 4500.0]
param float resonance = 0.45 [0.05, 0.85]
param float mix = 0.70 [0.0, 1.0]
state float lfo = 0
state float lp1 = 0
state float lp2 = 0
state float bp1 = 0
state float subTone = 0

lfo = (lfo + lfoRate / sr) % 1.0
wob = sin(lfo * 6.2831853) * 0.5 + 0.5

fCut = clamp((centerFreq * (0.6 + wob * 0.8)) / sr * 2.2, 0.01, 0.45)
lp1 = lp1 + fCut * (in - lp1 - resonance * bp1)
bp1 = bp1 + fCut * (lp1 - bp1)
lp2 = lp2 + fCut * (bp1 - lp2)

fSub = clamp(90.0 / sr * 2.2, 0.005, 0.1)
subTone = subTone + fSub * (in - subTone)

wet = (lp2 * 0.85 + bp1 * 0.65 + subTone * 0.4) * 1.35
u = wet * 1.2
sat = clamp(u - 0.18 * u * u * u, -0.92, 0.92)

out = (in * (1.0 - mix) + sat * mix) * 1.06
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embeds (Theme: y2k_acid)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "y2k-acid-chrome-starburst",
        "title": "Y2K Acid Chrome Starburst",
        "description": "Y2K liquid chrome shader with multi-point specular starburst rays, iridescent chromatic fringes, and audio-reactive radial warp.",
        "embed_desc": "Y2K cyber-acid liquid chrome shader with multi-point specular starburst rays, iridescent chromatic dispersion, and audio-reactive radial warp. Bass pulses the molten chrome core, mids rotate the iridescent starburst rays, and highs ignite electric cyan specular highlights.",
        "tags": ["pixel", "y2k", "acid", "chrome", "starburst", "shader"],
        "params": {
            "starburstPoints": 8.0,
            "chromeGloss": 1.4,
            "chromaticSpread": 0.35,
            "warpSpeed": 0.8,
            "rimGlow": 1.2
        },
        "code": """param float starburstPoints = 8.0 [4.0, 16.0]
param float chromeGloss = 1.4 [0.5, 3.0]
param float chromaticSpread = 0.35 [0.05, 1.0]
param float warpSpeed = 0.8 [0.1, 2.5]
param float rimGlow = 1.2 [0.2, 2.5]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2
time = t * warpSpeed

r = length(p)
ang = atan2(p.y, p.x)

warp = sin(r * 12.0 - time * 2.0) * 0.06
angWarp = ang + warp

rays = abs(cos(angWarp * starburstPoints * 0.5 + time * 0.5))
star = pow(rays, 6.0) * smoothstep(0.9, 0.1, r)

core = smoothstep(0.55, 0.15, r + warp * 0.5)
rim = smoothstep(0.04, 0.005, abs(r - 0.52))

spec = pow(max(0.0, 1.0 - abs(p.x * 0.7 + p.y * 0.7)), 8.0) * chromeGloss

cDark = vec3(0.067, 0.067, 0.133)
cMagenta = vec3(1.0, 0.0, 0.498)
cCyan = vec3(0.0, 1.0, 1.0)
cChrome = vec3(0.878, 0.878, 0.878)

irid = sin(vec3(0.0, 2.0, 4.0) + angWarp * 2.0 + time) * 0.5 + 0.5

col = mix(cDark, cMagenta, star * 0.75)
col = mix(col, cCyan, rim * rimGlow + irid * chromaticSpread * star)
col = mix(col, cChrome, (core * 0.65 + spec * 0.8))

out = clamp(col, 0.0, 1.0)
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
  float time = t * 0.8;

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.8;
  float h = audio_high * 2.0;

  float r = length(p);
  float ang = atan(p.y, p.x);

  float warp = sin(r * 12.0 - time * 2.0 + b * 4.0) * (0.06 + b * 0.08);
  float angWarp = ang + warp + m * 0.8;

  float rays = abs(cos(angWarp * 4.0 + time * 0.5));
  float star = pow(rays, 6.0) * smoothstep(0.95 + b * 0.2, 0.1, r);

  float coreR = 0.52 + b * 0.15;
  float core = smoothstep(coreR, 0.15, r + warp * 0.5);
  float rim = smoothstep(0.04 + h * 0.03, 0.005, abs(r - coreR));

  float spec = pow(max(0.0, 1.0 - abs(p.x * 0.7 + p.y * 0.7)), 8.0) * (1.4 + h * 1.2);

  vec3 cDark = vec3(0.067, 0.067, 0.133);
  vec3 cMagenta = vec3(1.0, 0.0, 0.498);
  vec3 cCyan = vec3(0.0, 1.0, 1.0);
  vec3 cChrome = vec3(0.878, 0.878, 0.878);

  vec3 irid = sin(vec3(0.0, 2.0, 4.0) + angWarp * 2.0 + time + m * 2.0) * 0.5 + 0.5;

  vec3 col = mix(cDark, cMagenta, star * (0.75 + b * 0.3));
  col = mix(col, cCyan, rim * (1.2 + h * 1.5) + irid * 0.35 * star);
  col = mix(col, cChrome, (core * 0.65 + spec * 0.8));

  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "y2k-acid-liquid-matrix",
        "title": "Y2K Acid Liquid Matrix",
        "description": "Audio-reactive fluid chrome displacement grid with undulating iridescent ripples, acid magenta caustics, and specular metallic contours.",
        "embed_desc": "Fluid molten chrome displacement matrix with undulating iridescent ripples, vivid acid magenta caustics, and audio-reactive specular contours. Bass deepens the chrome fluid displacement, mids accelerate the liquid wave matrix, and highs shimmer metallic cyan reflection highlights.",
        "tags": ["pixel", "y2k", "acid", "liquid", "matrix", "shader"],
        "params": {
            "waveScale": 6.0,
            "waveSpeed": 0.9,
            "metallicGloss": 1.3,
            "causticDepth": 0.8,
            "chromaticShift": 0.38
        },
        "code": """param float waveScale = 6.0 [2.0, 16.0]
param float waveSpeed = 0.9 [0.1, 2.5]
param float metallicGloss = 1.3 [0.4, 2.8]
param float causticDepth = 0.8 [0.1, 2.0]
param float chromaticShift = 0.38 [0.05, 1.0]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0
time = t * waveSpeed

w1 = sin(p.x * waveScale + time) * cos(p.y * waveScale * 0.8 + time * 0.8)
w2 = sin((p.x + p.y) * waveScale * 0.6 - time * 1.2) * 0.5
liquid = w1 + w2

dist = length(p)
caustic = pow(abs(liquid), 3.0) * causticDepth

gx = sin(p.x * 14.0 + liquid * 3.0)
gy = cos(p.y * 14.0 + liquid * 3.0)
grid = (smoothstep(0.85, 0.98, abs(gx)) + smoothstep(0.85, 0.98, abs(gy))) * 0.5

spec = pow(max(0.0, sin(liquid * 4.0 + time)), 6.0) * metallicGloss

cDark = vec3(0.067, 0.067, 0.133)
cMagenta = vec3(1.0, 0.0, 0.498)
cCyan = vec3(0.0, 1.0, 1.0)
cChrome = vec3(0.878, 0.878, 0.878)

irid = sin(vec3(0.0, 2.1, 4.2) + liquid * 3.0 + time) * 0.5 + 0.5

col = mix(cDark, cMagenta, caustic * 0.8)
col = mix(col, cCyan, grid * 0.7 + irid * chromaticShift)
col = mix(col, cChrome, spec * 0.85 + (1.0 - smoothstep(0.0, 1.2, dist)) * 0.25)

out = clamp(col, 0.0, 1.0)
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
  float time = t * 0.9;

  float b = audio_bass * 1.6;
  float m = audio_mid * 1.8;
  float h = audio_high * 2.0;

  float waveScale = 6.0 + b * 2.0;
  float w1 = sin(p.x * waveScale + time + b * 2.5) * cos(p.y * waveScale * 0.8 + time * 0.8);
  float w2 = sin((p.x + p.y) * waveScale * 0.6 - time * 1.2 + m * 3.0) * (0.5 + b * 0.4);
  float liquid = w1 + w2;

  float dist = length(p);
  float caustic = pow(abs(liquid), 3.0) * (0.8 + b * 0.6);

  float gx = sin(p.x * 14.0 + liquid * 3.0);
  float gy = cos(p.y * 14.0 + liquid * 3.0);
  float grid = (smoothstep(0.85, 0.98, abs(gx)) + smoothstep(0.85, 0.98, abs(gy))) * 0.5;

  float spec = pow(max(0.0, sin(liquid * 4.0 + time + h * 4.0)), 6.0) * (1.3 + h * 1.5);

  vec3 cDark = vec3(0.067, 0.067, 0.133);
  vec3 cMagenta = vec3(1.0, 0.0, 0.498);
  vec3 cCyan = vec3(0.0, 1.0, 1.0);
  vec3 cChrome = vec3(0.878, 0.878, 0.878);

  vec3 irid = sin(vec3(0.0, 2.1, 4.2) + liquid * 3.0 + time + m * 2.5) * 0.5 + 0.5;

  vec3 col = mix(cDark, cMagenta, caustic * 0.8);
  col = mix(col, cCyan, grid * (0.7 + h * 0.8) + irid * (0.38 + m * 0.3));
  col = mix(col, cChrome, spec * 0.85 + (1.0 - smoothstep(0.0, 1.2, dist)) * 0.25);

  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
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
    print("=== Creative Expansion Pack 23: Build and Gate Runner ===")
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
  c bg 0.000 0.188 0.286
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
