// Frei Otto Anticlastic Saddle 3D - Infinite Library (Sketch 3D)
param("grid_n", 14, 8, 20);
param("saddle_curve", 0.85, 0.35, 1.35);
param("mast_height", 1.75, 1.1, 2.4);
param("orbit", 0.25, 0.0, 1.0);

function draw(t) {
  // Architectural three-quarter perspective
  scale(0.54);
  rotateX(0.32 + 0.04 * sin(t * 0.35));
  rotateY(0.68 + 0.16 * sin(t * orbit * 0.45));
  translate(0, -0.55, 0);

  const n = floor(grid_n);
  const span = 2.6;
  const half = span * 0.5;

  // Concrete anchor plinth slab (#242B33)
  fill(0.141, 0.169, 0.200);
  push();
  translate(0, -0.06, 0);
  box(span + 0.9, 0.12, span + 0.9);
  pop();

  // Four tapering steel compression masts & guy cables at the corners
  const corners = [[-1, -1], [1, -1], [1, 1], [-1, 1]];
  for (let c = 0; c < 4; c++) {
    const sx = corners[c][0];
    const sz = corners[c][1];
    const mx = sx * (half + 0.22);
    const mz = sz * (half + 0.22);
    const isHigh = (sx * sz) > 0;
    const mh = isHigh ? mast_height : mast_height * 0.56;

    // Steel mast (#D7D3CA)
    fill(0.843, 0.827, 0.792);
    push();
    translate(mx, mh * 0.5, mz);
    cylinder(0.045, mh);
    pop();

    // Cast-steel masthead cap (#6E94B8)
    fill(0.431, 0.580, 0.722);
    push();
    translate(mx, mh, mz);
    sphere(0.075);
    pop();

    // Anchor tie-down block
    fill(0.231, 0.275, 0.322);
    push();
    translate(mx * 1.16, 0.08, mz * 1.16);
    box(0.18, 0.16, 0.18);
    pop();
  }

  // Anticlastic hyperbolic paraboloid cable-net: y = y0 + k * (x^2 - z^2)
  const baseY = mast_height * 0.52;
  const k = saddle_curve / (half * half);
  const step = span / n;

  // Primary & secondary tension cables + translucent acrylic clamp nodes
  for (let i = 0; i <= n; i++) {
    const u = -half + i * step;
    for (let j = 0; j < n; j++) {
      const v0 = -half + j * step;
      const v1 = v0 + step;
      const vm = (v0 + v1) * 0.5;

      // X-direction cable segment
      const y0x = baseY + k * (v0 * v0 - u * u) * 0.65;
      const y1x = baseY + k * (v1 * v1 - u * u) * 0.65;
      const ymx = (y0x + y1x) * 0.5;
      const dxAngle = atan2(y1x - y0x, step);
      const lenX = sqrt(step * step + (y1x - y0x) * (y1x - y0x));

      fill(0.843, 0.827, 0.792);
      push();
      translate(vm, ymx, u);
      rotateZ(dxAngle);
      box(lenX, 0.022, 0.022);
      pop();

      // Z-direction cable segment
      const y0z = baseY + k * (u * u - v0 * v0) * 0.65;
      const y1z = baseY + k * (u * u - v1 * v1) * 0.65;
      const ymz = (y0z + y1z) * 0.5;
      const dzAngle = atan2(y1z - y0z, step);
      const lenZ = sqrt(step * step + (y1z - y0z) * (y1z - y0z));

      fill(0.431, 0.580, 0.722);
      push();
      translate(u, ymz, vm);
      rotateX(-dzAngle);
      box(0.022, 0.022, lenZ);
      pop();

      // Translucent acrylic membrane tile & cast-steel node
      if (i < n) {
        const xc = u + step * 0.5;
        const zc = vm;
        const yc = baseY + k * (xc * xc - zc * zc) * 0.65;
        fill(0.431, 0.580, 0.722, 0.34);
        push();
        translate(xc, yc, zc);
        box(step * 0.84, 0.012, step * 0.84);
        pop();
      }
    }
  }
}
