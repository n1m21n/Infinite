// Thundr Brutalist Hazard Crosshair - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Postmodern brutalist technical poster with high-contrast diagonal hazard stripes, bold black monolithic slabs, neon warning accents, and precision reticle crosshairs.
param("zoom", 1.0, 0.5, 2.0);
param("line_weight", 2.2, 1.0, 6.0);
param("stripe_count", 12.0, 6.0, 24.0);
param("slab_width", 1.0, 0.6, 1.8);

function draw(t) {
  background(0.07, 0.07, 0.08);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88 * zoom;

  push();
  translate(cx, cy);

  // Outer framing boundary
  noFill();
  stroke(1.0, 0.8, 0.0); // Hazard yellow
  strokeWeight(line_weight * 1.5);
  rect(-span * 0.5, -span * 0.5, span, span);

  // Inner margin outline
  stroke(0.28, 0.28, 0.32);
  strokeWeight(1.0);
  rect(-span * 0.47, -span * 0.47, span * 0.94, span * 0.94);

  // Top heavy black monolithic header slab
  fill(0.12, 0.12, 0.14);
  noStroke();
  rect(-span * 0.45, -span * 0.45, span * 0.9, span * 0.18);

  // Signal red accent blocks
  fill(1.0, 0.0, 0.2); // Signal Red
  rect(-span * 0.45, -span * 0.45, span * 0.06, span * 0.18);
  rect(-span * 0.36, -span * 0.42, span * 0.38 * slab_width, span * 0.04);
  rect(-span * 0.36, -span * 0.35, span * 0.55 * slab_width, span * 0.05);

  // High-contrast diagonal hazard stripe block
  push();
  translate(0, span * 0.04);
  const hW = span * 0.85;
  const hH = span * 0.22;
  fill(0.04, 0.04, 0.05);
  rect(-hW * 0.5, -hH * 0.5, hW, hH);

  // Diagonal stripes in hazard yellow
  const count = floor(stripe_count);
  const step = hW / count;
  for (let i = -count; i <= count * 2; i++) {
    const x0 = -hW * 0.5 + i * step;
    fill(1.0, 0.8, 0.0);
    noStroke();
    beginShape();
    vertex(max(-hW * 0.5, min(hW * 0.5, x0)), -hH * 0.5);
    vertex(max(-hW * 0.5, min(hW * 0.5, x0 + step * 0.5)), -hH * 0.5);
    vertex(max(-hW * 0.5, min(hW * 0.5, x0 + step * 0.5 - hH)), hH * 0.5);
    vertex(max(-hW * 0.5, min(hW * 0.5, x0 - hH)), hH * 0.5);
    endShape();
  }

  // Border around hazard block
  noFill();
  stroke(1.0, 0.8, 0.0);
  strokeWeight(line_weight);
  rect(-hW * 0.5, -hH * 0.5, hW, hH);
  pop();

  // Central precision reticle crosshair target
  const rOut = span * 0.14;
  noFill();
  stroke(0.95, 0.95, 0.98); // Stark white
  strokeWeight(line_weight);
  circle(0, span * 0.04, rOut * 2.0);
  circle(0, span * 0.04, rOut * 0.8);

  fill(1.0, 0.0, 0.2);
  circle(0, span * 0.04, rOut * 0.25);

  stroke(1.0, 0.8, 0.0);
  line(-rOut * 1.5, span * 0.04, rOut * 1.5, span * 0.04);
  line(0, span * 0.04 - rOut * 1.5, 0, span * 0.04 + rOut * 1.5);

  // Bottom constructivist rectangular data panels
  fill(0.92, 0.92, 0.95);
  noStroke();
  rect(-span * 0.45, span * 0.24, span * 0.42 * slab_width, span * 0.18);

  fill(0.12, 0.12, 0.14);
  rect(-span * 0.42, span * 0.27, span * 0.36 * slab_width, span * 0.05);
  rect(-span * 0.42, span * 0.34, span * 0.24 * slab_width, span * 0.04);

  fill(1.0, 0.8, 0.0);
  rect(span * 0.02, span * 0.24, span * 0.43, span * 0.18);

  fill(0.08, 0.08, 0.09);
  rect(span * 0.06, span * 0.27, span * 0.35, span * 0.12);

  // Technical edge measurement calibration ticks
  stroke(0.95, 0.95, 0.98);
  strokeWeight(1.5);
  for (let d = -span * 0.45; d <= span * 0.45; d += span * 0.05) {
    line(d, -span * 0.5, d, -span * 0.5 + 10);
    line(d, span * 0.5 - 10, d, span * 0.5);
    line(-span * 0.5, d, -span * 0.5 + 10, d);
    line(span * 0.5 - 10, d, span * 0.5, d);
  }

  pop();
}
