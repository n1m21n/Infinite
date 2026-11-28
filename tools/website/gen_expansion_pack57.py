#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 57.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: bauhaus_textile_weave -> albers-gobelin-warp-grid, stolzl-twill-wave-tapestry)
2. Sketch 3D (Theme: habitat_cantilever_brutalism -> habitat-cantilever-cluster-3d, safdie-stepped-ziggurat-3d)
3. Field Synth (Themes: trautonium_subharmonic_organ, koto_harmonic_pluck -> trautonium-subharmonic-organ-synth, koto-harmonic-pluck-synth)
4. Field Effects (Themes: leslie_horn_doppler, resonant_comb_matrix -> leslie-horn-doppler-fx, resonant-comb-matrix-fx)
5. Field Pixel & Embeds (Themes: prism_internal_refraction, dichroic_lattice_caustics -> prism-internal-refraction, dichroic-lattice-caustics)
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
    ALT_BIN = "/Users/namansoni/Infinite/worktrees/main/build/Infinite.app/Contents/MacOS", "Infinite"
    BIN = os.path.expanduser("~/.local/bin/Infinite")
CMD = [BIN, "--cli"] if BIN.endswith("Infinite") and "Infinite.app" in BIN else [BIN]

LIB_DIR = os.path.join(ROOT, "website", "assets", "library")
SKETCH_DIR = os.path.join(LIB_DIR, "sketch")
PIXEL_DIR = os.path.join(LIB_DIR, "pixel")
PREVIEWS_DIR = os.path.join(LIB_DIR, "previews")
THUMBS_DIR = os.path.join(LIB_DIR, "thumbs")
TEMPLATES_DIR = os.path.join(ROOT, "website", "assets", "templates")
EMBEDS_DIR = os.path.join(ROOT, "website", "embeds")

TMP_DIR = "/tmp/pack57_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
try:
    import library
    interest_fn = library.interest
except FileNotFoundError:
    from PIL import ImageFilter
    def interest_fn(png):
        im = Image.open(png).convert("RGB")
        im = im.resize((135, 240) if im.height > im.width else (240, 135))
        lum = im.convert("L")
        spread = min(1.0, ImageStat.Stat(lum).stddev[0] / 60.0)
        edges = min(1.0, ImageStat.Stat(lum.filter(ImageFilter.FIND_EDGES)).mean[0] / 18.0)
        colours = min(1.0, len(set(im.quantize(16).getdata())) / 12.0)
        return round(0.4 * spread + 0.4 * edges + 0.2 * colours, 3)

sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview


# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: bauhaus_textile_weave)
#    Palette from albers_wall_hanging_1925.jpg & stolzl_tapestry.jpg:
#    #EDE6C8 (unbleached linen), #E6CE1A (cadmium yellow),
#    #2C5C54 (verdigris pine), #4A464B (slate charcoal), #C42E2E (crimson)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "albers-gobelin-warp-grid",
        "title": "Albers Gobelin Warp Grid",
        "description": "Bauhaus gobelin tapestry grid with interlocking cadmium-yellow, verdigris-pine, and slate-charcoal weft bands on unbleached linen.",
        "tags": ["bauhaus", "albers", "weaving", "textile", "grid", "procedural", "sketch"],
        "code": """// Albers Gobelin Warp Grid - Infinite Library (Sketch 2D)
param("warp_cols", 7, 4, 12);
param("weft_rows", 18, 10, 28);
param("shift", 0.25, 0.0, 0.8);
param("grain", 480, 120, 900);

function draw(t) {
  // Warm unbleached linen paper ground (#EDE6C8)
  background(0.929, 0.902, 0.784);
  const w = width;
  const h = height;
  const padX = w * 0.14;
  const padY = h * 0.08;
  const spanW = w - padX * 2;
  const spanH = h - padY * 2;
  const cols = floor(warp_cols);
  const rows = floor(weft_rows);
  const cw = spanW / cols;
  const rh = spanH / rows;

  // Fine linen-paper pencil drafting grid & fiber stipple
  stroke(0.68, 0.64, 0.54, 0.28);
  strokeWeight(1.0);
  for (let c = 0; c <= cols; c++) {
    const x = padX + c * cw;
    line(x, padY * 0.5, x, h - padY * 0.5);
  }
  for (let r = 0; r <= rows; r++) {
    const y = padY + r * rh;
    line(padX * 0.5, y, w - padX * 0.5, y);
  }

  noStroke();
  for (let i = 0; i < grain; i++) {
    const gx = ((i * 0.6180339887) % 1.0) * w;
    const gy = ((i * 0.3819660113 + sin(i * 7.1) * 0.03) % 1.0) * h;
    fill(0.45, 0.42, 0.35, 0.12);
    circle(gx, gy, 1.4);
  }

  // Gouache side selvedge washes
  fill(0.902, 0.808, 0.102, 0.22);
  rect(padX * 0.48, padY, padX * 0.48, spanH);
  rect(w - padX * 0.96, padY, padX * 0.48, spanH);

  // Interlocking weft blocks across warp columns
  for (let r = 0; r < rows; r++) {
    const y = padY + r * rh;
    let c = 0;
    while (c < cols) {
      const hVal = sin(r * 3.17 + c * 5.41 + floor(t * shift) * 0.7);
      const span = (abs(hVal) > 0.55 && c + 2 <= cols) ? 2 : ((abs(hVal) < 0.22 && c + 3 <= cols) ? 3 : 1);
      const sel = abs(sin(r * 1.9 + c * 2.7)) * 4.0;
      const bw = span * cw - 2.0;
      const bh = rh * 0.84;

      if (sel < 1.1) {
        // Cadmium yellow (#E6CE1A)
        fill(0.902, 0.808, 0.102, 0.94);
      } else if (sel < 2.2) {
        // Verdigris pine (#2C5C54)
        fill(0.173, 0.361, 0.329, 0.94);
      } else if (sel < 3.1) {
        // Slate charcoal (#4A464B)
        fill(0.290, 0.275, 0.294, 0.92);
      } else {
        // Warm raw linen gap (#F5F1E0)
        fill(0.961, 0.945, 0.878, 0.90);
      }
      rect(padX + c * cw + 1.0, y + rh * 0.08, bw, bh);

      // Subtle horizontal thread striations inside each block
      stroke(0.15, 0.14, 0.13, 0.16);
      strokeWeight(1.0);
      line(padX + c * cw + 2.0, y + rh * 0.36, padX + c * cw + bw, y + rh * 0.36);
      line(padX + c * cw + 2.0, y + rh * 0.64, padX + c * cw + bw, y + rh * 0.64);
      noStroke();

      c += span;
    }
  }

  // Crisp graphite border around tapestry field
  noFill();
  stroke(0.25, 0.24, 0.22, 0.65);
  strokeWeight(1.8);
  rect(padX, padY, spanW, spanH);
}
""".strip()
    },
    {
        "slug": "stolzl-twill-wave-tapestry",
        "title": "Stolzl Twill Wave Tapestry",
        "description": "Gunta Stolzl inspired Bauhaus tapestry with undulating crimson, cobalt, ochre, and sage wave bands and stepped checkerboard twill.",
        "tags": ["bauhaus", "stolzl", "tapestry", "twill", "waves", "procedural", "sketch"],
        "code": """// Stolzl Twill Wave Tapestry - Infinite Library (Sketch 2D)
param("bands", 14, 8, 22);
param("twill_step", 12, 6, 20);
param("wave_amp", 0.065, 0.01, 0.14);
param("drift", 0.35, 0.0, 1.2);

function draw(t) {
  // Warm woven canvas ground (#EAE2D0)
  background(0.918, 0.886, 0.816);
  const w = width;
  const h = height;
  const padX = w * 0.10;
  const padY = h * 0.09;
  const spanW = w - padX * 2;
  const spanH = h - padY * 2;
  const nBands = floor(bands);
  const nStep = floor(twill_step);
  const bandH = spanH / nBands;

  // Undulating wave-weft bands
  noStroke();
  for (let b = 0; b < nBands; b++) {
    const yBase = padY + b * bandH;
    const mod = b % 4;
    if (mod === 0) {
      fill(0.769, 0.180, 0.180, 0.92); // Crimson (#C42E2E)
    } else if (mod === 1) {
      fill(0.114, 0.235, 0.420, 0.92); // Cobalt (#1D3C6B)
    } else if (mod === 2) {
      fill(0.831, 0.639, 0.220, 0.92); // Ochre (#D4A338)
    } else {
      fill(0.373, 0.494, 0.337, 0.92); // Sage (#5F7E56)
    }

    beginShape();
    const segs = 40;
    for (let i = 0; i <= segs; i++) {
      const u = i / segs;
      const x = padX + u * spanW;
      const wave = sin(u * TAU * 1.5 + b * 0.65 + t * drift) * spanH * wave_amp;
      vertex(x, yBase + wave);
    }
    for (let i = segs; i >= 0; i--) {
      const u = i / segs;
      const x = padX + u * spanW;
      const wave = sin(u * TAU * 1.5 + (b + 1) * 0.65 + t * drift) * spanH * wave_amp;
      vertex(x, yBase + bandH + wave);
    }
    endShape(CLOSE);
  }

  // Central stepped checkerboard twill panel
  const twillW = spanW * 0.46;
  const twillH = spanH * 0.44;
  const tx0 = w * 0.5 - twillW * 0.5;
  const ty0 = h * 0.5 - twillH * 0.5;
  const cw = twillW / nStep;
  const ch = twillH / nStep;

  for (let ix = 0; ix < nStep; ix++) {
    for (let iy = 0; iy < nStep; iy++) {
      const diag = (ix + iy) % 4;
      if (diag === 0) {
        fill(0.114, 0.235, 0.420, 0.95);
      } else if (diag === 1) {
        fill(0.953, 0.933, 0.878, 0.95);
      } else if (diag === 2) {
        fill(0.769, 0.180, 0.180, 0.95);
      } else {
        fill(0.831, 0.639, 0.220, 0.95);
      }
      rect(tx0 + ix * cw, ty0 + iy * ch, cw * 0.94, ch * 0.94);
    }
  }

  // Woven warp fringe along bottom edge
  stroke(0.55, 0.48, 0.36, 0.72);
  strokeWeight(1.6);
  const fringeN = 56;
  for (let i = 0; i <= fringeN; i++) {
    const fx = padX + (i / fringeN) * spanW;
    line(fx, padY + spanH + 4, fx + sin(i * 1.7) * 2.5, padY + spanH + 28);
  }

  // Outer selvedge frame
  noFill();
  stroke(0.18, 0.16, 0.15, 0.85);
  strokeWeight(2.5);
  rect(padX, padY, spanW, spanH);
}
""".strip()
    }
]


# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: habitat_cantilever_brutalism)
#    Palette from habitat67_southwest.jpg & habitat67_dllu.jpg:
#    #9C9488 (sunlit limestone concrete), #5E5951 (shaded concrete),
#    #22252A (recessed window void), #3A4248 (terrace plinth)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "habitat-cantilever-cluster-3d",
        "title": "Habitat Cantilever Cluster 3D",
        "description": "Clustered precast brutalist concrete modules inspired by Moshe Safdie's Habitat 67 with cantilevered terraces and recessed window bays.",
        "tags": ["brutalist", "habitat67", "safdie", "concrete", "modular", "architecture", "3d"],
        "code": """// Habitat Cantilever Cluster 3D - Infinite Library (Sketch 3D)
param("tiers", 4, 2, 6);
param("cantilever", 0.48, 0.2, 0.85);
param("orbit", 0.14, 0.0, 0.5);

function draw(t) {
  const nTiers = floor(tiers);

  push();
  scale(0.62, 0.62, 0.62);
  rotateX(-0.34 + sin(t * 0.2) * 0.05);
  rotateY(0.58 + t * orbit);

  // Brutalist concrete promenade plinth
  push();
  translate(0, -1.05, 0);
  fill(0.32, 0.30, 0.28);
  box(3.8, 0.14, 3.4);
  pop();

  // Clustered precast concrete housing modules
  for (let tier = 0; tier < nTiers; tier++) {
    const y = -0.68 + tier * 0.52;
    const count = max(2, 5 - tier);
    for (let i = 0; i < count; i++) {
      const u = (i - (count - 1) * 0.5);
      const x = u * 0.84 + ((tier % 2 === 0) ? 0.22 : -0.22) * cantilever;
      const z = sin(tier * 1.7 + i * 2.1) * 0.65;
      const rot = ((i + tier) % 2 === 0) ? 0.0 : 1.5708;

      push();
      translate(x, y, z);
      rotateY(rot);

      // Main precast limestone-concrete module (#9C9488 -> #6E685F)
      const shade = 0.64 - tier * 0.03 + (i % 2) * 0.06;
      fill(shade, shade * 0.96, shade * 0.90);
      box(0.88, 0.44, 0.58);

      // Recessed dark glazing bay
      push();
      translate(0, 0.02, 0.28);
      fill(0.13, 0.15, 0.18);
      box(0.56, 0.24, 0.05);
      pop();

      // Cantilevered terrace parapet cap
      push();
      translate(0, 0.24, 0);
      fill(0.74, 0.71, 0.66);
      box(0.92, 0.04, 0.62);
      pop();

      pop();
    }
  }

  // Elevated concrete skywalk bridge
  push();
  translate(0, 0.12, 0.15);
  fill(0.54, 0.51, 0.47);
  box(2.6, 0.10, 0.26);
  pop();

  pop();
}
""".strip()
    },
    {
        "slug": "safdie-stepped-ziggurat-3d",
        "title": "Safdie Stepped Ziggurat 3D",
        "description": "Axonometric stepped brutalist concrete ziggurat with rotated modular bays, cantilevered overhang soffits, and deep shadow pockets.",
        "tags": ["brutalist", "ziggurat", "axonometric", "concrete", "architecture", "3d"],
        "code": """// Safdie Stepped Ziggurat 3D - Infinite Library (Sketch 3D)
param("levels", 5, 3, 7);
param("step_out", 0.36, 0.15, 0.65);
param("spin", 0.16, 0.0, 0.5);

function draw(t) {
  const nLvl = floor(levels);

  push();
  scale(0.64, 0.64, 0.64);
  rotateX(-0.38);
  rotateY(0.72 + t * spin);

  // Dark slate courtyard base
  push();
  translate(0, -1.12, 0);
  fill(0.20, 0.22, 0.24);
  box(3.6, 0.12, 3.6);
  pop();

  for (let l = 0; l < nLvl; l++) {
    const y = -0.78 + l * 0.46;
    const arm = 1.15 - l * 0.15;

    for (let k = 0; k < 4; k++) {
      const a = k * 1.5708 + (l % 2) * 0.22;
      push();
      rotateY(a);
      translate(arm * 0.55 + step_out * (l % 2), y, 0);

      // Cast concrete module
      const c = 0.68 - l * 0.04;
      fill(c, c * 0.96, c * 0.89);
      box(0.86, 0.40, 0.54);

      // Recessed architectural window slot
      push();
      translate(0.42, 0.02, 0);
      fill(0.11, 0.13, 0.16);
      box(0.05, 0.20, 0.36);
      pop();

      // Concrete cornice rib
      push();
      translate(0, 0.22, 0);
      fill(0.78, 0.75, 0.70);
      box(0.90, 0.04, 0.58);
      pop();

      pop();
    }
  }

  pop();
}
""".strip()
    }
]


