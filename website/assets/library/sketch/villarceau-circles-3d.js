// Villarceau Circles 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Yvon Villarceau's bitangent circle families (1848) on a torus corresponding to the Hopf fibration fiber bundles.
param("major_r", 1.45, 0.6, 3.0);
param("minor_r", 0.72, 0.2, 1.8);
param("circles", 28, 12, 64);
param("spin", 0.42, 0.0, 2.0);
param("chirality", 1.0, -1.0, 1.0);
param("hue", 0.60, 0.0, 1.0);
param("tube_r", 0.016, 0.005, 0.045);

function villarceauPoint(theta, phi, R, r, sign) {
  // Villarceau diagonal circles lie in planes inclined at angle alpha = arcsin(r / R)
  // Parametric circle on torus of radius R (major) and r (minor):
  // x = (R*cos(theta) - r*sin(theta)*sin(phi)) / (1)
  // Standard bitangent transformation:
  const alpha = asin(clamp(r / R, 0.01, 0.99));
  const cosA = cos(alpha);
  const sinA = sin(alpha);

  // A circle of radius R in plane inclined by alpha, rotated by angle phi around z
  const cx = R * cos(theta);
  const cy = R * sin(theta) * cosA - sign * r * sinA;
  const cz = R * sin(theta) * sinA + sign * r * cosA;

  // Rotate around z by phi
  const cosP = cos(phi);
  const sinP = sin(phi);
  const x = cx * cosP - cy * sinP;
  const y = cx * sinP + cy * cosP;
  const z = cz;

  return [x, y, z];
}

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(0.62 + sin(t * 0.3) * 0.16);
  rotateZ(t * spin * 0.25);

  const R = major_r;
  const r = minor_r;
  const nCircles = floor(circles);
  const ptsPerCircle = 48;

  // Left-handed family of Villarceau circles (sign = +1)
  for (let i = 0; i < nCircles; i++) {
    const frac = i / nCircles;
    const phi = frac * TAU + t * 0.12;
    const h = (hue + frac * 0.5) % 1.0;
    fill(hsl(h, 0.92, 0.64));

    let prev = villarceauPoint(0, phi, R, r, 1.0);
    for (let k = 1; k <= ptsPerCircle; k++) {
      const theta = (k / ptsPerCircle) * TAU;
      const curr = villarceauPoint(theta, phi, R, r, 1.0);
      tube(prev[0], prev[1], prev[2], curr[0], curr[1], curr[2], tube_r, 6);
      prev = curr;
    }
  }

  // Right-handed family of Villarceau circles (sign = -1, if chirality active)
  if (abs(chirality) > 0.2) {
    const oppCount = floor(nCircles * 0.5);
    for (let i = 0; i < oppCount; i++) {
      const frac = i / oppCount;
      const phi = frac * TAU - t * 0.12;
      const h = (hue + 0.35 + frac * 0.4) % 1.0;
      fill(hsl(h, 0.88, 0.72));

      let prev = villarceauPoint(0, phi, R, r, -1.0);
      for (let k = 1; k <= ptsPerCircle; k++) {
        const theta = (k / ptsPerCircle) * TAU;
        const curr = villarceauPoint(theta, phi, R, r, -1.0);
        tube(prev[0], prev[1], prev[2], curr[0], curr[1], curr[2], tube_r * 0.85, 6);
        prev = curr;
      }
    }
  }

  // Orbiting Hopf fiber quanta winding along Villarceau geodesics
  const quanta = 8;
  for (let q = 0; q < quanta; q++) {
    const qPhase = (t * 0.45 + q / quanta) * TAU;
    const phi = (q / quanta) * TAU;
    const pos = villarceauPoint(qPhase, phi, R, r, 1.0);

    push();
    translate(pos[0], pos[1], pos[2]);
    fill(hsl((hue + 0.5 + q / quanta) % 1.0, 0.96, 0.84));
    sphere(0.068, 10);
    pop();
  }

  pop();
}
