// Hyperbolic Poincaré Disk - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Conformal Poincaré disk model with regular hyperbolic tiling {p, q}, geodesic circular arcs and breathing non-Euclidean metric.
param("p_sides", 5, 3, 8);
param("q_order", 4, 3, 6);
param("depth", 3, 1, 5);
param("spin", 0.18, 0.0, 1.0);
param("pulse", 0.35, 0.0, 1.0);
param("hue", 0.58, 0.0, 1.0);
param("scale", 0.95, 0.4, 2.0);

function draw(t) {
  background(0.082, 0.098, 0.188); // brand midnight #151930
  const cx = width * 0.5, cy = height * 0.5;
  const diskR = min(width, height) * 0.45 * scale;
  const rot = t * spin;

  // Draw boundary circle of the Poincaré disk (horizon at infinity)
  noFill();
  strokeWeight(2.5);
  stroke(0.655, 0.694, 1.0, 0.85); // brand signal #A7B1FF
  ellipse(cx, cy, diskR * 2, diskR * 2);

  const p = floor(p_sides);
  const q = floor(q_order);
  const maxD = floor(depth);

  // Fundamental polygon radius in the Poincaré disk
  // cosh(r) = cos(pi / q) / sin(pi / p)
  const sinP = sin(PI / p);
  const cosQ = cos(PI / q);
  const coshR = max(1.001, cosQ / max(0.001, sinP));
  const tanhHalfR = sqrt((coshR - 1.0) / (coshR + 1.0));
  const r0 = tanhHalfR * (1.0 + sin(t * 1.6) * 0.15 * pulse);

  // Collect vertices of the central hyperbolic polygon
  const verts = [];
  for (let i = 0; i < p; i++) {
    const angle = rot + (i * TAU) / p;
    verts.push({
      x: cx + diskR * r0 * cos(angle),
      y: cy + diskR * r0 * sin(angle),
      angle: angle
    });
  }

  // Draw hyperbolic geodesic edges as circular arcs orthogonal to the boundary
  function drawGeodesic(p1, p2, col, w) {
    const u1 = (p1.x - cx) / diskR;
    const v1 = (p1.y - cy) / diskR;
    const u2 = (p2.x - cx) / diskR;
    const v2 = (p2.y - cy) / diskR;

    const cross = u1 * v2 - v1 * u2;
    if (abs(cross) < 0.0001) {
      strokeWeight(w);
      stroke(col);
      line(p1.x, p1.y, p2.x, p2.y);
      return;
    }

    const d1 = 1.0 + u1 * u1 + v1 * v1;
    const d2 = 1.0 + u2 * u2 + v2 * v2;
    const cX = (v2 * d1 - v1 * d2) / (2.0 * cross);
    const cY = (u1 * d2 - u2 * d1) / (2.0 * cross);
    const rC2 = cX * cX + cY * cY - 1.0;
    if (rC2 <= 0) {
      strokeWeight(w);
      stroke(col);
      line(p1.x, p1.y, p2.x, p2.y);
      return;
    }
    const rC = sqrt(rC2);

    const a1 = atan2(v1 - cY, u1 - cX);
    const a2 = atan2(v2 - cY, u2 - cX);
    let diff = a2 - a1;
    while (diff > PI) diff -= TAU;
    while (diff < -PI) diff += TAU;

    strokeWeight(w);
    stroke(col);
    const steps = 18;
    let prevX = p1.x, prevY = p1.y;
    for (let k = 1; k <= steps; k++) {
      const a = a1 + (diff * k) / steps;
      const px = cx + (cX + rC * cos(a)) * diskR;
      const py = cy + (cY + rC * sin(a)) * diskR;
      line(prevX, prevY, px, py);
      prevX = px;
      prevY = py;
    }
  }

  // Draw central polygon geodesics
  for (let i = 0; i < p; i++) {
    const next = (i + 1) % p;
    drawGeodesic(verts[i], verts[next], hsl(hue, 0.90, 0.65, 0.9), 2.0);
  }

  // Recursive circle inversion reflections into outer hyperbolic sectors
  function reflectPoint(pt, center, radius) {
    const dx = pt.x - center.x;
    const dy = pt.y - center.y;
    const d2 = dx * dx + dy * dy;
    if (d2 < 0.0001) return pt;
    const factor = (radius * radius) / d2;
    return { x: center.x + dx * factor, y: center.y + dy * factor };
  }

  // Generate inverted rings
  let currentLayer = verts.map(v => ({ x: v.x, y: v.y }));
  for (let d = 1; d <= maxD; d++) {
    const frac = d / maxD;
    const col = hsl((hue + frac * 0.45) % 1.0, 0.88, 0.62, 0.75 - frac * 0.35);
    const w = max(0.8, 2.0 - frac * 1.2);
    const nextLayer = [];

    for (let i = 0; i < currentLayer.length; i++) {
      const ptA = currentLayer[i];
      const ptB = currentLayer[(i + 1) % currentLayer.length];
      const midX = (ptA.x + ptB.x) * 0.5;
      const midY = (ptA.y + ptB.y) * 0.5;

      const normDist = sqrt((midX - cx) * (midX - cx) + (midY - cy) * (midY - cy)) / diskR;
      if (normDist < 0.98) {
        drawGeodesic(ptA, ptB, col, w);
        // Tangent circle center for inversion
        const subR = (1.0 - normDist) * diskR * 0.45;
        const subCenter = { x: midX, y: midY };
        const invA = reflectPoint(ptA, subCenter, subR);
        const invB = reflectPoint(ptB, subCenter, subR);
        nextLayer.push(invA);
        nextLayer.push(invB);
      }
    }
    currentLayer = nextLayer.slice(0, 360);
  }

  // Orbiting non-Euclidean geodesic quanta
  const quanta = 6;
  for (let q = 0; q < quanta; q++) {
    const ang = rot * 1.5 + (q * TAU) / quanta;
    const rad = diskR * r0 * (0.55 + 0.35 * sin(t * 2.0 + q));
    const qx = cx + rad * cos(ang);
    const qy = cy + rad * sin(ang);

    fill(1.0, 0.42, 0.21, 0.9); // brand ember #FF6B35
    noStroke();
    ellipse(qx, qy, 6.0, 6.0);
  }
}
