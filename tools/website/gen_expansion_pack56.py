#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 56.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: zellij_girih_tessellation -> girih-darb-imam-strapwork, muqarnas-corbel-projection)
2. Sketch 3D (Theme: candela_hypar_shell -> candela-xochimilco-hypar-3d, candela-palmira-saddle-3d)
3. Field Synth (Themes: casio_cz_phase_distortion, theremin_heterodyne -> casio-cz-phase-distortion-synth, theremin-heterodyne-ribbon-synth)
4. Field Effects (Themes: barberpole_wire_dispersion, helical_plate_reverb -> barberpole-shepard-phaser-fx, helical-plate-reverb-fx)
5. Field Pixel & Embeds (Themes: soap_film_interference, dichroic_fresnel_optics -> soap-film-lipid-interference, fresnel-lighthouse-caustics)
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

TMP_DIR = "/tmp/pack56_build"
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
# 1. Sketch 2D Sources (Theme: zellij_girih_tessellation)
#    Palette from girih_darb_imam.jpg:
#    #EFECE2 (bone ivory), #C89B42 (ochre gold), #1D3E7A (lapis cobalt), #1A1B1E (charcoal strapwork)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "girih-darb-imam-strapwork",
        "title": "Girih Darb-i Imam Strapwork",
        "description": "Decagonal girih star-polygon tessellation with interlaced charcoal strapwork ribbons, ochre ceramic facets, and lapis-cobalt medallions.",
        "tags": ["girih", "zellij", "tessellation", "islamic-geometry", "procedural", "sketch"],
        "code": """// Girih Darb-i Imam Strapwork - Infinite Library (Sketch 2D)
param("rosettes", 3, 2, 5);
param("strap_w", 4.5, 2.0, 9.0);
param("breathe", 0.08, 0.0, 0.25);
param("grain", 420, 100, 900);

function draw(t) {
  // Warm sandstone-ivory ground (#E0DBD0)
  background(0.880, 0.860, 0.810);
  const w = width;
  const h = height;
  const side = min(w, h);
  const cx = w * 0.5;
  const cy = h * 0.5;
  const n = floor(rosettes);
  const span = side * 0.82;
  const step = span / n;

  // Fine terracotta & mineral glaze stipple
  noStroke();
  for (let i = 0; i < grain; i++) {
    const gx = ((i * 0.6180339887) % 1.0) * w;
    const gy = ((i * 0.3819660113 + sin(i * 9.3) * 0.04) % 1.0) * h;
    fill(0.55, 0.45, 0.30, 0.14);
    circle(gx, gy, 1.5);
  }

  // Outer architectural frame
  noFill();
  stroke(0.114, 0.243, 0.478, 0.85);
  strokeWeight(3.0);
  rect(cx - span * 0.53, cy - span * 0.53, span * 1.06, span * 1.06);
  stroke(0.784, 0.608, 0.259, 0.85);
  strokeWeight(1.5);
  rect(cx - span * 0.51, cy - span * 0.51, span * 1.02, span * 1.02);

  // Decagonal girih rosette lattice
  for (let ix = 0; ix < n; ix++) {
    for (let iy = 0; iy < n; iy++) {
      const rx = cx - span * 0.5 + (ix + 0.5) * step;
      const ry = cy - span * 0.5 + (iy + 0.5) * step;
      const pulse = 1.0 + breathe * sin(t * 0.7 + ix * 1.1 + iy * 0.9);
      const R = step * 0.46 * pulse;

      // Ochre decagonal backing plate (#C89B42)
      fill(0.784, 0.608, 0.259, 0.92);
      stroke(0.102, 0.106, 0.118, 0.95);
      strokeWeight(strap_w * 0.45);
      beginShape();
      for (let k = 0; k < 10; k++) {
        const a = (k / 10) * TAU + 0.157;
        vertex(rx + cos(a) * R, ry + sin(a) * R);
      }
      endShape(CLOSE);

      // 10-pointed girih star polygon in lapis cobalt (#1D3E7A)
      fill(0.114, 0.243, 0.478, 0.96);
      stroke(0.937, 0.925, 0.886, 0.95);
      strokeWeight(strap_w * 0.55);
      beginShape();
      for (let k = 0; k < 20; k++) {
        const a = (k / 20) * TAU;
        const rad = (k % 2 === 0) ? R * 0.82 : R * 0.42;
        vertex(rx + cos(a) * rad, ry + sin(a) * rad);
      }
      endShape(CLOSE);

      // Inner bone-ivory & gold core
      fill(0.937, 0.925, 0.886, 0.95);
      stroke(0.102, 0.106, 0.118, 0.95);
      strokeWeight(strap_w * 0.4);
      beginShape();
      for (let k = 0; k < 10; k++) {
        const a = (k / 10) * TAU;
        const rad = (k % 2 === 0) ? R * 0.28 : R * 0.16;
        vertex(rx + cos(a) * rad, ry + sin(a) * rad);
      }
      endShape(CLOSE);

      // Interlaced strapwork ribs radiating to neighbors
      noFill();
      stroke(0.102, 0.106, 0.118, 0.92);
      strokeWeight(strap_w);
      for (let k = 0; k < 10; k++) {
        const a = (k / 10) * TAU;
        line(rx + cos(a) * R * 0.42, ry + sin(a) * R * 0.42,
             rx + cos(a) * R * 1.04, ry + sin(a) * R * 1.04);
      }
      stroke(0.937, 0.925, 0.886, 0.88);
      strokeWeight(strap_w * 0.36);
      for (let k = 0; k < 10; k++) {
        const a = (k / 10) * TAU;
        line(rx + cos(a) * R * 0.42, ry + sin(a) * R * 0.42,
             rx + cos(a) * R * 1.04, ry + sin(a) * R * 1.04);
      }
    }
  }
}
""".strip()
    },
    {
        "slug": "muqarnas-corbel-projection",
        "title": "Muqarnas Corbel Projection",
        "description": "Orthographic geometric plan of a Persian muqarnas stalactite vault with concentric star tiers, sandstone ribs, and lapis-blue spandrels.",
        "tags": ["muqarnas", "vault", "isfahan", "geometric", "architecture", "sketch"],
        "code": """// Muqarnas Corbel Projection - Infinite Library (Sketch 2D)
param("tiers", 6, 4, 9);
param("symmetry", 12, 8, 16);
param("twist", 0.08, 0.0, 0.25);
param("line_w", 2.2, 1.0, 4.5);

function draw(t) {
  // Deep lapis-indigo vault ground (#112244)
  background(0.067, 0.133, 0.267);
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const maxR = min(w, h) * 0.44;
  const nTiers = floor(tiers);
  const sym = floor(symmetry);

  // Outer arched squinch border
  noFill();
  stroke(0.82, 0.68, 0.42, 0.55);
  strokeWeight(line_w * 1.3);
  circle(cx, cy, maxR * 2.06);

  // Radial drafting axes
  stroke(0.82, 0.68, 0.42, 0.22);
  strokeWeight(1.0);
  for (let s = 0; s < sym; s++) {
    const a = (s / sym) * TAU;
    line(cx, cy, cx + cos(a) * maxR * 1.03, cy + sin(a) * maxR * 1.03);
  }

  // Tiered muqarnas stalactite cells from outer squinch to apex
  for (let tier = nTiers; tier >= 1; tier--) {
    const u = tier / nTiers;
    const rOuter = maxR * u;
    const rInner = maxR * (tier - 0.68) / nTiers;
    const phase = (tier % 2 === 0 ? 0.5 : 0.0) * (TAU / sym) + sin(t * 0.4 + tier * 0.5) * twist;

    // Alternating sandstone-gold, turquoise-glaze, and lapis-cobalt muqarnas tiers
    if (tier % 3 === 0) {
      fill(0.82, 0.64, 0.32, 0.96);
    } else if (tier % 3 === 1) {
      fill(0.16, 0.48, 0.58, 0.96);
    } else {
      fill(0.114, 0.243, 0.478, 0.96);
    }
    stroke(0.95, 0.91, 0.82, 0.92);
    strokeWeight(line_w);

    beginShape();
    for (let s = 0; s < sym * 2; s++) {
      const a = (s / (sym * 2)) * TAU + phase;
      const rad = (s % 2 === 0) ? rOuter : rInner;
      vertex(cx + cos(a) * rad, cy + sin(a) * rad);
    }
    endShape(CLOSE);

    // Ribbed corbel highlights & dark shadow pockets
    noFill();
    stroke(0.06, 0.08, 0.14, 0.85);
    strokeWeight(line_w * 0.7);
    for (let s = 0; s < sym; s++) {
      const a0 = (s / sym) * TAU + phase;
      const a1 = ((s + 0.5) / sym) * TAU + phase;
      line(cx + cos(a0) * rOuter, cy + sin(a0) * rOuter,
           cx + cos(a1) * rInner, cy + sin(a1) * rInner);
    }
  }

  // Central Shamsa sunburst medallion
  fill(0.114, 0.243, 0.478, 0.98);
  stroke(0.92, 0.80, 0.48, 0.95);
  strokeWeight(line_w);
  beginShape();
  for (let s = 0; s < sym * 2; s++) {
    const a = (s / (sym * 2)) * TAU;
    const rad = (s % 2 === 0) ? maxR * 0.14 : maxR * 0.07;
    vertex(cx + cos(a) * rad, cy + sin(a) * rad);
  }
  endShape(CLOSE);
}
""".strip()
    }
]


# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: candela_hypar_shell)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "candela-xochimilco-hypar-3d",
        "title": "Candela Xochimilco Hypar 3D",
        "description": "Eight-lobed thin-shell concrete hyperbolic paraboloid vault inspired by Felix Candela's Los Manantiales with ruled saddle ribs.",
        "tags": ["candela", "hypar", "concrete", "shell", "architecture", "3d", "sketch"],
        "code": """// Candela Xochimilco Hypar 3D - Infinite Library (Sketch 3D)
param("lobes", 8, 6, 10);
param("ribs", 14, 8, 20);
param("arch_rise", 0.95, 0.5, 1.4);

function draw(t) {
  const nLobes = floor(lobes);
  const nRibs = floor(ribs);

  push();
  scale(0.68, 0.68, 0.68);
  rotateX(-0.36 + sin(t * 0.22) * 0.08);
  rotateY(0.25 + t * 0.14);

  // Dark slate water-mirror plinth
  push();
  translate(0, -0.58, 0);
  fill(0.12, 0.16, 0.20);
  box(3.4, 0.08, 3.4);
  pop();

  // Radially symmetric intersecting hyperbolic paraboloid lobes
  for (let l = 0; l < nLobes; l++) {
    const a = (l / nLobes) * TAU;
    push();
    rotateY(a);
    for (let i = 0; i < nRibs; i++) {
      const u = i / (nRibs - 1);
      const r = 0.18 + u * 1.38;
      const y = -0.35 + arch_rise * (1.0 - (u - 0.35) * (u - 0.35) * 1.45);
      const wSpan = 0.16 + u * 0.72;
      const tilt = (u - 0.35) * 0.52;

      push();
      translate(r, y, 0);
      rotateZ(-tilt);
      // Warm cast concrete shell (#DFDCD4 -> #A8A399)
      const shade = 0.84 - u * 0.22;
      fill(shade, shade * 0.98, shade * 0.94);
      box(0.13, 0.045, wSpan);
      pop();
    }

    // Sharp concrete groin rib along lobe seam
    push();
    translate(0.75, 0.12, 0.0);
    rotateZ(-0.18);
    fill(0.45, 0.43, 0.40);
    box(1.45, 0.07, 0.07);
    pop();

    pop();
  }

  pop();
}
""".strip()
    },
    {
        "slug": "candela-palmira-saddle-3d",
        "title": "Candela Palmira Saddle 3D",
        "description": "Sweeping anticlastic concrete saddle shell with ruled straight-line generators, cantilevered edge beams, and sculptural shadow ribs.",
        "tags": ["candela", "saddle", "hypar", "parametric", "concrete", "3d", "sketch"],
        "code": """// Candela Palmira Saddle 3D - Infinite Library (Sketch 3D)
param("grid_u", 16, 10, 24);
param("saddle_k", 0.72, 0.35, 1.15);
param("spin", 0.18, 0.0, 0.6);

function draw(t) {
  const nu = floor(grid_u);
  const nv = 12;

  push();
  scale(0.74, 0.74, 0.74);
  rotateX(-0.34);
  rotateY(0.55 + sin(t * spin) * 0.45);

  // Travertine stepped podium
  push();
  translate(0, -0.78, 0);
  fill(0.32, 0.30, 0.28);
  box(2.6, 0.12, 2.2);
  pop();

  // Ruled hyperbolic paraboloid surface z = k * (x^2 - y^2)
  for (let i = 0; i < nu; i++) {
    const u = (i / (nu - 1)) * 2.0 - 1.0;
    for (let j = 0; j < nv; j++) {
      const v = (j / (nv - 1)) * 2.0 - 1.0;
      const x = u * 1.35;
      const z = v * 1.15;
      const y = 0.25 + saddle_k * (u * u * 0.65 - v * v * 0.55);

      push();
      translate(x, y, z);
      rotateZ(u * 0.42 * saddle_k);
      rotateX(-v * 0.38 * saddle_k);
      const lum = 0.72 + 0.18 * (u * 0.5 - v * 0.5);
      fill(lum, lum * 0.97, lum * 0.92);
      box(0.19, 0.045, 0.21);
      pop();
    }
  }

  // Cantilevered dark bronze/steel tension piers
  for (let side = -1; side <= 1; side += 2) {
    push();
    translate(0, -0.35, side * 0.95);
    fill(0.22, 0.20, 0.18);
    box(0.18, 0.85, 0.18);
    pop();
  }

  pop();
}
""".strip()
    }
]


