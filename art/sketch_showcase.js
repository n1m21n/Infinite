// =============================================================================
//  AURORA ENGINE - a detailed Sketch showcase for Infinite
// =============================================================================
//  Paste this whole file into a Sketch node's editor.
//
//  What it draws (back to front):
//    1. Night-sky gradient built from stacked bands
//    2. Twinkling starfield (deterministic, no flicker when you scrub)
//    3. Aurora ribbons: noise-driven curves in three colour layers
//    4. A rotating orbital "engine": concentric rings, satellites, spokes
//    5. A beat pulse ring that fires on every beat of the transport
//    6. A rolling mountain horizon with a reflection line
//    7. Kinetic title that rides a wave, plus a small readout
//
//  Every param() below becomes a real knob on the node: cable an LFO, a macro
//  or MIDI CC to any of them. (MIDI Out can send those same values to hardware.)
//
//  Rules this sketch follows, so it exports identically every time:
//    - No state is carried between frames: the picture is a pure function of
//      (t, beat, params, width, height).
//    - Randomness is hashed from an index (hash1), never random(), so scrubbing
//      the timeline gives the same stars every time.
//    - One draw() stays well under the 50 ms limit (about 3000 shapes).
//
//  Globals available: width, height, t (seconds), beat, frame.
//  Colours are 0..1. hsl(h, s, l) returns [r, g, b]; spread it to add alpha:
//      fill(...hsl(0.6, 0.7, 0.5), 0.4)
// =============================================================================


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
