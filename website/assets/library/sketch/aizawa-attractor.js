// Aizawa Attractor - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Aizawa attractor system:
// dx = (z - b) * x - d * y
// dy = d * x + (z - b) * y
// dz = c + a * z - (z^3) / 3 - (x^2 + y^2) * (1 + e * z) + f * z * (x^3)
param("a", 0.95, 0.5, 1.5);
param("b", 0.7, 0.1, 1.2);
param("c", 0.6, 0.1, 1.0);
param("d", 3.5, 1.0, 6.0);
param("trail", 1800, 300, 4500);
param("zoom", 1.1, 0.5, 2.5);

function draw(t) {
  background(0.082, 0.098, 0.188); // brand midnight #151930
  const dt = 0.01;
  const e = 0.25, f = 0.1;
  let x = 0.1, y = 0.0, z = 0.0;
  const steps = floor(t * 140) + 600;
  const pts = [];

  for (let i = 0; i < steps; i++) {
    const x2 = x * x, y2 = y * y, z2 = z * z;
    const dx = (z - b) * x - d * y;
    const dy = d * x + (z - b) * y;
    const dz = c + a * z - (z * z2) / 3.0 - (x2 + y2) * (1.0 + e * z) + f * z * (x * x2);
    x += dx * dt; y += dy * dt; z += dz * dt;
    pts.push([x, y, z]);
  }

  const from = max(0, pts.length - trail);
  const k = min(width, height) * 0.28 * zoom;
  const rot = t * 0.4;
  const cosR = cos(rot), sinR = sin(rot);

  noFill();
  strokeWeight(2.0);
  for (let i = from + 1; i < pts.length; i++) {
    const p1 = pts[i - 1], p2 = pts[i];
    const rx1 = p1[0] * cosR - p1[1] * sinR;
    const ry1 = (p1[0] * sinR + p1[1] * cosR) * 0.5 + p1[2] * 0.85;
    const rx2 = p2[0] * cosR - p2[1] * sinR;
    const ry2 = (p2[0] * sinR + p2[1] * cosR) * 0.5 + p2[2] * 0.85;

    const frac = (i - from) / (pts.length - from);
    stroke(hsl(0.64 - 0.60 * frac, 0.85, 0.45 + 0.35 * frac));
    line(width / 2 + rx1 * k, height / 2 - (ry1 - 0.5) * k, width / 2 + rx2 * k, height / 2 - (ry2 - 0.5) * k);
  }
}
