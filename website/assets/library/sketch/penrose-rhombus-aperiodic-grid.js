// Penrose Rhombus Aperiodic Grid - Infinite Library (Sketch 2D)
param("generations", 4, 2, 5);
param("gold_ratio", 0.38, 0.0, 1.0);
param("drift", 0.14, 0.0, 0.5);
param("subgrid", 0.55, 0.0, 1.0);

function draw(t) {
  // Deep charcoal architectural plate (#1A1D20)
  background(0.102, 0.114, 0.125);
  const w = width;
  const h = height;
  const s = min(w, h);
  const phi = 1.61803398875;

  // Initial decagonal wheel of 10 acute Robinson triangles (type 0: thin half-rhombus)
  let tris = [];
  const rad = s * 0.44;
  const rot0 = t * drift * 0.18;
  for (let i = 0; i < 10; i++) {
    const a1 = rot0 + (i * TAU) / 10.0;
    const a2 = rot0 + ((i + 1) * TAU) / 10.0;
    const b = [cos(a1) * rad, sin(a1) * rad];
    const c = [cos(a2) * rad, sin(a2) * rad];
    if (i % 2 === 0) {
      tris.push([0, [0, 0], b, c]);
    } else {
      tris.push([0, [0, 0], c, b]);
    }
  }

  // Robinson triangle deflation for Penrose P3 rhombi
  const gens = floor(generations);
  for (let g = 0; g < gens; g++) {
    const next = [];
    for (let k = 0; k < tris.length; k++) {
      const tri = tris[k];
      const kind = tri[0];
      const A = tri[1], B = tri[2], C = tri[3];
      if (kind === 0) {
        const P = [A[0] + (B[0] - A[0]) / phi, A[1] + (B[1] - A[1]) / phi];
        next.push([0, C, P, B]);
        next.push([1, P, C, A]);
      } else {
        const Q = [B[0] + (A[0] - B[0]) / phi, B[1] + (A[1] - B[1]) / phi];
        const R = [B[0] + (C[0] - B[0]) / phi, B[1] + (C[1] - B[1]) / phi];
        next.push([1, R, C, A]);
        next.push([1, Q, R, B]);
        next.push([0, R, Q, A]);
      }
    }
    tris = next;
  }

  translate(w * 0.5, h * 0.5);

  // Subtle architectural backing disc
  noStroke();
  fill(0.145, 0.161, 0.176);
  circle(0, 0, rad * 2.04);

  // Draw Penrose P3 rhombi (each Robinson triangle is half a rhombus; B-C is the shared diagonal)
  for (let k = 0; k < tris.length; k++) {
    const tri = tris[k];
    const kind = tri[0];
    const A = tri[1], B = tri[2], C = tri[3];
    const cx = (A[0] + B[0] + C[0]) / 3.0;
    const cy = (A[1] + B[1] + C[1]) / 3.0;
    const distR = sqrt(cx * cx + cy * cy) / rad;
    const wave = 0.5 + 0.5 * sin(distR * 8.0 - t * 0.8 + atan2(cy, cx) * 5.0);

    if (kind === 0) {
      // Acute / thick rhombus half: alternate between pale limestone (#E2E4E1) and olive-gold (#B5B93A)
      if (wave > 1.0 - gold_ratio) {
        fill(0.710, 0.725, 0.227, 0.96);
      } else {
        fill(0.886, 0.894, 0.882, 0.94);
      }
    } else {
      // Obtuse / thin rhombus half: architectural slate grey (#6E7378)
      fill(0.431, 0.451, 0.471, 0.94);
    }

    noStroke();
    triangle(A[0], A[1], B[0], B[1], C[0], C[1]);

    // Crisp rhombus outer edges (A-B and A-C; omit B-C internal diagonal)
    stroke(0.102, 0.114, 0.125, 0.92);
    strokeWeight(1.5);
    line(A[0], A[1], B[0], B[1]);
    line(A[0], A[1], C[0], C[1]);

    // Secondary inscribed golden-ratio construction lines (Queen Mary facade detail)
    if (subgrid > 0.05) {
      const mAB = [A[0] + (B[0] - A[0]) / phi, A[1] + (B[1] - A[1]) / phi];
      const mAC = [A[0] + (C[0] - A[0]) / phi, A[1] + (C[1] - A[1]) / phi];
      stroke(0.102, 0.114, 0.125, 0.28 * subgrid);
      strokeWeight(0.8);
      line(mAB[0], mAB[1], mAC[0], mAC[1]);
    }
  }

  // Outer architectural frame ring & calibration ticks
  noFill();
  stroke(0.886, 0.894, 0.882, 0.35);
  strokeWeight(1.4);
  circle(0, 0, rad * 2.08);
  for (let i = 0; i < 20; i++) {
    const a = rot0 + (i * TAU) / 20.0;
    const r0 = rad * 1.04;
    const r1 = rad * (i % 2 === 0 ? 1.09 : 1.065);
    stroke(0.710, 0.725, 0.227, i % 2 === 0 ? 0.85 : 0.45);
    strokeWeight(i % 2 === 0 ? 1.8 : 1.0);
    line(cos(a) * r0, sin(a) * r0, cos(a) * r1, sin(a) * r1);
  }
}
