// Jacquard Bauhaus Warp-Weft Grid - Infinite Library (Sketch 2D)
param("cols", 14, 8, 24);
param("rows", 28, 16, 48);
param("shift_speed", 0.25, 0.0, 1.2);
param("terracotta_mix", 0.35, 0.1, 0.7);

function draw(t) {
  // Warm unbleached linen parchment (#DFD3BA)
  background(0.875, 0.827, 0.729);
  const w = width;
  const h = height;
  const marginX = w * 0.16;
  const marginY = h * 0.12;
  const gw = w - marginX * 2.0;
  const gh = h - marginY * 2.0;

  const nC = floor(cols);
  const nR = floor(rows);
  const cw = gw / nC;
  const ch = gh / nR;

  // Fine graphite point-paper ruling lines
  stroke(0.58, 0.54, 0.46, 0.36);
  strokeWeight(1.0);
  for (let c = 0; c <= nC; c++) {
    const x = marginX + c * cw;
    line(x, marginY, x, marginY + gh);
  }
  for (let r = 0; r <= nR; r++) {
    const y = marginY + r * ch;
    line(marginX, y, marginX + gw, y);
  }

  // Stepped Bauhaus warp/weft interlacing blocks
  noStroke();
  const phase = floor(t * shift_speed * 2.0);
  for (let r = 0; r < nR; r++) {
    for (let c = 0; c < nC; c++) {
      const zone = ((c * 3 + r * 5) + phase) % 13;
      const bandH = sin(r * 0.42 + c * 0.15 + t * shift_speed * 0.6);
      const bandV = cos(c * 0.65 - r * 0.12);

      // Leave quiet parchment cells for negative space
      if (abs(bandH + bandV) < 0.32 && (c + r) % 3 !== 0) {
        continue;
      }

      const x = marginX + c * cw + 1.0;
      const y = marginY + r * ch + 1.0;
      const bw = cw - 2.0;
      const bh = ch - 2.0;

      if (zone < 3 || (c >= 4 && c <= 6 && r % 4 === 1)) {
        // Vermilion terracotta (#CE3B2A)
        if (((c + r) % 2 === 0) || bandH > terracotta_mix - 0.1) {
          fill(0.808, 0.231, 0.165, 0.94);
          rect(x, y, bw, bh);
        }
      } else if (bandH * bandV > 0.18 || (c % 5 === 0 && r % 2 === 0)) {
        // Charcoal ink (#1E1D1B)
        fill(0.118, 0.114, 0.106, 0.96);
        rect(x, y, bw, bh);
      } else if ((c + r * 2) % 5 === 0) {
        // Cool slate (#9EA6A8)
        fill(0.620, 0.651, 0.659, 0.88);
        rect(x, y, bw, bh);
      } else if ((r % 3 === 0 && c > 2) || bandV > 0.25) {
        // Raw ochre (#D7AE86)
        fill(0.843, 0.682, 0.525, 0.88);
        rect(x, y, bw, bh);
      }
    }
  }

  // Horizontal selvedge calibration bar at bottom margin
  const barY = marginY + gh + h * 0.035;
  const barH = h * 0.022;
  for (let c = 0; c < nC; c++) {
    const x = marginX + c * cw;
    if (c % 4 === 0) fill(0.118, 0.114, 0.106, 0.95);
    else if (c % 4 === 1) fill(0.808, 0.231, 0.165, 0.92);
    else if (c % 4 === 2) fill(0.843, 0.682, 0.525, 0.90);
    else fill(0.620, 0.651, 0.659, 0.88);
    rect(x + 1, barY, cw - 2, barH);
  }
  noFill();
  stroke(0.118, 0.114, 0.106, 0.55);
  strokeWeight(1.2);
  rect(marginX, marginY, gw, gh);
  rect(marginX, barY, gw, barH);
}