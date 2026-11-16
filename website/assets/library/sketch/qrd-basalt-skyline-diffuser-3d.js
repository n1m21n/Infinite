// QRD Basalt Skyline Diffuser 3D - Infinite Library (Sketch 3D)
param("prime", 11, 7, 13);
param("relief", 0.85, 0.3, 1.4);
param("orbit", 0.2, 0.0, 1.0);

function draw(t) {
  rotateY(t * orbit + 0.58);
  rotateX(-0.34);

  // Dark basalt slate plinth (#14110F)
  push();
  translate(0, -0.78, 0);
  fill(0.078, 0.067, 0.059);
  box(2.75, 0.14, 2.75);
  pop();

  // Brass perimeter datum frame (#C7A254)
  for (let s = -1; s <= 1; s += 2) {
    push();
    translate(0, -0.69, s * 1.32);
    fill(0.780, 0.635, 0.329);
    box(2.68, 0.03, 0.03);
    pop();
    push();
    translate(s * 1.32, -0.69, 0);
    fill(0.780, 0.635, 0.329);
    box(0.03, 0.03, 2.68);
    pop();
  }

  const p = floor(prime);
  const span = 2.35;
  const step = span / p;
  const w = step * 0.90;

  // 2D Quadratic Residue sequence wells
  for (let ix = 0; ix < p; ix++) {
    for (let iz = 0; iz < p; iz++) {
      const seq = (ix * ix + iz * iz) % p;
      const norm = seq / (p - 1);
      const pulse = 0.06 * sin(t * 1.2 + (ix + iz) * 0.45);
      const h = 0.18 + (norm + pulse) * relief;
      const x = ((ix + 0.5) / p - 0.5) * span;
      const z = ((iz + 0.5) / p - 0.5) * span;

      push();
      translate(x, -0.71 + h * 0.5, z);
      if (norm > 0.66) {
        fill(0.918, 0.898, 0.863); // warm travertine plaster
      } else if (norm > 0.33) {
        fill(0.541, 0.455, 0.357); // weathered ochre stone
      } else {
        fill(0.173, 0.149, 0.129); // deep basalt slate
      }
      box(w, h, w);
      pop();
    }
  }
}
