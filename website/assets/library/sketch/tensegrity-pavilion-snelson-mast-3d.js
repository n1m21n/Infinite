// Tensegrity Pavilion Snelson Mast 3D - Infinite Library (Sketch 3D)
param("stages", 3, 2, 5);
param("twist_deg", 36, 15, 65);
param("sway", 0.08, 0.0, 0.25);
param("orbit", 0.25, 0.0, 1.0);

function draw(t) {
  const nStages = floor(stages);
  const twist = (twist_deg * PI) / 180.0;
  rotateY(t * orbit + 0.35);

  // Dark architectural plinth (#222830)
  push();
  translate(0, -1.38, 0);
  fill(0.133, 0.157, 0.188);
  cylinder(1.15, 0.14);
  pop();

  // Base collar ring (#6E7885)
  push();
  translate(0, -1.28, 0);
  fill(0.431, 0.471, 0.522);
  torus(0.76, 0.025);
  pop();

  const stageH = 0.72;
  const rad = 0.68;

  for (let s = 0; s < nStages; s++) {
    const y0 = -1.2 + s * stageH;
    const baseAngle = s * twist + sin(t * 1.1 + s) * sway;
    const stageRad = rad * (1.0 - s * 0.10);

    // 3 compression struts per tensegrity prism
    for (let i = 0; i < 3; i++) {
      const a0 = baseAngle + (i * TAU) / 3.0;
      const a1 = baseAngle + ((i + 1) * TAU) / 3.0 + twist * 0.85;
      const x0 = cos(a0) * stageRad;
      const z0 = sin(a0) * stageRad;
      const x1 = cos(a1) * stageRad * 0.92;
      const z1 = sin(a1) * stageRad * 0.92;

      const mx = (x0 + x1) * 0.5;
      const my = y0 + stageH * 0.52;
      const mz = (z0 + z1) * 0.5;
      const dx = x1 - x0;
      const dy = stageH * 1.04;
      const dz = z1 - z0;
      const len = Math.sqrt(dx * dx + dy * dy + dz * dz);

      // Brushed steel compression strut (#B8C0C8)
      push();
      translate(mx, my, mz);
      rotateY(-atan2(dz, dx));
      rotateZ(atan2(Math.sqrt(dx * dx + dz * dz), dy));
      fill(0.722, 0.753, 0.784);
      cylinder(0.042, len);
      pop();

      // End cap nodes (#222830)
      push();
      translate(x0, y0, z0);
      fill(0.133, 0.157, 0.188);
      sphere(0.055);
      pop();

      // Vertical tension cable (#6E7885)
      push();
      translate(x0 * 0.96, y0 + stageH * 0.5, z0 * 0.96);
      fill(0.431, 0.471, 0.522);
      cylinder(0.008, stageH);
      pop();
    }

    // Horizontal tension ring per stage
    push();
    translate(0, y0 + stageH, 0);
    fill(0.431, 0.471, 0.522);
    torus(stageRad * 0.92, 0.012);
    pop();
  }
}
