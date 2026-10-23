// Jeener Klein 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Patrice Jeener's 8-surface Klein bottle minimal surface immersion with intersecting twisting topological lobes.
param("scale", 0.82, 0.2, 2.5);
param("tube_r", 0.018, 0.005, 0.05);
param("spin", 0.28, 0.0, 1.5);
param("rings", 36, 16, 64);
param("slices", 48, 20, 96);
param("twist", 1.0, 0.4, 2.0);
param("hue", 0.08, 0.0, 1.0);

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(0.55);

  const uMax = 6.2831853;
  const vMax = 6.2831853;

  for (let i = 0; i < rings; i++) {
    const u = (i / rings) * uMax;
    const pts = [];

    const halfU = u * 0.5 * twist;
    const cosHalfU = Math.cos(halfU);
    const sinHalfU = Math.sin(halfU);
    const cosU = Math.cos(u);

    // Toroidal radius modulation
    const r = 2.0 - cosU;

    for (let j = 0; j <= slices; j++) {
      const v = (j / slices) * vMax;
      const cosV = Math.cos(v);
      const sinV = Math.sin(v);
      const sin2V = Math.sin(2.0 * v);

      // Jeener's Klein surface coordinates
      const lobe = r * cosHalfU - sinHalfU * sin2V;
      const x = lobe * cosV;
      const y = lobe * sinV;
      const z = r * sinHalfU + cosHalfU * sin2V;

      pts.push([x * scale * 0.6, z * scale * 0.6, y * scale * 0.6]);
    }

    const frac = i / rings;
    // Ember-to-signal iridescent tone
    fill(hsl((hue + frac * 0.5) % 1.0, 0.9, 0.56));
    for (let j = 0; j < slices; j++) {
      const p1 = pts[j], p2 = pts[j + 1];
      tube(p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], tube_r, 8);
    }
  }

  // Cross generator ruling circles
  for (let j = 0; j <= slices; j += 4) {
    const v = (j / slices) * vMax;
    const cosV = Math.cos(v);
    const sinV = Math.sin(v);
    const sin2V = Math.sin(2.0 * v);
    const pts = [];

    for (let i = 0; i <= rings; i++) {
      const u = (i / rings) * uMax;
      const halfU = u * 0.5 * twist;
      const cosHalfU = Math.cos(halfU);
      const sinHalfU = Math.sin(halfU);
      const cosU = Math.cos(u);
      const r = 2.0 - cosU;

      const lobe = r * cosHalfU - sinHalfU * sin2V;
      const x = lobe * cosV;
      const y = lobe * sinV;
      const z = r * sinHalfU + cosHalfU * sin2V;

      pts.push([x * scale * 0.6, z * scale * 0.6, y * scale * 0.6]);
    }

    fill(hsl((hue + 0.5 + (j / slices) * 0.35) % 1.0, 0.85, 0.62));
    for (let i = 0; i < rings; i++) {
      const p1 = pts[i], p2 = pts[i + 1];
      tube(p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], tube_r * 0.65, 6);
    }
  }

  pop();
}
