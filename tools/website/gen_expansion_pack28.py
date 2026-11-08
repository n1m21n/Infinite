#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 28.
Generates 10 items across the 5 core creative families:
1. Sketch 2D (analog-crt-raster-beam-oscilloscope, analog-crt-magnetic-deflection-lattice)
2. Sketch 3D (technical-blueprint-armillary-sphere-3d, technical-blueprint-planetary-gear-lattice-3d)
3. Field Synth (footwork-syncopated-808-sub-roller-synth, footwork-rapid-fire-percussive-stab-synth)
4. Field Effects (amapiano-log-drum-warm-saturator-fx, amapiano-lush-spatial-reverb-diffusion-fx)
5. Field Pixel (vaporwave-collage-retro-sunset-grid, vaporwave-collage-prismatic-chroma-wave)
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

TMP_DIR = "/tmp/pack28_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview

# -------------------------------------------------------------
# 1. Sketch 2D Sources (Theme: analog_crt)
# -------------------------------------------------------------
SKETCH_2D_ITEMS = [
    {
        "slug": "analog-crt-raster-beam-oscilloscope",
        "title": "Analog CRT Raster Beam Oscilloscope",
        "description": "Cathode ray tube oscilloscope visualizer with dual-trace Lissajous beam deflection, high-frequency harmonic waveforms, horizontal sync pulse blanking, and calibrated green phosphor graticule grid.",
        "tags": ["analog", "crt", "oscilloscope", "lissajous", "raster", "drawing"],
        "code": """// Analog CRT Raster Beam Oscilloscope - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Cathode ray tube oscilloscope visualizer with dual-trace Lissajous beam deflection, high-frequency harmonic waveforms, horizontal sync pulse blanking, and calibrated green phosphor graticule grid.
param("frequency_x", 3.0, 1.0, 8.0);
param("frequency_y", 4.0, 1.0, 8.0);
param("speed", 0.45, 0.1, 1.5);
param("beam_width", 2.6, 1.0, 4.5);

function draw(t) {
  // Deep CRT glass shadow with dark phosphor tone (#08140c)
  background(0.038, 0.075, 0.048);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;

  push();
  translate(cx, cy);

  // 1. CRT bezel outer frame and corner curves
  stroke(0.0, 0.85, 0.35, 0.85);
  strokeWeight(2.6);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  // 2. Calibrated oscilloscope graticule grid (10x10)
  stroke(0.0, 0.55, 0.22, 0.45);
  strokeWeight(1.4);
  for (let k = -4; k <= 4; k++) {
    const p = (k / 5.0) * (span * 0.44);
    line(p, -span * 0.44, p, span * 0.44);
    line(-span * 0.44, p, span * 0.44, p);
  }

  // Central graticule axis tick marks
  stroke(0.0, 0.90, 0.38, 0.75);
  strokeWeight(1.8);
  line(0, -span * 0.46, 0, span * 0.46);
  line(-span * 0.46, 0, span * 0.46, 0);
  for (let k = -20; k <= 20; k++) {
    const p = (k / 20.0) * (span * 0.44);
    const tick = (k % 5 === 0) ? 10.0 : 5.0;
    line(p, -tick, p, tick);
    line(-tick, p, tick, p);
  }

  const time = t * speed;

  // 3. Dual-trace beam: Secondary beam (magenta reference wave)
  stroke(1.0, 0.0, 0.33, 0.82); // Phosphor red/magenta (#ff0055)
  strokeWeight(beam_width * 0.95);
  noFill();
  beginShape();
  const steps = 320;
  for (let i = 0; i <= steps; i++) {
    const u = (i / steps) * Math.PI * 2.0;
    const x = sin(u * frequency_x + time * 1.8) * (span * 0.40);
    const y = sin(u * (frequency_y + 1.0) - time * 1.2) * (span * 0.38);
    vertex(x, y);
  }
  endShape();

  // 4. Primary electron beam (high-intensity phosphor green #00ff66)
  stroke(0.0, 1.0, 0.42, 0.95);
  strokeWeight(beam_width);
  noFill();
  beginShape();
  for (let i = 0; i <= steps; i++) {
    const u = (i / steps) * Math.PI * 2.0;
    const x = sin(u * frequency_x + time * 1.4) * (span * 0.42);
    const y = sin(u * frequency_y + time * 1.1 + 0.75) * (span * 0.42);
    vertex(x, y);
  }
  endShape();

  // White core beam filament
  stroke(0.92, 1.0, 0.96, 0.90);
  strokeWeight(beam_width * 0.5);
  noFill();
  beginShape();
  for (let i = 0; i <= steps; i += 2) {
    const u = (i / steps) * Math.PI * 2.0;
    const x = sin(u * frequency_x + time * 1.4) * (span * 0.42);
    const y = sin(u * frequency_y + time * 1.1 + 0.75) * (span * 0.42);
    vertex(x, y);
  }
  endShape();

  // 5. Cathode focus electron target spots
  fill(1.0, 1.0, 1.0);
  noStroke();
  circle(sin(time * 1.4) * (span * 0.42), sin(time * 1.1 + 0.75) * (span * 0.42), 6.5);
  fill(0.0, 1.0, 0.42, 0.85);
  circle(sin(time * 1.4) * (span * 0.42), sin(time * 1.1 + 0.75) * (span * 0.42), 14.0);

  pop();
}
"""
    },
    {
        "slug": "analog-crt-magnetic-deflection-lattice",
        "title": "Analog CRT Magnetic Deflection Lattice",
        "description": "Electromagnetic yoke deflection raster study featuring orthogonal interlaced scanlines, RGB phosphor triad registration pips, sinusoidal beam flutter, and high-voltage anode calibration reticles.",
        "tags": ["analog", "crt", "raster", "scanline", "deflection", "drawing"],
        "code": """// Analog CRT Magnetic Deflection Lattice - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Electromagnetic yoke deflection raster study featuring orthogonal interlaced scanlines, RGB phosphor triad registration pips, sinusoidal beam flutter, and high-voltage anode calibration reticles.
param("scanlines", 36.0, 16.0, 64.0);
param("deflection", 1.25, 0.4, 2.5);
param("speed", 0.40, 0.1, 1.2);
param("line_width", 2.2, 1.0, 4.0);

function draw(t) {
  // Dark CRT glass background (#09150d)
  background(0.040, 0.082, 0.052);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;

  push();
  translate(cx, cy);

  // 1. Heavy outer magnetic shielding frame
  stroke(0.0, 0.90, 0.38, 0.85);
  strokeWeight(2.8);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  const time = t * speed;
  const count = floor(scanlines);

  // 2. Interlaced horizontal raster scanlines with sinusoidal yoke deflection
  for (let i = 0; i < count; i++) {
    const norm = (i / (count - 1)) * 2.0 - 1.0;
    const y0 = norm * span * 0.44;

    const isField2 = (i % 2 === 1);
    const isAccent = (i % 6 === 0);

    if (isAccent) {
      stroke(1.0, 0.0, 0.33, 0.90); // Phosphor red/magenta (#ff0055)
      strokeWeight(line_width * 1.3);
    } else if (isField2) {
      stroke(0.0, 1.0, 0.42, 0.85); // High-intensity phosphor green (#00ff66)
      strokeWeight(line_width * 0.95);
    } else {
      stroke(0.15, 0.75, 0.40, 0.75); // Low-persistence phosphor trace
      strokeWeight(line_width * 0.85);
    }
    noFill();

    beginShape();
    const steps = 40;
    for (let s = 0; s <= steps; s++) {
      const u = (s / steps) * 2.0 - 1.0;
      const x = u * span * 0.44;
      const flutter = sin(u * 3.2 * deflection + time * 1.6 + norm * 2.0) * (span * 0.035 * deflection);
      const warp = cos(norm * Math.PI * 0.5) * (span * 0.02 * sin(time * 0.8));
      const y = y0 + flutter + warp;
      vertex(x, y);
    }
    endShape();
  }

  // 3. Central magnetic anode alignment reticle
  push();
  rotate(time * 0.3);
  stroke(1.0, 1.0, 1.0, 0.95); // White electron beam focus
  strokeWeight(2.0);
  noFill();
  circle(0, 0, span * 0.22);
  circle(0, 0, span * 0.11);

  stroke(1.0, 0.0, 0.33, 0.85);
  strokeWeight(2.2);
  for (let a = 0; a < 4; a++) {
    const ang = (a / 4.0) * Math.PI * 2.0;
    push();
    rotate(ang);
    line(span * 0.07, 0, span * 0.16, 0);
    pop();
  }
  pop();

  // 4. Phosphor triad calibration dots along margins
  noStroke();
  for (let k = -6; k <= 6; k++) {
    const p = (k / 6.0) * (span * 0.44);
    // Green phosphor dot
    fill(0.0, 1.0, 0.42, 0.9);
    circle(p, -span * 0.46, 3.5);
    circle(p, span * 0.46, 3.5);
    // Magenta phosphor dot
    fill(1.0, 0.0, 0.33, 0.9);
    circle(-span * 0.46, p, 3.5);
    circle(span * 0.46, p, 3.5);
  }

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 2. Sketch 3D Sources (Theme: technical_blueprint)
# -------------------------------------------------------------
SKETCH_3D_ITEMS = [
    {
        "slug": "technical-blueprint-armillary-sphere-3d",
        "title": "Technical Blueprint Armillary Sphere 3D",
        "description": "Precision axonometric 3D armillary sphere mechanism with nested concentric equatorial rings, polar declination calipers, stepped azimuth pivots, and a central brass terrestrial core.",
        "tags": ["technical", "blueprint", "armillary", "mechanism", "geometry", "3d"],
        "code": """// Technical Blueprint Armillary Sphere 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Precision axonometric 3D armillary sphere mechanism with nested concentric equatorial rings, polar declination calipers, stepped azimuth pivots, and a central brass terrestrial core.
param("rings", 5.0, 3.0, 10.0);
param("spin", 0.40, 0.1, 1.2);
param("scale", 1.15, 0.5, 2.0);
param("strut_width", 0.045, 0.02, 0.10);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.48 + sin(time * 0.18) * 0.05);
  rotateY(time * 0.32 + 0.6);

  // Precision drafting reference plinth (#00162b)
  push();
  translate(0, 1.2 * scale, 0);
  fill(0.008, 0.086, 0.169); // Deep drafting blueprint plinth
  box(2.4 * scale, 0.12, 2.4 * scale);
  pop();

  const ringCount = floor(rings);

  // 1. Concentric rotating armillary meridian and declination rings
  for (let r = 0; r < ringCount; r++) {
    const rad = (0.65 + r * 0.18) * scale;
    const ang = (r / ringCount) * Math.PI;

    push();
    rotateY(time * (0.28 + r * 0.08) + ang);
    rotateX(ang * 0.5);

    const segments = 24;
    for (let s = 0; s < segments; s++) {
      const theta = (s / segments) * Math.PI * 2.0;
      push();
      translate(cos(theta) * rad, 0, sin(theta) * rad);
      rotateY(-theta);

      // Drafting cyan (#00d4ff) or white marker for key ring
      if (r === ringCount - 1) {
        fill(1.0, 1.0, 1.0); // Technical white outer ring
      } else if (r % 2 === 0) {
        fill(0.0, 0.83, 1.0); // Drafting cyan
      } else {
        fill(1.0, 0.72, 0.01); // Precision brass accent
      }
      box(strut_width * scale, strut_width * scale, (2.0 * Math.PI * rad / segments) * 0.95);
      pop();
    }
    pop();
  }

  // 2. Central brass core sphere and gimbal axle
  push();
  rotateY(-time * 0.5);
  rotateZ(time * 0.35);

  // Central brass core cube/sphere
  fill(1.0, 0.72, 0.01); // Precision brass
  box(0.36 * scale, 0.36 * scale, 0.36 * scale);

  // White coordinate indicator pips
  fill(1.0, 1.0, 1.0);
  for (let a = -1; a <= 1; a += 2) {
    push();
    translate(a * 0.22 * scale, 0, 0);
    box(0.10 * scale, 0.10 * scale, 0.10 * scale);
    pop();
    push();
    translate(0, a * 0.22 * scale, 0);
    box(0.10 * scale, 0.10 * scale, 0.10 * scale);
    pop();
    push();
    translate(0, 0, a * 0.22 * scale);
    box(0.10 * scale, 0.10 * scale, 0.10 * scale);
    pop();
  }
  pop();

  // 3. Polar coordinate axis drive shaft
  fill(0.0, 0.83, 1.0);
  box(strut_width * 1.2 * scale, 2.5 * scale, strut_width * 1.2 * scale);

  // Polar end finials
  fill(1.0, 1.0, 1.0);
  for (let s = -1; s <= 1; s += 2) {
    push();
    translate(0, s * 1.25 * scale, 0);
    box(0.14 * scale, 0.14 * scale, 0.14 * scale);
    pop();
  }

  pop();
}
"""
    },
    {
        "slug": "technical-blueprint-planetary-gear-lattice-3d",
        "title": "Technical Blueprint Planetary Gear Lattice 3D",
        "description": "Axonometric epicyclic planetary gear train assembly with rotating sun gear, orbiting planet pinions, precision annular ring teeth, and mechanical drafting reference plinths.",
        "tags": ["technical", "blueprint", "gears", "mechanical", "engineering", "3d"],
        "code": """// Technical Blueprint Planetary Gear Lattice 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Axonometric epicyclic planetary gear train assembly with rotating sun gear, orbiting planet pinions, precision annular ring teeth, and mechanical drafting reference plinths.
