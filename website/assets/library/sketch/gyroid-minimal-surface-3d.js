// Gyroid Minimal Surface 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Alan Schoen's triply periodic minimal surface (TPMS) with interwoven enantiomorph labyrinth channels and orbiting quanta.
param("periods", 2.0, 1.0, 4.0);
param("scale", 1.25, 0.4, 2.5);
param("ribs", 36, 16, 72);
param("spin", 0.35, 0.0, 1.5);
param("iso_level", 0.0, -0.6, 0.6);
param("hue", 0.65, 0.0, 1.0);
param("tube_r", 0.018, 0.006, 0.05);

function gyroidVal(x, y, z) {
  // TPMS Gyroid nodal equation: sin(x)*cos(y) + sin(y)*cos(z) + sin(z)*cos(x) = iso
  return sin(x) * cos(y) + sin(y) * cos(z) + sin(z) * cos(x);
}

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(0.55 + sin(t * 0.28) * 0.18);
  rotateZ(t * spin * 0.35);

  const nPer = periods;
  const ribCount = floor(ribs);
  const uSteps = 40;
  const iso = iso_level + sin(t * 1.2) * 0.15;
  const box = scale;

  // Helical labyrinth skeleton ribbons along the constant-z slices
  for (let i = 0; i < ribCount; i++) {
    const fracZ = (i / (ribCount - 1) - 0.5) * 2.0;
    const z = fracZ * box * PI * (nPer / 2.0);
    const zNorm = fracZ * box;
    const h = (hue + (i / ribCount) * 0.45) % 1.0;
    fill(hsl(h, 0.88, 0.62));

    let prev = null;
    for (let j = 0; j <= uSteps; j++) {
      const fracU = (j / uSteps - 0.5) * 2.0;
      const x = fracU * box * PI * (nPer / 2.0);

      // Solve for y on the gyroid surface: sin(x)*cos(y) + sin(y)*cos(z) = iso - sin(z)*cos(x)
      // Linearized trigonometric root approximation
      const cZ = cos(z);
      const sX = sin(x);
      const target = iso - sin(z) * cos(x);
      const amp = sqrt(sX * sX + cZ * cZ);
      const phase = atan2(cZ, sX);
      const clampedRatio = clamp(target / max(0.001, amp), -0.99, 0.99);
      const yAngle = asin(clampedRatio) - phase;

      const px = fracU * box;
      const py = (yAngle / (PI * nPer * 0.5)) * box;
      const pz = zNorm;

      if (prev && abs(py - prev[1]) < box * 0.45) {
        tube(prev[0], prev[1], prev[2], px, py, pz, tube_r, 6);
      }
      prev = [px, py, pz];
    }
  }

  // Cross-strut orthogonal helical rings through labyrinth pores
  const poreRings = 12;
  for (let r = 0; r < poreRings; r++) {
    const anglePore = (r / poreRings) * TAU;
    const ringZ = sin(anglePore * 2.0 + t * 0.5) * box * 0.65;
    const ringRadius = box * 0.45 * (0.8 + 0.2 * cos(anglePore * 3.0));
    const h = (hue + 0.35 + (r / poreRings) * 0.3) % 1.0;
    fill(hsl(h, 0.94, 0.68));

    const ringSteps = 32;
    let prevPt = null;
    for (let k = 0; k <= ringSteps; k++) {
      const a = (k / ringSteps) * TAU;
      const px = cos(a) * ringRadius;
      const py = sin(a) * ringRadius;
      const pz = ringZ + sin(a * 3.0) * box * 0.12;

      if (prevPt) {
        tube(prevPt[0], prevPt[1], prevPt[2], px, py, pz, tube_r * 1.1, 6);
      }
      prevPt = [px, py, pz];
    }
  }

  // Drifting quanta through Schoen's interwoven labyrinth channels
  const quanta = 10;
  for (let q = 0; q < quanta; q++) {
    const qPhase = (t * 0.4 + q / quanta) * TAU;
    const qx = cos(qPhase) * box * 0.72;
    const qy = sin(qPhase * 2.0) * box * 0.55;
    const qz = cos(qPhase * 3.0) * box * 0.65;

    push();
    translate(qx, qy, qz);
    fill(hsl((hue + 0.55 + q / quanta) % 1.0, 0.96, 0.82));
    sphere(0.062, 10);
    pop();
  }

  pop();
}
