// QRD Basalt Acoustic Diffuser 3D - Infinite Library (Sketch 3D)
param("prime_n", 13, 7, 17);
param("well_depth", 0.95, 0.35, 1.6);
param("tilt", 0.58, 0.2, 1.0);
param("orbit", 0.16, 0.0, 0.5);

function draw(t) {
  rotateX(tilt);
  rotateY(t * orbit + 0.55);

  const N = floor(prime_n);
  const cols = 11;
  const rows = 11;
  const pitch = 0.24;

  // Heavy dark slate acoustic plinth
  fill(0.11, 0.12, 0.14);
  push();
  translate(0, -0.32, 0);
  box(cols * pitch + 0.28, 0.14, rows * pitch + 0.28);
  pop();

  // Warm brass perimeter trim rail
  fill(0.76, 0.58, 0.31);
  push();
  translate(0, -0.24, 0);
  box(cols * pitch + 0.30, 0.025, rows * pitch + 0.30);
  pop();

  // 2D Quadratic Residue Diffuser well matrix: h(x,z) = (x^2 + z^2) mod N
  for (let ix = 0; ix < cols; ix++) {
    for (let iz = 0; iz < rows; iz++) {
      const q = (ix * ix + iz * iz) % N;
      const u = q / max(1, N - 1);
      const pulse = 0.06 * sin(t * 1.4 + u * 4.5);
      const h = 0.18 + (u + pulse) * well_depth;

      const x = (ix - (cols - 1) * 0.5) * pitch;
      const z = (iz - (rows - 1) * 0.5) * pitch;

      // Basalt tonal gradation from deep charcoal to warm limestone cap
      const shade = 0.22 + 0.42 * u;
      fill(shade, shade * 0.98, shade * 0.94);

      push();
      translate(x, h * 0.5 - 0.22, z);
      box(pitch * 0.84, h, pitch * 0.84);
      pop();

      // Specular brass acoustic cap on peak wells
      if (u > 0.72) {
        fill(0.84, 0.65, 0.34);
        push();
        translate(x, h - 0.21, z);
        box(pitch * 0.86, 0.028, pitch * 0.86);
        pop();
      }
    }
  }
}
