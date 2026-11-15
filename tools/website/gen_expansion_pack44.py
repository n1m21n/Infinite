#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 44.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: jacquard_loom -> jacquard-loom-warp-weft-draft, jacquard-loom-perforated-tabulature)
2. Sketch 3D (Theme: sashimono_joinery -> sashimono-joinery-kumiko-lattice-3d, sashimono-joinery-kanawa-tsugi-3d)
3. Field Synth (Theme: west_coast_buchla -> west-coast-buchla-complex-wavefold-synth, west-coast-buchla-krell-pendulum-synth)
4. Field Effects (Theme: tape_loop_polyphony -> tape-loop-polyphony-oxide-dispersion-fx, tape-loop-polyphony-frippertronics-echo-fx)
5. Field Pixel & Embeds (Theme: reaction_diffusion_belousov -> reaction-diffusion-belousov-spiral-waves, reaction-diffusion-belousov-turing-coral)
"""
import os, sys, json, subprocess, zipfile
from PIL import Image, ImageStat

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN = os.path.expanduser("~/.local/bin/Infinite")
if not os.path.exists(BIN):
    BIN = os.path.join(ROOT, "build", "Infinite.app", "Contents", "MacOS", "Infinite")
if not os.path.exists(BIN):
    BIN = "/Users/namansoni/Infinite/repo/build/Infinite.app/Contents/MacOS/Infinite"

LIB_DIR = os.path.join(ROOT, "website", "assets", "library")
SKETCH_DIR = os.path.join(LIB_DIR, "sketch")
PIXEL_DIR = os.path.join(LIB_DIR, "pixel")
PREVIEWS_DIR = os.path.join(LIB_DIR, "previews")
THUMBS_DIR = os.path.join(LIB_DIR, "thumbs")
TEMPLATES_DIR = os.path.join(ROOT, "website", "assets", "templates")
EMBEDS_DIR = os.path.join(ROOT, "website", "embeds")

TMP_DIR = "/tmp/pack44_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: jacquard_loom)
#    Palette: #F3EFE6 (linen), #1B2028 (charcoal warp),
#             #B84A32 (madder terracotta), #2E5A68 (indigo shuttle)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "jacquard-loom-warp-weft-draft",
        "title": "Jacquard Loom Warp Weft Draft",
        "description": "Multi-layered twill and satin weaving draft with animated shuttle passes, tie-up matrix, and linen selvedge grain.",
        "tags": ["jacquard", "loom", "weaving", "textile", "draft", "bauhaus", "procedural"],
        "code": """// Jacquard Loom Warp Weft Draft - Infinite Library (Sketch 2D)
param("grid_count", 16, 10, 24);
param("twill_shift", 3, 1, 7);
param("shuttle_speed", 0.55, 0.0, 2.0);
param("thread_gap", 0.14, 0.04, 0.30);

