// Ito Taichung Catenoid Vault 3D - Infinite Library (Sketch 3D)
param("arches", 20, 10, 32);
param("radius", 1.35, 0.8, 2.2);
param("undulate", 0.34, 0.05, 0.65);
param("spin", 0.16, 0.0, 0.5);

function draw(t) {
  const n = floor(arches);
  rotateY(t * spin + 0.45);
  rotateX(0.22);

  // Curved acoustic catenoid shell rings
  for (let i = 0; i < n; i++) {
    const u = (i / (n - 1)) * 2.0 - 1.0;
    const z = u * 1.45;
    const waist = 1.0 - 0.36 * cos(u * PI);
    const r = radius * waist;
    const segs = 18;

    for (let s = 0; s < segs; s++) {
      const a = (s / segs) * TAU;
      const wave = undulate * sin(a * 3.0 + u * PI + t * 0.4);
      const px = cos(a) * (r + wave);
      const py = sin(a) * (r * 0.78 + wave * 0.6);

      push();
      translate(px, py, z);
      rotateZ(a + PI * 0.5);
      rotateY(u * 0.35);
      const shade = 0.68 + 0.24 * cos(a - 0.7);
      fill(shade * 0.92, shade * 0.95, shade * 0.99);
      box(0.32, 0.09, 0.16);
      pop();
    }
  }
}
