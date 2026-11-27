// Gaudi Hyperbolic Vault 3D - Infinite Library (Sketch 3D)
param("columns", 6, 4, 10);
param("rib_tiers", 14, 8, 22);
param("flare", 0.65, 0.3, 1.1);
param("spin", 0.12, 0.0, 0.4);

function draw(t) {
  const nc = floor(columns);
  const nt = floor(rib_tiers);
  rotateY(t * spin + 0.35);
  rotateX(-0.28);

  // Central amber-gold oculus star rings
  for (let r = 0; r < 3; r++) {
    push();
    translate(0, 1.05 - r * 0.12, 0);
    rotateX(PI * 0.5);
    fill(0.86 - r * 0.08, 0.55 - r * 0.05, 0.12);
    torus(0.28 + r * 0.22, 0.032);
    pop();
  }

  // Branching fluted limestone columns & hyperbolic paraboloid star ribs
  for (let c = 0; c < nc; c++) {
    const ang = (c / nc) * TAU;
    const bx = cos(ang) * 0.92;
    const bz = sin(ang) * 0.92;

    // Fluted column shaft
    push();
    translate(bx, -0.55, bz);
    rotateZ(cos(ang) * 0.08);
    rotateX(-sin(ang) * 0.08);
    fill(0.76, 0.74, 0.70);
    cylinder(0.11, 1.35);
    pop();

    // Ellipsoid capital node
    push();
    translate(bx * 0.92, 0.15, bz * 0.92);
    fill(0.84, 0.68, 0.36);
    sphere(0.14);
    pop();

    // Radiating hyperbolic rib vault branches
    for (let k = 0; k < nt; k++) {
      const u = k / (nt - 1);
      const spread = (u - 0.5) * 1.4;
      const rx = bx * 0.88 * (1.0 - u * 0.62) + cos(ang + spread * flare) * u * 0.55;
      const rz = bz * 0.88 * (1.0 - u * 0.62) + sin(ang + spread * flare) * u * 0.55;
      const ry = 0.20 + sin(u * PI * 0.5) * 0.85;

      push();
      translate(rx, ry, rz);
      rotateY(-ang - spread * 0.5);
      rotateZ(0.45 * (1.0 - u));
      const warm = 0.68 + 0.26 * u;
      fill(warm, warm * 0.96, warm * 0.90);
      box(0.08, 0.16, 0.22);
      pop();
    }
  }
}
