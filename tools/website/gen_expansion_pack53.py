#!/usr/bin/env python3
"""Build and Gate Runner for Creative Expansion Pack 53.
Generates 10 items across the 5 core creative families:
1. Sketch 2D: vasarely-sophia-contour-shift, lewitt-polychrome-wall-planes
2. Sketch 3D: calatrava-ribbed-skylight-3d, schroeder-qrd-diffuser-3d
3. Field Synth: reed-clarinet-waveguide-synth, pulsar-train-formant-synth
4. Field Effects: diode-ladder-overdrive-fx, envelope-svf-autofilter-fx
5. Field Pixel & Embeds: moire-optical-fringes, vasarely-kinetic-mural
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
    ALT_BIN = "/Users/namansoni/Infinite/infinte-base/build/Infinite.app/Contents/MacOS/Infinite"
    if os.path.exists(ALT_BIN):
        BIN = ALT_BIN
    else:
        BIN = os.path.expanduser("~/.local/bin/Infinite")
CMD = [BIN, "--cli"] if BIN.endswith("Contents/MacOS/Infinite") else [BIN]

LIB_DIR = os.path.join(ROOT, "website", "assets", "library")
SKETCH_DIR = os.path.join(LIB_DIR, "sketch")
PIXEL_DIR = os.path.join(LIB_DIR, "pixel")
PREVIEWS_DIR = os.path.join(LIB_DIR, "previews")
THUMBS_DIR = os.path.join(LIB_DIR, "thumbs")
TEMPLATES_DIR = os.path.join(ROOT, "website", "assets", "templates")
EMBEDS_DIR = os.path.join(ROOT, "website", "embeds")

TMP_DIR = "/tmp/pack53_build"
os.makedirs(TMP_DIR, exist_ok=True)

sys.path.insert(0, os.path.join(ROOT, "tools", "universe"))
import library
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
import template_preview


NEW_ART_STYLES = [
    {
        "id": "vasarely_sophia_relief",
        "taxonomy": "new",
        "name": "Vasarely Sophia Kinetic Contour Relief",
        "palette": ["#8E8B84", "#F2F1EC", "#141517", "#4F4D48"],
        "sources": ["Sketch 2D"],
        "fx": ["vignette"],
        "comp": [],
        "ban": ["bloom", "glitch"],
        "motion": 0.2,
        "pairs": ["pulsar_train_formant"],
        "refs": [
            "Victor Vasarely Sophia (1954) UCV architectural black-on-ivory line-shift relief",
            "Steps Mono (Velvetyne) & Commit Mono (Uncut) brutalist kinetic type"
        ]
    },
    {
        "id": "lewitt_polychrome_planes",
        "taxonomy": "new",
        "name": "LeWitt Polychrome Wall Planes",
        "palette": ["#E62A14", "#1A82E0", "#14961B", "#58328C"],
        "sources": ["Sketch 2D"],
        "fx": ["vignette"],
        "comp": [],
        "ban": ["bloom", "glitch"],
        "motion": 0.15,
        "pairs": ["reed_clarinet_waveguide"],
        "refs": [
            "Sol LeWitt Wall Drawing 831 (Geometric Forms) matte polychrome wall planes",
            "Clash Display (Fontshare) & Azeret Mono (Fontshare) architectural exhibition type"
        ]
    },
    {
        "id": "calatrava_ribbed_vault",
        "taxonomy": "new",
        "name": "Calatrava Ribbed Steel & Glass Vault",
        "palette": ["#14181D", "#F4F5F2", "#5E8894", "#263B44"],
        "sources": ["Sketch 3D"],
        "fx": ["vignette"],
        "comp": [],
        "ban": ["glitch", "pixelate"],
        "motion": 0.25,
        "pairs": ["envelope_svf_autofilter"],
        "refs": [
            "Santiago Calatrava Liege-Guillemins station white steel ribs and sea-glass coffers",
            "Cabinet Grotesk (Fontshare) & Fragment Mono (Uncut) structural engineering type"
        ]
    },
    {
        "id": "schroeder_qrd_columns",
        "taxonomy": "new",
        "name": "Schroeder QRD Timber Acoustic Diffuser",
        "palette": ["#181412", "#D8B98A", "#8C5E3C", "#3B281A"],
        "sources": ["Sketch 3D"],
        "fx": ["vignette"],
        "comp": [],
        "ban": ["glitch", "bloom"],
        "motion": 0.2,
        "pairs": ["diode_ladder_overdrive"],
        "refs": [
            "Manfred Schroeder quadratic-residue prime N=17 timber acoustic diffuser skyline",
            "Steps Mono (Velvetyne) & Azeret Mono (Fontshare) studio acoustics type"
        ]
    },
    {
        "id": "moire_optical_fringes",
        "taxonomy": "new",
        "name": "Moire Mirror Curvature Interference",
        "palette": ["#181A1C", "#6D7175", "#D9DCE0", "#3D4147"],
        "sources": ["Field Pixel"],
        "fx": ["vignette"],
        "comp": [],
        "ban": ["bloom"],
        "motion": 0.35,
        "pairs": ["envelope_svf_autofilter"],
        "refs": [
            "Optical mirror curvature inspection by concentric Moire interference fringes",
            "Steps Mono (Velvetyne) & Martian Mono (Uncut) metrology optics type"
        ]
    },
    {
        "id": "vasarely_kinetic_mural",
        "taxonomy": "new",
        "name": "Vasarely UCV Kinetic Tile Mural",
        "palette": ["#171513", "#DFC442", "#8C4A27", "#B87A3D"],
        "sources": ["Field Pixel"],
        "fx": ["vignette"],
        "comp": [],
        "ban": ["glitch", "pixelate"],
        "motion": 0.3,
        "pairs": ["pulsar_train_formant"],
        "refs": [
            "Victor Vasarely UCV ceramic tile mural in ochre-gold, burnt sienna, and charcoal",
            "Terminal Grotesque (Velvetyne) & Azeret Mono (Fontshare) modernist mural type"
        ]
    }
]


NEW_MUSIC_STYLES = [
    {
        "id": "reed_clarinet_waveguide",
        "family": "experimental",
        "bpm": [84, 108],
        "swing": 0.04,
        "scale": [4, 1],
        "drums": {},
        "bass": "0.....3.0.....5.",
        "fx": ["Field Effect", "Reverb", "Delay"],
        "synth": ["Field Synth"],
        "ban": ["Bitcrusher", "Stutter"],
        "glitch": 0.0,
        "mod": 0.35,
        "refs": ["Nonlinear cubic reed pressure table coupled to cylindrical bore waveguide"],
        "sidechain": False,
        "chords": {"size": [3], "beats": 4.0, "steps": 4, "strum": 12.0},
        "notefx": [],
        "motif": {"synth": ["Field Synth"], "density": 0.4, "octave": 2, "gate": 75, "gain": -10.0},
        "groove": 0.3
    },
    {
        "id": "pulsar_train_formant",
        "family": "experimental",
        "bpm": [100, 124],
        "swing": 0.02,
        "scale": [6, 9],
        "drums": {},
        "bass": "0..2..4.0..5..7.",
        "fx": ["Field Effect", "Delay", "Reverb"],
        "synth": ["Field Synth"],
        "ban": ["Bitcrusher"],
        "glitch": 0.05,
        "mod": 0.45,
        "refs": ["Curtis Roads pulsar synthesis with windowed formant bursts and duty-cycle sweep"],
        "sidechain": False,
        "chords": {"size": [3, 4], "beats": 4.0, "steps": 4, "strum": 8.0},
        "notefx": [],
        "motif": {"synth": ["Field Synth"], "density": 0.5, "octave": 2, "gate": 65, "gain": -10.0},
        "groove": 0.4
    },
    {
        "id": "diode_ladder_overdrive",
        "family": "techno",
        "bpm": [116, 132],
        "swing": 0.0,
        "scale": [1, 5],
        "drums": {},
        "bass": "0.0.3.0.5.0.7.3.",
        "fx": ["Field Effect", "Audio Filter", "Drive"],
        "synth": ["Analog"],
        "ban": ["Bitcrusher"],
        "glitch": 0.0,
        "mod": 0.5,
        "refs": ["Four-stage nonlinear tanh diode ladder filter with asymmetric drive"],
        "sidechain": False,
        "chords": {"size": [3], "beats": 2.0, "steps": 4, "strum": 5.0},
        "notefx": [],
        "motif": {"synth": ["Analog"], "density": 0.55, "octave": 1, "gate": 55, "gain": -10.0},
        "groove": 0.4
    },
    {
        "id": "envelope_svf_autofilter",
        "family": "house",
        "bpm": [112, 124],
        "swing": 0.08,
        "scale": [4, 10],
        "drums": {},
        "bass": "0..0.2..3..5.3..",
        "fx": ["Field Effect", "Delay", "Chorus"],
        "synth": ["Analog"],
        "ban": ["Bitcrusher"],
        "glitch": 0.0,
        "mod": 0.55,
        "refs": ["Dual-ballistics envelope follower sweeping resonant state-variable filter"],
        "sidechain": False,
        "chords": {"size": [3, 4], "beats": 4.0, "steps": 4, "strum": 10.0},
        "notefx": [],
        "motif": {"synth": ["Analog"], "density": 0.5, "octave": 2, "gate": 60, "gain": -10.0},
        "groove": 0.45
    }
]


SKETCH_2D_ITEMS = [
    {
        "slug": "vasarely-sophia-contour-shift",
        "title": "Vasarely Sophia Contour Shift",
        "description": "Kinetic black-on-ivory parallel line relief with stepped diagonal and circular contour displacements framed in brutalist concrete.",
        "tags": ["vasarely", "op-art", "kinetic", "contour", "relief", "monochrome", "sketch"],
        "code": """// Vasarely Sophia Contour Shift - Infinite Library (Sketch 2D)
