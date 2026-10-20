// Dini's Surface 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Parametric pseudospherical surface of constant negative curvature twisted along a helicoid screw axis (Ulisse Dini, 1865).
param("scale_r", 0.55, 0.2, 1.5);
param("turns", 2.2, 1.0, 4.0);
param("pitch", 0.14, 0.05, 0.35);
param("u_ribs", 42, 16, 72);
param("v_segs", 24, 8, 48);
param("spin", 0.40, 0.0, 2.0);
param("hue", 0.62, 0.0, 1.0);
param("tube_r", 0.015, 0.005, 0.04);

function diniPoint(u, v, sc, bPitch) {
  const sinV = sin(v);
  const x = sc * cos(u) * sinV;
  const z = sc * sin(u) * sinV;
  const y = sc * (cos(v) + log(tan(v * 0.5))) + bPitch * u - 0.45;
  return [x, y, z];
}

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(0.42 + sin(t * 0.2) * 0.12);
  rotateZ(t * spin * 0.25);

  const U_COUNT = floor(u_ribs);
  const V_COUNT = floor(v_segs);
  const MAX_U = turns * TAU;
  const V_MIN = 0.09;
  const V_MAX = 1.95;

  // Helical longitudinal ribs (constant v, sweeping u)
  for (let j = 0; j <= V_COUNT; j += 2) {
    const fracV = j / V_COUNT;
    const v = V_MIN + fracV * (V_MAX - V_MIN);
    fill(hsl((hue + fracV * 0.42) % 1.0, 0.88, 0.56));

    let prev = diniPoint(0, v, scale_r, pitch);
    for (let i = 1; i <= U_COUNT; i++) {
      const fracU = i / U_COUNT;
      const u = fracU * MAX_U;
      const curr = diniPoint(u, v, scale_r, pitch);
      tube(prev[0], prev[1], prev[2], curr[0], curr[1], curr[2], tube_r, 6);
      prev = curr;
    }
  }

  // Meridian transversal rings (constant u, sweeping v)
  for (let i = 0; i <= U_COUNT; i += 3) {
    const fracU = i / U_COUNT;
    const u = fracU * MAX_U;
    fill(hsl((hue + 0.32 + fracU * 0.38) % 1.0, 0.92, 0.64));

    let prev = diniPoint(u, V_MIN, scale_r, pitch);
    for (let j = 1; j <= V_COUNT; j++) {
      const fracV = j / V_COUNT;
      const v = V_MIN + fracV * (V_MAX - V_MIN);
      const curr = diniPoint(u, v, scale_r, pitch);
      tube(prev[0], prev[1], prev[2], curr[0], curr[1], curr[2], tube_r * 1.12, 6);
      prev = curr;
    }
  }

  // Orbiting quanta spiraling along the pseudosphere helicoid horn
  const quanta = 6;
  for (let q = 0; q < quanta; q++) {
    const phase = (t * 0.28 + q / quanta) % 1.0;
    const qu = phase * MAX_U;
    const qv = V_MIN + (0.5 + 0.45 * sin(t * 0.6 + q * 1.2)) * (V_MAX - V_MIN);
    const pos = diniPoint(qu, qv, scale_r, pitch);

    push();
    translate(pos[0], pos[1], pos[2]);
    fill(hsl((hue + 0.55 + q / quanta) % 1.0, 0.95, 0.78));
    sphere(0.062, 10);
    pop();
  }

  pop();
}
