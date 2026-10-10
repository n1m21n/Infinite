// =============================================================================
//  BLOOM - a detailed flower for Infinite's Sketch node
// =============================================================================
//  Paste this whole file into a Sketch node's editor.
//
//  Back to front:
//    1. Soft sky gradient with drifting pollen motes
//    2. A swaying stem with two leaves (each leaf has a midrib and side veins)
//    3. The flower head: 3 petal layers (back, middle, front), each petal drawn
//       as a filled shape with a centre crease and a lighter tip
//    4. The seed head: a phyllotaxis spiral (golden angle) with a glowing ring
//    5. Stamen dots round the centre
//
//  "open" is the big one: 0 is a closed bud, 1 is fully open. Cable an LFO or a
//  macro to it and the flower breathes. "breeze" sways everything.
//
//  Like every sketch here it is a pure function of (t, beat, params, size):
//  no random(), no state carried between frames, so export matches the canvas.
//  Colours are 0..1; hsl(h, s, l) returns [r, g, b]; spread it to add alpha:
//      fill(...hsl(0.9, 0.7, 0.6), 0.5)
// =============================================================================


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
