// Candela Palmira Saddle 3D - Infinite Library (Sketch 3D)
param("grid_u", 16, 10, 24);
param("saddle_k", 0.72, 0.35, 1.15);
param("spin", 0.18, 0.0, 0.6);

function draw(t) {
  const nu = floor(grid_u);
  const nv = 12;

  push();
  scale(0.74, 0.74, 0.74);
  rotateX(-0.34);
  rotateY(0.55 + sin(t * spin) * 0.45);

  // Travertine stepped podium
  push();
  translate(0, -0.78, 0);
  fill(0.32, 0.30, 0.28);
  box(2.6, 0.12, 2.2);
  pop();

  // Ruled hyperbolic paraboloid surface z = k * (x^2 - y^2)
  for (let i = 0; i < nu; i++) {
    const u = (i / (nu - 1)) * 2.0 - 1.0;
    for (let j = 0; j < nv; j++) {
      const v = (j / (nv - 1)) * 2.0 - 1.0;
      const x = u * 1.35;
      const z = v * 1.15;
      const y = 0.25 + saddle_k * (u * u * 0.65 - v * v * 0.55);

      push();
      translate(x, y, z);
      rotateZ(u * 0.42 * saddle_k);
      rotateX(-v * 0.38 * saddle_k);
      const lum = 0.72 + 0.18 * (u * 0.5 - v * 0.5);
      fill(lum, lum * 0.97, lum * 0.92);
      box(0.19, 0.045, 0.21);
      pop();
    }
  }

  // Cantilevered dark bronze/steel tension piers
  for (let side = -1; side <= 1; side += 2) {
    push();
    translate(0, -0.35, side * 0.95);
    fill(0.22, 0.20, 0.18);
    box(0.18, 0.85, 0.18);
    pop();
  }

  pop();
}