param("lines", 74, 40, 120);
param("shiftAmp", 18.0, 4.0, 36.0);
param("panels", 3, 2, 4);
param("drift", 0.14, 0.0, 0.5);

function clamp(v, lo, hi) {
  return min(max(v, lo), hi);
}

function draw(t) {
  // Brutalist raw concrete ground (#8E8B84)
  background(0.557, 0.545, 0.518);
  const w = width;
  const h = height;
  const margin = min(w, h) * 0.085;
  const panelGap = 22.0;
  const totalW = w - margin * 2.0;
  const panelH = h - margin * 2.0;
  const pCount = floor(panels);
  const panelW = (totalW - panelGap * (pCount - 1)) / pCount;

  // Concrete formwork tie-rod dots
  noStroke();
  fill(0.310, 0.302, 0.282, 0.65);
  for (let r = 0; r < 4; r++) {
    for (let c = 0; c < 4; c++) {
      const tx = margin * 0.5 + (c / 3.0) * (w - margin);
      const ty = margin * 0.5 + (r / 3.0) * (h - margin);
      circle(tx, ty, 3.2);
    }
  }

  const lineCount = floor(lines);
  const segs = 48;
  const phase = t * drift;

  for (let p = 0; p < pCount; p++) {
    const px = margin + p * (panelW + panelGap);
    const py = margin;

    // Ivory relief plaque (#F2F1EC)
    noStroke();
    fill(0.949, 0.945, 0.925);
    rect(px, py, panelW, panelH);

    // Subtle recessed shadow border
    noFill();
    stroke(0.24, 0.235, 0.22, 0.85);
    strokeWeight(2.2);
    rect(px, py, panelW, panelH);

    // Kinetic stepped contour lines (#141517)
    stroke(0.078, 0.082, 0.090);
    strokeWeight(1.55);

    for (let i = 1; i < lineCount; i++) {
      const v = i / lineCount;
      const baseY = py + v * panelH;
      let prevX = px + 6.0;
      let prevY = baseY;

      for (let s = 1; s <= segs; s++) {
        const u = s / segs;
        const x = px + 6.0 + u * (panelW - 12.0);
        let disp = 0.0;

        if (p % 3 === 0) {
          // Circular stepped crater + diagonal shear
          const dx = (u - 0.52) * 1.4;
          const dy = (v - 0.44) * 2.2;
          const dist = sqrt(dx * dx + dy * dy);
          if (dist < 0.56) {
            const stepR = floor((0.56 - dist) * 7.0) / 7.0;
            disp = -stepR * shiftAmp * (0.85 + 0.25 * sin(phase * 1.3));
          }
          if (u + v * 0.6 > 0.92 && u + v * 0.6 < 1.25) {
            disp += shiftAmp * 0.45;
          }
        } else if (p % 3 === 1) {
          // Vertical zigzag frequency-modulated corridor
          const band = abs(u - 0.5 + 0.14 * sin(v * PI * 3.0 + phase));
          if (band < 0.22) {
            const q = floor((0.22 - band) * 12.0) / 12.0;
            disp = q * shiftAmp * (i % 2 === 0 ? 1.15 : -0.95);
          }
        } else {
          // Dual-chevron diamond contour shift
          const d1 = abs(u - 0.5) + abs(v - 0.5) * 0.65;
          if (d1 < 0.45 && d1 > 0.12) {
            disp = (u < 0.5 ? -1.0 : 1.0) * shiftAmp * 0.65 * sin(d1 * 14.0 + phase);
          }
        }

        const curY = clamp(baseY + disp, py + 4.0, py + panelH - 4.0);
        line(prevX, prevY, x, curY);
        prevX = x;
        prevY = curY;
      }
    }
  }
}"""
    },
    {
        "slug": "lewitt-polychrome-wall-planes",
        "title": "LeWitt Polychrome Wall Planes",
        "description": "Intersecting architectural wall planes in matte vermilion, cobalt, emerald, and violet with fine drafting registration lines.",
        "tags": ["lewitt", "minimalism", "polychrome", "geometric", "architecture", "wall-drawing", "sketch"],
        "code": """// LeWitt Polychrome Wall Planes - Infinite Library (Sketch 2D)
