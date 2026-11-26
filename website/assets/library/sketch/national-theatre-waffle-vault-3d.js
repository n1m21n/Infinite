// National Theatre Waffle Vault 3D - Infinite Library (Sketch 3D)
param("bays", 5, 3, 7);
param("coffer_depth", 0.32, 0.15, 0.55);
param("sway", 0.12, 0.0, 0.35);

function draw(t) {
  const n = floor(bays);
  const step = 0.62;
  const span = n * step;

  push();
  rotateX(-0.42 + sin(t * 0.25) * sway * 0.3);
  rotateY(0.38 + cos(t * 0.2) * sway);

  // Upper structural concrete deck
  push();
  translate(0, 0.55 + coffer_depth * 0.5, 0);
  fill(0.72, 0.70, 0.66);
  box(span + 0.4, 0.12, span + 0.4);
  pop();

  // Waffle-slab grid ribs and recessed coffer frames
  for (let ix = 0; ix <= n; ix++) {
    const x = (ix - n * 0.5) * step;
    push();
    translate(x, 0.38, 0);
    fill(0.62, 0.60, 0.55);
    box(0.11, coffer_depth, span + 0.12);
    pop();
  }
  for (let iz = 0; iz <= n; iz++) {
    const z = (iz - n * 0.5) * step;
    push();
    translate(0, 0.38, z);
    fill(0.56, 0.54, 0.49);
    box(span + 0.12, coffer_depth, 0.11);
    pop();
  }

  // Inner stepped coffer soffits
  for (let ix = 0; ix < n; ix++) {
    for (let iz = 0; iz < n; iz++) {
      const cx = (ix + 0.5 - n * 0.5) * step;
      const cz = (iz + 0.5 - n * 0.5) * step;
      push();
      translate(cx, 0.48, cz);
      fill(0.32, 0.31, 0.29);
      box(step * 0.74, 0.08, step * 0.74);
      pop();
    }
  }

  // Central brutalist support pier & cantilevered balustrade
  push();
  translate(0, -0.45, 0);
  fill(0.48, 0.46, 0.42);
  box(0.36, 1.5, 0.36);
  pop();

  push();
  translate(0, -0.55, span * 0.35);
  fill(0.24, 0.23, 0.21);
  box(span * 0.85, 0.28, 0.14);
  pop();

  pop();
}