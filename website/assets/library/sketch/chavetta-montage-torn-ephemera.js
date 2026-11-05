// Chavetta Montage Torn Ephemera - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Mixed-media collage on aged kraft paper with torn paper horizontal stratum bands, distressed terracotta color blocks, slate blue overlays, and botanical specimen silhouette contours.
param("zoom", 1.0, 0.5, 2.0);
param("torn_roughness", 1.2, 0.4, 2.5);
param("stratum_count", 7.0, 4.0, 12.0);
param("slate_overlay", 0.65, 0.2, 1.0);

function draw(t) {
  // Aged kraft paper base ground
  background(0.85, 0.77, 0.71);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88 * zoom;

  push();
  translate(cx, cy);

  // Outer charcoal framing boundary
  noFill();
  stroke(0.17, 0.17, 0.17);
  strokeWeight(2.5);
  rect(-span * 0.5, -span * 0.5, span, span);

  // Fine inner margin
  stroke(0.31, 0.36, 0.46, 0.5);
  strokeWeight(1.0);
  rect(-span * 0.47, -span * 0.47, span * 0.94, span * 0.94);

  // Layer 1: Charcoal foundation block at top-left
  fill(0.17, 0.17, 0.17);
  noStroke();
  rect(-span * 0.45, -span * 0.45, span * 0.42, span * 0.28);

  // Stamped archival label box on top-left block
  fill(0.85, 0.77, 0.71);
  rect(-span * 0.42, -span * 0.42, span * 0.26, span * 0.05);
  fill(0.64, 0.28, 0.22);
  rect(-span * 0.42, -span * 0.35, span * 0.36, span * 0.035);

  // Layer 2: Horizontal torn paper color bands
  const count = floor(stratum_count);
  const bandH = (span * 0.55) / count;
  for (let i = 0; i < count; i++) {
    const yTop = -span * 0.12 + i * bandH;
    const isTerra = (i % 2 === 0);
    if (isTerra) {
      fill(0.64, 0.28, 0.22, 0.92); // Terracotta rust
    } else {
      fill(0.31, 0.36, 0.46, slate_overlay); // Slate blue
    }
    noStroke();

    // Procedural torn jagged top edge
    beginShape();
    vertex(-span * 0.44, yTop + bandH * 0.9);
    for (let x = -span * 0.44; x <= span * 0.44; x += 18) {
      const n = sin(x * 0.035 * torn_roughness + i * 2.8) * 6.5 + cos(x * 0.08) * 3.0;
      vertex(x, yTop + n);
    }
    vertex(span * 0.44, yTop + bandH * 0.9);
    endShape();

    // Exposed paper edge highlight in kraft white
    stroke(0.92, 0.88, 0.82, 0.75);
    strokeWeight(1.2);
    noFill();
    beginShape();
    for (let x = -span * 0.44; x <= span * 0.44; x += 18) {
      const n = sin(x * 0.035 * torn_roughness + i * 2.8) * 6.5 + cos(x * 0.08) * 3.0;
      vertex(x, yTop + n);
    }
    endShape();
  }

  // Layer 3: Vertical collage strip on right side
  fill(0.17, 0.17, 0.17, 0.88);
  noStroke();
  rect(span * 0.14, -span * 0.45, span * 0.31, span * 0.88);

  // Terracotta and Kraft geometric inserts in right strip
  fill(0.64, 0.28, 0.22);
  rect(span * 0.18, -span * 0.38, span * 0.23, span * 0.12);
  fill(0.85, 0.77, 0.71);
  rect(span * 0.18, -span * 0.22, span * 0.16, span * 0.04);
  rect(span * 0.18, -span * 0.15, span * 0.23, span * 0.03);

  // Slate blue square block
  fill(0.31, 0.36, 0.46);
  rect(span * 0.18, span * 0.08, span * 0.23, span * 0.22);

  // Botanical specimen silhouette drawing
  stroke(0.85, 0.77, 0.71, 0.95);
  strokeWeight(2.0);
  noFill();
  push();
  translate(span * 0.295, span * 0.19);
  line(0, span * 0.08, 0, -span * 0.08);
  for (let b = -4; b <= 4; b++) {
    const by = b * (span * 0.018);
    const bl = span * 0.04 * (1.0 - abs(b) * 0.18);
    line(0, by, -bl, by - 6);
    line(0, by, bl, by - 6);
  }
  pop();

  // Classical circular medallion sketch on lower left
  push();
  translate(-span * 0.22, span * 0.22);
  stroke(0.17, 0.17, 0.17);
  strokeWeight(2.4);
  noFill();
  circle(0, 0, span * 0.34);
  circle(0, 0, span * 0.28);
  stroke(0.64, 0.28, 0.22);
  strokeWeight(1.4);
  circle(0, 0, span * 0.18);

  fill(0.64, 0.28, 0.22);
  noStroke();
  circle(0, 0, span * 0.06);

  // Radial compass ticks
  stroke(0.17, 0.17, 0.17);
  strokeWeight(1.2);
  for (let a = 0; a < 16; a++) {
    const ang = (a * 6.2831853) / 16;
    const r1 = span * 0.14;
    const r2 = span * 0.17;
    line(cos(ang) * r1, sin(ang) * r1, cos(ang) * r2, sin(ang) * r2);
  }
  pop();

  // Registration ticks and technical boundary stamps
  stroke(0.17, 0.17, 0.17);
  strokeWeight(1.5);
  const markLen = 14;
  line(-span * 0.5, 0, -span * 0.5 + markLen, 0);
  line(span * 0.5, 0, span * 0.5 - markLen, 0);
  line(0, -span * 0.5, 0, -span * 0.5 + markLen);
  line(0, span * 0.5, 0, span * 0.5 - markLen);

  pop();
}
