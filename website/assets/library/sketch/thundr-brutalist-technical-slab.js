// Thundr Brutalist Technical Slab - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Asymmetric constructivist techwear layout with segmented monolithic bars, stark warning red callouts, dense coordinate matrix rules, and calibrated technical markings.
param("zoom", 1.0, 0.5, 2.0);
param("line_weight", 2.2, 1.0, 6.0);
param("grid_cols", 10.0, 4.0, 20.0);
param("slab_offset", 0.15, 0.0, 0.4);

function draw(t) {
  background(0.065, 0.065, 0.075);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88 * zoom;

  push();
  translate(cx, cy);

  // Outer framing boundary
  noFill();
  stroke(0.92, 0.92, 0.95); // White frame
  strokeWeight(line_weight * 1.5);
  rect(-span * 0.5, -span * 0.5, span, span);

  // Background coordinate grid lines
  stroke(0.22, 0.22, 0.26);
  strokeWeight(1.0);
  const cols = floor(grid_cols);
  const step = span / cols;
  for (let i = -span * 0.5; i <= span * 0.5; i += step) {
    line(i, -span * 0.5, i, span * 0.5);
    line(-span * 0.5, i, span * 0.5, i);
  }

  // Heavy left monolithic black slab with warning yellow stripe
  fill(0.14, 0.14, 0.16);
  noStroke();
  rect(-span * 0.46, -span * 0.44, span * 0.32, span * 0.88);

  fill(1.0, 0.8, 0.0); // Hazard Yellow
  rect(-span * 0.46, -span * 0.44, span * 0.04, span * 0.88);

  // White data segments inside left slab
  fill(0.95, 0.95, 0.98);
  for (let k = 0; k < 6; k++) {
    const yk = -span * 0.36 + k * span * 0.12;
    rect(-span * 0.39, yk, span * 0.22, span * 0.035);
  }

  // Dominant cantilevered center slab in signal red
  fill(1.0, 0.0, 0.2); // Signal Red
  rect(-span * 0.10, -span * 0.32 + slab_offset * span, span * 0.54, span * 0.26);

  fill(0.07, 0.07, 0.08);
  rect(-span * 0.06, -span * 0.28 + slab_offset * span, span * 0.46, span * 0.08);

  fill(0.95, 0.95, 0.98);
  rect(-span * 0.06, -span * 0.16 + slab_offset * span, span * 0.30, span * 0.04);
  rect(span * 0.28, -span * 0.16 + slab_offset * span, span * 0.12, span * 0.04);

  // Bottom right technical grid box with diagonal cross-hatch
  push();
  translate(span * 0.17, span * 0.18);
  const bW = span * 0.27;
  const bH = span * 0.24;

  fill(0.10, 0.10, 0.12);
  stroke(1.0, 0.8, 0.0);
  strokeWeight(line_weight);
  rect(-bW * 0.5, -bH * 0.5, bW, bH);

  stroke(1.0, 0.8, 0.0, 0.7);
  strokeWeight(1.2);
  for (let x = -bW * 0.5; x <= bW * 0.5; x += 14) {
    line(x, -bH * 0.5, x + 16, bH * 0.5);
  }
  pop();

  // Registration crosshairs & corner brackets
  stroke(0.95, 0.95, 0.98);
  strokeWeight(line_weight);
  noFill();

  // Corner marks
  const cOff = span * 0.46;
  const cLen = 16;
  line(-cOff, -cOff, -cOff + cLen, -cOff);
  line(-cOff, -cOff, -cOff, -cOff + cLen);
  line(cOff, -cOff, cOff - cLen, -cOff);
  line(cOff, -cOff, cOff, -cOff + cLen);
  line(-cOff, cOff, -cOff + cLen, cOff);
  line(-cOff, cOff, -cOff, cOff - cLen);
  line(cOff, cOff, cOff - cLen, cOff);
  line(cOff, cOff, cOff, cOff - cLen);

  pop();
}