param("tilt", 0.22, 0.05, 0.45);
param("hatch", 64, 24, 110);
param("plinth", 0.20, 0.12, 0.32);
param("drift", 0.08, 0.0, 0.35);

function draw(t) {
  // Warm gallery concrete floor (#928E88)
  background(0.573, 0.557, 0.533);
  const w = width;
  const h = height;
  const sway = sin(t * drift) * 0.035;

  // Architectural room corner vertex
  const cx = w * (0.46 + sway);
  const cy = h * (0.64 - sway * 0.5);
  const topY = h * 0.08;
  const botY = h * (1.0 - plinth);

  // Left wall upper vermilion plane (#E62A14)
  noStroke();
  fill(0.902, 0.165, 0.078);
  beginShape();
  vertex(0, 0);
  vertex(cx, 0);
  vertex(cx, cy - h * 0.14);
  vertex(0, h * (0.72 + tilt * 0.25));
  endShape(CLOSE);

  // Left wall cobalt blue triangle (#1A82E0)
  fill(0.102, 0.510, 0.878);
  beginShape();
  vertex(0, h * (0.72 + tilt * 0.25));
  vertex(cx, cy - h * 0.14);
  vertex(cx, cy);
  vertex(0, botY + h * 0.09);
  endShape(CLOSE);

  // Center-right vermilion wall (#DC3216)
  fill(0.863, 0.196, 0.086);
  beginShape();
  vertex(cx, 0);
  vertex(w * 0.80, h * 0.12);
  vertex(w * 0.80, cy + h * 0.08);
  vertex(cx, cy);
  endShape(CLOSE);

  // Upper violet ceiling-corner wedge (#58328C)
  fill(0.345, 0.196, 0.549);
  beginShape();
  vertex(cx, 0);
  vertex(w, 0);
  vertex(w, h * 0.09);
  vertex(w * 0.80, h * 0.12);
  endShape(CLOSE);

  // Rightmost emerald green wall (#14961B)
  fill(0.078, 0.588, 0.106);
  beginShape();
  vertex(w * 0.80, h * 0.12);
  vertex(w, h * 0.09);
  vertex(w, cy + h * 0.13);
  vertex(w * 0.80, cy + h * 0.08);
  endShape(CLOSE);

  // Baseboard architectural concrete dado (#8A8680)
  fill(0.541, 0.525, 0.502);
  beginShape();
  vertex(0, botY + h * 0.09);
  vertex(cx, cy);
  vertex(w * 0.80, cy + h * 0.08);
  vertex(w, cy + h * 0.13);
  vertex(w, h);
  vertex(0, h);
  endShape(CLOSE);

  // Architectural exhibition light-track & matte-ivory upper band
  fill(0.955, 0.945, 0.920);
  rect(0, 0, w, h * 0.065);
  stroke(0.08, 0.08, 0.09);
  strokeWeight(4.0);
  line(0, h * 0.065, w, h * 0.065);

  // Fine LeWitt parallel ink hatching across each wall plane
  const nH = floor(hatch);
  // Left vermilion/cobalt wall: crisp ivory diagonal 45-deg drafting lines
  stroke(0.98, 0.97, 0.94, 0.65);
  strokeWeight(1.8);
  for (let i = -nH; i < nH * 2; i += 2) {
    const x0 = (i / nH) * cx;
    line(x0, h * 0.065, x0 + h * 0.45, h * 0.515);
  }
  // Center-right vermilion wall: crisp dark-ink vertical parallel lines
  stroke(0.08, 0.08, 0.09, 0.58);
  strokeWeight(1.8);
  for (let i = 1; i < nH; i += 2) {
    const x = cx + (i / nH) * (w * 0.80 - cx);
    line(x, h * 0.065, x, cy + h * 0.06);
  }
  // Right emerald wall: crisp ivory horizontal parallel lines
  stroke(0.96, 0.97, 0.94, 0.62);
  strokeWeight(1.8);
  for (let i = 1; i < floor(nH * 0.75); i += 2) {
    const y = h * 0.10 + (i / (nH * 0.75)) * (cy - h * 0.02);
    line(w * 0.80, y, w, y);
  }

  // Bold Sol LeWitt charcoal-black architectural corner & plane dividers
  stroke(0.07, 0.07, 0.08);
  strokeWeight(4.5);
  line(cx, h * 0.065, cx, cy);
  line(w * 0.80, h * 0.12, w * 0.80, cy + h * 0.08);
  line(0, botY + h * 0.09, cx, cy);
  line(cx, cy, w * 0.80, cy + h * 0.08);
  line(w * 0.80, cy + h * 0.08, w, cy + h * 0.13);
  line(0, h * (0.72 + tilt * 0.25), cx, cy - h * 0.14);

  // Dark charcoal concrete floor slab expansion joints
  stroke(0.14, 0.14, 0.15, 0.75);
  strokeWeight(2.0);
  for (let i = 1; i <= 5; i++) {
    const u = i / 6.0;
    line(cx + (u - 0.5) * 60.0, cy, u * w, h);
  }
  for (let r = 1; r <= 3; r++) {
    const fy = cy + (r / 4.0) * (h - cy);
    line(0, fy, w, fy);
  }
}"""
    }
]


SKETCH_3D_ITEMS = [
    {
        "slug": "calatrava-ribbed-skylight-3d",
        "title": "Calatrava Ribbed Skylight 3D",
        "description": "Sweeping white steel structural arches framing recessed sea-glass skylight coffers over a dark basalt concourse plinth.",
        "tags": ["calatrava", "architecture", "vault", "skylight", "ribs", "3d", "sketch"],
        "code": """// Calatrava Ribbed Skylight 3D - Infinite Library (Sketch 3D)
