// Muqarnas Abencerrajes Star Dome 3D - Infinite Library (Sketch 3D)
param("tiers", 6, 4, 8);
param("symmetry", 16, 8, 24);
param("corbel_step", 0.22, 0.12, 0.34);
param("orbit", 0.22, 0.0, 0.6);

function draw(t) {
  rotateX(0.55 + 0.08 * sin(t * 0.35));
  rotateY(t * orbit + 0.35);

  const nT = floor(tiers);
  const nS = floor(symmetry);

  // Octagonal limestone plinth base (#1A1410 / #C6934B)
  fill(0.22, 0.17, 0.13);
  push();
  translate(0, -1.05, 0);
  cylinder(1.95, 0.14);
  pop();

  // Concentric corbelled muqarnas stalactite tiers
  for (let tier = 0; tier < nT; tier++) {
    const u = tier / max(1, nT - 1);
    const radius = 1.65 * (1.0 - u * 0.72);
    const yPos = -0.82 + tier * corbel_step * 1.45;
    const count = max(8, floor(nS * (1.0 - u * 0.35)));
    const phaseOff = (tier % 2) * (PI / count);

    for (let i = 0; i < count; i++) {
      const a = (i / count) * TAU + phaseOff;
      const x = cos(a) * radius;
      const z = sin(a) * radius;

      // Warm Alhambra limestone gold & terracotta-ochre shading per tier
      const lum = 0.56 + 0.28 * (1.0 - u) + 0.08 * sin(i * 1.7);
      fill(lum * 0.96, lum * 0.74, lum * 0.42);

      push();
      translate(x, yPos, z);
      rotateY(-a);

      // Corbelled niche bracket & hanging stalactite prism
      box(0.24 * (1.0 - u * 0.3), 0.26, 0.28 * (1.0 - u * 0.25));

      // Hanging pendant muqarnas drop
      fill(lum * 0.82, lum * 0.60, lum * 0.32);
      translate(0, -0.16, 0.08);
      box(0.11 * (1.0 - u * 0.25), 0.18, 0.11 * (1.0 - u * 0.25));
      pop();
    }
  }

  // Clerestory lantern ring & glowing apex star
  fill(0.95, 0.86, 0.65);
  push();
  translate(0, -0.82 + nT * corbel_step * 1.45 + 0.05, 0);
  torus(0.38, 0.055);
  pop();

  // Radial star ribs at apex
  fill(0.78, 0.58, 0.30);
  for (let r = 0; r < 8; r++) {
    push();
    translate(0, -0.82 + nT * corbel_step * 1.45 + 0.08, 0);
    rotateY((r / 8.0) * PI);
    box(0.76, 0.05, 0.045);
    pop();
  }
}
