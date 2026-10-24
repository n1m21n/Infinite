#!/usr/bin/env python3
"""Infinite Art Movements & Aesthetics Taxonomy Engine.

Defines the master taxonomy of artistic movements, visual styles, and procedural
multi-element scene generators (3D, 2D vector, typography, shaders, sound).
Used by embed_gen.py, daily_art_bot.py, and patch_maker.py.
"""

import json
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

TAXONOMY = {
    # -------------------------------------------------------------------------
    # 1. DIGITAL COUNTER-CULTURE & SCREEN AESTHETICS
    # -------------------------------------------------------------------------
    "y2k_acid": {
        "family": "digital_counterculture",
        "name": "Y2K Cyber-Acid & Liquid Chrome",
        "description": "Iridescent liquid metaballs, spiked wireframe starbursts, translucent plastic, and cybernetic rave UI overlays.",
        "tags": ["#y2k", "#acidgraphics", "#chrome", "#3d", "#creativecoding", "#audioreactive"],
        "palette": ["#ff007f", "#00ffff", "#e0e0e0", "#111122"],
        "synth": "FM",
        "note_source": "Arpeggiator",
        "sketch_2d": """
param("stars", 6, 2, 16);
param("spikes", 4, 3, 8);
param("spin", 0.4, 0.1, 2);

function draw(t) {
  background(0.04, 0.03, 0.08);
  translate(width / 2, height / 2);
  noFill();
  
  // Rotating acid starbursts with offset scaling
  for (let s = 0; s < stars; s++) {
    push();
    rotate(t * spin * (s % 2 === 0 ? 1 : -1) + s * 0.2);
    const radOuter = min(width, height) * (0.12 + s * 0.05);
    const radInner = radOuter * 0.25;
    strokeWeight(1.8);
    stroke(hsl(0.85 + s * 0.06, 0.9, 0.65), 0.85);
    
    beginShape();
    const count = spikes * 2;
    for (let i = 0; i <= count; i++) {
      const a = (i * TAU) / count;
      const r = i % 2 === 0 ? radOuter : radInner;
      vertex(cos(a) * r, sin(a) * r);
    }
    endShape();
    pop();
  }
}
""".strip(),
        "sketch_3d": """
param("count", 18, 4, 40);
param("radius", 1.4, 0.5, 3.0);
param("spin", 0.5, 0.1, 2.0);

function draw(t) {
  for (let i = 0; i < count; i++) {
    const u = i / count;
    const a = u * TAU + t * spin;
    push();
    rotateY(a);
    translate(radius, 0.35 * sin(t * 2.5 + i * 0.5), 0);
    rotateX(t * 1.2 + i);
    fill(hsl(0.8 + 0.25 * sin(u * TAU), 0.8, 0.65));
    box(0.22, 0.45, 0.22);
    pop();
  }
}
""".strip(),
        "field_pixel": """p = (uv - 0.5) * aspect; r = length(p); a = atan2(p.y, p.x); col = vec3(0.5 + 0.5 * sin(r * 18.0 - t * 3.5 + a * 4.0)) * vec3(1.0, 0.2, 0.8) + vec3(0.0, 0.8, 0.9) * (0.5 + 0.5 * cos(a * 6.0 + t));"""
    },

    "glitch_datamosh": {
        "family": "digital_counterculture",
        "name": "Glitch & Datamosh Feedback",
        "description": "Compression artifact tearing, scanline displace blocks, buffer persistence, and RGB channel displacement.",
        "tags": ["#glitchart", "#datamosh", "#generative", "#postdigital", "#infinite"],
        "palette": ["#000000", "#ff0033", "#00ff66", "#0066ff"],
        "synth": "Wavetable",
        "note_source": "Random Note Generator",
        "sketch_2d": """
param("blocks", 24, 6, 60);
param("jitter", 25, 2, 80);

function draw(t) {
  background(0.02, 0.02, 0.03);
  noStroke();
  for (let i = 0; i < blocks; i++) {
    const y = ((i * 0.618033 + t * 0.1) % 1) * height;
    const h = (sin(i * 14.2) * 0.5 + 0.5) * 30 + 4;
    const shift = sin(t * 8.0 + i * 2.3) * jitter;
    
    fill(hsl(0.95, 0.9, 0.5, 0.7));
    rect(width * 0.1 + shift, y, width * 0.8, h);
    
    fill(hsl(0.5, 0.9, 0.5, 0.6));
    rect(width * 0.1 - shift * 0.8, y + 2, width * 0.8, h);
  }
}
""".strip(),
        "sketch_3d": """
param("density", 16, 4, 32);
param("warp", 0.4, 0, 1);

function draw(t) {
  const s = 2.4 / density;
  for (let i = 0; i < density; i++) {
    for (let j = 0; j < density; j++) {
      const d = noise(i * 0.25, j * 0.25, t * 1.5);
      if (d > 0.45) {
        push();
        const disp = (sin(t * 10 + i * 4) > 0.7 ? 0.3 : 0.0) * warp;
        translate((i - density / 2) * s + disp, (d - 0.5) * 1.5, (j - density / 2) * s);
        fill(hsl(d + t * 0.1, 0.85, 0.55));
        box(s * 0.9, 0.15 + d * 0.6, s * 0.9);
        pop();
      }
    }
  }
}
""".strip(),
        "field_pixel": """p = uv; slice = floor(p.y * 32.0); shift = sin(slice * 13.1 + floor(t * 12.0)) * 0.05 * step(0.7, sin(t * 4.0 + slice)); col = vec3(sin(p.x * 40.0 + shift * 10.0), cos(p.y * 30.0), sin(shift * 20.0));"""
    },

    "ditherpunk": {
        "family": "digital_counterculture",
        "name": "1-Bit Ditherpunk & Cyber-Esoteric",
        "description": "High-contrast duo-tone stippling, 1-bit Bayer matrices, medieval woodcut styling with sacred cyber-glyphs.",
        "tags": ["#dither", "#1bit", "#obradinn", "#esoteric", "#cyberpunk", "#lowtech"],
        "palette": ["#0011ff", "#f4f1ea"],
        "synth": "FM",
        "note_source": "Random Note Generator",
        "sketch_2d": """
param("points", 4000, 500, 10000);
param("rings", 5, 1, 10);

function draw(t) {
  background(0.02, 0.05, 0.55); // Klein Blue
  translate(width / 2, height / 2);
  noStroke();
  fill(0.96, 0.95, 0.92); // Cream Ivory
  
  // Stippled concentric circles and occult rays
  for (let i = 0; i < points; i++) {
    const rIdx = i % rings;
    const rad = (min(width, height) * 0.38) * (rIdx + 1) / rings;
    const a = (i / points) * TAU * rings + t * 0.05 * (rIdx % 2 === 0 ? 1 : -1);
    const jitter = (sin(i * 91.3) * 0.5 + 0.5) * 12.0;
    circle(cos(a) * (rad + jitter), sin(a) * (rad + jitter), 1.6);
  }
}
""".strip(),
        "sketch_3d": """
param("turns", 4, 1, 8);
param("count", 80, 20, 200);

function draw(t) {
  for (let i = 0; i < count; i++) {
    const u = i / count;
    const a = u * TAU * turns + t * 0.3;
    push();
    translate(cos(a) * 0.9, (u - 0.5) * 2.2, sin(a) * 0.9);
    fill(0.96, 0.95, 0.92);
    box(0.08, 0.08, 0.08);
    pop();
  }
}
""".strip(),
        "field_pixel": """p = uv * res; gx = floor(fmod(p.x, 4.0)); gy = floor(fmod(p.y, 4.0)); dither = (gx * 3.0 + gy * 7.0) / 32.0; lum = 0.5 + 0.5 * sin(length((uv - 0.5) * aspect) * 15.0 - t * 2.0); on = step(dither, lum); col = mix(vec3(0.01, 0.08, 0.6), vec3(0.96, 0.95, 0.9), on);"""
    },

    # -------------------------------------------------------------------------
    # 2. GRAPHIC PRINT, EDITORIAL & NEO-POP
    # -------------------------------------------------------------------------
    "neo_brutalist": {
        "family": "graphic_print",
        "name": "Neo-Brutalist Swiss Poster",
        "description": "High-contrast modular grids, monospace tabular metadata, bold black borders, and stark chromatic accent punches.",
        "tags": ["#neobrutalism", "#swissposter", "#graphicdesign", "#typography", "#modular"],
        "palette": ["#ffffff", "#000000", "#0033ff", "#ff3300"],
        "synth": "Analog",
        "note_source": "Arpeggiator",
        "sketch_2d": """
param("gridCols", 4, 2, 8);
param("speed", 0.5, 0.1, 2);

function draw(t) {
  background(0.94, 0.93, 0.90);
  const pad = 36;
  const w = (width - pad * 2) / gridCols;
  const h = (height - pad * 2) / gridCols;
  
  stroke(0.05);
  strokeWeight(2.5);
  
  for (let c = 0; c < gridCols; c++) {
    for (let r = 0; r < gridCols; r++) {
      const x = pad + c * w;
      const y = pad + r * h;
      const act = sin(t * speed + c * 1.5 + r * 2.0) > 0.3;
      
      if (act) {
        fill(c % 2 === 0 ? 0.05 : 0.85, c % 2 === 0 ? 0.25 : 0.15, 0.85);
        rect(x, y, w, h);
      } else {
        noFill();
        rect(x, y, w, h);
      }
    }
  }
}
""".strip(),
        "sketch_3d": """
param("count", 9, 3, 16);

function draw(t) {
  const s = 0.45;
  for (let x = -1; x <= 1; x++) {
    for (let y = -1; y <= 1; y++) {
      for (let z = -1; z <= 1; z++) {
        const off = sin(t * 1.5 + x + y + z);
        push();
        translate(x * s * 1.6, y * s * 1.6 + off * 0.2, z * s * 1.6);
        fill((x + 2) / 4, 0.2, (z + 2) / 4);
        box(s * 0.85);
        pop();
      }
    }
  }
}
""".strip(),
        "field_pixel": """g = floor(uv * 8.0); on = step(0.5, fract(sin(dot(g, vec2(12.9898, 78.233)) + floor(t * 2.0)) * 43758.5453)); col = mix(vec3(0.95), vec3(0.05, 0.15, 0.9), on);"""
    },

    "technical_blueprint": {
        "family": "graphic_print",
        "name": "Speculative Technical & Biological Blueprint",
        "description": "Scientific drafting arcs, patent dimension callouts, coordinate tags, and translucent false-color specimens.",
        "tags": ["#blueprint", "#technicaldrawing", "#diagram", "#speculative", "#anatomy"],
        "palette": ["#002b49", "#00d4ff", "#ffffff"],
        "synth": "String",
        "note_source": "Random Note Generator",
        "sketch_2d": """
param("rad", 220, 80, 400);
param("rot", 0.2, 0.05, 1);

function draw(t) {
  background(0.03, 0.08, 0.16); // Drafting Blue
  translate(width / 2, height / 2);
  noFill();
  stroke(0.0, 0.8, 1.0, 0.85); // Blueprint Cyan
  strokeWeight(1.4);
  
  // Radial compass circles and degree ticks
  circle(0, 0, rad * 2);
  circle(0, 0, rad * 1.3);
  
  rotate(t * rot);
  for (let i = 0; i < 24; i++) {
    const a = (i * TAU) / 24;
    line(cos(a) * rad, sin(a) * rad, cos(a) * (rad + 12), sin(a) * (rad + 12));
  }
  
  // Crosshairs & trajectory tangents
  strokeWeight(1.0);
  line(-rad * 1.4, 0, rad * 1.4, 0);
  line(0, -rad * 1.4, 0, rad * 1.4);
}
""".strip(),
        "sketch_3d": """
param("segments", 24, 8, 48);

function draw(t) {
  for (let i = 0; i < segments; i++) {
    const a = (i * TAU) / segments;
    push();
    rotateY(a + t * 0.2);
    translate(1.2, 0, 0);
    fill(0.0, 0.8, 1.0);
    box(0.04, 0.8, 0.04);
    pop();
  }
}
""".strip(),
        "field_pixel": """p = (uv - 0.5) * aspect; r = length(p); grid = step(0.96, max(fract(uv.x * 20.0), fract(uv.y * 20.0))); col = vec3(0.02, 0.09, 0.22) + vec3(0.0, 0.7, 1.0) * grid + vec3(0.0, 0.9, 0.8) * step(0.003, abs(r - 0.35));"""
    },

    # -------------------------------------------------------------------------
    # 3. HISTORICAL AVANT-GARDE & FINE ART
    # -------------------------------------------------------------------------
    "retro_surreal": {
        "family": "avant_garde",
        "name": "Retro-Surrealist Cutout Collage",
        "description": "Dadaist anatomical collage, clinical hospital halls, oversized eyeballs, miniature displacement, and matte paper grain.",
        "tags": ["#surrealism", "#dada", "#collage", "#vintage", "#assemblage", "#strange"],
        "palette": ["#4a7c59", "#d4a373", "#fefae0", "#2b2d42"],
        "synth": "Partials",
        "note_source": "Random Note Generator",
        "sketch_2d": """
param("eyeSize", 90, 30, 200);
param("orbit", 0.4, 0.05, 1);

function draw(t) {
  background(0.32, 0.42, 0.35); // Vintage Sage Room
  
  // Checkerboard linoleum floor
  const floorY = height * 0.65;
  noStroke();
  for (let x = 0; x < width; x += 40) {
    for (let y = floorY; y < height; y += 30) {
      const idx = floor(x / 40) + floor(y / 30);
      fill(idx % 2 === 0 ? 0.88 : 0.22, idx % 2 === 0 ? 0.86 : 0.45, idx % 2 === 0 ? 0.80 : 0.52);
      rect(x, y, 40, 30);
    }
  }
  
  // Floating surreal mechanical eye assembly
  translate(width / 2, height * 0.36 + sin(t * orbit) * 18);
  stroke(0.1);
  strokeWeight(4);
  fill(0.96, 0.95, 0.9);
  circle(0, 0, eyeSize * 2);
  
  // Iris & pupil
  fill(0.45, 0.25, 0.15);
  circle(sin(t * 0.8) * 15, 0, eyeSize * 0.9);
  fill(0.05);
  circle(sin(t * 0.8) * 15, 0, eyeSize * 0.4);
}
""".strip(),
        "sketch_3d": """
param("radius", 1.2, 0.4, 2.5);

function draw(t) {
  // Classical column pedestal + floating eyeball solid
  push();
  translate(0, -0.6, 0);
  fill(0.85, 0.82, 0.78);
  box(0.5, 0.9, 0.5);
  pop();
  
  push();
  translate(0, 0.4 + 0.15 * sin(t * 1.5), 0);
  fill(0.95, 0.94, 0.9);
  sphere(0.35, 16);
  pop();
}
""".strip(),
        "field_pixel": """p = uv; col = mix(vec3(0.28, 0.42, 0.35), vec3(0.85, 0.75, 0.65), step(0.65, p.y)); noise = fract(sin(dot(uv, vec2(12.9898, 78.233)) * 43758.5453)); col += (noise - 0.5) * 0.08;"""
    },

    # -------------------------------------------------------------------------
    # 4. OPTICAL, KINETIC & SACRED GEOMETRY
    # -------------------------------------------------------------------------
    "op_art": {
        "family": "optical_kinetic",
        "name": "Kinetic Op-Art & Moiré Undulation",
        "description": "High-contrast retinal vibration, parallel line fields bulging around invisible poles, and moiré lattice interference.",
        "tags": ["#opart", "#moire", "#kineticart", "#bridgetriley", "#opticalillusion"],
        "palette": ["#000000", "#ffffff"],
        "synth": "FM",
        "note_source": "Arpeggiator",
        "sketch_2d": """
param("lines", 60, 20, 140);
param("freq", 4.0, 1.0, 10.0);
param("amp", 45, 10, 120);

function draw(t) {
  background(0.96);
  stroke(0.04);
  strokeWeight(2.4);
  noFill();
  
  for (let i = 0; i < lines; i++) {
    const y = (i / lines) * height;
    beginShape();
    for (let x = 0; x <= width; x += 12) {
      const d = dist(x, y, width / 2, height / 2);
      const bulge = exp(-d * 0.007) * sin(d * 0.05 - t * 3.0) * amp;
      vertex(x, y + bulge);
    }
    endShape();
  }
}
""".strip(),
        "sketch_3d": """
param("slats", 32, 10, 60);

function draw(t) {
  for (let i = 0; i < slats; i++) {
    const u = i / slats;
    push();
    rotateY(u * PI + t * 0.4);
    translate(0, (u - 0.5) * 2.2, 0);
    fill(i % 2 === 0 ? 0.05 : 0.95);
    box(1.8, 0.035, 0.25);
    pop();
  }
}
""".strip(),
        "field_pixel": """p = (uv - 0.5) * aspect; r = length(p); a = atan2(p.y, p.x); wave = sin(p.x * 40.0 + sin(p.y * 15.0 + t * 2.0) * 8.0); on = step(0.0, wave); col = vec3(on);"""
    },

    # -------------------------------------------------------------------------
    # 5. ANALOG MEDIA, FILM & PSYCHEDELIA
    # -------------------------------------------------------------------------
    "analog_crt": {
        "family": "analog_psychedelia",
        "name": "Analog CRT & VHS Tracking Glitch",
        "description": "Phosphor RGB shadow-mask triads, cathode curvature, tape flutter, composite chroma bleed, and interlaced scanlines.",
        "tags": ["#crt", "#vhs", "#analog", "#retro", "#scanlines", "#synthwave"],
        "palette": ["#00ff66", "#050805", "#ff0055"],
        "synth": "Analog",
        "note_source": "Random Note Generator",
        "sketch_2d": """
param("scanlines", 80, 20, 200);

function draw(t) {
  background(0.02, 0.03, 0.02);
  
  // Phosphor vector green sweep
  stroke(0.1, 0.95, 0.4, 0.8);
  strokeWeight(2.0);
  noFill();
  
  translate(width / 2, height / 2);
  for (let i = 0; i < 4; i++) {
    rotate(t * 0.5 + i * 0.4);
    const r = min(width, height) * (0.2 + i * 0.08);
    ellipse(0, 0, r * 1.5, r * 0.7);
  }
}
""".strip(),
        "sketch_3d": """
param("count", 12, 4, 30);

function draw(t) {
  for (let i = 0; i < count; i++) {
    push();
    rotateY(t * 0.6 + i);
    rotateX(t * 0.3);
    fill(0.1, 0.9, 0.3);
    box(0.7);
    pop();
  }
}
""".strip(),
        "field_pixel": """p = uv; scanline = sin(p.y * 400.0) * 0.25; beam = 0.5 + 0.5 * sin(p.x * 20.0 + t * 4.0); col = vec3(0.05, 0.95, 0.3) * (beam - scanline);"""
    },

    # -------------------------------------------------------------------------
    # 6. PINTEREST DISCOVERY & CURATED SUB-MOVEMENTS
    # -------------------------------------------------------------------------
    "thundr_brutalist": {
        "family": "graphic_print",
        "name": "Thundr Postmodern Brutalism & Techwear",
        "description": "Utilitarian barcodes, receipt monospace typography, hazard diagonal stripes, and distressed vector textures.",
        "tags": ["#thundrco", "#brutalism", "#streetwear", "#techwear", "#receiptart", "#typography"],
        "palette": ["#000000", "#ffffff", "#ffcc00", "#ff0033"],
        "synth": "FM",
        "note_source": "Arpeggiator",
        "sketch_2d": """
param("bars", 32, 10, 64);

function draw(t) {
  background(0.95, 0.94, 0.90);
  
  // Barcode block
  stroke(0.05);
  noFill();
  const startX = width * 0.12, y = height * 0.3, h = 90;
  for (let i = 0; i < bars; i++) {
    const w = ((sin(i * 12.3) * 0.5 + 0.5) > 0.4 ? 4 : 1.5);
    strokeWeight(w);
    const x = startX + i * 14;
    line(x, y, x, y + h);
  }
  
  // Hazard stripe footer
  strokeWeight(2);
  for (let i = 0; i < 20; i++) {
    const hx = i * 45;
    fill(i % 2 === 0 ? 0.05 : 0.95);
    rect(hx, height * 0.85, 30, 24);
  }
}
""".strip(),
        "sketch_3d": """
param("cubes", 16, 4, 32);

function draw(t) {
  for (let i = 0; i < cubes; i++) {
    push();
    translate(sin(i + t) * 1.5, (i / cubes - 0.5) * 2.5, cos(i * 2 + t) * 0.8);
    fill(i % 2 === 0 ? 0.05 : 0.92);
    box(0.25, 0.1, 0.6);
    pop();
  }
}
""".strip(),
        "field_pixel": """p = uv; bar = step(0.5, fract(p.x * 30.0 + sin(floor(p.y * 10.0) * 4.0))); col = mix(vec3(0.05), vec3(0.95, 0.85, 0.1), bar);"""
    },

    "chavetta_montage": {
        "family": "avant_garde",
        "name": "Alejandro Chavetta Mixed-Media Montage",
        "description": "Tactile torn paper ephemera, classical sculptures, botanical overlays, and digital watercolor brush strokes.",
        "tags": ["#chavetta", "#mixedmedia", "#analogdigital", "#collage", "#ephemera", "#tactile"],
        "palette": ["#2b2b2b", "#d8c4b6", "#a34839", "#4f5d75"],
        "synth": "Analog",
        "note_source": "Random Note Generator",
        "sketch_2d": """
param("slices", 8, 3, 16);

function draw(t) {
  background(0.85, 0.80, 0.73); // Aged Kraft Paper
  
  // Torn paper horizontal color blocks
  noStroke();
  for (let i = 0; i < slices; i++) {
    const y = (i / slices) * height;
    const h = (height / slices) * 0.85;
    fill(i % 2 === 0 ? 0.64 : 0.18, i % 3 === 0 ? 0.28 : 0.22, 0.25, 0.75);
    rect(width * 0.1 + sin(t + i) * 20, y, width * 0.8, h);
  }
  
  // Classical circular medallion mask
  translate(width / 2, height / 2);
  stroke(0.15);
  strokeWeight(3.5);
  noFill();
  circle(0, 0, min(width, height) * 0.5);
}
""".strip(),
        "sketch_3d": """
param("layers", 10, 3, 20);

function draw(t) {
  for (let i = 0; i < layers; i++) {
    push();
    translate(0, (i / layers - 0.5) * 2.0, sin(t + i * 0.4) * 0.3);
    fill(0.7 - i * 0.04, 0.65 - i * 0.03, 0.58);
    box(1.6 - i * 0.1, 0.12, 0.8);
    pop();
  }
}
""".strip(),
        "field_pixel": """p = uv; paper = fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453); col = vec3(0.85, 0.8, 0.72) + (paper - 0.5) * 0.12;"""
    },

    "lilliputian_surreal": {
        "family": "avant_garde",
        "name": "Lilliputian Scale-Displacement Surrealism",
        "description": "Miniature figures traversing titanic everyday and anatomical objects inside vast uncanny domestic landscapes.",
        "tags": ["#miniature", "#lilliputian", "#surrealism", "#scottmoore", "#scale", "#uncanny"],
        "palette": ["#1a2a3a", "#e8d8c8", "#b84a39"],
        "synth": "Partials",
        "note_source": "Random Note Generator",
        "sketch_2d": """
param("tinyCount", 12, 4, 30);

function draw(t) {
  background(0.12, 0.15, 0.22); // Deep Shadow Interior
  
  // Gigantic focal sphere (giant eyeball / relic)
  translate(width / 2, height * 0.45);
  noStroke();
  fill(0.92, 0.88, 0.82);
  circle(0, 0, min(width, height) * 0.65);
  
  // Miniature human figures traversing below
  fill(0.85, 0.25, 0.15);
  for (let i = 0; i < tinyCount; i++) {
    const x = ((i / tinyCount + t * 0.03) % 1) * width - width / 2;
    const y = height * 0.38 + sin(i * 3.1) * 15;
    rect(x, y, 4, 12); // tiny figure body
    circle(x + 2, y - 4, 5); // tiny head
  }
}
""".strip(),
        "sketch_3d": """
param("count", 14, 4, 30);

function draw(t) {
  // Giant central monolith
  push();
  fill(0.9, 0.86, 0.8);
  sphere(0.9, 16);
  pop();
  
  // Tiny orbiting traveler figurines
  for (let i = 0; i < count; i++) {
    const a = (i * TAU) / count + t * 0.2;
    push();
    translate(cos(a) * 1.4, -0.6, sin(a) * 1.4);
    fill(0.85, 0.2, 0.1);
    box(0.04, 0.15, 0.04);
    pop();
  }
}
""".strip(),
        "field_pixel": """p = (uv - 0.5) * aspect; r = length(p); titan = step(r, 0.35); col = mix(vec3(0.08, 0.11, 0.18), vec3(0.92, 0.88, 0.8), titan);"""
    },

    "ukiyoe_cartography": {
        "family": "graphic_print",
        "name": "Ukiyo-e Woodblock Cartography",
        "description": "Edo-period antique mapmaking, topographical contour ripples, compass roses, red seal stamps, and indigo rice paper.",
        "tags": ["#ukiyoe", "#woodblock", "#cartography", "#hokusai", "#japanart", "#vintage"],
        "palette": ["#1d3557", "#e63946", "#f1faee", "#a8dadc"],
        "synth": "String",
        "note_source": "Random Note Generator",
        "sketch_2d": """
param("ripples", 24, 8, 48);

function draw(t) {
  background(0.94, 0.92, 0.85); // Rice Paper Tan
  translate(width / 2, height / 2);
  noFill();
  stroke(0.12, 0.24, 0.42); // Indigo Ink
  strokeWeight(1.8);
  
  // Topographical elevation contours
  for (let i = 0; i < ripples; i++) {
    const r = (i + 1) * 14;
    beginShape();
    for (let a = 0; a <= TAU; a += 0.2) {
      const warp = sin(a * 4.0 + t * 0.8 + i * 0.4) * 8.0;
      vertex(cos(a) * (r + warp), sin(a) * (r + warp));
    }
    endShape();
  }
  
  // Red seal hanko stamp
  noStroke();
  fill(0.85, 0.15, 0.18);
  rect(width * 0.25, height * 0.25, 36, 36);
}
""".strip(),
        "sketch_3d": """
param("peaks", 18, 6, 36);

function draw(t) {
  for (let i = 0; i < peaks; i++) {
    const a = (i * TAU) / peaks;
    push();
    rotateY(a + t * 0.15);
    translate(1.1, sin(i * 2.0 + t) * 0.3, 0);
    fill(0.12, 0.25, 0.45);
    box(0.15, 0.8, 0.05);
    pop();
  }
}
""".strip(),
        "field_pixel": """p = (uv - 0.5) * aspect; r = length(p); topo = sin(r * 35.0 + sin(atan2(p.y, p.x) * 4.0) * 3.0); col = mix(vec3(0.92, 0.9, 0.82), vec3(0.1, 0.2, 0.4), step(0.7, topo));"""
    },

    "indie_psychedelic": {
        "family": "analog_psychedelia",
        "name": "Dreamy Indie Psychedelia & Introspective Melt",
        "description": "Melting interior perspectives, organic vines sprouting into fractals, fluid domain warp, and vibrating neon palettes.",
        "tags": ["#indieart", "#psychedelic", "#melting", "#dreamcore", "#lofi", "#trippy"],
        "palette": ["#7209b7", "#4cc9f0", "#f72585", "#3a0ca3"],
        "synth": "Wavetable",
        "note_source": "Arpeggiator",
        "sketch_2d": """
param("branches", 12, 4, 24);

function draw(t) {
  background(0.08, 0.02, 0.14); // Deep Violet
  translate(width / 2, height * 0.7);
  noFill();
  strokeWeight(2.6);
  
  // Liquid melting vines reaching upward
  for (let b = 0; b < branches; b++) {
    stroke(hsl(0.75 + (b / branches) * 0.35, 0.9, 0.65), 0.85);
    beginShape();
    let x = (b - branches / 2) * 22;
    let y = 0;
    for (let s = 0; s < 28; s++) {
      vertex(x, y);
      x += sin(t * 1.5 + s * 0.4 + b) * 8.0;
      y -= 14.0;
    }
    endShape();
  }
}
""".strip(),
        "sketch_3d": """
param("blobs", 16, 4, 30);

function draw(t) {
  for (let i = 0; i < blobs; i++) {
    const u = i / blobs;
    push();
    rotateY(u * TAU + t * 0.4);
    translate(0.8 + 0.3 * sin(t * 2 + i), (u - 0.5) * 2.0, 0);
    fill(hsl(u + t * 0.1, 0.85, 0.6));
    sphere(0.18 + 0.08 * sin(t * 3 + i), 12);
    pop();
  }
}
""".strip(),
        "field_pixel": """p = (uv - 0.5) * aspect; melt = sin(p.y * 12.0 + t * 2.0) * 0.15; col = vec3(0.5 + 0.5 * sin(p.x * 10.0 + melt * 6.0), 0.2, 0.8);"""
    },

    "riso_manga_zine": {
        "family": "graphic_print",
        "name": "Risograph Manga Pop & Deconstructed Zine",
        "description": "Halftone Ben-Day dots, CMYK screen misregistration, vintage manga panel frames, and fluorescent spot inks.",
        "tags": ["#risograph", "#manga", "#zine", "#halftone", "#vintagecomic", "#popart"],
        "palette": ["#ff0077", "#00f0ff", "#ffee00", "#111111"],
        "synth": "FM",
        "note_source": "Random Note Generator",
        "sketch_2d": """
param("panels", 3, 2, 5);

function draw(t) {
  background(0.96, 0.94, 0.91);
  stroke(0.08);
  strokeWeight(3.5);
  noFill();
  
  // Deconstructed comic zine panels with plate misregistration
  const pw = width * 0.75, ph = height / panels - 24;
  for (let p = 0; p < panels; p++) {
    const y = 30 + p * (ph + 18);
    // Fluoro pink shadow misregister
    stroke(0.95, 0.1, 0.5, 0.6);
    rect(width * 0.12 - 4, y - 3, pw, ph);
    // Cyan shadow misregister
    stroke(0.1, 0.8, 0.9, 0.6);
    rect(width * 0.12 + 4, y + 3, pw, ph);
    // Black key plate
    stroke(0.08);
    rect(width * 0.12, y, pw, ph);
  }
}
""".strip(),
        "sketch_3d": """
param("frames", 8, 3, 16);

function draw(t) {
  for (let i = 0; i < frames; i++) {
    push();
    rotateY(t * 0.3 + i * 0.4);
    translate(0, (i / frames - 0.5) * 2.2, 0.6);
    fill(i % 2 === 0 ? 0.95 : 0.05, 0.1, 0.5);
    box(1.2, 0.04, 0.8);
    pop();
  }
}
""".strip(),
        "field_pixel": """p = uv * res; dots = step(0.3, fract(p.x / 8.0) * fract(p.y / 8.0)); col = mix(vec3(0.95, 0.1, 0.5), vec3(0.95, 0.92, 0.88), dots);"""
    },

    "liminal_void": {
        "family": "avant_garde",
        "name": "Liminal Anime Concept Art & Uncanny Voids",
        "description": "Desolate concrete architecture, infinite reflective water plane, solitary silhouettes, and dusk sunset skies.",
        "tags": ["#liminal", "#animeart", "#conceptart", "#dreamlike", "#melancholy", "#aesthetic"],
        "palette": ["#f77f00", "#d62828", "#003049", "#eae2b7"],
        "synth": "Analog",
        "note_source": "Random Note Generator",
        "sketch_2d": """
param("horizon", 0.62, 0.4, 0.8);

function draw(t) {
  // Sunset sky gradient
  const horizY = height * horizon;
  for (let y = 0; y < horizY; y += 4) {
    const f = y / horizY;
    stroke(hsl(0.05 + f * 0.1, 0.85, 0.3 + f * 0.4));
    strokeWeight(4);
    line(0, y, width, y);
  }
  
  // Infinite reflective water floor
  for (let y = horizY; y < height; y += 4) {
    const f = (y - horizY) / (height - horizY);
    stroke(hsl(0.6 - f * 0.1, 0.6, 0.15 + f * 0.2));
    strokeWeight(4);
    line(0, y, width, y);
  }
  
  // Solitary concrete pillars cutting through horizon
  noStroke();
  fill(0.06, 0.06, 0.1);
  rect(width * 0.35, horizY - 140, 32, 220);
  rect(width * 0.55, horizY - 90, 24, 160);
}
""".strip(),
        "sketch_3d": """
param("pillars", 12, 4, 24);

function draw(t) {
  for (let i = 0; i < pillars; i++) {
    const z = (i / pillars) * 4.0 - 2.0;
    push();
    translate(sin(i * 1.5) * 1.2, 0, z);
    fill(0.1, 0.1, 0.15);
    box(0.18, 1.8, 0.18);
    pop();
  }
}
""".strip(),
        "field_pixel": """p = uv; col = mix(vec3(0.9, 0.4, 0.1), vec3(0.05, 0.1, 0.25), step(0.6, p.y));"""
    }
}


