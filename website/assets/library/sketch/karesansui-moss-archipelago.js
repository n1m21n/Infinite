// Karesansui Moss Archipelago - Infinite Library (Sketch 2D)
param("rings", 16, 8, 28);
param("wave_amp", 4.5, 0.0, 12.0);
param("tempo", 0.15, 0.0, 0.5);
param("margin_pct", 0.08, 0.04, 0.16);

function draw(t) {
  // Pale weathered sand (#DAD5C8)
  background(0.855, 0.835, 0.784);
  const w = width;
  const h = height;
  const side = min(w, h);
  const m = side * margin_pct;
  const span = side - m * 2.0;
  const x0 = (w - side) * 0.5 + m;
  const y0 = (h - side) * 0.5 + m;

  // Timber veranda frame lines
  stroke(0.16, 0.14, 0.12, 0.75);
  strokeWeight(4.0);
  noFill();
  rect(x0, y0, span, span);
  strokeWeight(1.2);
  rect(x0 + 10, y0 + 10, span - 20, span - 20);

  const islands = [
    { x: x0 + span * 0.30, y: y0 + span * 0.32, r: span * 0.085, phase: 0.0 },
    { x: x0 + span * 0.68, y: y0 + span * 0.28, r: span * 0.055, phase: 1.2 },
    { x: x0 + span * 0.52, y: y0 + span * 0.58, r: span * 0.110, phase: 2.4 },
    { x: x0 + span * 0.26, y: y0 + span * 0.74, r: span * 0.062, phase: 3.6 },
    { x: x0 + span * 0.77, y: y0 + span * 0.76, r: span * 0.048, phase: 4.8 }
  ];

  // Subtle parallel background rake
  stroke(0.62, 0.59, 0.53, 0.28);
  strokeWeight(1.0);
  for (let y = y0 + 18; y < y0 + span - 18; y += 10) {
    line(x0 + 14, y, x0 + span - 14, y);
  }

  // Concentric harmonic ripple grooves around each island
  noFill();
  const nRings = floor(rings);
  for (let k = 0; k < islands.length; k++) {
    const isl = islands[k];
    for (let r = 1; r <= nRings; r++) {
      const baseR = isl.r * 1.15 + r * (span * 0.016);
      const alpha = max(0.08, 0.52 - r * 0.024);
      stroke(0.42, 0.40, 0.35, alpha);
      strokeWeight(1.4);
      beginShape();
      for (let s = 0; s <= 48; s++) {
        const a = (s / 48) * TAU;
        const wob = wave_amp * sin(a * 5.0 + t * tempo + isl.phase + r * 0.3);
        const rr = baseR + wob;
        const px = max(x0 + 14, min(x0 + span - 14, isl.x + cos(a) * rr));
        const py = max(y0 + 14, min(y0 + span - 14, isl.y + sin(a) * rr));
        vertex(px, py);
      }
      endShape();
    }
  }

  // Draw moss beds and dark stones on top
  noStroke();
  for (let k = 0; k < islands.length; k++) {
    const isl = islands[k];
    fill(0.35, 0.38, 0.17, 0.94);
    beginShape();
    for (let a = 0; a < 20; a++) {
      const ang = (a / 20) * TAU;
      const rad = isl.r * (0.96 + 0.10 * sin(ang * 4.0 + isl.phase));
      vertex(isl.x + cos(ang) * rad, isl.y + sin(ang) * rad * 0.85);
    }
    endShape();

    fill(0.15, 0.15, 0.14, 0.97);
    beginShape();
    for (let a = 0; a < 9; a++) {
      const ang = (a / 9) * TAU + 0.5;
      const rad = isl.r * (0.58 + 0.16 * cos(ang * 3.0 + k));
      vertex(isl.x + cos(ang) * rad, isl.y + sin(ang) * rad * 0.74);
    }
    endShape();
  }
}