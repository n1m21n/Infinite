// Riso Manga Panel Grid - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Risograph manga layout with offset halftone dots, dynamic screentone hatchings, and fluorescent misregistered panel borders.
param("zoom", 1.0, 0.5, 2.0);
param("halftone_density", 24.0, 8.0, 48.0);
param("misregister", 3.5, 0.0, 10.0);
param("line_weight", 3.0, 1.0, 8.0);

function draw(t) {
  // Warm riso newsprint background
  background(0.88, 0.86, 0.80);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.86 * zoom;
  const half = span * 0.5;
  const off = misregister;

  push();
  translate(cx - half, cy - half);

  // Outer border with cyan & pink misregistration shadow
  noFill();
  stroke(0.0, 0.941, 1.0, 0.7); // Process cyan plate
  strokeWeight(line_weight);
  rect(-off, off * 0.5, span, span);

  stroke(1.0, 0.0, 0.467, 0.7); // Fluorescent pink plate
  strokeWeight(line_weight);
  rect(off * 0.5, -off, span, span);

  stroke(0.067, 0.067, 0.067, 1.0); // Carbon black keyline
  strokeWeight(line_weight + 1.0);
  rect(0, 0, span, span);

  // Panel division: Top large panel, bottom two split panels
  const splitY = span * 0.55;
  const splitX = span * 0.5;

  // Horizontal divider
  stroke(0.067, 0.067, 0.067, 1.0);
  strokeWeight(line_weight);
  line(0, splitY, span, splitY);

  // Vertical divider in bottom half
  line(splitX, splitY, splitX, span);

  // Top Panel: Halftone dot screen matrix with dynamic radii
  const cols = floor(halftone_density);
  const rows = floor(cols * 0.5);
  const cellW = span / cols;
  const cellH = splitY / rows;

  noStroke();
  for (let j = 1; j < rows; j++) {
    for (let i = 1; i < cols; i++) {
      const px = i * cellW;
      const py = j * cellH;
      const d = dist(px, py, span * 0.5, splitY * 0.5);
      const r = (sin(d * 0.015 - t * 0.8) * 0.5 + 0.5) * (cellW * 0.42);

      // Pink dot plate with offset
      fill(1.0, 0.0, 0.467, 0.65);
      circle(px + off * 0.4, py - off * 0.4, r);

      // Black core dot
      fill(0.067, 0.067, 0.067, 0.9);
      circle(px, py, r * 0.75);
    }
  }

  // Bottom-Left Panel: Diagonal screentone speedlines
  push();
  stroke(0.0, 0.941, 1.0, 0.85); // Cyan screentone lines
  strokeWeight(line_weight * 0.6);
  const step = 14.0;
  for (let k = -span; k < span; k += step) {
    const x1 = max(0, k);
    const y1 = splitY + max(0, -k * 0.5);
    const x2 = min(splitX, k + splitY);
    const y2 = min(span, splitY + (splitX - k));
    if (x1 < splitX && y1 < span) {
      line(x1, y1, x2, y2);
    }
  }
  // Misregistered pink cross-hatch
  stroke(1.0, 0.0, 0.467, 0.5);
  for (let k = 0; k < splitX + (span - splitY); k += step * 1.5) {
    line(k + off, splitY, k - (span - splitY) + off, span);
  }
  pop();

  // Bottom-Right Panel: Bold geometric manga action starburst
  push();
  translate(splitX + (span - splitX) * 0.5, splitY + (span - splitY) * 0.5);
  noStroke();
  fill(1.0, 0.933, 0.0, 0.95); // Bright yellow impact burst
  const rays = 16;
  const innerR = span * 0.06;
  const outerR = span * 0.18;
  beginShape();
  for (let a = 0; a < rays; a++) {
    const ang1 = (a / rays) * TAU;
    const ang2 = ((a + 0.5) / rays) * TAU;
    const r1 = outerR * (0.8 + 0.4 * sin(a * 2.3 + t));
    const r2 = innerR;
    vertex(cos(ang1) * r1, sin(ang1) * r1);
    vertex(cos(ang2) * r2, sin(ang2) * r2);
  }
  endShape();

  // Black impact core
  fill(0.067, 0.067, 0.067);
  circle(0, 0, innerR * 0.8);
  fill(1.0, 0.0, 0.467);
  circle(off, -off, innerR * 0.4);
  pop();

  pop();
}
