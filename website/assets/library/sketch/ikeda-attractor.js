// Ikeda Attractor - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Non-linear laser resonator dynamics (K. Ikeda, 1979) producing swirling optical phase trajectories.
param("u", 0.918, 0.7, 0.99);
param("c1", 0.4, 0.1, 1.0);
param("c2", 6.0, 3.0, 10.0);
param("points", 28000, 5000, 50000);
param("drift", 0.15, 0.0, 1.0);
param("zoom", 1.0, 0.4, 2.5);
param("size", 1.4, 0.5, 4.0);
param("hue", 0.58, 0.0, 1.0);

function draw(t) {
  background(0.04, 0.04, 0.08);
  const ut = u + 0.012 * sin(t * drift);
  const c1t = c1 + 0.04 * cos(t * drift * 0.7);
  const c2t = c2 + 0.25 * sin(t * drift * 1.1);

  let x = 0.1, y = 0.1;
  const k = min(width, height) * 0.16 * zoom;
  const cx = width * 0.42;
  const cy = height * 0.52;
  noStroke();

  for (let i = 0; i < points; i++) {
    const r2 = x * x + y * y;
    const tn = c1t - c2t / (1.0 + r2);
    const cosT = cos(tn);
    const sinT = sin(tn);
    const nx = 1.0 + ut * (x * cosT - y * sinT);
    const ny = ut * (x * sinT + y * cosT);
    x = nx;
    y = ny;

    if (i > 40) {
      const frac = i / points;
      const h = (hue + 0.35 * frac + 0.08 * sin(t * 0.25)) % 1.0;
      fill(hsl(h, 0.88, 0.60, 0.32));
      circle(cx + x * k, cy + y * k, size);
    }
  }
}
