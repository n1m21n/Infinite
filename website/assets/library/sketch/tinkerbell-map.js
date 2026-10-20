// Tinkerbell Map - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// 2D chaotic dynamical system with looping orbital plumes and continuous parameter drift.
param("a", 0.90, -1.5, 1.5);
param("b", -0.60, -1.5, 1.5);
param("c", 2.0, 0.0, 3.0);
param("d", 0.50, -1.5, 1.5);
param("points", 34000, 5000, 60000);
param("drift", 0.20, 0.0, 1.0);
param("zoom", 1.15, 0.4, 2.5);
param("size", 1.5, 0.5, 4.0);
param("hue", 0.62, 0.0, 1.0);

function draw(t) {
  background(0.04, 0.04, 0.08);
  const at = a + 0.025 * sin(t * drift * 0.85);
  const bt = b + 0.020 * cos(t * drift * 1.15);
  const ct = c + 0.030 * sin(t * drift * 0.95);
  const dt = d + 0.025 * cos(t * drift * 0.75);

  let x = -0.72, y = -0.64;
  const k = min(width, height) * 0.24 * zoom;
  const cx = width * 0.5 + 0.12 * k;
  const cy = height * 0.5 + 0.16 * k;
  noStroke();

  for (let i = 0; i < points; i++) {
    const nx = x * x - y * y + at * x + bt * y;
    const ny = 2.0 * x * y + ct * x + dt * y;
    x = nx;
    y = ny;

    // Guard against chaotic divergence
    if (abs(x) > 10.0 || abs(y) > 10.0) {
      x = -0.72;
      y = -0.64;
      continue;
    }

    if (i > 40) {
      const frac = i / points;
      const h = (hue + 0.42 * frac + 0.08 * sin(t * 0.25)) % 1.0;
      fill(hsl(h, 0.90, 0.64, 0.26));
      circle(cx + x * k, cy + y * k, size);
    }
  }
}
