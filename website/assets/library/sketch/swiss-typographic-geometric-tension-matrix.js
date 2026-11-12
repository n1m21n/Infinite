// Swiss Typographic Geometric Tension Matrix - Infinite Library (Sketch 2D)
param("arcs", 6.0, 3.0, 10.0);
param("line_weight", 1.8, 0.8, 3.5);
param("accent_scale", 1.1, 0.5, 2.0);
param("contrast", 1.2, 0.8, 1.8);

function draw(t) {
  // Archival cream paper ground (#F5F5F2)
  background(0.961, 0.961, 0.949);

  const w = width;
  const h = height;
  const margin = 72;
  const innerW = w - margin * 2;
  const innerH = h - margin * 2;

  // 1. Framing graticule & registration marks
  stroke(0.067, 0.067, 0.067);
  strokeWeight(line_weight * 1.3);
  noFill();
  rect(margin, margin, innerW, innerH);

  strokeWeight(1.0);
  for (let x = margin + 36; x < w - margin; x += 36) {
    line(x, margin, x, margin + 7);
    line(x, h - margin, x, h - margin - 7);
  }
  for (let y = margin + 36; y < h - margin; y += 36) {
    line(margin, y, margin + 7, y);
    line(w - margin, y, w - margin - 7, y);
  }

  // 2. Muller-Brockmann concentric quadrant arcs radiating from lower-left focal datum
  const originX = margin + innerW * 0.25;
  const originY = h - margin - innerH * 0.25;
  const numArcs = floor(arcs);
  const maxR = innerW * 0.72;

  for (let i = 1; i <= numArcs; i++) {
    const frac = i / numArcs;
    const rad = frac * maxR;

    if (i % 2 === 0) {
      stroke(0.894, 0.149, 0.102);
      strokeWeight(line_weight * 2.4);
    } else {
      stroke(0.067, 0.067, 0.067);
      strokeWeight(line_weight * 1.2);
    }
    noFill();
    ellipse(originX, originY, rad * 2.0, rad * 2.0);
  }

  // 3. Orthogonal Structural Blocks & Stepped Rules
  noStroke();
  fill(0.894, 0.149, 0.102);
  rect(margin + innerW * 0.52, margin + innerH * 0.12, innerW * 0.38, innerH * 0.32);

  fill(0.067, 0.067, 0.067);
  rect(margin + innerW * 0.65, margin + innerH * 0.50, innerW * 0.25, innerH * 0.38);

  stroke(0.067, 0.067, 0.067);
  strokeWeight(line_weight * 1.5);
  const startY = margin + innerH * 0.18;
  for (let k = 0; k < 6; k++) {
    const yPos = startY + k * 28;
    line(margin + 40, yPos, margin + innerW * 0.44, yPos);
  }

  stroke(0.894, 0.149, 0.102);
  strokeWeight(line_weight * 2.0);
  line(originX, originY, originX + maxR * 0.75, originY - maxR * 0.75);

  fill(0.894, 0.149, 0.102);
  noStroke();
  ellipse(originX, originY, 18, 18);
  stroke(0.961, 0.961, 0.949);
  strokeWeight(2.0);
  line(originX - 14, originY, originX + 14, originY);
  line(originX, originY - 14, originX, originY + 14);
}
