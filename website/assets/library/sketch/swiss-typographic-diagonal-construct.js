// Swiss Typographic Diagonal Construct - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Dynamic 45-degree diagonal typographical construct with intersecting carbon rules, vermilion focal arc geometry, micro-metric scale divisions, and strict asymmetric mathematical balance.
param("angle", 45.0, 15.0, 75.0);
param("bar_count", 5.0, 2.0, 10.0);
param("vermilion_scale", 0.42, 0.2, 0.8);
param("speed", 0.30, 0.1, 1.0);

function draw(t) {
  // Archival Swiss cream paper (#F5F5F2)
  background(0.961, 0.961, 0.949);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.86;
  const time = t * speed;

  push();
  translate(cx, cy);

  // 1. Outer Minimal Enclosure Frame
  stroke(0.067, 0.067, 0.067);
  strokeWeight(2.8);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  // Registration crosshairs
  strokeWeight(1.4);
  const crossLen = span * 0.025;
  line(0, -span * 0.48 - crossLen, 0, -span * 0.48 + crossLen);
  line(-crossLen, -span * 0.48, crossLen, -span * 0.48);
  line(0, span * 0.48 - crossLen, 0, span * 0.48 + crossLen);
  line(-crossLen, span * 0.48, crossLen, span * 0.48);

  // 2. Dynamic 45-degree Angled Composition Construct
  push();
  const rad = (angle * Math.PI) / 180.0;
  rotate(rad);

  // Heavy diagonal Carbon rules
  stroke(0.067, 0.067, 0.067);
  strokeWeight(4.5);
  line(-span * 0.65, 0, span * 0.65, 0);

  strokeWeight(1.8);
  line(-span * 0.65, -span * 0.12, span * 0.65, -span * 0.12);
  line(-span * 0.65, span * 0.12, span * 0.65, span * 0.12);

  // Vermilion Red Dynamic Wedge / Arc (#E4261A)
  noStroke();
  fill(0.894, 0.149, 0.102);
  rect(-span * 0.25, -span * 0.28, span * 0.50, span * 0.14);

  // Vermilion focal circle
  circle(span * 0.22, span * 0.12, span * 0.24 * vermilion_scale * 2.0);

  // Sequential rhythmic carbon bars
  fill(0.067, 0.067, 0.067);
  const nBars = floor(bar_count);
  const bW = span * 0.065;
  for (let b = 0; b < nBars; b++) {
    const bx = -span * 0.35 + b * (span * 0.16);
    const by = span * 0.18;
    const bH = span * (0.15 + (b % 3) * 0.12);
    rect(bx, by, bW, bH);
  }
  pop();

  // 3. Static Orthogonal Baseline Grid Overprint
  stroke(0.067, 0.067, 0.067, 0.4);
  strokeWeight(1.2);
  for (let gy = -span * 0.42; gy <= span * 0.42; gy += span * 0.14) {
    line(-span * 0.44, gy, span * 0.44, gy);
  }

  // 4. Technical Index Numerals / Ratios (Modernist blocks)
  fill(0.067, 0.067, 0.067);
  noStroke();
  // Large index marker "01" / "SWISS"
  rect(-span * 0.44, span * 0.34, span * 0.16, span * 0.08);
  rect(-span * 0.24, span * 0.34, span * 0.32, span * 0.02);
  rect(-span * 0.24, span * 0.38, span * 0.24, span * 0.02);

  // Micro fine ticks along vertical margin
  stroke(0.067, 0.067, 0.067);
  strokeWeight(1.2);
  for (let my = -span * 0.44; my <= span * 0.44; my += span * 0.022) {
    line(-span * 0.48, my, -span * 0.46, my);
  }

  pop();
}
