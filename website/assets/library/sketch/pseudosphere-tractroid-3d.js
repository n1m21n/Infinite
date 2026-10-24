// Pseudosphere Tractroid 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Constant negative Gaussian curvature tractroid surface with parametric meridian ribs and orbiting equatorial satellites.
param("height", 1.8, 0.6, 2.5);
param("radius", 0.95, 0.3, 1.8);
param("ribs", 28, 8, 44);
param("spin", 0.5, 0.0, 3.0);
param("hue", 0.82, 0.0, 1.0);

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(0.45 + sin(t * 0.3) * 0.15);
  rotateZ(cos(t * 0.25) * 0.1);

  const numRibs = floor(ribs);
  const stepsU = 36;
  const uMax = 2.4;

  // Tractroid surface parametric equation:
  // x = sech(u) * cos(v), y = u - tanh(u), z = sech(u) * sin(v)
  // Both top horn (u > 0) and bottom horn (u < 0)
  for (let sign = -1; sign <= 1; sign += 2) {
    for (let r = 0; r < numRibs; r++) {
      const v = (r / numRibs) * TAU;
      const fracRib = r / numRibs;
      fill(hsl((hue + fracRib * 0.35) % 1.0, 0.78, 0.52));

      let prevX = 0, prevY = 0, prevZ = 0;
      for (let s = 0; s <= stepsU; s++) {
        const u = 0.05 + (s / stepsU) * (uMax - 0.05);
        // sech(u) = 1 / cosh(u)
        const coshU = (Math.exp(u) + Math.exp(-u)) * 0.5;
        const sechU = 1.0 / coshU;
        const tanhU = (Math.exp(u) - Math.exp(-u)) / (Math.exp(u) + Math.exp(-u));

        const px = radius * sechU * cos(v);
        const py = sign * height * (u - tanhU);
        const pz = radius * sechU * sin(v);

        if (s > 0) {
          tube(prevX, prevY, prevZ, px, py, pz, 0.022 * (1.0 - (s / stepsU) * 0.5), 8);
        }
        prevX = px;
        prevY = py;
        prevZ = pz;
      }
    }
  }

  // Equatorial connecting ring
  const ringPts = 48;
  for (let i = 0; i < ringPts; i++) {
    const v1 = (i / ringPts) * TAU;
    const v2 = ((i + 1) / ringPts) * TAU;
    const x1 = radius * cos(v1);
    const z1 = radius * sin(v1);
    const x2 = radius * cos(v2);
    const z2 = radius * sin(v2);
    fill(hsl((hue + 0.5) % 1.0, 0.85, 0.65));
    tube(x1, 0, z1, x2, 0, z2, 0.045, 10);
  }

  // Orbiting satellite beads on geodesic spiral paths
  const satellites = 8;
  for (let s = 0; s < satellites; s++) {
    const orbitT = (t * 0.4 + s / satellites) % 1.0;
    const uOrb = (orbitT * 2.0 - 1.0) * 1.8;
    const signOrb = uOrb >= 0 ? 1 : -1;
    const absU = Math.max(0.05, Math.abs(uOrb));
    const coshU = (Math.exp(absU) + Math.exp(-absU)) * 0.5;
    const sechU = 1.0 / coshU;
    const tanhU = (Math.exp(absU) - Math.exp(-absU)) / (Math.exp(absU) + Math.exp(-absU));

    const vOrb = orbitT * TAU * 3.0;
    const sx = radius * sechU * cos(vOrb);
    const sy = signOrb * height * (absU - tanhU);
    const sz = radius * sechU * sin(vOrb);

    push();
    translate(sx, sy, sz);
    fill(1.0, 0.42, 0.21); // Ember bead
    sphere(0.065, 10);
    pop();
  }

  pop();
}
