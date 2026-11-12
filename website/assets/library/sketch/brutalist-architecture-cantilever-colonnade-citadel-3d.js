// Brutalist Architecture Cantilever Colonnade Citadel 3D - Infinite Library (Sketch 3D)
param("spin", 0.30, 0.1, 1.2);
param("scale_factor", 1.15, 0.5, 2.2);
param("tiers", 5.0, 3.0, 8.0);
param("column_density", 8.0, 4.0, 16.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.52 + sin(time * 0.18) * 0.05);
  rotateY(time * 0.32 + 0.85);

  const nTiers = floor(tiers);
  const nCols = floor(column_density);

  for (let k = 0; k < nTiers; k++) {
    const yOffset = (k - nTiers * 0.5) * 0.32 * scale_factor;
    const tierWidth = (1.45 - k * 0.18) * scale_factor;
    const tierDepth = (1.45 - k * 0.18) * scale_factor;

    push();
    translate(0, yOffset, 0);

    if (k % 2 === 0) {
      fill(0.84, 0.83, 0.80); // Pale cast concrete (#d6d4cc)
    } else {
      fill(0.48, 0.48, 0.46); // Raw aggregate grey (#7a7a75)
    }
    box(tierWidth, 0.07 * scale_factor, tierDepth);

    if (k < nTiers - 1) {
      const colHeight = 0.25 * scale_factor;
      for (let c = 0; c < nCols; c++) {
        const frac = c / nCols;
        const colAngle = frac * Math.PI * 2.0;
        const colRadius = (tierWidth * 0.42);

        push();
        translate(Math.cos(colAngle) * colRadius, colHeight * 0.5, Math.sin(colAngle) * colRadius);
        fill(0.26, 0.26, 0.26); // Deep shadow concrete pillar (#424242)
        box(0.08 * scale_factor, colHeight, 0.08 * scale_factor);
        pop();
      }

      push();
      translate(0, colHeight * 0.5, 0);
      fill(0.38, 0.38, 0.37);
      box(tierWidth * 0.45, colHeight, tierDepth * 0.45);
      pop();
    }
    pop();
  }

  for (let b = 0; b < 4; b++) {
    const angle = b * Math.PI * 0.5 + 0.3;
    push();
    rotateY(angle);
    translate(0.85 * scale_factor, 0.05 * scale_factor, 0);
    fill(0.88, 0.87, 0.84);
    box(0.45 * scale_factor, 0.06 * scale_factor, 0.32 * scale_factor);
    pop();
  }

  pop();
}
