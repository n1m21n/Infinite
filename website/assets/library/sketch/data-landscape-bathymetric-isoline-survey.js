// Data Landscape Bathymetric Isoline Survey - Infinite Library (Sketch 2D)
param("isolines", 32.0, 16.0, 48.0);
param("depth_warp", 1.15, 0.4, 2.2);
param("harmonic_peaks", 4.0, 2.0, 8.0);
param("contour_opacity", 0.92, 0.5, 1.0);

function clamp(x, a, b) {
  return Math.max(a, Math.min(b, x));
}

function draw(t) {
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;

  // 1. Deep Abyssal Midnight Ground (#070d1e -> #101c38)
  const bgSteps = 50;
  for (let i = 0; i < bgSteps; i++) {
    const frac = i / bgSteps;
    const y1 = frac * h;
    const y2 = ((i + 1) / bgSteps) * h;
    const r = 0.027 + frac * 0.035;
    const g = 0.051 + frac * 0.058;
    const b = 0.118 + frac * 0.102;
    fill(r, g, b);
    noStroke();
    rect(0, y1, w, y2 - y1 + 1.0);
  }

  // 2. Nautical Bathymetry Graticule Lines
  stroke(0.357, 0.753, 0.745, 0.14 * contour_opacity);
  strokeWeight(0.8);
  for (let x = 60; x <= w - 60; x += 60) {
    line(x, 60, x, h - 60);
  }
  for (let y = 60; y <= h - 60; y += 60) {
    line(60, y, w - 60, y);
  }

  // Outer Survey Border
  stroke(0.357, 0.753, 0.745, 0.45 * contour_opacity);
  strokeWeight(1.4);
  rect(48, 48, w - 96, h - 96);
  rect(54, 54, w - 108, h - 108);

  // 3. Concentric Bathymetric Elevation Isolines
  const count = floor(isolines);
  const maxRadius = 450.0;
  const numAngles = 180;
  const dTheta = (PI * 2.0) / numAngles;

  for (let k = count - 1; k >= 0; k--) {
    const depthFrac = k / count; // 0 = summit, 1 = deep sea
    const baseR = maxRadius * (0.08 + 0.92 * depthFrac);

    // Isoline color gradient: deep oceanic blue -> bright turquoise cyan -> pure white summit
    const rCol = 0.10 + (1.0 - depthFrac) * 0.85;
    const gCol = 0.25 + (1.0 - depthFrac) * 0.73;
    const bCol = 0.48 + (1.0 - depthFrac) * 0.52;

    // Fill ring body with subtle depth shading
    fill(0.043 + (1.0 - depthFrac) * 0.05, 0.075 + (1.0 - depthFrac) * 0.08, 0.169 + (1.0 - depthFrac) * 0.12, 0.35);
    stroke(rCol, gCol, bCol, (0.65 + (1.0 - depthFrac) * 0.35) * contour_opacity);
    strokeWeight(k % 4 === 0 ? 2.2 : 1.1);

    beginShape();
    for (let i = 0; i < numAngles; i++) {
      const th = i * dTheta;

      // Complex multi-harmonic ocean trench & seamount contour perturbations
      const h1 = sin(th * 3.0 + t * 0.35 + depthFrac * 2.2) * 36.0;
      const h2 = cos(th * 5.0 - t * 0.50 + depthFrac * 1.5) * 22.0;
      const h3 = sin(th * 7.0 + t * 0.70) * 14.0;
      const h4 = cos(th * 2.0 + t * 0.20) * 28.0;

      const r = baseR + (h1 + h2 + h3 + h4) * depth_warp;
      const px = cx + cos(th) * r;
      const py = cy + sin(th) * r;
      vertex(px, py);
    }
    endShape(CLOSE);
  }

  // 4. Nautical Sounding Crosshairs & Depth Markers
  stroke(1.0, 1.0, 1.0, 0.85 * contour_opacity);
  strokeWeight(1.4);
  line(cx - 18, cy, cx + 18, cy);
  line(cx, cy - 18, cx, cy + 18);

  const quadrants = [
    [cx - 240, cy - 240], [cx + 240, cy - 240],
    [cx - 240, cy + 240], [cx + 240, cy + 240]
  ];
  for (let i = 0; i < quadrants.length; i++) {
    const qx = quadrants[i][0];
    const qy = quadrants[i][1];
    line(qx - 10, qy, qx + 10, qy);
    line(qx, qy - 10, qx, qy + 10);
    noFill();
    rect(qx - 16, qy - 16, 32, 32);
  }
}
