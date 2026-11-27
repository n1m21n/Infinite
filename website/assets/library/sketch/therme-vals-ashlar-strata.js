// Therme Vals Ashlar Strata - Infinite Library (Sketch 2D)
param("courses", 18, 10, 28);
param("slit_width", 0.032, 0.012, 0.07);
param("drift", 0.22, 0.0, 0.6);

function draw(t) {
  // Deep wet quartzite charcoal (#1C2024)
  background(0.110, 0.125, 0.141);
  const w = width;
  const h = height;
  const side = min(w, h);
  const pad = side * 0.065;
  const x0 = (w - side) * 0.5 + pad;
  const y0 = (h - side) * 0.5 + pad;
  const span = side - pad * 2.0;

  const nC = floor(courses);
  const rowH = span / nC;
  const ph = t * drift;

  // 15 cm modular Valser quartzite ashlar courses (3-height rhythm)
  noStroke();
  for (let r = 0; r < nC; r++) {
    const y = y0 + r * rowH;
    const tier = r % 3;
    const hCourse = rowH * (tier === 0 ? 0.86 : tier === 1 ? 0.90 : 0.82);
    const cols = 3 + ((r * 7) % 4);
    let curX = x0;
    for (let c = 0; c < cols; c++) {
      const wFrac = (c === cols - 1)
        ? (x0 + span - curX)
        : (span / cols) * (0.72 + 0.48 * (0.5 + 0.5 * sin(r * 3.7 + c * 5.3)));
      const segW = min(wFrac, x0 + span - curX);
      const tone = 0.22 + 0.14 * (0.5 + 0.5 * sin(r * 2.1 + c * 4.3)) + (tier === 1 ? 0.06 : 0.0);
      const isPoolBand = r >= floor(nC * 0.68);
      if (isPoolBand) {
        // Mineral thermal water band (#3D7880)
        const rip = 0.04 * sin(ph * 2.0 + r * 1.3 + c);
        fill(0.18 + rip, 0.36 + rip * 1.2, 0.42 + rip * 1.4, 0.92);
      } else {
        // Valser gneiss slate (#4A5157 - #9AA1A6)
        fill(tone, tone * 1.05, tone * 1.11, 0.96);
      }
      rect(curX + 1.5, y + 1.5, max(1.0, segW - 3.0), max(1.0, hCourse - 3.0));

      // Fine mineral quartz veining inside each slab
      if (!isPoolBand && segW > 24.0) {
        stroke(0.604, 0.631, 0.651, 0.22);
        strokeWeight(1.0 * (side / 1080.0));
        const vy = y + hCourse * (0.35 + 0.3 * sin(r + c * 2.7));
        line(curX + 6.0, vy, curX + segW - 6.0, vy + sin(r * 1.9 + c) * 2.2);
        noStroke();
      }
      curX += segW;
    }
  }

  // Narrow 8 cm clerestory light-slit shafts (#D9CBB5)
  const sW = span * slit_width;
  const slits = [0.28, 0.69];
  for (let s = 0; s < slits.length; s++) {
    const sx = x0 + span * (slits[s] + 0.015 * sin(ph + s * 1.7));
    for (let g = 0; g < 4; g++) {
      const alpha = 0.26 - g * 0.055;
      const expand = g * 7.0 * (side / 1080.0);
      fill(0.851, 0.796, 0.710, alpha);
      rect(sx - expand, y0, sW + expand * 2.0, span * (0.68 + s * 0.14));
    }
    // Crisp bright clerestory core
    fill(0.94, 0.90, 0.82, 0.78);
    rect(sx + sW * 0.25, y0, sW * 0.5, span * 0.66);
  }

  // Calm water-surface reflection bars in lower basin
  for (let k = 0; k < 7; k++) {
    const ry = y0 + span * (0.71 + k * 0.036);
    const rw = span * (0.28 - k * 0.025);
    const rx = x0 + span * 0.28 - rw * 0.25 + sin(ph * 1.6 + k) * 8.0;
    fill(0.851, 0.796, 0.710, 0.22 - k * 0.02);
    rect(rx, ry, rw, 2.2 * (side / 1080.0));
  }
}
