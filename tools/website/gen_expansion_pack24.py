#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 24.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (op-art-undulating-moire-lattice, op-art-concentric-vibration-field)
2. Sketch 3D (neo-brutalist-monolithic-pylon-3d, neo-brutalist-cantilever-plinth-3d)
3. Field Synth (minimal-techno-sine-core-sub-synth, minimal-techno-transient-chirp-synth)
4. Field Effects (idm-algorithmic-comb-resonator-fx, idm-pitch-warp-granular-delay-fx)
5. Field Pixel (ditherpunk-bayer-matrix-threshold, ditherpunk-cyber-glyph-lattice)
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

TMP_DIR = "/tmp/pack24_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: op_art)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "op-art-undulating-moire-lattice",
        "title": "Op-Art Undulating Moiré Lattice",
        "description": "Optical illusion kinetic moiré lattice with undulating sine-phase contour lines, high-frequency grid interference, and visceral retinal vibration.",
        "tags": ["op-art", "moire", "kinetic", "optical", "lattice", "drawing"],
        "code": """// Op-Art Undulating Moiré Lattice - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Optical illusion kinetic moiré lattice with undulating sine-phase contour lines, high-frequency grid interference, and visceral retinal vibration.
param("grid_density", 52.0, 24.0, 80.0);
param("undulation_amp", 36.0, 10.0, 70.0);
param("phase_speed", 0.5, 0.1, 1.5);
param("retinal_contrast", 0.95, 0.5, 1.0);

function draw(t) {
  // Alabaster white paper ground
  background(0.965, 0.965, 0.965);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.90;

  push();
  translate(cx, cy);

  // Outer framing rule
  stroke(0.04, 0.04, 0.05);
  strokeWeight(2.5);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  // Inner margin border
  stroke(0.15, 0.15, 0.18, 0.5);
  strokeWeight(1.0);
  rect(-span * 0.46, -span * 0.46, span * 0.92, span * 0.92);

  const lines = floor(grid_density);
  const time = t * phase_speed;

  // Primary undulating horizontal contour lines
  stroke(0.04, 0.04, 0.05, retinal_contrast);
  noFill();

  const wHalf = span * 0.44;
  for (let i = -lines / 2; i <= lines / 2; i++) {
    const yNorm = i / (lines * 0.5);
    const yBase = yNorm * (span * 0.44);
    const thick = 1.2 + 2.4 * pow(cos(yNorm * Math.PI * 0.5), 2.0);
    strokeWeight(thick);

    beginShape();
    for (let x = -wHalf; x <= wHalf; x += 14) {
      const d1 = dist(x, yBase, -span * 0.20, 0);
      const d2 = dist(x, yBase, span * 0.20, 0);
      const bulge1 = exp(-d1 * 0.006) * sin(d1 * 0.04 - time * 2.0) * undulation_amp;
      const bulge2 = exp(-d2 * 0.006) * cos(d2 * 0.04 + time * 1.8) * undulation_amp;
      const yWarp = yBase + bulge1 - bulge2;
      vertex(x, yWarp);
    }
    endShape();
  }

  // Secondary intersecting vertical interference grid causing moiré diamonds
  const vLines = floor(lines * 0.65);
  for (let j = -vLines / 2; j <= vLines / 2; j++) {
    const xNorm = j / (vLines * 0.5);
    const xBase = xNorm * (span * 0.44);
    const vThick = j % 2 === 0 ? 1.8 : 0.8;
    stroke(0.04, 0.04, 0.05, 0.75 * retinal_contrast);
    strokeWeight(vThick);

    beginShape();
    for (let y = -span * 0.44; y <= span * 0.44; y += 18) {
      const dCenter = dist(xBase, y, 0, 0);
      const xBulge = exp(-dCenter * 0.005) * sin(y * 0.03 + time * 1.5) * (undulation_amp * 0.6);
      vertex(xBase + xBulge, y);
    }
    endShape();
  }

  // Central focal oval aperture with inverted concentric rings
  push();
  noFill();
  stroke(0.04, 0.04, 0.05);
  for (let k = 1; k <= 9; k++) {
    strokeWeight(k % 2 === 0 ? 2.8 : 1.4);
    const rw = span * (0.04 * k);
    const rh = rw * 0.72;
    ellipse(0, 0, rw * 2.0, rh * 2.0);
  }
  pop();

  // Corner registration optical target ticks
  stroke(0.04, 0.04, 0.05);
  strokeWeight(2.0);
  const cr = span * 0.46;
  for (let signX of [-1, 1]) {
    for (let signY of [-1, 1]) {
      const kx = signX * cr;
      const ky = signY * cr;
      line(kx, ky, kx - signX * 22, ky);
      line(kx, ky, kx, ky - signY * 22);
    }
  }

  pop();
}
"""
    },
    {
        "slug": "op-art-concentric-vibration-field",
        "title": "Op-Art Concentric Vibration Field",
        "description": "Hypnotic concentric geometric ellipse field with alternating phase rings, logarithmic radial spokes, and pulsating retinal contrast shifts.",
        "tags": ["op-art", "concentric", "vibration", "optical", "geometric", "drawing"],
        "code": """// Op-Art Concentric Vibration Field - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Hypnotic concentric geometric ellipse field with alternating phase rings, logarithmic radial spokes, and pulsating retinal contrast shifts.
param("ring_count", 32.0, 16.0, 52.0);
param("vibration_freq", 14.0, 6.0, 24.0);
param("aspect_warp", 0.78, 0.4, 1.2);
param("contrast", 0.96, 0.5, 1.0);

function draw(t) {
  // Alabaster white ground
  background(0.965, 0.965, 0.965);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;

  push();
  translate(cx, cy);

  // Outer framing boundary
  stroke(0.04, 0.04, 0.05);
  strokeWeight(2.5);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  const numRings = floor(ring_count);
  const maxR = span * 0.45;

  // Alternating concentric undulating bands
  for (let i = numRings; i >= 1; i--) {
    const u = i / numRings;
    const rBase = maxR * u;
    const isDark = i % 2 === 0;

    if (isDark) {
      fill(0.04, 0.04, 0.05, contrast);
    } else {
      fill(0.965, 0.965, 0.965);
    }
    stroke(0.04, 0.04, 0.05);
    strokeWeight(1.5);

    beginShape();
    const steps = 72;
    for (let s = 0; s <= steps; s++) {
      const ang = (s * Math.PI * 2.0) / steps;
      const wave = sin(ang * vibration_freq + t * 2.5 + i * 0.4) * (span * 0.014 * (1.0 - u * 0.4));
      const rad = rBase + wave;
      const rx = cos(ang) * rad;
      const ry = sin(ang) * rad * aspect_warp;
      vertex(rx, ry);
    }
    endShape(CLOSE);
  }

  // Radiating logarithmic spoke sectors slicing the rings
  stroke(0.965, 0.965, 0.965, 0.85);
  strokeWeight(2.2);
  const spokes = 36;
  for (let s = 0; s < spokes; s++) {
    const a = (s * Math.PI * 2.0) / spokes + t * 0.3;
    const r1 = span * 0.08;
    const r2 = maxR * 0.96;
    line(cos(a) * r1, sin(a) * r1 * aspect_warp, cos(a) * r2, sin(a) * r2 * aspect_warp);
  }

  // Central pulsating black pupil core
  fill(0.04, 0.04, 0.05);
  noStroke();
  circle(0, 0, span * 0.10);

  // Central white needle point
  fill(0.965, 0.965, 0.965);
  circle(0, 0, span * 0.024);

  // Outer framing alignment crosses
  stroke(0.04, 0.04, 0.05);
  strokeWeight(1.8);
  for (let k = 0; k < 4; k++) {
    const ang = k * Math.PI * 0.5;
    const d = maxR * 0.98;
    const x = cos(ang) * d;
    const y = sin(ang) * d;
    line(x - 8, y, x + 8, y);
    line(x, y - 8, x, y + 8);
  }

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: neo_brutalist)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "neo-brutalist-monolithic-pylon-3d",
        "title": "Neo-Brutalist Monolithic Pylon 3D",
        "description": "Stark architectural composition featuring heavy chamfered concrete pylons with bold ultramarine accents, deep cast shadows, and cantilevered slabs.",
        "tags": ["neo-brutalist", "monolith", "pylon", "architecture", "concrete", "3d"],
        "code": """// Neo-Brutalist Monolithic Pylon 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Stark architectural composition featuring heavy chamfered concrete pylons with bold ultramarine accents, deep cast shadows, and cantilevered slabs.
param("pylon_scale", 1.0, 0.5, 2.0);
param("cantilever_span", 1.25, 0.5, 2.0);
param("rotation_speed", 0.35, 0.05, 1.0);
param("fluting_count", 5.0, 3.0, 8.0);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.46 + sin(time * 0.2) * 0.04);
  rotateY(time * 0.3 + 0.65);

  // Stepped foundation plinth in raw cast concrete
  push();
  translate(0, 0.88, 0);
  fill(0.78, 0.77, 0.74);
  box(2.2 * pylon_scale, 0.16, 2.2 * pylon_scale);
  pop();

  push();
  translate(0, 0.72, 0);
  fill(0.18, 0.19, 0.22); // Deep graphite shadow tier
  box(1.8 * pylon_scale, 0.16, 1.8 * pylon_scale);
  pop();

  // Central monolithic vertical pylon tower
  push();
  translate(0, -0.05, 0);
  fill(0.85, 0.84, 0.81);
  box(0.75 * pylon_scale, 1.4 * pylon_scale, 0.75 * pylon_scale);
  pop();

  // Fluting vertical slabs attached to the pylon
  const flutes = floor(fluting_count);
  for (let i = 0; i < flutes; i++) {
    const xOff = (i - (flutes - 1) * 0.5) * 0.15 * pylon_scale;
    push();
    translate(xOff, -0.05, 0.40 * pylon_scale);
    fill(i % 2 === 0 ? 0.72 : 0.88, i % 2 === 0 ? 0.71 : 0.87, i % 2 === 0 ? 0.68 : 0.84);
    box(0.08 * pylon_scale, 1.35 * pylon_scale, 0.12 * pylon_scale);
    pop();
  }

  // Heavy projecting cantilever slab in electric ultramarine
  push();
  translate(0.35 * cantilever_span, -0.42, 0);
  fill(0.0, 0.22, 0.95); // Iconic Neo-Brutalist Ultramarine punch
  box(1.4 * cantilever_span, 0.22, 0.85 * pylon_scale);
  pop();

  // Secondary cantilever slab in industrial charcoal
  push();
  translate(-0.40 * cantilever_span, 0.18, 0.12);
  fill(0.14, 0.15, 0.18);
  box(1.2 * cantilever_span, 0.20, 0.75 * pylon_scale);
  pop();

  // Top capping cornice beam with hazard orange datum accent
  push();
  translate(0, -0.80 * pylon_scale, 0);
  fill(1.0, 0.22, 0.0); // Industrial hazard orange stripe
  box(1.1 * pylon_scale, 0.08, 1.1 * pylon_scale);
  pop();

  push();
  translate(0, -0.88 * pylon_scale, 0);
  fill(0.85, 0.84, 0.81);
  box(1.2 * pylon_scale, 0.10, 1.2 * pylon_scale);
  pop();

  pop();
}
"""
    },
    {
        "slug": "neo-brutalist-cantilever-plinth-3d",
        "title": "Neo-Brutalist Cantilever Plinth 3D",
        "description": "Monumental stepped cantilever plinths in raw cast stone, framed with industrial hazard orange datum lines and stark geometric projections.",
        "tags": ["neo-brutalist", "cantilever", "plinth", "monumental", "geometric", "3d"],
        "code": """// Neo-Brutalist Cantilever Plinth 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Monumental stepped cantilever plinths in raw cast stone, framed with industrial hazard orange datum lines and stark geometric projections.
