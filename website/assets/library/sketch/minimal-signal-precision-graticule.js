// Minimal Signal Precision Graticule - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// High-density technical vector graticule with calibrated sub-division scales, concentric radar sweep rings, binary telemetry data columns, and high-contrast signal crosshairs.
param("scale_factor", 1.05, 0.5, 2.0);
param("sweep_speed", 0.40, 0.1, 1.5);
param("rings", 5.0, 3.0, 8.0);
param("density", 1.2, 0.5, 2.0);

function draw(t) {
  // Midnight telemetry black (#0a0d14)
  background(0.039, 0.051, 0.078);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88 * scale_factor;
  const time = t * sweep_speed;

  push();
  translate(cx, cy);

  // 1. Concentric radar telemetry circles
  const nRings = floor(rings);
  noFill();
  for (let i = 1; i <= nRings; i++) {
    const r = (i / nRings) * (span * 0.46);
    stroke(0.12, 0.16, 0.22);
    strokeWeight(1.2);
    circle(0, 0, r * 2.0);

    // Degree tick marks on outer ring
    if (i === nRings) {
      stroke(0.20, 0.26, 0.36);
      strokeWeight(1.4);
      const segs = 36;
      for (let s = 0; s < segs; s++) {
        const th = (s / segs) * Math.PI * 2.0;
        const len = (s % 9 === 0) ? span * 0.03 : (s % 3 === 0) ? span * 0.018 : span * 0.008;
        line(cos(th) * r, sin(th) * r, cos(th) * (r - len), sin(th) * (r - len));
      }
    }
  }

  // 2. Rotating radar sweep vector line & beam phosphor flare
  push();
  rotate(time * 1.8);
  stroke(1.0, 0.67, 0.02, 0.85); // Electric amber
  strokeWeight(2.0);
  line(0, 0, span * 0.46, 0);

  // Target strobe pips along the sweep line
  fill(1.0, 0.67, 0.02);
  noStroke();
  circle(span * 0.28, 0, span * 0.022);
  circle(span * 0.40, 0, span * 0.016);
  pop();

  // 3. Orthogonal coordinate calibration axes
  stroke(0.94, 0.96, 1.0);
  strokeWeight(1.8);
  line(-span * 0.46, 0, span * 0.46, 0);
  line(0, -span * 0.46, 0, span * 0.46);

  // Center optical crosshair reticle
  noFill();
  stroke(0.94, 0.96, 1.0);
  strokeWeight(2.2);
  circle(0, 0, span * 0.08);

  fill(0.94, 0.96, 1.0);
  circle(0, 0, span * 0.016);

  // 4. Binary telemetry data columns & step calibration scales
  const cols = 4;
  const rows = 12;
  const colW = span * 0.06;
  const rowH = span * 0.025;

  noStroke();
  for (let c = 0; c < cols; c++) {
    for (let r = 0; r < rows; r++) {
      const active = ((c * 7 + r * 13 + floor(time * 4.0)) % 5 === 0);
      if (active) {
        fill(1.0, 0.67, 0.02); // Amber active telemetry bit
      } else {
        fill(0.14, 0.18, 0.26);
      }
      rect(-span * 0.42 + c * colW, span * 0.14 + r * rowH, colW * 0.7, rowH * 0.6);
    }
  }

  // Symmetric telemetry readout on opposite quadrant
  for (let c = 0; c < cols; c++) {
    for (let r = 0; r < rows; r++) {
      const active = ((c * 11 + r * 5 + floor(time * 3.5)) % 4 === 0);
      if (active) {
        fill(0.94, 0.96, 1.0); // White data bit
      } else {
        fill(0.12, 0.15, 0.22);
      }
      rect(span * 0.22 + c * colW, -span * 0.40 + r * rowH, colW * 0.7, rowH * 0.6);
    }
  }

  // 5. Margin registration markers
  stroke(0.94, 0.96, 1.0, 0.7);
  strokeWeight(1.5);
  noFill();
  rect(-span * 0.46, -span * 0.46, span * 0.05, span * 0.05);
  rect(span * 0.41, -span * 0.46, span * 0.05, span * 0.05);
  rect(-span * 0.46, span * 0.41, span * 0.05, span * 0.05);
  rect(span * 0.41, span * 0.41, span * 0.05, span * 0.05);

  pop();
}
