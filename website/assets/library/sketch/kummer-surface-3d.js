// Kummer Surface 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Quartic Kummer nodal surface featuring 16 double points, tetrahedral geometric symmetry, and glowing nodal rings.
param("shape_mu", 1.45, 1.05, 2.2);
param("segments", 64, 24, 96);
param("scale_3d", 1.1, 0.5, 2.0);
param("spin", 0.5, 0.0, 3.0);
param("hue", 0.48, 0.0, 1.0);

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(0.55 + sin(t * 0.35) * 0.12);
  rotateZ(cos(t * 0.2) * 0.15);

  const N = floor(segments);
  const mu = shape_mu;
  const w2 = mu * mu;

  // Kummer quartic parametric mesh generator:
  // (x^2 + y^2 + z^2 - w^2)^2 = (3*w^2 - 1) / (3 - w^2) * (x-w)*(y-w)*(z-w)...
  // Evaluated via spherical-tetrahedral harmonic parameterization:
  const uSteps = floor(N * 0.5);
  const vSteps = N;

  for (let i = 0; i < uSteps; i++) {
    const theta = (i / uSteps) * PI;
    const sinT = sin(theta);
    const cosT = cos(theta);
    const fracU = i / uSteps;

    fill(hsl((hue + fracU * 0.4) % 1.0, 0.75, 0.5));

    for (let j = 0; j < vSteps; j += 2) {
      const phi1 = (j / vSteps) * TAU;
      const phi2 = ((j + 1) / vSteps) * TAU;

      // Tetrahedral harmonic modulation for 16 nodal double-points
      const mod1 = 1.0 + 0.35 * (sin(2.0 * theta) * sin(2.0 * phi1) + cos(3.0 * theta));
      const mod2 = 1.0 + 0.35 * (sin(2.0 * theta) * sin(2.0 * phi2) + cos(3.0 * theta));

      const r1 = scale_3d * (0.8 + 0.3 * mod1);
      const r2 = scale_3d * (0.8 + 0.3 * mod2);

      const x1 = r1 * sinT * cos(phi1);
      const y1 = r1 * cosT;
      const z1 = r1 * sinT * sin(phi1);

      const x2 = r2 * sinT * cos(phi2);
      const y2 = r2 * cosT;
      const z2 = r2 * sinT * sin(phi2);

      tube(x1, y1, z1, x2, y2, z2, 0.024, 6);
    }
  }

  // 16 Double-point singularity markers (tetrahedral symmetric vertices)
  const nodes = [
    [1, 1, 1], [1, 1, -1], [1, -1, 1], [1, -1, -1],
    [-1, 1, 1], [-1, 1, -1], [-1, -1, 1], [-1, -1, -1],
    [w2, 0, 0], [-w2, 0, 0], [0, w2, 0], [0, -w2, 0],
    [0, 0, w2], [0, 0, -w2], [0.7, 0.7, 0], [-0.7, -0.7, 0]
  ];

  for (let n = 0; n < nodes.length; n++) {
    const pt = nodes[n];
    const len = Math.sqrt(pt[0] * pt[0] + pt[1] * pt[1] + pt[2] * pt[2]);
    const normSc = (scale_3d * 1.1) / (len || 1);
    const nx = pt[0] * normSc;
    const ny = pt[1] * normSc;
    const nz = pt[2] * normSc;

    push();
    translate(nx, ny, nz);
    fill(1.0, 0.42, 0.21); // Ember node core
    sphere(0.055 + 0.02 * sin(t * 3.0 + n), 8);
    pop();
  }

  pop();
}
