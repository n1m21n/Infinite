// Albers Gobelin Warp Grid - Infinite Library (Sketch 2D)
param("warp_cols", 7, 4, 12);
param("weft_rows", 18, 10, 28);
param("shift", 0.25, 0.0, 0.8);
param("grain", 480, 120, 900);

function draw(t) {
  // Warm unbleached linen paper ground (#EDE6C8)
  background(0.929, 0.902, 0.784);
  const w = width;
  const h = height;
  const padX = w * 0.14;
  const padY = h * 0.08;
  const spanW = w - padX * 2;
  const spanH = h - padY * 2;
  const cols = floor(warp_cols);
  const rows = floor(weft_rows);
  const cw = spanW / cols;
  const rh = spanH / rows;

  // Fine linen-paper pencil drafting grid & fiber stipple
  stroke(0.68, 0.64, 0.54, 0.28);
  strokeWeight(1.0);
  for (let c = 0; c <= cols; c++) {
    const x = padX + c * cw;
    line(x, padY * 0.5, x, h - padY * 0.5);
  }
  for (let r = 0; r <= rows; r++) {
    const y = padY + r * rh;
    line(padX * 0.5, y, w - padX * 0.5, y);
  }

  noStroke();
  for (let i = 0; i < grain; i++) {
    const gx = ((i * 0.6180339887) % 1.0) * w;
    const gy = ((i * 0.3819660113 + sin(i * 7.1) * 0.03) % 1.0) * h;
    fill(0.45, 0.42, 0.35, 0.12);
    circle(gx, gy, 1.4);
  }

  // Gouache side selvedge washes
  fill(0.902, 0.808, 0.102, 0.22);
  rect(padX * 0.48, padY, padX * 0.48, spanH);
  rect(w - padX * 0.96, padY, padX * 0.48, spanH);

  // Interlocking weft blocks across warp columns
  for (let r = 0; r < rows; r++) {
    const y = padY + r * rh;
    let c = 0;
    while (c < cols) {
      const hVal = sin(r * 3.17 + c * 5.41 + floor(t * shift) * 0.7);
      const span = (abs(hVal) > 0.55 && c + 2 <= cols) ? 2 : ((abs(hVal) < 0.22 && c + 3 <= cols) ? 3 : 1);
      const sel = abs(sin(r * 1.9 + c * 2.7)) * 4.0;
      const bw = span * cw - 2.0;
      const bh = rh * 0.84;

      if (sel < 1.1) {
        // Cadmium yellow (#E6CE1A)
        fill(0.902, 0.808, 0.102, 0.94);
      } else if (sel < 2.2) {
        // Verdigris pine (#2C5C54)
        fill(0.173, 0.361, 0.329, 0.94);
      } else if (sel < 3.1) {
        // Slate charcoal (#4A464B)
        fill(0.290, 0.275, 0.294, 0.92);
      } else {
        // Warm raw linen gap (#F5F1E0)
        fill(0.961, 0.945, 0.878, 0.90);
      }
      rect(padX + c * cw + 1.0, y + rh * 0.08, bw, bh);

      // Subtle horizontal thread striations inside each block
      stroke(0.15, 0.14, 0.13, 0.16);
      strokeWeight(1.0);
      line(padX + c * cw + 2.0, y + rh * 0.36, padX + c * cw + bw, y + rh * 0.36);
      line(padX + c * cw + 2.0, y + rh * 0.64, padX + c * cw + bw, y + rh * 0.64);
      noStroke();

      c += span;
    }
  }

  // Crisp graphite border around tapestry field
  noFill();
  stroke(0.25, 0.24, 0.22, 0.65);
  strokeWeight(1.8);
  rect(padX, padY, spanW, spanH);
}