param("arches", 14, 8, 24);
param("span", 2.5, 1.6, 3.6);
param("archHeight", 1.35, 0.8, 2.0);
param("spin", 0.14, 0.0, 0.5);

function draw(t) {
  const n = floor(arches);
  rotateY(t * spin + 0.48);
  rotateX(0.24);

  // Dark slate concourse plinth
  push();
  translate(0, -0.76, 0);
  fill(0.078, 0.094, 0.114);
  box(span * 1.18, 0.08, span * 1.18);
  pop();

  // Rhythmic Calatrava-style white steel ribs & sea-glass skylight coffers
  for (let i = 0; i < n; i++) {
    const u = i / (n - 1);
    const z = (u - 0.5) * span * 0.92;
    const wave = 0.12 * sin(u * PI * 2.0 + t * 0.6);

    // Primary chalk-white parabolic arch rib (built from angled segments)
    const segs = 9;
    for (let s = 0; s < segs; s++) {
      const f = (s / (segs - 1)) * 2.0 - 1.0;
      const x = f * (span * 0.48);
      const y = -0.62 + (1.0 - f * f) * (archHeight + wave);
      const slope = -2.0 * f * (archHeight + wave) / (span * 0.48);
      const tilt = atan2(slope, 1.0);

      push();
      translate(x, y, z);
      rotateZ(tilt);
      fill(0.957, 0.961, 0.949);
      box(span * 0.14, 0.055, 0.055);
      pop();

      // Recessed sea-glass coffer panes between ribs
      if (i < n - 1 && s > 0 && s < segs - 1 && s % 2 === 1) {
        push();
        translate(x, y - 0.05, z + (span * 0.92) / (n - 1) * 0.5);
        rotateZ(tilt);
        fill(0.369, 0.533, 0.580);
        box(span * 0.11, 0.025, (span * 0.92) / (n - 1) * 0.72);
        pop();
      }
    }

    // Diagonal tension tie-struts
    push();
    translate(-span * 0.48, -0.68, z);
    fill(0.149, 0.231, 0.267);
    box(0.06, 0.18, 0.06);
    pop();
    push();
    translate(span * 0.48, -0.68, z);
    fill(0.149, 0.231, 0.267);
    box(0.06, 0.18, 0.06);
    pop();
  }
}"""
    },
    {
        "slug": "schroeder-qrd-diffuser-3d",
        "title": "Schroeder QRD Diffuser 3D",
        "description": "Two-dimensional quadratic residue acoustic diffuser skyline of Baltic birch and walnut timber wells following prime N=17 modular depths.",
        "tags": ["schroeder", "qrd", "acoustics", "diffuser", "timber", "modular", "3d", "sketch"],
        "code": """// Schroeder QRD Diffuser 3D - Infinite Library (Sketch 3D)
param("gridN", 11, 7, 15);
param("primeP", 17, 7, 23);
param("depth", 1.15, 0.5, 1.8);
param("spin", 0.16, 0.0, 0.5);

function draw(t) {
  const n = floor(gridN);
  const p = floor(primeP);
  const span = 2.2;
  const cell = span / n;

  rotateY(t * spin + 0.62);
  rotateX(0.42);

  // Obsidian acoustic backing frame
  push();
  translate(0, -0.42, 0);
  fill(0.094, 0.078, 0.071);
  box(span * 1.08, 0.12, span * 1.08);
  pop();

  for (let ix = 0; ix < n; ix++) {
    for (let iz = 0; iz < n; iz++) {
      const q = (ix * ix + iz * iz) % p;
      const u = q / p;
      const breathe = 0.06 * sin(t * 1.4 + q * 0.5);
      const h = 0.18 + (u + breathe) * depth;
      const x = (ix - (n - 1) * 0.5) * cell;
      const z = (iz - (n - 1) * 0.5) * cell;

      push();
      translate(x, -0.36 + h * 0.5, z);
      // Warm birch -> walnut -> espresso timber gradient by well height
      const r = 0.231 + u * 0.616;
      const g = 0.157 + u * 0.569;
      const b = 0.102 + u * 0.439;
      fill(r, g, b);
      box(cell * 0.86, h, cell * 0.86);
      pop();
    }
  }
}"""
    }
]


SYNTH_ITEMS = [
    {
        "slug": "reed-clarinet-waveguide-synth",
        "title": "Reed Clarinet Waveguide Synth",
        "description": "Physical-modeled woodwind synthesizer coupling a nonlinear cubic reed pressure valve to a cylindrical bore delay resonator and breath exciter.",
        "notes": [60, 63, 67, 70, 67, 65, 63, 60, 58, 60, 63, 67, 72, 70, 67, 63],
        "params": {
            "breath": 0.78,
            "stiffness": 0.42,
            "boreDamp": 0.64,
            "vibratoHz": 4.8,
            "vibratoAmt": 0.012
        },
        "code": """param float breath = 0.78 [0.2, 1.2];