# -------------------------------------------------------------
# 3. Field Synth Sources
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "trautonium-subharmonic-organ-synth",
        "title": "Trautonium Subharmonic Organ Synth",
        "description": "Mixtur-Trautonium inspired subharmonic frequency-divider synthesizer generating f0/2, f0/3, and f0/4 sub-octaves with formant bandpass shaping and germanium soft saturation.",
        "code": """param float sub2Mix = 0.62 [0.0, 1.0];
param float sub3Mix = 0.48 [0.0, 1.0];
param float sub4Mix = 0.35 [0.0, 1.0];
param float formantHz = 1650.0 [400.0, 5200.0];
param float decay = 0.9993 [0.996, 0.9997];
state float p0 = 0; state float p2 = 0; state float p3 = 0; state float p4 = 0;
state float env = 0; state float lp1 = 0; state float bp1 = 0;

env = if(gate > 0.5, env + (1.0 - env) * 0.12, env * decay);
f0 = clamp(freq, 55.0, 2200.0);

# Mixtur-Trautonium subharmonic frequency dividers (f0, f0/2, f0/3, f0/4)
p0 = (p0 + f0 / sr) % 1.0;
p2 = (p2 + (f0 * 0.5) / sr) % 1.0;
p3 = (p3 + (f0 / 3.0) / sr) % 1.0;
p4 = (p4 + (f0 * 0.25) / sr) % 1.0;

s0 = 1.0 - 2.0 * p0;
s2 = 1.0 - 2.0 * p2;
s3 = 1.0 - 2.0 * p3;
s4 = 1.0 - 2.0 * p4;
raw = (s0 * 0.42 + s2 * sub2Mix * 0.36 + s3 * sub3Mix * 0.28 + s4 * sub4Mix * 0.22) * env;

g = clamp(2.0 * sin(3.14159265 * formantHz / sr), 0.04, 0.86);
lp1 = lp1 + g * (raw - lp1);
bp1 = bp1 + g * 0.45 * (lp1 - bp1);

drv = (lp1 * 0.72 + (lp1 - bp1) * 0.48) * 1.45;
out = (drv / (1.0 + abs(drv))) * 0.72;
""",
        "params": {
            "sub2Mix": 0.62,
            "sub3Mix": 0.48,
            "sub4Mix": 0.35,
            "formantHz": 1650.0,
            "decay": 0.9993
        }
    },
    {
        "slug": "koto-harmonic-pluck-synth",
        "title": "Koto Harmonic Pluck Synth",
        "description": "Physical-inspired plucked koto string synthesizer with fast bridge-pluck transient, decaying harmonic partials, one-pole string damping, and wooden body resonance.",
        "code": """param float pluckBright = 0.78 [0.2, 1.0];
param float bridgeRes = 0.42 [0.05, 0.85];
param float bodyHz = 920.0 [320.0, 2800.0];
param float dampHz = 4200.0 [900.0, 8000.0];
param float decay = 0.9991 [0.995, 0.9997];
state float p1 = 0; state float env = 0; state float prevGate = 0;
state float transient = 0; state float lp1 = 0; state float bp1 = 0;

trig = if(gate > 0.5, if(prevGate < 0.5, 1.0, 0.0), 0.0);
prevGate = gate;
transient = if(trig > 0.5, 1.0, transient * 0.994);
env = if(gate > 0.5, env + (1.0 - env) * 0.24, env * decay);

f0 = clamp(freq, 65.0, 2200.0);
p1 = (p1 + f0 / sr) % 1.0;
ph = 6.2831853 * p1;

# Plucked string harmonic series with transient upper-partial bloom
h1 = sin(ph);
h2 = sin(ph * 2.0 + 0.3) * (0.45 + 0.35 * transient * pluckBright);
h3 = sin(ph * 3.0) * (0.22 + 0.32 * transient * pluckBright);
h5 = sin(ph * 5.0) * 0.14 * transient * pluckBright;
raw = (h1 * 0.65 + h2 + h3 + h5) * env;

gDamp = clamp(2.0 * sin(3.14159265 * dampHz / sr), 0.05, 0.88);
lp1 = lp1 + gDamp * (raw - lp1);

gBody = clamp(2.0 * sin(3.14159265 * bodyHz / sr), 0.04, 0.75);
bp1 = bp1 + gBody * (lp1 - bp1);

drv = (lp1 + (lp1 - bp1) * bridgeRes) * 1.35;
out = (drv / (1.0 + abs(drv))) * 0.74;
""",
        "params": {
            "pluckBright": 0.78,
            "bridgeRes": 0.42,
            "bodyHz": 920.0,
            "dampHz": 4200.0,
            "decay": 0.9991
        }
    }
]


