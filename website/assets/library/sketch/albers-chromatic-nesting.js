// Albers Chromatic Nesting - Infinite Library (Sketch 2D)
param("shift", 0.06, 0.0, 0.25);
param("frameW", 5.0, 2.0, 10.0);
param("gap", 0.065, 0.03, 0.12);
param("speed", 0.35, 0.0, 1.2);

function clamp(v, lo, hi) {
  return min(max(v, lo), hi);
}

function drawPanel(px, py, size, cols, t, phase) {
  // Brushed aluminum museum frame
  noStroke();
  fill(0.62, 0.64, 0.65);
  rect(px - frameW, py - frameW, size + frameW * 2.0, size + frameW * 2.0);
  fill(0.14, 0.15, 0.16);
  rect(px - 1.5, py - 1.5, size + 3.0, size + 3.0);

  // 4 nested squares with Albers bottom-heavy ratio: bottom=1u, side=2u, top=3u
  const ratios = [1.0, 0.78, 0.56, 0.35];
  const breathe = sin(t * speed + phase) * shift;
  for (let i = 0; i < 4; i++) {
    const s = size * ratios[i];
    const dx = (size - s) * 0.5;
    // Bottom margin is 1/4 of vertical slack, top margin is 3/4 of vertical slack
    const dy = (size - s) * 0.75;
    const c = cols[i];
    const r = clamp(c[0] + (i >= 2 ? breathe : 0.0), 0.0, 1.0);
    const g = clamp(c[1] + (i >= 2 ? breathe * 0.6 : 0.0), 0.0, 1.0);
    const b = clamp(c[2] - (i >= 2 ? breathe * 0.4 : 0.0), 0.0, 1.0);
    fill(r, g, b);
    rect(px + dx, py + dy, s, s);
  }
}

function draw(t) {
  // Warm museum gallery plaster (#DCDAD4)
  background(0.863, 0.855, 0.831);
  const w = width;
  const h = height;
  const span = min(w, h);
  const margin = span * 0.09;
  const g = span * gap;
  const cell = (span - margin * 2.0 - g) * 0.5;
  const x0 = (w - (cell * 2.0 + g)) * 0.5;
  const y0 = (h - (cell * 2.0 + g)) * 0.5;

  // Fine Bauhaus registration grid hairlines
  stroke(0.18, 0.19, 0.20, 0.18);
  strokeWeight(1.0);
  line(w * 0.5, margin * 0.35, w * 0.5, h - margin * 0.35);
  line(margin * 0.35, h * 0.5, w - margin * 0.35, h * 0.5);

  // 1. Emerald -> Pine -> Deep Teal
  const p1 = [[0.035, 0.365, 0.278], [0.055, 0.294, 0.259], [0.067, 0.235, 0.243], [0.082, 0.192, 0.220]];
  // 2. Burnt Ochre -> Marigold -> Cadmium -> Amber
  const p2 = [[0.725, 0.392, 0.118], [0.804, 0.518, 0.114], [0.878, 0.690, 0.078], [0.808, 0.620, 0.075]];
  // 3. Cobalt Ultramarine -> Petrol -> Viridian -> Emerald
  const p3 = [[0.047, 0.302, 0.569], [0.047, 0.341, 0.427], [0.039, 0.388, 0.365], [0.027, 0.435, 0.314]];
  // 4. Terracotta Rust -> Charcoal Umber -> Deep Teal -> Slate
  const p4 = [[0.541, 0.165, 0.094], [0.165, 0.169, 0.157], [0.059, 0.255, 0.259], [0.090, 0.325, 0.314]];

  drawPanel(x0, y0, cell, p1, t, 0.0);
  drawPanel(x0 + cell + g, y0, cell, p2, t, 1.57);
  drawPanel(x0, y0 + cell + g, cell, p3, t, 3.14);
  drawPanel(x0 + cell + g, y0 + cell + g, cell, p4, t, 4.71);

  // Plate index marks at bottom
  noStroke();
  fill(0.16, 0.17, 0.18, 0.65);
  for (let i = 0; i < 4; i++) {
    const bx = x0 + (i % 2) * (cell + g);
    const by = y0 + floor(i / 2) * (cell + g) + cell + 14;
    rect(bx, by, 18, 3);
    rect(bx + 24, by, 6, 3);
  }
}