param float stiffness = 0.42 [0.1, 0.9];
param float boreDamp = 0.64 [0.2, 0.95];
param float vibratoHz = 4.8 [0.5, 8.0];
param float vibratoAmt = 0.012 [0.0, 0.05];
state float p1 = 0; state float p2 = 0; state float p3 = 0;
state float vib = 0; state float env = 0;
state float lp1 = 0; state float bp1 = 0;

env = if(gate > 0.5, env + (1.0 - env) * 0.0045, env * 0.9985);
vib = (vib + vibratoHz / sr) % 1.0;
f0 = clamp(freq * (1.0 + sin(6.2831853 * vib) * vibratoAmt), 65.0, 2400.0);

p1 = (p1 + f0 / sr) % 1.0;
p2 = (p2 + (f0 * 3.0) / sr) % 1.0;
p3 = (p3 + (f0 * 5.0) / sr) % 1.0;

# Odd-harmonic cylindrical bore + nonlinear reed valve shaping
bore = sin(6.2831853 * p1) + sin(6.2831853 * p2) * 0.44 + sin(6.2831853 * p3) * 0.20;
dp = breath * env - bore * (0.45 + stiffness * 0.35);
reed = clamp(dp - 0.35 * dp * dp * dp, -1.0, 1.0);

g = clamp(2.0 * sin(3.14159265 * (900.0 + boreDamp * 2600.0) / sr), 0.05, 0.85);
lp1 = lp1 + g * (reed * env - lp1);
bp1 = bp1 + 0.14 * (lp1 - bp1);

out = (lp1 * 0.72 + bp1 * 0.28) * 0.68;
"""
    },
    {
        "slug": "pulsar-train-formant-synth",
        "title": "Pulsar Train Formant Synth",
        "description": "Windowed pulsar-train synthesizer with independent formant frequency sweep, variable duty cycle, and warm body resonance.",
        "notes": [48, 55, 60, 62, 67, 62, 60, 55, 50, 57, 62, 64, 69, 64, 62, 57],
        "params": {
            "formantRatio": 3.4,
            "duty": 0.42,
            "sweepAmt": 1.6,
            "bodyHz": 680.0,
            "decay": 0.9991
        },
        "code": """param float formantRatio = 3.4 [1.2, 8.0];
param float duty = 0.42 [0.15, 0.85];
param float sweepAmt = 1.6 [0.0, 4.0];
param float bodyHz = 680.0 [220.0, 1800.0];
param float decay = 0.9991 [0.996, 0.9996];
state float p0 = 0; state float env = 0; state float formEnv = 0;
state float bp1 = 0; state float lp1 = 0;

env = if(gate > 0.5, env + (1.0 - env) * 0.025, env * decay);
formEnv = if(gate > 0.5, formEnv * 0.9988 + 0.0012, 1.0);

f0 = clamp(freq, 55.0, 2200.0);
p0 = (p0 + f0 / sr) % 1.0;

# Windowed pulsar train: raised-cosine window over duty fraction
u = clamp(p0 / max(0.08, duty), 0.0, 1.0);
win = if(p0 < duty, 0.5 - 0.5 * cos(6.2831853 * u), 0.0);
fRatio = formantRatio + sweepAmt * (1.0 - formEnv);
pulsar = sin(6.2831853 * u * fRatio) * win * env;
sub = sin(6.2831853 * p0) * 0.42 * env;

sig = pulsar * 0.85 + sub;
g = clamp(2.0 * sin(3.14159265 * bodyHz / sr), 0.03, 0.78);
bp1 = bp1 + g * (sig - lp1 - 0.55 * bp1);
lp1 = lp1 + g * bp1;

out = ((sig * 0.70 + bp1 * 0.45) / (1.0 + abs(sig) * 0.45)) * 0.68;
"""
    }
]


FX_ITEMS = [
    {
        "slug": "diode-ladder-overdrive-fx",
        "title": "Diode Ladder Overdrive FX",
        "description": "Four-stage nonlinear diode ladder lowpass filter with asymmetric input saturation, resonant feedback, and dry-wet blend.",
        "notes": [48, 48, 55, 48, 51, 55, 58, 55, 48, 48, 55, 48, 53, 55, 51, 50],
        "params": {
            "cutoffHz": 1450.0,
            "resonance": 0.48,
            "drive": 2.2,
            "bias": 0.14,
            "mix": 0.82
        },
        "code": """param float cutoffHz = 1450.0 [220.0, 5500.0];
param float resonance = 0.48 [0.0, 0.88];
param float drive = 2.2 [0.8, 5.0];
param float bias = 0.14 [0.0, 0.4];
param float mix = 0.82 [0.0, 1.0];
state float s1 = 0; state float s2 = 0; state float s3 = 0; state float s4 = 0;

g = clamp(2.0 * sin(3.14159265 * cutoffHz / sr), 0.02, 0.72);
k = resonance * 3.1;

x = in * drive + bias - k * s4;
satX = x / (1.0 + abs(x) * 0.65);

s1 = s1 + g * (satX - s1);
s2 = s2 + g * (s1 / (1.0 + abs(s1) * 0.25) - s2);
s3 = s3 + g * (s2 / (1.0 + abs(s2) * 0.25) - s3);
s4 = s4 + g * (s3 - s4);

wet = s4 / (1.0 + abs(s4) * 0.4);
out = (in * (1.0 - mix) + wet * mix) * 0.72;
"""
    },
    {
        "slug": "envelope-svf-autofilter-fx",
        "title": "Envelope SVF Autofilter FX",
        "description": "Dual-ballistics envelope follower sweeping a resonant state-variable bandpass and lowpass filter with harmonic warmth.",
        "notes": [55, 60, 62, 67, 55, 62, 65, 67, 53, 58, 60, 65, 55, 60, 64, 67],
        "params": {
            "baseHz": 380.0,
            "sweepHz": 2800.0,
            "resonance": 0.62,
            "sens": 2.4,
            "mix": 0.85
        },
        "code": """param float baseHz = 380.0 [120.0, 1400.0];
