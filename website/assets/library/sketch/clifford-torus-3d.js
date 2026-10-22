// Clifford Torus 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// 4D Clifford Torus stereographically projected into 3D with 4D isometric rotation.
param("radius", 1.3, 0.5, 2.8);
param("tube_r", 0.03, 0.01, 0.1);
param("rot4d", 0.5, 0.0, 2.5);
param("spin", 0.35, 0.0, 2.0);
param("hue", 0.05, 0.0, 1.0);

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(0.5);

  const circles = 28;
  const segs = 48;
  const angle4D = t * rot4d;
  const c4 = cos(angle4D), s4 = sin(angle4D);

  for (let c = 0; c < circles; c++) {
    const u = (c / circles) * TAU;
    const pts = [];

    for (let i = 0; i <= segs; i++) {
      const v = (i / segs) * TAU;
      // 4D flat torus coordinates on S3:
      const x0 = cos(u) * 0.7071;
      const y0 = sin(u) * 0.7071;
      const z0 = cos(v) * 0.7071;
      const w0 = sin(v) * 0.7071;

      // 4D rotation in XW and YZ planes
      const x4 = x0 * c4 - w0 * s4;
      const w4 = x0 * s4 + w0 * c4;
      const y4 = y0 * c4 - z0 * s4;
      const z4 = y0 * s4 + z0 * c4;

      // Stereographic projection from 4D S3 into 3D
      const denom = 1.6 - w4;
      const px = (x4 / denom) * radius;
      const py = (y4 / denom) * radius;
      const pz = (z4 / denom) * radius;
      pts.push([px, py, pz]);
    }

    const frac = c / circles;
    fill(hsl((hue + frac * 0.55) % 1.0, 0.82, 0.55));
    for (let i = 0; i < segs; i++) {
      const p1 = pts[i], p2 = pts[i + 1];
      tube(p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], tube_r, 8);
    }
  }

  pop();
}
