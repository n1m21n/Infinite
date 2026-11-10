// Cymatics Sine Standing Wave Radial Ripples - Infinite Library (Sketch 2D)
param("radial_modes", 8.0, 4.0, 16.0);
param("angular_sectors", 6.0, 2.0, 12.0);
param("sand_concentration", 0.70, 0.2, 1.0);
param("drift", 0.25, 0.05, 0.7);

function draw(t) {
  // Deep obsidian/charcoal plate (#080B11)
  background(0.031, 0.043, 0.067);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;
  const time = t * drift;

  push();
  translate(cx, cy);

  // 1. Heavy Circular Resonator Brass Rim with Calibrated Degree Ticks
  const plateRadius = span * 0.46;
  stroke(0.773, 0.627, 0.349); // Brass
  strokeWeight(3.5);
  noFill();
  circle(0, 0, plateRadius * 2.0);

  stroke(0.306, 0.804, 0.769, 0.6); // Cyan
  strokeWeight(1.2);
  circle(0, 0, plateRadius * 1.94);

  // Calibrated degree tick marks around brass perimeter
  stroke(0.773, 0.627, 0.349, 0.85);
  strokeWeight(1.4);
  const nTicks = 72;
  for (let k = 0; k < nTicks; k++) {
    const angle = (k / nTicks) * Math.PI * 2.0;
    const tickLen = (k % 6 === 0) ? span * 0.024 : span * 0.012;
    const r1 = plateRadius * 0.97;
    const r2 = r1 - tickLen;
    line(cos(angle) * r1, sin(angle) * r1, cos(angle) * r2, sin(angle) * r2);
  }

  // 2. Bessel Radial Standing Wave Rings (J_m(k*r) * cos(m*theta))
  const nRings = floor(radial_modes);
  const mSectors = floor(angular_sectors);

  for (let r = 1; r <= nRings; r++) {
    const rNorm = r / nRings;
    const rad = rNorm * plateRadius * 0.88;

    // Resonant ring contour in silica sand
    stroke(0.957, 0.965, 0.973, 0.80);
    strokeWeight(1.8 + (1.0 - rNorm) * 1.2);
    noFill();

    beginShape();
    const pts = 90;
    for (let p = 0; p <= pts; p++) {
      const a = (p / pts) * Math.PI * 2.0;
      const mod = sin(a * mSectors + time * 1.5) * (span * 0.016 * (1.0 - rNorm * 0.5));
      const px = cos(a) * (rad + mod);
      const py = sin(a) * (rad + mod);
      vertex(px, py);
    }
    endShape(1);

    // Cyan interference sub-harmonics between main rings
    if (r < nRings) {
      stroke(0.306, 0.804, 0.769, 0.30);
      strokeWeight(0.9);
      circle(0, 0, rad * 1.08 * 2.0);
    }
  }

  // 3. Hyperbolic Saddle Radial Spokes (Zero-Velocity Nodal Diameters)
  stroke(0.773, 0.627, 0.349, 0.55);
  strokeWeight(1.4);
  for (let s = 0; s < mSectors; s++) {
    const sa = (s / mSectors) * Math.PI;
    const offset = sin(time + s) * 0.03;
    line(cos(sa + offset) * plateRadius * 0.88, sin(sa + offset) * plateRadius * 0.88,
        -cos(sa + offset) * plateRadius * 0.88, -sin(sa + offset) * plateRadius * 0.88);
  }

  // 4. Stippled Sand Particles Trapped in Low-Energy Radial Traps
  noStroke();
  fill(0.957, 0.965, 0.973, 0.92);
  const nGrains = floor(450 * sand_concentration);
  for (let i = 0; i < nGrains; i++) {
    const angle = ((i * 77.3) % 360) * (Math.PI / 180.0);
    const ringChoice = (i % nRings) + 1;
    const baseR = (ringChoice / nRings) * plateRadius * 0.88;
    const jitter = sin(i * 12.3) * (span * 0.012);
    const rDist = baseR + jitter;

    const gx = cos(angle) * rDist;
    const gy = sin(angle) * rDist;
    const gSize = 1.4 + ((i % 4) * 0.7);
    circle(gx, gy, gSize);
  }

  // 5. Central Brass Hub & Transducer Exciter
  fill(0.773, 0.627, 0.349);
  circle(0, 0, span * 0.065);
  fill(0.306, 0.804, 0.769);
  circle(0, 0, span * 0.038);
  fill(0.031, 0.043, 0.067);
  circle(0, 0, span * 0.018);

  pop();
}
