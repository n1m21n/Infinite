// Vasarely Sophia Contour Shift - Infinite Library (Sketch 2D)
param("lines", 74, 40, 120);
param("shiftAmp", 18.0, 4.0, 36.0);
param("panels", 3, 2, 4);
param("drift", 0.14, 0.0, 0.5);

function clamp(v, lo, hi) {
  return min(max(v, lo), hi);
}

function draw(t) {
  // Brutalist raw concrete ground (#8E8B84)
  background(0.557, 0.545, 0.518);
  const w = width;
  const h = height;
  const margin = min(w, h) * 0.085;
  const panelGap = 22.0;
  const totalW = w - margin * 2.0;
  const panelH = h - margin * 2.0;
  const pCount = floor(panels);
  const panelW = (totalW - panelGap * (pCount - 1)) / pCount;

  // Concrete formwork tie-rod dots
  noStroke();
  fill(0.310, 0.302, 0.282, 0.65);
  for (let r = 0; r < 4; r++) {
    for (let c = 0; c < 4; c++) {
      const tx = margin * 0.5 + (c / 3.0) * (w - margin);
      const ty = margin * 0.5 + (r / 3.0) * (h - margin);
      circle(tx, ty, 3.2);
    }
  }

  const lineCount = floor(lines);
  const segs = 48;
  const phase = t * drift;

  for (let p = 0; p < pCount; p++) {
    const px = margin + p * (panelW + panelGap);
    const py = margin;

    // Ivory relief plaque (#F2F1EC)
    noStroke();
    fill(0.949, 0.945, 0.925);
    rect(px, py, panelW, panelH);

    // Subtle recessed shadow border
    noFill();
    stroke(0.24, 0.235, 0.22, 0.85);
    strokeWeight(2.2);
    rect(px, py, panelW, panelH);

    // Kinetic stepped contour lines (#141517)
    stroke(0.078, 0.082, 0.090);
    strokeWeight(1.55);

    for (let i = 1; i < lineCount; i++) {
      const v = i / lineCount;
      const baseY = py + v * panelH;
      let prevX = px + 6.0;
      let prevY = baseY;

      for (let s = 1; s <= segs; s++) {
        const u = s / segs;
        const x = px + 6.0 + u * (panelW - 12.0);
        let disp = 0.0;

        if (p % 3 === 0) {
          // Circular stepped crater + diagonal shear
          const dx = (u - 0.52) * 1.4;
          const dy = (v - 0.44) * 2.2;
          const dist = sqrt(dx * dx + dy * dy);
          if (dist < 0.56) {
            const stepR = floor((0.56 - dist) * 7.0) / 7.0;
            disp = -stepR * shiftAmp * (0.85 + 0.25 * sin(phase * 1.3));
          }
          if (u + v * 0.6 > 0.92 && u + v * 0.6 < 1.25) {
            disp += shiftAmp * 0.45;
          }
        } else if (p % 3 === 1) {
          // Vertical zigzag frequency-modulated corridor
          const band = abs(u - 0.5 + 0.14 * sin(v * PI * 3.0 + phase));
          if (band < 0.22) {
            const q = floor((0.22 - band) * 12.0) / 12.0;
            disp = q * shiftAmp * (i % 2 === 0 ? 1.15 : -0.95);
          }
        } else {
          // Dual-chevron diamond contour shift
          const d1 = abs(u - 0.5) + abs(v - 0.5) * 0.65;
          if (d1 < 0.45 && d1 > 0.12) {
            disp = (u < 0.5 ? -1.0 : 1.0) * shiftAmp * 0.65 * sin(d1 * 14.0 + phase);
          }
        }

        const curY = clamp(baseY + disp, py + 4.0, py + panelH - 4.0);
        line(prevX, prevY, x, curY);
        prevX = x;
        prevY = curY;
      }
    }
  }
}