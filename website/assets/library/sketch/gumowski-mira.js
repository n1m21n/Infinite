// Gumowski-Mira Attractor - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Non-linear accelerator mapping (I. Gumowski & C. Mira, 1980) generating biological wing and feather dynamics.
param("alpha", 0.008, -0.05, 0.05);
param("mu", -0.49, -1.0, 1.0);
param("points", 26000, 5000, 48000);
param("drift", 0.12, 0.0, 1.0);
param("zoom", 1.0, 0.4, 2.5);
param("size", 1.4, 0.5, 4.0);
param("hue", 0.64, 0.0, 1.0);

function gFn(x, muVal) {
  return muVal * x + (2.0 * (1.0 - muVal) * x * x) / (1.0 + x * x);
}

function draw(t) {
  background(0.04, 0.04, 0.08);
  const at = alpha + 0.003 * sin(t * drift);
  const mt = mu + 0.015 * cos(t * drift * 0.8);

  let x = 0.1, y = 0.1;
  const k = min(width, height) * 0.042 * zoom;
  noStroke();

  for (let i = 0; i < points; i++) {
    const gx = gFn(x, mt);
    const nx = y + at * (1.0 - 0.05 * y * y) * y + gx;
    const gnx = gFn(nx, mt);
    const ny = -x + gnx;
    x = nx;
    y = ny;

    if (i > 30) {
      const frac = i / points;
      const h = (hue + 0.38 * frac + 0.06 * sin(t * 0.3)) % 1.0;
      fill(hsl(h, 0.88, 0.58, 0.34));
      circle(width / 2 + x * k, height / 2 + y * k, size);
    }
  }
}
