// Generative Plotter Molnar Cubes 3D - Infinite Library (Sketch 3D)
param("grid_size", 4, 3, 6);
param("jitter_amt", 0.16, 0.0, 0.35);
param("tilt_angle", 0.52, 0.1, 1.1);
param("spin_speed", 0.25, 0.0, 1.0);

function draw(t) {
  rotateX(tilt_angle);
  rotateY(t * spin_speed + 0.62);

  const ng = floor(grid_size);
  const spacing = 0.62;
  const offset = (ng - 1) * spacing * 0.5;

  // Outer plotter registration frame
  push();
  fill(0.067, 0.067, 0.067);
  torus(1.55, 0.018, 48, 8);
  pop();

  for (let ix = 0; ix < ng; ix++) {
    for (let iz = 0; iz < ng; iz++) {
      const seed = ix * 17.3 + iz * 31.7;
      const jx = Math.sin(seed + t * 0.3) * jitter_amt;
      const jz = Math.cos(seed * 1.4 - t * 0.25) * jitter_amt;
      const hStep = 0.28 + ((ix + iz * 2) % 3) * 0.24;
      const isAccent = ((ix + iz) % 4 === 1);

      push();
      translate(ix * spacing - offset + jx, 0, iz * spacing - offset + jz);
      rotateY(Math.sin(seed) * 0.28);

      if (isAccent) {
        fill(0.820, 0.188, 0.122); // #d1301f vermilion
      } else {
        fill(0.067, 0.067, 0.067); // #111111 technical ink
      }

      // Stepped plotter frame bars
      box(0.42, 0.05, 0.42);
      translate(0, hStep * 0.5, 0);
      box(0.06, hStep, 0.06);
      translate(0, hStep * 0.5, 0);
      box(0.32, 0.05, 0.32);
      pop();
    }
  }
}
