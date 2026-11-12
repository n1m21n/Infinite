// Op Art Riley Perceptual Moire Grid - Infinite Library (Sketch 2D)
param("frequency", 28.0, 12.0, 60.0);
param("amplitude", 36.0, 10.0, 80.0);
param("line_weight", 1.8, 0.8, 4.0);
param("contrast", 1.25, 0.8, 1.8);

function draw(t) {
  // Deep ivory background (#f8f8f5)
  background(0.972, 0.972, 0.960);

  const w = width;
  const h = height;
  const margin = 64;
  const innerW = w - margin * 2;
  const innerH = h - margin * 2;

  // Outer framing boundary
  stroke(0.05, 0.05, 0.06);
  strokeWeight(line_weight * 1.5);
  noFill();
  rect(margin, margin, innerW, innerH);

  // Optical wave field: dense horizontal sinusoidal ribbons creating moire vibration
  const numLines = floor(frequency * 2.2);
  const dy = innerH / numLines;

  for (let i = 0; i < numLines; i++) {
    const y0 = margin + i * dy;
    const normI = i / numLines;

    // Bridget Riley undulating wave displacement
    const phaseShift = normI * Math.PI * 4.0;
    const amp = amplitude * Math.sin(normI * Math.PI);

    // Dynamic stroke weight modulation creating compression/dilation banding
    const sw = (line_weight * 0.8) + Math.abs(Math.sin(normI * Math.PI * 3.0)) * line_weight * 1.6;
    strokeWeight(sw);
    stroke(0.04, 0.04, 0.05);
    noFill();

    // Render continuous wave path
    const steps = 90;
    const dx = innerW / steps;
    let prevX = margin;
    let prevY = y0 + Math.sin(phaseShift) * amp;

    for (let s = 1; s <= steps; s++) {
      const curX = margin + s * dx;
      const wave = Math.sin((s / steps) * Math.PI * 6.0 + phaseShift);
      const wave2 = Math.cos((s / steps) * Math.PI * 3.0 - phaseShift * 0.5);
      const curY = y0 + (wave * 0.7 + wave2 * 0.3) * amp;
      line(prevX, prevY, curX, curY);
      prevX = curX;
      prevY = curY;
    }
  }

  // Vertical intersecting counter-phase interference lines (creating moire diamonds)
  strokeWeight(line_weight * 0.9);
  stroke(0.04, 0.04, 0.05, 0.75);
  const numV = 24;
  const dv = innerW / numV;
  for (let j = 0; j <= numV; j++) {
    const vx = margin + j * dv;
    const vPhase = (j / numV) * Math.PI * 3.0;
    const vAmp = amplitude * 0.45;
    let px = vx + Math.sin(vPhase) * vAmp;
    let py = margin;
    for (let s = 1; s <= 60; s++) {
      const cy = margin + (s / 60) * innerH;
      const cx = vx + Math.sin((s / 60) * Math.PI * 4.0 + vPhase) * vAmp;
      line(px, py, cx, cy);
      px = cx;
      py = cy;
    }
  }

  // Optical focal diamond tension accents
  noStroke();
  fill(0.04, 0.04, 0.05);
  const cx = w * 0.5;
  const cy = h * 0.5;
  ellipse(cx, cy, 24, 24);
  stroke(0.972, 0.972, 0.960);
  strokeWeight(2.0);
  line(cx - 16, cy, cx + 16, cy);
  line(cx, cy - 16, cx, cy + 16);
}
