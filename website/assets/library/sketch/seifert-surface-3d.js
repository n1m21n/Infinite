// Seifert Surface 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Orientable topological Seifert surface spanning a knotted boundary with ribbon twists and orbiting quanta.
param("twists", 3, 1, 7);
param("radius", 0.45, 0.2, 1.2);
param("ribbon_w", 0.28, 0.05, 0.6);
param("segs", 72, 24, 120);
param("strips", 16, 6, 32);
param("spin", 0.35, 0.0, 2.0);
param("hue", 0.58, 0.0, 1.0);
param("tube_r", 0.015, 0.005, 0.04);

function trefoilCenter(u, r) {
  // Classic trefoil knot space curve
  const x = r * (sin(u) + 2.0 * sin(2.0 * u));
  const z = r * (cos(u) - 2.0 * cos(2.0 * u));
  const y = r * (-sin(3.0 * u) * 1.2);
  return [x, y, z];
}

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(0.48 + sin(t * 0.22) * 0.16);
  rotateZ(t * spin * 0.25);

  const N = floor(segs);
  const M = floor(strips);
  const qTwist = floor(twists);

  // Precompute centerline points and tangent directors
  const centerPts = [];
  for (let i = 0; i <= N; i++) {
    const u = (i / N) * TAU;
    centerPts.push(trefoilCenter(u, radius));
  }

  // Draw longitudinal ribbon bands across the Seifert surface
  for (let m = 0; m <= M; m++) {
    const v = -1.0 + (2.0 * m) / M;
    const bandPts = [];

    for (let i = 0; i <= N; i++) {
      const u = (i / N) * TAU;
      const c = centerPts[i];
      // Surface normal twist angle spanning the knot
      const theta = (qTwist * u) * 0.5;
      const nx = cos(theta) * cos(u);
      const ny = sin(theta);
      const nz = cos(theta) * sin(u);

      const x = c[0] + v * ribbon_w * nx;
      const y = c[1] + v * ribbon_w * ny;
      const z = c[2] + v * ribbon_w * nz;
      bandPts.push([x, y, z]);
    }

    const fracV = m / M;
    const h = (hue + fracV * 0.42) % 1.0;
    fill(hsl(h, 0.88, 0.60));

    for (let i = 0; i < N; i++) {
      const p1 = bandPts[i], p2 = bandPts[i + 1];
      tube(p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], tube_r, 6);
    }
  }

  // Transversal surface ribs linking the boundary
  for (let i = 0; i < N; i += 3) {
    const u = (i / N) * TAU;
    const c = centerPts[i];
    const theta = (qTwist * u) * 0.5;
    const nx = cos(theta) * cos(u);
    const ny = sin(theta);
    const nz = cos(theta) * sin(u);

    const pA = [c[0] - ribbon_w * nx, c[1] - ribbon_w * ny, c[2] - ribbon_w * nz];
    const pB = [c[0] + ribbon_w * nx, c[1] + ribbon_w * ny, c[2] + ribbon_w * nz];

    fill(hsl((hue + 0.35 + (i / N) * 0.3) % 1.0, 0.92, 0.72));
    tube(pA[0], pA[1], pA[2], pB[0], pB[1], pB[2], tube_r * 1.1, 6);
  }

  // Orbiting boundary quanta flowing along the trefoil perimeter
  const quanta = 6;
  for (let q = 0; q < quanta; q++) {
    const phase = (t * 0.35 + q / quanta) % 1.0;
    const u = phase * TAU;
    const c = trefoilCenter(u, radius);
    const theta = (qTwist * u) * 0.5;
    const nx = cos(theta) * cos(u);
    const ny = sin(theta);
    const nz = cos(theta) * sin(u);
    const side = (q % 2 === 0) ? 1.0 : -1.0;

    push();
    translate(c[0] + side * ribbon_w * nx, c[1] + side * ribbon_w * ny, c[2] + side * ribbon_w * nz);
    fill(hsl((hue + 0.6 + q / quanta) % 1.0, 0.98, 0.85));
    sphere(0.062, 10);
    pop();
  }

  pop();
}
