// Scarpa Brion Gradoni Vault 3D - Infinite Library (Sketch 3D)
param("steps", 5, 3, 8);
param("rib_count", 4, 2, 6);
param("cam_tilt", 0.36, 0.15, 0.65);
param("spin", 0.16, 0.0, 0.45);

function draw(t) {
  rotateX(cam_tilt + 0.04 * sin(t * 0.35));
  rotateY(t * spin - 0.58);

  const nS = floor(steps);
  const nR = floor(rib_count);

  // Dark slate architectural plinth (#1A1815)
  fill(0.102, 0.094, 0.082);
  push();
  translate(0, -1.05, 0);
  box(3.6, 0.14, 3.0);
  pop();

  // Twin stepped gradoni concrete buttress wings (#D9D0BE)
  for (let side = -1; side <= 1; side += 2) {
    for (let s = 0; s < nS; s++) {
      const u = s / max(1, nS - 1);
      const w = 0.24 + (nS - s) * 0.08;
      const y = -0.92 + s * 0.16;
      const x = side * (0.42 + s * 0.07);
      fill(0.851 - u * 0.08, 0.816 - u * 0.08, 0.745 - u * 0.07);
      push();
      translate(x, y, 0.25);
      box(w, 0.14, 2.2 - s * 0.12);
      pop();
    }
  }

  // Central recessed concrete lintel & shadow channel
  fill(0.690, 0.525, 0.376);
  push();
  translate(0, -0.08, -0.15);
  box(1.95, 0.18, 0.55);
  pop();

  // Cantilevered arched concrete spine ribs
  for (let r = 0; r < nR; r++) {
    const rx = ((r / max(1, nR - 1)) - 0.5) * 0.85;
    for (let k = 0; k < 10; k++) {
      const u = k / 9.0;
      const ang = (u - 0.5) * 1.1;
      const y = 0.05 + cos(ang) * 0.95;
      const z = -0.35 + sin(ang) * 0.95;
      fill(0.835, 0.796, 0.725);
      push();
      translate(rx, y, z);
      rotateX(-ang * 0.7);
      box(0.11, 0.24, 0.22);
      pop();
    }
  }

  // Oxidized bronze architectural inlay tie-bars (#7A633C)
  fill(0.478, 0.388, 0.235);
  for (let b = -1; b <= 1; b += 2) {
    push();
    translate(0, 0.42 + b * 0.24, -0.35);
    box(1.42, 0.04, 0.06);
    pop();
  }
}
