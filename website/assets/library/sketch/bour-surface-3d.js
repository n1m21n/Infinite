// Bour's Minimal Surface 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Self-intersecting 3D minimal surface with 3-lobed rotational symmetry (Edmond Bour, 1862).
param("scale_r", 0.68, 0.2, 1.5);
param("u_ribs", 38, 12, 64);
param("v_segs", 48, 16, 72);
param("spin", 0.35, 0.0, 2.0);
param("hue", 0.65, 0.0, 1.0);
param("tube_r", 0.014, 0.005, 0.04);

function bourPoint(u, v, sc) {
  // Edmond Bour's minimal surface equations
  const cosV = cos(v);
  const sinV = sin(v);
  const cos2V = cos(2.0 * v);
  const sin2V = sin(2.0 * v);
  const cos15V = cos(1.5 * v);

  const uSqrt = sqrt(max(0.0, u));
  const u15 = u * uSqrt;

  const x = sc * (u * cosV - 0.5 * u * u * cos2V);
  const z = sc * (-u * sinV - 0.5 * u * u * sin2V);
  const y = sc * (1.3333333 * u15 * cos15V) - 0.15;
  return [x, y, z];
}

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(0.52 + sin(t * 0.22) * 0.14);
  rotateZ(t * spin * 0.28);

  const U_COUNT = floor(u_ribs);
  const V_COUNT = floor(v_segs);
  const U_MIN = 0.06;
  const U_MAX = 1.35;
  const MAX_V = TAU;

  // Longitudinal petal ribs (constant v, sweeping u)
  for (let j = 0; j <= V_COUNT; j += 2) {
    const fracV = j / V_COUNT;
    const v = fracV * MAX_V;
    fill(hsl((hue + fracV * 0.44) % 1.0, 0.88, 0.58));

    let prev = bourPoint(U_MIN, v, scale_r);
    for (let i = 1; i <= U_COUNT; i++) {
      const fracU = i / U_COUNT;
      const u = U_MIN + fracU * (U_MAX - U_MIN);
      const curr = bourPoint(u, v, scale_r);
      tube(prev[0], prev[1], prev[2], curr[0], curr[1], curr[2], tube_r, 6);
      prev = curr;
    }
  }

  // Transversal contour rings (constant u, sweeping v)
  for (let i = 1; i <= U_COUNT; i += 3) {
    const fracU = i / U_COUNT;
    const u = U_MIN + fracU * (U_MAX - U_MIN);
    fill(hsl((hue + 0.35 + fracU * 0.38) % 1.0, 0.92, 0.65));

    let prev = bourPoint(u, 0, scale_r);
    for (let j = 1; j <= V_COUNT; j++) {
      const fracV = j / V_COUNT;
      const v = fracV * MAX_V;
      const curr = bourPoint(u, v, scale_r);
      tube(prev[0], prev[1], prev[2], curr[0], curr[1], curr[2], tube_r * 1.1, 6);
      prev = curr;
    }
  }

  // Orbiting quanta floating along the three minimal petals
  const quanta = 6;
  for (let q = 0; q < quanta; q++) {
    const phase = (t * 0.25 + q / quanta) % 1.0;
    const qv = phase * MAX_V;
    const qu = U_MIN + (0.55 + 0.4 * sin(t * 0.7 + q * 1.5)) * (U_MAX - U_MIN);
    const pos = bourPoint(qu, qv, scale_r);

    push();
    translate(pos[0], pos[1], pos[2]);
    fill(hsl((hue + 0.58 + q / quanta) % 1.0, 0.96, 0.80));
    sphere(0.065, 10);
    pop();
  }

  pop();
}
