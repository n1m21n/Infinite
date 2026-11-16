// QRD Basalt Columnar Causeway 3D - Infinite Library (Sketch 3D)
param("rings", 4, 3, 6);
param("stepHeight", 0.95, 0.4, 1.5);
param("orbit", 0.18, 0.0, 1.0);

function draw(t) {
  rotateY(t * orbit + 0.42);
  rotateX(-0.30);

  // Deep slate base slab (#14110F)
  push();
  translate(0, -0.92, 0);
  fill(0.078, 0.067, 0.059);
  box(3.1, 0.12, 3.1);
  pop();

  const nRings = floor(rings);
  const rad = 0.165;
  const dx = rad * 1.78;
  const dz = rad * 1.54;

  // Hexagonal basalt column grid
  for (let q = -nRings; q <= nRings; q++) {
    for (let r = -nRings; r <= nRings; r++) {
      const s = -q - r;
      const dist = max(abs(q), max(abs(r), abs(s)));
      if (dist > nRings) continue;

      const x = (q + r * 0.5) * dx;
      const z = r * dz;
      const hash = ((sin(q * 12.989 + r * 78.233) * 43758.54) % 1 + 1) % 1;
      const terrace = 0.25 + (dist / nRings) * 0.72 + hash * 0.35;
      const wave = 0.04 * sin(t * 1.0 + dist * 0.9 + q * 0.3);
      const h = 0.22 + (terrace + wave) * stepHeight;
      const yMid = -0.86 + h * 0.5;

      // Build crisp hexagonal prism from 3 rotated rectangular prisms
      for (let k = 0; k < 3; k++) {
        push();
        translate(x, yMid, z);
        rotateY(k * (PI / 3.0));
        if (dist % 2 === 0) {
          fill(0.173, 0.149, 0.129); // dark basalt
        } else {
          fill(0.245, 0.212, 0.184); // charcoal basalt
        }
        box(rad * 1.05, h, rad * 1.82);
        pop();
      }

      // Weathered ochre mineral cap on top of each column
      for (let k = 0; k < 3; k++) {
        push();
        translate(x, -0.86 + h + 0.022, z);
        rotateY(k * (PI / 3.0));
        if (hash > 0.52) {
          fill(0.918, 0.898, 0.863); // pale travertine crest
        } else {
          fill(0.541, 0.455, 0.357); // weathered ochre
        }
        box(rad * 0.94, 0.045, rad * 1.66);
        pop();
      }
    }
  }
}