param("planets", 4.0, 3.0, 8.0);
param("gear_speed", 0.45, 0.1, 1.5);
param("scale", 1.15, 0.5, 2.0);
param("tooth_depth", 0.08, 0.03, 0.18);

function draw(t) {
  push();
  const time = t * gear_speed;

  rotateX(0.55 + sin(time * 0.15) * 0.04);
  rotateY(time * 0.30 + 0.5);

  // Precision blueprint drafting base plinth (#00162b)
  push();
  translate(0, 1.25 * scale, 0);
  fill(0.008, 0.086, 0.169);
  box(2.4 * scale, 0.12, 2.4 * scale);
  pop();

  // 1. Central sun gear (rotating clockwise)
  push();
  rotateY(time * 1.2);

  // Sun gear core body
  fill(1.0, 0.72, 0.01); // Precision brass accent
  box(0.55 * scale, 0.22 * scale, 0.55 * scale);

  // Central axle drive arbor
  fill(1.0, 1.0, 1.0);
  box(0.18 * scale, 1.8 * scale, 0.18 * scale);

  // Sun gear radial teeth
  fill(0.0, 0.83, 1.0); // Drafting cyan
  const sunTeeth = 8;
  for (let i = 0; i < sunTeeth; i++) {
    const a = (i / sunTeeth) * Math.PI * 2.0;
    push();
    rotateY(a);
    translate(0.32 * scale, 0, 0);
    box(tooth_depth * 1.5 * scale, 0.20 * scale, 0.08 * scale);
    pop();
  }
  pop();

  // 2. Orbiting planetary pinions
  const nPlanets = floor(planets);
  const orbitRad = 0.72 * scale;
  for (let p = 0; p < nPlanets; p++) {
    const orbitAngle = (p / nPlanets) * Math.PI * 2.0 + time * 0.45;
    push();
    translate(cos(orbitAngle) * orbitRad, 0, sin(orbitAngle) * orbitRad);
    rotateY(-time * 1.4); // Counter-rotation of planet pinion

    // Planet pinion hub
    fill(0.0, 0.83, 1.0); // Drafting cyan
    box(0.38 * scale, 0.18 * scale, 0.38 * scale);

    // Planet pinion spindle
    fill(1.0, 1.0, 1.0);
    box(0.10 * scale, 0.45 * scale, 0.10 * scale);

    // Planet teeth
    fill(1.0, 0.72, 0.01);
    const planetTeeth = 6;
    for (let j = 0; j < planetTeeth; j++) {
      const theta = (j / planetTeeth) * Math.PI * 2.0;
      push();
      rotateY(theta);
      translate(0.22 * scale, 0, 0);
      box(tooth_depth * 1.2 * scale, 0.16 * scale, 0.06 * scale);
      pop();
    }
    pop();
  }

  // 3. Outer annular ring gear casing
  push();
  rotateY(-time * 0.15);
  const ringSegments = 24;
  const ringRad = 1.05 * scale;
  for (let s = 0; s < ringSegments; s++) {
    const phi = (s / ringSegments) * Math.PI * 2.0;
    push();
    translate(cos(phi) * ringRad, 0, sin(phi) * ringRad);
    rotateY(-phi);
    if (s % 4 === 0) {
      fill(1.0, 1.0, 1.0); // White index markers
    } else {
      fill(0.0, 0.83, 1.0); // Drafting cyan ring
    }
    box(0.06 * scale, 0.26 * scale, (2.0 * Math.PI * ringRad / ringSegments) * 0.95);
    pop();
  }
  pop();

  pop();
}
"""
    }
]

# -------------------------------------------------------------
# 3. Field Synth Sources (Theme: footwork)
# -------------------------------------------------------------
SYNTH_ITEMS = [
    {
        "slug": "footwork-syncopated-808-sub-roller-synth",
        "title": "Footwork Syncopated 808 Sub Roller Synth",
        "description": "Deep syncopated 808 sub bass synthesizer with rapid exponential pitch envelope drop, saturated sine core, sub-octave drive, and fast transient click for 160 BPM footwork.",
        "tags": ["synth", "footwork", "808", "sub", "bass", "audio"],
        "note_root": 40,
        "params": {
            "pitchDecay": 0.08,
            "subDrive": 0.45,
            "clickLevel": 0.25,
            "decayTime": 0.28,
            "cutoff": 0.65
        },
        "code": """param float pitchDecay = 0.08 [0.02, 0.4]
