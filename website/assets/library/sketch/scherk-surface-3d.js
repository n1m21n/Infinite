// Scherk Minimal Surface 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Heinrich Scherk's doubly periodic minimal surface (1834) with orthogonal intersecting saddle bridges.
param("scale", 1.25, 0.4, 3.0);
param("tube_r", 0.02, 0.008, 0.06);
param("spin", 0.35, 0.0, 1.5);
param("layers", 36, 12, 64);
param("steps", 44, 16, 80);
param("hue", 0.58, 0.0, 1.0);

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(0.75);

  const uLimit = 1.35; // keep inside (-pi/2, pi/2)
  const vLimit = 1.35;

  for (let i = 0; i < layers; i++) {
    const uFrac = i / (layers - 1);
    const u = -uLimit + uFrac * 2.0 * uLimit;
    const pts = [];

    const cosU = Math.cos(u);

    for (let j = 0; j <= steps; j++) {
      const vFrac = j / steps;
      const v = -vLimit + vFrac * 2.0 * vLimit;
      const cosV = Math.cos(v);

      // Scherk minimal surface: z = ln(cos(u) / cos(v))
      const ratio = Math.max(0.001, Math.min(1000.0, cosU / cosV));
      const z = Math.log(ratio) * 0.65;

      pts.push([u * scale, z * scale, v * scale]);
    }

    const frac = i / layers;
    // Brand colour palette gradient: Signal -> Emerald -> Ember
    fill(hsl((hue + frac * 0.45) % 1.0, 0.85, 0.55));
    for (let j = 0; j < steps; j++) {
      const p1 = pts[j], p2 = pts[j + 1];
      tube(p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], tube_r, 8);
    }
  }

  // Cross-struts along v to create the structural minimal lattice
  for (let j = 0; j <= steps; j += 4) {
    const vFrac = j / steps;
    const v = -vLimit + vFrac * 2.0 * vLimit;
    const cosV = Math.cos(v);
    const pts = [];

    for (let i = 0; i < layers; i++) {
      const uFrac = i / (layers - 1);
      const u = -uLimit + uFrac * 2.0 * uLimit;
      const cosU = Math.cos(u);
      const ratio = Math.max(0.001, Math.min(1000.0, cosU / cosV));
      const z = Math.log(ratio) * 0.65;
      pts.push([u * scale, z * scale, v * scale]);
    }

    fill(hsl((hue + (vFrac * 0.5 + 0.3)) % 1.0, 0.9, 0.6));
    for (let i = 0; i < layers - 1; i++) {
      const p1 = pts[i], p2 = pts[i + 1];
      tube(p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], tube_r * 0.75, 6);
    }
  }

  pop();
}
