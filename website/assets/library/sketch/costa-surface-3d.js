// Costa Surface 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Celso Costa's complete embedded minimal surface of genus 1 with three ends (1982).
param("scale", 1.4, 0.4, 3.0);
param("tube_r", 0.025, 0.01, 0.08);
param("spin", 0.3, 0.0, 1.5);
param("layers", 32, 12, 64);
param("steps", 48, 16, 96);
param("hue", 0.62, 0.0, 1.0);

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(0.55);

  const uMin = -1.3, uMax = 1.3;
  const vMin = -Math.PI + 0.1, vMax = Math.PI - 0.1;

  for (let i = 0; i < layers; i++) {
    const uFrac = i / (layers - 1);
    const u = uMin + uFrac * (uMax - uMin);
    const pts = [];

    for (let j = 0; j <= steps; j++) {
      const vFrac = j / steps;
      const v = vMin + vFrac * (vMax - vMin);

      // Weierstrass-Enneper Costa immersion approximation
      const cosh2u = cosh(2.0 * u);
      const cos2v = cos(2.0 * v);
      const denom = max(0.15, cosh2u - cos2v);

      const x = 0.5 * (u - (sinh(2.0 * u) * cos2v) / denom);
      const y = -0.5 * (v + (sinh(2.0 * u) * sin(2.0 * v)) / denom);
      const z = 1.414 * (sinh(u) * cos(v)) / sqrt(denom);

      pts.push([x * scale, z * scale, y * scale]);
    }

    const frac = i / layers;
    // Iridescent brand color gradient
    fill(hsl((hue + frac * 0.45) % 1.0, 0.85, 0.52));
    for (let j = 0; j < steps; j++) {
      const p1 = pts[j], p2 = pts[j + 1];
      tube(p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], tube_r, 8);
    }
  }

  pop();
}
