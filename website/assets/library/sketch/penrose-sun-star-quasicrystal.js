// Penrose Sun-Star Quasicrystal - Infinite Library (Sketch 2D)
param("generations", 4, 2, 5);
param("arc_weight", 2.2, 0.5, 4.5);
param("spin", 0.12, 0.0, 0.5);
param("pulse", 0.35, 0.0, 1.0);

function draw(t) {
  // Warm cream exhibition paper (#EFE6D5)
  background(0.937, 0.902, 0.835);
  const w = width;
  const h = height;
  const s = min(w, h);
  const phi = 1.61803398875;

  // P2 Kite & Dart wheel (10 half-kites around origin -> Sun vertex)
  // kind 0 = half-kite, kind 1 = half-dart
  let tris = [];
  const rad = s * 0.41;
  const rot0 = t * spin * 0.25;
  for (let i = 0; i < 10; i++) {
    const a1 = rot0 + (i * TAU) / 10.0;
    const a2 = rot0 + ((i + 1) * TAU) / 10.0;
    const b = [cos(a1) * rad, sin(a1) * rad];
    const c = [cos(a2) * rad, sin(a2) * rad];
    if (i % 2 === 0) {
      tris.push([0, [0, 0], c, b]);
    } else {
      tris.push([0, [0, 0], b, c]);
    }
  }

  const gens = floor(generations);
  for (let g = 0; g < gens; g++) {
    const next = [];
    for (let k = 0; k < tris.length; k++) {
      const tri = tris[k];
      const kind = tri[0];
      const A = tri[1], B = tri[2], C = tri[3];
      if (kind === 0) {
        // Half-kite -> 2 half-kites + 1 half-dart
        const Q = [A[0] + (B[0] - A[0]) / phi, A[1] + (B[1] - A[1]) / phi];
        const R = [B[0] + (C[0] - B[0]) / phi, B[1] + (C[1] - B[1]) / phi];
        next.push([0, R, Q, B]);
        next.push([0, R, A, Q]);
        next.push([1, C, A, R]);
      } else {
        // Half-dart -> 1 half-kite + 1 half-dart
        const P = [B[0] + (A[0] - B[0]) / phi, B[1] + (A[1] - B[1]) / phi];
        next.push([1, P, C, A]);
        next.push([0, C, P, B]);
      }
    }
    tris = next;
  }

  translate(w * 0.5, h * 0.5);

  // Subtle geometric border plate
  noFill();
  stroke(0.118, 0.114, 0.106, 0.22);
  strokeWeight(1.2);
  rect(-s * 0.45, -s * 0.45, s * 0.90, s * 0.90);

  for (let k = 0; k < tris.length; k++) {
    const tri = tris[k];
    const kind = tri[0];
    const A = tri[1], B = tri[2], C = tri[3];
    const cx = (A[0] + B[0] + C[0]) / 3.0;
    const cy = (A[1] + B[1] + C[1]) / 3.0;
    const distR = sqrt(cx * cx + cy * cy) / rad;
    const breath = 0.5 + 0.5 * sin(t * 1.4 - distR * 5.0);

    if (kind === 0) {
      // Kites: DASA crimson (#D6334C) modulated slightly by radial pulse
      fill(0.839 - breath * 0.08 * pulse, 0.200 + breath * 0.05 * pulse, 0.298, 0.95);
    } else {
      // Darts: Deep forest teal (#1B5E52)
      fill(0.106, 0.369 + breath * 0.06 * pulse, 0.322, 0.95);
    }

    noStroke();
    triangle(A[0], A[1], B[0], B[1], C[0], C[1]);

    // Outer tile edges (A-B and B-C; omit A-C symmetry seam)
    stroke(0.937, 0.902, 0.835, 0.85);
    strokeWeight(1.3);
    line(A[0], A[1], B[0], B[1]);
    line(B[0], B[1], C[0], C[1]);

    // Conway golden matching-rule rib lines across each tile
    const p1 = [A[0] + (B[0] - A[0]) * 0.382, A[1] + (B[1] - A[1]) * 0.382];
    const p2 = [B[0] + (C[0] - B[0]) * 0.618, B[1] + (C[1] - B[1]) * 0.618];
    if (kind === 0) {
      stroke(0.937, 0.902, 0.835, 0.55);
    } else {
      stroke(0.910, 0.745, 0.340, 0.72);
    }
    strokeWeight(arc_weight * 0.55);
    line(p1[0], p1[1], p2[0], p2[1]);
  }

  // Central 10-fold decagonal brass pin
  noStroke();
  fill(0.937, 0.902, 0.835);
  circle(0, 0, s * 0.022);
  fill(0.118, 0.114, 0.106);
  circle(0, 0, s * 0.010);
}
