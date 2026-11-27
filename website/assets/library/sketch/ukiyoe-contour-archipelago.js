// Ukiyo-e Contour Archipelago - Infinite Library (Sketch 2D)
param("contours", 16, 8, 28);
param("swell", 0.35, 0.05, 0.9);
param("shoals", 5, 2, 9);
param("drift", 0.14, 0.0, 0.5);

function fieldVal(nx, ny, t) {
  const d1 = sqrt((nx + 0.18) * (nx + 0.18) + (ny - 0.12) * (ny - 0.12));
  const d2 = sqrt((nx - 0.24) * (nx - 0.24) + (ny + 0.18) * (ny + 0.18));
  const d3 = sqrt((nx - 0.08) * (nx - 0.08) + (ny - 0.32) * (ny - 0.32));
  const undul = 0.08 * sin(nx * 7.0 + t * drift) * cos(ny * 6.0 - t * drift * 0.7);
  const ridge = exp(-d1 * 4.2) + 0.85 * exp(-d2 * 5.0) + 0.65 * exp(-d3 * 6.5) + undul;
  return ridge;
}

function draw(t) {
  // Warm aged washi paper (#E4DFCE)
  background(0.894, 0.875, 0.808);
  const w = width;
  const h = height;
  const margin = min(w, h) * 0.085;
  const span = min(w, h) - margin * 2.0;
  const x0 = (w - span) * 0.5;
  const y0 = (h - span) * 0.5;

  // Outer woodblock registration double border (sumi ink #191E24)
  noFill();
  stroke(0.098, 0.118, 0.141, 0.92);
  strokeWeight(2.6);
  rect(x0, y0, span, span);
  strokeWeight(1.0);
  rect(x0 + 8, y0 + 8, span - 16, span - 16);

  // Subtle ochre wood-grain horizon washes (#C6A76C)
  noStroke();
  for (let b = 0; b < 5; b++) {
    const by = y0 + 18 + b * (span * 0.045);
    fill(0.776, 0.655, 0.424, 0.18 - b * 0.025);
    rect(x0 + 10, by, span - 20, span * 0.026);
  }

  // Compass rose & rhumb lines radiating from maritime datum
  const rx = x0 + span * 0.72;
  const ry = y0 + span * 0.26;
  stroke(0.098, 0.118, 0.141, 0.18);
  strokeWeight(0.9);
  for (let i = 0; i < 16; i++) {
    const a = (i * TAU) / 16.0;
    line(rx, ry, rx + cos(a) * span * 0.85, ry + sin(a) * span * 0.85);
  }
  stroke(0.086, 0.216, 0.349, 0.45);
  strokeWeight(1.3);
  circle(rx, ry, span * 0.11);
  circle(rx, ry, span * 0.035);

  // Bathymetric contour lattice via marching-style cell segments
  const res = 54;
  const cw = (span - 24) / res;
  const ox = x0 + 12;
  const oy = y0 + 12;
  const levels = floor(contours);

  for (let iy = 0; iy < res; iy++) {
    for (let ix = 0; ix < res; ix++) {
      const nx = (ix / res) * 2.0 - 1.0;
      const ny = (iy / res) * 2.0 - 1.0;
      const v = fieldVal(nx, ny, t);
      const q = floor(v * levels);
      const frac = v * levels - q;

      const px = ox + ix * cw;
      const py = oy + iy * cw;

      // Coastal celadon shoal wash (#8FB5AC)
      if (v > 0.28 && v < 0.56) {
        noStroke();
        fill(0.561, 0.710, 0.675, 0.34);
        rect(px, py, cw + 0.8, cw + 0.8);
      } else if (v >= 0.56 && v < 0.82) {
        noStroke();
        fill(0.776, 0.655, 0.424, 0.32);
        rect(px, py, cw + 0.8, cw + 0.8);
      }

      // Crisp Prussian blue woodblock contour hatches (#163759)
      if (frac < 0.28) {
        const ang = atan2(ny + 0.12, nx + 0.18) + PI * 0.5 + sin(v * 12.0 + t * drift) * swell * 0.35;
        const dx = cos(ang) * cw * 0.65;
        const dy = sin(ang) * cw * 0.65;
        stroke(0.086, 0.216, 0.349, v > 0.45 ? 0.92 : 0.68);
        strokeWeight(v > 0.65 ? 2.1 : 1.35);
        line(px + cw * 0.5 - dx, py + cw * 0.5 - dy, px + cw * 0.5 + dx, py + cw * 0.5 + dy);
      }

      // Fine foam stipple dots along wave crests
      if ((ix * 7 + iy * 13) % floor(shoals + 4) === 0 && v > 0.18 && v < 0.42) {
        noStroke();
        fill(0.086, 0.216, 0.349, 0.48);
        circle(px + cw * 0.5, py + cw * 0.5, 2.2);
      }
    }
  }

  // Vermilion Edo cartouche & hanko seal (#B83A2A)
  const cx = x0 + 26;
  const cy = y0 + 26;
  fill(0.894, 0.875, 0.808, 0.94);
  stroke(0.098, 0.118, 0.141, 0.92);
  strokeWeight(1.8);
  rect(cx, cy, 44, 152);
  stroke(0.722, 0.227, 0.165, 0.95);
  strokeWeight(1.4);
  rect(cx + 5, cy + 5, 34, 142);

  // Inner geometric woodblock seal glyph
  noStroke();
  fill(0.722, 0.227, 0.165, 0.94);
  rect(cx + 10, cy + 114, 24, 24);
  fill(0.894, 0.875, 0.808, 0.95);
  rect(cx + 15, cy + 119, 14, 4);
  rect(cx + 20, cy + 119, 4, 14);
}
