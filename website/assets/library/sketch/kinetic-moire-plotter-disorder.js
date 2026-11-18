// Kinetic Moire Plotter Disorder - Infinite Library (Sketch 2D)
param("grid", 8, 5, 12);
param("rings", 6, 3, 9);
param("disorder", 0.24, 0.05, 0.5);
param("drift", 0.4, 0.0, 1.5);

function draw(t) {
  // Archival ivory paper (#F4F1EA)
  background(0.957, 0.945, 0.918);
  const w = width;
  const h = height;
  const margin = w * 0.095;
  const nGrid = floor(grid);
  const nRings = floor(rings);
  const cell = (w - margin * 2.0) / nGrid;

  // Plotter corner registration crosshairs
  noFill();
  stroke(0.067, 0.075, 0.094, 0.55);
  strokeWeight(1.2);
  const cm = margin * 0.55;
  const cl = 14;
  line(cm - cl, cm, cm + cl, cm); line(cm, cm - cl, cm, cm + cl);
  line(w - cm - cl, cm, w - cm + cl, cm); line(w - cm, cm - cl, w - cm, cm + cl);
  line(cm - cl, h - cm, cm + cl, h - cm); line(cm, h - cm - cl, cm, h - cm + cl);
  line(w - cm - cl, h - cm, w - cm + cl, h - cm); line(w - cm, h - cm - cl, w - cm, h - cm + cl);

  // Nested concentric squares with progressive radial disorder
  for (let gx = 0; gx < nGrid; gx++) {
    for (let gy = 0; gy < nGrid; gy++) {
      const cx = margin + (gx + 0.5) * cell;
      const cy = margin + (gy + 0.5) * cell;
      const dxNorm = (gx + 0.5) / nGrid - 0.5;
      const dyNorm = (gy + 0.5) / nGrid - 0.5;
      const radial = sqrt(dxNorm * dxNorm + dyNorm * dyNorm) * 1.414;
      const cellChaos = disorder * (0.18 + 0.82 * radial);

      for (let r = 0; r < nRings; r++) {
        const frac = (r + 1) / nRings;
        const half = cell * 0.44 * frac;
        const seed = gx * 31.7 + gy * 17.3 + r * 7.1;

        // Dual-ink accent selection
        const isVermilion = ((gx * 3 + gy * 5 + r) % 11 === 0);
        const isBlue = ((gx * 7 + gy * 2 + r) % 9 === 0) && !isVermilion;

        if (isVermilion) {
          stroke(0.851, 0.220, 0.118, 0.88);
          strokeWeight(1.8);
        } else if (isBlue) {
          stroke(0.106, 0.286, 0.541, 0.82);
          strokeWeight(1.6);
        } else {
          stroke(0.067, 0.075, 0.094, 0.84);
          strokeWeight(1.35);
        }

        beginShape();
        for (let c = 0; c <= 4; c++) {
          const ci = c % 4;
          const sx = (ci === 0 || ci === 3) ? -1 : 1;
          const sy = (ci < 2) ? -1 : 1;
          const jx = sin(seed + ci * 13.7 + t * drift) * half * cellChaos;
          const jy = cos(seed * 1.3 + ci * 9.2 - t * drift * 0.8) * half * cellChaos;
          vertex(cx + sx * half + jx, cy + sy * half + jy);
        }
        endShape();
      }
    }
  }
}