# -------------------------------------------------------------
# 4. Field Effects Sources
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "leslie-horn-doppler-fx",
        "title": "Leslie Horn Doppler FX",
        "description": "Dual-rotor rotary speaker simulation splitting signal into treble horn and bass drum rotors with quadrature Doppler phase modulation and tube warmth.",
        "code": """param float rotorHz = 3.8 [0.4, 8.5];
param float depth = 0.68 [0.1, 0.92];
param float crossHz = 820.0 [350.0, 1800.0];
param float drive = 1.45 [0.8, 2.8];
param float mix = 0.72 [0.0, 1.0];
state float lfoH = 0; state float lfoD = 0;
state float lpCross = 0; state float ap1 = 0; state float ap2 = 0; state float ap3 = 0;

lfoH = (lfoH + rotorHz / sr) % 1.0;
lfoD = (lfoD + (rotorHz * 0.76) / sr) % 1.0;

gC = clamp(2.0 * sin(3.14159265 * crossHz / sr), 0.03, 0.72);
lpCross = lpCross + gC * (in - lpCross);
drum = lpCross;
horn = in - lpCross;

# Quadrature Doppler allpass dispersion + horn amplitude sweep
cH = 0.35 + 0.45 * depth * sin(6.2831853 * lfoH);
y1 = -cH * horn + ap1; ap1 = horn + cH * y1;
y2 = -cH * y1 + ap2; ap2 = y1 + cH * y2;
hornMod = y2 * (0.72 + 0.28 * cos(6.2831853 * lfoH));

cD = 0.25 + 0.35 * depth * sin(6.2831853 * lfoD);
drumMod = (-cD * drum + ap3) * (0.80 + 0.20 * cos(6.2831853 * lfoD));
ap3 = drum + cD * drumMod;

drv = (hornMod + drumMod) * drive;
sat = drv / (1.0 + abs(drv));
out = (in * (1.0 - mix) + sat * mix) * 0.56;
""",
        "params": {
            "rotorHz": 3.8,
            "depth": 0.68,
            "crossHz": 820.0,
            "drive": 1.45,
            "mix": 0.72
        }
    },
    {
        "slug": "resonant-comb-matrix-fx",
        "title": "Resonant Comb Matrix FX",
        "description": "Four-stage staggered allpass-comb dispersion matrix with prime-ratio damping, slow LFO detune drift, and soft-clipping feedback limiter.",
        "code": """param float res = 0.66 [0.15, 0.86];
param float driftHz = 0.42 [0.05, 2.5];
param float dampHz = 3100.0 [700.0, 7500.0];
param float feedback = 0.58 [0.0, 0.82];
param float mix = 0.62 [0.0, 1.0];
state float lfo = 0; state float fb = 0; state float lp1 = 0;
state float ap1 = 0; state float ap2 = 0; state float ap3 = 0; state float ap4 = 0;

lfo = (lfo + driftHz / sr) % 1.0;
mod = 0.08 * sin(6.2831853 * lfo);
src = in + fb * feedback;

k1 = clamp(res + mod, 0.1, 0.89);
k2 = clamp(res * 0.87 - mod, 0.1, 0.89);
k3 = clamp(res * 0.73 + mod * 0.6, 0.1, 0.89);
k4 = clamp(res * 0.59 - mod * 0.6, 0.1, 0.89);

y1 = -k1 * src + ap1; ap1 = src + k1 * y1;
y2 = -k2 * y1 + ap2; ap2 = y1 + k2 * y2;
y3 = -k3 * y2 + ap3; ap3 = y2 + k3 * y3;
y4 = -k4 * y3 + ap4; ap4 = y3 + k4 * y4;

g = clamp(2.0 * sin(3.14159265 * dampHz / sr), 0.04, 0.85);
lp1 = lp1 + g * (y4 - lp1);

drv = lp1 * 1.35;
sat = drv / (1.0 + abs(drv));
fb = sat;

out = (in * (1.0 - mix) + sat * mix) * 0.52;
""",
        "params": {
            "res": 0.66,
            "driftHz": 0.42,
            "dampHz": 3100.0,
            "feedback": 0.58,
            "mix": 0.62
        }
    }
]


# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embeds
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "prism-internal-refraction",
        "title": "Prism Internal Refraction",
        "description": "Equilateral borosilicate glass prism with total-internal-reflection laser beam bounces, chromatic dispersion fringes, and micro-dust specular sparkles.",
        "embed_desc": "Audio-reactive borosilicate glass prism with total-internal-reflection beam bounces and chromatic dispersion fringes.",
        "tags": ["prism", "refraction", "optics", "laser", "caustics", "shader", "pixel", "audioreactive"],
        "code": """# Prism Internal Refraction (Field Pixel)
param float beam_angle = 0.35 [0.0, 1.5];
param float dispersion = 0.045 [0.01, 0.12];
param float glow_gain = 1.25 [0.5, 2.0];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
q = p + vec2(0.0, 0.04);

# Equilateral triangle SDF (branchless fold)
k = 1.7320508;
qx0 = abs(q.x) - 0.34;
qy0 = q.y + 0.34 / k;
fold = step(0.0, qx0 + k * qy0);
qx = mix(qx0, (qx0 - k * qy0) * 0.5, fold);
qy = mix(qy0, (-k * qx0 - qy0) * 0.5, fold);
qx = qx - clamp(qx, -0.68, 0.0);
dPrism = -length(vec2(qx, qy)) * sign(qy);

inside = smoothstep(0.008, -0.008, dPrism);
edge = smoothstep(0.016, 0.0, abs(dPrism));

# Total-internal-reflection V-bounce rays inside the prism
ang = beam_angle + 0.12 * sin(t * 0.65);
rayIn = abs((p.y + 0.12) - (p.x + 0.12) * tan(0.72 + ang * 0.25));
rayBounce1 = abs((p.y + 0.16) + (p.x - 0.02) * 1.15);
rayBounce2 = abs((p.y + 0.16) - (p.x - 0.02) * 1.15);

beamGreen = exp(-rayIn * 28.0) + exp(-rayBounce1 * 24.0) * inside;
beamRuby = exp(-rayBounce2 * 26.0) * inside;

# Ground table caustic & micro-dust speckles
tableMask = smoothstep(0.06, -0.22, p.y + 0.16);
causticFloor = exp(-abs(p.x) * 2.6) * tableMask * (0.55 + 0.35 * sin(p.x * 16.0 - t * 1.5));
dust = pow(max(0.0, sin(p.x * 140.0 + sin(p.y * 90.0)) * cos(p.y * 130.0)), 10.0) * inside;

cObsidian = vec3(0.030, 0.045, 0.040);
cJade = vec3(0.08, 0.94, 0.32);
cRuby = vec3(0.96, 0.34, 0.18);
cGlass = vec3(0.16, 0.52, 0.36);

c0 = cObsidian + cGlass * inside * 0.38 + vec3(0.38, 0.98, 0.58) * edge * 0.72;
c1 = c0 + cJade * (beamGreen * 0.95 + causticFloor * 0.85) * glow_gain;
c2 = c1 + cRuby * beamRuby * 0.90 * glow_gain + vec3(0.85, 1.0, 0.88) * dust * 0.85;
col = clamp(c2, 0.0, 1.0);
""",
        "params": {
            "beam_angle": 0.35,
            "dispersion": 0.045,
            "glow_gain": 1.15
        },
        "glsl_frag": """
    precision highp float;
    uniform vec2 res;
    uniform float t;
    uniform float aspect;
    uniform float audio_bass;
    uniform float audio_mid;
    uniform float audio_high;

    void main() {
      vec2 uv = gl_FragCoord.xy / res;
      vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
      vec2 q = p + vec2(0.0, 0.06);

      float k = 1.7320508;
      q.x = abs(q.x) - 0.32;
      q.y = q.y + 0.32 / k;
      if (q.x + k * q.y > 0.0) {
        q = vec2(q.x - k * q.y, -k * q.x - q.y) * 0.5;
      }
      q.x -= clamp(q.x, -0.64, 0.0);
      float dPrism = -length(q) * sign(q.y);

      float inside = smoothstep(0.008, -0.008, dPrism);
      float edge = smoothstep(0.014, 0.0, abs(dPrism));

      float ang = 0.35 + 0.14 * sin(t * 0.65) + audio_mid * 0.32;
      float rayIn = abs((p.y + 0.14) - (p.x + 0.12) * tan(0.72 + ang * 0.25));
      float rayBounce1 = abs((p.y + 0.18) + (p.x - 0.02) * 1.15);
      float rayBounce2 = abs((p.y + 0.18) - (p.x - 0.02) * 1.15);

      float gain = 1.12 + audio_bass * 0.65;
      float beamGreen = exp(-rayIn * 42.0) + exp(-rayBounce1 * 36.0) * inside;
      float beamRuby = exp(-rayBounce2 * 38.0) * inside;

      float tableMask = smoothstep(0.04, -0.18, p.y + 0.18);
      float causticFloor = exp(-abs(p.x) * 3.8) * tableMask * (0.45 + 0.35 * sin(p.x * 18.0 - t * 1.5));
      float dust = pow(max(0.0, sin(p.x * 140.0 + sin(p.y * 90.0)) * cos(p.y * 130.0)), 14.0) * inside * (1.0 + audio_high * 1.4);

      vec3 cObsidian = vec3(0.018, 0.024, 0.022);
      vec3 cJade = vec3(0.08, 0.92, 0.30);
      vec3 cRuby = vec3(0.94, 0.32, 0.18);
      vec3 cGlass = vec3(0.14, 0.48, 0.34);

      vec3 col = cObsidian + cGlass * inside * 0.32 + vec3(0.35, 0.98, 0.55) * edge * 0.65;
      col += cJade * (beamGreen * 0.95 + causticFloor * 0.72) * gain;
      col += cRuby * beamRuby * 0.85 * gain;
      col += vec3(0.85, 1.0, 0.88) * dust * 0.85;
      gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
    }
"""
    },
    {
        "slug": "dichroic-lattice-caustics",
        "title": "Dichroic Lattice Caustics",
        "description": "Multi-plane dichroic glass fins with thin-film spectral interference, domain-warped caustic ribbons, and angle-dependent cyan-magenta-amber transmission.",
        "embed_desc": "Audio-reactive dichroic glass lattice with thin-film spectral interference and domain-warped caustic ribbons.",
        "tags": ["dichroic", "caustics", "interference", "optics", "shader", "pixel", "audioreactive"],
        "code": """# Dichroic Lattice Caustics (Field Pixel)
param float fin_count = 7.0 [3.0, 14.0];
param float warp = 0.42 [0.1, 1.0];
param float phase_shift = 0.5 [0.0, 2.0];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);

# Domain-warped caustic coordinates
wx = p.x + 0.12 * sin(p.y * 6.5 + t * 0.55) * warp;
wy = p.y + 0.12 * cos(p.x * 6.5 - t * 0.45) * warp;

# Stepped dichroic glass fin lattice
fin = abs(fract(wx * fin_count + wy * 1.5) - 0.5);
finEdge = smoothstep(0.12, 0.01, fin);

# Thin-film optical path difference across fin angles
opd = (length(vec2(wx, wy)) * 9.0 + wx * 4.5 + t * 0.65 + phase_shift) * 3.14159;
spec = vec3(0.5 + 0.5 * cos(opd * 1.0 + 0.3), 0.5 + 0.5 * cos(opd * 1.24 + 1.4), 0.5 + 0.5 * cos(opd * 1.48 + 2.5));

# Radial vignette & deep indigo optical chamber
vign = smoothstep(0.68, 0.12, length(p));
cChamber = vec3(0.025, 0.032, 0.068);
cBase = mix(cChamber, spec * (0.45 + 0.65 * finEdge), vign);
col = clamp(cBase + vec3(0.95, 0.72, 0.35) * pow(finEdge, 2.2) * vign * 0.55, 0.0, 1.0);
""",
        "params": {
            "fin_count": 7.0,
            "warp": 0.42,
            "phase_shift": 0.5
        },
        "glsl_frag": """
    precision highp float;
    uniform vec2 res;
    uniform float t;
    uniform float aspect;
    uniform float audio_bass;
    uniform float audio_mid;
    uniform float audio_high;

    void main() {
      vec2 uv = gl_FragCoord.xy / res;
      vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);

      float warp = 0.42 + audio_bass * 0.38;
      float wx = p.x + 0.12 * sin(p.y * 6.5 + t * 0.55) * warp;
      float wy = p.y + 0.12 * cos(p.x * 6.5 - t * 0.45) * warp;

      float fin = abs(fract(wx * 7.0 + wy * 1.5) - 0.5);
      float finEdge = smoothstep(0.12, 0.01, fin);

      float opd = (length(vec2(wx, wy)) * 9.0 + wx * 4.5 + t * 0.65 + audio_mid * 1.2) * 3.14159;
      vec3 spec = vec3(
        0.5 + 0.5 * cos(opd * 1.0 + 0.3),
        0.5 + 0.5 * cos(opd * 1.24 + 1.4),
        0.5 + 0.5 * cos(opd * 1.48 + 2.5)
      );

      float vign = smoothstep(0.68, 0.12, length(p));
      vec3 cChamber = vec3(0.025, 0.032, 0.068);
      vec3 col = mix(cChamber, spec * (0.45 + 0.65 * finEdge), vign);
      col += vec3(0.95, 0.72, 0.35) * pow(finEdge, 2.2) * vign * (0.55 + audio_high * 0.75);
      gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
    }
"""
    }
]


