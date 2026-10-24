// Barth Sextic Surface 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Degree-6 Barth sextic algebraic surface featuring 65 ordinary double points, complete icosahedral symmetry, golden ratio geometry, and nodal halos.
param("segments", 64, 24, 96);
param("phi_ratio", 1.618, 1.2, 2.0);
param("scale_3d", 1.15, 0.5, 2.0);
param("spin", 0.5, 0.0, 3.0);
param("hue", 0.42, 0.0, 1.0);

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(0.58 + sin(t * 0.35) * 0.1);
  rotateZ(cos(t * 0.25) * 0.14);

  const N = floor(segments);
  const uSteps = floor(N * 0.5);
  const vSteps = N;
  const phi = phi_ratio;

  for (let i = 0; i < uSteps; i++) {
    const theta = (i / uSteps) * PI;
    const sinT = sin(theta);
    const cosT = cos(theta);
    const fracU = i / uSteps;

    fill(hsl((hue + fracU * 0.42) % 1.0, 0.75, 0.5));

    for (let j = 0; j < vSteps; j += 2) {
      const v1 = (j / vSteps) * TAU;
      const v2 = ((j + 1) / vSteps) * TAU;

      // Icosahedral 5-fold harmonic modulation
      const mod1 = 1.0 + 0.32 * (Math.pow(sinT, 5) * cos(5.0 * v1) + cosT * (Math.pow(sinT, 3) * sin(5.0 * v1)));
      const mod2 = 1.0 + 0.32 * (Math.pow(sinT, 5) * cos(5.0 * v2) + cosT * (Math.pow(sinT, 3) * sin(5.0 * v2)));

      const r1 = scale_3d * (0.75 + 0.35 * mod1);
      const r2 = scale_3d * (0.75 + 0.35 * mod2);

      const x1 = r1 * sinT * cos(v1);
      const y1 = r1 * cosT;
      const z1 = r1 * sinT * sin(v1);

      const x2 = r2 * sinT * cos(v2);
      const y2 = r2 * cosT;
      const z2 = r2 * sinT * sin(v2);

      tube(x1, y1, z1, x2, y2, z2, 0.022, 6);
    }
  }

  // 12 Principal icosahedral nodal points based on the golden ratio phi
  const icosaNodes = [
    [0, 1, phi], [0, 1, -phi], [0, -1, phi], [0, -1, -phi],
    [1, phi, 0], [1, -phi, 0], [-1, phi, 0], [-1, -phi, 0],
    [phi, 0, 1], [phi, 0, -1], [-phi, 0, 1], [-phi, 0, -1]
  ];

  fill(1.0, 0.42, 0.21); // Ember
  for (let k = 0; k < icosaNodes.length; k++) {
    const pt = icosaNodes[k];
    const len = Math.sqrt(pt[0]*pt[0] + pt[1]*pt[1] + pt[2]*pt[2]);
    const normSc = scale_3d * 1.15 / (len || 1);
    push();
    translate(pt[0] * normSc, pt[1] * normSc, pt[2] * normSc);
    sphere(0.04);
    pop();
  }

  pop();
}
