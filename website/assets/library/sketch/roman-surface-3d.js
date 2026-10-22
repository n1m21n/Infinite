// Roman Surface 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Steiner's Roman Surface: non-orientable self-intersecting projective plane with tetrahedral symmetry.
param("radius", 1.4, 0.5, 3.0);
param("twist", 1.0, 0.0, 3.0);
param("spin", 0.4, 0.0, 2.0);
param("tube_r", 0.035, 0.01, 0.1);
param("hue", 0.62, 0.0, 1.0);

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(t * spin * 0.65 + 0.3);
  rotateZ(sin(t * 0.3) * 0.2);

  const uSteps = 40, vSteps = 24;
  for (let j = 0; j <= vSteps; j++) {
    const v = (j / vSteps) * PI - PI * 0.5;
    const pts = [];
    for (let i = 0; i <= uSteps; i++) {
      const u = (i / uSteps) * PI;
      // Steiner Roman Surface parametric formulation:
      const r = radius * (1.0 + 0.15 * sin(t * twist + u * 2.0));
      const x = r * sin(2.0 * u) * cos(v) * cos(v);
      const y = r * sin(u) * sin(2.0 * v);
      const z = r * cos(u) * sin(2.0 * v);
      pts.push([x, y, z]);
    }

    const frac = j / vSteps;
    fill(hsl((hue + frac * 0.45) % 1.0, 0.85, 0.52));
    for (let i = 0; i < uSteps; i++) {
      const p1 = pts[i], p2 = pts[i + 1];
      tube(p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], tube_r, 8);
    }
  }

  // Cross-cap nodal satellite beads
  const ringPts = 32;
  for (let k = 0; k < ringPts; k++) {
    const a = (k / ringPts) * TAU;
    const ox = radius * 0.6 * cos(a + t * 0.8);
    const oy = radius * 0.6 * sin(a + t * 0.8);
    push();
    translate(ox, oy, 0);
    fill(hsl((hue + 0.5 + (k / ringPts) * 0.3) % 1.0, 0.9, 0.65));
    sphere(tube_r * 1.8, 8);
    pop();
  }

  pop();
}