param float sweepHz = 2800.0 [400.0, 6000.0];
param float resonance = 0.62 [0.1, 0.9];
param float sens = 2.4 [0.5, 5.0];
param float mix = 0.85 [0.0, 1.0];
state float env = 0; state float lp = 0; state float bp = 0;

lvl = abs(in);
env = if(lvl > env, env + (lvl - env) * 0.015, env + (lvl - env) * 0.00045);

fc = clamp(baseHz + clamp(env * sens, 0.0, 1.0) * sweepHz, 120.0, 7200.0);
g = clamp(2.0 * sin(3.14159265 * fc / sr), 0.02, 0.78);
q = clamp(1.2 - resonance * 0.95, 0.15, 1.2);

hp = in - lp - q * bp;
bp = bp + g * hp;
lp = lp + g * bp;

wet = (lp * 0.65 + bp * 0.55) / (1.0 + abs(bp) * 0.35);
out = (in * (1.0 - mix) + wet * mix) * 0.74;
"""
    }
]


PIXEL_ITEMS = [
    {
        "slug": "moire-optical-fringes",
        "title": "Moire Optical Fringes",
        "description": "Concentric silver-halide optical interference fringes with dual-center curvature warp and fine emulsion grain.",
        "embed_desc": "Audio-reactive concentric Moire optical interference fringes with silver-halide grain.",
        "tags": ["moire", "interference", "optics", "monochrome", "halide", "pixel", "shader"],
        "params": {
            "freq": 28.0,
            "offset": 0.11,
            "warp": 0.36,
            "grain": 0.14,
            "speed": 0.25
        },
        "code": """param float freq = 28.0 [10.0, 60.0];
param float offset = 0.11 [0.01, 0.30];
param float warp = 0.36 [0.0, 1.0];
param float grain = 0.14 [0.0, 0.4];
param float speed = 0.25 [0.0, 1.0];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
ca = cos(t * speed * 0.4);
sa = sin(t * speed * 0.4);
c1 = vec2(ca * offset, sa * offset);
c2 = vec2(-ca * offset, -sa * offset);

w1 = warp * 0.06 * sin(p.y * 9.0 + t * speed);
w2 = warp * 0.06 * cos(p.x * 9.0 - t * speed * 0.8);
r1 = length(p - c1 + vec2(w1, w2));
r2 = length(p - c2 - vec2(w2, w1));

f1 = 0.5 + 0.5 * cos(r1 * freq * 3.14159 - t * speed * 1.4);
f2 = 0.5 + 0.5 * cos(r2 * freq * 3.14159 + t * speed * 1.1);
fringe = mix(f1 * f2, abs(f1 - f2), 0.38);

# Fine silver-halide emulsion grain
n = fract(sin(dot(uv * res, vec2(12.9898, 78.233))) * 43758.5453);
lum = clamp(fringe + (n - 0.5) * grain, 0.0, 1.0);

cDark = vec3(0.094, 0.102, 0.110);
cMid = vec3(0.427, 0.443, 0.459);
cLight = vec3(0.851, 0.863, 0.878);
col = mix(cDark, cMid, smoothstep(0.05, 0.52, lum));
col = mix(col, cLight, smoothstep(0.48, 0.92, lum));
col = clamp(col, 0.0, 1.0);
""",
        "webgl_glsl": """  vec2 uv = gl_FragCoord.xy / res;
  vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
  float spd = 0.25;
  float ca = cos(t * spd * 0.4);
  float sa = sin(t * spd * 0.4);
  float off = 0.11 + audio_mid * 0.14;
  vec2 c1 = vec2(ca * off, sa * off);
  vec2 c2 = vec2(-ca * off, -sa * off);

  float wrp = 0.36 + audio_bass * 0.45;
  float w1 = wrp * 0.06 * sin(p.y * 9.0 + t * spd);
  float w2 = wrp * 0.06 * cos(p.x * 9.0 - t * spd * 0.8);
  float r1 = length(p - c1 + vec2(w1, w2));
  float r2 = length(p - c2 - vec2(w2, w1));

  float fq = 28.0 + audio_bass * 16.0;
  float f1 = 0.5 + 0.5 * cos(r1 * fq * 3.14159 - t * spd * 1.4);
  float f2 = 0.5 + 0.5 * cos(r2 * fq * 3.14159 + t * spd * 1.1);
  float fringe = mix(f1 * f2, abs(f1 - f2), 0.38);

  float n = fract(sin(dot(uv * res, vec2(12.9898, 78.233))) * 43758.5453);
  float lum = clamp(fringe + (n - 0.5) * (0.14 + audio_high * 0.14), 0.0, 1.0);

  vec3 cDark = vec3(0.094, 0.102, 0.110);
  vec3 cMid = vec3(0.427, 0.443, 0.459);
  vec3 cLight = vec3(0.851, 0.863, 0.878);
  vec3 col = mix(cDark, cMid, smoothstep(0.05, 0.52, lum));
  col = mix(col, cLight, smoothstep(0.48, 0.92, lum));
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);"""
    },
    {
        "slug": "vasarely-kinetic-mural",
        "title": "Vasarely Kinetic Mural",
        "description": "Ceramic architectural tile grid with sweeping kinetic curved cutouts in ochre-gold, burnt sienna, and deep charcoal.",
        "embed_desc": "Audio-reactive Vasarely-inspired kinetic ceramic tile mural in ochre-gold and burnt sienna.",
        "tags": ["vasarely", "mural", "tiles", "kinetic", "ochre", "sienna", "pixel", "shader"],
        "params": {
            "gridSize": 18.0,
            "curveAmp": 0.42,
            "grout": 0.06,
            "speed": 0.22,
            "warmth": 0.78
        },
        "code": """param float gridSize = 18.0 [8.0, 32.0];
param float curveAmp = 0.42 [0.1, 0.9];
param float grout = 0.06 [0.01, 0.15];
param float speed = 0.22 [0.0, 1.0];
param float warmth = 0.78 [0.2, 1.0];

