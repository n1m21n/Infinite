// Swiss Typographic Modular Grid - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Asymmetric International Typographic Style layout with multi-column modular grid proportions, bold red architectural numerals, and hairline alignment rules.
param("zoom", 1.0, 0.5, 2.0);
param("line_weight", 2.0, 1.0, 6.0);
param("accent_scale", 1.0, 0.4, 2.0);
param("grid_cols", 6.0, 3.0, 12.0);

function draw(t) {
  // Pure warm Swiss cream ground
  background(0.961, 0.961, 0.949);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88 * zoom;
  const left = cx - span * 0.5;
  const top = cy - span * 0.5;

  push();
  translate(left, top);

  // Outer framing boundary
  noFill();
  stroke(0.067, 0.067, 0.067);
  strokeWeight(line_weight);
  rect(0, 0, span, span);

  const cols = floor(grid_cols);
  const colW = span / cols;
  const rows = 8;
  const rowH = span / rows;

  // Hairline subtle grid matrix
  stroke(0.78, 0.78, 0.76);
  strokeWeight(1.0);
  for (let c = 1; c < cols; c++) {
    line(c * colW, 0, c * colW, span);
  }
  for (let r = 1; r < rows; r++) {
    line(0, r * rowH, span, r * rowH);
  }

  // Major architectural International Signal Red block (asymmetric golden ratio placement)
  noStroke();
  fill(0.894, 0.149, 0.102); // International Red
  const redX = colW * 2.0;
  const redY = rowH * 1.0;
  const redW = colW * 3.0 * accent_scale;
  const redH = rowH * 2.5 * accent_scale;
  rect(redX, redY, redW, redH);

  // Jet black structural accent block (aligned on lower grid intersection)
  fill(0.067, 0.067, 0.067);
  rect(colW * 0.0, rowH * 4.5, colW * 2.0, rowH * 2.5);

  // Signal red counter-form circle in bottom-right quadrant
  fill(0.894, 0.149, 0.102);
  const circR = colW * 0.9 * accent_scale;
  circle(colW * 4.5, rowH * 5.5, circR);

  // Concentric hairline registration ring
  noFill();
  stroke(0.067, 0.067, 0.067);
  strokeWeight(line_weight);
  circle(colW * 4.5, rowH * 5.5, circR * 1.5);
  line(colW * 4.5 - circR * 1.8, rowH * 5.5, colW * 4.5 + circR * 1.8, rowH * 5.5);
  line(colW * 4.5, rowH * 5.5 - circR * 1.8, colW * 4.5, rowH * 5.5 + circR * 1.8);

  // Heavy black Swiss typographic bar numerals and rules
  stroke(0.067, 0.067, 0.067);
  strokeWeight(line_weight + 3.0);
  line(colW * 0.5, rowH * 0.5, colW * 1.5, rowH * 0.5);
  line(colW * 0.5, rowH * 0.5, colW * 0.5, rowH * 1.8);

  // Horizontal high-contrast typographic bands
  strokeWeight(line_weight * 1.5);
  for (let k = 0; k < 5; k++) {
    const yk = rowH * 7.2 + k * (rowH * 0.12);
    line(colW * 0.5, yk, colW * 3.5, yk);
  }

  // Vertical alignment tick marks along top header
  strokeWeight(1.5);
  for (let i = 0; i < cols * 4; i++) {
    const tx = (i / (cols * 4)) * span;
    const th = (i % 4 === 0) ? 14 : 6;
    line(tx, 0, tx, th);
  }

  pop();
}
