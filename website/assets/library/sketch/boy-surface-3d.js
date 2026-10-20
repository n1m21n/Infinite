// Boy's Surface 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Parametric non-orientable immersion of the real projective plane with 3-fold rotational symmetry (Werner Boy, 1901 / Apery, 1986).
param("scale_r", 0.72, 0.2, 1.6);
param("u_ribs", 36, 12, 64);
param("v_segs", 42, 16, 72);
param("spin", 0.35, 0.0, 2.0);
param("hue", 0.62, 0.0, 1.0);
param("tube_r", 0.012, 0.005, 0.04);

function boyPoint(u, v, sc) {
  const sinV = sin(v);
  const cosV = cos(v);
  const sin2V = sin(2.0 * v);
  const cosV2 = cosV * cosV;
  const sin3U = sin(3.0 * u);
  const cosU = cos(u);
  const sinU = sin(u);
  const cos2U = cos(2.0 * u);
  const sin2U = sin(2.0 * u);

  const sqrt2 = 1.41421356;
  const denom = max(0.25, 2.0 - sqrt2 * sin3U * sin2V);

  const x = sc * (sqrt2 * cosV2 * cos2U + cosU * sin2V) / denom;
  const y = sc * (sqrt2 * cosV2 * sin2U - sinU * sin2V) / denom;
  const z = sc * (3.0 * cosV2 / denom - 1.0);
  return [x, z, y];
}

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(0.45 + sin(t * 0.22) * 0.14);
  rotateZ(t * spin * 0.25);

  const U_COUNT = floor(u_ribs);
  const V_COUNT = floor(v_segs);
  const U_MIN = 0.0;
  const U_MAX = PI;
  const V_MIN = 0.04;
  const V_MAX = PI - 0.04;

  // Longitudinal meridian ribs (sweeping v at constant u)
  for (let i = 0; i <= U_COUNT; i += 2) {
    const fracU = i / U_COUNT;
    const u = U_MIN + fracU * (U_MAX - U_MIN);
    fill(hsl((hue + fracU * 0.42) % 1.0, 0.88, 0.60));

    let prev = boyPoint(u, V_MIN, scale_r);
    for (let j = 1; j <= V_COUNT; j++) {
      const fracV = j / V_COUNT;
      const v = V_MIN + fracV * (V_MAX - V_MIN);
      const curr = boyPoint(u, v, scale_r);
      tube(prev[0], prev[1], prev[2], curr[0], curr[1], curr[2], tube_r, 6);
      prev = curr;
    }
  }

  // Transversal contour rings (sweeping u at constant v)
  for (let j = 1; j <= V_COUNT; j += 3) {
    const fracV = j / V_COUNT;
    const v = V_MIN + fracV * (V_MAX - V_MIN);
    fill(hsl((hue + 0.35 + fracV * 0.38) % 1.0, 0.92, 0.68));

    let prev = boyPoint(U_MIN, v, scale_r);
    for (let i = 1; i <= U_COUNT; i++) {
      const fracU = i / U_COUNT;
      const u = U_MIN + fracU * (U_MAX - U_MIN);
      const curr = boyPoint(u, v, scale_r);
      tube(prev[0], prev[1], prev[2], curr[0], curr[1], curr[2], tube_r * 0.85, 6);
      prev = curr;
    }
  }

  // Orbiting quanta tracing helical paths around the central triple point
  const numQuanta = 24;
  for (let k = 0; k < numQuanta; k++) {
    const fracK = k / numQuanta;
    const orbU = U_MIN + ((fracK + t * 0.07) % 1.0) * (U_MAX - U_MIN);
    const orbV = V_MIN + (0.5 + 0.44 * sin(t * 0.60 + fracK * TAU)) * (V_MAX - V_MIN);
    const pt = boyPoint(orbU, orbV, scale_r);

    push();
    translate(pt[0], pt[1], pt[2]);
    fill(hsl((hue + fracK * 0.52 + 0.18) % 1.0, 0.95, 0.75));
    sphere(tube_r * 2.2, 8, 8);
    pop();
  }

  pop();
}