param float subDrive = 0.45 [0.0, 1.0]
param float clickLevel = 0.25 [0.0, 0.8]
param float decayTime = 0.28 [0.05, 0.9]
param float cutoff = 0.65 [0.1, 1.0]
state float p1 = 0
state float pClick = 0
state float env = 0
state float pEnv = 0
state float lp1 = 0
state float lp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * decayTime), 0.0001, 0.05)
env = if(target > env, env + (target - env) * 0.90, env - env * decayRate)

pRate = clamp(1.0 / (sr * pitchDecay), 0.0005, 0.08)
pEnv = if(target > pEnv, target, pEnv - pEnv * pRate)

fBase = clamp(freq * (1.0 + pEnv * 2.8), 30.0, 480.0)

dp = fBase / sr
p1 = p1 + dp
p1 = if(p1 >= 1.0, p1 - 1.0, p1)
sine1 = sin(p1 * 6.2831853)

dpC = 1200.0 / sr
pClick = pClick + dpC
pClick = if(pClick >= 1.0, pClick - 1.0, pClick)
click = sin(pClick * 6.2831853) * pEnv * clickLevel

raw = sine1 * (1.0 + subDrive * 1.4) + click
sat = (raw / (1.0 + abs(raw * 0.7))) * env

fCut = clamp(cutoff * 0.38, 0.02, 0.45)
lp1 = lp1 + fCut * (sat - lp1)
lp2 = lp2 + fCut * (lp1 - lp2)

