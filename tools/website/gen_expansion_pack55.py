#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 55.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: karesansui_zen_contours -> karesansui-raked-contours, karesansui-moss-archipelago)
2. Sketch 3D (Theme: brutalist_waffle_coffers -> national-theatre-waffle-vault-3d, regenstein-coffered-atrium-3d)
3. Field Synth (Themes: shakuhachi_bamboo_breath, sho_mouth_organ_cluster -> shakuhachi-bamboo-breath-synth, sho-mouth-organ-cluster-synth)
4. Field Effects (Themes: solenoid_spring_resonator, binaural_haas_room_slit -> solenoid-spring-resonator-fx, binaural-haas-room-slit-fx)
5. Field Pixel & Embeds (Themes: eliasson_sodium_monofrequency, soto_kinetic_penetrable -> eliasson-sodium-fog-orb, soto-kinetic-penetrable-moire)
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
    ALT_BIN = "/Users/namansoni/Infinite/worktrees/main/build/Infinite.app/Contents/MacOS/Infinite"
    if os.path.exists(ALT_BIN):
        BIN = ALT_BIN
    else:
        BIN = os.path.expanduser("~/.local/bin/Infinite")
CMD = [BIN, "--cli"] if BIN.endswith("Infinite") and "Infinite.app" in BIN else [BIN]

LIB_DIR = os.path.join(ROOT, "website", "assets", "library")
SKETCH_DIR = os.path.join(LIB_DIR, "sketch")
PIXEL_DIR = os.path.join(LIB_DIR, "pixel")
PREVIEWS_DIR = os.path.join(LIB_DIR, "previews")
THUMBS_DIR = os.path.join(LIB_DIR, "thumbs")
TEMPLATES_DIR = os.path.join(ROOT, "website", "assets", "templates")
EMBEDS_DIR = os.path.join(ROOT, "website", "embeds")

TMP_DIR = "/tmp/pack55_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview


# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: karesansui_zen_contours)
#    Palette: #D8D4C9 (limestone gravel), #585D2A (moss), #968166 (clay ochre), #232320 (basalt)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "karesansui-raked-contours",
        "title": "Karesansui Raked Contours",
        "description": "Kyoto dry-garden raked limestone gravel lines deflecting smoothly around moss-rimmed basalt islands with fine mineral grain.",
        "tags": ["karesansui", "zen", "contours", "minimal", "procedural", "sketch"],
        "code": """// Karesansui Raked Contours - Infinite Library (Sketch 2D)
param("rake_lines", 38, 18, 64);
param("deflect", 1.15, 0.4, 2.0);
param("grain_count", 600, 100, 1400);
param("drift", 0.12, 0.0, 0.5);

function draw(t) {
  // Warm limestone gravel ground (#D8D4C9)
  background(0.847, 0.831, 0.788);
  const w = width;
  const h = height;
  const side = min(w, h);
  const margin = side * 0.09;
  const x0 = (w - side) * 0.5 + margin;
  const y0 = (h - side) * 0.5 + margin;
  const span = side - margin * 2.0;

  // Weathered clay-ochre garden enclosure border
  stroke(0.588, 0.506, 0.400, 0.65);
  strokeWeight(2.5);
  noFill();
  rect(x0 - 10, y0 - 10, span + 20, span + 20);

  // Three rock islands (normalized in garden frame)
  const rocks = [
    { x: x0 + span * 0.27, y: y0 + span * 0.34, r: span * 0.095 },
    { x: x0 + span * 0.56, y: y0 + span * 0.47, r: span * 0.068 },
    { x: x0 + span * 0.75, y: y0 + span * 0.70, r: span * 0.105 }
  ];

  // Fine mineral gravel stipple
  noStroke();
  for (let i = 0; i < grain_count; i++) {
    const gx = x0 + ((i * 0.6180339887) % 1.0) * span;
    const gy = y0 + ((i * 0.3819660113 + sin(i * 12.7) * 0.03) % 1.0) * span;
    const shade = (i % 3 === 0) ? 0.62 : 0.92;
    fill(shade, shade * 0.98, shade * 0.94, 0.32);
    circle(gx, gy, 1.4);
  }

  // Parallel raked gravel furrows deflecting around islands
  const rows = floor(rake_lines);
  const segs = 96;
  noFill();
  for (let r = 0; r < rows; r++) {
    const baseY = y0 + (r + 0.5) * (span / rows);
    // Shadow groove
    stroke(0.48, 0.46, 0.42, 0.42);
    strokeWeight(1.8);
    beginShape();
    for (let s = 0; s <= segs; s++) {
      const px = x0 + (s / segs) * span;
      let py = baseY + sin(px * 0.008 + t * drift) * 2.5;
      for (let k = 0; k < rocks.length; k++) {
        const dx = px - rocks[k].x;
        const dy = py - rocks[k].y;
        const d2 = dx * dx + dy * dy + 1.0;
        const R2 = (rocks[k].r * 1.45) * (rocks[k].r * 1.45);
        if (d2 < R2 * 4.5) {
          const pushY = (dy >= 0 ? 1 : -1) * (R2 / (d2 + R2 * 0.45)) * rocks[k].r * 0.58 * deflect;
          py += pushY;
        }
      }
      vertex(px, py);
    }
    endShape();

    // Sunlit ridge highlight
    stroke(0.95, 0.94, 0.90, 0.55);
    strokeWeight(1.0);
    beginShape();
    for (let s = 0; s <= segs; s++) {
      const px = x0 + (s / segs) * span;
      let py = baseY - 2.2 + sin(px * 0.008 + t * drift) * 2.5;
      for (let k = 0; k < rocks.length; k++) {
        const dx = px - rocks[k].x;
        const dy = py - rocks[k].y;
        const d2 = dx * dx + dy * dy + 1.0;
        const R2 = (rocks[k].r * 1.45) * (rocks[k].r * 1.45);
        if (d2 < R2 * 4.5) {
          const pushY = (dy >= 0 ? 1 : -1) * (R2 / (d2 + R2 * 0.45)) * rocks[k].r * 0.58 * deflect;
          py += pushY;
        }
      }
      vertex(px, py);
    }
    endShape();
  }

  // Concentric ripple rings, moss halos, and faceted basalt stones
  for (let k = 0; k < rocks.length; k++) {
    const rk = rocks[k];
    noFill();
    for (let ring = 1; ring <= 4; ring++) {
      stroke(0.46, 0.44, 0.40, 0.45 - ring * 0.07);
      strokeWeight(1.5);
      circle(rk.x, rk.y, rk.r * (1.05 + ring * 0.16) * 2.0);
    }
    // Moss halo (#585D2A)
    noStroke();
    fill(0.345, 0.365, 0.165, 0.92);
    beginShape();
    for (let a = 0; a < 24; a++) {
      const ang = (a / 24) * TAU;
      const rad = rk.r * (0.92 + 0.12 * sin(ang * 3.0 + k * 1.7));
      vertex(rk.x + cos(ang) * rad, rk.y + sin(ang) * rad * 0.78);
    }
    endShape();
    // Basalt stone (#232320)
    fill(0.137, 0.137, 0.125, 0.96);
    beginShape();
    for (let a = 0; a < 10; a++) {
      const ang = (a / 10) * TAU + 0.3;
      const rad = rk.r * (0.62 + 0.18 * cos(ang * 2.0 + k * 2.3));
      vertex(rk.x + cos(ang) * rad, rk.y + sin(ang) * rad * 0.72);
    }
    endShape();
  }
}
""".strip()
    },
    {
        "slug": "karesansui-moss-archipelago",
        "title": "Karesansui Moss Archipelago",
        "description": "Asymmetric Kyoto rock-island archipelago with harmonic concentric rake waves, lichen-olive moss beds, and timber frame lines.",
        "tags": ["karesansui", "archipelago", "zen", "waves", "procedural", "sketch"],
        "code": """// Karesansui Moss Archipelago - Infinite Library (Sketch 2D)
param("rings", 16, 8, 28);
param("wave_amp", 4.5, 0.0, 12.0);
param("tempo", 0.15, 0.0, 0.5);
param("margin_pct", 0.08, 0.04, 0.16);

function draw(t) {
  // Pale weathered sand (#DAD5C8)
  background(0.855, 0.835, 0.784);
  const w = width;
  const h = height;
  const side = min(w, h);
  const m = side * margin_pct;
  const span = side - m * 2.0;
  const x0 = (w - side) * 0.5 + m;
  const y0 = (h - side) * 0.5 + m;

  // Timber veranda frame lines
  stroke(0.16, 0.14, 0.12, 0.75);
  strokeWeight(4.0);
  noFill();
  rect(x0, y0, span, span);
  strokeWeight(1.2);
  rect(x0 + 10, y0 + 10, span - 20, span - 20);

  const islands = [
    { x: x0 + span * 0.30, y: y0 + span * 0.32, r: span * 0.085, phase: 0.0 },
    { x: x0 + span * 0.68, y: y0 + span * 0.28, r: span * 0.055, phase: 1.2 },
    { x: x0 + span * 0.52, y: y0 + span * 0.58, r: span * 0.110, phase: 2.4 },
    { x: x0 + span * 0.26, y: y0 + span * 0.74, r: span * 0.062, phase: 3.6 },
    { x: x0 + span * 0.77, y: y0 + span * 0.76, r: span * 0.048, phase: 4.8 }
  ];

  // Subtle parallel background rake
  stroke(0.62, 0.59, 0.53, 0.28);
  strokeWeight(1.0);
  for (let y = y0 + 18; y < y0 + span - 18; y += 10) {
    line(x0 + 14, y, x0 + span - 14, y);
  }

  // Concentric harmonic ripple grooves around each island
  noFill();
  const nRings = floor(rings);
  for (let k = 0; k < islands.length; k++) {
    const isl = islands[k];
    for (let r = 1; r <= nRings; r++) {
      const baseR = isl.r * 1.15 + r * (span * 0.016);
      const alpha = max(0.08, 0.52 - r * 0.024);
      stroke(0.42, 0.40, 0.35, alpha);
      strokeWeight(1.4);
      beginShape();
      for (let s = 0; s <= 48; s++) {
        const a = (s / 48) * TAU;
        const wob = wave_amp * sin(a * 5.0 + t * tempo + isl.phase + r * 0.3);
        const rr = baseR + wob;
        const px = max(x0 + 14, min(x0 + span - 14, isl.x + cos(a) * rr));
        const py = max(y0 + 14, min(y0 + span - 14, isl.y + sin(a) * rr));
        vertex(px, py);
      }
      endShape();
    }
  }

  // Draw moss beds and dark stones on top
  noStroke();
  for (let k = 0; k < islands.length; k++) {
    const isl = islands[k];
    fill(0.35, 0.38, 0.17, 0.94);
    beginShape();
    for (let a = 0; a < 20; a++) {
      const ang = (a / 20) * TAU;
      const rad = isl.r * (0.96 + 0.10 * sin(ang * 4.0 + isl.phase));
      vertex(isl.x + cos(ang) * rad, isl.y + sin(ang) * rad * 0.85);
    }
    endShape();

    fill(0.15, 0.15, 0.14, 0.97);
    beginShape();
    for (let a = 0; a < 9; a++) {
      const ang = (a / 9) * TAU + 0.5;
      const rad = isl.r * (0.58 + 0.16 * cos(ang * 3.0 + k));
      vertex(isl.x + cos(ang) * rad, isl.y + sin(ang) * rad * 0.74);
    }
    endShape();
  }
}
""".strip()
    }
]


# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: brutalist_waffle_coffers)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "national-theatre-waffle-vault-3d",
        "title": "National Theatre Waffle Vault 3D",
        "description": "Cantilevered brutalist concrete waffle-slab ceiling with deep pyramidal coffer recesses, diagonal ribs, and architectural shadow rhythm.",
        "tags": ["brutalist", "waffle-slab", "architecture", "concrete", "3d", "sketch"],
        "code": """// National Theatre Waffle Vault 3D - Infinite Library (Sketch 3D)
param("bays", 5, 3, 7);
param("coffer_depth", 0.32, 0.15, 0.55);
param("sway", 0.12, 0.0, 0.35);

function draw(t) {
  const n = floor(bays);
  const step = 0.62;
  const span = n * step;

  push();
  rotateX(-0.42 + sin(t * 0.25) * sway * 0.3);
  rotateY(0.38 + cos(t * 0.2) * sway);

  // Upper structural concrete deck
  push();
  translate(0, 0.55 + coffer_depth * 0.5, 0);
  fill(0.72, 0.70, 0.66);
  box(span + 0.4, 0.12, span + 0.4);
  pop();

  // Waffle-slab grid ribs and recessed coffer frames
  for (let ix = 0; ix <= n; ix++) {
    const x = (ix - n * 0.5) * step;
    push();
    translate(x, 0.38, 0);
    fill(0.62, 0.60, 0.55);
    box(0.11, coffer_depth, span + 0.12);
    pop();
  }
  for (let iz = 0; iz <= n; iz++) {
    const z = (iz - n * 0.5) * step;
    push();
    translate(0, 0.38, z);
    fill(0.56, 0.54, 0.49);
    box(span + 0.12, coffer_depth, 0.11);
    pop();
  }

  // Inner stepped coffer soffits
  for (let ix = 0; ix < n; ix++) {
    for (let iz = 0; iz < n; iz++) {
      const cx = (ix + 0.5 - n * 0.5) * step;
      const cz = (iz + 0.5 - n * 0.5) * step;
      push();
      translate(cx, 0.48, cz);
      fill(0.32, 0.31, 0.29);
      box(step * 0.74, 0.08, step * 0.74);
      pop();
    }
  }

  // Central brutalist support pier & cantilevered balustrade
  push();
  translate(0, -0.45, 0);
  fill(0.48, 0.46, 0.42);
  box(0.36, 1.5, 0.36);
  pop();

  push();
  translate(0, -0.55, span * 0.35);
  fill(0.24, 0.23, 0.21);
  box(span * 0.85, 0.28, 0.14);
  pop();

  pop();
}
""".strip()
    },
    {
        "slug": "regenstein-coffered-atrium-3d",
        "title": "Regenstein Coffered Atrium 3D",
        "description": "Monumental brutalist coffered concrete atrium bays with suspended linear light-baffle frames and deep rhythmic shadow bays.",
        "tags": ["brutalist", "coffered", "atrium", "light-baffle", "3d", "sketch"],
        "code": """// Regenstein Coffered Atrium 3D - Infinite Library (Sketch 3D)
param("cols", 4, 2, 6);
param("rows", 4, 2, 6);
param("drop", 0.28, 0.12, 0.5);

function draw(t) {
  const nx = floor(cols);
  const nz = floor(rows);
  const cell = 0.72;

  push();
  rotateX(-0.32);
  rotateY(0.45 + sin(t * 0.18) * 0.16);

  // Coffered concrete ceiling slab
  push();
  translate(0, 0.75, 0);
  fill(0.76, 0.73, 0.67);
  box(nx * cell + 0.3, 0.14, nz * cell + 0.3);
  pop();

  for (let ix = 0; ix < nx; ix++) {
    for (let iz = 0; iz < nz; iz++) {
      const cx = (ix + 0.5 - nx * 0.5) * cell;
      const cz = (iz + 0.5 - nz * 0.5) * cell;

      // Deep concrete coffer perimeter ribs
      push();
      translate(cx, 0.52, cz);
      fill(0.64, 0.61, 0.55);
      box(cell * 0.94, 0.30, 0.09);
      box(0.09, 0.30, cell * 0.94);
      pop();

      // Suspended hexagonal/rectangular fluorescent baffle frame
      push();
      translate(cx, 0.52 - drop, cz);
      fill(0.95, 0.94, 0.90);
      box(cell * 0.56, 0.06, 0.07);
      translate(0, 0, cell * 0.22);
      box(cell * 0.56, 0.06, 0.07);
      pop();
    }
  }

  // Brutalist reading-room concrete columns
  for (let side = -1; side <= 1; side += 2) {
    push();
    translate(side * (nx * cell * 0.46), -0.35, -nz * cell * 0.38);
    fill(0.52, 0.49, 0.45);
    box(0.24, 1.9, 0.24);
    pop();
  }

  pop();
}
""".strip()
    }
]


