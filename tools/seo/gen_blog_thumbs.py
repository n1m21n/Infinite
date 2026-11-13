#!/usr/bin/env python3
"""Generate brand-compliant, topic-relevant 1200x675 thumbnails for all website blog posts.

Follows docs/brand/BRAND.md v2.1:
- Midnight mode ground (#151930) & deep (#0D1020), Surface-1 (#20263F), Moon (#EEF1FA), Signal (#A7B1FF), Ember (#FF6B35)
- Zero outlines on cards/containers (elevation via surface step + soft shadow)
- One Ember focal element per frame (<10% area)
- Programmed field dot texture (<=5% opacity)
- Geist 800 headline (2-4 words) + Geist Mono kicker & spec + Infinite lemniscate mark
- Bespoke visual stage per post (3D brand cutouts + mathematical SVG/Canvas2D diagrams)
- Safe margins keeping top-left/top-right chips and bottom bento title pill clear

Usage:
    python3 tools/seo/gen_blog_thumbs.py [--only slug1,slug2] [--update-posts]
"""
from __future__ import annotations
import argparse
import glob
import html
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BRAND = json.load(open(os.path.join(ROOT, "docs", "brand", "brand.json")))
M = BRAND["colour"]["modes"]["midnight"]
FONTS = os.path.join(ROOT, "art", "brand", "fonts")
LOGO_SVG = open(os.path.join(ROOT, "art", "brand", "logo", "svg", "mark.svg")).read()
RENDERS = os.path.join(ROOT, "art", "brand", "3d", "renders")
POSTS_DIR = os.path.join(ROOT, "tools", "seo", "posts")
ASSETS_DIR = os.path.join(ROOT, "website", "assets")

CHROME = next(
    (
        p
        for p in (
            "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
            shutil.which("google-chrome") or "",
            shutil.which("chromium") or "",
        )
        if p and os.path.exists(p)
    ),
    None,
)

W, H = 1200, 675
E = html.escape


def font_faces() -> str:
    faces = [
        ("Geist", 400, "Geist-Regular"),
        ("Geist", 500, "Geist-Medium"),
        ("Geist", 600, "Geist-SemiBold"),
        ("Geist", 800, "Geist-ExtraBold"),
        ("Geist Mono", 400, "GeistMono-Regular"),
        ("Geist Mono", 500, "GeistMono-Medium"),
        ("Caveat", 700, "Caveat-Bold"),
    ]
    out = []
    for fam, wt, fname in faces:
        p = os.path.join(FONTS, f"{fname}.ttf")
        if os.path.exists(p):
            out.append(f'@font-face{{font-family:"{fam}";font-weight:{wt};src:url("file://{p}")}}')
    return "\n".join(out)


def headline_html(headline: str, hand: str | None) -> str:
    if hand and hand in headline:
        a, b = headline.split(hand, 1)
        return (
            f'{E(a)}<span class="hand">{E(hand)}'
            f'<svg class="ul" viewBox="0 0 100 12" preserveAspectRatio="none">'
            f'<path d="M2 8 Q 30 2 55 6 T 98 5"/></svg></span>{E(b)}'
        )
    return E(headline)


