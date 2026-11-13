// Ditherpunk Atkinson Cluster Matrix - Infinite Library (Sketch 2D)
param("rings", 12.0, 6.0, 24.0);
param("sectors", 24.0, 12.0, 48.0);
param("line_weight", 1.5, 0.8, 3.0);
param("contrast", 1.15, 0.8, 1.8);

function draw(t) {
  // Midnight charcoal canvas (#0d0e15)
  background(0.05, 0.055, 0.08);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const maxR = min(width, height) * 0.42;

  // Outer technical coordinate framing
  stroke(0.85, 0.88, 0.94);
  strokeWeight(line_weight * 1.2);
  noFill();
  rect(48, 48, width - 96, height - 96);
  rect(56, 56, width - 112, height - 112);

  // Border tick marks
  stroke(0.92, 0.94, 0.98);
  strokeWeight(1.0);
  for (let x = 80; x <= width - 80; x += 36) {
    line(x, 48, x, 54);
    line(x, height - 48, x, height - 54);
  }
  for (let y = 80; y <= height - 80; y += 36) {
    line(48, y, 54, y);
    line(width - 48, y, width - 54, y);
  }

  // Radial sector ray spokes (dither matrix radial threshold)
  const numSectors = floor(sectors);
  for (let i = 0; i < numSectors; i++) {
    const angle = (i / numSectors) * Math.PI * 2.0;
    const cosA = Math.cos(angle);
    const sinA = Math.sin(angle);
    const isMajor = (i % 4 === 0);
    const r1 = isMajor ? maxR * 0.15 : maxR * 0.35;
    const r2 = isMajor ? maxR * 1.02 : maxR * 0.95;

    stroke(isMajor ? 0.95 : 0.45, isMajor ? 0.95 : 0.52, isMajor ? 0.98 : 0.65, isMajor ? 0.85 : 0.4);
    strokeWeight(isMajor ? line_weight : 1.0);
    line(cx + cosA * r1, cy + sinA * r1, cx + cosA * r2, cy + sinA * r2);
  }

  // Concentric dithered ring boundaries and crosshatch arcs
  const numRings = floor(rings);
  for (let r = 1; r <= numRings; r++) {
    const frac = r / numRings;
    const rad = frac * maxR;

    noFill();
    stroke(0.72, 0.78, 0.88, 0.65);
    strokeWeight(line_weight);
    ellipse(cx, cy, rad * 2.0, rad * 2.0);

    // Stepped crosshatch chords
    if (r % 2 === 0) {
      stroke(0.88, 0.92, 0.98, 0.5);
      strokeWeight(1.0);
      const chordStep = 18;
      for (let k = -rad; k <= rad; k += chordStep) {
        const halfSpan = Math.sqrt(Math.max(0, rad * rad - k * k));
        line(cx - halfSpan, cy + k, cx + halfSpan, cy + k);
      }
    }
  }

  // Center crosshairs
  stroke(1.0, 1.0, 1.0);
  strokeWeight(1.5);
  line(cx - 32, cy, cx + 32, cy);
  line(cx, cy - 32, cx, cy + 32);
}
