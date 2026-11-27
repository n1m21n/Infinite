// Raku Glaze Craquelure - Infinite Library (Sketch 2D)
param("cells", 28, 12, 48);
param("glaze_rings", 9, 4, 16);
param("drift", 0.08, 0.0, 0.4);
param("grog_count", 650, 150, 1200);

function draw(t) {
  // Raw stoneware bone ground (#E4DDD2)
  background(0.894, 0.867, 0.824);
  const w = width;
  const h = height;
  const side = min(w, h);
  const cx = w * 0.5;
  const cy = h * 0.5;
  const rad = side * 0.39;

  // Fine mineral stoneware grog speckles
  noStroke();
  for (let i = 0; i < grog_count; i++) {
    const a = i * 2.3999632;
    const r = sqrt((i + 0.5) / grog_count) * rad * 1.16;
    const gx = cx + cos(a) * r;
    const gy = cy + sin(a) * r;
    fill(0.24, 0.19, 0.16, (i % 5 === 0) ? 0.26 : 0.11);
    circle(gx, gy, (i % 7 === 0) ? 2.4 : 1.3);
  }

  // Concentric iron-oxide raku glaze pooling
  const nr = floor(glaze_rings);
  for (let k = nr; k >= 1; k--) {
    const u = k / nr;
    const rr = rad * (0.24 + 0.74 * u);
    noStroke();
    const ox = 0.62 - 0.14 * u;
    const oy = 0.21 + 0.08 * u;
    const oz = 0.13 + 0.05 * u;
    fill(ox, oy, oz, 0.34 + 0.18 * (1.0 - u));
    circle(cx + sin(k * 1.9 + t * drift) * 4.0, cy + cos(k * 2.3) * 4.0, rr * 2.0);
  }

  // Warm kiln ash halo inside the rim
  noFill();
  stroke(0.42, 0.16, 0.10, 0.55);
  strokeWeight(22.0);
  circle(cx, cy, rad * 1.84);

  // Raw unglazed stoneware rim ring
  noFill();
  stroke(0.82, 0.77, 0.70, 0.92);
  strokeWeight(14.0);
  circle(cx, cy, rad * 2.0);
  stroke(0.24, 0.17, 0.14, 0.68);
  strokeWeight(2.0);
  circle(cx, cy, rad * 2.04);

  // Branching carbon-trapped craquelure hairline fractures
  const nc = floor(cells);
  for (let i = 0; i < nc; i++) {
    const a0 = i * 2.3999632 + sin(i * 3.1) * 0.2;
    const r0 = sqrt((i + 0.4) / nc) * rad * 0.88;
    let px = cx + cos(a0) * r0;
    let py = cy + sin(a0) * r0;
    let dir = a0 + 1.15 + 0.3 * sin(i * 1.7 + t * drift);

    stroke(0.14, 0.10, 0.09, 0.72);
    strokeWeight(1.5);
    noFill();
    beginShape();
    vertex(px, py);
    const steps = 7;
    for (let s = 1; s <= steps; s++) {
      dir += 0.42 * sin(i * 4.3 + s * 2.1 + t * drift);
      const stepLen = rad * 0.072 * (1.0 - s * 0.06);
      px += cos(dir) * stepLen;
      py += sin(dir) * stepLen;
      const dCenter = sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
      if (dCenter > rad * 0.95) break;
      vertex(px, py);
    }
    endShape();

    // Secondary fine tributary crack
    if (i % 2 === 0) {
      let bx = cx + cos(a0) * r0;
      let by = cy + sin(a0) * r0;
      let bdir = dir - 1.05;
      stroke(0.22, 0.16, 0.13, 0.45);
      strokeWeight(0.9);
      beginShape();
      vertex(bx, by);
      for (let s = 1; s <= 4; s++) {
        bdir += 0.35 * cos(i * 2.7 + s * 1.9);
        bx += cos(bdir) * rad * 0.055;
        by += sin(bdir) * rad * 0.055;
        const dC = sqrt((bx - cx) * (bx - cx) + (by - cy) * (by - cy));
        if (dC > rad * 0.95) break;
        vertex(bx, by);
      }
      endShape();
    }
  }
}
