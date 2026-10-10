#pragma once
// Built-in Sketch presets. Header-only so SketchNode and sketch-check share one list.
// Every preset is a pure function of (t, params, width, height): no state carried
// between frames, so scrubbing and export give the same pixels.
namespace SketchPresets
{
struct Entry { const char* name; const char* code; const char* svg = nullptr; };  // svg: optional document the preset animates

inline const Entry* All(int& n)
{
   static const Entry k[] = {
{"Radial burst", R"(// Radial burst. Every param() is a knob you can modulate.
param("count", 12, 1, 64);
param("spin", 0.2, 0, 2);
param("size", 48, 4, 160);

function draw(t) {
  background(0.05);
  translate(width / 2, height / 2);
  noStroke();
  const r = min(width, height) * 0.3;
  for (let i = 0; i < count; i++) {
    rotate(TAU / count + spin * t * 0.1);
    fill(hsl(i / count, 0.7, 0.6));
    circle(r * 1.2 + r * 0.4 * sin(t + i), 0, size);
  }
}
)"},
{"Flow field", R"(// Flow-field lines.
param("lines", 140, 20, 600);
param("scale", 2.5, 0.5, 8);
param("weight", 1.5, 0.5, 6);

function draw(t) {
  background(0.04, 0.05, 0.08);
  noFill();
  strokeWeight(weight);
  for (let i = 0; i < lines; i++) {
    let x = ((i * 0.6180339) % 1) * width;
    let y = ((i * 0.7548776) % 1) * height;
    stroke(hsl(0.55 + 0.2 * (i / lines), 0.6, 0.65), 0.7);
    beginShape();
    for (let s = 0; s < 32; s++) {
      vertex(x, y);
      const a = noise(x / width * scale, y / height * scale, t * 0.2) * TAU * 2;
      x += cos(a) * 10;
      y += sin(a) * 10;
    }
    endShape();
  }
}
)"},
{"Tree", R"(// Recursive tree.
param("depth", 9, 2, 12);
param("angle", 0.45, 0.1, 1.2);
param("sway", 0.08, 0, 0.4);

let T = 0;
function branch(len, d) {
  strokeWeight(max(1, d * 0.9));
  stroke(0.35 + 0.05 * (12 - d), 0.55, 0.3 + 0.04 * (12 - d));
  line(0, 0, 0, -len);
  translate(0, -len);
  if (d <= 0) return;
  push(); rotate(angle + sway * sin(T * 1.3 + d)); branch(len * 0.74, d - 1); pop();
  push(); rotate(-angle + sway * sin(T * 1.1 + d)); branch(len * 0.74, d - 1); pop();
}

function draw(t) {
  T = t;
  background(0.93, 0.92, 0.88);
  translate(width / 2, height * 0.95);
  branch(height * 0.2, depth);
}
)"},
{"Kinetic type", R"(// Words that ride a wave. Change the string, keep the knobs.
param("size", 260, 24, 500);
param("wave", 40, 0, 200);
param("speed", 1, 0, 4);

const word = "INFINITE";
function draw(t) {
  background(0.06);
  textSize(size);
  noStroke();
  const total = textWidth(word);
  let x = (width - total) / 2;
  for (let i = 0; i < word.length; i++) {
    const ch = word[i];
    const y = height / 2 + wave * sin(t * speed * 2 + i * 0.7);
    fill(hsl(0.08 + i * 0.06, 0.8, 0.65));
    text(ch, x, y);
    x += textWidth(ch);
  }
}
)"},
{"Grid poster", R"(// Swiss-style grid of rotating squares.
param("cols", 8, 2, 24);
param("gap", 0.15, 0, 0.5);
param("twist", 1, 0, 4);

function draw(t) {
  background(0.95, 0.94, 0.9);
  const rows = Math.max(1, Math.round(cols * height / width));
  const cw = width / cols, ch = height / rows;
  noStroke();
  for (let r = 0; r < rows; r++) {
    for (let c = 0; c < cols; c++) {
      const k = (r * cols + c) / (rows * cols);
      push();
      translate((c + 0.5) * cw, (r + 0.5) * ch);
      rotate(twist * sin(t * 0.6 + k * TAU));
      fill(hsl(0.02 + 0.1 * k, 0.8, 0.5 + 0.1 * sin(t + k * 9)));
      const s = Math.min(cw, ch) * (1 - gap);
      rect(-s / 2, -s / 2, s, s);
      pop();
    }
  }
}
)"},
{"Particles", R"(// A particle swarm. Each particle is a pure function of t, so it scrubs.
param("count", 300, 10, 1500);
param("orbit", 0.45, 0.05, 0.6);
param("dot", 7, 1, 20);

function hash(n) { const s = Math.sin(n * 127.1 + 311.7) * 43758.5453; return s - Math.floor(s); }

function draw(t) {
  background(0.02, 0.02, 0.05);
  noStroke();
  const R = min(width, height);
  for (let i = 0; i < count; i++) {
    const r = R * orbit * (0.3 + 0.7 * hash(i));
    const w = (0.2 + hash(i + 17)) * (hash(i + 5) < 0.5 ? 1 : -1);
    const a = hash(i + 91) * TAU + t * w;
    const x = width / 2 + r * cos(a), y = height / 2 + r * sin(a) * 0.8;
    fill(hsl(0.55 + 0.3 * hash(i + 3), 0.7, 0.7), 0.8);
    circle(x, y, dot * (0.5 + hash(i + 40)));
  }
}
)"},
{"Lissajous", R"(// A harmonograph: two frequencies trace a looping curve.
param("a", 3, 1, 9);
param("b", 4, 1, 9);
param("trail", 400, 50, 1200);

function draw(t) {
  background(0.03, 0.03, 0.06);
  noFill();
  strokeWeight(3);
  stroke(hsl(0.12 + 0.1 * sin(t * 0.3), 0.8, 0.65));
  const R = min(width, height) * 0.42;
  beginShape();
  for (let i = 0; i < trail; i++) {
    const u = t * 0.5 - i * 0.01;
    vertex(width / 2 + R * sin(a * u + 1.2), height / 2 + R * sin(b * u));
  }
  endShape();
}
)"},
{"Rings", R"(// Concentric pulsing rings.
param("rings", 14, 2, 40);
param("pulse", 0.5, 0, 1);
param("weight", 4, 1, 20);

function draw(t) {
  background(0.97);
  noFill();
  strokeWeight(weight);
  const R = min(width, height) * 0.48;
  for (let i = 1; i <= rings; i++) {
    const k = i / rings;
    stroke(hsl(0.6 - 0.5 * k, 0.6, 0.45), 0.9);
    circle(width / 2, height / 2, 2 * R * k * (1 + pulse * 0.1 * sin(t * 2 - k * 6)));
  }
}
)"},
{"Bloom", R"JS(// A flower. 'open' takes it from bud to full bloom: cable an LFO to it and it breathes.
// ---- Knobs -------------------------------------------------------------------
param("open",       0.85, 0,    1);      // bud (0) to full bloom (1)
param("petals",     12,   5,    24);     // petals in the front layer
param("layers",     3,    1,    3);      // how many petal layers to draw
param("petalLen",   0.26, 0.10, 0.40);   // petal length, fraction of the short side
param("petalWide",  0.34, 0.15, 0.60);   // petal width relative to its length
param("hue",        0.93, 0,    1);      // petal hue (0.93 = pink, 0.12 = sunflower)
param("sat",        0.70, 0,    1);
param("seeds",      260,  40,   600);    // seeds in the centre spiral
param("breeze",     0.35, 0,    1.5);    // sway amount
param("spinHead",   0.05, -0.5, 0.5);    // slow rotation of the head
param("beatBloom",  0.15, 0,    0.5);    // how much each beat makes the flower swell
param("motes",      40,   0,    120);    // drifting pollen count


// ---- Helpers -----------------------------------------------------------------
const GOLDEN = 2.399963229728653;        // golden angle in radians

function hash1(n) {                       // deterministic 0..1 from an index
  const s = Math.sin(n * 127.1 + 311.7) * 43758.5453;
  return s - Math.floor(s);
}

function ease(x) { return x * x * (3 - 2 * x); }          // smoothstep on 0..1

// A flat bloom pulse on the beat: quick swell, slow settle.
function beatSwell() {
  const p = beat - Math.floor(beat);
  return Math.pow(1 - p, 3) * Math.min(1, p * 10);
}

// Draws one petal pointing along +x from the origin, length L, half-width W.
// The outline is built from two mirrored sides so it is always symmetric.
function petalShape(L, W, notch) {
  const n = 18;
  beginShape();
  // up one side
  for (let i = 0; i <= n; i++) {
    const u = i / n;
    const w = W * Math.pow(Math.sin(Math.PI * Math.pow(u, 0.75)), 0.9);
    vertex(L * u, -w);
  }
  // back down the other side; a small notch at the tip for a natural look
  for (let i = n; i >= 0; i--) {
    const u = i / n;
    const w = W * Math.pow(Math.sin(Math.PI * Math.pow(u, 0.75)), 0.9);
    const tipDip = (i > n - 3) ? notch * W * 0.25 : 0;
    vertex(L * u - tipDip, w);
  }
  endShape(CLOSE);
}

function leaf(L, W) {                      // a pointed leaf along +x with veins
  const n = 16;
  beginShape();
  for (let i = 0; i <= n; i++) {
    const u = i / n;
    vertex(L * u, -W * Math.sin(Math.PI * Math.pow(u, 0.8)));
  }
  for (let i = n; i >= 0; i--) {
    const u = i / n;
    vertex(L * u, W * Math.sin(Math.PI * Math.pow(u, 0.8)));
  }
  endShape(CLOSE);
}


// ---- Main --------------------------------------------------------------------
function draw(t) {
  const S = Math.min(width, height);
  const cx = width * 0.5;
  const headY = height * 0.40;
  const baseY = height * 1.02;
  const swell = 1 + beatBloom * beatSwell();
  const sway = Math.sin(t * 0.9) * breeze;            // shared sway, radians-ish

  // ---- 1. Sky -------------------------------------------------------------
  background(0.97, 0.94, 0.90);
  noStroke();
  for (let i = 0; i < 20; i++) {
    const k = i / 19;
    fill(...hsl(hue - 0.55 + 0.05 * k, 0.45, 0.93 - 0.10 * k));
    rect(0, k * height, width, height / 20 + 3);
  }
  // pollen motes drifting upward and sideways
  const nm = Math.floor(motes);
  for (let i = 0; i < nm; i++) {
    const sp = 0.02 + 0.05 * hash1(i * 3.3);
    const x = ((hash1(i * 5.1) + t * 0.01 * (1 + hash1(i))) % 1) * width;
    const y = height - (((hash1(i * 7.7 + 1) + t * sp) % 1) * height);
    fill(1, 0.95, 0.7, 0.35 + 0.35 * Math.sin(t * 2 + i));
    circle(x + Math.sin(t + i) * 8, y, 3 + 4 * hash1(i * 9.1));
  }

  // ---- 2. Stem and leaves -------------------------------------------------
  // The stem is a bezier-like curve sampled as a polyline so we can also place
  // leaves along it at known points.
  const stemPts = [];
  const steps = 28;
  for (let i = 0; i <= steps; i++) {
    const u = i / steps;                               // 0 at base, 1 at the head
    const x = cx + Math.sin(u * 2.2 + 0.3) * S * 0.035 * (1 - u * 0.3)
                 + sway * S * 0.05 * u * u;
    const y = baseY + (headY - baseY) * u;
    stemPts.push([x, y]);
  }
  noFill();
  stroke(0.22, 0.45, 0.20);
  strokeWeight(S * 0.014);
  beginShape();
  for (const p of stemPts) vertex(p[0], p[1]);
  endShape();
  // lighter highlight stripe
  stroke(0.36, 0.60, 0.30, 0.6);
  strokeWeight(S * 0.004);
  beginShape();
  for (const p of stemPts) vertex(p[0] - S * 0.003, p[1]);
  endShape();

  // two leaves at fixed heights up the stem, mirrored, each swaying a little
  const leafAt = [{ u: 0.30, side: -1, ang: 0.55 }, { u: 0.55, side: 1, ang: 0.50 }];
  for (const lf of leafAt) {
    const p = stemPts[Math.floor(lf.u * steps)];
    push();
    translate(p[0], p[1]);
    const a = lf.side < 0 ? Math.PI + lf.ang : -lf.ang;
    rotate(a + Math.sin(t * 1.1 + lf.u * 5) * 0.06 * (1 + breeze));
    // leaf body
    noStroke();
    fill(0.30, 0.58, 0.26);
    leaf(S * 0.20, S * 0.045);
    // darker lower half for form
    fill(0.20, 0.45, 0.18, 0.45);
    push(); scale(1, 0.5); leaf(S * 0.20, S * 0.045); pop();
    // midrib and side veins
    stroke(0.85, 0.95, 0.70, 0.7);
    strokeWeight(1.5);
    line(0, 0, S * 0.19, 0);
    strokeWeight(1);
    for (let v = 1; v < 6; v++) {
      const vx = S * 0.20 * v / 6;
      line(vx, 0, vx + S * 0.03, -S * 0.025);
      line(vx, 0, vx + S * 0.03,  S * 0.025);
    }
    pop();
  }

  // ---- 3. Flower head -----------------------------------------------------
  const hx = stemPts[steps][0], hy = stemPts[steps][1];
  push();
  translate(hx, hy);
  rotate(sway * 0.12 + t * spinHead);

  const o = ease(constrain(open, 0, 1));               // eased openness
  const nLayers = Math.floor(layers);
  const N = Math.floor(petals);

  // Layer table: back layer is wider and darker, front is brighter and shorter.
  const L = [
    { count: N,                lenK: 1.00, wideK: 1.00, lit: 0.46, rot: 0.0,          lift: 0.00 },
    { count: Math.max(5, N-2), lenK: 0.82, wideK: 0.95, lit: 0.56, rot: Math.PI / N,  lift: 0.10 },
    { count: Math.max(5, N-4), lenK: 0.62, wideK: 0.90, lit: 0.67, rot: Math.PI / N * 0.5, lift: 0.20 },
  ];

  for (let li = 0; li < nLayers; li++) {
    const ly = L[li];
    const len = S * petalLen * ly.lenK * swell * (0.35 + 0.65 * o);   // bud is short
    const wid = len * petalWide * ly.wideK * (0.30 + 0.70 * o);       // and narrow
    for (let i = 0; i < ly.count; i++) {
      const a = ly.rot + (i / ly.count) * TAU;
      // each petal flutters slightly and cups inward when closed
      const flutter = Math.sin(t * 1.3 + i * 1.7 + li) * 0.025 * (1 + breeze);
      const cup = (1 - o) * 0.55;                      // closed petals lean in
      push();
      rotate(a + flutter);
      translate(len * 0.05, 0);

      // body
      noStroke();
      fill(...hsl(hue + 0.015 * li, sat, ly.lit));
      petalShape(len, wid, 0.6);

      // darker crease down the middle
      stroke(...hsl(hue, sat, ly.lit - 0.18), 0.55);
      strokeWeight(Math.max(1, S * 0.003));
      line(len * 0.05, 0, len * 0.85, 0);

      // lighter tip so the petal reads as lit from above
      noStroke();
      fill(...hsl(hue + 0.01, sat * 0.8, Math.min(0.9, ly.lit + 0.22)), 0.55);
      push(); translate(len * 0.62, 0); scale(0.38, 0.8); petalShape(len, wid, 0.6); pop();

      // soft outline
      noFill();
      stroke(...hsl(hue, sat, ly.lit - 0.22), 0.5);
      strokeWeight(1.2);
      petalShape(len, wid, 0.6);
      pop();
    }
  }

  // ---- 4. Seed head (phyllotaxis) ----------------------------------------
  const nSeeds = Math.floor(seeds);
  const headR = S * 0.065 * swell * (0.5 + 0.5 * o);
  // dark disc under the seeds
  noStroke();
  fill(0.28, 0.16, 0.10);
  circle(0, 0, headR * 2.25);
  for (let i = 0; i < nSeeds; i++) {
    const r = headR * Math.sqrt((i + 0.5) / nSeeds);   // even area coverage
    const a = i * GOLDEN + t * spinHead * 2;
    const k = i / nSeeds;
    // centre dark, edge golden; a travelling shimmer along the spiral
    const sh = 0.5 + 0.5 * Math.sin(t * 2.2 - k * 14);
    fill(...hsl(0.09 + 0.04 * k, 0.75, 0.22 + 0.30 * k + 0.12 * sh));
    circle(Math.cos(a) * r, Math.sin(a) * r, headR * 0.075 * (0.7 + 0.5 * k));
  }
  // glowing ring where seeds meet petals
  noFill();
  stroke(1.0, 0.85, 0.40, 0.5);
  strokeWeight(Math.max(1.5, S * 0.004));
  circle(0, 0, headR * 2.2);

  // ---- 5. Stamens ---------------------------------------------------------
  for (let i = 0; i < 16; i++) {
    const a = i / 16 * TAU + 0.2;
    const r1 = headR * 1.15, r2 = headR * (1.35 + 0.1 * Math.sin(t * 2 + i));
    stroke(0.95, 0.80, 0.45, 0.7);
    strokeWeight(1);
    line(Math.cos(a) * r1, Math.sin(a) * r1, Math.cos(a) * r2, Math.sin(a) * r2);
    noStroke();
    fill(1.0, 0.88, 0.40);
    circle(Math.cos(a) * r2, Math.sin(a) * r2, S * 0.008);
  }
  pop();
}
)JS"},
{"Aurora", R"JS(// Aurora ribbons over an orbiting engine, with a beat pulse and a moving title.
// ---- Knobs -------------------------------------------------------------------
// Sky and mood
param("hue",        0.58, 0.00, 1.00);   // base hue of the whole palette
param("hueShift",   0.18, 0.00, 0.50);   // how far the second/third layers drift from it
param("night",      0.06, 0.00, 0.30);   // sky darkness (0 black, 0.3 dusk)

// Stars
param("stars",      180,  0,    400);    // star count
param("twinkle",    1.2,  0,    6);      // twinkle speed

// Aurora
param("ribbons",    5,    1,    14);     // ribbons per colour layer
param("flow",       0.35, 0,    2);      // how fast the aurora churns
param("curl",       2.4,  0.2,  8);      // noise frequency, higher = tighter curls
param("glow",       0.85, 0,    1);      // ribbon opacity

// Orbital engine
param("rings",      5,    1,    10);     // concentric rings
param("spin",       0.25, -2,   2);      // rotation speed (negative reverses)
param("orbit",      0.30, 0.1,  0.5);    // engine size as a fraction of the short side
param("sats",       6,    0,    16);     // satellites per ring

// Beat pulse
param("pulse",      0.8,  0,    1);      // beat ring strength
param("bpmFeel",    1,    0.25, 4);      // beats per pulse cycle (1 = every beat)

// Horizon and title
param("mount",      0.16, 0,    0.4);    // mountain height as a fraction of height
param("titleSize",  120,  20,   300);
param("wave",       24,   0,    120);    // title wave height in pixels


// ---- Helpers -----------------------------------------------------------------

// Cheap integer-ish hash -> 0..1. Same index always gives the same number.
function hash1(n) {
  const s = Math.sin(n * 127.1 + 311.7) * 43758.5453;
  return s - Math.floor(s);
}

// Smooth ease for the beat pulse: fast attack, slow decay.
function pulseCurve(x) {            // x in 0..1 across one pulse cycle
  const a = Math.min(1, x * 8);     // attack over the first 12%
  const d = Math.pow(1 - x, 3);     // decay
  return a * d;
}

// A palette entry: hue offset from the base hue, wrapped to 0..1 by hsl().
function pal(offset, s, l, a) {
  return a === undefined ? hsl(hue + offset, s, l) : [...hsl(hue + offset, s, l), a];
}

function colour(c, a) {              // fill/stroke helper that accepts a palette array
  return a === undefined ? c : [c[0], c[1], c[2], a];
}


// ---- 1. Sky ------------------------------------------------------------------
function drawSky() {
  background(night * 0.6, night * 0.8, night * 1.4);
  noStroke();
  const bands = 24;
  for (let i = 0; i < bands; i++) {
    const k = i / (bands - 1);                     // 0 top, 1 horizon
    const c = hsl(hue + 0.04 * k, 0.55, night + 0.10 * k * k);
    fill(...c, 0.55);
    rect(0, k * height * 0.8, width, height * 0.8 / bands + 1);
  }
}


// ---- 2. Stars ----------------------------------------------------------------
function drawStars() {
  noStroke();
  const n = Math.floor(stars);
  for (let i = 0; i < n; i++) {
    const x = hash1(i * 3.1) * width;
    const y = hash1(i * 5.7 + 1) * height * 0.72;      // keep them in the sky
    const size = 0.8 + hash1(i * 9.3 + 2) * 2.2;
    const phase = hash1(i * 7.9 + 3) * TAU;
    const tw = 0.55 + 0.45 * sin(t * twinkle * (0.6 + hash1(i) ) + phase);
    fill(0.92, 0.95, 1.0, 0.25 + 0.7 * tw);
    circle(x, y, size * (0.8 + 0.4 * tw));
  }
}


// ---- 3. Aurora ---------------------------------------------------------------
// Each ribbon is a polyline whose height at every x comes from 2D noise that
// slides through time. Three layers (back, middle, front) use different
// hue offsets and opacities to build depth.
function drawAurora() {
  const layers = [
    { off: 0.00,            s: 0.65, l: 0.55, a: 0.55, lift: 0.00, amp: 0.20 },
    { off: hueShift,        s: 0.70, l: 0.60, a: 0.80, lift: 0.04, amp: 0.16 },
    { off: -hueShift * 0.6, s: 0.60, l: 0.70, a: 1.00, lift: 0.08, amp: 0.12 },
  ];
  const nRib = Math.floor(ribbons);
  const steps = 48;                                   // x samples per ribbon
  noFill();
  for (let li = 0; li < layers.length; li++) {
    const L = layers[li];
    for (let r = 0; r < nRib; r++) {
      const seed = li * 31 + r * 7.3;
      const baseY = height * (0.18 + 0.30 * (r / Math.max(1, nRib - 1)) - L.lift);
      // Soft wide stroke, then a bright thin core, for a glow without blur.
      for (let pass = 0; pass < 2; pass++) {
        strokeWeight(pass === 0 ? 16 : 2.4);
        stroke(...hsl(hue + L.off + 0.02 * r / nRib, L.s, L.l + pass * 0.15),
               glow * L.a * (pass === 0 ? 0.20 : 0.95));
        beginShape();
        for (let s = 0; s <= steps; s++) {
          const u = s / steps;
          const x = u * width;
          const n = noise(u * curl + seed, r * 0.37, t * flow);
          const m = noise(u * curl * 2.1 + 40 + seed, r * 0.9, t * flow * 1.4);
          const y = baseY + (n - 0.5) * height * L.amp * 2 + (m - 0.5) * height * 0.04;
          vertex(x, y);
        }
        endShape();
      }
    }
  }
}


// ---- 4. Orbital engine -------------------------------------------------------
function drawEngine() {
  const R = Math.min(width, height) * orbit;
  const cx = width * 0.5, cy = height * 0.46;
  const nRings = Math.floor(rings);
  const nSats = Math.floor(sats);

  push();
  translate(cx, cy);

  // Faint spokes behind everything
  stroke(...pal(0.05, 0.4, 0.8), 0.10);
  strokeWeight(1);
  for (let i = 0; i < 24; i++) {
    push();
    rotate(i / 24 * TAU + t * spin * 0.2);
    line(R * 0.1, 0, R * 1.15, 0);
    pop();
  }

  // Rings, each rotating at its own speed; alternate direction.
  for (let i = 0; i < nRings; i++) {
    const k = (i + 1) / nRings;
    const rr = R * k;
    const dir = (i % 2 === 0) ? 1 : -1;
    const ang = t * spin * dir * (0.6 + 0.8 * k);

    noFill();
    stroke(...pal(0.10 * k, 0.6, 0.62), 0.25 + 0.35 * (1 - k));
    strokeWeight(1 + 1.2 * (1 - k));
    circle(0, 0, rr * 2);

    // A dashed arc drawn as short segments, so it visibly turns.
    stroke(...pal(0.10 * k + 0.12, 0.8, 0.72), 0.85);
    strokeWeight(2.2);
    const seg = 14;
    for (let s = 0; s < seg; s += 2) {
      const a0 = ang + s / seg * TAU;
      const a1 = ang + (s + 1) / seg * TAU;
      line(cos(a0) * rr, sin(a0) * rr, cos(a1) * rr, sin(a1) * rr);
    }

    // Satellites on this ring
    noStroke();
    for (let s = 0; s < nSats; s++) {
      const a = ang * 1.7 + s / nSats * TAU;
      const sx = cos(a) * rr, sy = sin(a) * rr;
      const bob = 0.5 + 0.5 * sin(t * 2 + s + i);
      fill(...pal(0.2 * k + 0.05 * s / Math.max(1, nSats), 0.75, 0.6 + 0.2 * bob), 0.9);
      circle(sx, sy, 3 + 5 * bob * (1 - k * 0.5));
    }
  }

  // Core: layered discs that breathe
  noStroke();
  for (let i = 5; i >= 0; i--) {
    const breath = 1 + 0.06 * sin(t * 1.5 + i);
    fill(...pal(0.02 * i, 0.7, 0.35 + 0.1 * (5 - i)), 0.18 + 0.08 * (5 - i));
    circle(0, 0, R * 0.10 * (i + 1) * breath);
  }
  pop();
}


// ---- 5. Beat pulse -----------------------------------------------------------
// Uses the transport's beat counter, so it follows tempo and stays locked when
// you scrub. `phase` is 0..1 inside each pulse cycle.
function drawPulse() {
  if (pulse <= 0) return;
  const cycle = Math.max(0.25, bpmFeel);
  const phase = (beat / cycle) - Math.floor(beat / cycle);
  const e = pulseCurve(phase);
  const R = Math.min(width, height) * orbit;
  const cx = width * 0.5, cy = height * 0.46;

  noFill();
  stroke(...pal(0.0, 0.5, 0.9), pulse * e * 0.9);
  strokeWeight(2 + 10 * e);
  circle(cx, cy, R * 2 * (1.05 + 0.35 * phase));

  // A second, fainter echo ring
  stroke(...pal(0.12, 0.6, 0.8), pulse * e * 0.35);
  strokeWeight(1 + 4 * e);
  circle(cx, cy, R * 2 * (1.05 + 0.65 * phase));
}


// ---- 6. Horizon --------------------------------------------------------------
function drawHorizon() {
  const baseY = height * 0.82;
  const H = height * mount;
  noStroke();

  // Two mountain layers; the back one is lighter (atmospheric depth).
  const layers = [
    { off: 0,   yScale: 1.0, l: 0.14, s: 0.35, freq: 3.2, seed: 5   },
    { off: 0.0, yScale: 0.7, l: 0.07, s: 0.30, freq: 5.0, seed: 17  },
  ];
  for (let li = 0; li < layers.length; li++) {
    const L = layers[li];
    fill(...hsl(hue + 0.02, L.s, L.l));
    beginShape();
    vertex(0, height);
    const steps = 64;
    for (let s = 0; s <= steps; s++) {
      const u = s / steps;
      const ridge = noise(u * L.freq + L.seed, 0.5, 0);          // static terrain
      vertex(u * width, baseY - ridge * H * L.yScale + li * H * 0.05);
    }
    vertex(width, height);
    endShape(CLOSE);
  }

  // A thin reflective water line just under the aurora's colour
  stroke(...pal(0.0, 0.6, 0.6), 0.25);
  strokeWeight(1);
  line(0, baseY + H * 0.05, width, baseY + H * 0.05);
}


// ---- 7. Title and readout ----------------------------------------------------
function drawTitle() {
  const word = "AURORA";
  textSize(titleSize);
  noStroke();
  const total = textWidth(word);
  let x = (width - total) / 2;
  const y0 = height * 0.18;
  for (let i = 0; i < word.length; i++) {
    const ch = word[i];
    const y = y0 + wave * sin(t * 1.6 + i * 0.8);
    // Soft shadow, then the letter
    fill(0, 0, 0, 0.35);
    text(ch, x + 3, y + 4);
    fill(...pal(0.04 * i, 0.55, 0.88), 0.95);
    text(ch, x, y);
    x += textWidth(ch);
  }

  // Small monospace-ish readout, bottom-left, so you can see time and beat move.
  textSize(Math.max(12, height * 0.018));
  fill(0.9, 0.95, 1.0, 0.7);
  text("t " + t.toFixed(2) + "s   beat " + beat.toFixed(2) + "   frame " + frame, 20, height - 18);
}


// ---- Main --------------------------------------------------------------------
function draw(t) {
  drawSky();
  drawStars();
  drawAurora();
  drawEngine();
  drawPulse();
  drawHorizon();
  drawTitle();
}
)JS"},
{"Jellyfish", R"JS(// Two jellyfish drifting in deep water. Bell pulse, trailing tentacles, marine snow.
param("pulse", 0.9, 0.2, 2.5);
param("tentacles", 14, 4, 28);
param("hue", 0.88, 0, 1);
param("glow", 0.7, 0, 1);
param("snow", 70, 0, 200);

function hash(n) { const s = Math.sin(n * 127.1 + 311.7) * 43758.5453; return s - Math.floor(s); }

function jelly(cx, cy, S, ph, h) {
  const beatPh = t * pulse + ph;
  const squeeze = 0.5 + 0.5 * sin(beatPh * TAU * 0.5);         // 0 relaxed, 1 contracted
  const rx = S * (1 - 0.16 * squeeze), ry = S * (0.78 + 0.18 * squeeze);
  const n = Math.floor(tentacles);
  push();
  translate(cx + sin(t * 0.3 + ph) * S * 0.25, cy + sin(beatPh * TAU * 0.5) * S * -0.12);
  rotate(sin(t * 0.25 + ph) * 0.08);

  // tentacles first so the bell sits on top
  noFill();
  for (let i = 0; i < n; i++) {
    const u = n === 1 ? 0 : i / (n - 1) * 2 - 1;                // -1..1 across the rim
    const x0 = u * rx * 0.92, y0 = ry * 0.1 * (1 - u * u);
    const len = S * (1.6 + 0.9 * hash(i + ph * 10));
    stroke(...hsl(h + 0.04 * u, 0.7, 0.72), glow * 0.55);
    strokeWeight(max(1, S * 0.022 * (1 - 0.4 * abs(u))));
    beginShape();
    for (let k = 0; k <= 22; k++) {
      const v = k / 22;
      const sway = sin(t * 1.6 - v * 5 + i * 0.9 + ph) * S * 0.22 * v * v
                 + sin(beatPh * TAU * 0.5 - v * 3) * S * 0.1 * v;
      vertex(x0 + sway + u * S * 0.12 * v, y0 + v * len);
    }
    endShape();
  }
  // oral arms: fewer, thicker, frilled
  for (let i = 0; i < 4; i++) {
    const x0 = (i - 1.5) * rx * 0.2;
    stroke(...hsl(h + 0.08, 0.5, 0.85), glow * 0.7);
    strokeWeight(S * 0.05);
    beginShape();
    for (let k = 0; k <= 16; k++) {
      const v = k / 16;
      vertex(x0 + sin(t * 2 - v * 6 + i * 1.7 + ph) * S * 0.12 * v, v * S * 1.1);
    }
    endShape();
  }

  // bell: translucent stacked layers, then a bright rim and spots
  noStroke();
  for (let l = 0; l < 6; l++) {
    const k = 1 - l * 0.12;
    fill(...hsl(h, 0.65, 0.35 + l * 0.06), 0.16 + 0.04 * l);
    beginShape();
    for (let a = 0; a <= 24; a++) {
      const th = PI * a / 24;
      vertex(-cos(th) * rx * k, -sin(th) * ry * k + ry * 0.12 * (1 - k));
    }
    endShape(CLOSE);
  }
  noFill();
  stroke(...hsl(h, 0.5, 0.88), 0.85 * glow + 0.15);
  strokeWeight(max(1.5, S * 0.03));
  beginShape();
  for (let a = 0; a <= 24; a++) {
    const th = PI * a / 24;
    vertex(-cos(th) * rx, -sin(th) * ry);
  }
  endShape();
  noStroke();
  for (let i = 0; i < 9; i++) {
    const a = PI * (0.12 + 0.76 * hash(i + ph));
    const r = 0.35 + 0.5 * hash(i * 3 + 1 + ph);
    fill(...hsl(h + 0.1, 0.4, 0.92), 0.5 * glow + 0.1);
    circle(-cos(a) * rx * r, -sin(a) * ry * r, S * 0.05 * (0.5 + hash(i + 9)));
  }
  pop();
}

function draw(t) {
  background(0.01, 0.02, 0.06);
  noStroke();
  for (let i = 0; i < 18; i++) {                                // depth gradient
    const k = i / 17;
    fill(0.02 + 0.03 * (1 - k), 0.05 + 0.10 * (1 - k), 0.14 + 0.18 * (1 - k), 0.7);
    rect(0, k * height, width, height / 18 + 3);
  }
  const R = min(width, height);
  const ns = Math.floor(snow);
  for (let i = 0; i < ns; i++) {                                // marine snow
    const x = ((hash(i * 3.1) + t * 0.004 * (1 + hash(i))) % 1) * width;
    const y = ((hash(i * 5.3) - t * (0.01 + 0.02 * hash(i + 4))) % 1 + 1) % 1 * height;
    fill(0.8, 0.95, 1, 0.12 + 0.2 * hash(i + 8));
    circle(x, y, 1 + 3 * hash(i + 2));
  }
  jelly(width * 0.68, height * 0.34, R * 0.10, 1.7, hue + 0.12);   // far, small
  jelly(width * 0.36, height * 0.40, R * 0.17, 0.0, hue);          // near, large
}
)JS"},
{"Sunset sea", R"JS(// A sun going down over layered water, with its reflection and a few birds.
param("sun", 0.35, 0, 1);          // 0 high noon-gold, 1 below the horizon
param("hue", 0.04, 0, 1);          // warm hue at the horizon
param("swell", 0.5, 0, 1.5);       // wave height
param("speed", 0.6, 0, 3);
param("birds", 5, 0, 12);

function hash(n) { const s = Math.sin(n * 127.1 + 311.7) * 43758.5453; return s - Math.floor(s); }

function draw(t) {
  const hy = height * 0.58;                                    // horizon line
  noStroke();
  // sky: warm at the horizon, deep blue-violet above
  const bands = 28;
  for (let i = 0; i < bands; i++) {
    const k = i / (bands - 1);                                 // 0 top .. 1 horizon
    const w = k * k;
    fill(hsl(hue - 0.30 * (1 - w), 0.55 + 0.25 * w, 0.16 + 0.5 * w * (1 - sun * 0.4)));
    rect(0, k * hy, width, hy / bands + 2);
  }
  // sun: sits between high and below horizon
  const sx = width * 0.5, sr = min(width, height) * 0.11;
  const sy = hy - sr * 1.3 + sun * sr * 2.6;
  for (let g = 6; g >= 1; g--) {
    fill(1, 0.75 - 0.05 * g, 0.4, 0.05 + 0.02 * (7 - g));
    circle(sx, sy, sr * 2 + g * sr * 0.7);
  }
  fill(1, 0.93, 0.7);
  circle(sx, sy, sr * 2);
  // water: dark teal gradient bands that fade the reflection
  for (let i = 0; i < 16; i++) {
    const k = i / 15;
    fill(hsl(0.54 - 0.04 * k, 0.5, 0.14 - 0.08 * k));
    rect(0, hy + k * (height - hy), width, (height - hy) / 16 + 2);
  }
  // reflection: horizontal dashes narrowing toward the viewer
  for (let i = 0; i < 40; i++) {
    const k = i / 39;
    const y = hy + 6 + k * k * (height - hy) * 0.95;
    const w = sr * (2.2 - 1.3 * k) * (0.5 + 0.5 * sin(t * speed * 2 + i * 1.9));
    const x = sx + sin(t * speed + i * 0.7) * sr * 0.8 * k;
    fill(1, 0.8, 0.45, 0.8 * (1 - k * 0.7) * (1 - sun * 0.5));
    rect(x - w / 2, y, w, 1.5 + 2.5 * k);
  }
  // wave layers front to back, each darker and bigger toward the viewer
  for (let l = 0; l < 4; l++) {
    const k = l / 3;
    const base = hy + (height - hy) * (0.08 + 0.3 * l * 0.9);
    const amp = (height - hy) * 0.03 * (1 + l) * swell;
    fill(hsl(0.56, 0.5, 0.22 - 0.045 * l), 0.55 + 0.1 * l);
    beginShape();
    vertex(0, height);
    for (let s = 0; s <= 48; s++) {
      const u = s / 48;
      vertex(u * width, base + sin(u * (5 + 3 * l) + t * speed * (1 + k)) * amp
                            + sin(u * 13 - t * speed * 1.3 + l) * amp * 0.35);
    }
    vertex(width, height);
    endShape(CLOSE);
  }
  // birds
  const nb = Math.floor(birds);
  noFill();
  strokeWeight(max(1.5, height * 0.004));
  for (let i = 0; i < nb; i++) {
    const x = ((hash(i * 7) + t * 0.03 * (0.5 + hash(i + 1))) % 1.2 - 0.1) * width;
    const y = hy * (0.25 + 0.5 * hash(i * 3 + 2)) + sin(t * 0.7 + i) * 8;
    const w = height * 0.022 * (0.6 + hash(i + 5));
    const flap = sin(t * 6 + i * 2) * w * 0.6;
    stroke(0.08, 0.05, 0.12, 0.85);
    beginShape();
    vertex(x - w, y - flap); vertex(x, y); vertex(x + w, y - flap);
    endShape();
  }
}
)JS"},
{"Mandala", R"JS(// A kaleidoscopic mandala. Rings of petals share one symmetry; a rose curve sits at the heart.
param("folds", 12, 3, 32);
param("rings", 6, 1, 10);
param("hue", 0.58, 0, 1);
param("spin", 0.12, -1, 1);
param("bloom", 0.5, 0, 1);       // petal width
param("rose", 5, 2, 12);         // lobes of the central rose curve

function petal(L, W) {
  beginShape();
  for (let i = 0; i <= 14; i++) { const u = i / 14; vertex(L * u, -W * sin(PI * pow(u, 0.7))); }
  for (let i = 14; i >= 0; i--) { const u = i / 14; vertex(L * u,  W * sin(PI * pow(u, 0.7))); }
  endShape(CLOSE);
}

function draw(t) {
  background(0.03, 0.03, 0.07);
  const R = min(width, height) * 0.47;
  const n = Math.floor(folds), nr = Math.floor(rings);
  push();
  translate(width / 2, height / 2);
  for (let r = nr; r >= 1; r--) {                         // big rings first, small on top
    const k = r / nr;
    const dir = r % 2 ? 1 : -1;
    const cnt = n * (r % 3 === 0 ? 2 : 1);
    push();
    rotate(t * spin * dir * (1.2 - k) + r * 0.2);
    for (let i = 0; i < cnt; i++) {
      push();
      rotate(i / cnt * TAU);
      translate(R * k * 0.28, 0);
      const L = R * k * 0.62 * (1 + 0.08 * sin(t * 1.4 + i + r));
      const W = L * (0.12 + 0.30 * bloom) * (0.85 + 0.15 * sin(t + i * 0.5));
      noStroke();
      fill(hsl(hue + 0.09 * k + 0.02 * r, 0.65, 0.28 + 0.38 * (1 - k)), 0.45);
      petal(L, W);
      noFill();
      stroke(hsl(hue + 0.12 * k, 0.7, 0.8), 0.55);
      strokeWeight(1.2);
      petal(L, W);
      pop();
    }
    pop();
  }
  // rose curve r = cos(k * theta), drawn with a travelling highlight
  noFill();
  const ro = R * 0.24;
  for (let pass = 0; pass < 2; pass++) {
    stroke(hsl(hue + 0.3 + pass * 0.05, 0.8, 0.8), pass ? 0.95 : 0.35);
    strokeWeight(pass ? 1.6 : 5);
    beginShape();
    for (let i = 0; i <= 240; i++) {
      const a = i / 240 * TAU * 2;
      const rr = ro * cos(rose * 0.5 * a + t * 0.4);
      vertex(cos(a) * rr, sin(a) * rr);
    }
    endShape();
  }
  noStroke();
  fill(1, 1, 1, 0.9);
  circle(0, 0, R * 0.03);
  pop();
}
)JS"},
{"Beat words", R"JS(// Kinetic typography locked to the beat. Each word slams in letter by letter, holds, then leaves.
param("perWord", 2, 1, 8);       // beats each word stays
param("stagger", 0.07, 0, 0.25); // delay between letters (fraction of the word's time)
param("bounce", 1, 0, 2);
param("hue", 0.02, 0, 1);
param("fit", 0.82, 0.3, 1);      // widest a word may be, fraction of the frame

const words = ["MAKE", "SOME", "NOISE", "TOGETHER"];

function outBack(x) { const c1 = 1.70158 * 1.2, c3 = c1 + 1; return 1 + c3 * pow(x - 1, 3) + c1 * pow(x - 1, 2); }
function inCubic(x) { return x * x * x; }

function draw(t) {
  const idx = Math.floor(beat / perWord);
  const w = words[((idx % words.length) + words.length) % words.length];
  const p = (beat / perWord) - Math.floor(beat / perWord);        // 0..1 through the word
  const bg = idx % 2 === 0;
  background(...hsl(hue + 0.13 * (idx % 5), 0.75, bg ? 0.52 : 0.1));
  const ink = bg ? [0.05, 0.05, 0.08] : hsl(hue + 0.13 * (idx % 5), 0.8, 0.7);

  // fit the word to the frame
  textSize(100);
  const s = 100 * min(height * 0.35 / 100 * 1.7, fit * width / max(1, textWidth(w)) * 1.0);
  textSize(s);
  const total = textWidth(w);
  let x = (width - total) / 2;
  const base = height * 0.56;
  const n = w.length;
  noStroke();
  for (let i = 0; i < n; i++) {
    const d = i * stagger;
    const tin = constrain((p - d) / 0.25, 0, 1);                  // letter enters over 25% of the word
    const tout = constrain((p - 0.78 - d * 0.5) / 0.22, 0, 1);    // and leaves at the end
    const rise = (1 - (bounce > 0 ? lerp(outBack(tin), tin, 1 - bounce * 0.5) : tin)) * height * 0.45;
    const drop = inCubic(tout) * height * 0.6;
    const sc = 1 + 0.12 * sin(tin * PI) * bounce;
    const dir = (i % 2) ? 1 : -1;
    push();
    translate(x + textWidth(w[i]) / 2, base + rise * dir - drop * dir);
    scale(sc, sc);
    rotate((1 - tin) * 0.35 * dir * bounce + tout * 0.3 * dir);
    fill(...ink, 1 - tout);
    text(w[i], -textWidth(w[i]) / 2, 0);
    pop();
    x += textWidth(w[i]);
  }
  // beat bar and counter
  fill(...ink, 0.9);
  rect(width * 0.1, height * 0.86, width * 0.8 * p, height * 0.012);
  fill(...ink, 0.25);
  rect(width * 0.1 + width * 0.8 * p, height * 0.86, width * 0.8 * (1 - p), height * 0.012);
  textSize(height * 0.035);
  fill(...ink, 0.8);
  text((idx + 1) + " / beat " + (Math.floor(beat) % 4 + 1), width * 0.1, height * 0.82);
}
)JS"},
{"Type tunnel", R"JS(// A word rushing at you. Copies of it grow from the vanishing point and fade as they pass.
param("speed", 0.18, 0, 1);
param("copies", 9, 4, 30);
param("hue", 0.6, 0, 1);
param("twist", 0.05, -1, 1);     // how much the tunnel turns with depth
param("reach", 3.0, 1, 6);       // how big the nearest copy gets (log scale)

const word = "INFINITE";

function draw(t) {
  background(0.02, 0.02, 0.05);
  const n = Math.floor(copies);
  const base = min(width, height) * 0.05;
  noStroke();
  for (let i = 0; i < n; i++) {
    const z = ((t * speed + i / n) % 1 + 1) % 1;             // 0 far .. 1 near
    const size = base * Math.exp(z * reach);
    const a = min(1, z * 8) * pow(1 - z, 1.4);               // fade in at the far end, out as it nears
    push();
    translate(width / 2, height / 2);
    rotate((z - 0.5) * twist * TAU + sin(t * 0.3) * 0.05);
    textSize(size);
    const w = textWidth(word);
    fill(...hsl(hue + 0.25 * z, 0.7, 0.45 + 0.3 * z), a);
    text(word, -w / 2, size * 0.35);
    pop();
  }
  // the vanishing point: a quiet glow
  for (let g = 0; g < 5; g++) {
    fill(...hsl(hue, 0.6, 0.7), 0.05);
    circle(width / 2, height / 2, base * (1 + g * 2));
  }
}
)JS"},
{"Murmuration", R"JS(// A starling murmuration: hundreds of dashes follow one morphing cloud across a dusk sky.
param("birds", 520, 50, 900);
param("cloud", 0.45, 0.1, 0.8);      // size of the flock, fraction of the short side
param("morph", 0.5, 0, 2);           // how quickly the shape changes
param("travel", 0.35, 0, 1);         // how far the flock roams
param("dusk", 0.08, 0, 1);           // sky hue

function hash(n) { const s = Math.sin(n * 127.1 + 311.7) * 43758.5453; return s - Math.floor(s); }

function pos(i, T, R, cx, cy) {
  const u = hash(i * 1.7), v = hash(i * 2.9 + 4), w = hash(i * 4.1 + 8);
  const a = u * TAU + T * (0.3 + 0.5 * v) * morph;
  const stretch = 1 + 0.9 * noise(T * 0.25 * morph, 3.1, 0);          // the cloud elongates and relaxes
  const squash = 0.35 + 0.65 * noise(T * 0.2 * morph, 9.7, 1);
  const r = R * sqrt(w) * (0.4 + 0.6 * noise(u * 5 + T * 0.2 * morph, v * 5, T * 0.15));
  const ang = T * 0.12 * morph + noise(T * 0.1, 5, 2) * 2;
  const lx = cos(a) * r * stretch, ly = sin(a) * r * squash;
  return [cx + lx * cos(ang) - ly * sin(ang), cy + lx * sin(ang) + ly * cos(ang)];
}

function draw(t) {
  noStroke();
  for (let i = 0; i < 24; i++) {                                 // dusk sky
    const k = i / 23;
    fill(hsl(dusk - 0.30 * (1 - k * k), 0.5 + 0.2 * k, 0.16 + 0.52 * k * k));
    rect(0, k * height, width, height / 24 + 3);
  }
  const R = min(width, height) * cloud;
  const cx = width * (0.5 + 0.22 * travel * sin(t * 0.17));
  const cy = height * (0.42 + 0.12 * travel * sin(t * 0.23 + 1));
  const n = Math.floor(birds);
  strokeWeight(max(1.2, height * 0.0035));
  for (let i = 0; i < n; i++) {
    const p0 = pos(i, t, R, cx, cy);
    const p1 = pos(i, t + 0.06, R, cx, cy);                      // heading from a small step ahead
    let dx = p1[0] - p0[0], dy = p1[1] - p0[1];
    const m = max(0.001, sqrt(dx * dx + dy * dy));
    const len = min(20, 5 + m * 1.2) * (height / 720);
    dx /= m; dy /= m;
    const depth = hash(i * 6.3);
    stroke(0.04, 0.03, 0.08, 0.45 + 0.5 * depth);
    line(p0[0] - dx * len, p0[1] - dy * len, p0[0], p0[1]);
  }
}
)JS"},
{"SVG animate", R"JS(// Animate any SVG by id. Drop your own .svg on the canvas, then
// drive its attributes: svgSet("#id", "attr", value). svgDraw() paints it
// fitted into the frame, and svgBox("#id") gives an element's bounds.
param("spin", 0.5, -3, 3);
param("hue", 0.0, 0, 1);
param("pulse", 6, 0, 20);

function hex(h) {
  const c = hsl(h % 1, 0.7, 0.6);
  return "#" + c.map(v => Math.round(v * 255).toString(16).padStart(2, "0")).join("");
}

function draw(t) {
  background(0.07);
  for (let i = 0; i < 6; i++) svgSet("#p" + i, "fill", hex(hue + i / 6));
  svgSet("#wheel", "transform", "rotate(" + (t * spin * 60) + " 100 100)");
  svgSet("#core", "r", 16 + pulse * sin(t * 3));
  const m = min(width, height) * 0.9;
  svgDraw((width - m) / 2, (height - m) / 2, m, m);
}
)JS", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="200" height="212" viewBox="0 0 200 212">
<g id="wheel">
<ellipse id="p0" cx="100" cy="48" rx="14" ry="34" transform="rotate(0 100 100)"/>
<ellipse id="p1" cx="100" cy="48" rx="14" ry="34" transform="rotate(60 100 100)"/>
<ellipse id="p2" cx="100" cy="48" rx="14" ry="34" transform="rotate(120 100 100)"/>
<ellipse id="p3" cx="100" cy="48" rx="14" ry="34" transform="rotate(180 100 100)"/>
<ellipse id="p4" cx="100" cy="48" rx="14" ry="34" transform="rotate(240 100 100)"/>
<ellipse id="p5" cx="100" cy="48" rx="14" ry="34" transform="rotate(300 100 100)"/>
</g>
<circle id="core" cx="100" cy="100" r="16" fill="#ffffff"/>
<text id="word" x="100" y="206" text-anchor="middle" font-size="14" fill="#ffffff">vector</text>
</svg>)SVG"},
   };
   n = (int)(sizeof(k) / sizeof(k[0]));
   return k;
}
} // namespace SketchPresets
