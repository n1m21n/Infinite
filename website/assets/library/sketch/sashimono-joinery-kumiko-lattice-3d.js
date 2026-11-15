// Sashimono Joinery Kumiko Lattice 3D - Infinite Library (Sketch 3D)
param("lattice_cells", 4, 3, 6);
param("rib_depth", 0.16, 0.08, 0.28);
param("tilt_angle", 0.44, 0.1, 1.0);
param("spin_speed", 0.22, 0.0, 0.8);

function draw(t) {
  rotateX(tilt_angle);
  rotateY(t * spin_speed + 0.58);

  const nc = floor(lattice_cells);
  const span = 2.2;
  const step = span / nc;
  const half = span * 0.5;

  // Outer mitered charred sugi frame (#2B2118)
  fill(0.169, 0.129, 0.094);
  const fThick = 0.11;
  const fDepth = rib_depth * 1.45;
  push(); translate(0, half, 0); box(span + fThick, fThick, fDepth); pop();
  push(); translate(0, -half, 0); box(span + fThick, fThick, fDepth); pop();
  push(); translate(-half, 0, 0); box(fThick, span - fThick, fDepth); pop();
  push(); translate(half, 0, 0); box(fThick, span - fThick, fDepth); pop();

  // Brass corner joinery dowels (#C89D66)
  fill(0.784, 0.616, 0.400);
  for (let sx = -1; sx <= 1; sx += 2) {
    for (let sy = -1; sy <= 1; sy += 2) {
      push();
      translate(sx * half, sy * half, 0);
      rotateX(1.5708);
      cylinder(0.032, fDepth * 1.18, 14);
      pop();
    }
  }

  // Horizontal and vertical half-lap hinoki cypress ribs
  fill(0.84, 0.72, 0.54);
  for (let i = 1; i < nc; i++) {
    const p = -half + i * step;
    push();
    translate(p, 0, 0.015);
    box(0.035, span - fThick * 0.5, rib_depth);
    pop();

    push();
    translate(0, p, -0.015);
    box(span - fThick * 0.5, 0.035, rib_depth);
    pop();
  }

  // Diagonal asanoha (hemp-leaf) cedar infill ribs (#8C5A32)
  fill(0.549, 0.353, 0.196);
  const diagLen = step * 1.38;
  for (let ix = 0; ix < nc; ix++) {
    for (let iy = 0; iy < nc; iy++) {
      const cx = -half + (ix + 0.5) * step;
      const cy = -half + (iy + 0.5) * step;
      push();
      translate(cx, cy, 0);
      rotateZ(0.7854);
      box(diagLen, 0.024, rib_depth * 0.72);
      pop();

      push();
      translate(cx, cy, 0);
      rotateZ(-0.7854);
      box(diagLen, 0.024, rib_depth * 0.72);
      pop();
    }
  }
}
