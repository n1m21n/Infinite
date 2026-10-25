// Y2K Chrome Torus Orbit 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Interlocking liquid chrome ribbed tori rotating with differential gyroscopic axes, surrounded by orbiting spiked star satellites and metallic highlights.
param("ring_count", 16.0, 6.0, 32.0);
param("major_radius", 1.6, 0.6, 3.0);
param("minor_radius", 0.35, 0.1, 0.8);
param("orbit_speed", 0.6, 0.1, 2.0);
param("satellite_count", 8.0, 3.0, 16.0);

function draw(t) {
  push();
  const time = t * orbit_speed;

  // Gyroscopic scene rotation
  rotateY(time * 0.7);
  rotateX(0.65 + 0.15 * sin(time * 0.5));
  rotateZ(time * 0.3);

  const N = floor(ring_count);
  const R = major_radius;
  const r = minor_radius;

  // 1. Primary outer chrome torus constructed from tubular segments
  for (let i = 0; i < N; i++) {
    const a1 = (i / N) * 6.2831853;
    const a2 = ((i + 1) / N) * 6.2831853;

    const x1 = cos(a1) * R;
    const z1 = sin(a1) * R;
    const y1 = sin(a1 * 3.0 + time * 2.0) * (r * 0.6);

    const x2 = cos(a2) * R;
    const z2 = sin(a2) * R;
    const y2 = sin(a2 * 3.0 + time * 2.0) * (r * 0.6);

    // Chrome reflection sheen modulation
    const sheen = sin(a1 * 2.0 - time * 3.0) * 0.5 + 0.5;
    fill(
      0.655 + 0.28 * sheen,
      0.694 + 0.25 * sheen,
      1.0
    );
    tube(x1, y1, z1, x2, y2, z2, 0.055, 8);

    // Ribbed chrome beads
    push();
    translate(x1, y1, z1);
    fill(0.933, 0.945, 0.980); // White specular highlight
    sphere(0.065, 8, 8);
    pop();
  }

  // 2. Secondary inner counter-rotating tilted ring
  push();
  rotateX(1.570796 + sin(time * 0.8) * 0.3);
  rotateY(time * 1.2);
  const R_inner = R * 0.62;
  for (let i = 0; i < N; i++) {
    const a1 = (i / N) * 6.2831853;
    const a2 = ((i + 1) / N) * 6.2831853;

    const x1 = cos(a1) * R_inner;
    const z1 = sin(a1) * R_inner;
    const y1 = 0;

    const x2 = cos(a2) * R_inner;
    const z2 = sin(a2) * R_inner;
    const y2 = 0;

    // Acid iridescent rim
    if (i % 2 === 0) {
      fill(1.0, 0.420, 0.208); // Ember chrome
    } else {
      fill(0.306, 0.949, 0.722); // Mint chrome
    }
    tube(x1, y1, z1, x2, y2, z2, 0.04, 6);
  }
  pop();

  // 3. Orbiting Y2K spiked star satellite clusters
  const numSats = floor(satellite_count);
  for (let k = 0; k < numSats; k++) {
    const aSat = (k / numSats) * 6.2831853 + time * 1.5;
    const orbitR = R * 1.35 + 0.2 * sin(time * 3.0 + k);
    const sx = cos(aSat) * orbitR;
    const sz = sin(aSat) * orbitR;
    const sy = sin(aSat * 2.0 + time * 2.0) * 0.45;

    push();
    translate(sx, sy, sz);
    rotateX(time * 3.0 + k);
    rotateY(time * 2.5 + k);

    // Spiked star satellite: crossed diamond bars
    fill(0.933, 0.945, 0.980);
    box(0.22, 0.04, 0.04);
    box(0.04, 0.22, 0.04);
    box(0.04, 0.04, 0.22);
    pop();
  }

  // 4. Central liquid chrome core sphere
  fill(0.20, 0.24, 0.40);
  sphere(0.32, 16, 16);
  fill(0.655, 0.694, 1.0);
  sphere(0.26, 12, 12);

  pop();
}
