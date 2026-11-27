// Vals Clerestory Monolith 3D - Infinite Library (Sketch 3D)
param("slabs", 10, 6, 16);
param("slit_gap", 0.12, 0.04, 0.26);
param("spin", 0.12, 0.0, 0.5);

function draw(t) {
  const nS = floor(slabs);

  push();
  translate(0, -0.10, 0);
  scale(0.82);
  rotateX(-0.24);
  rotateY(0.55 + t * spin);

  // Calm mineral thermal basin floor (#3D7880)
  push();
  translate(0, -0.92, 0);
  fill(0.239, 0.471, 0.502);
  box(2.25, 0.08, 2.25);
  pop();

  // Three cantilevered Valser quartzite table piers separated by clerestory slits
  const piers = [
    { x: -0.68 - slit_gap * 0.5, z: -0.25, w: 0.62, d: 1.45, h: 1.55 },
    { x:  0.04,                    z:  0.15, w: 0.56, d: 1.25, h: 1.62 },
    { x:  0.72 + slit_gap * 0.5, z: -0.15, w: 0.58, d: 1.50, h: 1.48 }
  ];

  for (let p = 0; p < piers.length; p++) {
    const pr = piers[p];
    const courseH = pr.h / nS;
    for (let s = 0; s < nS; s++) {
      const y = -0.84 + s * courseH + courseH * 0.5;
      const shade = 0.30 + 0.18 * (0.5 + 0.5 * sin(s * 2.3 + p * 1.7));
      const xOff = (s % 2 === 0 ? 0.015 : -0.015);
      push();
      translate(pr.x + xOff, y, pr.z);
      fill(shade, shade * 1.04, shade * 1.09);
      box(pr.w, courseH * 0.86, pr.d);
      pop();
    }
    // Cantilevered concrete roof table cap
    push();
    translate(pr.x, -0.84 + pr.h + 0.08, pr.z);
    fill(0.56, 0.59, 0.62);
    box(pr.w * 1.18, 0.11, pr.d * 1.12);
    pop();
  }

  // Warm clerestory sunbeam planes slicing through the roof gaps (#D9CBB5)
  for (let b = 0; b < 2; b++) {
    const bx = b === 0 ? -0.32 : 0.38;
    push();
    translate(bx, 0.02, 0.0);
    fill(0.851, 0.796, 0.710);
    box(0.025, 1.75, 1.15);
    pop();
  }

  pop();
}