# -------------------------------------------------------------
# 3. Field Synth Sources
#    (shakuhachi_bamboo_breath & sho_mouth_organ_cluster)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "shakuhachi-bamboo-breath-synth",
        "title": "Shakuhachi Bamboo Breath Synth",
        "description": "End-blown bamboo flute model combining deterministic jet-edge breath noise, phase-modulated bore resonance, overblown octave chiff, and soft saturation.",
        "note_root": 62,
        "params": {
            "breath": 0.32,
            "chiff": 0.45,
            "vibratoHz": 4.6,
            "brightHz": 2600.0,
            "decay": 0.9993
        },
        "code": """param float breath = 0.32 [0.05, 0.75];
param float chiff = 0.45 [0.0, 1.0];
param float vibratoHz = 4.6 [0.5, 8.5];
param float brightHz = 2600.0 [700.0, 6500.0];
param float decay = 0.9993 [0.996, 0.9997];
state float p0 = 0; state float vib = 0;
state float env = 0; state float atk = 0;
state float rng = 17325.0; state float bp1 = 0; state float lp1 = 0;

env = if(gate > 0.5, env + (1.0 - env) * 0.06, env * decay);
atk = if(gate > 0.5, atk * 0.996, 1.0);
vib = (vib + vibratoHz / sr) % 1.0;

f0 = clamp(freq * (1.0 + 0.008 * sin(6.2831853 * vib) * env), 80.0, 2600.0);
p0 = (p0 + f0 / sr) % 1.0;

rng = (rng * 16807.0) % 2147483647.0;
nz = (rng / 1073741823.5) - 1.0;
bp1 = bp1 + 0.25 * (nz - bp1);

ph = 6.2831853 * p0;
bore = sin(ph + 0.25 * sin(ph)) * 0.76 + sin(ph * 2.0) * (0.22 + atk * 0.32 * chiff);
raw = (bore + (nz - bp1) * breath * (0.4 + atk * 0.6)) * env;

gLp = clamp(2.0 * sin(3.14159265 * brightHz / sr), 0.05, 0.85);
lp1 = lp1 + gLp * (raw - lp1);

drv = lp1 * 1.35;
out = (drv / (1.0 + abs(drv))) * 0.68;
"""
    },
    {
        "slug": "sho-mouth-organ-cluster-synth",
        "title": "Sho Mouth Organ Cluster Synth",
        "description": "Gagaku free-reed harmonic cluster synthesizer with beating detuned fifths, slow breath-swell envelope, and warm reed-pipe formant filtering.",
        "note_root": 57,
        "params": {
            "detune": 0.0045,
            "fifthMix": 0.58,
            "reedBright": 2400.0,
            "swell": 0.025,
            "decay": 0.9994
        },
        "code": """param float detune = 0.0045 [0.0005, 0.02];
param float fifthMix = 0.58 [0.0, 1.0];
param float reedBright = 2400.0 [600.0, 6000.0];
param float swell = 0.025 [0.005, 0.25];
param float decay = 0.9994 [0.996, 0.9997];
state float p1 = 0; state float p2 = 0; state float p3 = 0; state float p4 = 0;
state float env = 0; state float lp1 = 0; state float lp2 = 0;

env = if(gate > 0.5, env + (1.0 - env) * swell, env * decay);

f0 = clamp(freq, 65.0, 2200.0);
p1 = (p1 + f0 / sr) % 1.0;
p2 = (p2 + (f0 * (1.0 + detune)) / sr) % 1.0;
p3 = (p3 + (f0 * 1.498) / sr) % 1.0;
p4 = (p4 + (f0 * 2.002) / sr) % 1.0;

# Free-reed odd+even harmonic pair
r1 = sin(6.2831853 * p1) + 0.35 * sin(6.2831853 * p1 * 3.0);
r2 = sin(6.2831853 * p2) + 0.30 * sin(6.2831853 * p2 * 2.0);
r3 = sin(6.2831853 * p3) * fifthMix;
r4 = sin(6.2831853 * p4) * 0.42;

cluster = (r1 * 0.36 + r2 * 0.34 + r3 * 0.28 + r4 * 0.24) * env;

g = clamp(2.0 * sin(3.14159265 * reedBright / sr), 0.04, 0.85);
lp1 = lp1 + g * (cluster - lp1);
lp2 = lp2 + g * (lp1 - lp2);

drv = lp2 * 1.4;
out = (drv / (1.0 + abs(drv))) * 0.72;
"""
    }
]


