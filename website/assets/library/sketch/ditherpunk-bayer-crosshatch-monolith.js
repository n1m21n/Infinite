// Ditherpunk Bayer Crosshatch Monolith - Infinite Library (Sketch 2D)
param("tiers", 5.0, 3.0, 8.0);
param("line_weight", 1.4, 0.8, 3.0);
param("hatch_density", 14.0, 8.0, 24.0);
param("contrast", 1.2, 0.8, 1.8);

function draw(t) {
  // Midnight ground (#0c0d14)
  background(0.047, 0.051, 0.078);

  const cx = width * 0.5;
  const cy = height * 0.52;
  const w = width;
  const h = height;

  // 1. Framing graticule
  stroke(0.82, 0.86, 0.94);
  strokeWeight(line_weight * 1.2);
  noFill();
  rect(48, 48, w - 96, h - 96);
  rect(56, 56, w - 112, h - 112);

  // Calibration tick marks
  stroke(0.90, 0.92, 0.96);
  strokeWeight(1.0);
  for (let x = 80; x <= w - 80; x += 36) {
    line(x, 48, x, 54);
    line(x, h - 48, x, h - 54);
  }
  for (let y = 80; y <= h - 80; y += 36) {
    line(48, y, 54, y);
    line(w - 48, y, w - 54, y);
  }

  // 2. Stepped Isometric Monolith Tiers
  const nTiers = floor(tiers);
  const isoAngle = Math.PI / 6.0; // 30 degrees
  const cosIso = Math.cos(isoAngle);
  const sinIso = Math.sin(isoAngle);

  for (let i = nTiers - 1; i >= 0; i--) {
    const frac = i / nTiers;
    const tierW = (180.0 + (nTiers - 1 - i) * 60.0);
    const tierH = 46.0;
    const baseY = cy + (i - nTiers * 0.5) * 68.0;

    // Center apex of isometric cube
    const px = cx;
    const py = baseY;

    // Vertices of top isometric diamond
    const xTop = px;
    const yTop = py - tierW * 0.5 * sinIso;
    const xRight = px + tierW * 0.5 * cosIso;
    const yRight = py;
    const xBottom = px;
    const yBottom = py + tierW * 0.5 * sinIso;
    const xLeft = px - tierW * 0.5 * cosIso;
    const yLeft = py;

    // Top face ordered crosshatch (Bayer level 1: high reflectance)
    stroke(0.94, 0.96, 0.98);
    strokeWeight(line_weight);
    line(xTop, yTop, xRight, yRight);
    line(xRight, yRight, xBottom, yBottom);
    line(xBottom, yBottom, xLeft, yLeft);
    line(xLeft, yLeft, xTop, yTop);

    const step = 16.0;
    for (let d = -tierW * 0.4; d <= tierW * 0.4; d += step) {
      line(px + d * cosIso, py + d * sinIso - tierW * 0.25 * sinIso,
           px + d * cosIso, py + d * sinIso + tierW * 0.25 * sinIso);
    }

    // Left vertical face (Bayer level 2: medium hatch)
    stroke(0.68, 0.74, 0.86);
    strokeWeight(1.0);
    line(xLeft, yLeft, xLeft, yLeft + tierH);
    line(xBottom, yBottom, xBottom, yBottom + tierH);
    line(xLeft, yLeft + tierH, xBottom, yBottom + tierH);

    for (let k = 4.0; k < tierH; k += 8.0) {
      line(xLeft, yLeft + k, xBottom, yBottom + k);
    }

    // Right vertical face (Bayer level 3: dense crosshatch)
    stroke(0.42, 0.48, 0.62);
    line(xRight, yRight, xRight, yRight + tierH);
    line(xBottom, yBottom + tierH, xRight, yRight + tierH);

    for (let k = 3.0; k < tierH; k += 6.0) {
      line(xBottom, yBottom + k, xRight, yRight + k);
      line(xBottom + (xRight - xBottom) * (k / tierH), yBottom + k,
           xBottom + (xRight - xBottom) * (k / tierH), yBottom + tierH);
    }
  }

  // Datum indicator
  stroke(1.0, 1.0, 1.0);
  strokeWeight(1.2);
  line(cx - 24, h - 32, cx + 24, h - 32);
  line(cx, h - 44, cx, h - 20);
}
