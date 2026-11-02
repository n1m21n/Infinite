// Acid Graphics Warp Typography - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Algorithmic Warp-era typographic layout with curvilinear sine-distorted letterforms, bold chromatic contrast rules, and geometric registration marks.
param("zoom", 1.0, 0.5, 2.0);
param("line_weight", 2.2, 1.0, 6.0);
param("wave_freq", 3.5, 1.0, 8.0);
param("accent_scale", 1.1, 0.5, 2.0);

function draw(t) {
  background(0.065, 0.055, 0.09);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88 * zoom;

  push();
  translate(cx, cy);

  noFill();
  stroke(0.78, 1.0, 0.0); // Acid chartreuse
  strokeWeight(line_weight * 1.5);
  rect(-span * 0.5, -span * 0.5, span, span);

  stroke(0.3, 0.22, 0.42);
  strokeWeight(1.5);
  const bands = 14;
  for (let b = 0; b < bands; b++) {
    const y0 = (-0.45 + (b / bands) * 0.9) * span;
    beginShape();
    for (let x = -span * 0.48; x <= span * 0.48; x += span * 0.04) {
      const w = sin(x * (0.012 * wave_freq) + b * 0.4) * (span * 0.06);
      vertex(x, y0 + w);
    }
    endShape();
  }

  noStroke();
  fill(0.78, 1.0, 0.0);
  rect(-span * 0.38, -span * 0.22, span * 0.16 * accent_scale, span * 0.55 * accent_scale);
  rect(-span * 0.14, -span * 0.22, span * 0.14 * accent_scale, span * 0.32 * accent_scale);

  fill(1.0, 0.0, 0.5);
  rect(-span * 0.14, span * 0.16, span * 0.45 * accent_scale, span * 0.14 * accent_scale);
  rect(span * 0.18, -span * 0.32, span * 0.18 * accent_scale, span * 0.62 * accent_scale);

  stroke(0.96, 0.98, 1.0);
  strokeWeight(line_weight);
  noFill();
  circle(span * 0.22, 0, span * 0.22 * accent_scale);
  circle(span * 0.22, 0, span * 0.08);

  stroke(0.78, 1.0, 0.0);
  line(-span * 0.45, span * 0.38, span * 0.45, span * 0.38);
  for (let s = -span * 0.42; s <= span * 0.42; s += span * 0.06) {
    line(s, span * 0.36, s, span * 0.40);
  }

  pop();
}
