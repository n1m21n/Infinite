// Moholy Kinetic Modulator 3D - Infinite Library (Sketch 3D)
param("rings", 4, 2, 7);
param("rods", 12, 6, 20);
param("spin", 0.24, 0.0, 0.8);

function draw(t) {
  const nR = floor(rings);
  const nRods = floor(rods);

  push();
  translate(0, -0.08, 0);
  scale(0.80);
  rotateX(-0.20);
  rotateY(0.35 + t * spin);

  // Dark matte-iron mechanical plinth frame
  for (let c = 0; c < 4; c++) {
    const sx = (c % 2 === 0 ? -1 : 1) * 0.76;
    const sz = (c < 2 ? -1 : 1) * 0.76;
    push();
    translate(sx, -0.85, sz);
    fill(0.12, 0.13, 0.16);
    box(0.06, 0.62, 0.06);
    pop();
  }
  push();
  translate(0, -1.14, 0);
  fill(0.14, 0.15, 0.18);
  box(1.58, 0.05, 1.58);
  pop();

  // Horizontal brushed-steel base & crown collector plates
  for (let p = 0; p < 2; p++) {
    push();
    translate(0, p === 0 ? -0.54 : 0.92, 0);
    fill(0.78, 0.80, 0.84);
    cylinder(0.84, 0.035);
    pop();
  }

  // Counter-rotating concentric perforated kinetic rings
  for (let i = 0; i < nR; i++) {
    const u = i / max(1, nR - 1);
    const y = -0.34 + u * 1.05;
    const rad = 0.72 - u * 0.18;
    push();
    translate(0, y, 0);
    rotateY(t * spin * (i % 2 === 0 ? 1.4 : -1.1) + i * 0.5);
    rotateX(0.16 * sin(i * 1.3 + t * 0.3));
    fill(i % 2 === 0 ? 0.82 : 0.66, i % 2 === 0 ? 0.84 : 0.70, i % 2 === 0 ? 0.87 : 0.75);
    torus(rad, 0.032);
    pop();
  }

  // Vertical polished-steel tension rods & warm tungsten accent fin
  for (let r = 0; r < nRods; r++) {
    const a = (r / nRods) * TAU;
    const rad = 0.68;
    push();
    translate(cos(a) * rad, 0.19, sin(a) * rad);
    fill(r % 4 === 0 ? 0.910 : 0.76, r % 4 === 0 ? 0.659 : 0.79, r % 4 === 0 ? 0.314 : 0.83);
    cylinder(0.018, 1.44);
    pop();
  }

  // Central helical light-prop spire
  for (let k = 0; k < 18; k++) {
    const u = k / 17.0;
    const a = u * TAU * 2.5 + t * spin * 1.8;
    push();
    translate(cos(a) * 0.18, -0.45 + u * 1.28, sin(a) * 0.18);
    rotateY(a);
    fill(0.910, 0.659, 0.314);
    box(0.24, 0.035, 0.08);
    pop();
  }

  pop();
}