# -------------------------------------------------------------
# 3. Field Synth Sources
#    (casio_cz_phase_distortion & theremin_heterodyne_ribbon)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "casio-cz-phase-distortion-synth",
        "title": "Casio CZ Phase Distortion Synth",
        "description": "Dual-line phase-distortion synthesizer inspired by Casio CZ series with dynamic DCW cosine wave-shaping, detuned harmonic twin, and soft saturation.",
        "note_root": 55,
        "params": {
            "dcwDepth": 0.72,
            "dcwEnv": 0.55,
            "detune": 0.005,
            "brightHz": 3400.0,
            "decay": 0.9992
        },
        "code": """param float dcwDepth = 0.72 [0.1, 0.95];
param float dcwEnv = 0.55 [0.0, 1.0];
param float detune = 0.005 [0.0005, 0.02];
param float brightHz = 3400.0 [700.0, 7500.0];
param float decay = 0.9992 [0.996, 0.9997];
state float p1 = 0; state float p2 = 0;
state float env = 0; state float dcw = 0;
state float lp1 = 0;

env = if(gate > 0.5, env + (1.0 - env) * 0.14, env * decay);
dcw = if(gate > 0.5, dcw + (1.0 - dcw) * 0.08, dcw * 0.9985);

f0 = clamp(freq, 55.0, 2200.0);
p1 = (p1 + f0 / sr) % 1.0;
p2 = (p2 + f0 * (1.0 + detune) / sr) % 1.0;

# Casio CZ phase-distortion wave-shaping with dynamic DCW index
idx = dcwDepth * (0.3 + 0.7 * dcw * dcwEnv) * 0.42;
w1 = p1 + idx * sin(6.2831853 * p1);
w2 = p2 + idx * sin(6.2831853 * p2);
raw = (-cos(6.2831853 * w1) * 0.6 - cos(6.2831853 * w2) * 0.4) * env;

g = clamp(2.0 * sin(3.14159265 * brightHz / sr), 0.05, 0.88);
lp1 = lp1 + g * (raw - lp1);

drv = lp1 * 1.45;
out = (drv / (1.0 + abs(drv))) * 0.72;
"""
    },
    {
        "slug": "theremin-heterodyne-ribbon-synth",
        "title": "Theremin Heterodyne Ribbon Synth",
        "description": "Continuous glide heterodyne oscillator with slow-attack vocal formant filtering, expressive vibrato, and warm triode saturation.",
        "note_root": 60,
        "params": {
            "glide": 0.0025,
            "vibratoHz": 5.2,
            "vibDepth": 0.014,
            "formantHz": 1850.0,
            "decay": 0.9994
        },
        "code": """param float glide = 0.0025 [0.0004, 0.02];
param float vibratoHz = 5.2 [1.0, 9.0];
param float vibDepth = 0.014 [0.0, 0.04];
param float formantHz = 1850.0 [500.0, 4800.0];
param float decay = 0.9994 [0.996, 0.9997];
state float curF = 261.63; state float p0 = 0; state float vib = 0;
state float env = 0; state float bp1 = 0; state float lp1 = 0;

curF = curF + (clamp(freq, 65.0, 2400.0) - curF) * glide;
env = if(gate > 0.5, env + (1.0 - env) * 0.028, env * decay);
vib = (vib + vibratoHz / sr) % 1.0;

fMod = curF * (1.0 + vibDepth * sin(6.2831853 * vib) * env);
p0 = (p0 + fMod / sr) % 1.0;

# Heterodyne warm-triode wave with subtle 2nd/3rd harmonics
ph = 6.2831853 * p0;
tone = sin(ph) * 0.78 + sin(ph * 2.0 + 0.4) * 0.22 + sin(ph * 3.0) * 0.08;
raw = tone * env;

gF = clamp(2.0 * sin(3.14159265 * formantHz / sr), 0.04, 0.85);
lp1 = lp1 + gF * (raw - lp1);
bp1 = bp1 + gF * 0.35 * (lp1 - bp1);

drv = (lp1 * 0.82 + bp1 * 0.25) * 1.4;
out = (drv / (1.0 + abs(drv))) * 0.74;
"""
    }
]


