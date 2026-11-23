// Muqarnas Isfahan Peacock Vault 3D - Infinite Library (Sketch 3D)
param("rings", 7, 4, 10);
param("petals", 16, 10, 24);
param("rib_arch", 0.32, 0.15, 0.55);
param("spin", 0.18, 0.0, 0.5);

function draw(t) {
  rotateX(0.48 + 0.07 * cos(t * 0.3));
  rotateY(t * spin - 0.25);

  const nR = floor(rings);
  const nP = floor(petals);

  // Four turquoise glazed squinch ribs (#38899C) rising to the dome drum
  fill(0.22, 0.54, 0.61);
  for (let q = 0; q < 4; q++) {
    const qa = q * (PI * 0.5) + (PI * 0.25);
    for (let s = 0; s < 10; s++) {
      const u = s / 9.0;
      const rr = 1.75 - u * 0.55;
      const yy = -1.0 + sin(u * (PI * 0.5)) * 1.15;
      push();
      translate(cos(qa) * rr, yy, sin(qa) * rr);
      rotateY(-qa);
      rotateZ(u * 0.65);
      cylinder(0.085, 0.22);
      pop();
    }
  }

  // Cobalt blue glazed drum band (#1B3B6F)
  fill(0.106, 0.231, 0.435);
  push();
  translate(0, 0.15, 0);
  torus(1.22, 0.075);
  pop();

  // Concentric spiralling peacock-feather tile medallions (#C6934B & #1B3B6F)
  for (let r = 0; r < nR; r++) {
    const u = r / max(1, nR - 1);
    const phi = u * (PI * 0.5) * 0.92;
    const rad = 1.20 * cos(phi);
    const yy = 0.20 + 1.05 * sin(phi);
    const count = max(6, floor(nP * (1.0 - u * 0.55)));
    const twist = r * 0.18;

    for (let i = 0; i < count; i++) {
      const a = (i / count) * TAU + twist;
      const x = cos(a) * rad;
      const z = sin(a) * rad;

      push();
      translate(x, yy, z);
      rotateY(-a);
      rotateZ(phi);

      // Sandstone gold medallion tile
      fill(0.78 - u * 0.12, 0.58 - u * 0.10, 0.29);
      box(0.20 * (1.0 - u * 0.45), 0.05, 0.26 * (1.0 - u * 0.45));

      // Cobalt & turquoise inset boss
      fill(0.11, 0.24, 0.46);
      translate(0, 0.032, 0);
      box(0.10 * (1.0 - u * 0.45), 0.025, 0.13 * (1.0 - u * 0.45));
      pop();
    }
  }

  // Central golden sun rosette
  fill(0.92, 0.76, 0.42);
  push();
  translate(0, 1.26, 0);
  sphere(0.14);
  pop();
}