def make_embed_html(it):
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>{it['title']} | Infinite</title>
  <style>
    * {{ box-sizing: border-box; margin: 0; padding: 0; }}
    html, body {{ width: 100%; height: 100%; overflow: hidden; background: #151930; }}
    canvas {{ width: 100%; height: 100%; display: block; }}
  </style>
</head>
<body>
  <canvas id="gl"></canvas>
  <script>
    const canvas = document.getElementById("gl");
    const gl = canvas.getContext("webgl") || canvas.getContext("experimental-webgl");
    if (!gl) {{
      document.body.innerHTML = "webgl not supported";
      throw new Error("WebGL not supported");
    }}

    const vsSource = `
      attribute vec2 position;
      void main() {{
        gl_Position = vec4(position, 0.0, 1.0);
      }}
    `;

    const fsSource = `{it['glsl_frag']}`;

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
        sc = interest_fn(frame_png)

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
  c bg 0.145 0.180 0.235
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
        sc = interest_fn(frame_png)

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
        inf_content = f"""infinite-patch 1
# {it['title']}
node 1 Notes Note Sequencer
  id notes
  pos 0 1024
  f rateBeats 0.5
  i steps 16
  i note0 52
  i note1 55
  i note2 57
  i note3 59
  i note4 62
  i note5 59
  i note6 57
  i note7 55
  i note8 52
  i note9 59
  i note10 62
  i note11 64
  i note12 62
  i note13 59
  i note14 57
  i note15 55
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
  pos 1040 1024
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
        report.append(f"PASS [Field Effect] {slug}: lufs={lufs:.2f}, peak={peak:.2f}dBFS, silence={silence}")

    # 5. Field Pixel & WebGL Embeds
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
        sc = interest_fn(frame_png)

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
                "tags": ["synth", "subharmonic", "trautonium", "koto", "pluck", "audio", "dsp"],
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
                "tags": ["effect", "leslie", "doppler", "comb", "allpass", "audio", "dsp"],
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
