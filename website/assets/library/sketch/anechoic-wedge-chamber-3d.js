// Anechoic Wedge Chamber 3D - Infinite Library (Sketch 3D)
param("clusters", 5, 3, 7);
param("wedgeLen", 0.52, 0.25, 0.85);
param("breath", 0.12, 0.0, 0.35);
param("spin", 0.14, 0.0, 0.5);

function draw(t) {
  const n = floor(clusters);
  rotateY(t * spin + 0.58);
  rotateX(0.32);

  // Concrete chamber base slab (#232A30)
  push();
  translate(0, -0.95, 0);
  fill(0.137, 0.165, 0.188);
  box(2.7, 0.1, 2.7);
  pop();

  // Suspended sodium-amber acoustic measurement datum frame (#D9A84E)
  push();
  translate(0, -0.58, 0);
  fill(0.851, 0.659, 0.306);
  box(1.9, 0.03, 0.04);
  box(0.04, 0.03, 1.9);
  pop();

  // Alternating orthogonal acoustic absorption wedge clusters
  const step = 0.44;
  const half = (n - 1) * 0.5;
  for (let iy = 0; iy < n; iy++) {
    for (let ix = 0; ix < n; ix++) {
      const px = (ix - half) * step;
      const py = (iy - half) * step + 0.05;
      const parity = (ix + iy) % 2;
      const pulse = 1.0 + breath * sin(t * 1.4 + ix * 0.6 + iy * 0.5);

      // Back wall wedge cluster
      push();
      translate(px, py, -1.05);
      rotateX(PI * 0.5);
      if (parity === 1) rotateY(PI * 0.5);
      fill(parity === 0 ? 0.306 : 0.478, parity === 0 ? 0.318 : 0.510, parity === 0 ? 0.275 : 0.518);
      cone(0.19, wedgeLen * pulse);
      pop();

      // Left wall wedge cluster
      push();
      translate(-1.05, py, px);
      rotateZ(-PI * 0.5);
      if (parity === 0) rotateY(PI * 0.5);
      fill(parity === 1 ? 0.306 : 0.420, parity === 1 ? 0.318 : 0.451, parity === 1 ? 0.275 : 0.460);
      cone(0.19, wedgeLen * pulse);
      pop();

      // Floor sub-grid wedges
      if (ix > 0 && iy > 0 && ix < n - 1 && iy < n - 1) {
        push();
        translate(px, -0.78, (iy - half) * step);
        fill(0.24, 0.26, 0.25);
        cone(0.16, wedgeLen * 0.65);
        pop();
      }
    }
  }

  // Central omnidirectional measurement microphone mast
  push();
  translate(0, -0.1, 0);
  fill(0.851, 0.659, 0.306);
  cylinder(0.025, 0.95);
  translate(0, 0.52, 0);
  fill(0.92, 0.93, 0.95);
  sphere(0.085);
  pop();
}
