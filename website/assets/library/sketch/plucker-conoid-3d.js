// Plücker's Conoid 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Julius Plücker's ruled algebraic surface (1864) with n-lobed helical symmetry and traveling ruling quanta.
param("lobes", 3, 1, 6);
param("radius", 1.35, 0.4, 3.0);
param("height_sc", 0.65, 0.2, 1.8);
param("ribs", 44, 16, 80);
param("spin", 0.38, 0.0, 2.0);
param("hue", 0.64, 0.0, 1.0);
param("tube_r", 0.016, 0.005, 0.045);

function conoidPoint(u, v, n, scR, scH) {
  const x = u * scR * cos(v);
  const z = u * scR * sin(v);
  const y = scH * sin(n * v);
  return [x, y, z];
}

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(0.55 + sin(t * 0.25) * 0.15);
  rotateZ(t * spin * 0.3);

  const n = floor(lobes);
  const ribCount = floor(ribs);
  const uSteps = 24;

  // Ruling line generator ribs (straight ruling lines at angle v)
  for (let i = 0; i < ribCount; i++) {
    const fracV = i / ribCount;
    const v = fracV * TAU;
    const h = (hue + fracV * 0.5) % 1.0;
    fill(hsl(h, 0.90, 0.62));

    let prev = conoidPoint(-1.0, v, n, radius, height_sc);
    for (let j = 1; j <= uSteps; j++) {
      const u = -1.0 + (2.0 * j) / uSteps;
      const curr = conoidPoint(u, v, n, radius, height_sc);
      tube(prev[0], prev[1], prev[2], curr[0], curr[1], curr[2], tube_r, 6);
      prev = curr;
    }
  }

  // Concentric radial contour rings (constant u sweeping v)
  const rings = 8;
  for (let r = 1; r <= rings; r++) {
    const u = r / rings;
    const ringPts = 64;
    const h = (hue + 0.3 + (r / rings) * 0.4) % 1.0;
    fill(hsl(h, 0.94, 0.70));

    let prev = conoidPoint(u, 0, n, radius, height_sc);
    for (let k = 1; k <= ringPts; k++) {
      const v = (k / ringPts) * TAU;
      const curr = conoidPoint(u, v, n, radius, height_sc);
      tube(prev[0], prev[1], prev[2], curr[0], curr[1], curr[2], tube_r * 1.15, 6);
      prev = curr;
    }
  }

  // Traveling photon quanta drifting along the ruled helical lobes
  const quanta = 8;
  for (let q = 0; q < quanta; q++) {
    const phase = (t * 0.3 + q / quanta) % 1.0;
    const v = phase * TAU;
    const u = sin(t * 0.8 + q * 1.2);
    const pos = conoidPoint(u, v, n, radius, height_sc);

    push();
    translate(pos[0], pos[1], pos[2]);
    fill(hsl((hue + 0.5 + q / quanta) % 1.0, 0.95, 0.82));
    sphere(0.065, 10);
    pop();
  }

  pop();
}