# Post specifications: slug -> {kicker, headline, hand (optional), spec, visual_type, params}
# Rule: if `hand` is set, the hand word is the ONE Ember element; visual uses Signal + Moon.
#       if `hand` is None, the visual stage has the ONE Ember focal element.
POST_SPECS: dict[str, dict] = {
    "ai-agent-mcp-audio-visual-engine-guide": {
        "kicker": "PRODUCT · MCP",
        "headline": "Agents patch live",
        "hand": None,
        "spec": "MCP tools · Python SDK · headless",
        "visual": "mcp_pipeline",
    },
    "ai-agents-make-music-art-3d-headless": {
        "kicker": "PRODUCT · HEADLESS",
        "headline": "Headless creation",
        "hand": "creation",
        "spec": ".infinite → audio + 3D + video",
        "visual": "cutout",
        "cutout": "brand_ball_cutout.png",
        "badge": "CLI · 0 WINDOWS",
    },
    "audio-reactive-glsl-shaders": {
        "kicker": "CREATIVE · GLSL",
        "headline": "Audio-reactive shaders",
        "hand": None,
        "spec": "FFT bands → GPU uniforms · 60 FPS",
        "visual": "glsl_fft",
    },
    "audio-reactive-particle-systems-in-real-time": {
        "kicker": "CREATIVE · PARTICLES",
        "headline": "Particle physics",
        "hand": None,
        "spec": "Euler · curl noise · GPU points",
        "visual": "particles",
    },
    "automatic-visuals-for-dj-sets": {
        "kicker": "CREATIVE · LIVE DJ",
        "headline": "Hands-free DJ visuals",
        "hand": None,
        "spec": "Sub / Mid / High split · live sync",
        "visual": "dj_bands",
    },
    "delaunay-triangulation-and-voronoi-geometry-in-real-time": {
        "kicker": "CREATIVE · GEOMETRY",
        "headline": "Delaunay & Voronoi",
        "hand": None,
        "spec": "Bowyer-Watson · dual cells · 3D",
        "visual": "delaunay",
    },
    "drum-and-bass-reese-bass-sound-design": {
        "kicker": "CREATIVE · SOUND DESIGN",
        "headline": "Reese bass design",
        "hand": None,
        "spec": "Detuned saws · 3-band split · drive",
        "visual": "reese_bass",
    },
    "dsp-without-juce": {
        "kicker": "PRODUCT · C++20",
        "headline": "DSP without JUCE",
        "hand": None,
        "spec": "Lock-free audio · zero heap alloc",
        "visual": "dsp_ring",
    },
    "electronic-music-sound-design-sub-bass-pluck-pad": {
        "kicker": "CREATIVE · SYNTHESIS",
        "headline": "Sub, pluck & pad",
        "hand": None,
        "spec": "Sine sub · Karplus pluck · Field pad",
        "visual": "three_voices",
    },
    "euclidean-rhythm-sequencer-algorithm": {
        "kicker": "CREATIVE · SEQUENCING",
        "headline": "Euclidean rhythms",
        "hand": None,
        "spec": "E(5, 16) · Bjorklund distribution",
        "visual": "euclidean",
    },
    "every-note-node-in-infinite-generative-sequencing-guide": {
        "kicker": "CREATIVE · NOTES",
        "headline": "24 note nodes",
        "hand": None,
        "spec": "Arp · Markov · scale quantizer",
        "visual": "note_lattice",
    },
    "every-synthesizer-node-in-infinite-sound-design-guide": {
        "kicker": "CREATIVE · SYNTHS",
        "headline": "20 synth nodes",
        "hand": "synth",
        "spec": "Wavetable · FM · Modal · Granular",
        "visual": "cutout",
        "cutout": "node_hero_cutout.png",
        "badge": "20 ENGINES · 48 KHZ",
    },
    "field-language-write-what-a-node-does": {
        "kicker": "PRODUCT · FIELD",
        "headline": "One Field kernel",
        "hand": "Field",
        "spec": "Shader · Deformer · Audio DSP",
        "visual": "cutout",
        "cutout": "node_field_pixel_cutout.png",
        "badge": "FIELD LANGUAGE",
    },
    "free-touchdesigner-alternative-for-audio-reactive-visuals": {
        "kicker": "CREATIVE · WORKFLOW",
        "headline": "Open visual patching",
        "hand": "Open",
        "spec": "Audio + 3D + shaders · free",
        "visual": "cutout",
        "cutout": "patch_cutout.png",
        "badge": "MAC · WIN · LINUX",
    },
    "generative-visuals-live-performance": {
        "kicker": "CREATIVE · STAGE",
        "headline": "Live stage visuals",
        "hand": None,
        "spec": "FFT → feedback loop · 60 FPS",
        "visual": "feedback_loop",
    },
    "granular-synthesis-modular-audio-sound-design": {
        "kicker": "CREATIVE · GRANULAR",
        "headline": "Grain clouds",
        "hand": None,
        "spec": "Hann windows · stochastic spray",
        "visual": "granular",
    },
    "hardware-midi-and-controller-mapping-in-infinite": {
        "kicker": "PRODUCT · HARDWARE",
        "headline": "Hardware MIDI & DMX",
        "hand": None,
        "spec": "MIDI learn · CC curves · low latency",
        "visual": "midi_encoders",
    },
    "infinite-v0-6-benchmarks": {
        "kicker": "PRODUCT · BENCHMARKS",
        "headline": "v0.6 benchmarks",
        "hand": None,
        "spec": "MacBook Air M2 · audio + GPU",
        "visual": "benchmarks",
    },
    "interactive-audio-reactive-webgl-embeds-for-websites": {
        "kicker": "CREATIVE · WEBGL",
        "headline": "WebGL embeds",
        "hand": None,
        "spec": "Self-contained <iframe> · mic live",
        "visual": "webgl_embed",
    },
    "javascript-sketches": {
        "kicker": "CREATIVE · CODE",
        "headline": "JS sketch nodes",
        "hand": None,
        "spec": "Draw with code · expose CV pins",
        "visual": "js_sketch",
    },
    "l-system-procedural-3d-branching-fractals": {
        "kicker": "CREATIVE · FRACTALS",
        "headline": "L-system branching",
        "hand": None,
        "spec": "3D turtle · recursive extrusion",
        "visual": "lsystem",
    },
    "linux-daw-modular-audio-and-generative-visuals": {
        "kicker": "CREATIVE · LINUX",
        "headline": "Linux modular DAW",
        "hand": None,
        "spec": "PipeWire / JACK · VST3 · Vulkan",
        "visual": "linux_stack",
    },
    "lorenz-attractor-interactive-simulation": {
        "kicker": "CREATIVE · CHAOS",
        "headline": "Lorenz attractor",
        "hand": None,
        "spec": "σ = 10 · ρ = 28 · β = 8/3",
        "visual": "lorenz",
    },
    "make-your-first-audio-reactive-visual": {
        "kicker": "CREATIVE · TUTORIAL",
        "headline": "Your first patch",
        "hand": "first",
        "spec": "Audio Analyze → 1 cable → Shape",
        "visual": "first_patch",
    },
    "modal-synthesis-and-physical-modelling-of-metallic-percussion": {
        "kicker": "CREATIVE · ACOUSTICS",
        "headline": "Modal percussion",
        "hand": None,
        "spec": "12 resonators · Chladni plate",
        "visual": "modal_chladni",
    },
    "modular-vst3-host-audio-routing-and-modulation": {
        "kicker": "PRODUCT · VST3",
        "headline": "Modular VST3 host",
        "hand": None,
        "spec": "Sandboxed plugins · MIDI + CV",
        "visual": "vst3_host",
    },
    "modulate-vst3-plugins-with-visuals-and-lfos": {
        "kicker": "CREATIVE · MODULATION",
        "headline": "Modulate VST3 live",
        "hand": "live",
        "spec": "LFO · webcam · audio → plugin CV",
        "visual": "cutout",
        "cutout": "node_lfo_cutout.png",
        "badge": "LFO + VISUAL CV",
    },
    "motion-tracking-hand-face-pose-modulators": {
        "kicker": "CREATIVE · TRACKING",
        "headline": "Motion tracking",
        "hand": None,
        "spec": "Hand · Face · Pose → CV pins",
        "visual": "hand_track",
    },
    "node-based-daw-modular-audio-routing": {
        "kicker": "CREATIVE · ROUTING",
        "headline": "Node-based DAW",
        "hand": None,
        "spec": "Audio + MIDI + shaders on 1 canvas",
        "visual": "node_graph",
    },
    "one-graph-for-sound-and-pictures": {
        "kicker": "PRODUCT · ENGINE",
        "headline": "One shared graph",
        "hand": None,
        "spec": "120 BPM clock · 48 kHz + 60 FPS",
        "visual": "one_graph",
    },
    "open-source-vj-software": {
        "kicker": "CREATIVE · VJ",
        "headline": "Open source VJ",
        "hand": None,
        "spec": "Dual decks · FFT shaders · stage",
        "visual": "vj_decks",
    },
    "original-audiovisual-nodes-wave-terrain-shape-resonator-cymatics": {
        "kicker": "CREATIVE · ORIGINALS",
        "headline": "Original AV nodes",
        "hand": None,
        "spec": "Wave Terrain · Resonator · Cymatics",
        "visual": "triptych_av",
    },
    "paulstretch-extreme-spectral-time-stretching": {
        "kicker": "CREATIVE · SPECTRAL",
        "headline": "PaulStretch drones",
        "hand": None,
        "spec": "FFT phase smear · 100× stretch",
        "visual": "paulstretch",
    },
    "per-sample-dsp-audio-kernels-in-field-language": {
        "kicker": "PRODUCT · DSP",
        "headline": "48 kHz DSP kernels",
        "hand": None,
        "spec": "Per-sample register VM · Field",
        "visual": "dsp_register",
    },
    "physical-modelling-string-synthesis-in-real-time": {
        "kicker": "CREATIVE · WAVEGUIDE",
        "headline": "String synthesis",
        "hand": None,
        "spec": "Karplus-Strong · allpass dispersion",
        "visual": "string_waveguide",
    },
    "procedural-3d-geometry-nodes-and-audio-deformation": {
        "kicker": "CREATIVE · 3D MESH",
        "headline": "3D mesh deform",
        "hand": None,
        "spec": "Normal displacement · Field 3D",
        "visual": "mesh_deform",
    },
    "raymarching-signed-distance-fields-in-real-time-glsl": {
        "kicker": "CREATIVE · RAYMARCHING",
        "headline": "SDF raymarching",
        "hand": None,
        "spec": "Sphere tracing · smooth-min · GLSL",
        "visual": "raymarching",
    },
    "reaction-diffusion-simulation-in-real-time-glsl": {
        "kicker": "CREATIVE · SIMULATION",
        "headline": "Reaction-diffusion",
        "hand": None,
        "spec": "Gray-Scott PDE · Turing patterns",
        "visual": "reaction_diffusion",
    },
    "real-time-fluid-simulation-in-glsl": {
        "kicker": "CREATIVE · FLUIDS",
        "headline": "Navier-Stokes fluid",
        "hand": None,
        "spec": "Vorticity · pressure solve · GLSL",
        "visual": "fluid_sim",
    },
    "render-video-and-audio-from-a-text-file": {
        "kicker": "PRODUCT · CLI",
        "headline": "Text to video",
        "hand": None,
        "spec": "Plain-text .inf → MP4 + LUFS",
        "visual": "text_to_video",
    },
    "shadertoy-to-infinite-audio-reactive-glsl": {
        "kicker": "CREATIVE · SHADERTOY",
        "headline": "Shadertoy to live",
        "hand": None,
        "spec": "iChannel audio → live uniforms",
        "visual": "shadertoy",
    },
    "wave-terrain-synthesis-in-real-time": {
        "kicker": "CREATIVE · SYNTHESIS",
        "headline": "Wave terrain",
        "hand": None,
        "spec": "3D surface · orbital trajectory",
        "visual": "wave_terrain",
    },
}


def build_stage_script(spec: dict) -> str:
    """Return JS code that draws the bespoke visual inside <canvas id='cv'> or <svg id='sv'>."""
    vis = spec["visual"]
    if vis == "cutout":
        return ""
    # Canvas2D deterministic generative renderers using exact brand tokens
    return f"renderVisual({json.dumps(vis)}, {json.dumps(spec)});"


