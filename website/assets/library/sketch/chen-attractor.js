// Chen Attractor - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Guanrong Chen chaotic dynamical system (1999):
// dx = a * (y - x)
// dy = (c - a) * x - x * z + c * y
// dz = x * y - b * z
param("a", 35.0, 20.0, 50.0);
param("b", 3.0, 1.0, 8.0);
param("c", 28.0, 15.0, 40.0);
param("trail", 2400, 400, 5000);
param("zoom", 1.2, 0.5, 3.0);
param("spin", 0.4, 0.0, 2.0);

function draw(t) {
  background(0.082, 0.098, 0.188); // brand midnight #151930
  const dt = 0.0012;
  let x = -0.1, y = 0.5, z = -0.6;
  const steps = floor(t * 300) + 1200;
  const pts = [];

  for (let i = 0; i < steps; i++) {
    // RK2 Midpoint integration for numerical stability
    const dx1 = a * (y - x);
    const dy1 = (c - a) * x - x * z + c * y;
    const dz1 = x * y - b * z;

    const mx = x + 0.5 * dt * dx1;
    const my = y + 0.5 * dt * dy1;
    const mz = z + 0.5 * dt * dz1;

    const dx2 = a * (my - mx);
    const dy2 = (c - a) * mx - mx * mz + c * my;
    const dz2 = mx * my - b * mz;

    x += dx2 * dt;
    y += dy2 * dt;
    z += dz2 * dt;

    if (i % 2 === 0) {
      pts.push([x, y, z]);
    }
  }

  const from = max(0, pts.length - trail);
  const k = min(width, height) * 0.0125 * zoom;
  const rot = t * spin;
  const cosR = cos(rot), sinR = sin(rot);

  noFill();
  strokeWeight(2.0);
  for (let i = from + 1; i < pts.length; i++) {
    const p1 = pts[i - 1], p2 = pts[i];
    // Rotate in 3D and project to 2D screen
    const rx1 = p1[0] * cosR - p1[1] * sinR;
    const ry1 = (p1[0] * sinR + p1[1] * cosR) * 0.55 + p1[2] * 0.72;
    const rx2 = p2[0] * cosR - p2[1] * sinR;
    const ry2 = (p2[0] * sinR + p2[1] * cosR) * 0.55 + p2[2] * 0.72;

    const frac = (i - from) / (pts.length - from);
    // Gradient along trajectory: signal #A7B1FF -> ember #FF6B35
    stroke(hsl(0.62 - 0.56 * frac, 0.88, 0.46 + 0.36 * frac));
    line(width / 2 + rx1 * k, height / 2 - (ry1 - 20.0) * k, width / 2 + rx2 * k, height / 2 - (ry2 - 20.0) * k);
  }
}
