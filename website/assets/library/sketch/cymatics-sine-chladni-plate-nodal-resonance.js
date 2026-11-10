// Cymatics Sine Chladni Plate Nodal Resonance - Infinite Library (Sketch 2D)
param("m_mode", 3.0, 2.0, 7.0);
param("n_mode", 5.0, 2.0, 7.0);
param("grain_density", 0.65, 0.2, 1.0);
param("vibration", 0.30, 0.05, 0.8);

function draw(t) {
  // Deep acoustic resonance plate background (#0B0E14)
  background(0.043, 0.055, 0.078);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;
  const time = t * vibration;

  push();
  translate(cx, cy);

  // 1. Resonant Brass Plate Outer Boundary & Fastener Pins
  stroke(0.773, 0.627, 0.349); // Resonant Brass (#C5A059)
  strokeWeight(3.5);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  stroke(0.306, 0.804, 0.769, 0.65); // Interference Cyan (#4ECDC4)
  strokeWeight(1.5);
  rect(-span * 0.465, -span * 0.465, span * 0.93, span * 0.93);

  // Brass corner mounting bolts
  fill(0.773, 0.627, 0.349);
  noStroke();
  const boltR = span * 0.016;
  const boltOffset = span * 0.45;
  circle(-boltOffset, -boltOffset, boltR * 2.0);
  circle(boltOffset, -boltOffset, boltR * 2.0);
  circle(-boltOffset, boltOffset, boltR * 2.0);
  circle(boltOffset, boltOffset, boltR * 2.0);

  // 2. Harmonic Antinode Velocity Fields (Concentric Acoustic Ripples)
  stroke(0.306, 0.804, 0.769, 0.25);
  strokeWeight(1.2);
  noFill();
  for (let r = 0.08; r < 0.44; r += 0.035) {
    const wave = sin(r * 32.0 - time * 2.5) * 0.008;
    rect(-span * (r + wave), -span * (r + wave), span * (r + wave) * 2.0, span * (r + wave) * 2.0, 4.0);
  }

  // 3. Chladni 2D Mathematical Standing Wave Nodal Contours
  // Nodal equation: a*sin(n*pi*x)*sin(m*pi*y) + b*sin(m*pi*x)*sin(n*pi*y) = 0
  const m = floor(m_mode);
  const n = floor(n_mode);
  const aCoeff = 1.0;
  const bCoeff = -0.85 + sin(time) * 0.25;

  // Draw continuous nodal line contours
  stroke(0.957, 0.965, 0.973, 0.85); // Pure silica sand (#F4F6F8)
  strokeWeight(2.2);
  noFill();

  const gridSteps = 72;
  const stepSize = (span * 0.86) / gridSteps;
  const halfGrid = span * 0.43;

  for (let iy = 0; iy < gridSteps; iy++) {
    const yNorm = (iy / gridSteps) * 2.0 - 1.0;
    beginShape();
    let inContour = false;
    for (let ix = 0; ix <= gridSteps; ix++) {
      const xNorm = (ix / gridSteps) * 2.0 - 1.0;
      const xPix = -halfGrid + ix * stepSize;
      const yPix = -halfGrid + iy * stepSize;

      const chladni = aCoeff * sin(n * Math.PI * xNorm * 0.5) * sin(m * Math.PI * yNorm * 0.5)
                    + bCoeff * sin(m * Math.PI * xNorm * 0.5) * sin(n * Math.PI * yNorm * 0.5);

      if (abs(chladni) < 0.14) {
        vertex(xPix, yPix + chladni * 12.0);
        inContour = true;
      } else if (inContour) {
        endShape();
        beginShape();
        inContour = false;
      }
    }
    endShape();
  }

  // 4. Stippled Sand Grain Accumulations on Nodal Ridge Trajectories
  noStroke();
  fill(0.957, 0.965, 0.973, 0.90);
  const nGrains = floor(420 * grain_density);
  for (let i = 0; i < nGrains; i++) {
    const u = ((i * 137.5) % 360) * (Math.PI / 180.0);
    const rad = (0.05 + 0.38 * ((i * 0.618033) % 1.0)) * span;
    const gx = cos(u) * rad;
    const gy = sin(u) * rad;
    const gxNorm = gx / (span * 0.43);
    const gyNorm = gy / (span * 0.43);

    const val = aCoeff * sin(n * Math.PI * gxNorm * 0.5) * sin(m * Math.PI * gyNorm * 0.5)
              + bCoeff * sin(m * Math.PI * gxNorm * 0.5) * sin(n * Math.PI * gyNorm * 0.5);

    if (abs(val) < 0.22) {
      const gSize = 1.5 + ((i % 5) * 0.6);
      circle(gx, gy, gSize);
    }
  }

  // 5. Central Acoustic Transducer Exciter Core
  fill(0.773, 0.627, 0.349);
  circle(0, 0, span * 0.055);
  fill(0.306, 0.804, 0.769);
  circle(0, 0, span * 0.032);
  fill(0.043, 0.055, 0.078);
  circle(0, 0, span * 0.015);

  pop();
}
