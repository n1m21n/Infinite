// Harmonograph Pendulum Damped Lissajous - Infinite Library (Sketch 2D)
param("ratio_x", 3.012, 1.5, 5.0);
param("ratio_y", 2.006, 1.5, 5.0);
param("damping", 0.018, 0.006, 0.045);
param("phase_drift", 0.42, 0.0, 1.5);

function draw(t) {
  // Warm aged cotton rag paper ground (#E2DACB)
  background(0.886, 0.855, 0.796);

  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const rad = min(w, h) * 0.36;

  // Subtle laid-paper horizontal fibre texture
  strokeWeight(1.0);
  for (let i = 0; i < 48; i++) {
    const y = (i / 48) * h;
    stroke(0.553, 0.482, 0.353, 0.045);
    line(w * 0.04, y, w * 0.96, y);
  }

  // Brass-sepia outer plotting plate & corner registration marks
  noFill();
  stroke(0.553, 0.482, 0.353, 0.55);
  strokeWeight(1.6);
  rect(w * 0.06, h * 0.06, w * 0.88, h * 0.88);
  circle(cx, cy, rad * 2.16);

  // Degree calibration ticks around outer plate circle
  for (let i = 0; i < 72; i++) {
    const a = (i / 72) * TAU;
    const r0 = rad * 1.08;
    const r1 = r0 + (i % 6 === 0 ? 14 : 6);
    stroke(0.098, 0.110, 0.227, i % 6 === 0 ? 0.55 : 0.24);
    strokeWeight(i % 6 === 0 ? 1.5 : 0.9);
    line(cx + cos(a) * r0, cy + sin(a) * r0, cx + cos(a) * r1, cy + sin(a) * r1);
  }

  // Axis crosshairs
  stroke(0.553, 0.482, 0.353, 0.25);
  strokeWeight(1.0);
  line(cx - rad * 1.12, cy, cx + rad * 1.12, cy);
  line(cx, cy - rad * 1.12, cx, cy + rad * 1.12);

  // Secondary rhythmic counter-structure: vermilion harmonic envelope
  const pShift = t * phase_drift * 0.45;
  noFill();
  stroke(0.714, 0.231, 0.157, 0.42);
  strokeWeight(1.1);
  beginShape();
  for (let i = 0; i <= 360; i++) {
    const u = i * 0.06;
    const env = exp(-damping * 0.85 * u);
    const ex = cx + rad * 0.92 * env * (0.58 * sin(ratio_x * u + pShift) + 0.42 * cos((ratio_x + 1.0) * u * 0.5));
    const ey = cy + rad * 0.92 * env * (0.58 * sin(ratio_y * u + 1.1) + 0.42 * sin((ratio_y - 0.5) * u));
    vertex(ex, ey);
  }
  endShape();

  // Primary form: 4-pendulum damped Prussian indigo ink tracing
  const steps = 1450;
  const dt = 0.042;
  let px = 0;
  let py = 0;
  for (let i = 0; i <= steps; i++) {
    const u = i * dt;
    const env1 = exp(-damping * u);
    const env2 = exp(-damping * 1.28 * u);
    const x = cx + rad * (0.62 * env1 * sin(ratio_x * u + 0.35 + pShift) + 0.38 * env2 * sin((ratio_x * 0.5 + 0.015) * u + 1.4));
    const y = cy + rad * (0.62 * env1 * sin(ratio_y * u + 1.57) + 0.38 * env2 * cos((ratio_y * 1.5 - 0.012) * u + 0.4 + pShift * 0.6));
    if (i > 0) {
      const alpha = 0.28 + 0.64 * env1;
      stroke(0.098, 0.110, 0.227, alpha);
      strokeWeight(1.15 + 0.55 * env1);
      line(px, py, x, y);
    }
    px = x;
    py = y;
  }

  // Center brass pivot jewel
  noStroke();
  fill(0.714, 0.231, 0.157, 0.85);
  circle(cx, cy, 7.0);
}