out = lp2 * 0.44
"""
    },
    {
        "slug": "footwork-rapid-fire-percussive-stab-synth",
        "title": "Footwork Rapid Fire Percussive Stab Synth",
        "description": "Aggressive Chicago footwork FM percussive chord stab synthesizer with punchy exponential decay, resonant bandpass formant contour, and crisp saturated transient bite.",
        "tags": ["synth", "footwork", "stab", "percussive", "fm", "audio"],
        "note_root": 55,
        "params": {
            "fmDepth": 0.45,
            "modRatio": 2.0,
            "stabDecay": 0.16,
            "resonance": 0.35,
            "drive": 0.30
        },
        "code": """param float fmDepth = 0.45 [0.0, 1.0]
param float modRatio = 2.0 [0.5, 6.0]
param float stabDecay = 0.16 [0.03, 0.6]
param float resonance = 0.35 [0.0, 0.85]
param float drive = 0.30 [0.0, 1.0]
state float pCarrier = 0
state float pMod = 0
state float env = 0
state float lp1 = 0
state float lp2 = 0

target = if(gate > 0.5, 1.0, 0.0)
decayRate = clamp(1.0 / (sr * stabDecay), 0.0001, 0.06)
env = if(target > env, env + (target - env) * 0.90, env - env * decayRate)