# -------------------------------------------------------------
# 4. Field Effects Sources
#    (solenoid_spring_resonator & binaural_haas_room_slit)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "solenoid-spring-resonator-fx",
        "title": "Solenoid Spring Resonator FX",
        "description": "Electro-mechanical spring-coil dispersion effect with staggered allpass tanks, resonant metallic chime feedback, and gentle saturation.",
        "params": {
            "tension": 0.64,
            "lfoHz": 1.15,
            "feedback": 0.56,
            "dampHz": 3100.0,
            "mix": 0.68
        },
        "code": """param float tension = 0.64 [0.15, 0.92];
param float lfoHz = 1.15 [0.1, 5.0];
param float feedback = 0.56 [0.0, 0.82];
param float dampHz = 3100.0 [700.0, 7500.0];
param float mix = 0.68 [0.0, 1.0];
state float lfo = 0; state float ap1 = 0; state float ap2 = 0; state float ap3 = 0;
state float lp1 = 0; state float fb = 0;

lfo = (lfo + lfoHz / sr) % 1.0;
c1 = clamp(tension + sin(6.2831853 * lfo) * 0.14, 0.08, 0.90);
c2 = clamp(tension * 0.85 - cos(6.2831853 * lfo) * 0.12, 0.08, 0.90);

src = in + fb * feedback;
y1 = -c1 * src + ap1; ap1 = src + c1 * y1;
y2 = -c2 * y1 + ap2; ap2 = y1 + c2 * y2;
y3 = -0.68 * y2 + ap3; ap3 = y2 + 0.68 * y3;

g = clamp(2.0 * sin(3.14159265 * dampHz / sr), 0.04, 0.85);
lp1 = lp1 + g * (y3 - lp1);
drv = lp1 * 1.25;
fb = drv / (1.0 + abs(drv));

out = (in * (1.0 - mix) + fb * mix) * 0.78;
"""
    },
    {
        "slug": "binaural-haas-room-slit-fx",
        "title": "Binaural Haas Room Slit FX",
        "description": "Architectural concrete slit-aperture spatializer using micro-delayed allpass early reflections, air-absorption damping, and warm saturation.",
        "params": {
            "aperture": 0.52,
            "airHz": 4200.0,
            "feedback": 0.44,
            "drive": 1.35,
            "mix": 0.64
        },
        "code": """param float aperture = 0.52 [0.1, 0.88];
param float airHz = 4200.0 [900.0, 8000.0];
param float feedback = 0.44 [0.0, 0.75];
param float drive = 1.35 [0.8, 2.8];
param float mix = 0.64 [0.0, 1.0];
state float ap1 = 0; state float ap2 = 0;
state float lp1 = 0; state float fb = 0;

src = in + fb * feedback;
y1 = -aperture * src + ap1; ap1 = src + aperture * y1;
y2 = -(aperture * 0.72) * y1 + ap2; ap2 = y1 + (aperture * 0.72) * y2;

g = clamp(2.0 * sin(3.14159265 * airHz / sr), 0.05, 0.88);
lp1 = lp1 + g * (y2 - lp1);

drv = lp1 * drive;
sat = drv / (1.0 + abs(drv));
fb = sat;

out = (in * (1.0 - mix) + sat * mix) * 0.40;
"""
    }
]


# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embed Sources
#    (eliasson_sodium_monofrequency & soto_kinetic_penetrable)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "eliasson-sodium-fog-orb",
        "title": "Eliasson Sodium Fog Orb",
        "description": "Monofrequency sodium-vapor atmospheric light chamber with mirrored horizon reflection, drifting volumetric mist, and audio-reactive solar corona.",
        "embed_desc": "Audio-reactive monofrequency sodium-vapor light chamber inspired by Olafur Eliasson.",
        "tags": ["eliasson", "sodium", "atmosphere", "light", "shader", "pixel", "audioreactive"],
        "params": {
            "radius": 0.28,
            "mist_density": 0.65,
            "horizon": 0.36
        },
        "code": """# Eliasson Sodium Fog Orb (Field Pixel)
p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
horizonY = -0.08;
py = abs(p.y - horizonY);
q = vec2(p.x, py);

# Sodium-vapor solar disc
dOrb = length(q - vec2(0.0, 0.14)) - (0.26 + 0.02 * sin(t * 0.7));
disc = smoothstep(0.015, -0.015, dOrb);
corona = exp(-max(0.0, dOrb) * 4.5) * 0.85;

# Volumetric turbine-hall mist & architectural wall ribs
mist = 0.5 + 0.5 * sin(p.x * 5.0 + t * 0.35 + sin(p.y * 8.0 - t * 0.2));
ribs = 0.85 + 0.15 * cos(p.x * 26.0) * smoothstep(0.18, 0.55, abs(p.x));

# Mirror floor attenuation below horizon
mirror = mix(0.68, 1.0, step(horizonY, p.y));
energy = (disc * 1.15 + corona * (0.75 + 0.35 * mist)) * ribs * mirror;

# Monofrequency sodium-amber palette (#120602 -> #E86A00 -> #FFCC00)
cDeep = vec3(0.071, 0.024, 0.008);
cAmber = vec3(0.910, 0.416, 0.0);
cSun = vec3(1.0, 0.85, 0.12);

col = mix(cDeep, cAmber, clamp(energy * 0.9, 0.0, 1.0));
col = mix(col, cSun, clamp(disc * 0.95 + max(0.0, energy - 0.85), 0.0, 1.0));
""",
        "webgl_fs": """precision highp float;
uniform vec2 res;
uniform float t;
uniform float aspect;
uniform float audio_bass;
uniform float audio_mid;
uniform float audio_high;

void main() {
  vec2 uv = gl_FragCoord.xy / res;
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
  float horizonY = -0.08;
  vec2 q = vec2(p.x, abs(p.y - horizonY));

  float rOrb = 0.26 + 0.02 * sin(t * 0.7) + audio_bass * 0.055;
  float dOrb = length(q - vec2(0.0, 0.14)) - rOrb;
  float disc = smoothstep(0.015, -0.015, dOrb);
  float corona = exp(-max(0.0, dOrb) * (4.5 - audio_mid * 1.2)) * (0.85 + audio_high * 0.35);

  float mist = 0.5 + 0.5 * sin(p.x * 5.0 + t * 0.35 + sin(p.y * 8.0 - t * 0.2));
  float ribs = 0.85 + 0.15 * cos(p.x * 26.0) * smoothstep(0.18, 0.55, abs(p.x));
  float mirror = mix(0.68, 1.0, step(horizonY, p.y));

  float energy = (disc * 1.15 + corona * (0.75 + 0.35 * mist)) * ribs * mirror;

  vec3 cDeep = vec3(0.071, 0.024, 0.008);
  vec3 cAmber = vec3(0.910, 0.416, 0.0);
  vec3 cSun = vec3(1.0, 0.85, 0.12);

  vec3 col = mix(cDeep, cAmber, clamp(energy * 0.9, 0.0, 1.0));
  col = mix(col, cSun, clamp(disc * 0.95 + max(0.0, energy - 0.85), 0.0, 1.0));
  gl_FragColor = vec4(col, 1.0);
}"""
    },
    {
        "slug": "soto-kinetic-penetrable-moire",
        "title": "Soto Kinetic Penetrable Moire",
        "description": "Suspended vertical rod array creating optical moire parallax and shimmering cobalt-ivory interference bands driven by kinetic wave phase.",
        "embed_desc": "Audio-reactive kinetic rod-array moire interference inspired by Jesus Rafael Soto.",
        "tags": ["soto", "kinetic", "moire", "op-art", "parallax", "pixel", "audioreactive"],
        "params": {
            "rod_freq": 42.0,
            "parallax": 0.35,
            "speed": 0.65
        },
        "code": """# Soto Kinetic Penetrable Moire (Field Pixel)
p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);

# Foreground & background suspended rod planes
sway1 = sin(t * 0.65 + p.y * 2.2) * 0.03;
sway2 = cos(t * 0.48 - p.y * 1.8) * 0.03;

rods1 = smoothstep(0.35, 0.65, 0.5 + 0.5 * cos((p.x + sway1) * 140.0));
rods2 = smoothstep(0.35, 0.65, 0.5 + 0.5 * cos((p.x - sway2) * 128.0 + p.y * 6.0));

# Kinetic suspended-length envelope
lenMask = smoothstep(0.42, 0.05, abs(p.y + 0.08 * sin(p.x * 7.0 + t * 0.5)));
moire = rods1 * 0.65 + rods2 * 0.45 * (0.4 + 0.6 * lenMask);

# Deep cobalt (#163C6E) & ivory rod highlights (#EEF1FA)
cCobalt = vec3(0.086, 0.235, 0.431);
cShadow = vec3(0.039, 0.078, 0.149);
cIvory = vec3(0.933, 0.945, 0.980);

bg = mix(cShadow, cCobalt, 0.5 + 0.5 * sin(p.y * 3.0 + p.x * 2.0));
col = mix(bg, cIvory, clamp(moire * 0.85, 0.0, 1.0));
""",
        "webgl_fs": """precision highp float;
uniform vec2 res;
uniform float t;
uniform float aspect;
uniform float audio_bass;
uniform float audio_mid;
uniform float audio_high;

void main() {
  vec2 uv = gl_FragCoord.xy / res;
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);

  float sway1 = sin(t * 0.65 + p.y * 2.2) * (0.03 + audio_bass * 0.05);
  float sway2 = cos(t * 0.48 - p.y * 1.8) * (0.03 + audio_mid * 0.04);

  float rods1 = smoothstep(0.35, 0.65, 0.5 + 0.5 * cos((p.x + sway1) * 140.0));
  float rods2 = smoothstep(0.35, 0.65, 0.5 + 0.5 * cos((p.x - sway2) * (128.0 + audio_high * 12.0) + p.y * 6.0));

  float lenMask = smoothstep(0.42, 0.05, abs(p.y + 0.08 * sin(p.x * 7.0 + t * 0.5)));
  float moire = rods1 * 0.65 + rods2 * 0.45 * (0.4 + 0.6 * lenMask);

  vec3 cCobalt = vec3(0.086, 0.235, 0.431);
  vec3 cShadow = vec3(0.039, 0.078, 0.149);
  vec3 cIvory = vec3(0.933, 0.945, 0.980);

  vec3 bg = mix(cShadow, cCobalt, 0.5 + 0.5 * sin(p.y * 3.0 + p.x * 2.0));
  vec3 col = mix(bg, cIvory, clamp(moire * 0.85, 0.0, 1.0));
  gl_FragColor = vec4(col, 1.0);
}"""
    }
]


