// Lorenz Attractor - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Lorenz attractor: dx=s(y-x), dy=x(r-z)-y, dz=xy-bz, integrated from t=0 each frame.
param("rho", 28, 10, 40);
param("trail", 1400, 200, 4000);
param("zoom", 1, 0.5, 2);

function draw(t) {
  background(0.04, 0.04, 0.08);
  const s = 10, b = 8 / 3, dt = 0.006;
  let x = 0.1, y = 0, z = 0;
  const steps = floor(t * 120) + 400;
  const pts = [];
  for (let i = 0; i < steps; i++) {
    const dx = s * (y - x), dy = x * (rho - z) - y, dz = x * y - b * z;
    x += dx * dt; y += dy * dt; z += dz * dt;
    pts.push([x, y, z]);
  }
  const from = max(0, pts.length - trail);
  const k = min(width, height) / 60 * zoom;
  noFill();
  strokeWeight(2.2);
  for (let i = from + 1; i < pts.length; i++) {
    const a = pts[i - 1], p = pts[i];
    const f = (i - from) / (pts.length - from);
    stroke(hsl(0.58 - 0.5 * f, 0.8, 0.35 + 0.35 * f));
    line(width / 2 + a[0] * k, height * 0.9 - a[2] * k, width / 2 + p[0] * k, height * 0.9 - p[2] * k);
  }
}
