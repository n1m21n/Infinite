// Henneberg Surface 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Ernst Lebrecht Henneberg's non-orientable minimal surface (1875) with self-intersecting Möbius band topology.
param("scale", 0.72, 0.2, 2.2);
param("tube_r", 0.02, 0.006, 0.05);
param("spin", 0.35, 0.0, 1.5);
param("layers", 40, 14, 64);
param("steps", 48, 16, 96);
param("u_range", 1.25, 0.4, 1.8);
param("v_range", 3.14159, 1.5, 6.283);
param("hue", 0.62, 0.0, 1.0);

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(0.5);

  const uMin = -u_range, uMax = u_range;
  const vMin = -v_range * 0.5, vMax = v_range * 0.5;

  for (let i = 0; i < layers; i++) {
    const uFrac = i / (layers - 1);
    const u = uMin + uFrac * (uMax - uMin);
    const pts = [];

    const sinhU = Math.sinh(u);
    const sinh3U = Math.sinh(3.0 * u);
    const cosh2U = Math.cosh(2.0 * u);

    for (let j = 0; j <= steps; j++) {
      const vFrac = j / steps;
      const v = vMin + vFrac * (vMax - vMin);

      const cosV = Math.cos(v);
      const sinV = Math.sin(v);
      const cos3V = Math.cos(3.0 * v);
      const sin3V = Math.sin(3.0 * v);
      const cos2V = Math.cos(2.0 * v);

      // Henneberg minimal surface parametric formulas
      const x = 2.0 * sinhU * cosV - (2.0 / 3.0) * sinh3U * cos3V;
      const y = 2.0 * sinhU * sinV + (2.0 / 3.0) * sinh3U * sin3V;
      const z = 2.0 * cosh2U * cos2V;

      pts.push([x * scale, (z - 1.0) * scale, y * scale]);
    }

    const frac = i / layers;
    // Iridescent brand signal-to-emerald gradient
    fill(hsl((hue + frac * 0.45) % 1.0, 0.88, 0.58));
    for (let j = 0; j < steps; j++) {
      const p1 = pts[j], p2 = pts[j + 1];
      tube(p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], tube_r, 8);
    }
  }

  // Cross transversal generative ruling ribs
  for (let j = 0; j <= steps; j += 6) {
    const vFrac = j / steps;
    const v = vMin + vFrac * (vMax - vMin);
    const cosV = Math.cos(v);
    const sinV = Math.sin(v);
    const cos3V = Math.cos(3.0 * v);
    const sin3V = Math.sin(3.0 * v);
    const cos2V = Math.cos(2.0 * v);
    const pts = [];

    for (let i = 0; i < layers; i++) {
      const uFrac = i / (layers - 1);
      const u = uMin + uFrac * (uMax - uMin);
      const sinhU = Math.sinh(u);
      const sinh3U = Math.sinh(3.0 * u);
      const cosh2U = Math.cosh(2.0 * u);

      const x = 2.0 * sinhU * cosV - (2.0 / 3.0) * sinh3U * cos3V;
      const y = 2.0 * sinhU * sinV + (2.0 / 3.0) * sinh3U * sin3V;
      const z = 2.0 * cosh2U * cos2V;

      pts.push([x * scale, (z - 1.0) * scale, y * scale]);
    }

    fill(hsl((hue + 0.45 + vFrac * 0.3) % 1.0, 0.92, 0.62));
    for (let i = 0; i < layers - 1; i++) {
      const p1 = pts[i], p2 = pts[i + 1];
      tube(p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], tube_r * 0.65, 6);
    }
  }

  pop();
}
