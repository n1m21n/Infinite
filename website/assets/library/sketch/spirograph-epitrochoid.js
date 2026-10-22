// Spirograph Epitrochoid - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Multi-frequency planetary epitrochoid drawing with phased eccentric harmonics.
param("R", 1.8, 0.5, 4.0);
param("r", 0.72, 0.1, 2.0);
param("d", 1.15, 0.1, 3.0);
param("harmonics", 3.0, 1.0, 8.0);
param("points", 3200, 800, 6000);
param("zoom", 1.05, 0.4, 2.5);

function draw(t) {
  background(0.082, 0.098, 0.188); // brand midnight #151930
  const cx = width / 2;
  const cy = height / 2;
  const scale = min(width, height) * 0.14 * zoom;

  const kRatio = (R + r) / r;
  const warp = sin(t * 0.6) * 0.25;
  const dMod = d * (1.0 + warp);
  const rot = t * 0.35;

  noFill();
  strokeWeight(1.8);

  let prevX = 0, prevY = 0;
  for (let i = 0; i <= points; i++) {
    const theta = (i / points) * Math.PI * 2 * harmonics + rot;
    // Primary epitrochoid
    const baseTheta = theta;
    const x0 = (R + r) * cos(baseTheta) - dMod * cos(kRatio * baseTheta);
    const y0 = (R + r) * sin(baseTheta) - dMod * sin(kRatio * baseTheta);

    // Secondary eccentric harmonic oscillation
    const hTheta = theta * 5.0 + t * 0.8;
    const px = x0 + cos(hTheta) * 0.22;
    const py = y0 + sin(hTheta) * 0.22;

    const sx = cx + px * scale;
    const sy = cy - py * scale;

    if (i > 0) {
      const frac = i / points;
      // Iridescent brand palette: signal #A7B1FF -> emerald #4EF2B8 -> ember #FF6B35
      const h = (0.64 + frac * 0.7) % 1.0;
      stroke(hsl(h, 0.84, 0.52));
      line(prevX, prevY, sx, sy);
    }
    prevX = sx;
    prevY = sy;
  }
}