p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
cellId = floor(uv * gridSize);
cellUv = fract(uv * gridSize);
groutMask = step(grout, cellUv.x) * step(grout, cellUv.y) * step(cellUv.x, 1.0 - grout) * step(cellUv.y, 1.0 - grout);

# Sample center of each ceramic tile for crisp mosaic look
tc = (cellId + 0.5) / gridSize;
tp = vec2((tc.x - 0.5) * aspect, tc.y - 0.5);

band1 = sin(tp.x * 7.5 + cos(tp.y * 5.2 + t * speed) * curveAmp * 3.0);
band2 = cos(tp.y * 6.5 - sin(tp.x * 4.8 - t * speed * 0.7) * curveAmp * 2.6);
zone = band1 * 0.6 + band2 * 0.4;

cCharcoal = vec3(0.090, 0.082, 0.075);
cSienna = vec3(0.549, 0.290, 0.153);
cAmber = vec3(0.722, 0.478, 0.239);
cGold = vec3(0.875, 0.769, 0.259);

tileCol = mix(cCharcoal, cSienna, smoothstep(-0.35, -0.05, zone));
tileCol = mix(tileCol, cAmber, smoothstep(0.02, 0.35, zone));
tileCol = mix(tileCol, cGold, smoothstep(0.32, 0.68, zone) * warmth);

# Subtle ceramic glaze sheen per tile
sheen = 0.92 + 0.08 * sin(cellId.x * 12.7 + cellId.y * 31.3);
col = mix(cCharcoal * 0.65, tileCol * sheen, groutMask);
col = clamp(col, 0.0, 1.0);
""",
        "webgl_glsl": """  vec2 uv = gl_FragCoord.xy / res;
  float gs = 18.0 + floor(audio_bass * 6.0);
  vec2 cellId = floor(uv * gs);
  vec2 cellUv = fract(uv * gs);
  float grout = 0.06;
  float groutMask = step(grout, cellUv.x) * step(grout, cellUv.y) * step(cellUv.x, 1.0 - grout) * step(cellUv.y, 1.0 - grout);

  vec2 tc = (cellId + 0.5) / gs;
  vec2 tp = vec2((tc.x - 0.5) * aspect, tc.y - 0.5);
  float spd = 0.22;
  float cAmp = 0.42 + audio_mid * 0.45;

  float band1 = sin(tp.x * 7.5 + cos(tp.y * 5.2 + t * spd) * cAmp * 3.0);
  float band2 = cos(tp.y * 6.5 - sin(tp.x * 4.8 - t * spd * 0.7) * cAmp * 2.6);
  float zone = band1 * 0.6 + band2 * 0.4;

  vec3 cCharcoal = vec3(0.090, 0.082, 0.075);
  vec3 cSienna = vec3(0.549, 0.290, 0.153);
  vec3 cAmber = vec3(0.722, 0.478, 0.239);
  vec3 cGold = vec3(0.875, 0.769, 0.259);

  vec3 tileCol = mix(cCharcoal, cSienna, smoothstep(-0.35, -0.05, zone));
  tileCol = mix(tileCol, cAmber, smoothstep(0.02, 0.35, zone));
  tileCol = mix(tileCol, cGold, smoothstep(0.32, 0.68, zone) * (0.78 + audio_high * 0.3));

  float sheen = 0.92 + 0.08 * sin(cellId.x * 12.7 + cellId.y * 31.3);
  vec3 col = mix(cCharcoal * 0.65, tileCol * sheen, groutMask);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);"""
    }
]


def make_embed_html(item):
    slug = item["slug"]
    title = item["title"]
    desc = item["embed_desc"]
    glsl = item["webgl_glsl"]
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
  <meta name="theme-color" content="#151930">
  <title>{title} | Infinite</title>
  <meta name="description" content="{desc}">
  <link rel="canonical" href="https://infinitedevices.org/embeds/{slug}.html">
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
    <canvas id="gl" role="img" aria-label="{title}, an audio-reactive visual"></canvas>
  <div class="controls">
    <button class="btn" id="btn-audio" type="button" aria-label="Enable microphone" title="Enable microphone"></button>
    <button class="btn" id="btn-share" type="button" aria-label="Share" title="Share"></button>
  </div>
  <a href="https://infinitedevices.org/" target="_blank" rel="noopener" class="badge" title="Infinite, a DAW for you">
    <span class="badge-icon" aria-hidden="true"></span>
    <span class="sr-only">made in infinite</span>
  </a>
  </main>
  <script>
    const canvas = document.getElementById("gl");
    const gl = canvas.getContext("webgl") || canvas.getContext("experimental-webgl");
    const vsSource = `
      attribute vec2 position;
      void main() {{ gl_Position = vec4(position, 0.0, 1.0); }}
    `;
    const fsSource = `
      precision highp float;
      uniform vec2 res;
      uniform float t;
      uniform float aspect;
      uniform float audio_bass;
      uniform float audio_mid;
      uniform float audio_high;
      void main() {{
{glsl}
      }}
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
</html>"""


def write_notes_block(notes):
    lines = []
    for idx, n in enumerate(notes):
        lines.append(f"  i note{idx} {n}")
        lines.append(f"  b en{idx} 1")
        lines.append(f"  f vel{idx} 0.86")
    return "\n".join(lines)


def update_style_catalogs():
    art_path = os.path.join(ROOT, "tools", "catalog", "styles_art.json")
    art = json.load(open(art_path))
    art_ids = {s["id"] for s in art["styles"]}
    added_art = 0
    for s in NEW_ART_STYLES:
        if s["id"] not in art_ids:
            art["styles"].append(s)
            added_art += 1
    if added_art:
        with open(art_path, "w") as f:
            json.dump(art, f, indent=2)

    mus_path = os.path.join(ROOT, "tools", "catalog", "styles_music.json")
    mus = json.load(open(mus_path))
    mus_ids = {s["id"] for s in mus["styles"]}
    added_mus = 0
    for s in NEW_MUSIC_STYLES:
        if s["id"] not in mus_ids:
            mus["styles"].append(s)
            added_mus += 1
    if added_mus:
        with open(mus_path, "w") as f:
            json.dump(mus, f, indent=2)
    print(f"Style catalogs updated (+{added_art} art, +{added_mus} music)")