JS_RENDERERS = r"""
const C = {
  ground: '#151930',
  deep: '#0D1020',
  s1: '#20263F',
  s2: '#2A3052',
  s3: '#343B5E',
  line: '#343B5E',
  moon: '#EEF1FA',
  ink2: '#A3AACB',
  ink3: '#8890B1',
  signal: '#A7B1FF',
  ember: '#FF6B35',
  mod: '#FFBE5A'
};

function mulberry32(a) {
  return function() {
    let t = a += 0x6D2B79F5;
    t = Math.imul(t ^ t >>> 15, t | 1);
    t ^= t + Math.imul(t ^ t >>> 7, t | 61);
    return ((t ^ t >>> 14) >>> 0) / 4294967296;
  }
}

function pill(ctx, x, y, w, h, r, fill) {
  ctx.beginPath();
  ctx.roundRect(x, y, w, h, r);
  ctx.fillStyle = fill;
  ctx.fill();
}

function nodeBox(ctx, x, y, w, h, title, cat, pinsIn=[], pinsOut=[], highlight=false) {
  ctx.save();
  ctx.shadowColor = 'rgba(5,7,18,0.5)';
  ctx.shadowBlur = 18;
  ctx.shadowOffsetY = 8;
  pill(ctx, x, y, w, h, 16, C.s1);
  ctx.restore();

  // Title strip (no outline)
  ctx.save();
  ctx.beginPath();
  ctx.roundRect(x, y, w, 30, [16, 16, 0, 0]);
  ctx.fillStyle = highlight ? 'rgba(255,107,53,0.18)' : 'rgba(167,177,255,0.13)';
  ctx.fill();
  ctx.restore();

  ctx.font = '600 13px Geist, sans-serif';
  ctx.fillStyle = C.moon;
  ctx.fillText(title, x + 14, y + 20);

  ctx.font = '500 10px "Geist Mono", monospace';
  ctx.fillStyle = highlight ? C.ember : C.signal;
  const cw = ctx.measureText(cat).width;
  ctx.fillText(cat, x + w - cw - 14, y + 19);

  // Pins
  pinsIn.forEach((py, i) => {
    ctx.beginPath();
    ctx.arc(x, y + 44 + i * 22, 4.5, 0, Math.PI * 2);
    ctx.fillStyle = C.signal;
    ctx.fill();
    ctx.font = '500 11px "Geist Mono", monospace';
    ctx.fillStyle = C.ink2;
    ctx.fillText(py, x + 12, y + 48 + i * 22);
  });
  pinsOut.forEach((py, i) => {
    ctx.beginPath();
    ctx.arc(x + w, y + 44 + i * 22, 4.5, 0, Math.PI * 2);
    ctx.fillStyle = py.ember ? C.ember : C.signal;
    ctx.fill();
    ctx.font = '500 11px "Geist Mono", monospace';
    ctx.fillStyle = C.ink2;
    const lw = ctx.measureText(py.label || py).width;
    ctx.fillText(py.label || py, x + w - lw - 12, y + 48 + i * 22);
  });
}

function cable(ctx, x1, y1, x2, y2, ember=false, width=3) {
  const dx = Math.max(40, Math.abs(x2 - x1) * 0.5);
  ctx.beginPath();
  ctx.moveTo(x1, y1);
  ctx.bezierCurveTo(x1 + dx, y1, x2 - dx, y2, x2, y2);
  ctx.strokeStyle = ember ? C.ember : C.signal;
  ctx.lineWidth = width;
  ctx.lineCap = 'round';
  ctx.stroke();
}

function graticule(ctx, W, H, step=40) {
  ctx.strokeStyle = 'rgba(52,59,94,0.45)';
  ctx.lineWidth = 1;
  for (let x = step; x < W; x += step) {
    ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, H); ctx.stroke();
  }
  for (let y = step; y < H; y += step) {
    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke();
  }
}

function renderVisual(vis, spec) {
  const cv = document.getElementById('cv');
  if (!cv) return;
  const dpr = 2;
  const W = cv.clientWidth, H = cv.clientHeight;
  cv.width = W * dpr; cv.height = H * dpr;
  const ctx = cv.getContext('2d');
  ctx.scale(dpr, dpr);
  const rand = mulberry32(vis.split('').reduce((a,c)=>a*31+c.charCodeAt(0), 7));

  if (vis === 'modal_chladni') {
    graticule(ctx, W, H, 48);
    // Left: Chladni nodal plate
    const cx = 148, cy = 195, R = 104;
    pill(ctx, 28, 36, 240, 318, 16, C.s1);
    ctx.font = '500 11px "Geist Mono", monospace';
    ctx.fillStyle = C.ink3;
    ctx.fillText('CHLADNI PLATE · m=3, n=2', 44, 62);
    for (let py = -R; py <= R; py += 3) {
      for (let px = -R; px <= R; px += 3) {
        if (px*px + py*py > R*R) continue;
        const nx = px / R, ny = py / R;
        const val = Math.cos(3 * Math.PI * nx * 0.65) * Math.cos(2 * Math.PI * ny * 0.65)
                  - Math.cos(2 * Math.PI * nx * 0.65) * Math.cos(3 * Math.PI * ny * 0.65);
        if (Math.abs(val) < 0.11) {
          ctx.fillStyle = Math.abs(val) < 0.04 ? C.moon : 'rgba(167,177,255,0.6)';
          ctx.fillRect(cx + px, cy + py, 2.2, 2.2);
        }
      }
    }
    ctx.beginPath(); ctx.arc(cx, cy, 8, 0, Math.PI*2); ctx.fillStyle = C.ember; ctx.fill();
    ctx.font = '500 11px "Geist Mono", monospace';
    ctx.fillStyle = C.ember; ctx.fillText('MALLET STRIKE', 44, 336);

    // Right: Inharmonic resonator peaks
    pill(ctx, 286, 36, 276, 318, 16, C.s1);
    ctx.fillStyle = C.ink3;
    ctx.fillText('INHARMONIC SPECTRUM', 304, 62);
    const ratios = [1.0, 1.48, 2.02, 2.76, 3.45, 4.18, 5.40, 6.2];
    const amps = [0.95, 0.55, 0.72, 0.80, 0.42, 0.35, 0.50, 0.25];
    const baseY = 308;
    ctx.beginPath();
    ctx.moveTo(304, baseY);
    for (let x = 0; x <= 240; x++) {
      const f = 0.6 + (x / 240) * 6.2;
      let yv = 0;
      ratios.forEach((r, i) => { yv += amps[i] * Math.exp(-Math.pow((f - r) * 6.5, 2)); });
      ctx.lineTo(304 + x, baseY - yv * 205);
    }
    ctx.lineTo(544, baseY);
    ctx.closePath();
    ctx.fillStyle = 'rgba(167,177,255,0.18)';
    ctx.fill();
    ctx.strokeStyle = C.signal; ctx.lineWidth = 2.5; ctx.stroke();
    // Highlight fundamental peak in Ember? Wait: mallet strike is already Ember, keep peaks Signal/Moon
    [0, 3, 6].forEach(idx => {
      const px = 304 + ((ratios[idx] - 0.6) / 6.2) * 240;
      const py = baseY - amps[idx] * 205;
      ctx.beginPath(); ctx.arc(px, py, 4, 0, Math.PI*2); ctx.fillStyle = C.moon; ctx.fill();
      ctx.font = '500 10px "Geist Mono", monospace';
      ctx.fillStyle = C.ink2;
      ctx.fillText(ratios[idx].toFixed(2) + 'x', px - 12, py - 10);
    });
  }

  else if (vis === 'delaunay') {
    graticule(ctx, W, H, 48);
    // Clean geometric Voronoi + Delaunay mesh with one Ember circumcircle & site
    const pts = [
      [110, 95], [240, 75], [390, 90], [495, 130],
      [85, 220], [205, 195], [335, 185], [460, 240],
      [135, 330], [275, 315], [415, 345], [515, 330]
    ];
    const tris = [
      [0,1,5],[0,5,4],[1,2,6],[1,6,5],[2,3,7],[2,7,6],
      [4,5,8],[5,9,8],[5,6,9],[6,10,9],[6,7,10],[7,11,10]
    ];
    // Highlighted cavity triangles
    [3, 4, 8, 9].forEach(ti => {
      const [a,b,c] = tris[ti];
      ctx.beginPath();
      ctx.moveTo(pts[a][0], pts[a][1]);
      ctx.lineTo(pts[b][0], pts[b][1]);
      ctx.lineTo(pts[c][0], pts[c][1]);
      ctx.closePath();
      ctx.fillStyle = 'rgba(167,177,255,0.12)';
      ctx.fill();
    });
    // Edges
    tris.forEach(([a,b,c]) => {
      ctx.beginPath();
      ctx.moveTo(pts[a][0], pts[a][1]);
      ctx.lineTo(pts[b][0], pts[b][1]);
      ctx.lineTo(pts[c][0], pts[c][1]);
      ctx.closePath();
      ctx.strokeStyle = 'rgba(167,177,255,0.55)';
      ctx.lineWidth = 1.6;
      ctx.stroke();
    });
    // Empty circumcircle around central site
    ctx.beginPath();
    ctx.arc(275, 200, 96, 0, Math.PI * 2);
    ctx.strokeStyle = C.ember;
    ctx.lineWidth = 2;
    ctx.setLineDash([6, 5]);
    ctx.stroke();
    ctx.setLineDash([]);
    // Vertices
    pts.forEach(([x,y]) => {
      ctx.beginPath(); ctx.arc(x, y, 4.5, 0, Math.PI*2);
      ctx.fillStyle = C.moon; ctx.fill();
    });
    // Inserted site in Ember
    ctx.beginPath(); ctx.arc(275, 200, 7, 0, Math.PI*2);
    ctx.fillStyle = C.ember; ctx.fill();
    pill(ctx, 28, 326, 198, 30, 999, C.s1);
    ctx.font = '500 11px "Geist Mono", monospace';
    ctx.fillStyle = C.ember;
    ctx.fillText('SITE P(u, v) · R = 96 PX', 42, 345);
  }

  else if (vis === 'mcp_pipeline') {
    graticule(ctx, W, H, 48);
    nodeBox(ctx, 28, 56, 152, 112, 'AI Agent', 'MCP', [], [{label:'patch()'}, {label:'render()', ember:true}]);
    nodeBox(ctx, 28, 206, 152, 112, 'Audio Analyze', 'MOD', ['in'], [{label:'fft_low'}]);
    nodeBox(ctx, 218, 56, 166, 128, 'Infinite CLI', 'ENGINE', ['graph', 'params'], [{label:'audio'}, {label:'3d_mesh'}], true);
    nodeBox(ctx, 418, 126, 150, 126, 'Render 3D', 'OUT', ['mesh', 'mod'], [{label:'mp4'}]);

    cable(ctx, 180, 100, 218, 100, false);
    cable(ctx, 180, 122, 218, 122, true); // ONE Ember key cable
    cable(ctx, 384, 122, 418, 170, false);
    cable(ctx, 180, 250, 418, 192, false);
  }

  else if (vis === 'glsl_fft') {
    graticule(ctx, W, H, 48);
    // Concentric audio-reactive shader rings on right
    const cx = 370, cy = 185;
    for (let r = 135; r >= 25; r -= 22) {
      ctx.beginPath();
      for (let a = 0; a <= Math.PI * 2 + 0.05; a += 0.08) {
        const wob = Math.sin(a * 6 + r * 0.05) * (r * 0.07);
        const px = cx + Math.cos(a) * (r + wob);
        const py = cy + Math.sin(a) * (r + wob);
        a === 0 ? ctx.moveTo(px, py) : ctx.lineTo(px, py);
      }
      ctx.strokeStyle = r === 91 ? C.ember : 'rgba(167,177,255,0.65)';
      ctx.lineWidth = r === 91 ? 3 : 1.8;
      ctx.stroke();
    }
    // FFT bars on left
    pill(ctx, 28, 42, 160, 306, 16, C.s1);
    ctx.font = '500 11px "Geist Mono", monospace';
    ctx.fillStyle = C.ink3;
    ctx.fillText('FFT UNIFORMS', 44, 68);
    for (let i = 0; i < 12; i++) {
      const bw = 20 + Math.pow(Math.sin(i * 0.45 + 0.5), 2) * 105;
      pill(ctx, 44, 86 + i * 20, bw, 11, 5, i === 2 ? C.ember : C.signal);
    }
    cable(ctx, 188, 132, 255, 185, true, 2.5);
  }

  else if (vis === 'particles') {
    graticule(ctx, W, H, 48);
    const cx = W / 2, cy = H / 2;
    for (let i = 0; i < 240; i++) {
      const ang = rand() * Math.PI * 2;
      const dist = 20 + Math.pow(rand(), 0.7) * 185;
      const curl = Math.sin(ang * 3 + dist * 0.02) * 28;
      const x = cx + Math.cos(ang) * dist + curl;
      const y = cy + Math.sin(ang) * dist * 0.78;
      const isCore = dist < 52 && i % 5 === 0;
      const r = isCore ? 3.8 : 1.5 + rand() * 2.5;
      ctx.beginPath();
      ctx.arc(x, y, r, 0, Math.PI * 2);
      ctx.fillStyle = isCore ? C.ember : (rand() > 0.3 ? C.signal : C.moon);
      ctx.fill();
    }
    // Velocity vectors
    for (let i = 0; i < 18; i++) {
      const a = (i / 18) * Math.PI * 2;
      const r1 = 90, r2 = 128;
      ctx.beginPath();
      ctx.moveTo(cx + Math.cos(a) * r1, cy + Math.sin(a) * r1 * 0.78);
      ctx.lineTo(cx + Math.cos(a + 0.18) * r2, cy + Math.sin(a + 0.18) * r2 * 0.78);
      ctx.strokeStyle = 'rgba(167,177,255,0.45)';
      ctx.lineWidth = 1.5;
      ctx.stroke();
    }
  }

  else if (vis === 'dj_bands' || vis === 'vj_decks') {
    graticule(ctx, W, H, 48);
    const bands = [
      { label: 'SUB 20–120 HZ', val: 0.86, ember: true },
      { label: 'MID 120–2.4 KHZ', val: 0.62, ember: false },
      { label: 'HIGH 2.4–16 KHZ', val: 0.74, ember: false },
    ];
    bands.forEach((b, i) => {
      const y = 48 + i * 104;
      pill(ctx, 32, y, 528, 84, 16, C.s1);
      ctx.font = '500 12px "Geist Mono", monospace';
      ctx.fillStyle = b.ember ? C.ember : C.ink2;
      ctx.fillText(b.label, 52, y + 30);
      pill(ctx, 52, y + 46, 488, 16, 8, C.deep);
      pill(ctx, 52, y + 46, 488 * b.val, 16, 8, b.ember ? C.ember : C.signal);
    });
  }

  else if (vis === 'reese_bass') {
    graticule(ctx, W, H, 48);
    pill(ctx, 28, 40, 536, 310, 16, C.s1);
    ctx.font = '500 12px "Geist Mono", monospace';
    ctx.fillStyle = C.ink3;
    ctx.fillText('DETUNED SAW PHASE INTERFERENCE · ±18 CENTS', 48, 70);
    // Saw 1 + Saw 2 interference
    ctx.beginPath();
    for (let x = 0; x <= 496; x++) {
      const t = x / 496;
      const s1 = ((t * 7) % 1) * 2 - 1;
      const s2 = ((t * 7.28 + 0.15) % 1) * 2 - 1;
      const y = 195 + (s1 + s2) * 48;
      x === 0 ? ctx.moveTo(48 + x, y) : ctx.lineTo(48 + x, y);
    }
    ctx.strokeStyle = C.signal; ctx.lineWidth = 2.5; ctx.stroke();
    // Sub sine in Ember
    ctx.beginPath();
    for (let x = 0; x <= 496; x++) {
      const y = 275 + Math.sin((x / 496) * Math.PI * 6) * 28;
      x === 0 ? ctx.moveTo(48 + x, y) : ctx.lineTo(48 + x, y);
    }
    ctx.strokeStyle = C.ember; ctx.lineWidth = 3; ctx.stroke();
  }

  else if (vis === 'dsp_ring' || vis === 'dsp_register') {
    graticule(ctx, W, H, 48);
    const blocks = ['x[n]', 'z⁻¹', 'biquad', 'tanh()', 'y[n]'];
    blocks.forEach((b, i) => {
      const bx = 28 + i * 108;
      pill(ctx, bx, 145, 88, 88, 16, i === 2 ? C.s2 : C.s1);
      ctx.font = '600 15px "Geist Mono", monospace';
      ctx.fillStyle = i === 2 ? C.ember : C.moon;
      const tw = ctx.measureText(b).width;
      ctx.fillText(b, bx + (88 - tw)/2, 195);
      if (i < blocks.length - 1) {
        cable(ctx, bx + 88, 189, bx + 108, 189, i === 1, 3);
      }
    });
    ctx.font = '500 12px "Geist Mono", monospace';
    ctx.fillStyle = C.ink3;
    ctx.fillText('48000 HZ · 128-SAMPLE LOCK-FREE BLOCK', 28, 310);
  }

  else if (vis === 'three_voices') {
    graticule(ctx, W, H, 48);
    const rows = [
      { name: 'SUB BASS', fn: t => Math.sin(t * Math.PI * 6) * 26, ember: true },
      { name: 'KARPLUS PLUCK', fn: t => Math.exp(-t * 4.5) * Math.sin(t * Math.PI * 24) * 30, ember: false },
      { name: 'FIELD PAD', fn: t => (Math.sin(t * Math.PI * 4) * 0.6 + Math.sin(t * Math.PI * 10) * 0.4) * 26, ember: false },
    ];
    rows.forEach((r, i) => {
      const y0 = 42 + i * 108;
      pill(ctx, 28, y0, 536, 88, 16, C.s1);
      ctx.font = '500 11px "Geist Mono", monospace';
      ctx.fillStyle = r.ember ? C.ember : C.ink2;
      ctx.fillText(r.name, 48, y0 + 26);
      ctx.beginPath();
      for (let x = 0; x <= 496; x++) {
        const y = y0 + 54 + r.fn(x / 496);
        x === 0 ? ctx.moveTo(48 + x, y) : ctx.lineTo(48 + x, y);
      }
      ctx.strokeStyle = r.ember ? C.ember : C.signal;
      ctx.lineWidth = 2.4; ctx.stroke();
    });
  }

  else if (vis === 'euclidean') {
    graticule(ctx, W, H, 48);
    const cx = W / 2, cy = H / 2, R = 132;
    ctx.beginPath(); ctx.arc(cx, cy, R, 0, Math.PI * 2);
    ctx.strokeStyle = C.s3; ctx.lineWidth = 2; ctx.stroke();
    const hits = [0, 3, 6, 10, 13];
    // Polygon connecting hits
    ctx.beginPath();
    hits.forEach((h, i) => {
      const a = (h / 16) * Math.PI * 2 - Math.PI / 2;
      const x = cx + Math.cos(a) * R, y = cy + Math.sin(a) * R;
      i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    });
    ctx.closePath();
    ctx.fillStyle = 'rgba(167,177,255,0.12)'; ctx.fill();
    ctx.strokeStyle = C.signal; ctx.lineWidth = 2.5; ctx.stroke();
    // 16 step nodes
    for (let s = 0; s < 16; s++) {
      const a = (s / 16) * Math.PI * 2 - Math.PI / 2;
      const x = cx + Math.cos(a) * R, y = cy + Math.sin(a) * R;
      const isHit = hits.includes(s);
      ctx.beginPath(); ctx.arc(x, y, isHit ? 8 : 4, 0, Math.PI * 2);
      ctx.fillStyle = s === 0 ? C.ember : (isHit ? C.signal : C.s3);
      ctx.fill();
    }
    ctx.font = '600 18px "Geist Mono", monospace';
    ctx.fillStyle = C.moon;
    ctx.fillText('E(5, 16)', cx - 44, cy + 6);
  }

  else if (vis === 'note_lattice') {
    graticule(ctx, W, H, 48);
    pill(ctx, 28, 38, 536, 314, 16, C.s1);
    const notes = [[0,4],[1,2],[2,5],[3,1],[4,3],[5,6],[6,2],[7,4],[8,7],[9,5],[10,3],[11,6]];
    notes.forEach(([col, row], i) => {
      const x = 52 + col * 41, y = 290 - row * 32;
      if (i > 0) {
        const [pc, pr] = notes[i-1];
        ctx.beginPath();
        ctx.moveTo(52 + pc * 41 + 16, 290 - pr * 32 + 10);
        ctx.lineTo(x + 16, y + 10);
        ctx.strokeStyle = 'rgba(167,177,255,0.45)';
        ctx.lineWidth = 1.8; ctx.stroke();
      }
      pill(ctx, x, y, 32, 20, 6, i === 8 ? C.ember : C.signal);
    });
    ctx.font = '500 11px "Geist Mono", monospace';
    ctx.fillStyle = C.ink3;
    ctx.fillText('DORIAN QUANTIZER · MARKOV CHAIN', 52, 66);
  }

  else if (vis === 'feedback_loop') {
    graticule(ctx, W, H, 48);
    const cx = W / 2, cy = H / 2;
    for (let i = 0; i < 8; i++) {
      const s = 260 - i * 28;
      ctx.save();
      ctx.translate(cx, cy);
      ctx.rotate(i * 0.14);
      ctx.beginPath();
      ctx.roundRect(-s/2, -s*0.32, s, s*0.64, 16);
      ctx.strokeStyle = i === 2 ? C.ember : `rgba(167,177,255,${0.25 + i * 0.09})`;
      ctx.lineWidth = i === 2 ? 3 : 2;
      ctx.stroke();
      ctx.restore();
    }
  }

  else if (vis === 'granular') {
    graticule(ctx, W, H, 48);
    pill(ctx, 28, 42, 536, 306, 16, C.s1);
    // Carrier waveform
    ctx.beginPath();
    for (let x = 48; x < 544; x++) {
      const y = 210 + Math.sin(x * 0.08) * Math.sin(x * 0.017) * 58;
      x === 48 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    }
    ctx.strokeStyle = 'rgba(167,177,255,0.35)'; ctx.lineWidth = 1.5; ctx.stroke();
    // Hann-windowed grains
    for (let g = 0; g < 16; g++) {
      const gx = 68 + rand() * 420, gy = 110 + rand() * 170, gw = 44 + rand() * 44;
      ctx.beginPath();
      for (let i = 0; i <= gw; i++) {
        const env = 0.5 * (1 - Math.cos(2 * Math.PI * i / gw));
        const y = gy - env * 34;
        i === 0 ? ctx.moveTo(gx + i, y) : ctx.lineTo(gx + i, y);
      }
      ctx.strokeStyle = g === 7 ? C.ember : C.signal;
      ctx.lineWidth = g === 7 ? 3 : 1.8;
      ctx.stroke();
    }
  }

  else if (vis === 'midi_encoders') {
    graticule(ctx, W, H, 48);
    const knobs = [
      { label: 'CC 74 CUTOFF', val: 0.72, ember: true },
      { label: 'CC 71 RES', val: 0.38, ember: false },
      { label: 'CC 01 MOD', val: 0.55, ember: false },
      { label: 'DMX DIMMER', val: 0.85, ember: false },
    ];
    knobs.forEach((k, i) => {
      const col = i % 2, row = Math.floor(i / 2);
      const cx = 160 + col * 270, cy = 115 + row * 165;
      pill(ctx, cx - 115, cy - 70, 230, 145, 16, C.s1);
      const a0 = Math.PI * 0.75, a1 = a0 + Math.PI * 1.5 * k.val;
      ctx.beginPath(); ctx.arc(cx, cy - 8, 38, a0, Math.PI * 2.25);
      ctx.strokeStyle = C.s3; ctx.lineWidth = 6; ctx.lineCap = 'round'; ctx.stroke();
      ctx.beginPath(); ctx.arc(cx, cy - 8, 38, a0, a1);
      ctx.strokeStyle = k.ember ? C.ember : C.signal; ctx.lineWidth = 6; ctx.lineCap = 'round'; ctx.stroke();
      ctx.beginPath(); ctx.arc(cx, cy - 8, 24, 0, Math.PI * 2);
      ctx.fillStyle = C.s2; ctx.fill();
      ctx.font = '500 11px "Geist Mono", monospace';
      ctx.fillStyle = C.ink2;
      const tw = ctx.measureText(k.label).width;
      ctx.fillText(k.label, cx - tw/2, cy + 54);
    });
  }

  else if (vis === 'benchmarks') {
    graticule(ctx, W, H, 48);
    const items = [
      { name: 'DSP 64 VOICES', pct: 0.28, val: '1.8 ms', ember: true },
      { name: 'MESH 50K VERTS', pct: 0.42, val: '3.1 ms', ember: false },
      { name: 'GLSL 4K SHADER', pct: 0.58, val: '4.6 ms', ember: false },
      { name: 'HEADLESS RENDER', pct: 0.82, val: '6.4x RT', ember: false },
    ];
    items.forEach((it, i) => {
      const y = 44 + i * 82;
      pill(ctx, 28, y, 536, 66, 16, C.s1);
      ctx.font = '500 11px "Geist Mono", monospace';
      ctx.fillStyle = C.ink2; ctx.fillText(it.name, 48, y + 26);
      ctx.fillStyle = it.ember ? C.ember : C.moon;
      ctx.fillText(it.val, 490, y + 26);
      pill(ctx, 48, y + 38, 496, 12, 6, C.deep);
      pill(ctx, 48, y + 38, 496 * it.pct, 12, 6, it.ember ? C.ember : C.signal);
    });
  }

  else if (vis === 'webgl_embed' || vis === 'shadertoy') {
    graticule(ctx, W, H, 48);
    pill(ctx, 36, 36, 520, 318, 20, C.s1);
    pill(ctx, 56, 56, 480, 224, 14, C.deep);
    const cx = 296, cy = 168;
    for (let i = 0; i < 6; i++) {
      ctx.beginPath();
      ctx.ellipse(cx, cy, 35 + i * 24, 18 + i * 12, i * 0.25, 0, Math.PI * 2);
      ctx.strokeStyle = i === 3 ? C.ember : 'rgba(167,177,255,0.6)';
      ctx.lineWidth = i === 3 ? 2.5 : 1.5;
      ctx.stroke();
    }
    ctx.font = '500 12px "Geist Mono", monospace';
    ctx.fillStyle = C.ink2;
    ctx.fillText(vis === 'webgl_embed' ? '<iframe src="embed.html">' : 'uniform float iAudioBass;', 58, 322);
  }

  else if (vis === 'js_sketch') {
    graticule(ctx, W, H, 48);
    pill(ctx, 28, 38, 536, 314, 16, C.s1);
    const cx = 360, cy = 195;
    ctx.beginPath();
    for (let t = 0; t <= Math.PI * 2; t += 0.02) {
      const r = 85 + 28 * Math.sin(5 * t);
      const x = cx + Math.cos(t) * r, y = cy + Math.sin(t) * r;
      t === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    }
    ctx.closePath();
    ctx.strokeStyle = C.signal; ctx.lineWidth = 2.5; ctx.stroke();
    ctx.font = '500 12px "Geist Mono", monospace';
    ctx.fillStyle = C.ink2;
    ['pin("radius", 85)', 'pin("lobes", 5)', 'out("energy")'].forEach((l, i) => {
      ctx.fillStyle = i === 2 ? C.ember : C.ink2;
      ctx.fillText(l, 52, 140 + i * 32);
    });
  }

  else if (vis === 'lsystem') {
    graticule(ctx, W, H, 48);
    function branch(x, y, len, ang, depth) {
      if (depth === 0) return;
      const x2 = x + Math.cos(ang) * len, y2 = y + Math.sin(ang) * len;
      ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(x2, y2);
      ctx.strokeStyle = depth === 1 ? C.ember : C.signal;
      ctx.lineWidth = Math.max(1.2, depth * 0.9);
      ctx.lineCap = 'round';
      ctx.stroke();
      branch(x2, y2, len * 0.72, ang - 0.42, depth - 1);
      branch(x2, y2, len * 0.72, ang + 0.36, depth - 1);
    }
    branch(W / 2, 355, 78, -Math.PI / 2, 6);
  }

  else if (vis === 'linux_stack') {
    graticule(ctx, W, H, 48);
    nodeBox(ctx, 36, 56, 220, 118, 'PipeWire / JACK', 'AUDIO', ['hw:0'], [{label:'48 kHz'}]);
    nodeBox(ctx, 36, 210, 220, 118, 'Linux VST3', 'PLUGIN', ['midi'], [{label:'stereo', ember:true}]);
    nodeBox(ctx, 320, 130, 236, 130, 'Vulkan / OpenGL', 'VISUAL', ['audio', 'cv'], [{label:'stage'}], true);
    cable(ctx, 256, 100, 320, 174, false);
    cable(ctx, 256, 254, 320, 196, true);
  }

  else if (vis === 'lorenz') {
    graticule(ctx, W, H, 48);
    let x = 0.1, y = 0, z = 0, dt = 0.006;
    const pts = [];
    for (let i = 0; i < 2800; i++) {
      const dx = 10 * (y - x), dy = x * (28 - z) - y, dz = x * y - (8/3) * z;
      x += dx * dt; y += dy * dt; z += dz * dt;
      if (i > 150) pts.push([W / 2 + x * 9.5, 345 - z * 6.2]);
    }
    ctx.beginPath();
    pts.forEach(([px, py], i) => i === 0 ? ctx.moveTo(px, py) : ctx.lineTo(px, py));
    ctx.strokeStyle = 'rgba(167,177,255,0.65)'; ctx.lineWidth = 1.5; ctx.stroke();
    // Highlight last 140 steps in Ember
    ctx.beginPath();
    pts.slice(-140).forEach(([px, py], i) => i === 0 ? ctx.moveTo(px, py) : ctx.lineTo(px, py));
    ctx.strokeStyle = C.ember; ctx.lineWidth = 2.8; ctx.stroke();
    const [hx, hy] = pts[pts.length - 1];
    ctx.beginPath(); ctx.arc(hx, hy, 6, 0, Math.PI * 2); ctx.fillStyle = C.ember; ctx.fill();
  }

  else if (vis === 'first_patch') {
    graticule(ctx, W, H, 48);
    nodeBox(ctx, 28, 128, 155, 118, 'Audio Analyze', 'MOD', ['audio'], [{label:'energy'}]);
    nodeBox(ctx, 218, 128, 145, 118, 'Scale', 'MATH', ['in'], [{label:'out'}]);
    nodeBox(ctx, 398, 128, 165, 118, 'Shape 3D', '3D', ['scale'], [{label:'render'}]);
    cable(ctx, 183, 172, 218, 172, false, 3);
    cable(ctx, 363, 172, 398, 172, false, 3);
  }

  else if (vis === 'vst3_host') {
    graticule(ctx, W, H, 48);
    nodeBox(ctx, 36, 56, 210, 120, 'Plugin Sandbox', 'VST3', ['midi_in'], [{label:'audio_out'}, {label:'cv_mod', ember:true}], true);
    nodeBox(ctx, 36, 212, 210, 110, 'LFO', 'MOD', [], [{label:'phase'}]);
    nodeBox(ctx, 330, 130, 224, 128, 'Audio Output', 'OUT', ['L / R', 'cv'], []);
    cable(ctx, 246, 100, 330, 174, false);
    cable(ctx, 246, 256, 330, 196, true);
  }

  else if (vis === 'hand_track') {
    graticule(ctx, W, H, 48);
    const palm = [296, 290];
    const fingers = [
      [[296,290],[235,255],[200,210],[182,175]],
      [[296,290],[262,215],[254,155],[250,105]],
      [[296,290],[296,205],[298,138],[300,82]],
      [[296,290],[330,215],[340,155],[346,105]],
      [[296,290],[360,235],[380,188],[392,150]],
    ];
    fingers.forEach(f => {
      ctx.beginPath();
      f.forEach(([x,y], i) => i === 0 ? ctx.moveTo(x,y) : ctx.lineTo(x,y));
      ctx.strokeStyle = C.signal; ctx.lineWidth = 2.5; ctx.stroke();
      f.forEach(([x,y]) => {
        ctx.beginPath(); ctx.arc(x, y, 4.5, 0, Math.PI * 2);
        ctx.fillStyle = C.moon; ctx.fill();
      });
    });
    // Pinch vector in Ember between thumb tip & index tip
    ctx.beginPath(); ctx.moveTo(182, 175); ctx.lineTo(250, 105);
    ctx.strokeStyle = C.ember; ctx.lineWidth = 3; ctx.setLineDash([5,4]); ctx.stroke(); ctx.setLineDash([]);
    ctx.beginPath(); ctx.arc(182, 175, 6.5, 0, Math.PI * 2); ctx.fillStyle = C.ember; ctx.fill();
    ctx.beginPath(); ctx.arc(250, 105, 6.5, 0, Math.PI * 2); ctx.fillStyle = C.ember; ctx.fill();
  }

  else if (vis === 'node_graph' || vis === 'one_graph') {
    graticule(ctx, W, H, 48);
    nodeBox(ctx, 26, 56, 152, 110, 'Note Arp', 'NOTES', ['clock'], [{label:'pitch'}]);
    nodeBox(ctx, 26, 206, 152, 110, 'Audio Filter', 'FX', ['in'], [{label:'fft'}]);
    nodeBox(ctx, 214, 56, 158, 112, 'Wavetable', 'SYNTH', ['pitch'], [{label:'audio', ember:true}]);
    nodeBox(ctx, 408, 126, 160, 120, 'GLSL Shader', 'VISUAL', ['audio', 'fft'], [{label:'frame'}], true);
    cable(ctx, 178, 100, 214, 100, false);
    cable(ctx, 372, 100, 408, 170, true);
    cable(ctx, 178, 250, 408, 192, false);
  }

  else if (vis === 'triptych_av') {
    graticule(ctx, W, H, 48);
    const titles = ['WAVE TERRAIN', 'SHAPE RESONATOR', 'CYMATICS'];
    titles.forEach((t, i) => {
      const x = 28 + i * 184;
      pill(ctx, x, 48, 168, 294, 16, C.s1);
      ctx.font = '500 10px "Geist Mono", monospace';
      ctx.fillStyle = i === 1 ? C.ember : C.ink2;
      ctx.fillText(t, x + 16, 76);
      const cx = x + 84, cy = 205;
      for (let r = 18; r <= 58; r += 13) {
        ctx.beginPath();
        for (let a = 0; a <= Math.PI * 2 + 0.1; a += 0.1) {
          const rad = r + (i === 0 ? Math.sin(a * 3) * 6 : i === 1 ? Math.cos(a * 5) * 7 : Math.sin(a * 4) * 5);
          const px = cx + Math.cos(a) * rad, py = cy + Math.sin(a) * rad;
          a === 0 ? ctx.moveTo(px, py) : ctx.lineTo(px, py);
        }
        ctx.strokeStyle = i === 1 && r === 44 ? C.ember : C.signal;
        ctx.lineWidth = 2; ctx.stroke();
      }
    });
  }

  else if (vis === 'paulstretch') {
    graticule(ctx, W, H, 48);
    pill(ctx, 28, 42, 536, 306, 16, C.s1);
    // Dense transient on left -> stretched smooth drone on right
    for (let x = 48; x < 544; x += 4) {
      const t = (x - 48) / 496;
      const amp = t < 0.25 ? Math.sin(x * 0.9) * 80 : Math.sin(x * 0.04) * 42 * (0.6 + 0.4 * Math.cos(x * 0.015));
      ctx.beginPath();
      ctx.moveTo(x, 195 - Math.abs(amp));
      ctx.lineTo(x, 195 + Math.abs(amp));
      ctx.strokeStyle = Math.abs(t - 0.25) < 0.04 ? C.ember : C.signal;
      ctx.lineWidth = 2.2; ctx.stroke();
    }
  }

  else if (vis === 'string_waveguide') {
    graticule(ctx, W, H, 48);
    pill(ctx, 28, 48, 536, 294, 16, C.s1);
    for (let h = 1; h <= 4; h++) {
      ctx.beginPath();
      for (let x = 0; x <= 480; x++) {
        const env = Math.sin((x / 480) * Math.PI);
        const y = 195 + env * Math.sin((x / 480) * Math.PI * h) * (68 / h);
        x === 0 ? ctx.moveTo(56 + x, y) : ctx.lineTo(56 + x, y);
      }
      ctx.strokeStyle = h === 1 ? C.ember : 'rgba(167,177,255,0.55)';
      ctx.lineWidth = h === 1 ? 3 : 1.8; ctx.stroke();
    }
    ctx.beginPath(); ctx.arc(56, 195, 5, 0, Math.PI*2); ctx.fillStyle = C.moon; ctx.fill();
    ctx.beginPath(); ctx.arc(536, 195, 5, 0, Math.PI*2); ctx.fillStyle = C.moon; ctx.fill();
  }

  else if (vis === 'mesh_deform') {
    graticule(ctx, W, H, 48);
    const cx = W / 2, cy = H / 2;
    for (let lat = -4; lat <= 4; lat++) {
      ctx.beginPath();
      for (let lon = 0; lon <= 64; lon++) {
        const a = (lon / 64) * Math.PI * 2;
        const r = 125 * Math.cos(lat * 0.32) + Math.sin(a * 4 + lat) * 14;
        const x = cx + Math.cos(a) * r;
        const y = cy + lat * 26 + Math.sin(a) * r * 0.28;
        lon === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
      }
      ctx.strokeStyle = lat === 0 ? C.ember : 'rgba(167,177,255,0.6)';
      ctx.lineWidth = lat === 0 ? 2.8 : 1.6; ctx.stroke();
    }
  }

  else if (vis === 'raymarching') {
    graticule(ctx, W, H, 48);
    // SDF sphere on right
    const sx = 430, sy = 195, sr = 82;
    ctx.beginPath(); ctx.arc(sx, sy, sr, 0, Math.PI * 2);
    ctx.fillStyle = 'rgba(167,177,255,0.16)'; ctx.fill();
    ctx.strokeStyle = C.signal; ctx.lineWidth = 2.5; ctx.stroke();
    // Sphere tracing steps from left
    let rx = 75, ry = 195;
    ctx.beginPath(); ctx.moveTo(rx, ry); ctx.lineTo(sx - sr, sy);
    ctx.strokeStyle = C.moon; ctx.lineWidth = 2; ctx.stroke();
    for (let step = 0; step < 4; step++) {
      const dist = (sx - sr) - rx;
      if (dist < 6) break;
      ctx.beginPath(); ctx.arc(rx, ry, dist, 0, Math.PI * 2);
      ctx.strokeStyle = 'rgba(167,177,255,0.35)'; ctx.lineWidth = 1.4; ctx.stroke();
      ctx.beginPath(); ctx.arc(rx, ry, 4, 0, Math.PI * 2); ctx.fillStyle = C.signal; ctx.fill();
      rx += dist * 0.62;
    }
    ctx.beginPath(); ctx.arc(sx - sr, sy, 7, 0, Math.PI * 2); ctx.fillStyle = C.ember; ctx.fill();
  }

  else if (vis === 'reaction_diffusion') {
    graticule(ctx, W, H, 48);
    pill(ctx, 28, 36, 536, 318, 16, C.s1);
    for (let y = 58; y < 332; y += 16) {
      for (let x = 52; x < 540; x += 16) {
        const v = Math.sin(x * 0.045) * Math.cos(y * 0.045) + Math.sin((x + y) * 0.03);
        const r = Math.max(1.5, (v + 1.8) * 3.1);
        ctx.beginPath(); ctx.arc(x, y, r, 0, Math.PI * 2);
        const isHot = Math.hypot(x - 300, y - 195) < 28;
        ctx.fillStyle = isHot ? C.ember : (v > 0.2 ? C.signal : C.s3);
        ctx.fill();
      }
    }
  }

  else if (vis === 'fluid_sim') {
    graticule(ctx, W, H, 48);
    const cx1 = 220, cy1 = 165, cx2 = 385, cy2 = 220;
    for (let y = 48; y < 345; y += 26) {
      for (let x = 44; x < 550; x += 26) {
        const dx1 = x - cx1, dy1 = y - cy1, d1 = Math.hypot(dx1, dy1) + 25;
        const dx2 = x - cx2, dy2 = y - cy2, d2 = Math.hypot(dx2, dy2) + 25;
        const vx = (-dy1 / d1) * 18 + (dy2 / d2) * 18;
        const vy = (dx1 / d1) * 18 - (dx2 / d2) * 18;
        ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(x + vx, y + vy);
        ctx.strokeStyle = d1 < 75 ? C.ember : 'rgba(167,177,255,0.65)';
        ctx.lineWidth = d1 < 75 ? 2.4 : 1.6; ctx.lineCap = 'round'; ctx.stroke();
      }
    }
  }

  else if (vis === 'text_to_video') {
    graticule(ctx, W, H, 48);
    pill(ctx, 28, 56, 250, 278, 16, C.s1);
    ctx.font = '500 12px "Geist Mono", monospace';
    const lines = ['node Osc Sine', 'node F Shader', 'wire Osc -> F', 'render 1080p'];
    lines.forEach((l, i) => {
      ctx.fillStyle = i === 3 ? C.ember : C.ink2;
      ctx.fillText(l, 48, 110 + i * 36);
    });
    cable(ctx, 278, 195, 330, 195, true, 3);
    pill(ctx, 330, 76, 234, 238, 16, C.s2);
    ctx.beginPath(); ctx.arc(447, 195, 58, 0, Math.PI * 2);
    ctx.strokeStyle = C.signal; ctx.lineWidth = 3; ctx.stroke();
    ctx.font = '500 11px "Geist Mono", monospace';
    ctx.fillStyle = C.moon; ctx.fillText('-14.0 LUFS', 410, 288);
  }

  else if (vis === 'wave_terrain') {
    graticule(ctx, W, H, 48);
    const ox = 85, oy = 115;
    for (let row = 0; row < 12; row++) {
      ctx.beginPath();
      for (let col = 0; col <= 36; col++) {
        const u = col / 36, v = row / 11;
        const z = Math.sin(u * Math.PI * 3) * Math.cos(v * Math.PI * 2) * 34;
        const px = ox + u * 350 + v * 95;
        const py = oy + v * 160 - z;
        col === 0 ? ctx.moveTo(px, py) : ctx.lineTo(px, py);
      }
      ctx.strokeStyle = 'rgba(167,177,255,0.55)'; ctx.lineWidth = 1.6; ctx.stroke();
    }
    // Orbital trajectory in Ember
    ctx.beginPath();
    for (let a = 0; a <= Math.PI * 2 + 0.05; a += 0.08) {
      const u = 0.5 + 0.32 * Math.cos(a), v = 0.5 + 0.34 * Math.sin(a);
      const z = Math.sin(u * Math.PI * 3) * Math.cos(v * Math.PI * 2) * 34;
      const px = ox + u * 350 + v * 95;
      const py = oy + v * 160 - z;
      a === 0 ? ctx.moveTo(px, py) : ctx.lineTo(px, py);
    }
    ctx.strokeStyle = C.ember; ctx.lineWidth = 3; ctx.stroke();
  }
}
"""


