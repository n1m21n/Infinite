// Vector Wireframe Gyroscope Icosahedron 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Gimballed 3D vector wireframe gyroscope with concentric rotating orbital rings, nested icosahedral geometric cages, and high-luminance phosphor green vector beams.
param("rings", 6.0, 3.0, 12.0);
param("spin", 0.45, 0.1, 1.2);
param("scale", 1.15, 0.5, 2.0);
param("cage_density", 8.0, 4.0, 16.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.48 + sin(time * 0.2) * 0.06);
  rotateY(time * 0.35 + 0.5);

  // Deep CRT vector monitor plinth (#08140c)
  push();
  translate(0, 1.15, 0);
  fill(0.031, 0.078, 0.047);
  box(2.4 * scale, 0.14, 2.4 * scale);
  pop();

  // 1. Concentric rotating gimbal rings
  const ringCount = floor(rings);
  for (let r = 0; r < ringCount; r++) {
    const rad = (0.7 + r * 0.16) * scale;
    const ang = r * Math.PI / ringCount;
    push();
    rotateY(time * (0.3 + r * 0.1) + ang);
    rotateX(ang * 0.6);

    const segments = 24;
    for (let s = 0; s < segments; s++) {
      const theta = (s / segments) * Math.PI * 2.0;
      push();
      translate(cos(theta) * rad, 0, sin(theta) * rad);
      rotateY(-theta);
      // Phosphor green beam segment (#00ff6a)
      fill(0.0, 1.0, 0.416);
      box(0.04 * scale, 0.04 * scale, (2.0 * Math.PI * rad / segments) * 0.95);
      pop();
    }
    pop();
  }

  // 2. Central rotating nested geometric vector cage
  push();
  rotateY(-time * 0.6);
  rotateZ(time * 0.4);

  // Central white phosphor core junction (#ffffff)
  fill(1.0, 1.0, 1.0);
  box(0.35 * scale, 0.35 * scale, 0.35 * scale);

  // Radiating icosahedral vector cage struts
  const cageCount = floor(cage_density);
  for (let c = 0; c < cageCount; c++) {
    const phi = (c / cageCount) * Math.PI * 2.0;
    push();
    rotateY(phi);
    rotateX(0.55);

    // Green vector strut
    fill(0.0, 1.0, 0.416);
    translate(0, 0.35 * scale, 0);
    box(0.06 * scale, 0.7 * scale, 0.06 * scale);

    // White apex vector pip
    translate(0, 0.38 * scale, 0);
    fill(1.0, 1.0, 1.0);
    box(0.12 * scale, 0.12 * scale, 0.12 * scale);
    pop();
  }
  pop();

  // 3. Axial phosphor calibration antennas
  for (let a = -1; a <= 1; a += 2) {
    push();
    translate(0, a * 0.95 * scale, 0);
    fill(0.0, 1.0, 0.416);
    box(0.05, 0.35 * scale, 0.05);
    pop();
  }

  pop();
}
