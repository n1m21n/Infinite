// Frei Otto Tensile Mast 3D - Infinite Library (Sketch 3D)
param("cables", 18, 10, 28);
param("saddle_amp", 0.55, 0.2, 0.9);
param("spin", 0.14, 0.0, 0.4);

function draw(t) {
  const n = floor(cables);
  rotateY(t * spin + 0.58);
  rotateX(0.32);

  // Dark slate architectural plinth
  push();
  translate(0, -1.05, 0);
  fill(0.14, 0.16, 0.18);
  box(3.1, 0.08, 3.1);
  pop();

  // Four tilted steel compression masts & cast clevis rings
  for (let m = 0; m < 4; m++) {
    const ang = m * (PI * 0.5) + PI * 0.25;
    const mx = cos(ang) * 1.15;
    const mz = sin(ang) * 1.15;
    push();
    translate(mx, -0.1, mz);
    rotateY(-ang);
    rotateZ(0.22);
    fill(0.45, 0.48, 0.52);
    cylinder(0.055, 2.05);
    pop();

    // Cast steel clevis node at mast head
    push();
    translate(mx * 0.84, 0.82, mz * 0.84);
    fill(0.78, 0.80, 0.82);
    sphere(0.11);
    pop();
  }

  // Anticlastic hyperbolic-paraboloid cable-net canopy
  const span = 2.15;
  for (let i = 0; i < n; i++) {
    const u = (i / (n - 1)) * 2.0 - 1.0;
    for (let j = 0; j < n; j++) {
      const v = (j / (n - 1)) * 2.0 - 1.0;
      const x = u * span * 0.55;
      const z = v * span * 0.55;
      const y = 0.22 + saddle_amp * (u * u - v * v) * 0.65 + 0.04 * sin(t * 0.8 + u * 3.0);

      // Translucent acrylic membrane tile
      push();
      translate(x, y, z);
      rotateZ(saddle_amp * u * 0.55);
      rotateX(-saddle_amp * v * 0.55);
      const shade = 0.72 + 0.18 * (u * u - v * v);
      fill(shade * 0.90, shade * 0.93, shade * 0.96);
      box(0.10, 0.018, 0.10);
      pop();

      // Tensioned steel cable clamp nodes
      if ((i + j) % 2 === 0) {
        push();
        translate(x, y + 0.015, z);
        fill(0.22, 0.24, 0.27);
        box(0.032, 0.032, 0.032);
        pop();
      }
    }
  }
}