fBase = clamp(freq, 60.0, 1200.0)

dpM = (fBase * modRatio) / sr
pMod = pMod + dpM
pMod = if(pMod >= 1.0, pMod - 1.0, pMod)
modSig = sin(pMod * 6.2831853) * fmDepth * env

dpC = fBase / sr
pCarrier = pCarrier + dpC + modSig * 0.08
pCarrier = if(pCarrier >= 1.0, pCarrier - 1.0, pCarrier)
carrier = sin(pCarrier * 6.2831853)

raw = carrier * (1.0 + drive * 1.5)
sat = (raw / (1.0 + abs(raw * 0.6))) * env

fCut = clamp(0.12 + env * 0.40, 0.02, 0.45)
lp1 = lp1 + fCut * (sat + lp1 * resonance * 0.40 - lp1)
lp2 = lp2 + fCut * (lp1 - lp2)

out = lp2 * 0.42
"""
    }
]

# -------------------------------------------------------------
# 4. Field Effects Sources (Theme: amapiano)
# -------------------------------------------------------------
FX_ITEMS = [
    {
        "slug": "amapiano-log-drum-warm-saturator-fx",
        "title": "Amapiano Log Drum Warm Saturator FX",
        "description": "Warm analog low-end saturator and transient contour filter tailored for amapiano log drum basslines, with asymmetric polynomial drive, harmonic warmth, and smooth top-end roll-off.",
        "tags": ["effect", "amapiano", "saturator", "logdrum", "drive", "audio"],
        "params": {
            "drive": 0.42,
            "warmth": 0.35,
            "bassBoost": 0.40,
            "damping": 3400.0,
            "mix": 0.65
        },
        "code": """param float drive = 0.42 [0.0, 1.0]
