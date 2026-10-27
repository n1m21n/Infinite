// Swiss Typographic Geometric Axis - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Constructive Swiss modernism with dynamic 45-degree diagonal coordinate axes, cantilevered rectangular color blocks, and typographic signal hierarchy.
param("zoom", 1.0, 0.5, 2.0);
param("axis_angle", 0.785398, 0.0, 1.570796);
param("line_weight", 2.5, 1.0, 7.0);
param("accent_size", 1.1, 0.5, 2.0);

function draw(t) {
  // Pure warm Swiss cream ground
  background(0.961, 0.961, 0.949);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.86 * zoom;

  push();
  translate(cx, cy);

  // Outer framing boundary
  noFill();
  stroke(0.067, 0.067, 0.067);
  strokeWeight(line_weight);
  rect(-span * 0.5, -span * 0.5, span, span);

  // Subtle coordinate grid background lines
  stroke(0.82, 0.82, 0.80);
  strokeWeight(1.0);
  const step = span / 8.0;
  for (let k = -span * 0.5; k <= span * 0.5; k += step) {
    line(k, -span * 0.5, k, span * 0.5);
    line(-span * 0.5, k, span * 0.5, k);
  }

  // Major 45-degree constructive diagonal axis
  push();
  rotate(axis_angle);

  // International Signal Red rectangular monolith on primary axis
  noStroke();
  fill(0.894, 0.149, 0.102);
  rect(-span * 0.28, -span * 0.08, span * 0.56 * accent_size, span * 0.16 * accent_size);

  // Deep jet black crossing counter-block
  fill(0.067, 0.067, 0.067);
  rect(-span * 0.12, -span * 0.35, span * 0.24, span * 0.7);

  // Crisp white inner cutout circle
  fill(0.961, 0.961, 0.949);
  circle(0, 0, span * 0.14);

  // Red concentric point
  fill(0.894, 0.149, 0.102);
  circle(0, 0, span * 0.05);

  // Long constructive hairline projection axes
  stroke(0.067, 0.067, 0.067);
  strokeWeight(line_weight);
  line(-span * 0.7, 0, span * 0.7, 0);
  line(0, -span * 0.7, 0, span * 0.7);

  // Axis measurement ticks
  strokeWeight(1.5);
  for (let d = -span * 0.6; d <= span * 0.6; d += span * 0.04) {
    line(d, -5, d, 5);
  }
  pop();

  // Horizontal asymmetric baseline alignment bar at bottom
  noStroke();
  fill(0.067, 0.067, 0.067);
  rect(-span * 0.45, span * 0.36, span * 0.9, span * 0.04);

  // Red indicator square on the baseline
  fill(0.894, 0.149, 0.102);
  rect(-span * 0.45, span * 0.31, span * 0.08, span * 0.04);

  pop();
}