def build_html(slug: str, spec: dict) -> str:
    vis = spec["visual"]
    kicker = spec["kicker"]
    headline = spec["headline"]
    hand = spec.get("hand")
    spec_line = spec["spec"]

    # Dot grid programmed field (<= 4.5% opacity)
    dots = []
    for gx in range(36, W - 24, 32):
        for gy in range(36, H - 24, 32):
            dots.append(f'<circle cx="{gx}" cy="{gy}" r="1.1"/>')
    dot_svg = f'<svg class="pfield" viewBox="0 0 {W} {H}" aria-hidden="true"><g fill="rgba(167,177,255,0.055)">{"".join(dots)}</g></svg>'

    if vis == "cutout":
        cutout_path = os.path.join(RENDERS, spec["cutout"])
        badge = spec.get("badge", "")
        badge_html = f'<div class="stage-badge">{E(badge)}</div>' if badge else ""
        stage_inner = (
            f'<div class="cutout-wrap">'
            f'<img class="cutout-img" src="file://{cutout_path}" alt="">'
            f'{badge_html}'
            f"</div>"
        )
    else:
        stage_inner = '<canvas id="cv" class="vis-canvas"></canvas>'

    return f"""<!doctype html>
<html><head><meta charset="utf-8"><style>
{font_faces()}
*{{box-sizing:border-box;margin:0;padding:0}}
html,body{{width:{W}px;height:{H}px;overflow:hidden;background:{M['ground']};color:{M['ink']};font-family:Geist,sans-serif;position:relative}}
.bg-glow{{position:absolute;inset:0;background:radial-gradient(circle at 74% 48%, rgba(42,48,82,0.55) 0%, rgba(13,16,32,0.85) 100%)}}
.pfield{{position:absolute;inset:0;width:100%;height:100%;pointer-events:none}}
.wrap{{position:relative;z-index:2;width:100%;height:100%;padding:64px 68px 82px;display:flex;align-items:center;gap:36px}}
.left{{width:445px;flex:none;display:flex;flex-direction:column;justify-content:center;gap:20px}}
.brand-row{{display:flex;align-items:center;gap:14px}}
.mark{{width:52px;flex:none}} .mark svg{{width:100%;display:block}}
.kicker{{font:500 13px/1.2 "Geist Mono",monospace;letter-spacing:.12em;text-transform:uppercase;color:{M['ink-3']}}}
h1{{font:800 54px/1.02 Geist,sans-serif;letter-spacing:-.04em;color:{M['ink']}}}
.hand{{font-family:Caveat,cursive;font-weight:700;color:{M['accent']};letter-spacing:0;font-size:1.18em;position:relative;white-space:nowrap;display:inline-block}}
.ul{{position:absolute;left:0;right:0;bottom:-.05em;width:100%;height:.16em;overflow:visible}}
.ul path{{fill:none;stroke:{M['accent']};stroke-width:3;stroke-linecap:round;vector-effect:non-scaling-stroke}}
.spec{{font:500 14px/1.4 "Geist Mono",monospace;color:{M['ink-2']};letter-spacing:.01em}}
.stage{{flex:1;height:100%;min-width:0;border-radius:24px;background:{M['deep']};box-shadow:0 24px 64px rgba(5,7,18,0.55);overflow:hidden;position:relative;display:flex;align-items:center;justify-content:center}}
.vis-canvas{{width:100%;height:100%;display:block}}
.cutout-wrap{{width:100%;height:100%;display:flex;align-items:center;justify-content:center;padding:18px;position:relative}}
.cutout-img{{max-width:100%;max-height:100%;object-fit:contain;filter:drop-shadow(0 18px 36px rgba(5,7,18,0.6))}}
.stage-badge{{position:absolute;top:20px;left:22px;padding:7px 14px;border-radius:999px;background:{M['surface-1']};font:500 11px/1 "Geist Mono",monospace;letter-spacing:.08em;color:{M['signal']}}}
</style></head>
<body>
<div class="bg-glow"></div>
{dot_svg}
<div class="wrap">
  <div class="left">
    <div class="brand-row">
      <div class="mark">{LOGO_SVG}</div>
      <div class="kicker">{E(kicker)}</div>
    </div>
    <h1>{headline_html(headline, hand)}</h1>
    <p class="spec">{E(spec_line)}</p>
  </div>
  <div class="stage">{stage_inner}</div>
</div>
<script>
{JS_RENDERERS}
document.fonts.ready.then(() => {{
  {build_stage_script(spec)}
  document.body.dataset.done = "1";
}});
</script>
</body></html>"""


