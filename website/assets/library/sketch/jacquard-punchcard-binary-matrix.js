// Jacquard Punchcard Binary Matrix - Infinite Library (Sketch 2D)
param("cards", 7, 4, 12);
param("columns", 24, 14, 36);
param("feed_speed", 0.35, 0.0, 1.5);
param("hole_scale", 0.36, 0.2, 0.45);

function draw(t) {
  // Cast-iron loom slate ground (#262B32)
  background(0.149, 0.169, 0.196);
  const w = width;
  const h = height;
  const marginX = w * 0.11;
  const cardW = w - marginX * 2.0;
  const nCards = floor(cards);
  const nCols = floor(columns);
  const cardH = (h * 0.82) / nCards;
  const startY = h * 0.09;
  const scroll = t * feed_speed;

  // Vertical cast-iron guide rails
  noStroke();
  fill(0.10, 0.12, 0.14, 0.95);
  rect(marginX * 0.45, 0, marginX * 0.32, h);
  rect(w - marginX * 0.77, 0, marginX * 0.32, h);

  for (let k = 0; k < nCards; k++) {
    const cy = startY + k * cardH;
    const gap = 4.0;
    const ch = cardH - gap;

    // Kraft manila pasteboard card (#B49468) with subtle alternating fold shade
    const shade = (k % 2 === 0) ? 1.0 : 0.91;
    fill(0.706 * shade, 0.580 * shade, 0.408 * shade, 0.98);
    rect(marginX, cy, cardW, ch);

    // Top & bottom card fold shadow
    fill(0.12, 0.10, 0.08, 0.35);
    rect(marginX, cy + ch - 3.0, cardW, 3.0);

    // Registration peg holes at left and right edges
    fill(0.94, 0.93, 0.90, 0.96);
    circle(marginX + 18, cy + ch * 0.5, 11.0);
    circle(marginX + cardW - 18, cy + ch * 0.5, 11.0);

    // Lacing cord stitches between cards
    stroke(0.92, 0.88, 0.80, 0.75);
    strokeWeight(2.0);
    line(marginX + 42, cy - 3, marginX + 42, cy + 5);
    line(marginX + cardW - 42, cy - 3, marginX + cardW - 42, cy + 5);
    noStroke();

    // 4 sub-rows of binary perforations per punchcard
    const innerX = marginX + 60;
    const innerW = cardW - 120;
    const cellW = innerW / nCols;
    const subRows = 4;
    const cellH = (ch - 14) / subRows;
    const rad = min(cellW, cellH) * hole_scale;

    for (let r = 0; r < subRows; r++) {
      const rowIdx = k * subRows + r + floor(scroll * 2.0);
      for (let c = 0; c < nCols; c++) {
        // Twill + diamond jacquard weave bit pattern
        const d1 = abs(c - nCols * 0.5);
        const bit = ((c * 3 + rowIdx * 5) % 7 < 3) || ((floor(d1 + rowIdx) % 6) === 0);
        const hx = innerX + (c + 0.5) * cellW;
        const hy = cy + 7 + (r + 0.5) * cellH;
        if (bit) {
          // Daylight through punched hole
          fill(0.953, 0.945, 0.918, 0.98);
          circle(hx, hy, rad * 2.0);
          // Subtle inner rim shadow
          noFill();
          stroke(0.28, 0.22, 0.15, 0.45);
          strokeWeight(1.0);
          circle(hx, hy, rad * 2.0);
          noStroke();
        } else {
          // Unpunched dimple mark
          fill(0.56 * shade, 0.45 * shade, 0.31 * shade, 0.55);
          circle(hx, hy, rad * 0.55);
        }
      }
    }
  }
}