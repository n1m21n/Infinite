// Xenakis Philips Pavilion Shells 3D - Infinite Library (Sketch 3D)
param("ribs", 16, 8, 28);
param("peak_h", 1.85, 1.0, 2.5);
param("twist", 0.35, 0.1, 0.8);
param("spin", 0.18, 0.0, 0.5);

function draw(t) {
  rotateX(0.32 + 0.05 * sin(t * 0.4));
  rotateY(t * spin - 0.55);

  const nR = floor(ribs);

  // Architectural plinth base (#2C2A29)
  fill(0.173, 0.165, 0.161);
  push();
  translate(0, -1.05, 0);
  box(3.4, 0.12, 2.6);
  pop();

  // Three asymmetric hyperbolic paraboloid ruled shells (#EAE3D5)
  const peaks = [
    [-0.65, peak_h * 0.95, -0.35, 1.15],
    [0.55, peak_h * 0.78, 0.25, 0.95],
    [0.05, peak_h * 0.62, -0.85, 0.85]
  ];

  for (let p = 0; p < 3; p++) {
    const px = peaks[p][0];
    const py = peaks[p][1];
    const pz = peaks[p][2];
    const span = peaks[p][3];
    const baseAngle = p * 2.1 + twist;

    for (let i = 0; i < nR; i++) {
      const u = i / max(1, nR - 1);
      const a0 = baseAngle + (u - 0.5) * 1.4;
      const bx = px + cos(a0) * span;
      const bz = pz + sin(a0) * span;

      // Ruled straight-line generator segments from ground edge to apex ridge
      const mx = (bx + px) * 0.5;
      const my = (-0.98 + py) * 0.5;
      const mz = (bz + pz) * 0.5;

      push();
      translate(mx, my, mz);
      rotateY(-a0);
      rotateZ(0.46 + u * 0.18);
      fill(0.918 - u * 0.10, 0.890 - u * 0.10, 0.835 - u * 0.08);
      box(0.032, 2.25, 0.065);
      pop();
    }

    // Terracotta architectural apex beacon (#B85D3B)
    fill(0.722, 0.365, 0.231);
    push();
    translate(px, py * 0.72, pz);
    sphere(0.065);
    pop();
  }

  // Tensioned steel ridge spine collar
  fill(0.722, 0.365, 0.231);
  push();
  translate(0, -0.96, 0);
  torus(1.42, 0.028);
  pop();
}