function draw(t) {
  // Unbleached linen ground (#F3EFE6)
  background(0.953, 0.937, 0.902);

  const w = width;
  const h = height;
  const n = floor(grid_count);
  const shift = floor(twill_shift);

  // Subtle linen warp/weft fibre grain
  strokeWeight(1.0);
  for (let i = 0; i < 64; i++) {
    const u = i / 64;
    stroke(0.106, 0.125, 0.157, 0.035);
    line(w * u, h * 0.03, w * u, h * 0.97);
    line(w * 0.03, h * u, w * 0.97, h * u);
  }

  // Outer architectural loom draft frame
  noFill();
  stroke(0.106, 0.125, 0.157, 0.85);
  strokeWeight(2.5);
  rect(w * 0.05, h * 0.05, w * 0.90, h * 0.90);

  // Main drawdown region (left-bottom 68% x 68%)
  const mx = w * 0.09;
  const my = h * 0.24;
  const mw = w * 0.62;
  const mh = h * 0.62;
  const cw = mw / n;
  const ch = mh / n;
  const phase = floor(t * shuttle_speed * 4.0);

  // Drawdown warp/weft interlacing cells
  for (let r = 0; r < n; r++) {
    for (let c = 0; c < n; c++) {
      const x = mx + c * cw;
      const y = my + r * ch;
      const pad = Math.min(cw, ch) * thread_gap;

      // Jacquard twill + satin hybrid lift rule
      const diag1 = (c + r * shift + phase) % 6;
      const diag2 = (c * 2 - r + n * 4) % 5;
      const warpUp = (diag1 < 3) !== (diag2 === 0);

      noStroke();
      if (warpUp) {
        // Warp thread (indigo or charcoal)
        if ((c % 4) === 1) {
          fill(0.180, 0.353, 0.408, 0.94); // #2E5A68 indigo
        } else {
          fill(0.106, 0.125, 0.157, 0.92); // #1B2028 charcoal
        }
        rect(x + pad, y + 1.0, cw - pad * 2.0, ch - 2.0);
        // Subtle yarn highlight rib
        fill(0.953, 0.937, 0.902, 0.18);
        rect(x + cw * 0.42, y + 2.0, cw * 0.14, ch - 4.0);
      } else {
        // Weft thread (madder terracotta or natural linen shadow)
        if ((r % 5) === 2) {
          fill(0.722, 0.290, 0.196, 0.92); // #B84A32 madder terracotta
        } else {
          fill(0.845, 0.815, 0.755, 0.95);
        }
        rect(x + 1.0, y + pad, cw - 2.0, ch - pad * 2.0);
        fill(0.106, 0.125, 0.157, 0.12);
        rect(x + 2.0, y + ch * 0.72, cw - 4.0, ch * 0.12);
      }

      // Fine grid rule
      noFill();
      stroke(0.106, 0.125, 0.157, 0.16);
      strokeWeight(0.8);
      rect(x, y, cw, ch);
    }
  }

  // Top threading draft strip (secondary counter-structure)
  const ty = h * 0.09;
  const th = h * 0.11;
  const shafts = 4;
  const sh = th / shafts;
  for (let c = 0; c < n; c++) {
    const shaftIdx = (c + floor(c / 4)) % shafts;
    const x = mx + c * cw;
    for (let s = 0; s < shafts; s++) {
      noFill();
      stroke(0.106, 0.125, 0.157, 0.28);
      strokeWeight(1.0);
      rect(x, ty + s * sh, cw, sh);
      if (s === shaftIdx) {
        noStroke();
        fill((c % 4) === 1 ? 0.180 : 0.106, (c % 4) === 1 ? 0.353 : 0.125, (c % 4) === 1 ? 0.408 : 0.157, 0.92);
        circle(x + cw * 0.5, ty + s * sh + sh * 0.5, Math.min(cw, sh) * 0.46);
      }
    }
  }

  // Right treadling & tie-up column
  const rx = w * 0.75;
  const rw = w * 0.16;
  const tw = rw / shafts;
  // Tie-up box (top right)
  for (let s = 0; s < shafts; s++) {
    for (let tr = 0; tr < shafts; tr++) {
      noFill();
      stroke(0.106, 0.125, 0.157, 0.45);
      strokeWeight(1.2);
      rect(rx + tr * tw, ty + s * sh, tw, sh);
      if ((s + tr) % 2 === 0 || (s === 0 && tr === 3)) {
        noStroke();
        fill(0.722, 0.290, 0.196, 0.90);
        rect(rx + tr * tw + tw * 0.22, ty + s * sh + sh * 0.22, tw * 0.56, sh * 0.56);
      }
    }
  }
  // Treadling punch strip (bottom right)
  for (let r = 0; r < n; r++) {
    const trIdx = (r * shift + phase) % shafts;
    const y = my + r * ch;
    for (let tr = 0; tr < shafts; tr++) {
      noFill();
      stroke(0.106, 0.125, 0.157, 0.25);
      strokeWeight(1.0);
      rect(rx + tr * tw, y, tw, ch);
      if (tr === trIdx) {
        noStroke();
        fill(0.722, 0.290, 0.196, 0.92);
        circle(rx + tr * tw + tw * 0.5, y + ch * 0.5, Math.min(tw, ch) * 0.48);
      }
    }
  }

  // Selvedge shuttle marker bar
  const activeRow = (phase % n);
  const sy = my + activeRow * ch + ch * 0.5;
  stroke(0.722, 0.290, 0.196, 0.88);
  strokeWeight(2.4);
  line(w * 0.06, sy, mx - w * 0.012, sy);
  noStroke();
  fill(0.722, 0.290, 0.196, 0.95);
  circle(mx - w * 0.012, sy, 8.0);
}
"""
    },
    {
        "slug": "jacquard-loom-perforated-tabulature",
        "title": "Jacquard Loom Perforated Tabulature",
        "description": "Linked binary loom punchcard strips with sprocket guide tracks, terracotta lacing cords, and woven herringbone preview.",
        "tags": ["jacquard", "punchcard", "binary", "textile", "notation", "bauhaus", "procedural"],
        "code": """// Jacquard Loom Perforated Tabulature - Infinite Library (Sketch 2D)
param("card_count", 5, 3, 7);
param("hole_rows", 14, 8, 20);
param("card_phase", 0.45, 0.0, 2.0);
param("lace_tension", 0.35, 0.1, 0.8);

