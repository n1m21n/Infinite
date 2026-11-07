// Demoscene Plasma Sine Wave Interference - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Mathematical wave interference study with concentric interference rings, dynamic polar rosettes, and vivid Amiga demoscene copper gradient bars on pitch black ground.
param("frequencies", 5.0, 2.0, 10.0);
param("ring_density", 12.0, 6.0, 24.0);
param("speed", 0.45, 0.1, 1.2);
param("amplitude", 1.15, 0.5, 2.0);

function draw(t) {
  // Deep obsidian void (#0c0e18)
  background(0.048, 0.055, 0.088);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;

  push();
  translate(cx, cy);

  // Outer framing boundary
  stroke(0.12, 0.16, 0.28);
  strokeWeight(2.8);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  const time = t * speed;

  // 1. Concentric elliptical wave interference rings
  const rings = floor(ring_density);
  for (let r = rings; r >= 1; r--) {
    const fr = r / rings;
    const radX = fr * span * 0.44 * amplitude;
    const radY = fr * span * 0.32 * amplitude;
    const warp = sin(time * 3.0 + r * 0.7) * (span * 0.03);

    const isCyan = (r % 2 === 0);
    if (isCyan) {
      stroke(0.0, 0.878, 1.0, 0.85); // Electric Cyan
      strokeWeight(2.4);
    } else {
      stroke(1.0, 0.231, 0.0, 0.90); // Flame Orange
      strokeWeight(2.2);
    }
    noFill();
    ellipse(0, 0, radX * 2.0 + warp, radY * 2.0 - warp);
  }

  // 2. Rotating polar harmonic rose curve
  push();
  rotate(time * 0.6);
  stroke(1.0, 1.0, 1.0, 0.95);
  strokeWeight(2.6);
  noFill();
  beginShape();
  const k = floor(frequencies);
  const steps = 180;
  for (let s = 0; s <= steps; s++) {
    const theta = (s / steps) * Math.PI * 2.0;
    const radius = span * 0.26 * cos(k * theta) * amplitude;
    vertex(cos(theta) * radius, sin(theta) * radius);
  }
  endShape();
  pop();

  // 3. Counter-rotating copper starburst axes
  push();
  rotate(-time * 0.45);
  stroke(1.0, 0.231, 0.0, 0.92);
  strokeWeight(3.0);
  line(-span * 0.38, 0, span * 0.38, 0);
  line(0, -span * 0.38, 0, span * 0.38);
  stroke(0.0, 0.878, 1.0, 0.92);
  strokeWeight(2.0);
  line(-span * 0.28, -span * 0.28, span * 0.28, span * 0.28);
  line(-span * 0.28, span * 0.28, span * 0.28, -span * 0.28);
  pop();

  // 4. Center luminescent core
  fill(1.0, 1.0, 1.0, 0.98);
  noStroke();
  circle(0, 0, span * 0.09);

  // 5. Registration grid
  stroke(0.0, 0.878, 1.0, 0.7);
  strokeWeight(1.4);
  for (let i = -5; i <= 5; i++) {
    const p = (i / 5.0) * (span * 0.42);
    line(p, -8, p, 8);
    line(-8, p, 8, p);
  }

  pop();
}
