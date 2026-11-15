// Jacquard Loom Perforated Tabulature - Infinite Library (Sketch 2D)
param("card_count", 5, 3, 7);
param("hole_rows", 14, 8, 20);
param("card_phase", 0.45, 0.0, 2.0);
param("lace_tension", 0.35, 0.1, 0.8);

function draw(t) {
  // Unbleached linen paper ground
  background(0.895, 0.875, 0.832);

  const w = width;
  const h = height;
  const nc = floor(card_count);
  const nr = floor(hole_rows);

  // Subtle horizontal textile weave backing lines
  strokeWeight(1.0);
  for (let i = 0; i < 48; i++) {
    const y = h * (0.04 + 0.92 * (i / 48));
    stroke(0.106, 0.125, 0.157, 0.04);
    line(w * 0.04, y, w * 0.96, y);
  }

  // Lower herringbone preview ribbon
  const ribY = h * 0.79;
  const ribH = h * 0.13;
  const ribW = w * 0.84;
  const ribX = w * 0.08;
  noFill();
  stroke(0.106, 0.125, 0.157, 0.8);
  strokeWeight(2.0);
  rect(ribX, ribY, ribW, ribH);

  const cols = 42;
  const rows = 6;
  const cw = ribW / cols;
  const ch = ribH / rows;
  const scroll = floor(t * card_phase * 5.0);
  for (let r = 0; r < rows; r++) {
    for (let c = 0; c < cols; c++) {
      const idx = c + scroll;
      const zig = Math.abs((idx % 8) - 4);
      const on = ((zig + r) % 3) === 0;
      noStroke();
      if (on) {
        fill((c % 6 === 0) ? 0.722 : 0.180, (c % 6 === 0) ? 0.290 : 0.353, (c % 6 === 0) ? 0.196 : 0.408, 0.9);
      } else {
        fill(0.106, 0.125, 0.157, 0.12);
      }
      rect(ribX + c * cw + 1.5, ribY + r * ch + 1.5, cw - 3.0, ch - 3.0);
    }
  }

  // Linked vertical Jacquard punchcards
  const marginX = w * 0.08;
  const totalW = w * 0.84;
  const gap = w * 0.022;
  const cardW = (totalW - gap * (nc - 1)) / nc;
  const cardY = h * 0.08;
  const cardH = h * 0.66;
  const bitCols = 8;

  for (let k = 0; k < nc; k++) {
    const cx = marginX + k * (cardW + gap);

    // Card stock body with subtle shadow
    noStroke();
    fill(0.106, 0.125, 0.157, 0.08);
    rect(cx + 5, cardY + 6, cardW, cardH);

    fill(0.845, 0.815, 0.760, 0.98);
    stroke(0.106, 0.125, 0.157, 0.88);
    strokeWeight(2.0);
    rect(cx, cardY, cardW, cardH);

    // Inner punched matrix frame
    const padX = cardW * 0.16;
    const padY = cardH * 0.07;
    const matW = cardW - padX * 2.0;
    const matH = cardH - padY * 2.0;
    const dx = matW / bitCols;
    const dy = matH / nr;

    // Edge sprocket guide holes
    for (let r = 0; r < nr; r++) {
      const ry = cardY + padY + (r + 0.5) * dy;
      noStroke();
      fill(0.106, 0.125, 0.157, 0.78);
      circle(cx + padX * 0.45, ry, Math.min(dx, dy) * 0.34);
      circle(cx + cardW - padX * 0.45, ry, Math.min(dx, dy) * 0.34);
    }

    // Binary loom-lift punch holes
    for (let r = 0; r < nr; r++) {
      const rowCode = ((r + k * 3 + scroll) * 19 + k * 7) ^ (r * 5);
      for (let b = 0; b < bitCols; b++) {
        const bitOn = ((rowCode >> (b % 6)) & 1) === 1;
        const hx = cx + padX + (b + 0.5) * dx;
        const hy = cardY + padY + (r + 0.5) * dy;
        const rad = Math.min(dx, dy) * 0.36;
        if (bitOn) {
          noStroke();
          fill(0.106, 0.125, 0.157, 0.92);
          circle(hx, hy, rad * 1.8);
        } else {
          noFill();
          stroke(0.180, 0.353, 0.408, 0.35);
          strokeWeight(1.0);
          circle(hx, hy, rad * 1.4);
        }
      }
    }

    // Lacing cord connections to next card
    if (k < nc - 1) {
      const nx = cx + cardW;
      const span = gap;
      stroke(0.722, 0.290, 0.196, 0.92); // #B84A32 madder terracotta cord
      strokeWeight(2.4);
      for (let l = 0; l < 4; l++) {
        const ly = cardY + cardH * (0.14 + l * 0.24);
        const sag = span * lace_tension * 0.45;
        line(nx - padX * 0.3, ly - 6, nx + span * 0.5, ly + sag);
        line(nx + span * 0.5, ly + sag, nx + span + padX * 0.3, ly - 6);
        line(nx - padX * 0.3, ly + 6, nx + span + padX * 0.3, ly + 6);
      }
    }
  }
}
