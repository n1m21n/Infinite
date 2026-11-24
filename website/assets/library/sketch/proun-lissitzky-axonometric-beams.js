// Proun Lissitzky Axonometric Beams - Infinite Library (Sketch 2D)
param("beam_tilt", 0.28, 0.1, 0.55);
param("plane_layers", 6, 3, 10);
param("drift_speed", 0.22, 0.0, 0.8);
param("grain_lines", 38, 16, 64);

function draw(t) {
  // Warm parchment ground (#F4EFE6)
  background(0.957, 0.937, 0.902);
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const S = min(w, h);

  // Subtle architectural drafting paper weave
  stroke(0.84, 0.80, 0.74, 0.35);
  strokeWeight(1.0);
  const gN = floor(grain_lines);
  for (let i = 0; i < gN; i++) {
    const y = (i / (gN - 1)) * h;
    line(0, y, w, y);
  }

  // Secondary pale slate floating backdrop slab (#D8D5CF)
  push();
  translate(cx * 0.86, cy * 0.42);
  rotate(-0.14 + 0.03 * sin(t * drift_speed));
  noStroke();
  fill(0.847, 0.835, 0.812, 0.92);
  rect(-S * 0.36, -S * 0.09, S * 0.72, S * 0.18);
  pop();

  // Raw sienna vertical column rule (#C99848)
  push();
  translate(cx * 1.48, cy * 0.96);
  noStroke();
  fill(0.788, 0.596, 0.282, 0.92);
  rect(-S * 0.045, -S * 0.32, S * 0.09, S * 0.64);
  stroke(0.086, 0.086, 0.086, 0.45);
  strokeWeight(1.2);
  noFill();
  rect(-S * 0.045, -S * 0.32, S * 0.09, S * 0.64);
  pop();

  // Olive-umber textured monolith on left (#6B664E)
  push();
  translate(cx * 0.32, cy * 1.24);
  rotate(0.04);
  noStroke();
  fill(0.420, 0.400, 0.306, 0.94);
  rect(-S * 0.11, -S * 0.20, S * 0.22, S * 0.40);
  // Carbon inlay bar
  fill(0.086, 0.086, 0.086);
  rect(-S * 0.02, -S * 0.06, S * 0.028, S * 0.16);
  pop();

  // Primary axonometric diagonal carbon-black & slate beam (#161616 / #6E7377)
  const tilt = beam_tilt + 0.04 * sin(t * drift_speed * 0.8);
  push();
  translate(cx * 1.04, cy * 0.94);
  rotate(-tilt);
  noStroke();
  // Slate lit face
  fill(0.490, 0.506, 0.525);
  rect(-S * 0.038, -S * 0.34, S * 0.046, S * 0.68);
  // Carbon shadow face
  fill(0.086, 0.086, 0.086);
  rect(S * 0.008, -S * 0.34, S * 0.028, S * 0.68);
  // Top bevel cap
  fill(0.478, 0.325, 0.180);
  beginShape();
  vertex(-S * 0.038, -S * 0.34);
  vertex(S * 0.008, -S * 0.34);
  vertex(S * 0.036, -S * 0.365);
  vertex(-S * 0.012, -S * 0.365);
  endShape();
  pop();

  // Intersecting cantilevered carbon bar & warm cream square
  push();
  translate(cx * 0.84, cy * 1.18);
  rotate(-0.22);
  noStroke();
  fill(0.086, 0.086, 0.086, 0.96);
  rect(-S * 0.045, -S * 0.26, S * 0.09, S * 0.52);
  pop();

  push();
  translate(cx * 0.94, cy * 0.88);
  rotate(-0.18);
  fill(0.925, 0.890, 0.816, 0.88);
  stroke(0.478, 0.325, 0.180, 0.65);
  strokeWeight(1.5);
  rect(-S * 0.09, -S * 0.09, S * 0.18, S * 0.18);
  pop();

  // Vermilion constructivist cantilever accent (#C82820)
  push();
  translate(cx * 0.68, cy * 0.44 + sin(t * drift_speed * 1.4) * S * 0.015);
  noStroke();
  fill(0.086, 0.086, 0.086);
  rect(-S * 0.10, -S * 0.035, S * 0.18, S * 0.055);
  fill(0.784, 0.157, 0.125);
  rect(-S * 0.03, S * 0.02, S * 0.10, S * 0.055);
  // Copper-sienna arc segment
  fill(0.682, 0.388, 0.165);
  beginShape();
  for (let i = 0; i <= 16; i++) {
    const a = -PI * 0.5 + (i / 16) * PI;
    vertex(S * 0.07 + cos(a) * S * 0.03, -S * 0.03 + sin(a) * S * 0.09);
  }
  endShape();
  pop();

  // Precision drafting crosshairs & Proun registration ticks
  const nL = floor(plane_layers);
  stroke(0.086, 0.086, 0.086, 0.55);
  strokeWeight(1.3);
  for (let i = 0; i < nL; i++) {
    const px = w * (0.14 + (i / max(1, nL - 1)) * 0.72);
    const py = h * 0.06;
    line(px - 6, py, px + 6, py);
    line(px, py - 6, px, py + 6);
  }
  // Diagonal tension wire from upper block to center square
  stroke(0.682, 0.388, 0.165, 0.85);
  strokeWeight(2.4);
  line(cx * 0.78, cy * 0.48, cx * 0.94, cy * 0.98);
}
