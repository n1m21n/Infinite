// Catenoid-Helicoid 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Continuous isometric Bonnet transformation between catenoid and helicoid minimal surfaces.
param("scale", 0.65, 0.2, 1.8);
param("tube_r", 0.024, 0.01, 0.08);
param("morph_speed", 0.5, 0.0, 2.0);
param("spin", 0.28, 0.0, 1.5);
param("rings", 28, 12, 60);
param("segments", 44, 16, 80);
param("hue", 0.08, 0.0, 1.0);

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(0.45);

  // Bonnet isometric deformation parameter alpha oscillating between 0 (helicoid) and PI/2 (catenoid)
  const alpha = (sin(t * morph_speed) * 0.5 + 0.5) * (Math.PI * 0.5);
  const cosA = cos(alpha);
  const sinA = sin(alpha);

  const uSteps = rings;
  const vSteps = segments;
  const uMin = -Math.PI, uMax = Math.PI;
  const vMin = -1.6, vMax = 1.6;

  for (let i = 0; i <= uSteps; i++) {
    const u = uMin + (i / uSteps) * (uMax - uMin);
    const pts = [];

    for (let j = 0; j <= vSteps; j++) {
      const v = vMin + (j / vSteps) * (vMax - vMin);

      // Bonnet isometric associate minimal surface family
      const sinhV = sinh(v);
      const coshV = cosh(v);
      const sinU = sin(u);
      const cosU = cos(u);

      const x = cosA * sinhV * sinU + sinA * coshV * cosU;
      const y = -cosA * sinhV * cosU + sinA * coshV * sinU;
      const z = u * cosA + v * sinA;

      pts.push([x * scale, z * scale, y * scale]);
    }

    const frac = i / uSteps;
    fill(hsl((hue + frac * 0.55) % 1.0, 0.84, 0.54));
    for (let j = 0; j < vSteps; j++) {
      const p1 = pts[j], p2 = pts[j + 1];
      tube(p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], tube_r, 8);
    }
  }

  pop();
}
