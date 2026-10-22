// Calabi-Yau 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// 3D cross-section projection of the 6D Calabi-Yau quintic threefold Kähler manifold.
param("scale", 1.35, 0.4, 3.0);
param("tube_r", 0.022, 0.008, 0.06);
param("spin", 0.35, 0.0, 1.5);
param("layers", 36, 12, 64);
param("steps", 48, 16, 96);
param("folds", 5.0, 2.0, 8.0);
param("hue", 0.64, 0.0, 1.0);

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(0.62);

  const uMin = 0.0, uMax = Math.PI * 2.0;
  const vMin = -0.5 * Math.PI, vMax = 0.5 * Math.PI;

  for (let i = 0; i < layers; i++) {
    const uFrac = i / layers;
    const u = uMin + uFrac * (uMax - uMin);
    const pts = [];

    const n = folds;
    const alpha = (2.0 * Math.PI * uFrac) / n;

    for (let j = 0; j <= steps; j++) {
      const vFrac = j / steps;
      const v = vMin + vFrac * (vMax - vMin);

      // Calabi-Yau complex quintic cross-section projection
      const cosV = Math.cos(v);
      const sinV = Math.sin(v);
      const r = 1.0 + 0.32 * Math.cos(n * u);

      const x = r * cosV * Math.cos(u + alpha);
      const y = r * cosV * Math.sin(u + alpha);
      const z = 0.65 * sinV + 0.28 * Math.sin(n * v);

      pts.push([x * scale, z * scale, y * scale]);
    }

    const frac = i / layers;
    // Iridescent brand color gradient
    fill(hsl((hue + frac * 0.48) % 1.0, 0.88, 0.54));
    for (let j = 0; j < steps; j++) {
      const p1 = pts[j], p2 = pts[j + 1];
      tube(p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], tube_r, 8);
    }
  }

  pop();
}
