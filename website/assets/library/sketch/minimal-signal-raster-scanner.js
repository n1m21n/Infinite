// Minimal Signal Raster Scanner - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Ryoji Ikeda inspired high-frequency raster scanner with horizontal scanlines, binary barcode pulses, and electric red strobe markers.
param("scan_speed", 0.5, 0.1, 2.0);
param("line_density", 28.0, 10.0, 60.0);
param("strobe_rate", 1.2, 0.2, 4.0);
param("track_width", 0.85, 0.4, 0.95);

function draw(t) {
  // Deep carbon black background #101010
  background(0.063, 0.063, 0.063);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * track_width;
  const time = t * scan_speed;
  const lines = floor(line_density);

  push();
  translate(cx - span * 0.5, cy - span * 0.5);

  // Outer framing boundary
  noFill();
  stroke(0.94, 0.94, 0.94, 0.4);
  strokeWeight(1.5);
  rect(0, 0, span, span);

  // Raster scanlines & binary pulse bars
  for (let i = 0; i < lines; i++) {
    const y = (i / lines) * span;
    const h = span / lines * 0.7;

    // Scan segment alternating white and carbon blocks
    if (i % 2 === 0) {
      noStroke();
      fill(0.94, 0.94, 0.94);
      const segW = span * (0.15 + 0.35 * (sin(i * 1.7 + time) * 0.5 + 0.5));
      const segX = (span - segW) * (cos(i * 0.9 + time * 0.5) * 0.5 + 0.5);
      rect(segX, y, segW, h);

      // Barcode sub-ticks
      stroke(0.063, 0.063, 0.063);
      strokeWeight(1.2);
      for (let k = 0; k < 6; k++) {
        const tx = segX + (k / 5.0) * segW;
        line(tx, y, tx, y + h);
      }
    } else {
      // High-precision measurement grid line
      stroke(0.94, 0.94, 0.94, 0.35);
      strokeWeight(1.0);
      line(0, y + h * 0.5, span, y + h * 0.5);
    }

    // Electric signal red strobe indicator
    if (i % 7 === 3) {
      noStroke();
      fill(1.0, 0.2, 0.0);
      const rx = (sin(time * strobe_rate + i) * 0.5 + 0.5) * (span - 24.0);
      rect(rx, y, 22.0, h);
    }
  }

  // Central oscilloscope waveform ribbon
  stroke(1.0, 0.2, 0.0);
  strokeWeight(2.5);
  noFill();
  beginShape();
  for (let x = 0; x <= span; x += 8) {
    const normX = x / span;
    const wave = sin(normX * 18.0 + time * 4.0) * cos(normX * 6.0) * (span * 0.18);
    vertex(x, span * 0.5 + wave);
  }
  endShape();

  // Metric ticks along left and right
  stroke(0.94, 0.94, 0.94, 0.8);
  strokeWeight(1.5);
  for (let k = 0; k <= 10; k++) {
    const ty = (k / 10.0) * span;
    line(0, ty, 14, ty);
    line(span - 14, ty, span, ty);
  }

  pop();
}
