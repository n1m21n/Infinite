// Minimal Signal Oscilloscope Vector - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// High-precision vector oscilloscope waveform visualization with calibrated coordinate graticule, harmonic frequency traces, and amber trigger strobes.
param("frequency", 2.4, 0.5, 8.0);
param("harmonics", 3.0, 1.0, 6.0);
param("drift", 0.45, 0.1, 1.5);
param("sweep_gain", 1.1, 0.5, 2.0);

function draw(t) {
  background(0.06, 0.07, 0.09);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.86;
  const time = t * drift;

  push();
  translate(cx, cy);

  // 1. Oscilloscope graticule border & grid
  stroke(0.18, 0.22, 0.28);
  strokeWeight(2.0);
  noFill();
  rect(-span * 0.5, -span * 0.5, span, span);

  // 10x10 graticule grid subdivisions
  stroke(0.14, 0.18, 0.24);
  strokeWeight(1.2);
  const divs = 10;
  for (let i = 1; i < divs; i++) {
    const x = -span * 0.5 + (i / divs) * span;
    line(x, -span * 0.5, x, span * 0.5);
    line(-span * 0.5, x, span * 0.5, x);
  }

  // Major axis crosshairs with subdivision ticks
  stroke(0.28, 0.35, 0.45);
  strokeWeight(2.0);
  line(-span * 0.5, 0, span * 0.5, 0);
  line(0, -span * 0.5, 0, span * 0.5);

  const ticks = 50;
  for (let k = 0; k <= ticks; k++) {
    const p = -span * 0.5 + (k / ticks) * span;
    const len = (k % 5 === 0) ? span * 0.020 : span * 0.010;
    line(p, -len, p, len);
    line(-len, p, len, p);
  }

  // 2. Harmonic Lissajous reference orbit
  noFill();
  stroke(0.24, 0.32, 0.42);
  strokeWeight(1.8);
  const ringPts = 72;
  beginShape();
  for (let i = 0; i <= ringPts; i++) {
    const a = (i / ringPts) * Math.PI * 2.0;
    const r = span * 0.38 + sin(a * 4.0 + time * 1.5) * span * 0.03;
    vertex(cos(a) * r, sin(a) * r);
  }
  endShape();

  // 3. FFT spectrum bar readout across bottom
  const bars = 24;
  const barW = (span * 0.88) / bars;
  noStroke();
  for (let b = 0; b < bars; b++) {
    const frac = b / bars;
    const h = (sin(frac * 6.28 + time * 2.0) * 0.5 + 0.5) * (span * 0.12) + span * 0.02;
    fill(0.18, 0.26, 0.36);
    rect(-span * 0.44 + b * barW, span * 0.46 - h, barW * 0.75, h);
    fill(1.0, 0.72, 0.05);
    rect(-span * 0.44 + b * barW, span * 0.46 - h - span * 0.008, barW * 0.75, span * 0.006);
  }

  // 4. Primary waveform traces (Dual beam: CH1 White, CH2 Amber)
  const steps = 160;
  const nHarm = floor(harmonics);

  // CH2 Reference Waveform (Amber)
  stroke(1.0, 0.65, 0.05, 0.90);
  strokeWeight(2.8);
  noFill();
  beginShape();
  for (let i = 0; i <= steps; i++) {
    const frac = i / steps;
    const x = -span * 0.48 + frac * span * 0.96;
    const a = (frac * Math.PI * 2.0 * frequency * 0.5) - time * 1.8;
    const y = sin(a) * (span * 0.08 * sweep_gain) + span * 0.10;
    vertex(x, y);
  }
  endShape();

  // CH1 Main Signal Trace (Phosphor Cyan/White)
  stroke(0.96, 0.98, 1.0);
  strokeWeight(3.2);
  beginShape();
  for (let i = 0; i <= steps; i++) {
    const frac = i / steps;
    const x = -span * 0.48 + frac * span * 0.96;
    let y = 0;
    for (let h = 1; h <= nHarm; h++) {
      const a = (frac * Math.PI * 2.0 * frequency * h) - time * (2.0 + h * 0.5);
      y += (sin(a) / (h * 0.85)) * (span * 0.14 * sweep_gain);
    }
    vertex(x, y - span * 0.08);
  }
  endShape();

  // 5. Trigger marker and channel status badges
  const trigX = span * 0.28;
  stroke(1.0, 0.72, 0.05);
  strokeWeight(2.0);
  line(trigX - span * 0.03, -span * 0.08, trigX + span * 0.03, -span * 0.08);
  line(trigX, -span * 0.08 - span * 0.03, trigX, -span * 0.08 + span * 0.03);

  fill(1.0, 0.72, 0.05);
  noStroke();
  circle(trigX, -span * 0.08, span * 0.022);

  // Calibration corner boxes
  fill(0.96, 0.98, 1.0);
  rect(-span * 0.46, -span * 0.46, span * 0.08, span * 0.02);
  fill(1.0, 0.72, 0.05);
  rect(-span * 0.36, -span * 0.46, span * 0.04, span * 0.02);

  pop();
}
