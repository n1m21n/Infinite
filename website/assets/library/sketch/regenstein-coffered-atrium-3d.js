// Regenstein Coffered Atrium 3D - Infinite Library (Sketch 3D)
param("cols", 4, 2, 6);
param("rows", 4, 2, 6);
param("drop", 0.28, 0.12, 0.5);

function draw(t) {
  const nx = floor(cols);
  const nz = floor(rows);
  const cell = 0.72;

  push();
  rotateX(-0.32);
  rotateY(0.45 + sin(t * 0.18) * 0.16);

  // Coffered concrete ceiling slab
  push();
  translate(0, 0.75, 0);
  fill(0.76, 0.73, 0.67);
  box(nx * cell + 0.3, 0.14, nz * cell + 0.3);
  pop();

  for (let ix = 0; ix < nx; ix++) {
    for (let iz = 0; iz < nz; iz++) {
      const cx = (ix + 0.5 - nx * 0.5) * cell;
      const cz = (iz + 0.5 - nz * 0.5) * cell;

      // Deep concrete coffer perimeter ribs
      push();
      translate(cx, 0.52, cz);
      fill(0.64, 0.61, 0.55);
      box(cell * 0.94, 0.30, 0.09);
      box(0.09, 0.30, cell * 0.94);
      pop();

      // Suspended hexagonal/rectangular fluorescent baffle frame
      push();
      translate(cx, 0.52 - drop, cz);
      fill(0.95, 0.94, 0.90);
      box(cell * 0.56, 0.06, 0.07);
      translate(0, 0, cell * 0.22);
      box(cell * 0.56, 0.06, 0.07);
      pop();
    }
  }

  // Brutalist reading-room concrete columns
  for (let side = -1; side <= 1; side += 2) {
    push();
    translate(side * (nx * cell * 0.46), -0.35, -nz * cell * 0.38);
    fill(0.52, 0.49, 0.45);
    box(0.24, 1.9, 0.24);
    pop();
  }

  pop();
}