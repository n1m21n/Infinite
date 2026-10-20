// Kuen's Surface 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Parametric pseudospherical surface of constant negative curvature with twisting spire lobes (Alfred Kuen, 1884).
param("scale_r", 0.52, 0.2, 1.5);
param("u_ribs", 36, 12, 64);
param("v_segs", 46, 16, 72);
param("spin", 0.32, 0.0, 2.0);
param("hue", 0.60, 0.0, 1.0);
param("tube_r", 0.013, 0.005, 0.04);

function kuenPoint(u, v, sc) {
  const sinV = sin(v);
  const cosV = cos(v);
  const sinU = sin(u);
  const cosU = cos(u);

  const denom = 1.0 + u * u * sinV * sinV;
  const x = sc * (2.0 * (cosU + u * sinU) * sinV) / denom;
  const z = sc * (2.0 * (sinU - u * cosU) * sinV) / denom;
  const vClamped = max(0.04, min(PI - 0.04, v));
  const zCoord = log(tan(vClamped * 0.5)) + (2.0 * cosV) / denom;
  const y = sc * zCoord * 0.68;
  return [x, y, z];
}

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(0.48 + sin(t * 0.20) * 0.12);
  rotateZ(t * spin * 0.22);

  const U_COUNT = floor(u_ribs);
  const V_COUNT = floor(v_segs);
  const U_MIN = -3.8;
  const U_MAX = 3.8;
  const V_MIN = 0.12;
  const V_MAX = PI - 0.12;

  // Longitudinal spire ribs (sweeping v at constant u)
  for (let i = 0; i <= U_COUNT; i += 2) {
    const fracU = i / U_COUNT;
    const u = U_MIN + fracU * (U_MAX - U_MIN);
    fill(hsl((hue + fracU * 0.42) % 1.0, 0.88, 0.58));

    let prev = kuenPoint(u, V_MIN, scale_r);
    for (let j = 1; j <= V_COUNT; j++) {
      const fracV = j / V_COUNT;
      const v = V_MIN + fracV * (V_MAX - V_MIN);
      const curr = kuenPoint(u, v, scale_r);
      tube(prev[0], prev[1], prev[2], curr[0], curr[1], curr[2], tube_r, 6);
      prev = curr;
    }
  }

  // Transversal contour rings (sweeping u at constant v)
  for (let j = 1; j <= V_COUNT; j += 3) {
    const fracV = j / V_COUNT;
    const v = V_MIN + fracV * (V_MAX - V_MIN);
    fill(hsl((hue + 0.32 + fracV * 0.40) % 1.0, 0.92, 0.66));

    let prev = kuenPoint(U_MIN, v, scale_r);
    for (let i = 1; i <= U_COUNT; i++) {
      const fracU = i / U_COUNT;
      const u = U_MIN + fracU * (U_MAX - U_MIN);
      const curr = kuenPoint(u, v, scale_r);
      tube(prev[0], prev[1], prev[2], curr[0], curr[1], curr[2], tube_r * 0.85, 6);
      prev = curr;
    }
  }

  // Orbiting quanta particles tracing helical trajectories
  const numQuanta = 24;
  for (let k = 0; k < numQuanta; k++) {
    const fracK = k / numQuanta;
    const orbU = U_MIN + ((fracK + t * 0.08) % 1.0) * (U_MAX - U_MIN);
    const orbV = V_MIN + (0.5 + 0.42 * sin(t * 0.65 + fracK * TAU)) * (V_MAX - V_MIN);
    const pt = kuenPoint(orbU, orbV, scale_r);

    push();
    translate(pt[0], pt[1], pt[2]);
    fill(hsl((hue + fracK * 0.5 + 0.2) % 1.0, 0.95, 0.74));
    sphere(tube_r * 2.2, 8, 8);
    pop();
  }

  pop();
}
