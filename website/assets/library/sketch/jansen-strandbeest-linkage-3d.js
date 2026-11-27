// Jansen Strandbeest Linkage 3D - Infinite Library (Sketch 3D)
param("legs", 8, 4, 14);
param("stride", 0.55, 0.25, 0.9);
param("speed", 1.4, 0.2, 3.0);
param("spin", 0.18, 0.0, 0.6);

function draw(t) {
  const n = floor(legs);
  rotateY(t * spin + 0.52);
  rotateX(0.18);

  // Central spine axle
  push();
  fill(0.137, 0.149, 0.180);
  box(0.12, 0.12, n * 0.34);
  pop();

  for (let i = 0; i < n; i++) {
    const z = (i - (n - 1) * 0.5) * 0.32;
    const phase = t * speed + (i * TAU) / n;
    const crankX = cos(phase) * 0.28 * stride;
    const crankY = sin(phase) * 0.28 * stride + 0.25;

    for (let side = -1; side <= 1; side += 2) {
      const xOff = side * 0.42;
      const kneeX = xOff + crankX * 0.8 + side * 0.45;
      const kneeY = crankY - 0.35 + 0.18 * sin(phase + side);
      const footX = xOff + side * 0.38 - crankX * 0.6;
      const footY = -0.95 + max(0.0, sin(phase + 0.8)) * 0.28;

      // Upper strut (spine to crank)
      push();
      translate((xOff + crankX) * 0.5, (0.25 + crankY) * 0.5, z);
      rotateZ(atan2(crankY - 0.25, crankX - xOff));
      fill(0.902, 0.871, 0.784);
      box(0.55, 0.055, 0.055);
      pop();

      // Outer triangular truss strut (crank to knee)
      push();
      translate((crankX + kneeX) * 0.5, (crankY + kneeY) * 0.5, z);
      rotateZ(atan2(kneeY - crankY, kneeX - crankX));
      fill(0.784, 0.722, 0.604);
      box(0.75, 0.05, 0.05);
      pop();

      // Lower walking shin (knee to foot)
      push();
      translate((kneeX + footX) * 0.5, (kneeY + footY) * 0.5, z);
      rotateZ(atan2(footY - kneeY, footX - kneeX));
      fill(0.902, 0.871, 0.784);
      box(0.88, 0.048, 0.048);
      pop();

      // Graphite pivot joints
      push();
      translate(kneeX, kneeY, z);
      fill(0.137, 0.149, 0.180);
      sphere(0.065);
      pop();

      push();
      translate(footX, footY, z);
      fill(0.545, 0.271, 0.075);
      box(0.11, 0.06, 0.11);
      pop();
    }
  }
}
