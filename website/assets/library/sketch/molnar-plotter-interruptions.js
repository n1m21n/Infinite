// Molnar Plotter Interruptions - Infinite Library (Sketch 2D)
param("grid_n", 44, 24, 68);
param("void_thresh", 0.24, 0.05, 0.45);
param("jitter", 0.95, 0.2, 1.8);
param("drift", 0.12, 0.0, 0.5);

function hash2(x, y) {
  const s = sin(x * 127.1 + y * 311.7) * 43758.5453;
  return s - floor(s);
}

function draw(t) {
  // Archival off-white plotter paper (#E5E6E1)
  background(0.898, 0.902, 0.882);
  const w = width;
  const h = height;
  const margin = min(w, h) * 0.095;
  const span = min(w, h) - margin * 2.0;
  const x0 = (w - span) * 0.5;
  const y0 = (h - span) * 0.5;

  // Fine plotter registration ticks around border
  stroke(0.420, 0.427, 0.408, 0.72);
  strokeWeight(1.1);
  for (let i = 0; i <= 24; i++) {
    const u = i / 24.0;
    const px = x0 + u * span;
    const py = y0 + u * span;
    line(px, y0 - 16, px, y0 - 8);
    line(px, y0 + span + 8, px, y0 + span + 16);
    line(x0 - 16, py, x0 - 8, py);
    line(x0 + span + 8, py, x0 + span + 16, py);
  }

  // Vermilion plotter datum crosshairs (#B33A2E)
  stroke(0.702, 0.227, 0.180, 0.85);
  strokeWeight(1.4);
  const cOff = 24;
  line(x0 - cOff, y0 - cOff, x0 - cOff + 10, y0 - cOff);
  line(x0 - cOff, y0 - cOff, x0 - cOff, y0 - cOff + 10);
  line(x0 + span + cOff, y0 + span + cOff, x0 + span + cOff - 10, y0 + span + cOff);
  line(x0 + span + cOff, y0 + span + cOff, x0 + span + cOff, y0 + span + cOff - 10);

  // Algorithmic interrupted plotter hatching
  const n = floor(grid_n);
  const cell = span / n;
  const phase = floor(t * drift * 2.0);
  for (let iy = 0; iy < n; iy++) {
    for (let ix = 0; ix < n; ix++) {
      const cx = x0 + (ix + 0.5) * cell;
      const cy = y0 + (iy + 0.5) * cell;

      // Rectangular negative-space voids ("interruptions")
      const b1 = (ix > n * 0.18 && ix < n * 0.38 && iy > n * 0.62 && iy < n * 0.86);
      const b2 = (ix > n * 0.56 && ix < n * 0.78 && iy > n * 0.14 && iy < n * 0.44);
      const vHash = hash2(floor(ix / 5.0), floor(iy / 5.0));
      if (b1 || b2 || vHash < void_thresh * 0.65) continue;

      // Dense overlapping zone vs airy zone
      const dense = (ix > n * 0.22 && ix < n * 0.46 && iy > n * 0.16 && iy < n * 0.58);
      const passes = dense ? 2 : 1;

      for (let p = 0; p < passes; p++) {
        const h1 = hash2(ix + p * 73.0, iy + phase * 0.01);
        const h2 = hash2(ix * 1.7 + p * 19.0, iy * 2.3);
        const ang = (h1 - 0.5) * PI * jitter + sin(t * drift + ix * 0.08 + iy * 0.05) * 0.25;
        const len = cell * (0.72 + h2 * 0.75);
        const dx = cos(ang) * len * 0.5;
        const dy = sin(ang) * len * 0.5;

        if (h2 > 0.94) {
          stroke(0.702, 0.227, 0.180, 0.88);
          strokeWeight(1.5);
        } else {
          stroke(0.094, 0.094, 0.090, dense ? 0.88 : 0.78);
          strokeWeight(dense ? 1.35 : 1.15);
        }
        line(cx - dx, cy - dy, cx + dx, cy + dy);
      }
    }
  }
}