function draw(t) {
  // Unbleached linen paper ground
  background(0.895, 0.875, 0.832);

  const w = width;
  const h = height;
  const nc = floor(card_count);
  const nr = floor(hole_rows);

  // Subtle horizontal textile weave backing lines
  strokeWeight(1.0);
  for (let i = 0; i < 48; i++) {
    const y = h * (0.04 + 0.92 * (i / 48));
    stroke(0.106, 0.125, 0.157, 0.04);
    line(w * 0.04, y, w * 0.96, y);
  }

  // Lower herringbone preview ribbon
  const ribY = h * 0.79;
  const ribH = h * 0.13;
  const ribW = w * 0.84;
  const ribX = w * 0.08;
  noFill();
  stroke(0.106, 0.125, 0.157, 0.8);
  strokeWeight(2.0);
  rect(ribX, ribY, ribW, ribH);

  const cols = 42;
  const rows = 6;
  const cw = ribW / cols;
  const ch = ribH / rows;
  const scroll = floor(t * card_phase * 5.0);
  for (let r = 0; r < rows; r++) {
    for (let c = 0; c < cols; c++) {
      const idx = c + scroll;
      const zig = Math.abs((idx % 8) - 4);
      const on = ((zig + r) % 3) === 0;
      noStroke();
      if (on) {
        fill((c % 6 === 0) ? 0.722 : 0.180, (c % 6 === 0) ? 0.290 : 0.353, (c % 6 === 0) ? 0.196 : 0.408, 0.9);
      } else {
        fill(0.106, 0.125, 0.157, 0.12);
      }
      rect(ribX + c * cw + 1.5, ribY + r * ch + 1.5, cw - 3.0, ch - 3.0);
    }
  }

  // Linked vertical Jacquard punchcards
  const marginX = w * 0.08;
  const totalW = w * 0.84;
  const gap = w * 0.022;
  const cardW = (totalW - gap * (nc - 1)) / nc;
  const cardY = h * 0.08;
  const cardH = h * 0.66;
  const bitCols = 8;

  for (let k = 0; k < nc; k++) {
    const cx = marginX + k * (cardW + gap);

    // Card stock body with subtle shadow
    noStroke();
    fill(0.106, 0.125, 0.157, 0.08);
    rect(cx + 5, cardY + 6, cardW, cardH);

    fill(0.845, 0.815, 0.760, 0.98);
    stroke(0.106, 0.125, 0.157, 0.88);
    strokeWeight(2.0);
    rect(cx, cardY, cardW, cardH);

    // Inner punched matrix frame
    const padX = cardW * 0.16;
    const padY = cardH * 0.07;
    const matW = cardW - padX * 2.0;
    const matH = cardH - padY * 2.0;
    const dx = matW / bitCols;
    const dy = matH / nr;

    // Edge sprocket guide holes
    for (let r = 0; r < nr; r++) {
      const ry = cardY + padY + (r + 0.5) * dy;
      noStroke();
      fill(0.106, 0.125, 0.157, 0.78);
      circle(cx + padX * 0.45, ry, Math.min(dx, dy) * 0.34);
      circle(cx + cardW - padX * 0.45, ry, Math.min(dx, dy) * 0.34);
    }

    // Binary loom-lift punch holes
    for (let r = 0; r < nr; r++) {
      const rowCode = ((r + k * 3 + scroll) * 19 + k * 7) ^ (r * 5);
      for (let b = 0; b < bitCols; b++) {
        const bitOn = ((rowCode >> (b % 6)) & 1) === 1;
        const hx = cx + padX + (b + 0.5) * dx;
        const hy = cardY + padY + (r + 0.5) * dy;
        const rad = Math.min(dx, dy) * 0.36;
        if (bitOn) {
          noStroke();
          fill(0.106, 0.125, 0.157, 0.92);
          circle(hx, hy, rad * 1.8);
        } else {
          noFill();
          stroke(0.180, 0.353, 0.408, 0.35);
          strokeWeight(1.0);
          circle(hx, hy, rad * 1.4);
        }
      }
    }

    // Lacing cord connections to next card
    if (k < nc - 1) {
      const nx = cx + cardW;
      const span = gap;
      stroke(0.722, 0.290, 0.196, 0.92); // #B84A32 madder terracotta cord
      strokeWeight(2.4);
      for (let l = 0; l < 4; l++) {
        const ly = cardY + cardH * (0.14 + l * 0.24);
        const sag = span * lace_tension * 0.45;
        line(nx - padX * 0.3, ly - 6, nx + span * 0.5, ly + sag);
        line(nx + span * 0.5, ly + sag, nx + span + padX * 0.3, ly - 6);
        line(nx - padX * 0.3, ly + 6, nx + span + padX * 0.3, ly + 6);
      }
    }
  }
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: sashimono_joinery)
#    Palette: #EAE3D5 (hinoki), #8C5A32 (cedar), #2B2118 (charred sugi), #C89D66 (brass)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "sashimono-joinery-kumiko-lattice-3d",
        "title": "Sashimono Joinery Kumiko Lattice 3D",
        "description": "Interlocking Japanese Kumiko cypress latticework framed in charred sugi timber with brass corner dowels.",
        "tags": ["sashimono", "kumiko", "joinery", "timber", "3d", "architecture", "procedural"],
        "code": """// Sashimono Joinery Kumiko Lattice 3D - Infinite Library (Sketch 3D)
param("lattice_cells", 4, 3, 6);
param("rib_depth", 0.16, 0.08, 0.28);
param("tilt_angle", 0.44, 0.1, 1.0);
param("spin_speed", 0.22, 0.0, 0.8);

function draw(t) {
  rotateX(tilt_angle);
  rotateY(t * spin_speed + 0.58);

  const nc = floor(lattice_cells);
  const span = 2.2;
  const step = span / nc;
  const half = span * 0.5;

  // Outer mitered charred sugi frame (#2B2118)
  fill(0.169, 0.129, 0.094);
  const fThick = 0.11;
  const fDepth = rib_depth * 1.45;
  push(); translate(0, half, 0); box(span + fThick, fThick, fDepth); pop();
  push(); translate(0, -half, 0); box(span + fThick, fThick, fDepth); pop();
  push(); translate(-half, 0, 0); box(fThick, span - fThick, fDepth); pop();
  push(); translate(half, 0, 0); box(fThick, span - fThick, fDepth); pop();

  // Brass corner joinery dowels (#C89D66)
  fill(0.784, 0.616, 0.400);
  for (let sx = -1; sx <= 1; sx += 2) {
    for (let sy = -1; sy <= 1; sy += 2) {
      push();
      translate(sx * half, sy * half, 0);
      rotateX(1.5708);
      cylinder(0.032, fDepth * 1.18, 14);
      pop();
    }
  }

  // Horizontal and vertical half-lap hinoki cypress ribs
  fill(0.84, 0.72, 0.54);
  for (let i = 1; i < nc; i++) {
    const p = -half + i * step;
    push();
    translate(p, 0, 0.015);
    box(0.035, span - fThick * 0.5, rib_depth);
    pop();

    push();
    translate(0, p, -0.015);
    box(span - fThick * 0.5, 0.035, rib_depth);
    pop();
  }

  // Diagonal asanoha (hemp-leaf) cedar infill ribs (#8C5A32)
  fill(0.549, 0.353, 0.196);
  const diagLen = step * 1.38;
  for (let ix = 0; ix < nc; ix++) {
    for (let iy = 0; iy < nc; iy++) {
      const cx = -half + (ix + 0.5) * step;
      const cy = -half + (iy + 0.5) * step;
      push();
      translate(cx, cy, 0);
      rotateZ(0.7854);
      box(diagLen, 0.024, rib_depth * 0.72);
      pop();

      push();
      translate(cx, cy, 0);
      rotateZ(-0.7854);
      box(diagLen, 0.024, rib_depth * 0.72);
      pop();
    }
  }
}
"""
    },
    {
        "slug": "sashimono-joinery-kanawa-tsugi-3d",
        "title": "Sashimono Joinery Kanawa Tsugi 3D",
        "description": "Exploded Kanawa-tsugi scarf joint timber assembly with stepped mortise hooks and central charred sugi draw-pin wedge.",
        "tags": ["sashimono", "kanawa-tsugi", "joinery", "woodwork", "3d", "architecture", "procedural"],
        "code": """// Sashimono Joinery Kanawa Tsugi 3D - Infinite Library (Sketch 3D)
param("explode_dist", 0.28, 0.02, 0.65);
param("pin_offset", 0.18, 0.0, 0.55);
param("tilt_angle", 0.42, 0.1, 1.0);
param("spin_speed", 0.20, 0.0, 0.8);

function draw(t) {
  rotateX(tilt_angle);
  rotateY(t * spin_speed + 0.72);

  const pulse = 0.5 + 0.5 * Math.sin(t * 0.8);
  const gap = explode_dist * (0.45 + 0.55 * pulse);

  // Left beam: aged cedar (#8C5A32) with stepped scarf tenon
  push();
  translate(-gap * 0.75, 0, 0);
  fill(0.549, 0.353, 0.196);
  // Main beam shank
  push(); translate(-0.85, 0, 0); box(1.10, 0.46, 0.46); pop();
  // Lower scarf tongue
  push(); translate(-0.05, -0.11, 0); box(0.70, 0.22, 0.46); pop();
  // End hook abutment
  push(); translate(0.24, 0.05, 0); box(0.16, 0.34, 0.46); pop();
  // Mortise cheek rails
  push(); translate(-0.18, 0.12, 0.16); box(0.45, 0.20, 0.14); pop();
  push(); translate(-0.18, 0.12, -0.16); box(0.45, 0.20, 0.14); pop();
  pop();

  // Right beam: hinoki cypress (#D6B88A) interlocking partner
  push();
  translate(gap * 0.75, 0, 0);
  fill(0.839, 0.722, 0.541);
  // Main beam shank
  push(); translate(0.85, 0, 0); box(1.10, 0.46, 0.46); pop();
  // Upper scarf tongue
  push(); translate(0.05, 0.11, 0); box(0.70, 0.22, 0.46); pop();
  // End hook abutment
  push(); translate(-0.24, -0.05, 0); box(0.16, 0.34, 0.46); pop();
  // Mortise cheek rails
  push(); translate(0.18, -0.12, 0.16); box(0.45, 0.20, 0.14); pop();
  push(); translate(0.18, -0.12, -0.16); box(0.45, 0.20, 0.14); pop();
  pop();

  // Central charred sugi draw-pin wedge (shachi-sen, #2B2118)
  push();
  translate(0, pin_offset * (0.4 + 0.6 * pulse), 0);
  fill(0.169, 0.129, 0.094);
  box(0.16, 0.82, 0.22);
  // Brass cap trim (#C89D66)
  fill(0.784, 0.616, 0.400);
  translate(0, 0.42, 0);
  box(0.20, 0.05, 0.26);
  pop();

  // Base architectural cradle collar
  push();
  translate(0, -0.58, 0);
  fill(0.169, 0.129, 0.094);
  box(1.65, 0.07, 0.72);
  fill(0.784, 0.616, 0.400);
  translate(-0.72, 0.05, 0);
  box(0.08, 0.08, 0.74);
  translate(1.44, 0, 0);
  box(0.08, 0.08, 0.74);
  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: west_coast_buchla)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "west-coast-buchla-complex-wavefold-synth",
        "title": "West Coast Buchla Complex Wavefold Synth",
        "description": "Dual-oscillator PM/FM complex wavefolder with multi-stage trigonometric folding and vactrol low-pass gate decay.",
        "note_root": 48,
        "params": {
            "fold": 2.6,
            "fmIndex": 1.15,
            "modRatio": 1.5,
            "lpgDecay": 0.55,
            "symmetry": 0.18
        },
        "code": """param float fold = 2.6 [1.0, 5.5];
param float fmIndex = 1.15 [0.0, 3.5];
param float modRatio = 1.5 [0.5, 4.0];
param float lpgDecay = 0.55 [0.12, 1.8];
param float symmetry = 0.18 [0.0, 0.6];
state float pCar = 0;
state float pMod = 0;
state float pSub = 0;
state float env = 0;
state float lpgLp1 = 0;
state float lpgLp2 = 0;

target = if(gate > 0.5, 1.0, 0.0);
decayRate = clamp(1.0 / (sr * lpgDecay), 0.0001, 0.03);
env = if(target > env, env + (target - env) * 0.11, env - env * decayRate);

f0 = clamp(freq, 35.0, 2400.0);
pCar = (pCar + f0 / sr) % 1.0;
pMod = (pMod + (f0 * modRatio) / sr) % 1.0;
pSub = (pSub + (f0 * 0.5) / sr) % 1.0;

# Complex oscillator PM + trigonometric wavefolder
modWave = sin(6.2831853 * pMod) * fmIndex * env;
carWave = sin(6.2831853 * pCar + modWave) + symmetry * env;
foldGain = 1.0 + fold * env;
folded = sin(carWave * foldGain) + 0.35 * sin(carWave * foldGain * 2.0);
body = sin(6.2831853 * pSub) * 0.35 + folded * 0.65;

# Vactrol-inspired coupled low-pass gate
fCut = clamp((180.0 + env * env * 4200.0) / sr * 3.14, 0.01, 0.44);
lpgLp1 = lpgLp1 + fCut * (body - lpgLp1);
lpgLp2 = lpgLp2 + fCut * (lpgLp1 - lpgLp2);

sat = (lpgLp2 * 1.45) / (1.0 + abs(lpgLp2 * 1.45) * 0.42);
out = sat * env * 0.52;
"""
    },
    {
        "slug": "west-coast-buchla-krell-pendulum-synth",
        "title": "West Coast Buchla Krell Pendulum Synth",
        "description": "Self-modulating Krell FM synthesizer with timbre-envelope harmonic sweep, resonant body filter, and warm saturation.",
        "note_root": 53,
        "params": {
            "ratio": 2.0,
            "timbre": 2.2,
            "resonance": 0.42,
            "decay": 0.72,
            "warmth": 1.35
        },
        "code": """param float ratio = 2.0 [0.5, 5.0];
param float timbre = 2.2 [0.2, 4.5];
param float resonance = 0.42 [0.0, 0.78];
param float decay = 0.72 [0.15, 2.0];
param float warmth = 1.35 [0.8, 2.8];
state float p1 = 0;
state float p2 = 0;
state float lfo = 0;
state float env = 0;
state float bp = 0;
state float lp = 0;

target = if(gate > 0.5, 1.0, 0.0);
decayRate = clamp(1.0 / (sr * decay), 0.0001, 0.03);
env = if(target > env, env + (target - env) * 0.085, env - env * decayRate);

lfo = (lfo + 0.45 / sr) % 1.0;
ratioDrift = ratio + sin(6.2831853 * lfo) * 0.12;

f0 = clamp(freq, 40.0, 2600.0);
p1 = (p1 + f0 / sr) % 1.0;
p2 = (p2 + (f0 * ratioDrift) / sr) % 1.0;

# Harmonic FM core + soft odd-order wavefolder
fm = sin(6.2831853 * p2) * timbre * (0.25 + 0.75 * env);
core = sin(6.2831853 * p1 + fm);
folded = sin(core * (1.2 + env * 1.4));

# Resonant state-variable body filter
fCut = clamp((240.0 + env * 2800.0) / sr * 3.14, 0.015, 0.42);
q = 1.0 - resonance * 0.85;
hp = folded - lp - q * bp;
bp = clamp(bp + fCut * hp, -2.5, 2.5);
lp = clamp(lp + fCut * bp, -2.5, 2.5);

mixSig = lp * 0.75 + bp * 0.25;
sat = (mixSig * warmth) / (1.0 + abs(mixSig * warmth) * 0.45);
out = sat * env * 0.50;
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: tape_loop_polyphony)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "tape-loop-polyphony-oxide-dispersion-fx",
        "title": "Tape Loop Polyphony Oxide Dispersion FX",
        "description": "Cascaded allpass phase dispersion and reel-to-reel oxide hysteresis saturator with gentle azimuth flutter.",
        "params": {
            "dispersion": 0.62,
            "oxideDrive": 1.65,
            "flutter": 0.35,
            "tone": 0.68,
            "mix": 0.65
        },
        "code": """param float dispersion = 0.62 [0.0, 0.9];
param float oxideDrive = 1.65 [0.8, 3.5];
param float flutter = 0.35 [0.0, 1.0];
param float tone = 0.68 [0.15, 1.0];
param float mix = 0.65 [0.0, 1.0];
state float lfo = 0;
state float ap1 = 0;
state float ap2 = 0;
state float ap3 = 0;
state float ap4 = 0;
state float lp = 0;

lfo = (lfo + 1.85 / sr) % 1.0;
mod = sin(6.2831853 * lfo) * flutter * 0.14;
g = clamp(dispersion + mod, -0.88, 0.88);

# 4-stage cascaded allpass dispersion network
y1 = -g * in + ap1;
ap1 = in + g * y1;

y2 = -g * y1 + ap2;
ap2 = y1 + g * y2;

y3 = -g * y2 + ap3;
ap3 = y2 + g * y3;

y4 = -g * y3 + ap4;
ap4 = y3 + g * y4;

# Reel-to-reel oxide hysteresis saturation + high-frequency roll-off
driven = y4 * oxideDrive;
sat = driven / (1.0 + abs(driven) * 0.45);
fCut = clamp(0.08 + tone * 0.34, 0.04, 0.44);
lp = lp + fCut * (sat - lp);

out = in * (1.0 - mix) + lp * mix;
"""
    },
    {
        "slug": "tape-loop-polyphony-frippertronics-echo-fx",
        "title": "Tape Loop Polyphony Frippertronics Echo FX",
        "description": "Asynchronous multi-tap tape loop delay with prime-ratio diffusion, oxide damping, and warm feedback saturation.",
        "params": {
            "feedback": 0.62,
            "damping": 0.42,
            "wow": 0.28,
            "drive": 1.35,
            "mix": 0.58
        },
        "code": """param float feedback = 0.62 [0.0, 0.88];
param float damping = 0.42 [0.05, 0.85];
param float wow = 0.28 [0.0, 1.0];
param float drive = 1.35 [0.8, 2.8];
param float mix = 0.58 [0.0, 1.0];
state float lfo = 0;
state float tap1 = 0;
state float tap2 = 0;
state float tap3 = 0;
state float lp = 0;

lfo = (lfo + 0.55 / sr) % 1.0;
drift = 1.0 + sin(6.2831853 * lfo) * wow * 0.12;

fCut = clamp(0.10 + (1.0 - damping) * 0.30, 0.03, 0.42);
lp = lp + fCut * (in - lp);

# Asynchronous multi-tap tape loop
r1 = clamp(0.068 * drift, 0.02, 0.18);
r2 = clamp(0.051 * drift, 0.02, 0.18);
r3 = clamp(0.039 * drift, 0.02, 0.18);

tap1 = tap1 + r1 * (lp + tap3 * feedback * 0.55 - tap1);
tap2 = tap2 + r2 * (tap1 - tap2);
tap3 = tap3 + r3 * (tap2 - tap3);

echo = (tap1 * 0.42 + tap2 * 0.36 + tap3 * 0.28) * drive;
sat = echo / (1.0 + abs(echo) * 0.40);

out = (in * (1.0 - mix) + sat * mix) * 0.94;
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel & Embeds Sources (Theme: reaction_diffusion_belousov)
#    Palette: #0E1A1E (deep slate), #2A7B7B (verdigris),
#             #E8C589 (ochre), #D9594C (cinnabar)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "reaction-diffusion-belousov-spiral-waves",
        "title": "Reaction Diffusion Belousov Spiral Waves",
        "description": "Belousov-Zhabotinsky chemical spiral wave interference with domain-warped fronts in verdigris, ochre, and cinnabar glaze.",
        "embed_desc": "Audio-reactive Belousov-Zhabotinsky chemical wave visualizer. Bass expands the spiral wavefronts, mids advect the domain warp, and highs ignite the cinnabar reaction fronts.",
        "tags": ["reaction-diffusion", "belousov", "spiral", "chemical", "shader", "webgl", "audio-reactive"],
        "params": {
            "waveFreq": 7.5,
            "warpAmt": 0.42,
            "spiralArms": 3.0,
            "sharpness": 2.4,
            "glazeMix": 0.72
        },
        "code": """param float waveFreq = 7.5 [3.0, 16.0];
param float warpAmt = 0.42 [0.05, 1.0];
param float spiralArms = 3.0 [1.0, 6.0];
param float sharpness = 2.4 [1.0, 5.0];
param float glazeMix = 0.72 [0.2, 1.0];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0;

# Mineral glaze palette
cSlate = vec3(0.055, 0.102, 0.118);
cVerdigris = vec3(0.165, 0.482, 0.482);
cOchre = vec3(0.910, 0.773, 0.537);
cCinnabar = vec3(0.851, 0.349, 0.298);

# Domain warp simulating chemical advection
w1 = vec2(sin(p.y * 2.6 + t * 0.4), cos(p.x * 2.4 - t * 0.35));
w2 = vec2(sin((p.x + p.y) * 3.8 - t * 0.28), cos((p.x - p.y) * 3.5 + t * 0.32));
q = p + (w1 * 0.65 + w2 * 0.35) * warpAmt;

# Two coupled BZ spiral centers
cA = q - vec2(-0.34, 0.18);
cB = q - vec2(0.36, -0.22);
rA = length(cA);
rB = length(cB);
aA = atan2(cA.y, cA.x);
aB = atan2(cB.y, cB.x);

phaseA = rA * waveFreq - aA * spiralArms - t * 1.4;
phaseB = rB * (waveFreq * 0.92) + aB * spiralArms - t * 1.15;

uField = 0.5 + 0.5 * sin(phaseA + 0.65 * sin(phaseB));
vField = 0.5 + 0.5 * cos(phaseB - 0.55 * cos(phaseA));

# Reaction front ridge sharpening
front = pow(clamp(uField * (1.0 - vField * 0.45), 0.0, 1.0), sharpness);
crest = smoothstep(0.58, 0.88, uField) * smoothstep(0.35, 0.75, 1.0 - abs(uField - vField));

baseCol = mix(cSlate, cVerdigris, smoothstep(0.15, 0.72, uField) * glazeMix);
baseCol = mix(baseCol, cOchre, front * 0.88);
baseCol = mix(baseCol, cCinnabar, crest * 0.95);

col = clamp(baseCol, 0.0, 1.0);
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

  float b = audio_bass * 0.55;
  float m = audio_mid * 0.55;
  float h = audio_high * 0.55;

  vec3 cSlate = vec3(0.055, 0.102, 0.118);
  vec3 cVerdigris = vec3(0.165, 0.482, 0.482);
  vec3 cOchre = vec3(0.910, 0.773, 0.537);
  vec3 cCinnabar = vec3(0.851, 0.349, 0.298);

  vec2 w1 = vec2(sin(p.y * 2.6 + t * 0.4 + m), cos(p.x * 2.4 - t * 0.35));
  vec2 w2 = vec2(sin((p.x + p.y) * 3.8 - t * 0.28), cos((p.x - p.y) * 3.5 + t * 0.32 + m));
  vec2 q = p + (w1 * 0.65 + w2 * 0.35) * (0.42 + m * 0.35);

  vec2 cA = q - vec2(-0.34, 0.18);
  vec2 cB = q - vec2(0.36, -0.22);
  float rA = length(cA);
  float rB = length(cB);
  float aA = atan(cA.y, cA.x);
  float aB = atan(cB.y, cB.x);

  float freq = 7.5 + b * 2.8;
  float phaseA = rA * freq - aA * 3.0 - t * 1.4;
  float phaseB = rB * (freq * 0.92) + aB * 3.0 - t * 1.15;

  float uField = 0.5 + 0.5 * sin(phaseA + 0.65 * sin(phaseB));
  float vField = 0.5 + 0.5 * cos(phaseB - 0.55 * cos(phaseA));

  float front = pow(clamp(uField * (1.0 - vField * 0.45), 0.0, 1.0), 2.4);
  float crest = smoothstep(0.58, 0.88, uField) * smoothstep(0.35, 0.75, 1.0 - abs(uField - vField));

  vec3 baseCol = mix(cSlate, cVerdigris, smoothstep(0.15, 0.72, uField) * 0.78);
  baseCol = mix(baseCol, cOchre, front * (0.85 + b * 0.25));
  baseCol = mix(baseCol, cCinnabar, crest * (0.88 + h * 0.45));

  gl_FragColor = vec4(clamp(baseCol, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "reaction-diffusion-belousov-turing-coral",
        "title": "Reaction Diffusion Belousov Turing Coral",
        "description": "Anisotropic Turing coral-spot morphogenesis with cellular ridge fronts and mineral verdigris-cinnabar banding.",
        "embed_desc": "Audio-reactive Turing morphogenesis visualizer. Bass swells the coral-spot cells, mids bend the anisotropic lattice, and highs sharpen the cinnabar ridge contours.",
        "tags": ["turing", "reaction-diffusion", "morphogenesis", "coral", "shader", "webgl", "audio-reactive"],
        "params": {
            "cellScale": 4.2,
            "anisotropy": 0.38,
            "ridgeWidth": 0.14,
            "contourFreq": 6.0,
            "warmth": 0.75
        },
        "code": """param float cellScale = 4.2 [2.0, 9.0];
param float anisotropy = 0.38 [0.0, 0.9];
param float ridgeWidth = 0.14 [0.04, 0.35];
param float contourFreq = 6.0 [2.0, 12.0];
param float warmth = 0.75 [0.2, 1.0];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0;

cSlate = vec3(0.055, 0.102, 0.118);
cVerdigris = vec3(0.165, 0.482, 0.482);
cOchre = vec3(0.910, 0.773, 0.537);
cCinnabar = vec3(0.851, 0.349, 0.298);

# Anisotropic Turing domain distortion
warp = vec2(sin(p.y * 3.2 + t * 0.3), cos(p.x * 3.0 - t * 0.25)) * anisotropy;
q = (p + warp) * cellScale;

# Multi-frequency Turing coral interference
t1 = sin(q.x * 1.4 + cos(q.y * 1.3 + t * 0.35));
t2 = sin(q.y * 1.5 - sin(q.x * 1.2 - t * 0.28));
t3 = cos((q.x + q.y) * 1.1 + t * 0.22) * sin((q.x - q.y) * 1.15);
morph = (t1 + t2 + t3) / 3.0;

# Cellular ridge & contour bands
ridge = 1.0 - smoothstep(0.0, ridgeWidth, abs(morph));
spots = smoothstep(0.15, 0.55, morph);
band = 0.5 + 0.5 * sin(morph * contourFreq * 3.14159);

baseCol = mix(cSlate, cVerdigris, spots * 0.85);
baseCol = mix(baseCol, cOchre, band * 0.42 * warmth);
baseCol = mix(baseCol, cCinnabar, ridge * 0.92);

col = clamp(baseCol, 0.0, 1.0);
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

  float b = audio_bass * 0.55;
  float m = audio_mid * 0.55;
  float h = audio_high * 0.55;

  vec3 cSlate = vec3(0.055, 0.102, 0.118);
  vec3 cVerdigris = vec3(0.165, 0.482, 0.482);
  vec3 cOchre = vec3(0.910, 0.773, 0.537);
  vec3 cCinnabar = vec3(0.851, 0.349, 0.298);

  vec2 warp = vec2(sin(p.y * 3.2 + t * 0.3 + m), cos(p.x * 3.0 - t * 0.25)) * (0.38 + m * 0.32);
  vec2 q = (p + warp) * (4.2 - b * 0.65);

  float t1 = sin(q.x * 1.4 + cos(q.y * 1.3 + t * 0.35));
  float t2 = sin(q.y * 1.5 - sin(q.x * 1.2 - t * 0.28));
  float t3 = cos((q.x + q.y) * 1.1 + t * 0.22) * sin((q.x - q.y) * 1.15);
  float morph = (t1 + t2 + t3) / 3.0;

  float ridge = 1.0 - smoothstep(0.0, 0.14 + h * 0.08, abs(morph));
  float spots = smoothstep(0.15, 0.55, morph);
  float band = 0.5 + 0.5 * sin(morph * 6.0 * 3.14159);

  vec3 baseCol = mix(cSlate, cVerdigris, spots * 0.85);
  baseCol = mix(baseCol, cOchre, band * 0.42);
  baseCol = mix(baseCol, cCinnabar, ridge * (0.90 + h * 0.25));

  gl_FragColor = vec4(clamp(baseCol, 0.0, 1.0), 1.0);
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
        let bSum = 0, mSum = 0, hSum = 0;
        for (let i = 0; i < 4; i++) bSum += freqData[i];
        for (let i = 4; i < 16; i++) mSum += freqData[i];
        for (let i = 16; i < 32; i++) hSum += freqData[i];
        const targetB = (bSum / 4) / 255;
        const targetM = (mSum / 12) / 255;
        const targetH = (hSum / 16) / 255;
        bass += (targetB - bass) * 0.25;
        mid += (targetM - mid) * 0.2;
        high += (targetH - high) * 0.2;
      }} else {{
        bass = 0.25 + 0.25 * Math.sin(t * 2.2);
        mid = 0.25 + 0.25 * Math.sin(t * 3.1 + 1.2);
        high = 0.25 + 0.25 * Math.sin(t * 4.5 + 2.4);
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
  c bg 0.9176 0.8902 0.8353
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
  i note1 {nr}
  i note2 {nr+4}
  i note3 {nr+7}
  i note4 {nr}
  i note5 {nr+2}
  i note6 {nr+10}
  i note7 {nr+7}
  i note8 {nr}
  i note9 {nr}
  i note10 {nr+4}
  i note11 {nr+6}
  i note12 {nr}
  i note13 {nr+12}
  i note14 {nr+7}
  i note15 {nr}
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
  c fillColor 0.16 0.24 0.34
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
        subprocess.run([BIN, "frame", inf_file, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        im = Image.open(frame_png)

        prev_jpg = os.path.join(PREVIEWS_DIR, f"{slug}.jpg")
        im.convert("RGB").save(prev_jpg, quality=94)

        thumb_webp = os.path.join(THUMBS_DIR, f"{slug}.webp")
        im.convert("RGB").resize((540, 540), Image.Resampling.LANCZOS).save(thumb_webp, "WEBP", quality=85)

        tmpl_webp = os.path.join(TEMPLATES_DIR, f"{slug}.webp")
        template_preview.render(inf_file, tmpl_webp)

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
  i note4 48
  i note5 55
  i note6 52
  i note7 50
  i note8 48
  i note9 50
  i note10 55
  i note11 60
  i note12 48
  i note13 55
  i note14 52
  i note15 57
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
  f cutoff 2400.0
  f resonance 0.25
  f decay 0.45
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
  c fillColor 0.26 0.16 0.22
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
        subprocess.run([BIN, "frame", inf_file, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        im = Image.open(frame_png)

        prev_jpg = os.path.join(PREVIEWS_DIR, f"{slug}.jpg")
        im.convert("RGB").save(prev_jpg, quality=94)

        thumb_webp = os.path.join(THUMBS_DIR, f"{slug}.webp")
        im.convert("RGB").resize((540, 540), Image.Resampling.LANCZOS).save(thumb_webp, "WEBP", quality=85)

        tmpl_webp = os.path.join(TEMPLATES_DIR, f"{slug}.webp")
        template_preview.render(inf_file, tmpl_webp)

        report.append(f"PASS [Field Effects] {slug}: lufs={lufs:.2f}, peak={peak:.2f}dBFS, silence={silence}")

    # 5. Field Pixel & Embeds
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
                "tags": ["synth", "west-coast", "buchla", "audio", "dsp", "wavefolding", "fm"],
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
                "tags": ["effect", "tape-loop", "audio", "dsp", "allpass", "delay", "saturation"],
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
