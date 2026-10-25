// Analog CRT Vector Oscilloscope - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Dual-beam cathode ray vector oscilloscope with Lissajous harmonic curves, phosphor decay persistence, and drafting calibration graticule.
param("freq_x", 3.0, 1.0, 12.0);
param("freq_y", 4.0, 1.0, 12.0);
param("phase_speed", 0.5, 0.1, 2.0);
param("beam_intensity", 1.8, 0.5, 3.5);
param("graticule_alpha", 0.45, 0.1, 1.0);

function draw(t) {
  // Midnight ground with cathode CRT glass tint
  background(0.045, 0.055, 0.095);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const minDim = min(width, height);
  const time = t * phase_speed;

  push();
  translate(cx, cy);

  // 1. Oscilloscope calibration graticule grid
  noFill();
  strokeWeight(1.0);
  const gratAlpha = graticule_alpha * 0.25;
  stroke(0.2, 0.92, 0.58, gratAlpha);

  const gridDivs = 8;
  const gridW = minDim * 0.82;
  const gridH = minDim * 0.82;
  const stepX = gridW / gridDivs;
  const stepY = gridH / gridDivs;

  rect(-gridW * 0.5, -gridH * 0.5, gridW, gridH);

  for (let i = 1; i < gridDivs; i++) {
    const x = -gridW * 0.5 + i * stepX;
    const y = -gridH * 0.5 + i * stepY;
    // Major grid lines
    line(x, -gridH * 0.5, x, gridH * 0.5);
    line(-gridW * 0.5, y, gridW * 0.5, y);

    // Sub-division ticks on center axes
    stroke(0.2, 0.92, 0.58, gratAlpha * 2.0);
    line(x, -6, x, 6);
    line(-6, y, 6, y);
    stroke(0.2, 0.92, 0.58, gratAlpha);
  }

  // Circular graticule rings
  stroke(0.2, 0.92, 0.58, gratAlpha * 0.7);
  ellipse(0, 0, minDim * 0.4, minDim * 0.4);
  ellipse(0, 0, minDim * 0.75, minDim * 0.75);

  // 2. Phosphor Lissajous harmonic curves with multi-pass vector beam glow
  const rx = minDim * 0.35;
  const ry = minDim * 0.35;
  const steps = 360;

  // Outer diffuse bloom pass
  strokeWeight(3.5);
  stroke(0.2, 0.92, 0.58, 0.18 * beam_intensity);
  beginShape();
  for (let i = 0; i <= steps; i++) {
    const u = (i / steps) * 6.2831853;
    const aX = u * freq_x + time;
    const aY = u * freq_y + sin(time * 0.5) * 0.4;
    const px = cos(aX) * rx;
    const py = sin(aY) * ry;
    vertex(px, py);
  }
  endShape();

  // Secondary cyan beam pass
  strokeWeight(1.8);
  stroke(0.35, 0.85, 1.0, 0.45 * beam_intensity);
  beginShape();
  for (let i = 0; i <= steps; i++) {
    const u = (i / steps) * 6.2831853;
    const aX = u * freq_x + time;
    const aY = u * freq_y + sin(time * 0.5) * 0.4;
    const px = cos(aX) * rx;
    const py = sin(aY) * ry;
    vertex(px, py);
  }
  endShape();

  // Sharp electron beam core
  strokeWeight(1.0);
  stroke(0.92, 1.0, 0.96, 0.95);
  beginShape();
  for (let i = 0; i <= steps; i++) {
    const u = (i / steps) * 6.2831853;
    const aX = u * freq_x + time;
    const aY = u * freq_y + sin(time * 0.5) * 0.4;
    const px = cos(aX) * rx;
    const py = sin(aY) * ry;
    vertex(px, py);
  }
  endShape();

  // 3. Electron beam point scanning spot
  const spotAngle = (t * 4.0) % 6.2831853;
  const sx = cos(spotAngle * freq_x + time) * rx;
  const sy = sin(spotAngle * freq_y + sin(time * 0.5) * 0.4) * ry;

  noStroke();
  fill(0.2, 0.92, 0.58, 0.4);
  ellipse(sx, sy, 18, 18);
  fill(0.95, 1.0, 0.98, 0.95);
  ellipse(sx, sy, 6, 6);

  pop();
}
