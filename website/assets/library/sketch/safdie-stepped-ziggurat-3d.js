// Safdie Stepped Ziggurat 3D - Infinite Library (Sketch 3D)
param("levels", 5, 3, 7);
param("step_out", 0.36, 0.15, 0.65);
param("spin", 0.16, 0.0, 0.5);

function draw(t) {
  const nLvl = floor(levels);

  push();
  scale(0.64, 0.64, 0.64);
  rotateX(-0.38);
  rotateY(0.72 + t * spin);

  // Dark slate courtyard base
  push();
  translate(0, -1.12, 0);
  fill(0.20, 0.22, 0.24);
  box(3.6, 0.12, 3.6);
  pop();

  for (let l = 0; l < nLvl; l++) {
    const y = -0.78 + l * 0.46;
    const arm = 1.15 - l * 0.15;

    for (let k = 0; k < 4; k++) {
      const a = k * 1.5708 + (l % 2) * 0.22;
      push();
      rotateY(a);
      translate(arm * 0.55 + step_out * (l % 2), y, 0);

      // Cast concrete module
      const c = 0.68 - l * 0.04;
      fill(c, c * 0.96, c * 0.89);
      box(0.86, 0.40, 0.54);

      // Recessed architectural window slot
      push();
      translate(0.42, 0.02, 0);
      fill(0.11, 0.13, 0.16);
      box(0.05, 0.20, 0.36);
      pop();

      // Concrete cornice rib
      push();
      translate(0, 0.22, 0);
      fill(0.78, 0.75, 0.70);
      box(0.90, 0.04, 0.58);
      pop();

      pop();
    }
  }

  pop();
}