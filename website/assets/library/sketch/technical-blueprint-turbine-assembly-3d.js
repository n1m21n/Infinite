// Technical Blueprint Turbine Assembly 3D - Infinite Library (Sketch 3D)
param("blade_count", 18, 8, 32);
param("stage_count", 3, 2, 5);
param("blade_pitch", 0.45, 0.1, 0.9);
param("spin_rate", 0.35, 0.0, 1.2);

function draw(t) {
  rotateX(0.42);
  rotateY(t * spin_rate + 0.5);

  const nb = floor(blade_count);
  const ns = floor(stage_count);

  // Central drive shaft
  push();
  fill(0.847, 0.925, 1.0);
  cylinder(0.14, 2.5, 24);
  pop();

  // Outer blueprint calibration ring
  push();
  fill(0.357, 0.753, 0.745);
  torus(1.48, 0.022, 40, 10);
  pop();

  // Axial rotor stages
  for (let s = 0; s < ns; s++) {
    const sy = (s / (ns - 1) - 0.5) * 1.3;
    const rad = 0.88 - s * 0.12;

    // Hub collar
    push();
    translate(0, sy, 0);
    fill(0.357, 0.753, 0.745);
    cylinder(0.28, 0.16, 24);
    pop();

    // Shroud ring
    push();
    translate(0, sy, 0);
    rotateX(Math.PI * 0.5);
    fill(0.847, 0.925, 1.0);
    torus(rad + 0.22, 0.02, 36, 8);
    pop();

    // Radial turbine blades
    for (let b = 0; b < nb; b++) {
      const ang = (b / nb) * Math.PI * 2.0 + s * 0.22;
      const bx = Math.cos(ang) * (rad * 0.65);
      const bz = Math.sin(ang) * (rad * 0.65);
      push();
      translate(bx, sy, bz);
      rotateY(-ang);
      rotateX(blade_pitch);
      if (b % 3 === 0) fill(0.357, 0.753, 0.745);
      else fill(0.847, 0.925, 1.0);
      box(rad * 0.72, 0.035, 0.14);
      pop();
    }
  }
}
