// Candela Umbrella Cantilever 3D - Infinite Library (Sketch 3D)
param("clusters", 4, 2, 6);
param("ruled_ribs", 10, 6, 16);
param("cam_tilt", 0.34, 0.15, 0.65);
param("spin", 0.15, 0.0, 0.45);

function draw(t) {
  rotateX(cam_tilt + 0.04 * cos(t * 0.28));
  rotateY(t * spin - 0.45);

  const nC = floor(clusters);
  const nR = floor(ruled_ribs);

  // Dark concrete plaza slab
  fill(0.14, 0.16, 0.19);
  push();
  translate(0, -1.02, 0);
  box(4.0, 0.10, 4.0);
  pop();

  for (let c = 0; c < nC; c++) {
    const ca = (c / nC) * TAU + PI * 0.25;
    const rad = nC > 1 ? 0.95 : 0.0;
    const cx = cos(ca) * rad;
    const cz = sin(ca) * rad;
    const hOff = (c % 2) * 0.22;

    push();
    translate(cx, 0, cz);

    // Tapered central concrete column
    fill(0.52, 0.54, 0.58);
    push();
    translate(0, -0.45 + hOff * 0.5, 0);
    box(0.14, 1.15 + hOff, 0.14);
    pop();

    // 4-quadrant inverted hypar umbrella canopy (#ECE8DF)
    for (let q = 0; q < 4; q++) {
      push();
      rotateY((q / 4.0) * TAU);
      for (let i = 0; i < nR; i++) {
        const u = i / max(1, nR - 1);
        const span = 0.12 + u * 0.72;
        const y = 0.10 + hOff + u * 0.36;
        const shade = 0.92 - u * 0.10;
        fill(shade, shade * 0.98, shade * 0.94);
        push();
        translate(span * 0.5, y, span * 0.5);
        rotateY(PI * 0.25);
        rotateX(-0.28);
        box(0.08 + u * 0.65, 0.045, 0.08);
        pop();
      }
      // Ruled steel edge rib (#2C5AA0)
      fill(0.173, 0.353, 0.627);
      push();
      translate(0.45, 0.28 + hOff, 0);
      rotateZ(0.42);
      box(0.95, 0.04, 0.05);
      pop();
      pop();
    }
    pop();
  }
}