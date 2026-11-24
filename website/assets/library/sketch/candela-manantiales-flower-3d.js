// Candela Manantiales Flower 3D - Infinite Library (Sketch 3D)
param("petals", 8, 5, 12);
param("rib_steps", 12, 6, 18);
param("cam_tilt", 0.38, 0.15, 0.70);
param("spin", 0.18, 0.0, 0.45);

function draw(t) {
  rotateX(cam_tilt + 0.05 * sin(t * 0.3));
  rotateY(t * spin + 0.35);

  const nP = floor(petals);
  const nR = floor(rib_steps);

  // Travertine reflecting pool plinth
  fill(0.11, 0.14, 0.20);
  push();
  translate(0, -0.95, 0);
  box(3.8, 0.12, 3.8);
  pop();

  // Cobalt glass curtain core (#2C5AA0)
  fill(0.173, 0.353, 0.627);
  for (let p = 0; p < nP; p++) {
    const a = (p / nP) * TAU;
    push();
    rotateY(a);
    translate(0, -0.45, 0.85);
    box(0.68, 0.90, 0.04);
    pop();
  }

  // Eight intersecting hyperbolic paraboloid concrete shell petals (#ECE8DF)
  for (let p = 0; p < nP; p++) {
    const a = (p / nP) * TAU;
    push();
    rotateY(a);
    for (let u = 0; u < nR; u++) {
      const fu = u / max(1, nR - 1);
      const r = 0.25 + fu * 1.45;
      const archY = -0.78 + sin(fu * PI * 0.85) * 1.15;
      const ribW = 0.14 + fu * 0.56;
      const shade = 0.925 - fu * 0.14;
      fill(shade, shade * 0.985, shade * 0.945);

      push();
      translate(0, archY, r);
      rotateX(-0.35 + fu * 0.65);
      box(ribW, 0.06, 0.16);
      pop();
    }
    // Diagonal valley groin crease (#3A4659)
    fill(0.227, 0.275, 0.349);
    rotateY(PI / nP);
    push();
    translate(0, -0.35, 0.92);
    rotateX(0.55);
    box(0.06, 0.08, 1.55);
    pop();
    pop();
  }
}