def main():
    update_style_catalogs()
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

        frame_png = os.path.join(TMP_DIR, f"{slug}.png")
        fp = subprocess.run(CMD + ["frame", test_inf, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        if '"ok":true' not in fp.stdout or not os.path.exists(frame_png):
            print(f"FAIL frame: {slug}\n{fp.stdout}\n{fp.stderr}")
            failed = True
            continue

        im = Image.open(frame_png)
        lum = ImageStat.Stat(im.convert("L")).mean[0]
        sc = library.interest(frame_png)
        if not (14.0 <= lum <= 228.0) or sc < 0.45:
            print(f"FAIL metrics {slug}: lum={lum:.1f}, interest={sc:.3f}")
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
  id sk3d
  pos 0 0
  s code {escaped}
end
node 2 3D Render 3D
  id r3d
  pos 360 0
  f width 1080
  f height 1080
end
node 3 Utility Output
  id out
  pos 720 0
end
geo r3d 0 sk3d
cable out 0 r3d
"""
        test_inf = os.path.join(TMP_DIR, f"{slug}.inf")
        with open(test_inf, "w") as f:
            f.write(inf_content)

        v = subprocess.run(CMD + ["validate", test_inf], capture_output=True, text=True)
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}\n{v.stderr}")
            failed = True
            continue

        frame_png = os.path.join(TMP_DIR, f"{slug}.png")
        fp = subprocess.run(CMD + ["frame", test_inf, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        if '"ok":true' not in fp.stdout or not os.path.exists(frame_png):
            print(f"FAIL frame: {slug}\n{fp.stdout}\n{fp.stderr}")
            failed = True
            continue

        im = Image.open(frame_png)
        lum = ImageStat.Stat(im.convert("L")).mean[0]
        sc = library.interest(frame_png)
        if not (14.0 <= lum <= 228.0) or sc < 0.45:
            print(f"FAIL metrics {slug}: lum={lum:.1f}, interest={sc:.3f}")
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
        notes_blk = write_notes_block(it["notes"])
        inf_content = f"""infinite-patch 1
# {it['title']}
node 1 Notes Note Sequencer
  id notes
  pos 0 1024
  f rateBeats 0.5
  i steps 16
{notes_blk}
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

        sum_json = os.path.join(TMP_DIR, f"{slug}_audio.json")
        s = subprocess.run(CMD + ["audio", inf_file, sum_json], capture_output=True, text=True)
        if s.returncode != 0 or not os.path.exists(sum_json):
            print(f"FAIL audio: {slug}\n{s.stdout}\n{s.stderr}")
            failed = True
            continue

        d = json.load(open(sum_json))
        lufs = d.get("integrated_lufs")
        peak = d.get("sample_peak_dbfs")
        silence = d.get("silence_ratio")
        if lufs is None or not (-26.0 <= lufs <= -8.0) or peak is None or peak >= -0.5 or silence != 0:
            print(f"FAIL audio gates {slug}: lufs={lufs}, peak={peak}, silence={silence}")
            failed = True
            continue

        frame_png = os.path.join(TMP_DIR, f"{slug}.png")
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
        notes_blk = write_notes_block(it["notes"])
        inf_content = f"""infinite-patch 1
# {it['title']}
node 1 Notes Note Sequencer
  id notes
  pos 0 1024
  f rateBeats 0.5
  i steps 16
{notes_blk}
end
node 2 Synths Analog
  id osc
  pos 360 1024
  f cutoff 2400.0
  f resonance 0.25
  f decay 0.45
  f volume 0.85
end
node 3 AudioEffects Field Effect
  id fx
  pos 720 1024
  s code {escaped}
end
node 4 Utility Audio Out
  id speakers
  pos 1080 1024
end
node 5 Source Shape
  id pic
  pos 0 162
  c fillColor 0.28 0.22 0.16
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

        v = subprocess.run(CMD + ["validate", inf_file], capture_output=True, text=True)
        if '"ok":true' not in v.stdout:
            print(f"FAIL validate: {slug}\n{v.stdout}\n{v.stderr}")
            failed = True
            continue

        sum_json = os.path.join(TMP_DIR, f"{slug}_audio.json")
        s = subprocess.run(CMD + ["audio", inf_file, sum_json], capture_output=True, text=True)
        if s.returncode != 0 or not os.path.exists(sum_json):
            print(f"FAIL audio: {slug}\n{s.stdout}\n{s.stderr}")
            failed = True
            continue

        d = json.load(open(sum_json))
        lufs = d.get("integrated_lufs")
        peak = d.get("sample_peak_dbfs")
        silence = d.get("silence_ratio")
        if lufs is None or not (-26.0 <= lufs <= -8.0) or peak is None or peak >= -0.5 or silence != 0:
            print(f"FAIL audio gates {slug}: lufs={lufs}, peak={peak}, silence={silence}")
            failed = True
            continue

        frame_png = os.path.join(TMP_DIR, f"{slug}.png")
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

        frame_png = os.path.join(TMP_DIR, f"{slug}.png")
        fp = subprocess.run(CMD + ["frame", test_inf, "0", frame_png, "--size", "1080x1080"], capture_output=True, text=True)
        if '"ok":true' not in fp.stdout or not os.path.exists(frame_png):
            print(f"FAIL frame: {slug}\n{fp.stdout}\n{fp.stderr}")
            failed = True
            continue

        im = Image.open(frame_png)
        lum = ImageStat.Stat(im.convert("L")).mean[0]
        sc = library.interest(frame_png)
        if not (14.0 <= lum <= 228.0) or sc < 0.45:
            print(f"FAIL metrics {slug}: lum={lum:.1f}, interest={sc:.3f}")
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
                "tags": ["synth", "waveguide", "pulsar", "formant", "audio", "dsp"],
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
                "tags": ["effect", "diode", "ladder", "svf", "autofilter", "audio", "dsp"],
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
                tb_data["counts"][key] = {"font-size": 2, "hex": 6}
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
