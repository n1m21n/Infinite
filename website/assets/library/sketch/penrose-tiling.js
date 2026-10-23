// Penrose Tiling - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Aperiodic Penrose P3 tiling with 5-fold decagonal quasicrystal symmetry and recursive golden-ratio subdivision.
param("subdiv", 4, 1, 6);
param("scale", 1.05, 0.4, 2.5);
param("spin", 0.12, 0.0, 1.0);
param("gap", 0.04, 0.0, 0.2);
param("fade", 0.85, 0.2, 1.0);

function draw(t) {
  background(0.082, 0.098, 0.188); // brand midnight #151930
  const cx = width * 0.5, cy = height * 0.5;
  const radius = min(width, height) * 0.54 * scale;
  const phi = (1.0 + Math.sqrt(5.0)) * 0.5;
  const rot = t * spin;

  // Robinson triangles representation: type 0 = thin (golden triangle), type 1 = thick (golden gnomon)
  // [type, [ax, ay], [bx, by], [cx, cy]]
  let triangles = [];

  // Generate 10 decagonal golden triangles radiating from centre
  for (let i = 0; i < 10; i++) {
    const a1 = rot + (2 * i - 1) * Math.PI / 10.0;
    const a2 = rot + (2 * i + 1) * Math.PI / 10.0;
    const bx = cx + radius * Math.cos(a1);
    const by = cy + radius * Math.sin(a1);
    const ccx = cx + radius * Math.cos(a2);
    const ccy = cy + radius * Math.sin(a2);

    if (i % 2 === 0) {
      triangles.push([0, [cx, cy], [bx, by], [ccx, ccy]]);
    } else {
      triangles.push([0, [cx, cy], [ccx, ccy], [bx, by]]);
    }
  }

  // Recursive deflation
  const depth = floor(subdiv);
  for (let s = 0; s < depth; s++) {
    const nextTris = [];
    for (let k = 0; k < triangles.length; k++) {
      const type = triangles[k][0];
      const a = triangles[k][1], b = triangles[k][2], c = triangles[k][3];

      if (type === 0) {
        // Golden triangle subdivides into 1 golden triangle + 1 golden gnomon
        const px = a[0] + (b[0] - a[0]) / phi;
        const py = a[1] + (b[1] - a[1]) / phi;
        nextTris.push([0, c, [px, py], b]);
        nextTris.push([1, [px, py], c, a]);
      } else {
        // Golden gnomon subdivides into 1 golden triangle + 1 golden gnomon
        const qx = b[0] + (a[0] - b[0]) / phi;
        const qy = b[1] + (a[1] - b[1]) / phi;
        const rx = b[0] + (c[0] - b[0]) / phi;
        const ry = b[1] + (c[1] - b[1]) / phi;
        nextTris.push([1, [rx, ry], [qx, qy], b]);
        nextTris.push([0, [qx, qy], a, c]);
        nextTris.push([1, [qx, qy], c, [rx, ry]]);
      }
    }
    triangles = nextTris;
  }

  strokeWeight(1.2);
  for (let k = 0; k < triangles.length; k++) {
    const type = triangles[k][0];
    const a = triangles[k][1], b = triangles[k][2], c = triangles[k][3];

    // Distance from center for radial shading
    const midX = (a[0] + b[0] + c[0]) / 3.0;
    const midY = (a[1] + b[1] + c[1]) / 3.0;
    const d = Math.sqrt((midX - cx) * (midX - cx) + (midY - cy) * (midY - cy)) / radius;

    // Brand color palette: signal #A7B1FF for thin, emerald #4EF2B8 for thick, ember accents
    if (type === 0) {
      fill(0.655, 0.694, 1.0, (1.0 - d * 0.4) * fade * 0.7);
      stroke(0.96, 0.97, 1.0, 0.85);
    } else {
      fill(0.306, 0.949, 0.722, (1.0 - d * 0.4) * fade * 0.7);
      stroke(1.0, 0.42, 0.208, 0.85);
    }

    // Shrink triangle slightly by gap
    const g = gap * 0.1;
    const ax = a[0] * (1.0 - g) + midX * g, ay = a[1] * (1.0 - g) + midY * g;
    const bx = b[0] * (1.0 - g) + midX * g, by = b[1] * (1.0 - g) + midY * g;
    const cx_ = c[0] * (1.0 - g) + midX * g, cy_ = c[1] * (1.0 - g) + midY * g;

    triangle(ax, ay, bx, by, cx_, cy_);
  }
}
