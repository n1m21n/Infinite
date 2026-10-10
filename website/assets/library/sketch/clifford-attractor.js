// Clifford Attractor - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// x' = sin(a y) + c cos(a x), y' = sin(b x) + d cos(b y).
param("points", 9000, 1000, 30000);
param("drift", 0.15, 0, 1);
param("size", 1.6, 0.5, 4);

function draw(t) {
  background(0.03, 0.03, 0.06);
  const a = -1.4 + 0.25 * sin(t * drift), b = 1.6 + 0.2 * cos(t * drift * 0.8);
  const c = 1.0 + 0.2 * sin(t * drift * 0.6), d = 0.7;
  let x = 0.1, y = 0.1;
  const k = min(width, height) * 0.2;
  noStroke();
  for (let i = 0; i < points; i++) {
    const nx = sin(a * y) + c * cos(a * x), ny = sin(b * x) + d * cos(b * y);
    x = nx; y = ny;
    if (i > 20) {
      fill(hsl(0.55 + 0.25 * (i / points), 0.7, 0.62, 0.35));
      circle(width / 2 + x * k, height / 2 + y * k, size);
    }
  }
}
