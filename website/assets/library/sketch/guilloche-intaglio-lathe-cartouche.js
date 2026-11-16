// Guilloche Intaglio Lathe Cartouche - Infinite Library (Sketch 2D)
param("strands", 24, 10, 40);
param("freq_x", 5.0, 2.0, 10.0);
param("weave_amp", 0.12, 0.03, 0.24);
param("drift", 0.45, 0.0, 1.5);

function draw(t) {
  background(0.886, 0.859, 0.800);
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;

  // Fine vertical plate-wiping micro-hatch
  strokeWeight(0.8);
  for (let i = 0; i < 84; i++) {
    const x = w * (0.04 + 0.92 * (i / 83));
    stroke(0.102, 0.114, 0.133, 0.045);
    line(x, h * 0.05, x, h * 0.95);
  }

  // Outer engraved frame
  noFill();
  stroke(0.102, 0.114, 0.133, 0.9);
  strokeWeight(2.4);
  rect(w * 0.045, h * 0.045, w * 0.91, h * 0.91);

  // Horizontal guilloche interference ribbons
  const nStrands = floor(strands);
  const pts = 160;
  const ph = t * drift;

  for (let s = 0; s < nStrands; s++) {
    const u = s / Math.max(1, nStrands - 1);
    const yBase = h * (0.16 + 0.68 * u);
    const env = Math.sin(u * PI);

    if (s % 4 === 1) {
      stroke(0.831, 0.686, 0.216, 0.72);
    } else if (s % 6 === 3) {
      stroke(0.557, 0.141, 0.220, 0.64);
    } else {
      stroke(0.102, 0.114, 0.133, 0.78);
    }
    strokeWeight(1.2);

    beginShape();
    for (let i = 0; i <= pts; i++) {
      const vx = i / pts;
      const x = w * (0.09 + 0.82 * vx);
      const wave1 = sin(vx * TAU * freq_x + u * 8.0 + ph);
      const wave2 = cos(vx * TAU * (freq_x * 0.5) - u * 6.0 - ph * 0.7);
      const y = yBase + h * weave_amp * env * (0.65 * wave1 + 0.35 * wave2);
      vertex(x, y);
    }
    endShape();
  }

  // Central oval engraved cartouche medallion
  const rx = w * 0.26;
  const ry = h * 0.17;
  for (let k = 0; k < 10; k++) {
    const sc = 1.0 - k * 0.055;
    stroke(k % 3 === 1 ? 0.831 : 0.102, k % 3 === 1 ? 0.686 : 0.114, k % 3 === 1 ? 0.216 : 0.133, 0.85);
    strokeWeight(k === 0 ? 2.2 : 1.0);
    beginShape();
    for (let i = 0; i <= 120; i++) {
      const a = (i / 120) * TAU;
      const ripple = 1.0 + 0.035 * sin(a * 24.0 + k * 0.6 + ph);
      vertex(cx + cos(a) * rx * sc * ripple, cy + sin(a) * ry * sc * ripple);
    }
    endShape();
  }

  // Ruby jewel center mark
  noStroke();
  fill(0.557, 0.141, 0.220, 0.95);
  circle(cx, cy, w * 0.028);
}
