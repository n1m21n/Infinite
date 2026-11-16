// Origami Folded Vault Miura Shell 3D - Infinite Library (Sketch 3D)
param("bays", 9, 5, 14);
param("courses", 7, 4, 11);
param("fold_angle", 0.52, 0.2, 0.85);
param("orbit", 0.22, 0.0, 1.0);

function draw(t) {
  rotateY(t * orbit + 0.48);
  rotateX(-0.18);

  // Charcoal slate architectural plinth (#1B1D20)
  push();
  translate(0, -1.05, 0);
  fill(0.106, 0.114, 0.125);
  box(3.2, 0.12, 2.2);
  pop();

  // Brass longitudinal tie rods along base eaves (#C7A254)
  for (let side = -1; side <= 1; side += 2) {
    push();
    translate(0, -0.82, side * 0.92);
    rotateZ(PI * 0.5);
    fill(0.780, 0.635, 0.329);
    cylinder(0.018, 2.85);
    pop();
  }

  const nBays = floor(bays);
  const nCourses = floor(courses);
  const breath = fold_angle + 0.08 * sin(t * 1.1);
  const spanX = 2.65;
  const stepX = spanX / nBays;

  // Curved Miura-ori folded barrel vault
  for (let ix = 0; ix < nBays; ix++) {
    const ux = (ix + 0.5) / nBays - 0.5;
    const xBase = ux * spanX;
    for (let iz = 0; iz < nCourses; iz++) {
      const uz = (iz + 0.5) / nCourses - 0.5;
      const arcAng = uz * 2.15;
      const archR = 1.02;
      const y = -0.85 + cos(arcAng) * archR + ((ix + iz) % 2 === 0 ? 0.06 : -0.06);
      const z = sin(arcAng) * archR;
      const zigX = (iz % 2 === 0 ? 1 : -1) * 0.045;
      const tiltX = (ix % 2 === 0 ? 1 : -1) * breath;

      push();
      translate(xBase + zigX, y, z);
      rotateX(-arcAng * 0.85);
      rotateZ(tiltX);
      if ((ix + iz) % 2 === 0) {
        fill(0.918, 0.902, 0.871); // warm travertine facet
      } else {
        fill(0.680, 0.660, 0.625); // shaded limestone facet
      }
      box(stepX * 1.32, 0.036, 0.34);
      pop();
    }

    // Eave support piers connecting canopy edge to brass tie rods
    for (let side = -1; side <= 1; side += 2) {
      push();
      translate(xBase, -0.82, side * 0.92);
      fill(0.220, 0.228, 0.242);
      box(0.05, 0.38, 0.05);
      pop();
    }

    // Brass ridge pin at crown
    push();
    translate(xBase, -0.85 + 1.08, 0);
    fill(0.780, 0.635, 0.329);
    sphere(0.026);
    pop();
  }
}
