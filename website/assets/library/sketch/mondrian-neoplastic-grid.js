// Mondrian Neoplastic Grid - Infinite Library (Sketch 2D)
param("bar_weight", 11.0, 5.0, 22.0);
param("breathe", 0.08, 0.0, 0.25);
param("craquelure", 160, 0, 400);
param("speed", 0.35, 0.05, 1.2);

function draw(t) {
  // Warm gessoed linen ground (#F3EFE6)
  background(0.953, 0.937, 0.902);
  const w = width;
  const h = height;
  const side = min(w, h);
  const pad = side * 0.06;
  const x0 = (w - side) * 0.5 + pad;
  const y0 = (h - side) * 0.5 + pad;
  const span = side - pad * 2.0;

  // Dynamic asymmetric neoplastic proportions
  const ph = sin(t * speed) * breathe;
  const ph2 = cos(t * speed * 0.7) * breathe * 0.65;
  const vx1 = x0 + span * (0.24 + ph * 0.35);
  const vx2 = x0 + span * (0.87 + ph2 * 0.25);
  const hy1 = y0 + span * (0.31 - ph2 * 0.35);
  const hy2 = y0 + span * (0.72 + ph * 0.30);
  const hy3 = y0 + span * (0.86 - ph * 0.18);

  noStroke();

  // Primary vermilion plane (upper-right dominant field, #D92B20)
  fill(0.851, 0.169, 0.125);
  rect(vx1, y0, (x0 + span) - vx1, hy2 - y0);

  // Cobalt blue plane (lower-left anchor, #124E9B)
  fill(0.071, 0.306, 0.608);
  rect(x0, hy2, vx1 - x0, (y0 + span) - hy2);

  // Cadmium yellow plane (lower-right accent, #EBC328)
  fill(0.922, 0.765, 0.157);
  rect(vx2, hy3, (x0 + span) - vx2, (y0 + span) - hy3);

  // Subtle warm-grey architectural counter-pane (#DFDBD0)
  fill(0.875, 0.860, 0.816);
  rect(x0, hy1, vx1 - x0, hy2 - hy1);

  // Subtle canvas weave & micro-craquelure lines
  stroke(0.68, 0.65, 0.58, 0.14);
  strokeWeight(0.8);
  for (let i = 0; i < craquelure; i++) {
    const cx = x0 + ((i * 0.6180339887) % 1.0) * span;
    const cy = y0 + ((i * 0.3819660113 + sin(i * 7.3) * 0.05) % 1.0) * span;
    const ang = (i % 2 === 0 ? 0.12 : -0.15) + sin(i * 3.1) * 0.25;
    const len = 8.0 + (i % 5) * 4.0;
    line(cx, cy, cx + cos(ang) * len, cy + sin(ang) * len);
  }

  // Stark orthogonal charcoal-black bars (#141414)
  fill(0.078, 0.078, 0.082);
  noStroke();
  const bw = bar_weight * (side / 1080.0);
  // Full vertical & horizontal dividers
  rect(vx1 - bw * 0.5, y0, bw, span);
  rect(x0, hy2 - bw * 0.5, span, bw);
  // Partial dividers
  rect(x0, hy1 - bw * 0.5, vx1 - x0, bw);
  rect(vx2 - bw * 0.5, hy2, bw, (y0 + span) - hy2);
  rect(vx2, hy3 - bw * 0.5, (x0 + span) - vx2, bw);

  // Crisp outer perimeter frame
  noFill();
  stroke(0.078, 0.078, 0.082);
  strokeWeight(bw * 0.65);
  rect(x0, y0, span, span);
}
