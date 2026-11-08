// Analog CRT Magnetic Deflection Lattice - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Electromagnetic yoke deflection raster study featuring orthogonal interlaced scanlines, RGB phosphor triad registration pips, sinusoidal beam flutter, and high-voltage anode calibration reticles.
param("scanlines", 36.0, 16.0, 64.0);
param("deflection", 1.25, 0.4, 2.5);
param("speed", 0.40, 0.1, 1.2);
param("line_width", 2.2, 1.0, 4.0);

function draw(t) {
  // Dark CRT glass background (#09150d)
  background(0.040, 0.082, 0.052);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;

  push();
  translate(cx, cy);

  // 1. Heavy outer magnetic shielding frame
  stroke(0.0, 0.90, 0.38, 0.85);
  strokeWeight(2.8);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  const time = t * speed;
  const count = floor(scanlines);

  // 2. Interlaced horizontal raster scanlines with sinusoidal yoke deflection
  for (let i = 0; i < count; i++) {
    const norm = (i / (count - 1)) * 2.0 - 1.0;
    const y0 = norm * span * 0.44;

    const isField2 = (i % 2 === 1);
    const isAccent = (i % 6 === 0);

    if (isAccent) {
      stroke(1.0, 0.0, 0.33, 0.90); // Phosphor red/magenta (#ff0055)
      strokeWeight(line_width * 1.3);
    } else if (isField2) {
      stroke(0.0, 1.0, 0.42, 0.85); // High-intensity phosphor green (#00ff66)
      strokeWeight(line_width * 0.95);
    } else {
      stroke(0.15, 0.75, 0.40, 0.75); // Low-persistence phosphor trace
      strokeWeight(line_width * 0.85);
    }
    noFill();

    beginShape();
    const steps = 40;
    for (let s = 0; s <= steps; s++) {
      const u = (s / steps) * 2.0 - 1.0;
      const x = u * span * 0.44;
      const flutter = sin(u * 3.2 * deflection + time * 1.6 + norm * 2.0) * (span * 0.035 * deflection);
      const warp = cos(norm * Math.PI * 0.5) * (span * 0.02 * sin(time * 0.8));
      const y = y0 + flutter + warp;
      vertex(x, y);
    }
    endShape();
  }

  // 3. Central magnetic anode alignment reticle
  push();
  rotate(time * 0.3);
  stroke(1.0, 1.0, 1.0, 0.95); // White electron beam focus
  strokeWeight(2.0);
  noFill();
  circle(0, 0, span * 0.22);
  circle(0, 0, span * 0.11);

  stroke(1.0, 0.0, 0.33, 0.85);
  strokeWeight(2.2);
  for (let a = 0; a < 4; a++) {
    const ang = (a / 4.0) * Math.PI * 2.0;
    push();
    rotate(ang);
    line(span * 0.07, 0, span * 0.16, 0);
    pop();
  }
  pop();

  // 4. Phosphor triad calibration dots along margins
  noStroke();
  for (let k = -6; k <= 6; k++) {
    const p = (k / 6.0) * (span * 0.44);
    // Green phosphor dot
    fill(0.0, 1.0, 0.42, 0.9);
    circle(p, -span * 0.46, 3.5);
    circle(p, span * 0.46, 3.5);
    // Magenta phosphor dot
    fill(1.0, 0.0, 0.33, 0.9);
    circle(-span * 0.46, p, 3.5);
    circle(span * 0.46, p, 3.5);
  }

  pop();
}
