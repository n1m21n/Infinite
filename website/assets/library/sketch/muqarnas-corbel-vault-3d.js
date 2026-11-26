// Muqarnas Corbel Vault 3D - Infinite Library (Sketch 3D)
param("tiers", 6, 4, 9);
param("facets", 12, 8, 20);
param("vaultH", 1.85, 1.2, 2.6);
param("spin", 0.16, 0.0, 0.6);

function draw(t) {
  rotateY(t * spin + 0.42);
  rotateX(0.32);

  // Dark sandstone architectural plinth
  push();
  translate(0, -0.95, 0);
  fill(0.14, 0.13, 0.12);
  cylinder(1.62, 0.12, 8);
  pop();

  // Outer colonnade & pointed arch piers
  const pierN = 8;
  for (let p = 0; p < pierN; p++) {
    const pa = (p * TAU) / pierN;
    const pr = 1.38;
    push();
    translate(cos(pa) * pr, -0.35, sin(pa) * pr);
    rotateY(-pa);
    fill(0.76, 0.67, 0.54);
    box(0.16, 1.15, 0.22);
    pop();
  }

  // Ascending tiered muqarnas honeycomb corbels
  const tn = floor(tiers);
  const fn = floor(facets);
  for (let tier = 0; tier < tn; tier++) {
    const u = tier / (tn - 1);
    const rad = 1.30 * (1.0 - u * 0.68);
    const y = -0.25 + u * vaultH * 0.72;
    const count = fn + (tier % 2 === 0 ? 0 : 4);
    const cellW = (TAU * rad) / count * 0.78;
    const cellH = 0.24 * (1.0 - u * 0.35);

    for (let i = 0; i < count; i++) {
      const a = (i * TAU) / count + tier * 0.18;
      push();
      translate(cos(a) * rad, y, sin(a) * rad);
      rotateY(-a + PI * 0.5);
      rotateX(0.28 + u * 0.25);

      if ((tier + i) % 3 === 0) {
        fill(0.118, 0.267, 0.541); // Glazed cobalt
      } else if ((tier + i) % 3 === 1) {
        fill(0.784, 0.573, 0.212); // Ochre gold
      } else {
        fill(0.169, 0.478, 0.549); // Turquoise tile
      }
      box(cellW, cellH, 0.16);

      // Inner stalactite drop prism
      translate(0, -cellH * 0.42, 0.05);
      fill(0.84, 0.76, 0.64);
      box(cellW * 0.45, cellH * 0.55, 0.08);
      pop();
    }
  }

  // Central star-polygon cupola oculus
  push();
  translate(0, -0.25 + vaultH * 0.78, 0);
  fill(0.92, 0.78, 0.42);
  torus(0.32, 0.055, 8, 24);
  pop();
}
