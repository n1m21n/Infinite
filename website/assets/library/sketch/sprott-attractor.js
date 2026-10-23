// Sprott Attractor - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Clint Sprott's minimal chaotic flow with quadratic nonlinearity and velocity-mapped ribbon trails.
// dx/dt = a * y * z
// dy/dt = x - y
// dz/dt = 1.0 - b * x * y
param("a", 1.0, 0.5, 2.0);
param("b", 1.0, 0.4, 2.0);
param("trail", 2200, 500, 4000);
param("zoom", 1.15, 0.5, 2.5);
param("spin", 0.35, 0.0, 2.0);
param("hue", 0.62, 0.0, 1.0);

function draw(t) {
  background(0.082, 0.098, 0.188); // brand midnight #151930
  const dt = 0.022;
  let x = 0.63, y = 0.47, z = -0.52;
  const steps = floor(trail);
  const pts = [];

  for (let i = 0; i < steps; i++) {
    const dx = a * y * z;
    const dy = x - y;
    const dz = 1.0 - b * x * y;
    x += dx * dt;
    y += dy * dt;
    z += dz * dt;
    const speed = sqrt(dx * dx + dy * dy + dz * dz);
    pts.push([x, y, z, speed]);
  }

  const cx = width * 0.5;
  const cy = height * 0.5;
  const k = min(width, height) * 0.26 * zoom;
  const rotY = t * spin;
  const rotX = 0.45 + sin(t * 0.2) * 0.2;
  const cosY = cos(rotY), sinY = sin(rotY);
  const cosX = cos(rotX), sinX = sin(rotX);

  noFill();
  strokeWeight(1.8);

  for (let i = 1; i < pts.length; i++) {
    const p1 = pts[i - 1], p2 = pts[i];

    // 3D rotation projection
    const x1 = p1[0] * cosY - p1[1] * sinY;
    const y1_ = p1[0] * sinY + p1[1] * cosY;
    const y1 = y1_ * cosX - p1[2] * sinX;

    const x2 = p2[0] * cosY - p2[1] * sinY;
    const y2_ = p2[0] * sinY + p2[1] * cosY;
    const y2 = y2_ * cosX - p2[2] * sinX;

    const frac = i / pts.length;
    const vSpeed = p2[3];
    // Dynamic brand gradient: signal blue into ember orange
    const h = (hue + 0.35 * (1.0 - frac) + vSpeed * 0.12) % 1.0;
    const light = 0.45 + 0.45 * clamp(vSpeed * 0.4, 0.0, 1.0);
    const alpha = 0.25 + 0.75 * frac;
    stroke(hsl(h, 0.90, light, alpha));

    line(cx + x1 * k, cy - y1 * k, cx + x2 * k, cy - y2 * k);
  }
}
