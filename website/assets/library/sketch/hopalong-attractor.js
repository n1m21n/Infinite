// Hopalong Attractor - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Barry Martin's Hopalong chaotic attractor: x' = y - sign(x) * sqrt(abs(b x - c)), y' = a - x.
param("a", 2.0, -5.0, 5.0);
param("b", 1.0, 0.1, 4.0);
param("c", 0.0, -3.0, 3.0);
param("points", 22000, 4000, 45000);
param("drift", 0.15, 0.0, 1.0);
param("size", 1.5, 0.5, 4.0);
param("zoom", 1.0, 0.4, 3.0);
param("hue", 0.65, 0.0, 1.0);

function draw(t) {
  background(0.04, 0.04, 0.08);
  const at = a + 0.14 * sin(t * drift);
  const bt = b + 0.08 * cos(t * drift * 0.85);
  const ct = c + 0.18 * sin(t * drift * 0.65);

  let x = 0.0, y = 0.0;
  const k = min(width, height) * 0.038 * zoom;
  noStroke();

  for (let i = 0; i < points; i++) {
    const s = x < 0 ? -1 : (x > 0 ? 1 : 0);
    const nx = y - s * sqrt(abs(bt * x - ct));
    const ny = at - x;
    x = nx;
    y = ny;

    if (i > 40) {
      const frac = i / points;
      const h = (hue + 0.42 * frac + 0.05 * sin(t * 0.25)) % 1.0;
      fill(hsl(h, 0.85, 0.58, 0.32));
      circle(width / 2 + x * k, height / 2 + y * k, size);
    }
  }
}
