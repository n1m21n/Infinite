// Doesburg Counter Prism 3D - Infinite Library (Sketch 3D)
param("tilt", 0.785, 0.2, 1.2);
param("spread", 1.0, 0.6, 1.5);
param("drift", 0.15, 0.0, 0.5);

function draw(t) {
  push();
  rotateX(-0.32 + sin(t * drift) * 0.08);
  rotateY(0.55 + t * drift * 0.5);
  rotateZ(tilt);

  const s = spread;

  // Dominant vermilion counter-plane (#D92B20)
  push();
  translate(0.12 * s, 0.08 * s, 0.0);
  fill(0.851, 0.169, 0.125);
  box(1.35 * s, 1.35 * s, 0.12);
  pop();

  // Cobalt blue cantilevered volume (#124E9B)
  push();
  translate(-0.55 * s, -0.45 * s, 0.28 * s);
  fill(0.071, 0.306, 0.608);
  box(0.68 * s, 0.68 * s, 0.42);
  pop();

  // Cadmium yellow intersecting slab (#EBC328)
  push();
  translate(0.62 * s, -0.52 * s, -0.22 * s);
  fill(0.922, 0.765, 0.157);
  box(0.56 * s, 0.78 * s, 0.18);
  pop();

  // Warm off-white architectural planes (#F3EFE6)
  push();
  translate(-0.48 * s, 0.65 * s, -0.14 * s);
  fill(0.953, 0.937, 0.902);
  box(0.84 * s, 0.44 * s, 0.14);
  pop();

  push();
  translate(0.58 * s, 0.56 * s, 0.26 * s);
  fill(0.85, 0.84, 0.80);
  box(0.48 * s, 0.48 * s, 0.24);
  pop();

  // Charcoal-black structural counter-beams (#141414)
  const beams = [
    [-0.18 * s, 0.0, 0.08 * s, 0.08, 2.25 * s, 0.18],
    [0.0, -0.24 * s, -0.06 * s, 2.25 * s, 0.08, 0.18],
    [0.44 * s, 0.22 * s, 0.18 * s, 0.07, 1.45 * s, 0.48],
    [-0.36 * s, 0.34 * s, -0.18 * s, 1.35 * s, 0.07, 0.42]
  ];
  for (let i = 0; i < beams.length; i++) {
    const b = beams[i];
    push();
    translate(b[0], b[1], b[2]);
    fill(0.08, 0.08, 0.09);
    box(b[3], b[4], b[5]);
    pop();
  }

  pop();
}