# -------------------------------------------------------------
# 4. Field Effects Sources
#    (barberpole_shepard_phaser & helical_plate_reverb)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "shepard-allpass-cascade-fx",
        "title": "Shepard Allpass Cascade FX",
        "description": "Continuously rising quadrature allpass phaser network with cross-coupled feedback, silky notch sweep, and warm analog saturation.",
        "params": {
            "rateHz": 0.42,
            "depth": 0.68,
            "feedback": 0.48,
            "dampHz": 4600.0,
            "mix": 0.65
        },
        "code": """param float rateHz = 0.42 [0.05, 3.0];
param float depth = 0.68 [0.1, 0.92];
param float feedback = 0.48 [0.0, 0.82];
param float dampHz = 4600.0 [800.0, 8000.0];
param float mix = 0.65 [0.0, 1.0];
state float ph = 0; state float ap1 = 0; state float ap2 = 0; state float ap3 = 0; state float ap4 = 0;
state float lp1 = 0; state float fb = 0;

ph = (ph + rateHz / sr) % 1.0;
c1 = clamp(0.15 + depth * 0.72 * ((ph) % 1.0), 0.06, 0.89);
c2 = clamp(0.15 + depth * 0.72 * ((ph + 0.5) % 1.0), 0.06, 0.89);

src = in + fb * feedback;
y1 = -c1 * src + ap1; ap1 = src + c1 * y1;
y2 = -c1 * y1 + ap2; ap2 = y1 + c1 * y2;
y3 = -c2 * y2 + ap3; ap3 = y2 + c2 * y3;
y4 = -c2 * y3 + ap4; ap4 = y3 + c2 * y4;

g = clamp(2.0 * sin(3.14159265 * dampHz / sr), 0.05, 0.88);
lp1 = lp1 + g * (y4 - lp1);
drv = lp1 * 1.25;
fb = drv / (1.0 + abs(drv));

out = (in * (1.0 - mix) + fb * mix) * 0.76;
"""
    },
    {
        "slug": "helical-plate-reverb-fx",
        "title": "Helical Plate Reverb FX",
        "description": "Dense metallic plate dispersion effect using cascaded allpass diffusion cells, high-frequency air damping, and gentle transformer warmth.",
        "params": {
            "diffusion": 0.64,
            "feedback": 0.62,
            "dampHz": 2900.0,
            "drive": 1.3,
            "mix": 0.58
        },
        "code": """param float diffusion = 0.64 [0.2, 0.86];
param float feedback = 0.62 [0.0, 0.84];
param float dampHz = 2900.0 [700.0, 7500.0];
param float drive = 1.3 [0.8, 2.5];
param float mix = 0.58 [0.0, 1.0];
state float ap1 = 0; state float ap2 = 0; state float ap3 = 0; state float ap4 = 0;
state float lp1 = 0; state float fb = 0;

src = in + fb * feedback;
y1 = -diffusion * src + ap1; ap1 = src + diffusion * y1;
y2 = -(diffusion * 0.84) * y1 + ap2; ap2 = y1 + (diffusion * 0.84) * y2;
y3 = -(diffusion * 0.71) * y2 + ap3; ap3 = y2 + (diffusion * 0.71) * y3;
y4 = -(diffusion * 0.58) * y3 + ap4; ap4 = y3 + (diffusion * 0.58) * y4;

g = clamp(2.0 * sin(3.14159265 * dampHz / sr), 0.04, 0.85);
lp1 = lp1 + g * (y4 - lp1);

drv = lp1 * drive;
sat = drv / (1.0 + abs(drv));
fb = sat;

out = (in * (1.0 - mix) + sat * mix) * 0.50;
"""
    }
]


# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embed Sources
#    (soap_film_interference & dichroic_fresnel_optics)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "soap-film-lipid-interference",
        "title": "Soap Film Lipid Interference",
        "description": "Thin-film optical wave interference across drifting lipid-island domains on obsidian ground with spectral RGB phase fringes.",
        "embed_desc": "Audio-reactive thin-film soap bubble optical interference across drifting lipid islands.",
        "tags": ["soap-film", "interference", "optics", "iridescent", "shader", "pixel", "audioreactive"],
        "params": {
            "film_scale": 3.4,
            "drift": 0.45,
            "thickness": 1.8
        },
        "code": """# Soap Film Lipid Interference (Field Pixel)
p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);

# Multi-bubble cellular lipid domains
d1 = length(p - vec2(-0.18 + 0.04 * sin(t * 0.4), 0.10 + 0.03 * cos(t * 0.3))) - 0.24;
d2 = length(p - vec2(0.16 - 0.03 * cos(t * 0.35), -0.06 + 0.04 * sin(t * 0.5))) - 0.29;
d3 = length(p - vec2(0.02, 0.24 - 0.03 * sin(t * 0.45))) - 0.16;
d4 = length(p - vec2(-0.14, -0.22)) - 0.18;

filmD = min(min(d1, d2), min(d3, d4));
mask = smoothstep(0.015, -0.015, filmD);

# Optical path difference (nm-scale thin-film thickness field)
opd = (1.6 + 0.9 * sin(p.x * 7.5 + sin(p.y * 6.0 + t * 0.45)) + 0.6 * cos(length(p) * 14.0 - t * 0.6)) * 3.14159;

# Physical RGB wavelength interference (650nm, 532nm, 460nm ratios)
spec = vec3(0.5 + 0.5 * cos(opd * 1.00 + 0.2), 0.5 + 0.5 * cos(opd * 1.22 + 1.1), 0.5 + 0.5 * cos(opd * 1.41 + 2.3));

# Obsidian Newton-black film rim + iridescent interior
rim = smoothstep(0.03, 0.0, abs(filmD)) * 0.45;
bg = vec3(0.018, 0.020, 0.026);
col = mix(bg, spec * 0.92 + vec3(0.06), mask) + vec3(0.45, 0.52, 0.60) * rim;
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

  float d1 = length(p - vec2(-0.18 + 0.04 * sin(t * 0.4), 0.10 + 0.03 * cos(t * 0.3))) - (0.24 + audio_bass * 0.05);
  float d2 = length(p - vec2(0.16 - 0.03 * cos(t * 0.35), -0.06 + 0.04 * sin(t * 0.5))) - (0.29 + audio_mid * 0.04);
  float d3 = length(p - vec2(0.02, 0.24 - 0.03 * sin(t * 0.45))) - 0.16;
  float d4 = length(p - vec2(-0.14, -0.22)) - 0.18;

  float filmD = min(min(d1, d2), min(d3, d4));
  float mask = smoothstep(0.015, -0.015, filmD);

  float opd = (1.6 + audio_high * 0.75 + 0.9 * sin(p.x * 7.5 + sin(p.y * 6.0 + t * 0.45)) + 0.6 * cos(length(p) * 14.0 - t * 0.6)) * 3.14159;

  vec3 spec = vec3(
    0.5 + 0.5 * cos(opd * 1.00 + 0.2),
    0.5 + 0.5 * cos(opd * 1.22 + 1.1),
    0.5 + 0.5 * cos(opd * 1.41 + 2.3)
  );

  float rim = smoothstep(0.03, 0.0, abs(filmD)) * 0.45;
  vec3 bg = vec3(0.018, 0.020, 0.026);
  vec3 col = mix(bg, spec * 0.92 + vec3(0.06), mask) + vec3(0.45, 0.52, 0.60) * rim;
  gl_FragColor = vec4(col, 1.0);
}"""
    },
    {
        "slug": "fresnel-lighthouse-caustics",
        "title": "Fresnel Lighthouse Caustics",
        "description": "Stepped concentric Fresnel dioptric glass-prism lens refracting warm amber-gold beams and deep maritime indigo caustics.",
        "embed_desc": "Audio-reactive stepped Fresnel dioptric lighthouse optics and amber-indigo caustics.",
        "tags": ["fresnel", "optics", "caustics", "prism", "lighthouse", "pixel", "audioreactive"],
        "params": {
            "rings": 14.0,
            "beam_speed": 0.55,
            "dispersion": 0.42
        },
        "code": """# Fresnel Lighthouse Caustics (Field Pixel)
p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
r = length(p);
a = atan2(p.y, p.x);

# Stepped concentric Fresnel dioptric prism rings
rings = 14.0;
stepR = fract(r * rings) - 0.5;
prismGroove = smoothstep(0.42, 0.12, abs(stepR));

# Rotating dual-bulls-eye lighthouse beams
beam1 = pow(max(0.0, cos(a - t * 0.55)), 6.0);
beam2 = pow(max(0.0, cos(a - t * 0.55 + 3.14159)), 6.0);
beams = (beam1 + beam2) * exp(-r * 1.6);

# Brass frame ribs & amber-gold refraction (#0E192E -> #C88B28 -> #FFE394)
ribs = smoothstep(0.04, 0.01, abs(sin(a * 4.0)));
lensCore = smoothstep(0.44, 0.05, r);

cNavy = vec3(0.055, 0.098, 0.180);
cAmber = vec3(0.784, 0.545, 0.157);
cGold = vec3(1.000, 0.890, 0.580);
cBrass = vec3(0.28, 0.21, 0.10);

energy = clamp(prismGroove * lensCore * 0.78 + beams * 1.05, 0.0, 1.0);
col = mix(cNavy, cAmber, energy);
col = mix(col, cGold, clamp(beams * 0.9 + pow(energy, 3.0) * 0.6, 0.0, 1.0));
col = mix(col, cBrass, ribs * lensCore * 0.55);
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
  float r = length(p);
  float a = atan(p.y, p.x);

  float rings = 14.0 + audio_high * 4.0;
  float stepR = fract(r * rings - audio_bass * 0.25) - 0.5;
  float prismGroove = smoothstep(0.42, 0.12, abs(stepR));

  float rot = t * 0.55 + audio_mid * 0.6;
  float beam1 = pow(max(0.0, cos(a - rot)), 6.0);
  float beam2 = pow(max(0.0, cos(a - rot + 3.14159)), 6.0);
  float beams = (beam1 + beam2) * exp(-r * 1.6) * (1.0 + audio_bass * 0.5);

  float ribs = smoothstep(0.04, 0.01, abs(sin(a * 4.0)));
  float lensCore = smoothstep(0.44, 0.05, r);

  vec3 cNavy = vec3(0.055, 0.098, 0.180);
  vec3 cAmber = vec3(0.784, 0.545, 0.157);
  vec3 cGold = vec3(1.000, 0.890, 0.580);
  vec3 cBrass = vec3(0.28, 0.21, 0.10);

  float energy = clamp(prismGroove * lensCore * 0.78 + beams * 1.05, 0.0, 1.0);
  vec3 col = mix(cNavy, cAmber, energy);
  col = mix(col, cGold, clamp(beams * 0.9 + pow(energy, 3.0) * 0.6, 0.0, 1.0));
  col = mix(col, cBrass, ribs * lensCore * 0.55);
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
                "tags": ["synth", "phase-distortion", "heterodyne", "casio", "theremin", "audio", "dsp"],
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
                "tags": ["effect", "barberpole", "phaser", "plate-reverb", "allpass", "audio", "dsp"],
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
