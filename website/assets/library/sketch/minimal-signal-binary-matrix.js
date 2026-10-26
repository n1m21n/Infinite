// Minimal Signal Binary Matrix - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Precision digital test pattern matrix with algorithmic binary telemetry, segmented waveform ribbons, and focal signal reticles.
param("clock_speed", 0.45, 0.1, 2.0);
param("matrix_cols", 8.0, 4.0, 16.0);
param("strobe_pulse", 1.5, 0.5, 3.0);
param("reticle_scale", 1.1, 0.5, 2.0);

function draw(t) {
  // Pure carbon ground #101010
  background(0.063, 0.063, 0.063);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const minDim = min(width, height);
  const span = minDim * 0.86;
  const time = t * clock_speed;
  const cols = floor(matrix_cols);
  const colW = span / cols;

  push();
  translate(cx - span * 0.5, cy - span * 0.5);

  // Outer framing line
  noFill();
  stroke(0.94, 0.94, 0.94, 0.3);
  strokeWeight(1.0);
  rect(0, 0, span, span);

  // Vertical binary data tracks
  for (let c = 0; c < cols; c++) {
    const x = c * colW;

    // Track divider
    stroke(0.94, 0.94, 0.94, 0.2);
    strokeWeight(1.0);
    line(x, 0, x, span);

    // Segmented binary pulses in each column
    const rows = 12;
    const rowH = span / rows;
    for (let r = 0; r < rows; r++) {
      const y = r * rowH;
      const seedVal = sin(c * 17.3 + r * 31.7 + time * 2.5);

      if (seedVal > 0.15) {
        // High signal white telemetry block
        noStroke();
        fill(0.94, 0.94, 0.94);
        rect(x + 4.0, y + 3.0, colW - 8.0, rowH - 6.0);

        // Internal barcode slits
        stroke(0.063, 0.063, 0.063);
        strokeWeight(1.0);
        line(x + colW * 0.35, y + 3.0, x + colW * 0.35, y + rowH - 3.0);
        line(x + colW * 0.7, y + 3.0, x + colW * 0.7, y + rowH - 3.0);
      } else if (seedVal < -0.65) {
        // Critical signal red pulse marker
        noStroke();
        fill(1.0, 0.2, 0.0);
        rect(x + 6.0, y + rowH * 0.35, colW - 12.0, rowH * 0.3);
      } else {
        // Subtle measurement crosshair
        stroke(0.94, 0.94, 0.94, 0.4);
        strokeWeight(1.0);
        line(x + colW * 0.5 - 4.0, y + rowH * 0.5, x + colW * 0.5 + 4.0, y + rowH * 0.5);
      }
    }
  }

  // Central focal targeting reticle
  push();
  translate(span * 0.5, span * 0.5);
  noFill();
  stroke(1.0, 0.2, 0.0);
  strokeWeight(2.0);
  const retSize = minDim * 0.22 * reticle_scale;
  ellipse(0, 0, retSize, retSize);
  ellipse(0, 0, retSize * 0.5, retSize * 0.5);
  line(-retSize * 0.7, 0, retSize * 0.7, 0);
  line(0, -retSize * 0.7, 0, retSize * 0.7);

  // Red beacon dot
  noStroke();
  fill(1.0, 0.2, 0.0);
  circle(0, 0, 10.0);
  pop();

  // Top and bottom status text lines (simulated raster data)
  stroke(0.94, 0.94, 0.94, 0.85);
  strokeWeight(2.0);
  for (let k = 0; k < 18; k++) {
    const tx = (k / 18.0) * span + 6.0;
    line(tx, -12, tx + (k % 3 === 0 ? 12 : 5), -12);
    line(tx, span + 12, tx + (k % 2 === 0 ? 10 : 4), span + 12);
  }

  pop();
}
