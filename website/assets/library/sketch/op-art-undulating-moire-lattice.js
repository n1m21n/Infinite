// Op-Art Undulating Moiré Lattice - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Optical illusion kinetic moiré lattice with undulating sine-phase contour lines, high-frequency grid interference, and visceral retinal vibration.
param("grid_density", 52.0, 24.0, 80.0);
param("undulation_amp", 36.0, 10.0, 70.0);
param("phase_speed", 0.5, 0.1, 1.5);
param("retinal_contrast", 0.95, 0.5, 1.0);

function draw(t) {
  // Alabaster white paper ground
  background(0.965, 0.965, 0.965);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.90;

  push();
  translate(cx, cy);

  // Outer framing rule
  stroke(0.04, 0.04, 0.05);
  strokeWeight(2.5);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  // Inner margin border
  stroke(0.15, 0.15, 0.18, 0.5);
  strokeWeight(1.0);
  rect(-span * 0.46, -span * 0.46, span * 0.92, span * 0.92);

  const lines = floor(grid_density);
  const time = t * phase_speed;

  // Primary undulating horizontal contour lines
  stroke(0.04, 0.04, 0.05, retinal_contrast);
  noFill();

  const wHalf = span * 0.44;
  for (let i = -lines / 2; i <= lines / 2; i++) {
    const yNorm = i / (lines * 0.5);
    const yBase = yNorm * (span * 0.44);
    const thick = 1.2 + 2.4 * pow(cos(yNorm * Math.PI * 0.5), 2.0);
    strokeWeight(thick);

    beginShape();
    for (let x = -wHalf; x <= wHalf; x += 14) {
      const d1 = dist(x, yBase, -span * 0.20, 0);
      const d2 = dist(x, yBase, span * 0.20, 0);
      const bulge1 = exp(-d1 * 0.006) * sin(d1 * 0.04 - time * 2.0) * undulation_amp;
      const bulge2 = exp(-d2 * 0.006) * cos(d2 * 0.04 + time * 1.8) * undulation_amp;
      const yWarp = yBase + bulge1 - bulge2;
      vertex(x, yWarp);
    }
    endShape();
  }

  // Secondary intersecting vertical interference grid causing moiré diamonds
  const vLines = floor(lines * 0.65);
  for (let j = -vLines / 2; j <= vLines / 2; j++) {
    const xNorm = j / (vLines * 0.5);
    const xBase = xNorm * (span * 0.44);
    const vThick = j % 2 === 0 ? 1.8 : 0.8;
    stroke(0.04, 0.04, 0.05, 0.75 * retinal_contrast);
    strokeWeight(vThick);

    beginShape();
    for (let y = -span * 0.44; y <= span * 0.44; y += 18) {
      const dCenter = dist(xBase, y, 0, 0);
      const xBulge = exp(-dCenter * 0.005) * sin(y * 0.03 + time * 1.5) * (undulation_amp * 0.6);
      vertex(xBase + xBulge, y);
    }
    endShape();
  }

  // Central focal oval aperture with inverted concentric rings
  push();
  noFill();
  stroke(0.04, 0.04, 0.05);
  for (let k = 1; k <= 9; k++) {
    strokeWeight(k % 2 === 0 ? 2.8 : 1.4);
    const rw = span * (0.04 * k);
    const rh = rw * 0.72;
    ellipse(0, 0, rw * 2.0, rh * 2.0);
  }
  pop();

  // Corner registration optical target ticks
  stroke(0.04, 0.04, 0.05);
  strokeWeight(2.0);
  const cr = span * 0.46;
  for (let signX of [-1, 1]) {
    for (let signY of [-1, 1]) {
      const kx = signX * cr;
      const ky = signY * cr;
      line(kx, ky, kx - signX * 22, ky);
      line(kx, ky, kx, ky - signY * 22);
    }
  }

  pop();
}