def get_styles_for_family(family):
    """Returns all styles belonging to a specific family."""
    return [sid for sid, d in TAXONOMY.items() if d["family"] == family]


def build_patch_for_style(style_id, visual_type="pixel", with_audio=False):
    """Builds a complete, validated Infinite patch (.inf) for any given style.
    Supports authentic visual mediums: 'pixel' (Field Pixel), 'sketch_2d' (2D vector), 'sketch_3d'.
    Audio reactivity is completely optional - visuals stand strongly on their own art style.
    """
    style = TAXONOMY.get(style_id, TAXONOMY["y2k_acid"])
    synth = style["synth"]
    note_source = style["note_source"]

    lines = ["infinite-patch 1"]
    nid = 1

    # Optional audio track
    if with_audio:
        if note_source == "Arpeggiator":
            lines.append(f"node {nid} Notes Random Note Generator\n  id notes\nend")
            nid += 1
            lines.append(f"node {nid} Notes Arpeggiator\n  id arp\nend")
            nid += 1
            lines.append(f"node {nid} Synths {synth}\n  id synth\nend")
            nid += 1
            lines.append(f"node {nid} Utility Audio Out\n  id speaker\nend")
            nid += 1
        else:
            lines.append(f"node {nid} Notes {note_source}\n  id notes\nend")
            nid += 1
            lines.append(f"node {nid} Synths {synth}\n  id synth\nend")
            nid += 1
            lines.append(f"node {nid} Utility Audio Out\n  id speaker\nend")
            nid += 1

    # Visual node based on chosen medium
    if visual_type == "pixel" and "field_pixel" in style:
        code = style["field_pixel"].replace('\\', '\\\\').replace('\n', ' ')
        lines.append(f"node {nid} Source FieldPixel\n  id visuals\n  f width 720\n  f height 720\n  s code {code}\nend")
    elif visual_type == "sketch_3d" and "sketch_3d" in style:
        js_code = style["sketch_3d"].replace('\\', '\\\\').replace('\n', '\\n')
        lines.append(f"node {nid} Source Sketch 3D\n  id visuals\n  f width 720\n  f height 720\n  s code {js_code}\nend")
    else:
        js_code = style["sketch_2d"].replace('\\', '\\\\').replace('\n', '\\n')
        lines.append(f"node {nid} Source Sketch\n  id visuals\n  f width 720\n  f height 720\n  s code {js_code}\nend")

    vid = nid
    nid += 1
    lines.append(f"node {nid} Utility Output\n  id out\nend")

    # Wires
    if with_audio:
        if note_source == "Arpeggiator":
            lines.append("note arp 0 notes")
            lines.append("note synth 0 arp")
        else:
            lines.append("note synth 0 notes")
        lines.append("aud speaker 0 synth")

    lines.append("cable out 0 visuals")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    print(f"Loaded {len(TAXONOMY)} styles across {len(set(d['family'] for d in TAXONOMY.values()))} art families.")
    for sid, info in TAXONOMY.items():
        print(f"  [{info['family']}] {sid}: {info['name']}")
