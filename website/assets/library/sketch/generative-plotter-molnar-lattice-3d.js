// Generative Plotter Molnar Lattice 3D - Infinite Library (Sketch 3D)
param("grid_n", 5, 3, 7);
param("jitter", 0.28, 0.05, 0.65);
param("step_gap", 0.44, 0.30, 0.60);
param("spin_rate", 0.25, 0.0, 1.0);

function draw(t) {
  rotateX(0.48);
  rotateY(t * spin_rate + 0.55);

  const n = floor(grid_n);
  const half = (n - 1) * 0.5;

  for (let ix = 0; ix < n; ix++) {
    for (let iy = 0; iy < n; iy++) {
      for (let iz = 0; iz < 2; iz++) {
        const px = (ix - half) * step_gap;
        const py = (iy - half) * step_gap;
        const pz = (iz - 0.5) * step_gap * 1.4;

        const seed = ix * 7.13 + iy * 13.71 + iz * 23.3;
        const rx = Math.sin(seed + t * 0.3) * jitter;
        const ry = Math.cos(seed * 1.3 - t * 0.2) * jitter;

        push();
        translate(px, py, pz);
        rotateX(rx);
        rotateY(ry);

        if ((ix + iy + iz) % 5 === 0) {
          fill(0.820, 0.188, 0.122);
        } else {
          fill(0.067, 0.067, 0.067);
        }

        // Plotter square frame built from 4 thin struts
        const s = step_gap * 0.38;
        const th = 0.024;
        push(); translate(0, s, 0); box(s * 2.0, th, th); pop();
        push(); translate(0, -s, 0); box(s * 2.0, th, th); pop();
        push(); translate(s, 0, 0); box(th, s * 2.0, th); pop();
        push(); translate(-s, 0, 0); box(th, s * 2.0, th); pop();

        pop();
      }
    }
  }
}
