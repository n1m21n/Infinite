// Schroeder QRD Diffuser 3D - Infinite Library (Sketch 3D)
param("gridN", 11, 7, 15);
param("primeP", 17, 7, 23);
param("depth", 1.15, 0.5, 1.8);
param("spin", 0.16, 0.0, 0.5);

function draw(t) {
  const n = floor(gridN);
  const p = floor(primeP);
  const span = 2.2;
  const cell = span / n;

  rotateY(t * spin + 0.62);
  rotateX(0.42);

  // Obsidian acoustic backing frame
  push();
  translate(0, -0.42, 0);
  fill(0.094, 0.078, 0.071);
  box(span * 1.08, 0.12, span * 1.08);
  pop();

  for (let ix = 0; ix < n; ix++) {
    for (let iz = 0; iz < n; iz++) {
      const q = (ix * ix + iz * iz) % p;
      const u = q / p;
      const breathe = 0.06 * sin(t * 1.4 + q * 0.5);
      const h = 0.18 + (u + breathe) * depth;
      const x = (ix - (n - 1) * 0.5) * cell;
      const z = (iz - (n - 1) * 0.5) * cell;

      push();
      translate(x, -0.36 + h * 0.5, z);
      // Warm birch -> walnut -> espresso timber gradient by well height
      const r = 0.231 + u * 0.616;
      const g = 0.157 + u * 0.569;
      const b = 0.102 + u * 0.439;
      fill(r, g, b);
      box(cell * 0.86, h, cell * 0.86);
      pop();
    }
  }
}