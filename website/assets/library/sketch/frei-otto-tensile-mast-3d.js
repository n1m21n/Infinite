// Frei Otto Tensile Mast 3D - Infinite Library (Sketch 3D)
param("gridN", 14, 8, 22);
param("span", 2.5, 1.6, 3.2);
param("sag", 0.55, 0.2, 0.9);
param("spin", 0.18, 0.0, 0.6);

function draw(t) {
  rotateY(t * spin + 0.55);
  rotateX(0.26);

  // Anodized slate plaza deck
  push();
  translate(0, -0.88, 0);
  fill(0.11, 0.13, 0.16);
  box(span * 1.18, 0.08, span * 1.18);
  pop();

  // Four outward-tilted perimeter steel masts with ember tension collars
  const corners = [
    [-1, -1], [1, -1], [1, 1], [-1, 1]
  ];
  for (let c = 0; c < 4; c++) {
    const sx = corners[c][0];
    const sz = corners[c][1];
    push();
    translate(sx * span * 0.44, -0.05, sz * span * 0.44);
    rotateZ(-sx * 0.22);
    rotateX(sz * 0.22);
    fill(0.20, 0.24, 0.29);
    cylinder(0.045, 1.78, 12);

    // Ember anchor collar at masthead
    translate(0, 0.82, 0);
    fill(0.878, 0.353, 0.212);
    cylinder(0.075, 0.10, 12);
    pop();
  }

  // Hyperbolic-paraboloid cable-net ribs (y = sag * (u^2 - v^2))
  const n = floor(gridN);
  for (let i = 0; i < n; i++) {
    const u = (i / (n - 1)) * 2.0 - 1.0;
    for (let j = 0; j < n - 1; j++) {
      const v0 = (j / (n - 1)) * 2.0 - 1.0;
      const v1 = ((j + 1) / (n - 1)) * 2.0 - 1.0;
      const vm = (v0 + v1) * 0.5;
      const y = 0.28 + sag * (u * u - vm * vm);
      const dy = sag * (-(v1 * v1 - v0 * v0));
      const dz = (v1 - v0) * span * 0.44;
      const segLen = sqrt(dz * dz + dy * dy);
      const tilt = atan2(dy, dz);

      push();
      translate(u * span * 0.44, y, vm * span * 0.44);
      rotateX(-tilt);
      fill(i % 2 === 0 ? 0.85 : 0.62, i % 2 === 0 ? 0.88 : 0.68, i % 2 === 0 ? 0.91 : 0.74);
      box(0.028, 0.028, segLen * 1.04);
      pop();
    }
  }

  // Cross-tension ribs along X
  for (let j = 0; j < n; j += 2) {
    const v = (j / (n - 1)) * 2.0 - 1.0;
    for (let i = 0; i < n - 1; i++) {
      const u0 = (i / (n - 1)) * 2.0 - 1.0;
      const u1 = ((i + 1) / (n - 1)) * 2.0 - 1.0;
      const um = (u0 + u1) * 0.5;
      const y = 0.28 + sag * (um * um - v * v);
      const dy = sag * (u1 * u1 - u0 * u0);
      const dx = (u1 - u0) * span * 0.44;
      const segLen = sqrt(dx * dx + dy * dy);
      const tilt = atan2(dy, dx);

      push();
      translate(um * span * 0.44, y, v * span * 0.44);
      rotateZ(tilt);
      fill(0.75, 0.79, 0.84);
      box(segLen * 1.04, 0.022, 0.022);
      pop();
    }
  }
}
