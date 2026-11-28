// Candela Xochimilco Hypar 3D - Infinite Library (Sketch 3D)
param("lobes", 8, 6, 10);
param("ribs", 14, 8, 20);
param("arch_rise", 0.95, 0.5, 1.4);

function draw(t) {
  const nLobes = floor(lobes);
  const nRibs = floor(ribs);

  push();
  scale(0.68, 0.68, 0.68);
  rotateX(-0.36 + sin(t * 0.22) * 0.08);
  rotateY(0.25 + t * 0.14);

  // Dark slate water-mirror plinth
  push();
  translate(0, -0.58, 0);
  fill(0.12, 0.16, 0.20);
  box(3.4, 0.08, 3.4);
  pop();

  // Radially symmetric intersecting hyperbolic paraboloid lobes
  for (let l = 0; l < nLobes; l++) {
    const a = (l / nLobes) * TAU;
    push();
    rotateY(a);
    for (let i = 0; i < nRibs; i++) {
      const u = i / (nRibs - 1);
      const r = 0.18 + u * 1.38;
      const y = -0.35 + arch_rise * (1.0 - (u - 0.35) * (u - 0.35) * 1.45);
      const wSpan = 0.16 + u * 0.72;
      const tilt = (u - 0.35) * 0.52;

      push();
      translate(r, y, 0);
      rotateZ(-tilt);
      // Warm cast concrete shell (#DFDCD4 -> #A8A399)
      const shade = 0.84 - u * 0.22;
      fill(shade, shade * 0.98, shade * 0.94);
      box(0.13, 0.045, wSpan);
      pop();
    }

    // Sharp concrete groin rib along lobe seam
    push();
    translate(0.75, 0.12, 0.0);
    rotateZ(-0.18);
    fill(0.45, 0.43, 0.40);
    box(1.45, 0.07, 0.07);
    pop();

    pop();
  }

  pop();
}