param float warmth = 0.35 [0.0, 1.0]
param float bassBoost = 0.40 [0.0, 1.0]
param float damping = 3400.0 [800.0, 8000.0]
param float mix = 0.65 [0.0, 1.0]
state float lp = 0
state float lpBass = 0

fDamp = clamp(damping / sr * 2.0, 0.02, 0.45)
lp = lp + fDamp * (in - lp)

fBass = clamp(180.0 / sr * 2.0, 0.005, 0.15)
lpBass = lpBass + fBass * (in - lpBass)

shaped = lp + lpBass * bassBoost * 1.5

drv = shaped * (1.0 + drive * 2.0)
asym = drv + drv * drv * warmth * 0.35
sat = clamp(asym / (1.0 + abs(asym * 0.65)), -0.90, 0.90)

processed = in * (1.0 - mix) + sat * mix
out = processed * 0.78
"""
    },
    {
        "slug": "amapiano-lush-spatial-reverb-diffusion-fx",
        "title": "Amapiano Lush Spatial Reverb Diffusion FX",
        "description": "Expansive warm spatial reverb and early reflection diffusion network designed for amapiano jazz chord progressions and shaker grooves, with high-frequency damping and rich acoustic depth.",
        "tags": ["effect", "amapiano", "reverb", "diffusion", "spatial", "audio"],
        "params": {
            "roomSize": 0.58,
            "decayTime": 0.45,
            "damping": 3600.0,
            "diffusion": 0.42,
            "mix": 0.52
        },
        "code": """param float roomSize = 0.58 [0.1, 0.9]
param float decayTime = 0.45 [0.1, 0.8]
param float damping = 3600.0 [800.0, 8000.0]
param float diffusion = 0.42 [0.0, 0.85]
param float mix = 0.52 [0.0, 1.0]
state float c1 = 0
state float c2 = 0
state float c3 = 0
state float lp = 0

fDamp = clamp(damping / sr * 2.0, 0.03, 0.45)
lp = lp + fDamp * (in - lp)

fb = clamp(roomSize * 0.82, 0.1, 0.82)
dRate = clamp(decayTime * 0.32, 0.03, 0.32)

c1 = c1 + dRate * 1.02 * (lp + c3 * fb * 0.50 - c1)
c2 = c2 + dRate * 0.75 * (c1 * fb * 0.72 - c2)
c3 = c3 + dRate * 0.52 * (c2 * fb * 0.68 - c3)

diff = (c1 - c2) * diffusion * 1.6
wet = (c1 * 0.38 + c2 * 0.32 + c3 * 0.28 + diff)
sat = clamp(wet / (1.0 + abs(wet * 0.5)), -0.85, 0.85)

processed = in * (1.0 - mix) + sat * mix
out = processed * 1.45
"""
    }
]

# -------------------------------------------------------------
# 5. Field Pixel & WebGL Embeds (Theme: vaporwave_collage)
# -------------------------------------------------------------
PIXEL_ITEMS = [
    {
        "slug": "vaporwave-collage-retro-sunset-grid",
        "title": "Vaporwave Collage Retro Sunset Grid",
        "description": "Audio-reactive retro-futuristic horizon perspective neon grid receding towards a segmented neon sunset disc, with chromatic aberration scanlines and twilight violet sky.",
        "embed_desc": "Audio-reactive vaporwave landscape simulation featuring a perspective grid terrain receding towards an undulating sunset orb. Bass frequencies warp and pulse the horizon grid plane, mid frequencies modulate concentric solar segmentation bands, and high frequencies ignite luminous cyan-pink chromatic dispersion scanlines over twilight violet.",
        "tags": ["pixel", "vaporwave", "sunset", "perspective", "grid", "audioreactive"],
        "params": {
            "speed": 0.42,
            "gridScale": 14.0,
            "sunRadius": 0.35,
            "contrast": 1.15,
            "glow": 0.55
        },
        "code": """param float speed = 0.42 [0.1, 1.5]
param float gridScale = 14.0 [6.0, 30.0]
param float sunRadius = 0.35 [0.1, 0.7]
param float contrast = 1.15 [0.6, 2.0]
param float glow = 0.55 [0.1, 1.2]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5)
time = t * speed

cSky = vec3(0.141, 0.090, 0.204)
cPink = vec3(1.0, 0.443, 0.808)
cCyan = vec3(0.004, 0.804, 0.996)
cYellow = vec3(1.0, 0.984, 0.588)

