// Cosmatesque Quincunx Pavement - Infinite Library (Sketch 2D)
param("tess_density", 14, 8, 22);
param("ring_braids", 24, 12, 40);
param("rot_speed", 0.08, 0.0, 0.4);
param("inlay_scale", 0.84, 0.65, 0.95);

function draw(t) {
  // Slate marble matrix (#2E2D2B)
  background(0.180, 0.176, 0.169);
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const S = min(w, h) * inlay_scale;

  // Outer travertine frame (#DCD2BC)
  noFill();
  stroke(0.863, 0.824, 0.737, 0.92);
  strokeWeight(4.0);
  rect(cx - S * 0.5, cy - S * 0.5, S, S);
  stroke(0.722, 0.573, 0.290, 0.75);
  strokeWeight(1.8);
  rect(cx - S * 0.46, cy - S * 0.46, S * 0.92, S * 0.92);

  // Opus sectile triangular spandrel tessellation
  const grid = floor(tess_density);
  const cell = (S * 0.88) / grid;
  const x0 = cx - S * 0.44;
  const y0 = cy - S * 0.44;
  for (let iy = 0; iy < grid; iy++) {
    for (let ix = 0; ix < grid; ix++) {
      const px = x0 + ix * cell;
      const py = y0 + iy * cell;
      const dx = (px + cell * 0.5 - cx) / (S * 0.5);
      const dy = (py + cell * 0.5 - cy) / (S * 0.5);
      if (sqrt(dx * dx + dy * dy) < 0.36) continue;

      const parity = (ix + iy) % 3;
      if (parity === 0) fill(0.431, 0.141, 0.161, 0.88);
      else if (parity === 1) fill(0.141, 0.259, 0.208, 0.88);
      else fill(0.863, 0.824, 0.737, 0.82);

      stroke(0.12, 0.11, 0.10, 0.85);
      strokeWeight(1.2);
      beginShape();
      vertex(px, py);
      vertex(px + cell, py);
      vertex(px + cell * 0.5, py + cell * 0.5);
      endShape();

      fill(0.722, 0.573, 0.290, 0.65);
      beginShape();
      vertex(px, py + cell);
      vertex(px + cell, py + cell);
      vertex(px + cell * 0.5, py + cell * 0.5);
      endShape();
    }
  }

  // Diagonal porphyry lozenge frame
  push();
  translate(cx, cy);
  rotate(PI * 0.25);
  fill(0.180, 0.176, 0.169, 0.94);
  stroke(0.863, 0.824, 0.737, 0.95);
  strokeWeight(5.0);
  const dSize = S * 0.54;
  rect(-dSize * 0.5, -dSize * 0.5, dSize, dSize);
  pop();

  // Five-roundel quincunx (4 satellites + 1 central porphyry disc)
  const satOff = S * 0.235;
  const satRad = S * 0.115;
  const braids = floor(ring_braids);
  for (let q = 0; q < 4; q++) {
    const a = (q / 4.0) * TAU + PI * 0.25 + t * rot_speed * 0.25;
    const rx = cx + cos(a) * satOff;
    const ry = cy + sin(a) * satOff;

    fill(0.180, 0.176, 0.169, 0.98);
    stroke(0.863, 0.824, 0.737, 0.92);
    strokeWeight(3.2);
    circle(rx, ry, satRad * 2.15);

    // Serpentine/porphyry alternating inner disc
    fill(q % 2 === 0 ? 0.141 : 0.431, q % 2 === 0 ? 0.259 : 0.141, q % 2 === 0 ? 0.208 : 0.161, 0.96);
    stroke(0.722, 0.573, 0.290, 0.90);
    strokeWeight(2.0);
    circle(rx, ry, satRad * 1.32);

    // 6-point stone star inlay
    fill(0.863, 0.824, 0.737, 0.85);
    noStroke();
    beginShape();
    for (let k = 0; k <= 12; k++) {
      const sa = (k / 12.0) * TAU + a;
      const sr = k % 2 === 0 ? satRad * 0.56 : satRad * 0.22;
      vertex(rx + cos(sa) * sr, ry + sin(sa) * sr);
    }
    endShape();
  }

  // Central imperial porphyry roundel with guilloche braid ring
  const cRad = S * 0.165;
  fill(0.180, 0.176, 0.169, 0.98);
  stroke(0.863, 0.824, 0.737, 0.96);
  strokeWeight(4.0);
  circle(cx, cy, cRad * 2.24);

  for (let i = 0; i < braids; i++) {
    const a = (i / braids) * TAU - t * rot_speed;
    const bx = cx + cos(a) * cRad * 0.95;
    const by = cy + sin(a) * cRad * 0.95;
    fill(i % 2 === 0 ? 0.722 : 0.863, i % 2 === 0 ? 0.573 : 0.824, i % 2 === 0 ? 0.290 : 0.737, 0.90);
    noStroke();
    circle(bx, by, cRad * 0.16);
  }

  fill(0.431, 0.141, 0.161, 0.98);
  stroke(0.722, 0.573, 0.290, 0.95);
  strokeWeight(2.5);
  circle(cx, cy, cRad * 1.48);

  // Central travertine rosette
  fill(0.863, 0.824, 0.737, 0.92);
  noStroke();
  beginShape();
  for (let k = 0; k <= 16; k++) {
    const ra = (k / 16.0) * TAU + t * rot_speed * 0.5;
    const rr = k % 2 === 0 ? cRad * 0.56 : cRad * 0.24;
    vertex(cx + cos(ra) * rr, cy + sin(ra) * rr);
  }
  endShape();
}