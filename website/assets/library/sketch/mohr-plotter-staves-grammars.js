// Mohr Plotter Staves Grammars - Infinite Library (Sketch 2D)
param("staves", 8, 5, 12);
param("cols", 42, 24, 64);
param("complexity", 0.55, 0.2, 0.95);
param("drift", 0.18, 0.0, 0.6);

function hash(n) {
  const s = sin(n * 91.345 + 17.13) * 43758.5453;
  return s - floor(s);
}

function draw(t) {
  // Warm archival cotton-rag vellum (#D5D1C7)
  background(0.835, 0.820, 0.780);
  const w = width;
  const h = height;
  const s = min(w, h);
  const marginX = s * 0.115;
  const marginY = s * 0.12;
  const gw = w - marginX * 2.0;
  const gh = h - marginY * 2.0;
  const nS = floor(staves);
  const nC = floor(cols);
  const rowH = gh / nS;
  const stepX = gw / nC;

  noFill();

  for (let st = 0; st < nS; st++) {
    const baseY = marginY + (st + 0.5) * rowH;
    const amp = rowH * 0.34;

    // Subtle baseline hairline
    stroke(0.333, 0.341, 0.357, 0.22);
    strokeWeight(0.8);
    line(marginX, baseY, marginX + gw, baseY);

    let prevX = marginX;
    let prevY = baseY;

    for (let c = 0; c < nC; c++) {
      const x0 = marginX + c * stepX;
      const x1 = x0 + stepX;
      const seed = st * 131 + c * 17;
      const r = hash(seed + floor(t * drift) * 0.0);
      const mode = floor(r * 4.0);

      // Pen weight alternation: hairline vs bold inked bar
      const bold = hash(seed + 7.3) > 0.72;
      const isAccent = hash(seed + 19.7) > 0.965;
      if (isAccent) {
        stroke(0.784, 0.294, 0.192, 0.92);
        strokeWeight(2.8);
      } else {
        stroke(0.067, 0.071, 0.078, bold ? 0.95 : 0.78);
        strokeWeight(bold ? 3.0 : 1.15);
      }

      const wave = sin(c * 0.35 + st * 1.1 + t * drift * 0.7);
      if (mode === 0) {
        // 45-degree sawtooth zigzag
        const yPeak = baseY + (c % 2 === 0 ? -amp : amp) * (0.45 + 0.45 * abs(wave));
        line(prevX, prevY, x0 + stepX * 0.5, yPeak);
        line(x0 + stepX * 0.5, yPeak, x1, baseY);
        prevX = x1;
        prevY = baseY;
      } else if (mode === 1) {
        // Orthogonal square-wave plateau
        const yStep = baseY + (wave > 0 ? -amp * 0.7 : amp * 0.7);
        line(prevX, prevY, x0, yStep);
        line(x0, yStep, x1, yStep);
        prevX = x1;
        prevY = yStep;
      } else if (mode === 2 && r < complexity + 0.25) {
        // Dense orthogonal cubic-lattice cluster
        const sub = 3;
        const dx = stepX / sub;
        for (let k = 0; k < sub; k++) {
          const sx = x0 + k * dx;
          const sy = baseY + (hash(seed + k * 5.1) - 0.5) * amp * 1.6;
          const sh = (hash(seed + k * 9.3) - 0.5) * amp * 1.1;
          strokeWeight(k === 1 ? 2.4 : 1.0);
          line(sx, sy, sx + dx, sy);
          line(sx + dx, sy, sx + dx, sy + sh);
          line(sx, baseY, sx + dx, sy + sh);
        }
        prevX = x1;
        prevY = baseY;
      } else {
        // Quiet horizontal rest segment
        line(prevX, prevY, x1, baseY);
        prevX = x1;
        prevY = baseY;
      }
    }
  }
}
