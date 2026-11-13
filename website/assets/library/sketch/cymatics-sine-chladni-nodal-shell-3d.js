// Cymatics Sine Chladni Nodal Shell 3D - Infinite Library (Sketch 3D)
param("mode_m", 3.0, 1.0, 7.0);
param("mode_n", 5.0, 2.0, 9.0);
param("grid_res", 20, 10, 28);
param("wave_amp", 0.35, 0.1, 0.7);

function draw(t) {
  rotateY(t * 0.22);
  rotateX(0.48);

  const g = floor(grid_res);
  const span = 2.4;
  const step = span / g;

  // Outer resonant containment ring in electric cyan-ice (#7df9ff)
  push();
  fill(0.49, 0.976, 1.0);
  torus(1.42, 0.025, 36, 10);
  pop();

  // Inner reference ring in crisp white
  push();
  fill(0.95, 0.98, 1.0);
  torus(0.95, 0.018, 32, 8);
  pop();

  // Chladni standing-wave nodal field
  for (let ix = 0; ix < g; ix++) {
    const x = (ix / (g - 1) - 0.5) * span;
    for (let iz = 0; iz < g; iz++) {
      const z = (iz / (g - 1) - 0.5) * span;
      const r = Math.sqrt(x * x + z * z);
      if (r > 1.32) continue;

      const c1 = Math.cos(mode_m * x * 2.2) * Math.cos(mode_n * z * 2.2);
      const c2 = Math.cos(mode_n * x * 2.2) * Math.cos(mode_m * z * 2.2);
      const nodal = c1 - c2;
      const h = Math.abs(nodal) * wave_amp + 0.04;
      const isNode = Math.abs(nodal) < 0.28;

      push();
      translate(x, nodal * wave_amp * 0.5, z);
      if (isNode) {
        fill(0.49, 0.976, 1.0);
        box(step * 0.52, 0.05, step * 0.52);
      } else {
        fill(0.94, 0.96, 0.98);
        box(step * 0.36, h, step * 0.36);
      }
      pop();
    }
  }
}
