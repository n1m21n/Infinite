// Vasarely Plastic Sphere Lattice - Infinite Library (Sketch 2D)
param("cells", 18, 10, 28);
param("bulge", 0.62, 0.15, 0.95);
param("pulse_speed", 0.30, 0.0, 1.0);
param("accent_shift", 0.45, 0.0, 1.0);

function warpPt(nx, ny, t) {
  const r = sqrt(nx * nx + ny * ny);
  const rad = 0.88;
  if (r < rad && r > 0.0001) {
    const k = r / rad;
    const bulgeAmt = bulge * (0.85 + 0.15 * sin(t * pulse_speed * 2.0));
    const scale = 1.0 + bulgeAmt * (1.0 - k * k) * 0.58;
    return [nx * scale, ny * scale, 1.0 - k * k];
  }
  return [nx, ny, 0.0];
}

function draw(t) {
  // Warm slate-ivory architectural frame (#1D222B)
  background(0.115, 0.133, 0.169);
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const span = min(w, h) * 0.46;

  const n = floor(cells);
  const step = 2.0 / n;

  for (let iy = 0; iy < n; iy++) {
    for (let ix = 0; ix < n; ix++) {
      const x0 = -1.0 + ix * step;
      const y0 = -1.0 + iy * step;
      const x1 = x0 + step;
      const y1 = y0 + step;

      const p00 = warpPt(x0, y0, t);
      const p10 = warpPt(x1, y0, t);
      const p11 = warpPt(x1, y1, t);
      const p01 = warpPt(x0, y1, t);

      const mx = (x0 + x1) * 0.5;
      const my = (y0 + y1) * 0.5;
      const distC = sqrt(mx * mx + my * my);
      const dome = max(0.0, 1.0 - distC / 1.15);
      const parity = (ix + iy) % 2;

      if (parity === 0) {
        // Warm ivory to burnt ochre lift on the spherical bulge
        const luma = 0.52 + 0.46 * pow(dome, 0.7);
        const rCol = min(1.0, luma * (0.98 + 0.04 * accent_shift));
        const gCol = min(1.0, luma * (0.90 - 0.18 * dome * accent_shift));
        const bCol = min(1.0, luma * (0.78 - 0.36 * dome * accent_shift));
        fill(rCol, gCol, bCol, 0.98);
      } else {
        // Deep ink to cobalt blue modulation
        const cb = pow(dome, 0.85);
        fill(0.10 + 0.08 * cb, 0.18 + 0.24 * cb, 0.34 + 0.48 * cb, 0.98);
      }

      stroke(0.067, 0.067, 0.078, 0.85);
      strokeWeight(1.4);

      beginShape();
      vertex(cx + p00[0] * span, cy + p00[1] * span);
      vertex(cx + p10[0] * span, cy + p10[1] * span);
      vertex(cx + p11[0] * span, cy + p11[1] * span);
      vertex(cx + p01[0] * span, cy + p01[1] * span);
      endShape();

      // Inner plastic-alphabet circle on dome cells
      if (dome > 0.12) {
        const icx = cx + (p00[0] + p10[0] + p11[0] + p01[0]) * 0.25 * span;
        const icy = cy + (p00[1] + p10[1] + p11[1] + p01[1]) * 0.25 * span;
        const radCell = (span / n) * (0.28 + 0.30 * dome);
        noStroke();
        if (parity === 0) {
          fill(0.118, 0.294, 0.541, 0.94);
        } else {
          fill(0.957, 0.937, 0.902, 0.94);
        }
        ellipse(icx, icy, radCell * 2.0, radCell * 2.0);
      }
    }
  }
}
