// Richmond Surface 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Herbert Richmond complete minimal surface with monkey-saddle central singularity (1904).
param("scale", 1.25, 0.4, 3.0);
param("tube_r", 0.024, 0.008, 0.07);
param("spin", 0.3, 0.0, 1.5);
param("branches", 3.0, 2.0, 5.0);
param("layers", 32, 12, 60);
param("steps", 48, 16, 96);
param("hue", 0.58, 0.0, 1.0);

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(0.7);

  const uMin = 0.25, uMax = 1.35;
  const vMin = -Math.PI, vMax = Math.PI;
  const m = branches;

  for (let i = 0; i < layers; i++) {
    const uFrac = i / (layers - 1);
    const u = uMin + uFrac * (uMax - uMin);
    const pts = [];

    for (let j = 0; j <= steps; j++) {
      const vFrac = j / steps;
      const v = vMin + vFrac * (vMax - vMin);

      // Richmond minimal surface Weierstrass-Enneper representation
      const p1 = 2.0 * m - 1.0;
      const uInv = 1.0 / (2.0 * u);
      const uPowP = Math.pow(u, p1) / (2.0 * p1);

      const x = -uInv * Math.cos(v) - uPowP * Math.cos(p1 * v);
      const y = -uInv * Math.sin(v) + uPowP * Math.sin(p1 * v);
      const z = (1.0 / m) * Math.pow(u, m) * Math.cos(m * v);

      pts.push([x * scale, z * scale, y * scale]);
    }

    const frac = i / layers;
    // Iridescent brand color gradient
    fill(hsl((hue + frac * 0.44) % 1.0, 0.86, 0.52));
    for (let j = 0; j < steps; j++) {
      const p1 = pts[j], p2 = pts[j + 1];
      tube(p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], tube_r, 8);
    }
  }

  pop();
}
