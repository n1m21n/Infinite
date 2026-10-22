// Thomas Attractor - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Thomas cyclically symmetric chaotic system:
// dx = sin(y) - b * x
// dy = sin(z) - b * y
// dz = sin(x) - b * z
param("b", 0.208186, 0.15, 0.25);
param("trail", 2200, 500, 5000);
param("speed", 1.0, 0.2, 3.0);
param("zoom", 1.0, 0.4, 2.5);

function draw(t) {
  background(0.082, 0.098, 0.188); // brand midnight #151930
  const dt = 0.04 * speed;
  let x = 0.1, y = 0.0, z = 0.0;
  const steps = floor(t * 120) + 800;
  const pts = [];

  for (let i = 0; i < steps; i++) {
    const dx = sin(y) - b * x;
    const dy = sin(z) - b * y;
    const dz = sin(x) - b * z;
    x += dx * dt; y += dy * dt; z += dz * dt;
    pts.push([x, y, z]);
  }

  const from = max(0, pts.length - trail);
  const k = min(width, height) * 0.11 * zoom;
  const rotY = t * 0.35;
  const rotX = 0.45;
  const cosY = cos(rotY), sinY = sin(rotY);
  const cosX = cos(rotX), sinX = sin(rotX);

  noFill();
  strokeWeight(2.2);
  for (let i = from + 1; i < pts.length; i++) {
    const p1 = pts[i - 1], p2 = pts[i];
    const x1_ = p1[0] * cosY - p1[2] * sinY;
    const z1_ = p1[0] * sinY + p1[2] * cosY;
    const y1_ = p1[1] * cosX - z1_ * sinX;

    const x2_ = p2[0] * cosY - p2[2] * sinY;
    const z2_ = p2[0] * sinY + p2[2] * cosY;
    const y2_ = p2[1] * cosX - z2_ * sinX;

    const frac = (i - from) / (pts.length - from);
    stroke(hsl((0.04 + 0.60 * frac) % 1.0, 0.88, 0.4 + 0.35 * frac));
    line(width / 2 + x1_ * k, height / 2 - y1_ * k, width / 2 + x2_ * k, height / 2 - y2_ * k);
  }
}