param("tier_count", 6.0, 3.0, 10.0);
param("overhang_shift", 0.32, 0.1, 0.6);
param("plinth_width", 1.4, 0.6, 2.2);
param("rotation_speed", 0.38, 0.05, 1.2);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.48 + sin(time * 0.18) * 0.05);
  rotateY(time * 0.32 + 0.60);

  // Stepped foundation base
  push();
  translate(0, 0.92, 0);
  fill(0.18, 0.19, 0.22);
  box(2.4, 0.14, 2.4);
  pop();

  push();
  translate(0, 0.78, 0);
  fill(0.88, 0.87, 0.84);
  box(2.0, 0.14, 2.0);
  pop();

  const tiers = 6;
  const hTier = 0.22;
  for (let i = 0; i < tiers; i++) {
    const y = 0.58 - i * hTier * 1.15;
    const sign = (i % 2 === 0 ? 1 : -1);
    const xOff = sign * (i * overhang_shift * 0.42);
    const zOff = ((i % 3) - 1) * (i * overhang_shift * 0.28);
    const w = plinth_width - i * 0.08;

    push();
    translate(xOff, y, zOff);

    if (i === 1) {
      fill(1.0, 0.20, 0.0); // Hazard orange accent slab
    } else if (i === 3) {
      fill(0.0, 0.22, 0.95); // Ultramarine slab
    } else if (i % 2 === 0) {
      fill(0.92, 0.91, 0.88); // Crisp bright stone
    } else {
      fill(0.22, 0.23, 0.28); // Shadow graphite
    }
    box(w, hTier, w * 0.85);

    // Contrasting structural ribs
    for (let r = -2; r <= 2; r++) {
      push();
      translate(r * 0.22, 0, w * 0.44);
      fill(i % 2 === 0 ? 0.20 : 0.90, i % 2 === 0 ? 0.21 : 0.89, i % 2 === 0 ? 0.25 : 0.86);
      box(0.05, hTier * 1.05, 0.08);
      pop();
    }

    pop();
  }

  // Flanking vertical pylon
  push();
  translate(-plinth_width * 0.85, 0.0, -0.3);
  fill(0.88, 0.87, 0.84);
  box(0.24, 1.8, 0.65);
  pop();

  // Top cantilever cornice with orange datum
  push();
  translate(0.35, -0.85, 0);
  fill(1.0, 0.20, 0.0);
  box(1.2, 0.08, 0.9);
  pop();

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: minimal_techno)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "minimal-techno-sine-core-sub-synth",
        "title": "Minimal Techno Sine Core Sub Synth",
        "description": "Deep surgical sine-core sub synthesizer with micro-tuned transient punch, subtle saturation drive, and rapid exponential low-end decay.",
        "tags": ["synth", "minimal", "techno", "sub", "sine", "dsp"],
        "note_root": 36,
        "params": {
            "pitchMod": 0.55,
            "pitchDecay": 0.04,
            "subDecay": 0.32,
            "drive": 0.35,
            "tone": 0.45
        },
        "code": """param float pitchMod = 0.55 [0.0, 1.0]
param float pitchDecay = 0.04 [0.01, 0.2]
param float subDecay = 0.32 [0.08, 0.9]
param float drive = 0.35 [0.0, 1.0]
param float tone = 0.45 [0.1, 1.0]
state float p1 = 0
state float pSub = 0
state float env = 0
state float pEnv = 0
state float lp1 = 0
state float lp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * subDecay), 0.00008, 0.02)
pDecayRate = clamp(1.0 / (sr * pitchDecay), 0.0004, 0.06)

env = if(target > env, env + (target - env) * 0.45, env - env * decayRate)
pEnv = if(target > pEnv, pEnv + (target - pEnv) * 0.65, pEnv - pEnv * pDecayRate)

fBase = clamp(freq, 28.0, 320.0)
fEff = fBase * (1.0 + pEnv * pitchMod * 3.5)

dp1 = fEff / sr
p1 = p1 + dp1
p1 = if(p1 >= 1.0, p1 - 1.0, p1)
sineCore = sin(p1 * 6.2831853)

dpSub = (fBase * 0.5) / sr
pSub = pSub + dpSub
pSub = if(pSub >= 1.0, pSub - 1.0, pSub)
subSquare = if(pSub < 0.5, 0.4, -0.4)

raw = sineCore + subSquare * 0.35
drv = raw * (1.0 + drive * 1.5)
driven = (drv / (1.0 + abs(drv))) * env

fCut = clamp(tone * 0.35, 0.01, 0.4)
lp1 = lp1 + fCut * (driven - lp1)
lp2 = lp2 + fCut * (lp1 - lp2)

out = lp2 * 0.52
"""
    },
    {
        "slug": "minimal-techno-transient-chirp-synth",
        "title": "Minimal Techno Transient Chirp Synth",
        "description": "Percussive minimal techno modulation synthesizer with snappy envelope pitch chirps, resonant bandpass click, and tight metallic body.",
        "tags": ["synth", "minimal", "techno", "chirp", "percussive", "dsp"],
        "note_root": 48,
        "params": {
            "chirpDepth": 0.65,
            "clickTone": 2400.0,
            "bodyDecay": 0.24,
            "metallicMod": 0.40,
            "subPunch": 0.50
        },
        "code": """param float chirpDepth = 0.65 [0.1, 1.2]
param float clickTone = 2400.0 [600.0, 6000.0]
param float bodyDecay = 0.24 [0.06, 0.7]
param float metallicMod = 0.40 [0.0, 1.0]
param float subPunch = 0.50 [0.0, 1.0]
state float pCar = 0
state float pMod = 0
state float env = 0
state float pSnap = 0
state float bp1 = 0
state float bp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
envRate = clamp(1.0 / (sr * bodyDecay), 0.0001, 0.03)
snapRate = clamp(1.0 / (sr * 0.025), 0.0008, 0.08)

env = if(target > env, env + (target - env) * 0.50, env - env * envRate)
pSnap = if(target > pSnap, pSnap + (target - pSnap) * 0.75, pSnap - pSnap * snapRate)

fBase = clamp(freq, 40.0, 800.0)
fSnap = fBase * (1.0 + pSnap * chirpDepth * 4.0)

dpMod = (fBase * 2.76) / sr
pMod = pMod + dpMod
pMod = if(pMod >= 1.0, pMod - 1.0, pMod)
modSig = sin(pMod * 6.2831853) * metallicMod * 0.4

dpCar = fSnap / sr
pCar = pCar + dpCar + modSig
pCar = if(pCar >= 1.0, pCar - 1.0, pCar)
carSig = sin(pCar * 6.2831853)

subSig = sin(pCar * 3.14159265) * subPunch * 0.5
raw = (carSig + subSig) * env

fBP = clamp((clickTone / sr) * 1.5, 0.02, 0.45)
bp1 = bp1 + fBP * (raw - bp1)
bp2 = bp2 + fBP * (bp1 - bp2)
shaped = (bp1 - bp2) * 2.2 + raw * 0.4

out = clamp(shaped * 0.28, -0.65, 0.65)
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: idm)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "idm-algorithmic-comb-resonator-fx",
        "title": "IDM Algorithmic Comb Resonator FX",
        "description": "Intricate multi-tap comb resonator with tuned feedback damping, microtonal metallic resonances, and non-linear harmonic saturation.",
        "tags": ["effect", "idm", "comb", "resonator", "delay", "audio"],
        "params": {
            "resonance": 0.68,
            "damping": 3200.0,
            "microTune": 0.45,
            "mix": 0.62
        },
        "code": """param float resonance = 0.68 [0.1, 0.92]
param float damping = 3200.0 [800.0, 7000.0]
param float microTune = 0.45 [0.0, 1.0]
param float mix = 0.62 [0.0, 1.0]
state float c1 = 0
state float c2 = 0
state float c3 = 0
state float c4 = 0
state float lp = 0

fDamp = clamp(damping / sr * 2.0, 0.01, 0.42)
lp = lp + fDamp * (in - lp)

fb = clamp(resonance, 0.1, 0.88)
tune = 1.0 + microTune * 0.25

c1 = c1 + 0.18 * (lp + c4 * fb * 0.75 - c1)
c2 = c2 + 0.14 * (c1 * tune + c3 * fb * 0.65 - c2)
c3 = c3 + 0.11 * (c2 * tune + c1 * fb * 0.55 - c3)
c4 = c4 + 0.08 * (c3 * tune - c4)

resonated = (c1 + c2 * 0.8 - c3 * 0.6 + c4 * 0.5) * 0.35
sat = clamp(resonated * 1.2, -0.85, 0.85)

out = in * (1.0 - mix) + sat * mix
"""
    },
    {
        "slug": "idm-pitch-warp-granular-delay-fx",
        "title": "IDM Pitch Warp Granular Delay FX",
        "description": "Micro-modulated granular delay with pitch-warping buffer sweeps, asymmetric waveshaping distortion, and diffuse spatial feedback.",
        "tags": ["effect", "idm", "delay", "granular", "pitch", "audio"],
        "params": {
            "warpRate": 1.4,
            "delayFeedback": 0.58,
            "drive": 0.38,
            "mix": 0.65
        },
        "code": """param float warpRate = 1.4 [0.2, 4.0]
param float delayFeedback = 0.58 [0.1, 0.88]
param float drive = 0.38 [0.0, 1.0]
param float mix = 0.65 [0.0, 1.0]
state float ph = 0
state float d1 = 0
state float d2 = 0
state float d3 = 0
state float lp = 0

ph = ph + warpRate / sr
ph = if(ph >= 1.0, ph - 1.0, ph)
warpMod = sin(ph * 6.2831853) * 0.08

fb = clamp(delayFeedback, 0.1, 0.86)
fCut = clamp(0.22 + warpMod * 0.05, 0.02, 0.40)
lp = lp + fCut * (in - lp)

d1 = d1 + (0.16 + warpMod) * (lp + d3 * fb - d1)
d2 = d2 + (0.12 - warpMod) * (d1 * 0.8 + d2 * fb * 0.4 - d2)
d3 = d3 + 0.09 * (d2 * 0.85 - d3)

gran = (d1 + d2 * 0.7 - d3 * 0.5) * 1.2
sat = clamp(gran * 1.5, -0.85, 0.85)

out = in * (1.0 - mix) + sat * mix
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embeds (Theme: ditherpunk)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "ditherpunk-bayer-matrix-threshold",
        "title": "1-Bit Ditherpunk Bayer Matrix Threshold",
        "description": "High-contrast 1-bit Bayer ordered dither shader with audio-reactive threshold modulation, concentric cyber-glyphs, and duo-tone palette.",
        "embed_desc": "High-contrast 1-bit Bayer ordered dither shader with audio-reactive threshold modulation, sacred cyber-glyph lattices, and duo-tone Klein blue and ivory palette. Bass drives radial shockwave distortion, mids rotate the Bayer dither matrix, and highs scatter binary grain.",
        "tags": ["pixel", "ditherpunk", "1bit", "bayer", "shader"],
        "params": {
            "matrixScale": 3.0,
            "glyphRings": 6.0,
            "warpSpeed": 0.75,
            "thresholdOffset": 0.04,
            "edgeContrast": 1.25
        },
        "code": """param float matrixScale = 3.0 [1.0, 8.0]
param float glyphRings = 6.0 [2.0, 14.0]
param float warpSpeed = 0.75 [0.1, 2.5]
param float thresholdOffset = 0.04 [-0.4, 0.4]
param float edgeContrast = 1.25 [0.5, 2.5]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2
time = t * warpSpeed

r = length(p)
ang = atan2(p.y, p.x)

warp = sin(r * 10.0 - time * 2.0) * 0.08
rWarp = r + warp

rings = sin(rWarp * glyphRings * 3.14159) * 0.5 + 0.5
spokes = abs(cos(ang * 8.0 + time * 0.8))

rawLum = (rings * 0.65 + spokes * 0.35) * smoothstep(1.3, 0.1, r)

px = floor(uv * res / matrixScale)
gx = fmod(px.x, 4.0)
gy = fmod(px.y, 4.0)
bayer = (gx * 3.0 + gy * 7.0) / 16.0

dithered = step(bayer + thresholdOffset, rawLum * edgeContrast)

cBlue = vec3(0.012, 0.045, 0.450)
cIvory = vec3(0.965, 0.950, 0.915)

col = mix(cBlue, cIvory, dithered)
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
  float time = t * 0.75;

  float b = audio_bass * 1.8;
  float m = audio_mid * 1.6;
  float h = audio_high * 2.0;

  float r = length(p);
  float ang = atan(p.y, p.x);

  float warp = sin(r * 10.0 - time * 2.0 + b * 3.0) * (0.08 + b * 0.12);
  float rWarp = r + warp;

  float rings = sin(rWarp * 6.0 * 3.14159) * 0.5 + 0.5;
  float spokes = abs(cos(ang * 8.0 + time * 0.8 + m * 2.5));

  float rawLum = (rings * 0.65 + spokes * 0.35) * smoothstep(1.3 + b * 0.2, 0.1, r);

  vec2 px = floor(gl_FragCoord.xy / 3.0);
  float gx = mod(px.x, 4.0);
  float gy = mod(px.y, 4.0);
  float bayer = (gx * 3.0 + gy * 7.0) / 16.0;

  float dithered = step(bayer + 0.04 - b * 0.15 + h * 0.1, rawLum * (1.25 + h * 0.8));

  vec3 cBlue = vec3(0.012, 0.045, 0.450);
  vec3 cIvory = vec3(0.965, 0.950, 0.915);

  vec3 col = mix(cBlue, cIvory, dithered);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "ditherpunk-cyber-glyph-lattice",
        "title": "1-Bit Ditherpunk Cyber Glyph Lattice",
        "description": "Esoteric cyber-glyph grid shader with 1-bit procedural blue noise dithering, audio-reactive interference waves, and duo-tone stippling.",
        "embed_desc": "Esoteric cyber-glyph grid shader with 1-bit procedural blue noise dithering, audio-reactive interference waves, and duo-tone stippling. Bass pulses the geometric glyph scale, mids shift the horizontal scanline frequency, and highs ignite stippled dither sparks.",
        "tags": ["pixel", "ditherpunk", "1bit", "glyph", "lattice", "shader"],
        "params": {
            "gridDivs": 8.0,
            "stippleScale": 2.5,
            "waveSpeed": 0.85,
            "threshold": 0.48,
            "crosshairSize": 0.35
        },
        "code": """param float gridDivs = 8.0 [4.0, 16.0]
param float stippleScale = 2.5 [1.0, 6.0]
param float waveSpeed = 0.85 [0.1, 2.5]
param float threshold = 0.48 [0.1, 0.9]
param float crosshairSize = 0.35 [0.1, 0.8]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.0
time = t * waveSpeed

cell = fract(uv * gridDivs) - 0.5
gridId = floor(uv * gridDivs)

dCell = length(cell)
ring = smoothstep(0.06, 0.01, abs(dCell - 0.28))
cross = smoothstep(0.04, 0.01, min(abs(cell.x), abs(cell.y))) * step(dCell, crosshairSize)

wave = sin(gridId.x * 0.8 + gridId.y * 0.6 - time * 2.0) * 0.5 + 0.5
glyph = max(ring, cross) * wave

px = floor(uv * res / stippleScale)
hash = fract(sin(dot(px, vec2(12.9898, 78.233)) + time * 0.1) * 43758.5453)

dithered = step(threshold, glyph * 0.85 + hash * 0.45)

cBlue = vec3(0.012, 0.045, 0.450)
cIvory = vec3(0.965, 0.950, 0.915)

col = mix(cBlue, cIvory, dithered)
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
  float time = t * 0.85;

  float b = audio_bass * 1.8;
  float m = audio_mid * 1.6;
  float h = audio_high * 2.2;

  vec2 cell = fract(uv * 8.0) - 0.5;
  vec2 gridId = floor(uv * 8.0);

  float dCell = length(cell);
  float ring = smoothstep(0.06, 0.01, abs(dCell - (0.28 + b * 0.12)));
  float cross = smoothstep(0.04, 0.01, min(abs(cell.x), abs(cell.y))) * step(dCell, 0.35 + m * 0.15);

  float wave = sin(gridId.x * 0.8 + gridId.y * 0.6 - time * 2.0 + b * 2.5) * 0.5 + 0.5;
  float glyph = max(ring, cross) * wave;

  vec2 px = floor(gl_FragCoord.xy / 2.5);
  float hash = fract(sin(dot(px, vec2(12.9898, 78.233)) + time * 0.1) * 43758.5453);

  float dithered = step(0.48 - b * 0.12 + h * 0.15, glyph * (0.85 + h * 0.5) + hash * (0.45 + h * 0.4));

  vec3 cBlue = vec3(0.012, 0.045, 0.450);
  vec3 cIvory = vec3(0.965, 0.950, 0.915);

  vec3 col = mix(cBlue, cIvory, dithered);
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
  <meta name="twitter:title" content="{item['title']}: audio-reactive visual">
  <meta name="twitter:description" content="{item['embed_desc']}">
  <meta name="twitter:image" content="https://infinitedevices.org/assets/og-image.jpg">
  <style>
    * {{ box-sizing: border-box; margin: 0; padding: 0; }}
    html, body {{
      width: 100%;
      height: 100%;
      overflow: hidden;
      background: #020830;
      color: #f4f1ea;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    }}
    canvas {{
      display: block;
      width: 100vw;
      height: 100vh;
      width: 100dvw;
      height: 100dvh;
    }}
    .badge {{
      position: absolute;
      top: 14px;
      right: 14px;
      padding: 6px 12px;
      background: rgba(2, 8, 48, 0.85);
      backdrop-filter: blur(8px);
      border: 1px solid rgba(244, 241, 234, 0.2);
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
      background: rgba(10, 20, 70, 0.95);
      border-color: rgba(244, 241, 234, 0.5);
    }}
    .badge-icon {{
      width: 14px;
      height: 14px;
      border-radius: 50%;
      background: linear-gradient(135deg, #0011ff, #f4f1ea);
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
      background: rgba(2, 8, 48, 0.82);
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
    print("=== Creative Expansion Pack 24: Build and Gate Runner ===")
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
  c bg 0.100 0.110 0.130
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
  c fillColor 0.18 0.20 0.25
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
  c fillColor 0.05 0.15 0.35
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
