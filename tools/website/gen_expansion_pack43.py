#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 43.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (Theme: holographic_iridescent -> holographic-iridescent-foil-ribbon, holographic-iridescent-prism-blob)
2. Sketch 3D (Theme: generative_plotter -> generative-plotter-molnar-lattice-3d, generative-plotter-torus-strata-3d)
3. Field Synth (Theme: future_garage -> future-garage-sub-chord-organ-synth, future-garage-velvet-pad-drone-synth)
4. Field Effects (Theme: acid_house -> acid-house-303-resonant-filter-fx, acid-house-overdrive-tape-delay-fx)
5. Field Pixel & Embeds (Theme: ukiyoe_cartography -> ukiyoe-cartography-isobar-waves, ukiyoe-cartography-woodblock-relief)
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

TMP_DIR = "/tmp/pack43_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: holographic_iridescent)
#    Palette: #a9f0ff (ice cyan), #ffb3f5 (pastel rose), #d6ff9e (lime), #ffffff (specular), #101428 (deep indigo)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "holographic-iridescent-foil-ribbon",
        "title": "Holographic Iridescent Foil Ribbon",
        "description": "Layered chromatic foil ribbons twisting across a deep indigo field with pastel cyan, rose, and lime interference bands.",
        "tags": ["holographic", "iridescent", "foil", "ribbon", "y2k", "procedural"],
        "code": """// Holographic Iridescent Foil Ribbon - Infinite Library (Sketch 2D)
param("ribbon_count", 14, 6, 24);
param("twist_freq", 3.2, 1.0, 6.0);
param("wave_amp", 0.22, 0.06, 0.38);
param("drift_speed", 0.5, 0.0, 1.5);

function draw(t) {
  background(0.063, 0.078, 0.157);
  const w = width;
  const h = height;
  const nr = floor(ribbon_count);

  // Soft iridescent halo glow
  noStroke();
  for (let k = 0; k < 5; k++) {
    const ang = (k / 5) * Math.PI * 2.0 + t * drift_speed * 0.3;
    const gx = w * 0.5 + Math.cos(ang) * w * 0.18;
    const gy = h * 0.5 + Math.sin(ang) * h * 0.18;
    if (k % 3 === 0) fill(0.663, 0.941, 1.0, 0.12);
    else if (k % 3 === 1) fill(1.0, 0.702, 0.961, 0.12);
    else fill(0.839, 1.0, 0.620, 0.12);
    circle(gx, gy, w * 0.52);
  }

  // Twisting holographic foil ribbons
  for (let i = 0; i < nr; i++) {
    const u = i / max(1, nr - 1);
    const yBase = h * (0.14 + u * 0.72);
    const phase = t * drift_speed + i * 0.42;

    if (i % 3 === 0) {
      fill(0.663, 0.941, 1.0, 0.42);
      stroke(1.0, 1.0, 1.0, 0.85);
    } else if (i % 3 === 1) {
      fill(1.0, 0.702, 0.961, 0.42);
      stroke(0.663, 0.941, 1.0, 0.85);
    } else {
      fill(0.839, 1.0, 0.620, 0.40);
      stroke(1.0, 0.702, 0.961, 0.85);
    }
    strokeWeight(2.2);

    const segs = 64;
    const bandH = h * (0.045 + 0.025 * Math.sin(i * 1.1 + t * 0.4));
    beginShape();
    for (let s = 0; s <= segs; s++) {
      const sx = (s / segs) * w;
      const nx = s / segs;
      const dy = Math.sin(nx * Math.PI * twist_freq + phase) * h * wave_amp * 0.45
               + Math.cos(nx * Math.PI * 1.5 - phase * 0.6) * h * 0.06;
      vertex(sx, yBase + dy - bandH * 0.5);
    }
    for (let s = segs; s >= 0; s--) {
      const sx = (s / segs) * w;
      const nx = s / segs;
      const dy = Math.sin(nx * Math.PI * twist_freq + phase) * h * wave_amp * 0.45
               + Math.cos(nx * Math.PI * 1.5 - phase * 0.6) * h * 0.06;
      const pinch = 0.5 + 0.5 * Math.cos(nx * Math.PI * 2.0 + i * 0.5);
      vertex(sx, yBase + dy + bandH * (0.3 + 0.7 * pinch));
    }
    endShape(CLOSE);
  }
}
"""
    },
    {
        "slug": "holographic-iridescent-prism-blob",
        "title": "Holographic Iridescent Prism Blob",
        "description": "Morphing superellipse foil lobes with thin-film spectral contour rings and specular white highlights.",
        "tags": ["holographic", "iridescent", "prism", "superellipse", "y2k", "procedural"],
        "code": """// Holographic Iridescent Prism Blob - Infinite Library (Sketch 2D)
param("shell_count", 16, 6, 28);
param("lobe_order", 4, 2, 8);
param("warp_depth", 0.18, 0.04, 0.35);
param("spin_speed", 0.45, 0.0, 1.5);

function draw(t) {
  background(0.055, 0.067, 0.141);
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const maxR = min(w, h) * 0.43;
  const ns = floor(shell_count);
  const lo = floor(lobe_order);

  // Outer prismatic bloom rings
  for (let i = 0; i < ns; i++) {
    const u = 1.0 - i / ns;
    const rad = maxR * (0.14 + u * 0.86);
    const phase = t * spin_speed + i * 0.32;

    if (i % 4 === 0) {
      fill(0.663, 0.941, 1.0, 0.55);
      stroke(1.0, 1.0, 1.0, 0.85);
    } else if (i % 4 === 1) {
      fill(1.0, 0.702, 0.961, 0.55);
      stroke(0.663, 0.941, 1.0, 0.85);
    } else if (i % 4 === 2) {
      fill(0.839, 1.0, 0.620, 0.52);
      stroke(1.0, 0.702, 0.961, 0.85);
    } else {
      fill(0.11, 0.14, 0.28, 0.82);
      stroke(0.839, 1.0, 0.620, 0.90);
    }
    strokeWeight(2.4);

    beginShape();
    const segs = 80;
    for (let s = 0; s < segs; s++) {
      const a = (s / segs) * Math.PI * 2.0;
      const mod1 = Math.sin(a * lo + phase) * warp_depth;
      const mod2 = Math.cos(a * (lo + 1) - phase * 0.8) * (warp_depth * 0.5);
      const r = rad * (1.0 + mod1 + mod2);
      vertex(cx + Math.cos(a) * r, cy + Math.sin(a) * r);
    }
    endShape(CLOSE);
  }

  // Specular foil glints
  noStroke();
  for (let g = 0; g < 12; g++) {
    const ga = (g / 12) * Math.PI * 2.0 + t * spin_speed * 0.5;
    const gr = maxR * (0.42 + 0.32 * Math.sin(g * 1.7 + t * 0.6));
    fill(1.0, 1.0, 1.0, 0.85);
    circle(cx + Math.cos(ga) * gr, cy + Math.sin(ga) * gr, 10 + (g % 3) * 5);
  }
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: generative_plotter)
#    Palette: #f5f1e6 (archival cream), #111111 (India ink), #d1301f (vermilion)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "generative-plotter-molnar-lattice-3d",
        "title": "Generative Plotter Molnar Lattice 3D",
        "description": "Algorithmic 3D lattice of perturbed India-ink and vermilion frames inspired by Vera Molnar's Interruptions.",
        "tags": ["generative-plotter", "plotter", "molnar", "3d", "algorithmic", "minimal"],
        "code": """// Generative Plotter Molnar Lattice 3D - Infinite Library (Sketch 3D)
param("grid_n", 5, 3, 7);
param("jitter", 0.28, 0.05, 0.65);
param("step_gap", 0.44, 0.30, 0.60);
param("spin_rate", 0.25, 0.0, 1.0);

function draw(t) {
  rotateX(0.48);
  rotateY(t * spin_rate + 0.55);

  const n = floor(grid_n);
  const half = (n - 1) * 0.5;

  for (let ix = 0; ix < n; ix++) {
    for (let iy = 0; iy < n; iy++) {
      for (let iz = 0; iz < 2; iz++) {
        const px = (ix - half) * step_gap;
        const py = (iy - half) * step_gap;
        const pz = (iz - 0.5) * step_gap * 1.4;

        const seed = ix * 7.13 + iy * 13.71 + iz * 23.3;
        const rx = Math.sin(seed + t * 0.3) * jitter;
        const ry = Math.cos(seed * 1.3 - t * 0.2) * jitter;

        push();
        translate(px, py, pz);
        rotateX(rx);
        rotateY(ry);

        if ((ix + iy + iz) % 5 === 0) {
          fill(0.820, 0.188, 0.122);
        } else {
          fill(0.067, 0.067, 0.067);
        }

        // Plotter square frame built from 4 thin struts
        const s = step_gap * 0.38;
        const th = 0.024;
        push(); translate(0, s, 0); box(s * 2.0, th, th); pop();
        push(); translate(0, -s, 0); box(s * 2.0, th, th); pop();
        push(); translate(s, 0, 0); box(th, s * 2.0, th); pop();
        push(); translate(-s, 0, 0); box(th, s * 2.0, th); pop();

        pop();
      }
    }
  }
}
"""
    },
    {
        "slug": "generative-plotter-torus-strata-3d",
        "title": "Generative Plotter Torus Strata 3D",
        "description": "Stepped concentric torus and cylinder elevation strata in India ink and vermilion red evoking pen-plotter contours.",
        "tags": ["generative-plotter", "plotter", "strata", "3d", "contour", "algorithmic"],
        "code": """// Generative Plotter Torus Strata 3D - Infinite Library (Sketch 3D)
param("ring_count", 11, 5, 16);
param("tilt_amt", 0.35, 0.05, 0.75);
param("radius_span", 1.35, 0.8, 1.8);
param("spin_rate", 0.22, 0.0, 1.0);

function draw(t) {
  rotateX(0.62);
  rotateY(t * spin_rate + 0.4);

  const nr = floor(ring_count);

  // Central plotter calibration axis
  push();
  fill(0.820, 0.188, 0.122);
  cylinder(0.035, 2.2, 16);
  pop();

  for (let i = 0; i < nr; i++) {
    const u = i / max(1, nr - 1);
    const y = (u - 0.5) * 1.45;
    const rad = radius_span * (0.28 + 0.72 * Math.sin(u * Math.PI));
    const tilt = Math.sin(u * 4.5 + t * 0.4) * tilt_amt;

    push();
    translate(0, y, 0);
    rotateX(Math.PI * 0.5 + tilt);
    rotateY(u * 1.2);
    if (i % 4 === 1) {
      fill(0.820, 0.188, 0.122);
    } else {
      fill(0.067, 0.067, 0.067);
    }
    torus(rad, 0.024, 48, 10);
    pop();
  }
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: future_garage)
#    130-135 BPM, Scale 1/4, Refs: Burial, Mount Kimbie, Four Tet
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "future-garage-sub-chord-organ-synth",
        "title": "Future Garage Sub Chord Organ Synth",
        "description": "Warm detuned sine-fifth sub organ with soft vinyl-like envelope and subtle pitch drift.",
        "note_root": 48,
        "params": {
            "fifthMix": 0.45,
            "detune": 0.008,
            "warmth": 1200.0,
            "drift": 0.30,
            "decay": 0.75
        },
        "code": """param float fifthMix = 0.45 [0.0, 1.0];
param float detune = 0.008 [0.0, 0.03];
param float warmth = 1200.0 [200.0, 4000.0];
param float drift = 0.30 [0.0, 1.0];
param float decay = 0.75 [0.2, 2.0];
state float p1 = 0;
state float p2 = 0;
state float p5 = 0;
state float lfo = 0;
state float env = 0;
state float lp = 0;

target = if(gate > 0.5, 1.0, 0.0);
decayRate = clamp(1.0 / (sr * decay), 0.0001, 0.03);
env = if(target > env, env + (target - env) * 0.05, env - env * decayRate);

lfo = (lfo + 0.75 / sr) % 1.0;
wow = 1.0 + sin(6.2831853 * lfo) * drift * 0.006;

f0 = clamp(freq * wow, 35.0, 2400.0);
p1 = (p1 + f0 * (1.0 - detune) / sr) % 1.0;
p2 = (p2 + f0 * (1.0 + detune) / sr) % 1.0;
p5 = (p5 + f0 * 1.4983 / sr) % 1.0;

organ = (sin(6.2831853 * p1) + sin(6.2831853 * p2)) * 0.5 + sin(6.2831853 * p5) * fifthMix * 0.6;
fCut = clamp(warmth / sr * 3.14, 0.01, 0.44);
lp = lp + fCut * (organ - lp);

out = lp * env * 0.72;
"""
    },
    {
        "slug": "future-garage-velvet-pad-drone-synth",
        "title": "Future Garage Velvet Pad Drone Synth",
        "description": "Shimmering dual-oscillator velvet pad with slow LFO filter sweep and gentle harmonic overtones.",
        "note_root": 55,
        "params": {
            "detune": 0.013,
            "cutoff": 1400.0,
            "sweep": 0.45,
            "shimmer": 0.35,
            "decay": 0.95
        },
        "code": """param float detune = 0.013 [0.0, 0.035];
param float cutoff = 1400.0 [250.0, 4200.0];
param float sweep = 0.45 [0.0, 1.0];
param float shimmer = 0.35 [0.0, 1.0];
param float decay = 0.95 [0.25, 2.5];
state float p1 = 0;
state float p2 = 0;
state float pOct = 0;
state float lfo = 0;
state float env = 0;
state float lp1 = 0;
state float lp2 = 0;

target = if(gate > 0.5, 1.0, 0.0);
decayRate = clamp(1.0 / (sr * decay), 0.0001, 0.025);
env = if(target > env, env + (target - env) * 0.04, env - env * decayRate);

lfo = (lfo + 0.45 / sr) % 1.0;
f0 = clamp(freq, 40.0, 2600.0);
p1 = (p1 + f0 * (1.0 - detune) / sr) % 1.0;
p2 = (p2 + f0 * (1.0 + detune) / sr) % 1.0;
pOct = (pOct + f0 * 2.002 / sr) % 1.0;

saw1 = p1 * 2.0 - 1.0;
saw2 = p2 * 2.0 - 1.0;
triOct = 1.0 - 4.0 * abs(pOct - 0.5);
raw = (saw1 + saw2) * 0.35 + triOct * shimmer * 0.35;

modCut = cutoff * (1.0 + sin(6.2831853 * lfo) * sweep * 0.45);
fCut = clamp(modCut / sr * 3.14, 0.01, 0.44);
lp1 = lp1 + fCut * (raw - lp1);
lp2 = lp2 + fCut * (lp1 - lp2);

out = lp2 * env * 0.55;
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: acid_house)
#    120-130 BPM, Refs: Phuture, Roland TB-303, Hardfloor, Josh Wink
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "acid-house-303-resonant-filter-fx",
        "title": "Acid House 303 Resonant Filter FX",
        "description": "Sweeping resonant diode-style lowpass filter with soft waveshaped drive and LFO cutoff modulation.",
        "params": {
            "cutoff": 950.0,
            "resonance": 0.55,
            "lfoRate": 0.65,
            "drive": 1.6,
            "mix": 0.80
        },
        "code": """param float cutoff = 950.0 [180.0, 4200.0];
param float resonance = 0.55 [0.0, 0.85];
param float lfoRate = 0.65 [0.1, 4.0];
param float drive = 1.6 [0.5, 4.0];
param float mix = 0.80 [0.0, 1.0];
state float lfo = 0;
state float lp1 = 0;
state float lp2 = 0;

lfo = (lfo + lfoRate / sr) % 1.0;
mod = 1.0 + 0.55 * sin(6.2831853 * lfo);
fCut = clamp((cutoff * mod) / sr * 3.14, 0.02, 0.45);

fb = clamp(lp2 * resonance * 1.35, -0.95, 0.95);
driven = in * drive - fb;
sat = driven / (1.0 + abs(driven));

lp1 = lp1 + fCut * (sat - lp1);
lp2 = lp2 + fCut * (lp1 - lp2);

out = clamp(in * (1.0 - mix) + lp2 * mix * 1.15, -0.92, 0.92);
"""
    },
    {
        "slug": "acid-house-overdrive-tape-delay-fx",
        "title": "Acid House Overdrive Tape Delay FX",
        "description": "Punchy saturated analog delay with warm feedback damping and rhythmic echo smear.",
        "params": {
            "feedback": 0.52,
            "damping": 0.45,
            "drive": 1.8,
            "mix": 0.55
        },
        "code": """param float feedback = 0.52 [0.0, 0.85];
param float damping = 0.45 [0.1, 0.95];
param float drive = 1.8 [0.5, 4.0];
param float mix = 0.55 [0.0, 1.0];
state float d1 = 0;
state float d2 = 0;
state float lp = 0;

x = in * drive;
sat = x / (1.0 + abs(x));

d1 = d1 + 0.14 * (sat + d2 * feedback - d1);
d2 = d2 + 0.09 * (d1 - d2);

fCut = clamp(1.0 - damping * 0.72, 0.08, 0.85);
lp = lp + fCut * (d2 - lp);

out = clamp((in * (1.0 - mix) + lp * mix * 1.15) * 0.62, -0.92, 0.92);
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel & Embeds Sources (Theme: ukiyoe_cartography)
#    Palette: #1d3557 (indigo ink), #e63946 (vermilion seal), #f1faee (rice paper), #a8dadc (celadon mist)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "ukiyoe-cartography-isobar-waves",
        "title": "Ukiyo-e Cartography Isobar Waves",
        "description": "Woodblock-inspired topographical wave contours in Prussian indigo and celadon mist over warm rice paper with a vermilion seal accent.",
        "embed_desc": "Audio-reactive Edo-period woodblock wave cartography. Bass ripples the isobar contours, mids drift the celadon mist, and highs sharpen the paper grain.",
        "tags": ["ukiyoe", "cartography", "woodblock", "indigo", "waves", "shader", "webgl", "audio-reactive"],
        "params": {
            "contourFreq": 14.0,
            "waveAmp": 0.16,
            "paperGrain": 0.04,
            "sealRadius": 0.11,
            "contrast": 1.06
        },
        "code": """param float contourFreq = 14.0 [6.0, 28.0];
param float waveAmp = 0.16 [0.04, 0.35];
param float paperGrain = 0.04 [0.0, 0.12];
param float sealRadius = 0.11 [0.05, 0.22];
param float contrast = 1.06 [0.8, 1.4];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0;

# Ukiyo-e palette: Indigo (#1d3557), Celadon (#a8dadc), Rice paper (#f1faee), Vermilion (#e63946)
cIndigo = vec3(0.114, 0.208, 0.341);
cCeladon = vec3(0.659, 0.855, 0.863);
cPaper = vec3(0.945, 0.933, 0.890);
cSeal = vec3(0.902, 0.224, 0.275);

# Rice paper grain
grain = (fract(sin(dot(uv * res, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * paperGrain;

# Topographical wave heightfield
w1 = sin(p.x * 2.8 + t * 0.4) * cos(p.y * 2.4 - t * 0.25) * waveAmp;
w2 = sin((p.x + p.y) * 4.2 - t * 0.3) * (waveAmp * 0.45);
h = p.y * 0.5 + w1 + w2;

# Woodblock contour bands
iso = abs(fract(h * contourFreq) - 0.5);
lineMask = 1.0 - smoothstep(0.06, 0.14, iso);
mistMask = smoothstep(-0.25, 0.35, h);

baseCol = mix(cCeladon, cPaper, mistMask);
baseCol = mix(baseCol, cIndigo, lineMask * 0.88);

# Red hanko cartographer seal in upper right
sealPos = p - vec2(0.58, 0.56);
sealBox = max(abs(sealPos.x), abs(sealPos.y));
inSeal = 1.0 - smoothstep(sealRadius - 0.01, sealRadius + 0.01, sealBox);
baseCol = mix(baseCol, cSeal, inSeal * 0.92);

col = clamp((baseCol + grain) * contrast, 0.0, 1.0);
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

  float b = audio_bass * 0.4;
  float m = audio_mid * 0.4;
  float h = audio_high * 0.4;

  vec3 cIndigo = vec3(0.114, 0.208, 0.341);
  vec3 cCeladon = vec3(0.659, 0.855, 0.863);
  vec3 cPaper = vec3(0.945, 0.933, 0.890);
  vec3 cSeal = vec3(0.902, 0.224, 0.275);

  float grain = (fract(sin(dot(uv * res, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * (0.04 + h * 0.08);

  float waveAmp = 0.16 + b * 0.22;
  float w1 = sin(p.x * 2.8 + t * 0.4) * cos(p.y * 2.4 - t * 0.25) * waveAmp;
  float w2 = sin((p.x + p.y) * 4.2 - t * 0.3 + m * 2.0) * (waveAmp * 0.45);
  float hf = p.y * 0.5 + w1 + w2;

  float iso = abs(fract(hf * 14.0) - 0.5);
  float lineMask = 1.0 - smoothstep(0.06, 0.14, iso);
  float mistMask = smoothstep(-0.25, 0.35, hf);

  vec3 baseCol = mix(cCeladon, cPaper, mistMask);
  baseCol = mix(baseCol, cIndigo, lineMask * 0.88);

  vec2 sealPos = p - vec2(0.58, 0.56);
  float sealBox = max(abs(sealPos.x), abs(sealPos.y));
  float inSeal = 1.0 - smoothstep(0.10, 0.12, sealBox);
  baseCol = mix(baseCol, cSeal, inSeal * 0.92);

  vec3 col = clamp((baseCol + grain) * 1.06, 0.0, 1.0);
  gl_FragColor = vec4(col, 1.0);
}
"""
    },
    {
        "slug": "ukiyoe-cartography-woodblock-relief",
        "title": "Ukiyo-e Cartography Woodblock Relief",
        "description": "Layered Edo-era archipelago elevation strata with woodblock grain, directional relief, and vermilion cartographic markers.",
        "embed_desc": "Audio-reactive woodblock archipelago relief map. Bass swells the island elevation contours, mids sweep the compass meridians, and highs crisp the woodblock ink.",
        "tags": ["ukiyoe", "cartography", "woodblock", "relief", "archipelago", "shader", "webgl", "audio-reactive"],
        "params": {
            "strataCount": 10.0,
            "islandScale": 2.4,
            "grainAmt": 0.045,
            "ringDensity": 12.0,
            "contrast": 1.08
        },
        "code": """param float strataCount = 10.0 [4.0, 20.0];
param float islandScale = 2.4 [1.0, 5.0];
param float grainAmt = 0.045 [0.0, 0.12];
param float ringDensity = 12.0 [4.0, 24.0];
param float contrast = 1.08 [0.8, 1.4];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0;

cIndigo = vec3(0.114, 0.208, 0.341);
cCeladon = vec3(0.659, 0.855, 0.863);
cPaper = vec3(0.945, 0.933, 0.890);
cSeal = vec3(0.902, 0.224, 0.275);

grain = (fract(sin(dot(uv * res, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * grainAmt;

r = length(p);
nr = max(r, 0.001);
nx = p.x / nr;
ny = p.y / nr;

# Archipelago elevation field
elev = exp(-r * r * 1.4) + 0.35 * sin(p.x * islandScale + t * 0.2) * cos(p.y * islandScale - t * 0.15);
stepped = floor(elev * strataCount) / strataCount;
contour = 1.0 - smoothstep(0.04, 0.11, abs(fract(elev * strataCount) - 0.5));

# 8-point compass meridian rays
crossAxis = min(abs(nx), abs(ny));
diagAxis = abs(abs(nx) - abs(ny)) * 0.7071;
meridian = 1.0 - smoothstep(0.015, 0.035, min(crossAxis, diagAxis));

baseCol = mix(cPaper, cCeladon, clamp(stepped * 1.1, 0.0, 1.0));
baseCol = mix(baseCol, cIndigo, contour * 0.85);
baseCol = mix(baseCol, cIndigo, meridian * 0.25);

# Vermilion summit ring
summitRing = 1.0 - smoothstep(0.012, 0.028, abs(r - 0.24));
baseCol = mix(baseCol, cSeal, summitRing * 0.9);

col = clamp((baseCol + grain) * contrast, 0.0, 1.0);
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

  float b = audio_bass * 0.4;
  float m = audio_mid * 0.4;
  float h = audio_high * 0.4;

  vec3 cIndigo = vec3(0.114, 0.208, 0.341);
  vec3 cCeladon = vec3(0.659, 0.855, 0.863);
  vec3 cPaper = vec3(0.945, 0.933, 0.890);
  vec3 cSeal = vec3(0.902, 0.224, 0.275);

  float grain = (fract(sin(dot(uv * res, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * (0.045 + h * 0.08);

  float r = length(p);
  float ang = atan(p.y, p.x);

  float elev = exp(-r * r * (1.4 - b * 0.4)) + 0.35 * sin(p.x * 2.4 + t * 0.2) * cos(p.y * 2.4 - t * 0.15);
  float stepped = floor(elev * 10.0) / 10.0;
  float contour = 1.0 - smoothstep(0.04, 0.11, abs(fract(elev * 10.0) - 0.5));
  float meridian = 1.0 - smoothstep(0.015, 0.035, abs(sin(ang * 8.0 + t * 0.1 + m * 1.5)));

  vec3 baseCol = mix(cPaper, cCeladon, clamp(stepped * 1.1, 0.0, 1.0));
  baseCol = mix(baseCol, cIndigo, contour * 0.85);
  baseCol = mix(baseCol, cIndigo, meridian * 0.25);

  float summitRing = 1.0 - smoothstep(0.012, 0.028, abs(r - (0.24 + b * 0.08)));
  baseCol = mix(baseCol, cSeal, summitRing * 0.9);

  vec3 col = clamp((baseCol + grain) * 1.08, 0.0, 1.0);
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
  c bg 0.961 0.945 0.902
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
  i note2 {nr+3}
  i note3 {nr+7}
  i note4 {nr}
  i note5 {nr+3}
  i note6 {nr+10}
  i note7 {nr+7}
  i note8 {nr}
  i note9 {nr}
  i note10 {nr+3}
  i note11 {nr+5}
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
  c fillColor 0.22 0.24 0.38
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
  f rateBeats 0.25
  i steps 16
  i note0 38
  i note1 38
  i note2 50
  i note3 45
  i note4 38
  i note5 39
  i note6 48
  i note7 38
  i note8 38
  i note9 38
  i note10 50
  i note11 45
  i note12 38
  i note13 50
  i note14 43
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
  id osc
  pos 400 1024
  f cutoff 2800.0
  f resonance 0.25
  f decay 0.35
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
  c fillColor 0.22 0.30 0.20
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
                "tags": ["synth", "future-garage", "audio", "dsp", "organ", "pad"],
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
                "tags": ["effect", "acid-house", "audio", "dsp", "filter", "delay"],
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