def render_png(html_str: str, out_png: str) -> None:
    if not CHROME:
        sys.exit("Google Chrome not found; cannot render thumbnails.")
    with tempfile.TemporaryDirectory() as tmp:
        hpath = os.path.join(tmp, "card.html")
        open(hpath, "w", encoding="utf-8").write(html_str)
        cmd = [
            CHROME,
            "--headless=new",
            "--disable-gpu",
            "--hide-scrollbars",
            f"--window-size={W},{H}",
            "--virtual-time-budget=1500",
            f"--screenshot={out_png}",
            f"file://{hpath}",
        ]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def update_post_frontmatter(slug: str, rel_img: str) -> bool:
    md_path = os.path.join(POSTS_DIR, f"{slug}.md")
    if not os.path.exists(md_path):
        return False
    txt = open(md_path, encoding="utf-8").read()
    parts = txt.split("---", 2)
    if len(parts) < 3:
        return False
    fm = parts[1]
    if re.search(r"^image:\s*.*$", fm, re.M):
        fm2 = re.sub(r"^image:\s*.*$", f"image: {rel_img}", fm, count=1, flags=re.M)
    else:
        fm2 = fm.rstrip() + f"\nimage: {rel_img}\n"
    if fm2 != fm:
        open(md_path, "w", encoding="utf-8").write(f"---{fm2}---{parts[2]}")
        return True
    return False


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", help="Comma-separated slugs to render")
    ap.add_argument("--update-posts", action="store_true", help="Update image: frontmatter in tools/seo/posts/*.md")
    args = ap.parse_args()

    slugs = [s.strip() for s in args.only.split(",")] if args.only else sorted(POST_SPECS.keys())
    for slug in slugs:
        if slug not in POST_SPECS:
            print(f"SKIP unknown slug: {slug}", file=sys.stderr)
            continue
        rel_img = f"assets/blog_{slug}.png"
        out_png = os.path.join(ROOT, "website", rel_img)
        os.makedirs(os.path.dirname(out_png), exist_ok=True)
        render_png(build_html(slug, POST_SPECS[slug]), out_png)
        if args.update_posts:
            update_post_frontmatter(slug, rel_img)
        print(f"rendered {rel_img}")


if __name__ == "__main__":
    main()
