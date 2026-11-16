// Nervi Ferrocement Hyperbolic Shell 3D - Infinite Library (Sketch 3D)
param("rib_density", 11, 7, 16);
param("saddle_warp", 0.58, 0.25, 0.95);
param("tilt", 0.48, 0.2, 0.85);
param("orbit", 0.20, 0.0, 0.6);

function draw(t) {
  rotateX(tilt);
  rotateY(t * orbit + 0.62);

  const n = floor(rib_density);
  const span = 2.1;
  const half = span * 0.5;

  // Cast-iron corner support piers (#1F2124)
  fill(0.122, 0.129, 0.141);
  for (let sx = -1; sx <= 1; sx += 2) {
    for (let sz = -1; sz <= 1; sz += 2) {
      const yCorner = saddle_warp * sx * sz;
      push();
      translate(sx * half, yCorner * 0.5 - 0.32, sz * half);
      box(0.09, abs(yCorner) + 0.68, 0.09);
      pop();
    }
  }

  // Ruled-surface ferrocement rib generators along U & V
  for (let i = 0; i < n; i++) {
    const u = (i / (n - 1)) * 2.0 - 1.0;
    const x0 = u * half;
    const yA = saddle_warp * u * -1.0;
    const yB = saddle_warp * u * 1.0;
    const len = sqrt(span * span + (yB - yA) * (yB - yA));
    const angX = atan2(yB - yA, span);

    // U-family concrete ribs (#D8D2C7)
    fill(0.847, 0.824, 0.780);
    push();
    translate(x0, (yA + yB) * 0.5, 0);
    rotateX(-angX);
    box(0.044, 0.056, len);
    pop();

    // V-family concrete ribs (#B9B2A6)
    fill(0.725, 0.698, 0.651);
    push();
    translate(0, (yA + yB) * 0.5, x0);
    rotateZ(angX);
    box(len, 0.056, 0.044);
    pop();
  }

  // Warm bronze diagonal tension tie-cables (#C49A5B)
  fill(0.769, 0.604, 0.357);
  push();
  translate(0, -0.35, 0);
  rotateY(0.7854);
  box(span * 1.4, 0.018, 0.018);
  pop();
  push();
  translate(0, -0.35, 0);
  rotateY(-0.7854);
  box(span * 1.4, 0.018, 0.018);
  pop();

  // Dark slate base plinth (#4B4842)
  fill(0.294, 0.282, 0.259);
  push();
  translate(0, -0.68, 0);
  box(span * 1.15, 0.06, span * 1.15);
  pop();
}
