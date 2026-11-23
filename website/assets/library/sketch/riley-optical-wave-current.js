// Riley Optical Wave Current - Infinite Library (Sketch 2D)
param("ribbons", 36, 18, 64);
param("crest_amp", 0.085, 0.02, 0.16);
param("phase_drift", 0.35, 0.0, 1.2);
param("accent_mix", 0.28, 0.0, 0.8);

function draw(t) {
  // Warm archival rag paper (#F4EFE6)
  background(0.957, 0.937, 0.902);
  const w = width;
  const h = height;
  const margin = w * 0.09;
  const gw = w - margin * 2.0;
  const gh = h - margin * 2.0;

  const nR = floor(ribbons);
  const stepX = gw / nR;
  const segs = 72;

  // Fine drafting border
  noFill();
  stroke(0.067, 0.067, 0.078, 0.35);
  strokeWeight(1.2);
  rect(margin, margin, gw, gh);

  // Undulating parallel Op-Art wave bands
  for (let i = 0; i < nR; i++) {
    const u = i / max(1, nR - 1);
    const baseX = margin + (i + 0.5) * stepX;

    // Alternating ink and chromatic harmonic ribbons
    if (i % 9 === 4 && accent_mix > 0.1) {
      stroke(0.851, 0.478, 0.169, 0.94); // Burnt ochre (#D97A2B)
    } else if (i % 9 === 7 && accent_mix > 0.15) {
      stroke(0.118, 0.294, 0.541, 0.92); // Cobalt (#1E4B8A)
    } else {
      stroke(0.067, 0.067, 0.078, 0.96); // Deep ink (#111114)
    }

    const wMod = 0.5 + 0.5 * sin(u * PI * 3.0 + t * phase_drift * 0.6);
    strokeWeight(max(1.4, stepX * (0.28 + 0.34 * wMod)));

    beginShape();
    for (let s = 0; s <= segs; s++) {
      const v = s / segs;
      const y = margin + v * gh;
      // Compression wave creates the physiological Op-Art shimmer
      const env = sin(v * PI);
      const wave1 = sin(v * TAU * 2.2 - u * 4.5 + t * phase_drift);
      const wave2 = 0.42 * sin(v * TAU * 4.4 + u * 6.0 - t * phase_drift * 0.7);
      const dx = (wave1 + wave2) * w * crest_amp * env;
      vertex(baseX + dx, y);
    }
    endShape();
  }

  // Subtle horizontal counter-ticks along outer frame
  stroke(0.067, 0.067, 0.078, 0.45);
  strokeWeight(1.0);
  for (let k = 0; k <= 12; k++) {
    const ty = margin + (k / 12.0) * gh;
    line(margin - 10, ty, margin - 3, ty);
    line(margin + gw + 3, ty, margin + gw + 10, ty);
  }
}
