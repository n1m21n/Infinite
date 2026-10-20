// Svensson Attractor - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// 2D chaotic dynamical system with continuous sinusoidal coefficient modulation (Johnny Svensson).
param("a", 1.40, -3.0, 3.0);
param("b", 1.56, -3.0, 3.0);
param("c", 1.40, -3.0, 3.0);
param("d", -1.56, -3.0, 3.0);
param("points", 32000, 5000, 60000);
param("drift", 0.18, 0.0, 1.0);
param("zoom", 1.05, 0.4, 2.5);
param("size", 1.5, 0.5, 4.0);
param("hue", 0.65, 0.0, 1.0);

function draw(t) {
  background(0.04, 0.04, 0.08);
  const at = a + 0.04 * sin(t * drift);
  const bt = b + 0.03 * cos(t * drift * 0.85);
  const ct = c + 0.04 * sin(t * drift * 1.25);
  const dt = d + 0.05 * cos(t * drift * 0.65);

  let x = 0.1, y = 0.1;
  const k = min(width, height) * 0.18 * zoom;
  const cx = width * 0.5;
  const cy = height * 0.5;
  noStroke();

  for (let i = 0; i < points; i++) {
    const nx = dt * sin(at * x) - sin(bt * y);
    const ny = ct * cos(at * x) + cos(bt * y);
    x = nx;
    y = ny;

    if (i > 30) {
      const frac = i / points;
      const h = (hue + 0.38 * frac + 0.06 * sin(t * 0.2)) % 1.0;
      fill(hsl(h, 0.90, 0.62, 0.28));
      circle(cx + x * k, cy + y * k, size);
    }
  }
}
