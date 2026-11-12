// Holographic Iridescent Faceted Torus Lattice 3D - Infinite Library (Sketch 3D)
param("spin", 0.38, 0.1, 1.5);
param("scale_factor", 1.2, 0.5, 2.5);
param("torus_segments", 28.0, 16.0, 48.0);
param("satellites", 6.0, 3.0, 12.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.55 + sin(time * 0.18) * 0.06);
  rotateY(time * 0.34 + 0.4);

  const nSeg = floor(torus_segments);
  const majorR = 1.15 * scale_factor;

  for (let i = 0; i < nSeg; i++) {
    const a = (i / nSeg) * Math.PI * 2.0;
    const x = Math.cos(a) * majorR;
    const z = Math.sin(a) * majorR;
    const y = Math.sin(a * 3.0 + time * 0.5) * 0.18 * scale_factor;

    push();
    translate(x, y, z);
    rotateY(-a);
    rotateZ(time * 0.4);

    if (i % 3 === 0) {
      fill(0.663, 0.941, 1.0); // Cyan
    } else if (i % 3 === 1) {
      fill(1.0, 0.702, 0.961); // Pink
    } else {
      fill(0.839, 1.0, 0.620); // Mint
    }
    box(0.24 * scale_factor, 0.14 * scale_factor, 0.14 * scale_factor);
    pop();
  }

  for (let i = 0; i < nSeg; i++) {
    const a = (i / nSeg) * Math.PI * 2.0;
    const x = Math.cos(a) * (majorR * 0.75);
    const y = Math.sin(a) * (majorR * 0.75);
    const z = Math.cos(a * 2.0 + time * 0.4) * 0.15 * scale_factor;

    push();
    translate(x, y, z);
    rotateZ(-a);
    fill(0.96, 0.98, 1.0); // Prismatic white
    box(0.18 * scale_factor, 0.10 * scale_factor, 0.10 * scale_factor);
    pop();
  }

  const nSat = floor(satellites);
  for (let k = 0; k < nSat; k++) {
    const sa = (k / nSat) * Math.PI * 2.0 + time * 0.6;
    const sr = 1.55 * scale_factor;
    const sx = Math.cos(sa) * sr;
    const sy = Math.sin(sa * 2.0) * 0.45 * scale_factor;
    const sz = Math.sin(sa) * sr;

    push();
    translate(sx, sy, sz);
    fill(1.0, 0.702, 0.961); // Pink pearl
    box(0.22 * scale_factor, 0.22 * scale_factor, 0.22 * scale_factor);
    pop();
  }

  pop();
}
