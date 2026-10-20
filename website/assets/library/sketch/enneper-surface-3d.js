// Enneper Surface 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Alfred Enneper's self-intersecting minimal surface (1864) with zero mean curvature, saddle wings and orbiting quanta.
param("scale_r", 0.44, 0.15, 1.2);
param("u_ribs", 36, 12, 64);
param("v_segs", 24, 8, 48);
param("spin", 0.45, 0.0, 2.0);
param("hue", 0.60, 0.0, 1.0);
param("tube_r", 0.016, 0.005, 0.04);

function enneperPoint(u, v, sc) {
  // Classical Enneper minimal surface equations
  const x = sc * (u - (u * u * u) / 3.0 + u * v * v);
  const y = sc * (v - (v * v * v) / 3.0 + v * u * u);
  const z = sc * (u * u - v * v);
  return [x, y, z];
}

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(t * spin * 0.75 + 0.35);
  rotateZ(sin(t * 0.25) * 0.18);

  const U_COUNT = floor(u_ribs);
  const V_COUNT = floor(v_segs);
  const R_BOUND = 1.65;

  // Draw transverse parametric u-curves (constant u, sweeping v)
  for (let i = 0; i <= U_COUNT; i++) {
    const fracU = i / U_COUNT;
    const u = -R_BOUND + fracU * 2.0 * R_BOUND;
    fill(hsl((hue + fracU * 0.45) % 1.0, 0.85, 0.58));

    let prev = enneperPoint(u, -R_BOUND, scale_r);
    for (let j = 1; j <= V_COUNT; j++) {
      const fracV = j / V_COUNT;
      const v = -R_BOUND + fracV * 2.0 * R_BOUND;
      const curr = enneperPoint(u, v, scale_r);
      tube(prev[0], prev[1], prev[2], curr[0], curr[1], curr[2], tube_r, 6);
      prev = curr;
    }
  }

  // Draw conjugate v-curves (constant v, sweeping u)
  for (let j = 0; j <= V_COUNT; j += 2) {
    const fracV = j / V_COUNT;
    const v = -R_BOUND + fracV * 2.0 * R_BOUND;
    fill(hsl((hue + 0.25 + fracV * 0.45) % 1.0, 0.90, 0.65));

    let prev = enneperPoint(-R_BOUND, v, scale_r);
    for (let i = 1; i <= U_COUNT; i++) {
      const fracU = i / U_COUNT;
      const u = -R_BOUND + fracU * 2.0 * R_BOUND;
      const curr = enneperPoint(u, v, scale_r);
      tube(prev[0], prev[1], prev[2], curr[0], curr[1], curr[2], tube_r * 1.15, 6);
      prev = curr;
    }
  }

  // Orbiting minimal energy quanta traversing the saddle contours
  const quanta = 6;
  for (let q = 0; q < quanta; q++) {
    const angle = (t * 0.35 + q / quanta) * TAU;
    const qu = sin(angle) * 1.35;
    const qv = cos(angle * 1.5) * 1.35;
    const pos = enneperPoint(qu, qv, scale_r);

    push();
    translate(pos[0], pos[1], pos[2]);
    fill(hsl((hue + 0.5 + q / quanta) % 1.0, 0.95, 0.78));
    sphere(0.065, 10);
    pop();
  }

  pop();
}
