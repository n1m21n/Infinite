// Bill Mobius Ribbon Projection - Infinite Library (Sketch 2D)
param("u_steps", 96, 48, 160);
param("v_ribs", 14, 6, 24);
param("ribbon_w", 0.34, 0.15, 0.55);
param("spin", 0.22, 0.0, 0.6);

function draw(t) {
  // Warm limestone paper (#DFDAD0) for balanced luminance
  background(0.875, 0.855, 0.816);
  const w = width;
  const h = height;
  const side = min(w, h);
  const cx = w * 0.5;
  const cy = h * 0.47;
  const R = side * 0.27;

  // Architectural concrete plinth horizon & shadow band
  noStroke();
  fill(0.74, 0.72, 0.68, 0.85);
  rect(w * 0.12, cy + R * 0.92, w * 0.76, side * 0.14);
  stroke(0.094, 0.102, 0.125, 0.45);
  strokeWeight(1.5);
  line(w * 0.12, cy + R * 0.92, w * 0.88, cy + R * 0.92);

  // Fine Swiss elevation grid ticks
  stroke(0.094, 0.102, 0.125, 0.18);
  strokeWeight(1.0);
  for (let g = -4; g <= 4; g++) {
    const gx = cx + g * (side * 0.09);
    line(gx, h * 0.10, gx, h * 0.90);
  }
  for (let g = -3; g <= 3; g++) {
    const gy = cy + g * (side * 0.09);
    line(w * 0.10, gy, w * 0.90, gy);
  }

  const nu = floor(u_steps);
  const nv = floor(v_ribs);
  const rotY = t * spin + 0.55;
  const rotX = 0.42;
  const cyR = cos(rotY);
  const syR = sin(rotY);
  const cxR = cos(rotX);
  const sxR = sin(rotX);

  // Project Mobius strip ruled lines
  for (let i = 0; i < nu; i++) {
    const u = (i / nu) * TAU;
    const halfU = u * 0.5;
    const cu = cos(u);
    const su = sin(u);
    const ch = cos(halfU);
    const sh = sin(halfU);

    // Endpoints across ribbon width [-1, 1]
    const wHalf = R * ribbon_w;
    const rA = R + (-wHalf) * ch;
    const x3A = rA * cu;
    const y3A = (-wHalf) * sh;
    const z3A = rA * su;

    const rB = R + wHalf * ch;
    const x3B = rB * cu;
    const y3B = wHalf * sh;
    const z3B = rB * su;

    // 3D rotation
    const rxA = x3A * cyR - z3A * syR;
    const rzA = x3A * syR + z3A * cyR;
    const ryA = y3A * cxR - rzA * sxR;
    const depthA = y3A * sxR + rzA * cxR;

    const rxB = x3B * cyR - z3B * syR;
    const rzB = x3B * syR + z3B * cyR;
    const ryB = y3B * cxR - rzB * sxR;

    const shade = 0.5 + 0.5 * (depthA / (R * 1.4));
    // Red granite (#8C3B2B) to deep ink (#181A20)
    const rr = 0.549 * shade + 0.094 * (1.0 - shade);
    const gg = 0.231 * shade + 0.102 * (1.0 - shade);
    const bb = 0.169 * shade + 0.125 * (1.0 - shade);

    stroke(rr, gg, bb, 0.85);
    strokeWeight(2.2);
    line(cx + rxA, cy + ryA, cx + rxB, cy + ryB);
  }

  // Longitudinal isoparm curves
  noFill();
  for (let j = 0; j < nv; j++) {
    const vNorm = (j / (nv - 1)) * 2.0 - 1.0;
    const wOff = R * ribbon_w * vNorm;
    const isEdge = (j === 0 || j === nv - 1);
    stroke(isEdge ? 0.094 : 0.106, isEdge ? 0.102 : 0.286, isEdge ? 0.125 : 0.541, isEdge ? 0.92 : 0.42);
    strokeWeight(isEdge ? 2.4 : 1.1);
    beginShape();
    for (let i = 0; i <= nu; i++) {
      const u = (i / nu) * TAU;
      const r = R + wOff * cos(u * 0.5);
      const x3 = r * cos(u);
      const y3 = wOff * sin(u * 0.5);
      const z3 = r * sin(u);
      const rx = x3 * cyR - z3 * syR;
      const rz = x3 * syR + z3 * cyR;
      const ry = y3 * cxR - rz * sxR;
      vertex(cx + rx, cy + ry);
    }
    endShape();
  }
}
