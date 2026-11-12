// Swiss Typographic Modular Grid Harmony - Infinite Library (Sketch 2D)
param("columns", 12.0, 6.0, 16.0);
param("line_weight", 1.5, 0.8, 3.0);
param("accent_scale", 1.0, 0.5, 2.0);
param("contrast", 1.15, 0.8, 1.8);

function draw(t) {
  // Warm cream archival paper ground (#F5F5F2)
  background(0.961, 0.961, 0.949);

  const w = width;
  const h = height;
  const margin = 72;
  const innerW = w - margin * 2;
  const innerH = h - margin * 2;

  // 1. Technical framing & crop marks
  stroke(0.067, 0.067, 0.067);
  strokeWeight(line_weight * 1.4);
  noFill();
  rect(margin, margin, innerW, innerH);

  // Exterior corner crop marks
  strokeWeight(1.0);
  line(margin - 24, margin, margin - 6, margin);
  line(margin, margin - 24, margin, margin - 6);
  line(w - margin + 6, margin, w - margin + 24, margin);
  line(w - margin, margin - 24, w - margin, margin - 6);
  line(margin - 24, h - margin, margin - 6, h - margin);
  line(margin, h - margin + 6, margin, h - margin + 24);
  line(w - margin + 6, h - margin, w - margin + 24, h - margin);
  line(w - margin, h - margin + 6, w - margin, h - margin + 24);

  // Precision graticule ticks
  for (let x = margin + 36; x < w - margin; x += 36) {
    line(x, margin, x, margin + 8);
    line(x, h - margin, x, h - margin - 8);
  }
  for (let y = margin + 36; y < h - margin; y += 36) {
    line(margin, y, margin + 8, y);
    line(w - margin, y, w - margin - 8, y);
  }

  // 2. 12-Column Typographic Modular Grid Lines
  const cols = floor(columns);
  const colWidth = innerW / cols;
  stroke(0.78, 0.78, 0.76, 0.65);
  strokeWeight(0.8);
  for (let c = 1; c < cols; c++) {
    const x = margin + c * colWidth;
    line(x, margin, x, h - margin);
  }

  // Horizontal baseline grid
  const rowHeight = innerH / 16.0;
  for (let r = 1; r < 16; r++) {
    const y = margin + r * rowHeight;
    line(margin, y, w - margin, y);
  }

  // 3. Asymmetric Constructivist Modules (Black & Vermilion #E4261A)
  noStroke();
  fill(0.894, 0.149, 0.102);
  const vX = margin + colWidth * 2;
  const vY = margin + rowHeight * 3;
  const vW = colWidth * 5 * accent_scale;
  const vH = rowHeight * 4;
  rect(vX, vY, vW, vH);

  fill(0.067, 0.067, 0.067);
  rect(margin + colWidth * 7, margin + rowHeight * 7, colWidth * 4, rowHeight * 6);

  fill(0.067, 0.067, 0.067);
  rect(margin + colWidth * 1, margin + rowHeight * 11, colWidth * 4, rowHeight * 1.5);
  rect(margin + colWidth * 8, margin + rowHeight * 2, colWidth * 3, rowHeight * 0.8);

  stroke(0.894, 0.149, 0.102);
  strokeWeight(line_weight * 2.2);
  line(margin + colWidth * 2, margin + rowHeight * 14, margin + colWidth * 10, margin + rowHeight * 6);

  fill(0.067, 0.067, 0.067);
  noStroke();
  const circleX = margin + colWidth * 4;
  const circleY = margin + rowHeight * 9;
  ellipse(circleX, circleY, colWidth * 2.8, colWidth * 2.8);

  fill(0.961, 0.961, 0.949);
  ellipse(circleX, circleY, colWidth * 1.4, colWidth * 1.4);

  fill(0.894, 0.149, 0.102);
  ellipse(circleX, circleY, colWidth * 0.4, colWidth * 0.4);

  stroke(0.067, 0.067, 0.067);
  strokeWeight(1.2);
  line(circleX - 32, circleY, circleX + 32, circleY);
  line(circleX, circleY - 32, circleX, circleY + 32);
}
