// Klein Bottle 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Parametric 4D topological Klein bottle figure-8 immersion in 3D with ribs, spine rails and orbiting energy quanta.
param("scale_r", 1.15, 0.4, 2.5);
param("ribs", 48, 16, 96);
param("cross_segs", 20, 8, 36);
param("spin", 0.5, 0.0, 2.5);
param("hue", 0.62, 0.0, 1.0);
param("tube_r", 0.018, 0.006, 0.05);

function kleinPoint(u, v, sc) {
  const a = 2.0;
  const halfU = u * 0.5;
  const sinV = sin(v), cosV = cos(v);
  const sin2V = sin(2.0 * v);
  const sinHU = sin(halfU), cosHU = cos(halfU);
  const cosU = cos(u), sinU = sin(u);

  const rad = a + cosHU * sinV - sinHU * sin2V;
  const x = sc * rad * cosU * 0.45;
  const y = sc * rad * sinU * 0.45;
  const z = sc * (sinHU * sinV + cosHU * sin2V) * 0.65;
  return [x, y, z];
}

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(t * spin * 0.7);
  rotateZ(sin(t * 0.3) * 0.2);

  const N = floor(ribs);
  const M = floor(cross_segs);

  // Draw transverse circular cross-section rings
  for (let i = 0; i < N; i++) {
    const u = (i / N) * TAU;
    const fracU = i / N;
    fill(hsl((hue + fracU * 0.5) % 1.0, 0.8, 0.55));

    let prev = kleinPoint(u, 0, scale_r);
    for (let j = 1; j <= M; j++) {
      const v = (j / M) * TAU;
      const curr = kleinPoint(u, v, scale_r);
      tube(prev[0], prev[1], prev[2], curr[0], curr[1], curr[2], tube_r, 6);
      prev = curr;
    }
  }

  // Draw longitudinal guide rails
  const numRails = 8;
  for (let r = 0; r < numRails; r++) {
    const v = (r / numRails) * TAU;
    fill(hsl((hue + 0.2 + (r / numRails) * 0.4) % 1.0, 0.9, 0.65));

    let prev = kleinPoint(0, v, scale_r);
    for (let i = 1; i <= N; i++) {
      const u = (i / N) * TAU;
      const curr = kleinPoint(u, v, scale_r);
      tube(prev[0], prev[1], prev[2], curr[0], curr[1], curr[2], tube_r * 1.2, 6);
      prev = curr;
    }
  }

  // Orbiting topological pulse quanta
  const quanta = 6;
  for (let q = 0; q < quanta; q++) {
    const phaseU = (t * 0.2 + q / quanta) % 1.0;
    const phaseV = (t * 0.4 + q * 0.35) % 1.0;
    const pos = kleinPoint(phaseU * TAU, phaseV * TAU, scale_r);

    push();
    translate(pos[0], pos[1], pos[2]);
    fill(hsl((hue + 0.45 + q / quanta) % 1.0, 0.95, 0.75));
    sphere(0.06, 10);
    pop();
  }

  pop();
}
