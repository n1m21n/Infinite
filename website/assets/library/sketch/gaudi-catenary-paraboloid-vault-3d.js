// Gaudi Catenary Paraboloid Vault 3D - Infinite Library (Sketch 3D)
param("arches", 8, 5, 12);
param("vault_height", 1.55, 0.9, 2.2);
param("tilt", 0.36, 0.1, 0.75);
param("orbit", 0.18, 0.0, 0.5);

function draw(t) {
  rotateX(tilt);
  rotateY(t * orbit + 0.4);

  const n = floor(arches);
  const rBase = 1.45;
  const segs = 14;

  // Stone nave floor plinth (#2C241E)
  fill(0.173, 0.141, 0.118);
  push();
  translate(0, -0.78, 0);
  cylinder(rBase * 1.18, 0.12, 24);
  pop();

  // Radial catenary rib arches rising to the central oculus
  for (let i = 0; i < n; i++) {
    const a = (i / n) * TAU;
    for (let s = 0; s < segs; s++) {
      const u0 = s / segs;
      const u1 = (s + 1) / segs;
      const um = (u0 + u1) * 0.5;

      // Inverted catenary profile
      const r0 = rBase * (1.0 - u0 * 0.82);
      const r1 = rBase * (1.0 - u1 * 0.82);
      const rm = (r0 + r1) * 0.5;

      const y0 = -0.72 + vault_height * (1.0 - pow(1.0 - u0, 1.85));
      const y1 = -0.72 + vault_height * (1.0 - pow(1.0 - u1, 1.85));
      const ym = (y0 + y1) * 0.5;

      const dx = r0 - r1;
      const dy = y1 - y0;
      const segLen = sqrt(dx * dx + dy * dy) * 1.25;
      const pitch = -atan2(dy, dx);

      // Warm limestone & terracotta alternation
      if (s % 2 === 0) {
        fill(0.847, 0.761, 0.616); // Warm limestone
      } else {
        fill(0.612, 0.267, 0.133); // Brick terracotta
      }

      push();
      rotateY(a);
      translate(rm, ym, 0);
      rotateZ(pitch);
      box(segLen, 0.08, 0.09);
      pop();
    }
  }

  // Concentric horizontal compression rings & amber stained-glass oculus
  for (let k = 1; k <= 3; k++) {
    const u = k / 4;
    const rr = rBase * (1.0 - u * 0.82);
    const yy = -0.72 + vault_height * (1.0 - pow(1.0 - u, 1.85));
    fill(0.612, 0.267, 0.133);
    push();
    translate(0, yy, 0);
    torus(rr, 0.032, 24, 8);
    pop();
  }

  // Amber clerestory oculus crown
  fill(0.878, 0.565, 0.125);
  push();
  translate(0, -0.72 + vault_height + 0.02, 0);
  torus(rBase * 0.18, 0.048, 24, 10);
  pop();
}