horizon = 0.02
isTerrain = if(p.y < horizon, 1.0, 0.0)

perspY = max(horizon - p.y, 0.002)
pz = 0.4 / perspY
px = p.x * pz

gx = 1.0 - smoothstep(0.0, 0.12, abs(fract(px * gridScale * 0.08) - 0.5))
gz = 1.0 - smoothstep(0.0, 0.12, abs(fract(pz * 0.45 - time * 1.5) - 0.5))
gridLine = max(gx, gz)
gridFade = smoothstep(horizon, -0.4, p.y)
gridCol = mix(cCyan, cPink, clamp(pz * 0.12, 0.0, 1.0))

sunP = vec2(p.x, p.y - horizon - 0.08)
sunD = length(sunP)
sunMask = 1.0 - smoothstep(sunRadius * 0.96, sunRadius, sunD)
sunBlind = abs(sin(sunP.y * 55.0))
sunSegments = step(0.25, sunBlind)
sunCol = mix(cPink, cYellow, clamp((sunP.y / sunRadius) + 0.5, 0.0, 1.0)) * sunSegments

col = cSky
col = mix(col, sunCol, sunMask)
col = mix(col, gridCol, gridLine * gridFade * isTerrain)

glowDist = exp(-sunD * (3.5 - glow * 1.5))
col = col + cPink * (glowDist * 0.35)

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
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
  float time = t * 0.42;

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  vec3 cSky = vec3(0.141, 0.090, 0.204);
  vec3 cPink = vec3(1.0, 0.443, 0.808);
  vec3 cCyan = vec3(0.004, 0.804, 0.996);
  vec3 cYellow = vec3(1.0, 0.984, 0.588);

  float horizon = 0.02 + sin(time * 0.5) * 0.01;
  float isTerrain = step(p.y, horizon);

  float perspY = max(horizon - p.y, 0.002);
  float pz = 0.4 / perspY;
  float px = p.x * pz;

  float gx = 1.0 - smoothstep(0.0, 0.12 + b * 0.05, abs(fract(px * (14.0 + b * 2.0) * 0.08) - 0.5));
  float gz = 1.0 - smoothstep(0.0, 0.12, abs(fract(pz * 0.45 - time * 1.5) - 0.5));
  float gridLine = max(gx, gz);
  float gridFade = smoothstep(horizon, -0.4, p.y);
  vec3 gridCol = mix(cCyan, cPink, clamp(pz * 0.12, 0.0, 1.0));

  vec2 sunP = vec2(p.x, p.y - horizon - 0.08);
  float sunD = length(sunP);
  float sunR = 0.35 + b * 0.05;
  float sunMask = 1.0 - smoothstep(sunR * 0.96, sunR, sunD);
  float sunBlind = abs(sin(sunP.y * (55.0 + m * 20.0)));
  float sunSegments = step(0.25, sunBlind);
  vec3 sunCol = mix(cPink, cYellow, clamp((sunP.y / sunR) + 0.5, 0.0, 1.0)) * sunSegments;

  vec3 col = cSky;
  col = mix(col, sunCol, sunMask);
  col = mix(col, gridCol, gridLine * gridFade * isTerrain);

  float glowDist = exp(-sunD * 2.8);
  col += cPink * (glowDist * (0.35 + h * 0.25));

  gl_FragColor = vec4(clamp(col * 1.15, 0.0, 1.0), 1.0);
}
"""
    },
    {
        "slug": "vaporwave-collage-prismatic-chroma-wave",
        "title": "Vaporwave Collage Prismatic Chroma Wave",
        "description": "Audio-reactive liquid chromatic dispersion ripples and iridescent pastel gradients undulating across an ethereal 90s aesthetic reflective horizon.",
        "embed_desc": "Audio-reactive vaporwave liquid refraction surface modeling chromatic wave dispersion. Bass frequencies drive deep tidal ripple swells, mids induce complex interference patterns across pastel pink and cyan gradients, and highs shimmer with radiant mint-green caustic highlights.",
        "tags": ["pixel", "vaporwave", "prismatic", "chromatic", "waves", "audioreactive"],
        "params": {
            "speed": 0.38,
            "waveScale": 12.0,
            "contrast": 1.12,
            "dispersion": 0.45
        },
        "code": """param float speed = 0.38 [0.1, 1.5]
param float waveScale = 12.0 [4.0, 24.0]
param float contrast = 1.12 [0.6, 2.0]
param float dispersion = 0.45 [0.1, 1.0]

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 2.2
time = t * speed