def make_embed_html(it):
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
  c bg 0.106 0.137 0.169
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
  i note2 {nr+5}
  i note3 {nr+7}
  i note4 {nr+9}
  i note5 {nr+7}
  i note6 {nr+5}
  i note7 {nr+2}
  i note8 {nr}
  i note9 {nr+7}
  i note10 {nr+10}
  i note11 {nr+12}
  i note12 {nr+9}
  i note13 {nr+7}
  i note14 {nr+5}
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
                "domain": "audio"
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
  i note1 52
  i note2 55
  i note3 59
  i note4 60
  i note5 59
  i note6 55
  i note7 52
  i note8 48
  i note9 55
  i note10 59
  i note11 64
  i note12 59
  i note13 55
  i note14 52
  i note15 48
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
  id src
  pos 340 1024
end
node 3 AudioEffects Field Effect
  id fx
  pos 680 1024
  s code {escaped}
end
node 4 Utility Audio Out
  id speakers
  pos 1020 1024
end
node 5 Source Shape
  id pic
  pos 0 162
  c fillColor 0.24 0.30 0.36
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
note src 0 notes
aud fx 0 src
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
                "params": 3,
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
                "tags": ["synth", "shakuhachi", "sho", "bamboo", "reed", "audio", "dsp"],
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
                "tags": ["effect", "spring", "haas", "dispersion", "spatial", "audio", "dsp"],
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
                "params": len(it["params"]),
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
