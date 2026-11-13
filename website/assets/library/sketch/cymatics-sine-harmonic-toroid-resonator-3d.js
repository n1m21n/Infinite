// Cymatics Sine Harmonic Toroid Resonator 3D - Infinite Library (Sketch 3D)
param("ring_count", 9, 4, 16);
param("spoke_count", 24, 8, 40);
param("twist_rate", 0.35, 0.0, 1.0);
param("spread", 1.25, 0.7, 1.8);

function draw(t) {
  rotateY(t * 0.25);
  rotateX(0.38 + Math.sin(t * 0.4) * 0.08);

  // Central resonant sphere
  push();
  fill(0.49, 0.976, 1.0);
  sphere(0.24, 18);
  pop();

  // Nested harmonic torus shells
  const nr = floor(ring_count);
  for (let i = 0; i < nr; i++) {
    const u = i / nr;
    const rad = 0.42 + u * spread;
    const tilt = Math.sin(u * Math.PI * 2.0 + t * twist_rate) * 0.65;
    push();
    rotateX(tilt);
    rotateZ(u * Math.PI * 0.5);
    if (i % 2 === 0) {
      fill(0.96, 0.98, 1.0);
    } else {
      fill(0.49, 0.976, 1.0);
    }
    torus(rad, 0.022, 32, 8);
    pop();
  }

  // Radial standing-wave nodal spokes
  const ns = floor(spoke_count);
  for (let k = 0; k < ns; k++) {
    const a = (k / ns) * Math.PI * 2.0;
    const amp = 0.55 + 0.45 * Math.sin(a * 4.0 + t * 0.8);
    const rx = Math.cos(a) * 1.15;
    const rz = Math.sin(a) * 1.15;
    push();
    translate(rx, 0, rz);
    rotateY(-a);
    fill(0.92, 0.95, 0.98);
    box(0.45 * amp, 0.04, 0.04);
    pop();
  }
}
