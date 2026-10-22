// Dadras Attractor - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// S. Dadras & H. R. Momeni 5-parameter chaotic flow (2009):
// dx = y - a * x + b * y * z
// dy = c * y - x * z + z
// dz = d * x * y - e * z
param("a", 3.0, 1.0, 6.0);
param("b", 2.7, 1.0, 5.0);
param("c", 1.7, 0.5, 4.0);
param("d", 2.0, 0.5, 5.0);
param("e", 9.0, 2.0, 15.0);
param("trail", 2800, 500, 6000);
param("zoom", 1.15, 0.4, 3.0);
param("spin", 0.35, 0.0, 2.0);

function draw(t) {
  background(0.082, 0.098, 0.188); // brand midnight #151930
  const dt = 0.0018;
  let x = 0.8, y = -0.6, z = 0.4;
  const steps = floor(t * 320) + 1400;
  const pts = [];

  for (let i = 0; i < steps; i++) {
    // RK2 Midpoint numerical integration
    const dx1 = y - a * x + b * y * z;
    const dy1 = c * y - x * z + z;
    const dz1 = d * x * y - e * z;

    const mx = x + 0.5 * dt * dx1;
    const my = y + 0.5 * dt * dy1;
    const mz = z + 0.5 * dt * dz1;

    const dx2 = my - a * mx + b * my * mz;
    const dy2 = c * my - mx * mz + mz;
    const dz2 = d * mx * my - e * mz;

    x += dx2 * dt;
    y += dy2 * dt;
    z += dz2 * dt;

    if (i % 2 === 0) {
      pts.push([x, y, z]);
    }
  }

  const from = max(0, pts.length - trail);
  const k = min(width, height) * 0.038 * zoom;
  const rot = t * spin;
  const cosR = cos(rot), sinR = sin(rot);

  noFill();
  strokeWeight(2.0);
  for (let i = from + 1; i < pts.length; i++) {
    const p1 = pts[i - 1], p2 = pts[i];
    // 3D rotation and perspective projection to 2D
    const rx1 = p1[0] * cosR - p1[1] * sinR;
    const ry1 = (p1[0] * sinR + p1[1] * cosR) * 0.65 + p1[2] * 0.68;
    const rx2 = p2[0] * cosR - p2[1] * sinR;
    const ry2 = (p2[0] * sinR + p2[1] * cosR) * 0.65 + p2[2] * 0.68;

    const frac = (i - from) / (pts.length - from);
    // Brand gradient: emerald #4EF2B8 -> signal #A7B1FF -> ember #FF6B35
    stroke(hsl(0.48 - 0.42 * frac, 0.9, 0.48 + 0.32 * frac));
    line(width / 2 + rx1 * k, height / 2 - (ry1 - 4.5) * k, width / 2 + rx2 * k, height / 2 - (ry2 - 4.5) * k);
  }
}
