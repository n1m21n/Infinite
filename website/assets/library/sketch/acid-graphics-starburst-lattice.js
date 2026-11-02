// Acid Graphics Starburst Lattice - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Procedural rave-era acid graphics with concentric warped starburst contours, high-contrast chartreuse vector frames, and dense technical alignment ticks.
param("zoom", 1.0, 0.5, 2.0);
param("line_weight", 2.0, 1.0, 6.0);
param("star_spikes", 8.0, 4.0, 16.0);
param("grid_density", 12.0, 6.0, 24.0);

function draw(t) {
  // Deep obsidian ground with safe luminance
  background(0.08, 0.08, 0.11);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88 * zoom;

  push();
  translate(cx, cy);

  // Outer framing boundary
  noFill();
  stroke(0.78, 1.0, 0.0); // Acid chartreuse
  strokeWeight(line_weight * 1.5);
  rect(-span * 0.5, -span * 0.5, span, span);

  // High-contrast coordinate crosshair grid
  stroke(0.28, 0.32, 0.40);
  strokeWeight(1.0);
  const step = span / grid_density;
  for (let k = -span * 0.5; k <= span * 0.5; k += step) {
    line(k, -span * 0.5, k, span * 0.5);
    line(-span * 0.5, k, span * 0.5, k);
  }

  // Multi-pointed rave acid starbursts
  const spikes = floor(star_spikes);
  const count = spikes * 2;
  const rOut = span * 0.38;
  const rIn = span * 0.14;

  // Starburst drop shadow/halo
  noFill();
  stroke(1.0, 0.0, 0.5, 0.85); // Electric magenta
  strokeWeight(line_weight + 2.0);
  beginShape();
  for (let i = 0; i <= count; i++) {
    const a = (i * 6.2831853) / count;
    const r = (i % 2 === 0 ? rOut * 1.04 : rIn * 0.95);
    vertex(cos(a) * r, sin(a) * r);
  }
  endShape();

  // Primary acid chartreuse starburst
  fill(0.78, 1.0, 0.0);
  stroke(0.08, 0.08, 0.11);
  strokeWeight(line_weight);
  beginShape();
  for (let i = 0; i <= count; i++) {
    const a = (i * 6.2831853) / count;
    const r = (i % 2 === 0 ? rOut : rIn);
    vertex(cos(a) * r, sin(a) * r);
  }
  endShape();

  // Concentric interior cutout rings
  fill(0.08, 0.08, 0.11);
  circle(0, 0, rIn * 1.3);

  noFill();
  stroke(1.0, 0.0, 0.5);
  strokeWeight(line_weight + 1.0);
  circle(0, 0, rIn * 0.9);

  stroke(0.95, 0.96, 1.0);
  strokeWeight(line_weight);
  circle(0, 0, rIn * 0.5);

  // Target reticle crosshairs
  stroke(0.78, 1.0, 0.0);
  line(-rOut * 1.25, 0, rOut * 1.25, 0);
  line(0, -rOut * 1.25, 0, rOut * 1.25);

  // Technical edge ticks
  stroke(0.95, 0.96, 1.0);
  strokeWeight(1.5);
  for (let d = -span * 0.45; d <= span * 0.45; d += span * 0.05) {
    line(d, -span * 0.5, d, -span * 0.5 + 12);
    line(d, span * 0.5 - 12, d, span * 0.5);
    line(-span * 0.5, d, -span * 0.5 + 12, d);
    line(span * 0.5 - 12, d, span * 0.5, d);
  }

  pop();
}
