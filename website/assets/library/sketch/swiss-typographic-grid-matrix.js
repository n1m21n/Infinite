// Swiss Typographic Grid Matrix - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Rigid modernist modular grid composition featuring mathematical column divisions, asymmetrical vermilion accent bars, fine registration crosshairs, and dynamic ratio blocks.
param("grid_cols", 6.0, 3.0, 12.0);
param("accent_ratio", 0.38, 0.1, 0.8);
param("rule_weight", 2.4, 1.0, 6.0);
param("time_drift", 0.35, 0.1, 1.2);

function draw(t) {
  // Archival Swiss cream paper (#F5F5F2)
  background(0.961, 0.961, 0.949);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.86;
  const time = t * time_drift;

  push();
  translate(cx, cy);

  // 1. Outer Modular Framing & Precision Bleed Ticks
  stroke(0.067, 0.067, 0.067); // Jet ink black
  strokeWeight(rule_weight);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  // Precision corner crop marks
  strokeWeight(1.4);
  const markLen = span * 0.035;
  const corners = [
    [-span * 0.48, -span * 0.48], [span * 0.48, -span * 0.48],
    [-span * 0.48, span * 0.48], [span * 0.48, span * 0.48]
  ];
  for (let c of corners) {
    const sx = c[0] < 0 ? -1 : 1;
    const sy = c[1] < 0 ? -1 : 1;
    line(c[0] + sx * markLen * 0.5, c[1], c[0] - sx * markLen * 0.8, c[1]);
    line(c[0], c[1] + sy * markLen * 0.5, c[0], c[1] - sy * markLen * 0.8);
  }

  // 2. Strict Mathematical Swiss Grid Columns & Rules
  const cols = floor(grid_cols);
  const colW = (span * 0.88) / cols;
  const topY = -span * 0.42;
  const botY = span * 0.42;
  const gridH = botY - topY;

  stroke(0.067, 0.067, 0.067, 0.35);
  strokeWeight(1.0);
  for (let i = 0; i <= cols; i++) {
    const x = -span * 0.44 + i * colW;
    line(x, topY, x, botY);
  }

  // Horizontal metric bands
  const bands = 8;
  const bandH = gridH / bands;
  for (let j = 0; j <= bands; j++) {
    const y = topY + j * bandH;
    line(-span * 0.44, y, span * 0.44, y);
  }

  // 3. Bold Asymmetric International Vermilion Red Geometric Accents (#E4261A)
  noStroke();
  fill(0.894, 0.149, 0.102); // International Vermilion

  // Focal vermilion counterpoint disc
  const discX = -span * 0.44 + colW * (1.5 + sin(time * 1.5) * 0.25);
  const discY = topY + bandH * 2.5;
  const discR = colW * 1.25;
  circle(discX, discY, discR);

  // Vermilion horizontal anchor bar
  rect(-span * 0.44 + colW * 2.0, topY + bandH * 4.0, colW * 3.5, bandH * 0.35);

  // 4. Heavy Carbon Typography Blocks (Simulated Akzidenz-Grotesk geometric slabs)
  fill(0.067, 0.067, 0.067);

  // Hero title weight block
  rect(-span * 0.44 + colW * 0.2, topY + bandH * 0.3, colW * (cols * 0.55), bandH * 0.65);

  // Subtitle ratio bars
  for (let k = 0; k < 5; k++) {
    const yk = topY + bandH * (1.2 + k * 0.22);
    const wk = colW * (2.8 - (k % 3) * 0.5);
    rect(-span * 0.44 + colW * 0.2, yk, wk, bandH * 0.08);
  }

  // Lower grid asymmetric blocks
  rect(-span * 0.44 + colW * 3.0, topY + bandH * 5.0, colW * 2.8, bandH * 1.8);

  // Secondary text matrix
  fill(0.961, 0.961, 0.949); // Knockout white inside lower block
  for (let m = 0; m < 4; m++) {
    rect(-span * 0.44 + colW * 3.2, topY + bandH * (5.3 + m * 0.35), colW * 2.4, bandH * 0.10);
  }

  // 5. Technical Metric Scale / Tick Marks along baseline
  stroke(0.067, 0.067, 0.067);
  strokeWeight(1.2);
  const nTicks = 36;
  const tickStep = (span * 0.88) / nTicks;
  for (let tIdx = 0; tIdx <= nTicks; tIdx++) {
    const tx = -span * 0.44 + tIdx * tickStep;
    const tLen = (tIdx % 5 === 0) ? span * 0.024 : span * 0.012;
    line(tx, botY + 4.0, tx, botY + 4.0 + tLen);
  }

  pop();
}
