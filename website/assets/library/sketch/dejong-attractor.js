// De Jong Attractor - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Peter de Jong 4-parameter chaotic trigonometric attractor with high-density particle accumulation (Peter de Jong, 1990).
param("a", -2.24, -3.0, 3.0);
param("b", -1.65, -3.0, 3.0);
param("c", 0.43, -3.0, 3.0);
param("d", -2.43, -3.0, 3.0);
param("steps", 38000, 10000, 80000);
param("drift", 0.22, 0.0, 1.0);
param("speed", 0.30, 0.0, 1.5);
param("zoom", 1.15, 0.4, 2.5);
param("size", 1.35, 0.5, 4.0);
param("hue", 0.64, 0.0, 1.0);

function draw(t) {
  background(0.04, 0.04, 0.08);
  const aCur = a + 0.04 * sin(t * speed * 0.45 * drift);
  const bCur = b + 0.03 * cos(t * speed * 0.38 * drift);
  const cCur = c + 0.03 * sin(t * speed * 0.52 * drift);
  const dCur = d + 0.04 * cos(t * speed * 0.41 * drift);

  const k = min(width, height) * 0.23 * zoom;
  const cx = width * 0.5;
  const cy = height * 0.5;
  const rot = t * speed * 0.18;
  const cosR = cos(rot), sinR = sin(rot);
  noStroke();

  let x = 0.1, y = 0.1;
  const total = floor(steps);

  for (let i = 0; i < total; i++) {
    // Peter de Jong attractor iteration:
    // x_{n+1} = sin(a * y_n) - cos(b * x_n)
    // y_{n+1} = sin(c * x_n) - cos(d * y_n)
    const nextX = sin(aCur * y) - cos(bCur * x);
    const nextY = sin(cCur * x) - cos(dCur * y);
    x = nextX;
    y = nextY;

    if (i > 80) {
      // 2D centered canvas rotation
      const rx = x * cosR - y * sinR;
      const ry = x * sinR + y * cosR;

      const frac = i / total;
      const dist = sqrt(x * x + y * y) * 0.45;
      const h = (hue + 0.42 * frac + 0.25 * dist) % 1.0;
      const alpha = 0.15 + 0.18 * sin(frac * PI);
      fill(hsl(h, 0.92, 0.64, alpha));
      circle(cx + rx * k, cy + ry * k, size);
    }
  }
}
