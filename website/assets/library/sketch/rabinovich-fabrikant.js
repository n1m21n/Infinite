// Rabinovich-Fabrikant Attractor - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Mikhail Rabinovich & Anatoly Fabrikant chaotic flow (1979):
// dx = y * (z - 1.0 + x^2) + gamma * x
// dy = x * (3.0 * z + 1.0 - x^2) + gamma * y
// dz = -2.0 * z * (alpha + x * y)
param("alpha", 0.14, 0.05, 0.35);
param("gamma", 0.10, 0.02, 0.25);
param("trail", 3200, 800, 7000);
param("zoom", 1.25, 0.4, 3.0);
param("spin", 0.4, 0.0, 2.0);
param("tilt", 0.72, 0.0, 1.5);

function draw(t) {
  background(0.082, 0.098, 0.188); // brand midnight #151930
  const dt = 0.0035;
  let x = -0.1, y = 0.5, z = 0.5;
  const steps = floor(t * 360) + 1600;
  const pts = [];

  for (let i = 0; i < steps; i++) {
    // RK2 numerical midpoint integration
    const dx1 = y * (z - 1.0 + x * x) + gamma * x;
    const dy1 = x * (3.0 * z + 1.0 - x * x) + gamma * y;
    const dz1 = -2.0 * z * (alpha + x * y);

    const mx = x + 0.5 * dt * dx1;
    const my = y + 0.5 * dt * dy1;
    const mz = z + 0.5 * dt * dz1;

    const dx2 = my * (mz - 1.0 + mx * mx) + gamma * mx;
    const dy2 = mx * (3.0 * mz + 1.0 - mx * mx) + gamma * my;
    const dz2 = -2.0 * mz * (alpha + mx * my);

    x += dx2 * dt;
    y += dy2 * dt;
    z += dz2 * dt;

    // Numerical clamp to bound chaotic departures
    x = clamp(x, -4.0, 4.0);
    y = clamp(y, -4.0, 4.0);
    z = clamp(z, -1.0, 8.0);

    if (i % 2 === 0) {
      pts.push([x, y, z]);
    }
  }

  const from = max(0, pts.length - trail);
  const k = min(width, height) * 0.14 * zoom;
  const rot = t * spin;
  const cosR = cos(rot), sinR = sin(rot);
  const cosT = cos(tilt), sinT = sin(tilt);

  noFill();
  strokeWeight(2.0);
  for (let i = from + 1; i < pts.length; i++) {
    const p1 = pts[i - 1], p2 = pts[i];
    // 3D rotation and perspective transformation
    const rx1 = p1[0] * cosR - p1[1] * sinR;
    const ry1 = (p1[0] * sinR + p1[1] * cosR) * cosT - p1[2] * sinT;
    const rx2 = p2[0] * cosR - p2[1] * sinR;
    const ry2 = (p2[0] * sinR + p2[1] * cosR) * cosT - p2[2] * sinT;

    const frac = (i - from) / (pts.length - from);
    // Brand colour transition: Emerald (#4EF2B8) -> Signal (#A7B1FF) -> Ember (#FF6B35)
    if (frac < 0.5) {
      const f2 = frac * 2.0;
      stroke(lerp(0.306, 0.655, f2), lerp(0.949, 0.694, f2), lerp(0.722, 1.0, f2), frac * 0.95);
    } else {
      const f2 = (frac - 0.5) * 2.0;
      stroke(lerp(0.655, 1.0, f2), lerp(0.694, 0.42, f2), lerp(1.0, 0.208, f2), frac * 0.95);
    }

    line(width * 0.5 + rx1 * k, height * 0.52 - ry1 * k, width * 0.5 + rx2 * k, height * 0.52 - ry2 * k);
  }
}
