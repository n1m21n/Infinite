// Data Landscape Pulsar Elevation Profile - Infinite Library (Sketch 2D)
param("ridges", 46.0, 24.0, 72.0);
param("peak_height", 85.0, 40.0, 160.0);
param("noise_scale", 1.25, 0.5, 2.5);
param("glow_intensity", 0.90, 0.4, 1.0);

function clamp(x, a, b) {
  return Math.max(a, Math.min(b, x));
}

function draw(t) {
  const w = width;
  const h = height;

  // 1. Midnight Oceanic Gradient Background (#0b132b -> #1c2541)
  const bgSteps = 48;
  for (let i = 0; i < bgSteps; i++) {
    const frac = i / bgSteps;
    const y1 = frac * h;
    const y2 = ((i + 1) / bgSteps) * h;
    const r = 0.043 + frac * 0.067;
    const g = 0.075 + frac * 0.070;
    const b = 0.169 + frac * 0.086;
    fill(r, g, b);
    noStroke();
    rect(0, y1, w, y2 - y1 + 1.0);
  }

  // 2. Telemetry and Survey Coordinate Frame
  stroke(0.357, 0.753, 0.745, 0.22 * glow_intensity);
  strokeWeight(0.9);
  rect(48, 48, w - 96, h - 96);
  rect(56, 56, w - 112, h - 112);

  // Calibration tick marks on margins
  stroke(1.0, 1.0, 1.0, 0.55 * glow_intensity);
  strokeWeight(1.2);
  for (let x = 80; x <= w - 80; x += 40) {
    line(x, 48, x, 53);
    line(x, h - 48, x, h - 53);
  }
  for (let y = 80; y <= h - 80; y += 40) {
    line(48, y, 53, y);
    line(w - 48, y, w - 53, y);
  }

  // 3. Stacked Pulsar Ridge Profiles with Hidden-Line Occlusion
  const numRidges = floor(ridges);
  const startY = 120.0;
  const endY = h - 120.0;
  const ridgeSpacing = (endY - startY) / numRidges;

  const leftX = 100.0;
  const rightX = w - 100.0;
  const numSamples = 130;
  const dx = (rightX - leftX) / (numSamples - 1);

  for (let r = 0; r < numRidges; r++) {
    const fracR = r / numRidges;
    const baseY = startY + r * ridgeSpacing;

    // Build the elevation profile points for this ridge
    const pts = [];
    for (let j = 0; j < numSamples; j++) {
      const curX = leftX + j * dx;
      const nx = (curX - (w * 0.5)) / 190.0;

      // Dual Gaussian bell envelopes centered around the midsection
      const env1 = exp(-nx * nx * 1.6);
      const env2 = exp(-(nx - 0.45) * (nx - 0.45) * 4.5) * 0.65;
      const env3 = exp(-(nx + 0.50) * (nx + 0.50) * 3.8) * 0.55;
      const combinedEnv = env1 + env2 + env3;

      // Multi-frequency harmonic perturbation
      const s1 = sin(nx * 7.5 + r * 0.48 + t * 0.75);
      const s2 = cos(nx * 15.0 - r * 0.32 + t * 1.1) * 0.42;
      const s3 = sin(nx * 32.0 + r * 1.1 + t * 1.8) * 0.22;
      const wave = (s1 + s2 + s3) * combinedEnv * peak_height * noise_scale;

      const curY = baseY - abs(wave);
      pts.push([curX, curY]);
    }

    // A. Occlusion Under-Polygon (Mask background so front lines occlude rear lines)
    fill(0.043, 0.075, 0.169);
    noStroke();
    beginShape();
    vertex(leftX, baseY + ridgeSpacing * 1.5);
    vertex(leftX, pts[0][1]);
    for (let j = 0; j < pts.length; j++) {
      vertex(pts[j][0], pts[j][1]);
    }
    vertex(rightX, pts[pts.length - 1][1]);
    vertex(rightX, baseY + ridgeSpacing * 1.5);
    endShape(CLOSE);

    // B. Phosphorescent Vector Contour Line
    // Color transitions from deep cyan at base to crisp white at crest peaks
    for (let j = 0; j < pts.length - 1; j++) {
      const p1 = pts[j];
      const p2 = pts[j + 1];
      const elevFrac = clamp((baseY - (p1[1] + p2[1]) * 0.5) / peak_height, 0.0, 1.0);

      const colR = 0.357 + elevFrac * 0.643;
      const colG = 0.753 + elevFrac * 0.247;
      const colB = 0.745 + elevFrac * 0.255;
      const alpha = (0.70 + elevFrac * 0.30) * glow_intensity;

      stroke(colR, colG, colB, alpha);
      strokeWeight(elevFrac > 0.4 ? 1.8 : 1.2);
      line(p1[0], p1[1], p2[0], p2[1]);
    }
  }

  // 4. Center Axis Crosshairs & Title Datum
  stroke(1.0, 1.0, 1.0, 0.65 * glow_intensity);
  strokeWeight(1.0);
  line(w * 0.5 - 24, h - 32, w * 0.5 + 24, h - 32);
  line(w * 0.5, h - 44, w * 0.5, h - 20);
}
