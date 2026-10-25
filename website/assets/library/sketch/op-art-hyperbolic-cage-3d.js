// Op Art Hyperbolic Cage 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Kinetic optical illusion ruled hyperboloid cage whose rotating straight struts generate curved saddle moire interference patterns.
param("strut_count", 32.0, 12.0, 64.0);
param("twist_angle", 1.8, 0.5, 3.14);
param("waist_radius", 0.45, 0.15, 1.2);
param("spin_rate", 0.5, 0.1, 2.0);
param("cage_height", 2.2, 1.0, 4.0);

function draw(t) {
  push();
  const time = t * spin_rate;

  // Scene tumble and rotation
  rotateY(time * 0.8);
  rotateX(0.45 + sin(time * 0.4) * 0.2);
  rotateZ(time * 0.2);

  const N = floor(strut_count);
  const H_half = cage_height * 0.5;
  const R_rim = waist_radius * 2.2;

  // 1. Dual circular rim collars
  const rimSteps = 36;
  for (let s = 0; s < rimSteps; s++) {
    const a1 = (s / rimSteps) * 6.2831853;
    const a2 = ((s + 1) / rimSteps) * 6.2831853;

    // Top collar
    fill(0.655, 0.694, 1.0); // Signal rim
    tube(cos(a1) * R_rim, H_half, sin(a1) * R_rim,
         cos(a2) * R_rim, H_half, sin(a2) * R_rim, 0.035, 8);

    // Bottom collar
    fill(0.655, 0.694, 1.0);
    tube(cos(a1) * R_rim, -H_half, sin(a1) * R_rim,
         cos(a2) * R_rim, -H_half, sin(a2) * R_rim, 0.035, 8);
  }

  // 2. Ruled hyperbolic straight struts generating curved optical moire surface
  for (let i = 0; i < N; i++) {
    const aTop = (i / N) * 6.2831853;
    const aBot = aTop + twist_angle + sin(time * 0.6) * 0.35;

    const x1 = cos(aTop) * R_rim;
    const z1 = sin(aTop) * R_rim;
    const y1 = H_half;

    const x2 = cos(aBot) * R_rim;
    const z2 = sin(aBot) * R_rim;
    const y2 = -H_half;

    // Alternating high-contrast optical stripes
    const isLight = (i % 2 === 0);
    if (isLight) {
      fill(0.933, 0.945, 0.980); // White strut
    } else {
      fill(0.655, 0.694, 1.0); // Signal strut
    }

    tube(x1, y1, z1, x2, y2, z2, 0.024, 6);

    // Junction beads on top and bottom collars
    push();
    translate(x1, y1, z1);
    fill(1.0, 0.42, 0.21); // Ember accent bead
    sphere(0.042, 6, 6);
    pop();

    // 3. Counter-twisting inner optical cage
    const aBotInner = aTop - twist_angle * 0.8;
    const R_in = R_rim * 0.55;
    const xi1 = cos(aTop) * R_in;
    const zi1 = sin(aTop) * R_in;
    const xi2 = cos(aBotInner) * R_in;
    const zi2 = sin(aBotInner) * R_in;

    fill(0.306, 0.949, 0.722); // Mint inner counter-strut
    tube(xi1, y1 * 0.8, zi1, xi2, y2 * 0.8, zi2, 0.016, 6);
  }

  pop();
}
