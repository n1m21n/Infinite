// Hyperbolic Helicoid 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Bonnet isometric transformation morphing continuously between a catenoid and helicoid.
param("morph", 0.5, 0.0, 1.0);
param("turns", 2.2, 1.0, 4.0);
param("radius", 1.1, 0.4, 2.5);
param("tube_r", 0.03, 0.01, 0.1);
param("spin", 0.5, 0.0, 3.0);

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(0.45 + sin(t * 0.3) * 0.15);
  rotateZ(t * spin * 0.3);

  // Bonnet isometric angle theta interpolating catenoid (0) and helicoid (PI/2)
  const theta = (morph + 0.25 * sin(t * 0.8)) * (PI * 0.5);
  const cosT = cos(theta);
  const sinT = sin(theta);

  const uSteps = 36;
  const vSteps = 16;
  const uRange = turns * PI;
  const vRange = 1.35;

  for (let j = 0; j <= vSteps; j++) {
    const v = -vRange + (j / vSteps) * 2.0 * vRange;
    const sinhV = (exp(v) - exp(-v)) * 0.5;
    const coshV = (exp(v) + exp(-v)) * 0.5;

    const rowPts = [];
    for (let i = 0; i <= uSteps; i++) {
      const u = -uRange + (i / uSteps) * 2.0 * uRange;

      // Bonnet isometric representation equations
      const x = (cosT * sinhV * sin(u) + sinT * coshV * cos(u)) * radius;
      const y = (-cosT * sinhV * cos(u) + sinT * coshV * sin(u)) * radius;
      const z = (u * cosT + v * sinT) * (radius * 0.45);

      rowPts.push([x, y, z]);
    }

    const frac = j / vSteps;
    // Brand gradient: signal blue to ember orange
    const hue = (0.62 + 0.42 * frac) % 1.0;
    fill(hsl(hue, 0.85, 0.58));

    for (let i = 0; i < uSteps; i++) {
      const p1 = rowPts[i];
      const p2 = rowPts[i + 1];
      tube(p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], tube_r, 8);
    }
  }

  pop();
}
