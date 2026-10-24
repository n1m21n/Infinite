// Cayley Hyperbolic Tree - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Recursive Bethe lattice and Cayley tree projected onto the hyperbolic Poincaré plane with branching nodes and radial breathing.
param("depth", 5, 2, 6);
param("branching", 3, 2, 4);
param("spread", 0.75, 0.4, 1.2);
param("spin", 0.35, 0.0, 2.0);
param("hue", 0.12, 0.0, 1.0);

function draw(t) {
  background(0.043, 0.051, 0.098); // #0B0D19 midnight ground
  const cx = width * 0.5;
  const cy = height * 0.5;
  const diskR = min(width, height) * 0.44;
  const maxDepth = floor(depth);
  const branches = floor(branching);

  // Outer hyperbolic horizon
  noFill();
  strokeWeight(2.0);
  stroke(0.655, 0.694, 1.0, 0.7); // Signal #A7B1FF
  ellipse(cx, cy, diskR * 2, diskR * 2);

  // Poincaré disk background rings
  strokeWeight(1.0);
  for (let r = 1; r <= 3; r++) {
    stroke(0.12, 0.15, 0.28, 0.4);
    ellipse(cx, cy, diskR * 2 * (r / 4), diskR * 2 * (r / 4));
  }

  // Draw hyperbolic geodesic line between two points in the Poincaré disk
  function drawGeodesic(u1, v1, u2, v2, col, w) {
    const cross = u1 * v2 - v1 * u2;
    if (Math.abs(cross) < 1e-4) {
      stroke(col);
      strokeWeight(w);
      line(cx + u1 * diskR, cy + v1 * diskR, cx + u2 * diskR, cy + v2 * diskR);
      return;
    }
    const d1 = 1.0 + u1 * u1 + v1 * v1;
    const d2 = 1.0 + u2 * u2 + v2 * v2;
    const cX = (v2 * d1 - v1 * d2) / (2.0 * cross);
    const cY = (u1 * d2 - u2 * d1) / (2.0 * cross);
    const rC2 = cX * cX + cY * cY - 1.0;
    if (rC2 <= 0) {
      stroke(col);
      strokeWeight(w);
      line(cx + u1 * diskR, cy + v1 * diskR, cx + u2 * diskR, cy + v2 * diskR);
      return;
    }
    const rC = Math.sqrt(rC2);
    const a1 = Math.atan2(v1 - cY, u1 - cX);
    const a2 = Math.atan2(v2 - cY, u2 - cX);
    let diff = a2 - a1;
    while (diff > Math.PI) diff -= 2.0 * Math.PI;
    while (diff < -Math.PI) diff += 2.0 * Math.PI;

    stroke(col);
    strokeWeight(w);
    const segs = 14;
    let prevX = cx + u1 * diskR;
    let prevY = cy + v1 * diskR;
    for (let k = 1; k <= segs; k++) {
      const a = a1 + (diff * k) / segs;
      const nx = cx + (cX + rC * Math.cos(a)) * diskR;
      const ny = cy + (cY + rC * Math.sin(a)) * diskR;
      line(prevX, prevY, nx, ny);
      prevX = nx;
      prevY = ny;
    }
  }

  // Recursive tree generation
  function branchTree(d, u0, v0, angle, span) {
    if (d > maxDepth) return;
    const stepDist = 0.28 * Math.pow(0.72, d) * (1.0 + 0.1 * Math.sin(t * 1.5 + d));
    const rCur = Math.sqrt(u0 * u0 + v0 * v0);
    const targetR = Math.min(0.92, rCur + stepDist);

    const childCount = (d === 0) ? branches : branches - 1;
    for (let b = 0; b < childCount; b++) {
      const offset = (childCount === 1) ? 0 : ((b / (childCount - 1)) - 0.5) * span;
      const childAngle = angle + offset;
      const u1 = targetR * Math.cos(childAngle);
      const v1 = targetR * Math.sin(childAngle);

      const f = d / maxDepth;
      const branchCol = hsl((hue + f * 0.45) % 1.0, 0.85, 0.5 + 0.3 * (1.0 - f), 0.85);
      drawGeodesic(u0, v0, u1, v1, branchCol, Math.max(1.0, 3.2 - d * 0.6));

      // Node point
      noStroke();
      if (d === maxDepth) {
        fill(1.0, 0.42, 0.21, 0.9); // ember leaf nodes
        circle(cx + u1 * diskR, cy + v1 * diskR, 3.5 + 1.5 * Math.sin(t * 3.0 + childAngle));
      } else {
        fill(0.95, 0.96, 1.0, 0.85);
        circle(cx + u1 * diskR, cy + v1 * diskR, 2.8);
      }

      branchTree(d + 1, u1, v1, childAngle, span * spread);
    }
  }

  const baseRot = t * spin;
  for (let root = 0; root < branches; root++) {
    const rootAngle = baseRot + (root * 2.0 * Math.PI) / branches;
    const rRoot = 0.28;
    const u1 = rRoot * Math.cos(rootAngle);
    const v1 = rRoot * Math.sin(rootAngle);
    drawGeodesic(0, 0, u1, v1, hsl(hue, 0.9, 0.65), 3.5);
    branchTree(1, u1, v1, rootAngle, 1.4);
  }

  // Central hub
  fill(0.655, 0.694, 1.0, 1.0);
  circle(cx, cy, 6.0);
}
