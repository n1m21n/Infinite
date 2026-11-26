// Penrose Ammann Rhombi - Infinite Library (Sketch 2D)
param("subdiv", 4, 2, 5);
param("arcWeight", 2.2, 0.8, 4.0);
param("pulse", 0.22, 0.0, 0.6);
param("drift", 0.08, 0.0, 0.4);

function clamp(v, lo, hi) {
  return min(max(v, lo), hi);
}

function draw(t) {
  // Architectural granite ground (#1C1F24)
  background(0.110, 0.122, 0.141);
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const R = min(w, h) * 0.44;
  const phi = 1.618033988749895;
  const invPhi = 1.0 / phi;

  // Build 10-fold sun wheel of Robinson triangles: [type (0=thin, 1=fat), ax, ay, bx, by, cx, cy]
  let tris = [];
  const rot = t * drift * 0.25;
  for (let i = 0; i < 10; i++) {
    const a0 = (i * TAU) / 10.0 + rot;
    const a1 = ((i + 1) * TAU) / 10.0 + rot;
    const bx = cx + cos(a0) * R;
    const by = cy + sin(a0) * R;
    const px = cx + cos(a1) * R;
    const py = cy + sin(a1) * R;
    if (i % 2 === 0) {
      tris.push([0, cx, cy, px, py, bx, by]);
    } else {
      tris.push([0, cx, cy, bx, by, px, py]);
    }
  }

  const levels = floor(subdiv);
  for (let s = 0; s < levels; s++) {
    const next = [];
    for (let k = 0; k < tris.length; k++) {
      const tr = tris[k];
      const tp = tr[0];
      const ax = tr[1], ay = tr[2];
      const bx = tr[3], by = tr[4];
      const cxp = tr[5], cyp = tr[6];
      if (tp === 0) {
        const px = ax + (bx - ax) * invPhi;
        const py = ay + (by - ay) * invPhi;
        next.push([0, cxp, cyp, px, py, bx, by]);
        next.push([1, px, py, cxp, cyp, ax, ay]);
      } else {
        const qx = bx + (ax - bx) * invPhi;
        const qy = by + (ay - by) * invPhi;
        const rx = bx + (cxp - bx) * invPhi;
        const ry = by + (cyp - by) * invPhi;
        next.push([1, rx, ry, cxp, cyp, ax, ay]);
        next.push([1, qx, qy, rx, ry, bx, by]);
        next.push([0, rx, ry, qx, qy, ax, ay]);
      }
    }
    tris = next;
  }

  // Draw deflated rhombi (pairs of Robinson triangles)
  for (let k = 0; k < tris.length; k++) {
    const tr = tris[k];
    const tp = tr[0];
    const ax = tr[1], ay = tr[2];
    const bx = tr[3], by = tr[4];
    const cxp = tr[5], cyp = tr[6];
    const mx = (bx + cxp) * 0.5;
    const my = (by + cyp) * 0.5;
    const dist = sqrt((mx - cx) * (mx - cx) + (my - cy) * (my - cy)) / R;
    const wave = sin(dist * 10.0 - t * 1.4) * pulse;

    if (tp === 0) {
      // Acute thin tile: deep slate vs warm stone
      const lum = clamp(0.24 + 0.08 * sin(dist * 6.0) + wave * 0.14, 0.12, 0.55);
      fill(lum, lum * 1.04, lum * 1.10);
    } else {
      // Fat tile: weathered granite or ember accent near core
      if (dist < 0.24 && (k % 5 === 0)) {
        fill(0.784, 0.325, 0.212, 0.92);
      } else {
        const lum = clamp(0.42 + 0.10 * cos(dist * 5.0) + wave * 0.12, 0.22, 0.72);
        fill(lum, lum * 0.99, lum * 0.96);
      }
    }
    stroke(0.110, 0.122, 0.141, 0.75);
    strokeWeight(1.0);
    beginShape();
    vertex(ax, ay);
    vertex(bx, by);
    vertex(cxp, cyp);
    endShape(CLOSE);

    // Ammann-style ribbon chord across each triangle
    const p1x = ax + (bx - ax) * 0.618;
    const p1y = ay + (by - ay) * 0.618;
    const p2x = ax + (cxp - ax) * 0.382;
    const p2y = ay + (cyp - ay) * 0.382;
    if (tp === 0) {
      stroke(0.941, 0.929, 0.898, 0.78);
    } else {
      stroke(0.157, 0.173, 0.200, 0.65);
    }
    strokeWeight(arcWeight);
    line(p1x, p1y, p2x, p2y);
  }

  // Outer architectural circular datum & tenfold ticks
  noFill();
  stroke(0.941, 0.929, 0.898, 0.45);
  strokeWeight(1.5);
  circle(cx, cy, R * 2.08);
  for (let i = 0; i < 20; i++) {
    const a = (i * TAU) / 20.0 + rot;
    const r0 = R * 1.04;
    const r1 = R * (i % 2 === 0 ? 1.09 : 1.065);
    line(cx + cos(a) * r0, cy + sin(a) * r0, cx + cos(a) * r1, cy + sin(a) * r1);
  }
}