r = length(p)
theta = atan2(p.y, p.x)

w1 = sin(p.x * waveScale * 0.8 + time * 2.2) * cos(p.y * waveScale * 0.6 - time * 1.5)
w2 = sin(r * waveScale * 1.2 - time * 2.8 + theta * 2.0)
wave = (w1 + w2) * 0.5

rDisp = wave * dispersion * 0.08
dispUv = p + vec2(rDisp, -rDisp)
waveDisp = sin(length(dispUv) * waveScale - time * 2.5)

cTwilight = vec3(0.141, 0.090, 0.204)
cPink = vec3(1.0, 0.443, 0.808)
cCyan = vec3(0.004, 0.804, 0.996)
cMint = vec3(0.020, 1.0, 0.631)
cGold = vec3(1.0, 0.984, 0.588)

base = mix(cTwilight, cCyan, clamp(wave * 0.5 + 0.5, 0.0, 1.0))
col = mix(base, cPink, clamp(waveDisp * 0.6 + 0.4, 0.0, 1.0))

crest = 1.0 - smoothstep(0.35, 0.75, abs(wave))
col = mix(col, cMint, crest * 0.55)

spec = exp(-abs(wave - 0.6) * 12.0)
col = mix(col, cGold, spec * 0.65)

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
  float time = t * 0.38;

  float b = audio_bass * 1.5;
  float m = audio_mid * 1.6;
  float h = audio_high * 1.8;

  float r = length(p);
  float theta = atan(p.y, p.x);

  float wScale = 12.0 + m * 4.0;
  float w1 = sin(p.x * wScale * 0.8 + time * 2.2) * cos(p.y * wScale * 0.6 - time * 1.5);
  float w2 = sin(r * wScale * 1.2 - time * 2.8 + theta * 2.0);
  float wave = (w1 + w2) * 0.5 * (1.0 + b * 0.4);

  float rDisp = wave * (0.45 + h * 0.25) * 0.08;
  vec2 dispUv = p + vec2(rDisp, -rDisp);
  float waveDisp = sin(length(dispUv) * wScale - time * 2.5);

  vec3 cTwilight = vec3(0.141, 0.090, 0.204);
  vec3 cPink = vec3(1.0, 0.443, 0.808);
  vec3 cCyan = vec3(0.004, 0.804, 0.996);
  vec3 cMint = vec3(0.020, 1.0, 0.631);
  vec3 cGold = vec3(1.0, 0.984, 0.588);

  vec3 base = mix(cTwilight, cCyan, clamp(wave * 0.5 + 0.5, 0.0, 1.0));
  vec3 col = mix(base, cPink, clamp(waveDisp * 0.6 + 0.4, 0.0, 1.0));

  float crest = 1.0 - smoothstep(0.35, 0.75, abs(wave));
  col = mix(col, cMint, crest * (0.55 + h * 0.3));

  float spec = exp(-abs(wave - 0.6) * 12.0);
  col = mix(col, cGold, spec * (0.65 + h * 0.4));

  gl_FragColor = vec4(clamp(col * 1.12, 0.0, 1.0), 1.0);
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
  <meta property="og:image" content="https://infinitedevices.org/assets/og/embed-{item['slug']}.jpg">
  <meta name="twitter:card" content="summary_large_image">
  <meta name="twitter:title" content="{item['title']}: audio-reactive visual">
  <meta name="twitter:description" content="{item['embed_desc']}">
  <meta name="twitter:image" content="https://infinitedevices.org/assets/og/embed-{item['slug']}.jpg">
  <style>
    * {{ box-sizing: border-box; margin: 0; padding: 0; }}
    html, body {{
      width: 100%;
      height: 100%;
      overflow: hidden;
      background: #0b132b;
      color: #f4efe6;
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
    print("=== Creative Expansion Pack 28: Build and Gate Runner ===")
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
  c bg 0.012 0.082 0.165
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
  i note2 {nr+5}
  i note3 {nr+7}
  i note4 {nr}
  i note5 {nr+3}
  i note6 {nr+7}
  i note7 {nr+10}
  i note8 {nr}
  i note9 {nr}
  i note10 {nr+5}
  i note11 {nr+12}
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
  c fillColor 0.16 0.24 0.35
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
node 5 Modulators Audio Analyze
  id ana
  pos 0 2200
end
node 6 Source Shape
  id pic
  pos 0 162
  c fillColor 0.14 0.20 0.30
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
