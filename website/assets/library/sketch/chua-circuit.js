// Chua Circuit - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Chua's non-linear double-scroll chaotic attractor with piecewise linear diode characteristics.
param("alpha", 10.2, 7.0, 15.0);
param("beta", 15.6, 10.0, 24.0);
param("speed", 1.8, 0.4, 4.0);
param("points", 3200, 1000, 8000);

function draw(t) {
  background(0.04, 0.05, 0.09);
  const cx = width / 2;
  const cy = height / 2;
  const k = min(width, height) * 0.14;

  const m0 = -1.143;
  const m1 = -0.714;

  // Numerical Runge-Kutta 2 integration of Chua equations
  let x = 0.7 * sin(t * 0.2), y = 0.0, z = 0.0;
  const dt = 0.018 * speed;

  let prevPx = 0, prevPy = 0;
  noFill();

  for (let i = 0; i < points; i++) {
    // Piecewise non-linear resistor characteristic g(x)
    const gx = m1 * x + 0.5 * (m0 - m1) * (abs(x + 1.0) - abs(x - 1.0));

    // Chua vector field
    const dx = alpha * (y - x - gx);
    const dy = x - y + z;
    const dz = -beta * y;

    x += dx * dt;
    y += dy * dt;
    z += dz * dt;

    // Projected 2D coordinates with subtle rotation
    const rot = t * 0.15;
    const rx = x * cos(rot) - z * sin(rot);
    const ry = y * 1.2 + (x * sin(rot) + z * cos(rot)) * 0.35;

    const px = cx + rx * k;
    const py = cy + ry * k;

    if (i > 30) {
      const frac = i / points;
      // Signal accent transitioning to ember
      const hue = (0.64 + 0.38 * frac + sin(z * 0.2) * 0.08) % 1.0;
      stroke(hsl(hue, 0.88, 0.65, 0.28 + 0.55 * frac));
      strokeWeight(1.4);
      line(prevPx, prevPy, px, py);
    }
    prevPx = px;
    prevPy = py;
  }
}
