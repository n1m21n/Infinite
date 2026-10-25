// Ditherpunk Halftone Grid - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// 1-bit cybernetic ditherpunk lattice with ordered 8x8 Bayer matrix threshold clusters, bitmap scanline glitch, and retro terminal telemetry.
param("matrix_scale", 32.0, 12.0, 80.0);
param("dither_threshold", 0.52, 0.1, 0.9);
param("glitch_jitter", 18.0, 0.0, 60.0);
param("scanline_count", 48.0, 16.0, 96.0);
param("spin_rate", 0.35, 0.0, 2.0);

function draw(t) {
  // Midnight ground #151930
  background(0.082, 0.098, 0.188);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const minDim = min(width, height);
  const time = t * spin_rate;

  // 8x8 Bayer ordered dither matrix normalized [0, 63] / 64
  const bayer8 = [
     0/64, 32/64,  8/64, 40/64,  2/64, 34/64, 10/64, 42/64,
    48/64, 16/64, 56/64, 24/64, 50/64, 18/64, 58/64, 26/64,
    12/64, 44/64,  4/64, 36/64, 14/64, 46/64,  6/64, 38/64,
    60/64, 28/64, 52/64, 20/64, 62/64, 30/64, 54/64, 22/64,
     3/64, 35/64, 11/64, 43/64,  1/64, 33/64,  9/64, 41/64,
    51/64, 19/64, 59/64, 27/64, 49/64, 17/64, 57/64, 25/64,
    15/64, 47/64,  7/64, 39/64, 13/64, 45/64,  5/64, 37/64,
    63/64, 31/64, 55/64, 23/64, 61/64, 29/64, 53/64, 21/64
  ];

  // CRT scanline underlay with horizontal displacement glitch
  push();
  const numScans = floor(scanline_count);
  strokeWeight(1.0);
  for (let s = 0; s < numScans; s++) {
    const yScan = (s / numScans) * height;
    const scanGlitch = sin(s * 17.3 + time * 8.0);
    const xDisplace = scanGlitch > 0.85 ? sin(time * 30.0 + s) * glitch_jitter : 0;
    const alphaScan = 0.08 + 0.06 * sin(s * 4.0 - time * 3.0);
    stroke(0.655, 0.694, 1.0, alphaScan); // Signal trace
    line(xDisplace, yScan, width + xDisplace, yScan);
  }
  pop();

  // Procedural dithered cybernetic mandala ring
  const cellSize = minDim / matrix_scale;
  noStroke();

  for (let x = 0; x < width; x += cellSize) {
    for (let y = 0; y < height; y += cellSize) {
      const bx = floor(x / cellSize) % 8;
      const by = floor(y / cellSize) % 8;
      const bayerVal = bayer8[by * 8 + bx];

      const dx = x - cx;
      const dy = y - cy;
      const r = sqrt(dx * dx + dy * dy) / (minDim * 0.48);
      const angle = atan2(dy, dx);

      // Cybernetic telemetry wave fields
      const ringWave = sin(r * 18.0 - time * 4.0 + angle * 6.0) * 0.5 + 0.5;
      const coreIntensity = max(0.0, 1.0 - r * 1.1) * (0.6 + 0.4 * ringWave);
      const orbitRing = exp(-pow(r - 0.72, 2.0) * 80.0) * (sin(angle * 12.0 + time * 3.0) * 0.5 + 0.5);
      const lum = coreIntensity * 0.75 + orbitRing * 0.65;

      if (lum > bayerVal * dither_threshold) {
        // High contrast 1-bit terminal pixels
        if (r < 0.32) {
          fill(1.0, 0.42, 0.21, 0.95); // Core Ember
        } else if (r < 0.62) {
          fill(0.306, 0.949, 0.722, 0.9); // Emerald Cyber
        } else {
          fill(0.655, 0.694, 1.0, 0.85); // Signal Outer
        }
        rect(x, y, cellSize * 0.9, cellSize * 0.9);
      }
    }
  }

  // Precision central telemetry crosshair
  push();
  translate(cx, cy);
  rotate(time * 0.2);
  stroke(0.96, 0.94, 0.91, 0.8);
  strokeWeight(1.5);
  line(-minDim * 0.08, 0, minDim * 0.08, 0);
  line(0, -minDim * 0.08, 0, minDim * 0.08);
  noFill();
  ellipse(0, 0, minDim * 0.12, minDim * 0.12);
  ellipse(0, 0, minDim * 0.24, minDim * 0.24);
  pop();
}
