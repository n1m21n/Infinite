// Kinetic Moire Plotter Grating - Infinite Library (Sketch 2D)
param("lines", 78, 36, 120);
param("shear", 0.085, 0.01, 0.2);
param("tilt", 0.075, 0.01, 0.22);
param("speed", 0.35, 0.0, 1.5);

function draw(t) {
  // Archival ivory paper (#F4F1EA)
  background(0.957, 0.945, 0.918);
  const w = width;
  const h = height;
  const margin = w * 0.085;
  const span = w - margin * 2.0;
  const n = floor(lines);

  // Subtle paper fiber grain
  noStroke();
  for (let i = 0; i < 160; i++) {
    const px = ((sin(i * 19.31) * 43758.54) % 1 + 1) % 1;
    const py = ((sin(i * 73.17 + 2.1) * 29431.13) % 1 + 1) % 1;
    fill(0.75, 0.72, 0.66, 0.14);
    circle(px * w, py * h, 1.4 + (i % 2));
  }

  // Outer registration border and scale ticks
  noFill();
  stroke(0.067, 0.075, 0.094, 0.45);
  strokeWeight(1.2);
  rect(margin * 0.65, margin * 0.65, w - margin * 1.3, h - margin * 1.3);

  for (let k = 0; k <= 24; k++) {
    const tx = margin + (k / 24.0) * span;
    const tickH = k % 4 === 0 ? 10 : 5;
    line(tx, margin * 0.65 - tickH, tx, margin * 0.65);
    line(tx, h - margin * 0.65, tx, h - margin * 0.65 + tickH);
  }

  // Primary vertical wave-sheared carbon ink grating (#111318)
  const phase = t * speed;
  stroke(0.067, 0.075, 0.094, 0.88);
  strokeWeight(1.65);
  for (let i = 0; i < n; i++) {
    const u = i / (n - 1);
    const xBase = margin + u * span;
    beginShape();
    for (let s = 0; s <= 32; s++) {
      const v = s / 32.0;
      const y = margin + v * span;
      const dx = sin(v * PI * 3.0 + u * 5.0 + phase) * span * shear * 0.45;
      vertex(xBase + dx, y);
    }
    endShape();
  }

  // Secondary tilted ultramarine grating (#1B498A)
  push();
  translate(w * 0.5, h * 0.5);
  rotate(tilt + 0.025 * sin(phase * 0.8));
  stroke(0.106, 0.286, 0.541, 0.72);
  strokeWeight(1.45);
  for (let i = 0; i < n; i++) {
    const u = i / (n - 1) - 0.5;
    const xBase = u * span * 0.94;
    beginShape();
    for (let s = 0; s <= 28; s++) {
      const v = s / 28.0 - 0.5;
      const y = v * span * 0.94;
      const dx = cos(v * PI * 4.0 - u * 4.0 - phase * 1.1) * span * shear * 0.35;
      vertex(xBase + dx, y);
    }
    endShape();
  }
  pop();

  // Vermilion accent harmonic cross-lines (#D9381E)
  push();
  translate(w * 0.5, h * 0.5);
  rotate(-tilt * 1.15 - 0.02 * cos(phase * 0.7));
  stroke(0.851, 0.220, 0.118, 0.65);
  strokeWeight(1.25);
  const nAccent = floor(n * 0.45);
  for (let i = 0; i < nAccent; i++) {
    const u = i / (nAccent - 1) - 0.5;
    const xBase = u * span * 0.86;
    line(xBase, -span * 0.43, xBase, span * 0.43);
  }
  pop();
}
