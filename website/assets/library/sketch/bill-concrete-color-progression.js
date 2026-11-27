// Bill Concrete Color Progression - Infinite Library (Sketch 2D)
param("grid_n", 6, 4, 10);
param("twist", 0.28, 0.0, 0.8);
param("margin_pct", 0.11, 0.05, 0.20);
param("tempo", 0.18, 0.0, 0.6);

function draw(t) {
  // Warm archival bone paper (#F2EFE9)
  background(0.949, 0.937, 0.914);
  const w = width;
  const h = height;
  const side = min(w, h);
  const margin = side * margin_pct;
  const span = side - margin * 2.0;
  const x0 = (w - span) * 0.5;
  const y0 = (h - span) * 0.5;

  // Fine Swiss typographic registration frame
  stroke(0.094, 0.102, 0.125, 0.35);
  strokeWeight(1.2);
  noFill();
  rect(x0 - 12, y0 - 12, span + 24, span + 24);

  // Corner datum crosshairs
  for (let c = 0; c < 4; c++) {
    const cx = (c % 2 === 0) ? x0 - 22 : x0 + span + 22;
    const cy = (c < 2) ? y0 - 22 : y0 + span + 22;
    line(cx - 6, cy, cx + 6, cy);
    line(cx, cy - 6, cx, cy + 6);
  }

  const n = floor(grid_n);
  const cell = span / n;

  // Concrete Art palette (Red Granite, Cobalt, Ochre, Ink, Slate)
  const palR = [0.549, 0.106, 0.878, 0.094, 0.255, 0.784];
  const palG = [0.231, 0.286, 0.647, 0.102, 0.329, 0.365];
  const palB = [0.169, 0.541, 0.149, 0.125, 0.420, 0.251];

  for (let iy = 0; iy < n; iy++) {
    for (let ix = 0; ix < n; ix++) {
      const cx = x0 + (ix + 0.5) * cell;
      const cy = y0 + (iy + 0.5) * cell;
      const idx = (ix * 3 + iy * 5) % 6;
      const idx2 = (ix + iy * 2 + 1) % 6;
      const idx3 = (ix * 2 + iy + 3) % 6;

      // Outer cell square
      push();
      translate(cx, cy);
      noStroke();
      fill(palR[idx], palG[idx], palB[idx], 0.94);
      rect(-cell * 0.48, -cell * 0.48, cell * 0.96, cell * 0.96);

      // Rotated inner diamond progression
      const ang = twist * sin(t * tempo + (ix + iy) * 0.35) + ((ix + iy) % 2 === 0 ? 0.785398 : 0.0);
      rotate(ang);
      fill(palR[idx2], palG[idx2], palB[idx2], 0.92);
      const s2 = cell * 0.62;
      rect(-s2 * 0.5, -s2 * 0.5, s2, s2);

      // Innermost concrete core square
      rotate(-ang * 1.5);
      fill(0.949, 0.937, 0.914, 0.96);
      const s3 = cell * 0.28;
      rect(-s3 * 0.5, -s3 * 0.5, s3, s3);

      // Center accent dot
      fill(palR[idx3], palG[idx3], palB[idx3], 0.95);
      circle(0, 0, cell * 0.12);
      pop();

      // Hairline division
      stroke(0.094, 0.102, 0.125, 0.22);
      strokeWeight(1.0);
      noFill();
      rect(x0 + ix * cell, y0 + iy * cell, cell, cell);
    }
  }
}
