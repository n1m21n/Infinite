// Op-Art Concentric Vibration Field - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Hypnotic concentric geometric ellipse field with alternating phase rings, logarithmic radial spokes, and pulsating retinal contrast shifts.
param("ring_count", 32.0, 16.0, 52.0);
param("vibration_freq", 14.0, 6.0, 24.0);
param("aspect_warp", 0.78, 0.4, 1.2);
param("contrast", 0.96, 0.5, 1.0);

function draw(t) {
  // Alabaster white ground
  background(0.965, 0.965, 0.965);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;

  push();
  translate(cx, cy);

  // Outer framing boundary
  stroke(0.04, 0.04, 0.05);
  strokeWeight(2.5);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  const numRings = floor(ring_count);
  const maxR = span * 0.45;

  // Alternating concentric undulating bands
  for (let i = numRings; i >= 1; i--) {
    const u = i / numRings;
    const rBase = maxR * u;
    const isDark = i % 2 === 0;

    if (isDark) {
      fill(0.04, 0.04, 0.05, contrast);
    } else {
      fill(0.965, 0.965, 0.965);
    }
    stroke(0.04, 0.04, 0.05);
    strokeWeight(1.5);

    beginShape();
    const steps = 72;
    for (let s = 0; s <= steps; s++) {
      const ang = (s * Math.PI * 2.0) / steps;
      const wave = sin(ang * vibration_freq + t * 2.5 + i * 0.4) * (span * 0.014 * (1.0 - u * 0.4));
      const rad = rBase + wave;
      const rx = cos(ang) * rad;
      const ry = sin(ang) * rad * aspect_warp;
      vertex(rx, ry);
    }
    endShape(CLOSE);
  }

  // Radiating logarithmic spoke sectors slicing the rings
  stroke(0.965, 0.965, 0.965, 0.85);
  strokeWeight(2.2);
  const spokes = 36;
  for (let s = 0; s < spokes; s++) {
    const a = (s * Math.PI * 2.0) / spokes + t * 0.3;
    const r1 = span * 0.08;
    const r2 = maxR * 0.96;
    line(cos(a) * r1, sin(a) * r1 * aspect_warp, cos(a) * r2, sin(a) * r2 * aspect_warp);
  }

  // Central pulsating black pupil core
  fill(0.04, 0.04, 0.05);
  noStroke();
  circle(0, 0, span * 0.10);

  // Central white needle point
  fill(0.965, 0.965, 0.965);
  circle(0, 0, span * 0.024);

  // Outer framing alignment crosses
  stroke(0.04, 0.04, 0.05);
  strokeWeight(1.8);
  for (let k = 0; k < 4; k++) {
    const ang = k * Math.PI * 0.5;
    const d = maxR * 0.98;
    const x = cos(ang) * d;
    const y = sin(ang) * d;
    line(x - 8, y, x + 8, y);
    line(x, y - 8, x, y + 8);
  }

  pop();
}
