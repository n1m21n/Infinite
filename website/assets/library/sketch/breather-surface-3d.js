// Breather Surface 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Parametric Breather soliton surface from the sine-Gordon equation with helical breathing lobes and transverse ribs.
param("aa", 0.44, 0.25, 0.75);
param("u_ribs", 34, 12, 60);
param("v_segs", 42, 16, 72);
param("scale_r", 0.38, 0.15, 0.9);
param("spin", 0.32, 0.0, 2.0);
param("breathe", 0.35, 0.0, 1.0);
param("hue", 0.65, 0.0, 1.0);
param("tube_r", 0.012, 0.005, 0.04);

function sinh(x) { return (Math.exp(x) - Math.exp(-x)) * 0.5; }
function cosh(x) { return (Math.exp(x) + Math.exp(-x)) * 0.5; }

function breatherPoint(u, v, aaVal, sc) {
  const w = 1.0 - aaVal * aaVal;
  const b = sqrt(max(0.01, w));
  const coshAU = cosh(aaVal * u);
  const sinhAU = sinh(aaVal * u);
  const sinBV = sin(b * v);
  const cosBV = cos(b * v);

  const denom = max(0.08, aaVal * (pow(b * coshAU, 2.0) + pow(aaVal * sinBV, 2.0)));

  const x = sc * (-u + (2.0 * w * coshAU * sinhAU) / denom);
  const y = sc * ((2.0 * b * coshAU * (-b * cos(v) * cosBV - sin(v) * sinBV)) / denom);
  const z = sc * ((2.0 * b * coshAU * (-b * sin(v) * cosBV + cos(v) * sinBV)) / denom);
  return [x, y, z];
}

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(0.48 + sin(t * 0.25) * 0.16);
  rotateZ(t * spin * 0.22);

  const aaCur = clamp(aa + 0.04 * sin(t * 0.65 * breathe), 0.28, 0.68);
  const U_COUNT = floor(u_ribs);
  const V_COUNT = floor(v_segs);
  const U_RANGE = 8.5;
  const V_RANGE = 18.0;

  // Longitudinal meridian ribs (sweeping v at constant u)
  for (let i = 0; i <= U_COUNT; i += 2) {
    const fracU = i / U_COUNT;
    const u = -U_RANGE + fracU * 2.0 * U_RANGE;
    fill(hsl((hue + fracU * 0.38) % 1.0, 0.88, 0.60));

    let prev = breatherPoint(u, -V_RANGE, aaCur, scale_r);
    for (let j = 1; j <= V_COUNT; j++) {
      const fracV = j / V_COUNT;
      const v = -V_RANGE + fracV * 2.0 * V_RANGE;
      const curr = breatherPoint(u, v, aaCur, scale_r);
      tube(prev[0], prev[1], prev[2], curr[0], curr[1], curr[2], tube_r, 6);
      prev = curr;
    }
  }

  // Transversal contour rings (sweeping u at constant v)
  for (let j = 0; j <= V_COUNT; j += 3) {
    const fracV = j / V_COUNT;
    const v = -V_RANGE + fracV * 2.0 * V_RANGE;
    fill(hsl((hue + 0.35 + fracV * 0.35) % 1.0, 0.92, 0.68));

    let prev = breatherPoint(-U_RANGE, v, aaCur, scale_r);
    for (let i = 1; i <= U_COUNT; i++) {
      const fracU = i / U_COUNT;
      const u = -U_RANGE + fracU * 2.0 * U_RANGE;
      const curr = breatherPoint(u, v, aaCur, scale_r);
      tube(prev[0], prev[1], prev[2], curr[0], curr[1], curr[2], tube_r * 0.85, 6);
      prev = curr;
    }
  }

  // Orbiting quanta tracing helical paths along soliton crests
  const numQuanta = 28;
  for (let k = 0; k < numQuanta; k++) {
    const fracK = k / numQuanta;
    const orbU = -U_RANGE + ((fracK + t * 0.08) % 1.0) * 2.0 * U_RANGE;
    const orbV = -V_RANGE + (0.5 + 0.48 * sin(t * 0.55 + fracK * TAU)) * 2.0 * V_RANGE;
    const pt = breatherPoint(orbU, orbV, aaCur, scale_r);

    push();
    translate(pt[0], pt[1], pt[2]);
    fill(hsl((hue + fracK * 0.55 + 0.2) % 1.0, 0.95, 0.78));
    sphere(tube_r * 2.4, 8, 8);
    pop();
  }

  pop();
}
