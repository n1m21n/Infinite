// Karesansui Raked Contours - Infinite Library (Sketch 2D)
param("rake_lines", 38, 18, 64);
param("deflect", 1.15, 0.4, 2.0);
param("grain_count", 600, 100, 1400);
param("drift", 0.12, 0.0, 0.5);

function draw(t) {
  // Warm limestone gravel ground (#D8D4C9)
  background(0.847, 0.831, 0.788);
  const w = width;
  const h = height;
  const side = min(w, h);
  const margin = side * 0.09;
  const x0 = (w - side) * 0.5 + margin;
  const y0 = (h - side) * 0.5 + margin;
  const span = side - margin * 2.0;

  // Weathered clay-ochre garden enclosure border
  stroke(0.588, 0.506, 0.400, 0.65);
  strokeWeight(2.5);
  noFill();
  rect(x0 - 10, y0 - 10, span + 20, span + 20);

  // Three rock islands (normalized in garden frame)
  const rocks = [
    { x: x0 + span * 0.27, y: y0 + span * 0.34, r: span * 0.095 },
    { x: x0 + span * 0.56, y: y0 + span * 0.47, r: span * 0.068 },
    { x: x0 + span * 0.75, y: y0 + span * 0.70, r: span * 0.105 }
  ];

  // Fine mineral gravel stipple
  noStroke();
  for (let i = 0; i < grain_count; i++) {
    const gx = x0 + ((i * 0.6180339887) % 1.0) * span;
    const gy = y0 + ((i * 0.3819660113 + sin(i * 12.7) * 0.03) % 1.0) * span;
    const shade = (i % 3 === 0) ? 0.62 : 0.92;
    fill(shade, shade * 0.98, shade * 0.94, 0.32);
    circle(gx, gy, 1.4);
  }

  // Parallel raked gravel furrows deflecting around islands
  const rows = floor(rake_lines);
  const segs = 96;
  noFill();
  for (let r = 0; r < rows; r++) {
    const baseY = y0 + (r + 0.5) * (span / rows);
    // Shadow groove
    stroke(0.48, 0.46, 0.42, 0.42);
    strokeWeight(1.8);
    beginShape();
    for (let s = 0; s <= segs; s++) {
      const px = x0 + (s / segs) * span;
      let py = baseY + sin(px * 0.008 + t * drift) * 2.5;
      for (let k = 0; k < rocks.length; k++) {
        const dx = px - rocks[k].x;
        const dy = py - rocks[k].y;
        const d2 = dx * dx + dy * dy + 1.0;
        const R2 = (rocks[k].r * 1.45) * (rocks[k].r * 1.45);
        if (d2 < R2 * 4.5) {
          const pushY = (dy >= 0 ? 1 : -1) * (R2 / (d2 + R2 * 0.45)) * rocks[k].r * 0.58 * deflect;
          py += pushY;
        }
      }
      vertex(px, py);
    }
    endShape();

    // Sunlit ridge highlight
    stroke(0.95, 0.94, 0.90, 0.55);
    strokeWeight(1.0);
    beginShape();
    for (let s = 0; s <= segs; s++) {
      const px = x0 + (s / segs) * span;
      let py = baseY - 2.2 + sin(px * 0.008 + t * drift) * 2.5;
      for (let k = 0; k < rocks.length; k++) {
        const dx = px - rocks[k].x;
        const dy = py - rocks[k].y;
        const d2 = dx * dx + dy * dy + 1.0;
        const R2 = (rocks[k].r * 1.45) * (rocks[k].r * 1.45);
        if (d2 < R2 * 4.5) {
          const pushY = (dy >= 0 ? 1 : -1) * (R2 / (d2 + R2 * 0.45)) * rocks[k].r * 0.58 * deflect;
          py += pushY;
        }
      }
      vertex(px, py);
    }
    endShape();
  }

  // Concentric ripple rings, moss halos, and faceted basalt stones
  for (let k = 0; k < rocks.length; k++) {
    const rk = rocks[k];
    noFill();
    for (let ring = 1; ring <= 4; ring++) {
      stroke(0.46, 0.44, 0.40, 0.45 - ring * 0.07);
      strokeWeight(1.5);
      circle(rk.x, rk.y, rk.r * (1.05 + ring * 0.16) * 2.0);
    }
    // Moss halo (#585D2A)
    noStroke();
    fill(0.345, 0.365, 0.165, 0.92);
    beginShape();
    for (let a = 0; a < 24; a++) {
      const ang = (a / 24) * TAU;
      const rad = rk.r * (0.92 + 0.12 * sin(ang * 3.0 + k * 1.7));
      vertex(rk.x + cos(ang) * rad, rk.y + sin(ang) * rad * 0.78);
    }
    endShape();
    // Basalt stone (#232320)
    fill(0.137, 0.137, 0.125, 0.96);
    beginShape();
    for (let a = 0; a < 10; a++) {
      const ang = (a / 10) * TAU + 0.3;
      const rad = rk.r * (0.62 + 0.18 * cos(ang * 2.0 + k * 2.3));
      vertex(rk.x + cos(ang) * rad, rk.y + sin(ang) * rad * 0.72);
    }
    endShape();
  }
}