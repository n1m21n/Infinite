// Halvorsen Attractor - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// 3-fold cyclically symmetric chaotic strange attractor with looping orbital ribbons (Nils Halvorsen, 1989).
param("a", 1.89, 1.2, 2.5);
param("steps", 32000, 5000, 60000);
param("drift", 0.25, 0.0, 1.0);
param("speed", 0.35, 0.0, 1.5);
param("zoom", 1.15, 0.4, 2.5);
param("size", 1.4, 0.5, 4.0);
param("hue", 0.62, 0.0, 1.0);

function draw(t) {
  background(0.04, 0.04, 0.08);
  const aCur = a + 0.02 * sin(t * drift * 0.85);
  const rotY = t * speed * 0.42;
  const rotX = 0.52 + 0.12 * sin(t * speed * 0.32);

  const cosY = cos(rotY), sinY = sin(rotY);
  const cosX = cos(rotX), sinX = sin(rotX);

  const k = min(width, height) * 0.038 * zoom;
  const cx = width * 0.5;
  const cy = height * 0.5;
  noStroke();

  let x = -1.48, y = -0.22, z = 0.54;
  const dt = 0.0075;

  for (let i = 0; i < steps; i++) {
    // Halvorsen cyclic differential equations: dx/dt = -a*x - 4*y - 4*z - y^2, cyclically permuted
    const dx = -aCur * x - 4.0 * y - 4.0 * z - y * y;
    const dy = -aCur * y - 4.0 * z - 4.0 * x - z * z;
    const dz = -aCur * z - 4.0 * x - 4.0 * y - x * x;

    x += dx * dt;
    y += dy * dt;
    z += dz * dt;

    if (i > 60) {
      // 3D rotation projection
      const x1 = x * cosY + z * sinY;
      const z1 = -x * sinY + z * cosY;
      const y2 = y * cosX - z1 * sinX;
      const z2 = y * sinX + z1 * cosX;

      const frac = i / steps;
      const h = (hue + 0.38 * frac + 0.06 * sin(t * 0.22)) % 1.0;
      const alpha = 0.18 + 0.16 * ((z2 + 8.0) / 16.0);
      fill(hsl(h, 0.90, 0.62, alpha));
      circle(cx + x1 * k, cy + y2 * k, size);
    }
  }
}
