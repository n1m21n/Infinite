// Catalan Surface 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Eugène Charles Catalan's ruled minimal surface (1842) with cycloidal directrix and undulating helical ribbons.
param("scale", 0.65, 0.2, 2.0);
param("tube_r", 0.022, 0.008, 0.06);
param("spin", 0.3, 0.0, 1.5);
param("layers", 38, 12, 64);
param("steps", 48, 16, 96);
param("u_range", 6.28, 2.0, 12.56);
param("v_range", 1.4, 0.5, 2.5);
param("hue", 0.08, 0.0, 1.0);

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(0.55);

  const uMin = -u_range * 0.5, uMax = u_range * 0.5;
  const vMin = -v_range, vMax = v_range;

  for (let i = 0; i < layers; i++) {
    const uFrac = i / (layers - 1);
    const u = uMin + uFrac * (uMax - uMin);
    const pts = [];

    const sinU = Math.sin(u);
    const cosU = Math.cos(u);
    const sinHalfU = Math.sin(u * 0.5);

    for (let j = 0; j <= steps; j++) {
      const vFrac = j / steps;
      const v = vMin + vFrac * (vMax - vMin);

      const coshV = Math.cosh(v);
      const sinhHalfV = Math.sinh(v * 0.5);

      // Catalan ruled minimal surface
      const x = u - sinU * coshV;
      const y = 1.0 - cosU * coshV;
      const z = 4.0 * sinHalfU * sinhHalfV;

      pts.push([x * scale, z * scale, (y - 1.0) * scale]);
    }

    const frac = i / layers;
    // Iridescent brand ember-to-signal gradient
    fill(hsl((hue + frac * 0.55) % 1.0, 0.9, 0.56));
    for (let j = 0; j < steps; j++) {
      const p1 = pts[j], p2 = pts[j + 1];
      tube(p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], tube_r, 8);
    }
  }

  // Cross generator ruling lines connecting adjacent ribs
  for (let j = 0; j <= steps; j += 6) {
    const vFrac = j / steps;
    const v = vMin + vFrac * (vMax - vMin);
    const coshV = Math.cosh(v);
    const sinhHalfV = Math.sinh(v * 0.5);
    const pts = [];

    for (let i = 0; i < layers; i++) {
      const uFrac = i / (layers - 1);
      const u = uMin + uFrac * (uMax - uMin);
      const sinU = Math.sin(u);
      const cosU = Math.cos(u);
      const sinHalfU = Math.sin(u * 0.5);

      const x = u - sinU * coshV;
      const y = 1.0 - cosU * coshV;
      const z = 4.0 * sinHalfU * sinhHalfV;

      pts.push([x * scale, z * scale, (y - 1.0) * scale]);
    }

    fill(hsl((hue + 0.5 + vFrac * 0.3) % 1.0, 0.85, 0.6));
    for (let i = 0; i < layers - 1; i++) {
      const p1 = pts[i], p2 = pts[i + 1];
      tube(p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], tube_r * 0.7, 6);
    }
  }

  pop();
}
