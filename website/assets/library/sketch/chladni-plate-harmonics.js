// Chladni Plate Harmonics - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Ernst Chladni acoustic nodal patterns on a vibrating square plate with modal superposition and sand quanta.
param("mode_m", 3.0, 1.0, 7.0);
param("mode_n", 5.0, 1.0, 7.0);
param("resonance", 0.75, 0.1, 1.5);
param("drift", 0.35, 0.0, 1.0);
param("particles", 1200, 400, 3000);
param("hue", 0.62, 0.0, 1.0);
param("stroke_w", 1.4, 0.5, 3.0);

// Persistent grain quanta array
let grains = null;

function chladni(x, y, m, n, a, b) {
  // Wave equation on square plate with free boundaries:
  // w(x,y) = a * sin(n*pi*x)*sin(m*pi*y) - b * sin(m*pi*x)*sin(n*pi*y)
  return a * sin(n * PI * x) * sin(m * PI * y) - b * sin(m * PI * x) * sin(n * PI * y);
}

function draw(t) {
  background(0.082, 0.098, 0.188); // brand midnight #151930
  const cx = width * 0.5, cy = height * 0.5;
  const plateSize = min(width, height) * 0.82;
  const halfPlate = plateSize * 0.5;

  // Draw plate boundary frame
  noFill();
  strokeWeight(2.0);
  stroke(0.655, 0.694, 1.0, 0.45); // brand signal
  rect(cx - halfPlate, cy - halfPlate, plateSize, plateSize, 8);

  // Dynamic modal parameters
  const m = mode_m + sin(t * 0.4) * 0.25 * drift;
  const n = mode_n + cos(t * 0.5) * 0.25 * drift;
  const a = 1.0 + sin(t * 1.2) * 0.2 * resonance;
  const b = 1.0 - sin(t * 1.2) * 0.2 * resonance;

  // Render contour nodal curves (where chladni amplitude |w| < threshold)
  const gridRes = 72;
  const step = plateSize / gridRes;
  noFill();

  for (let gy = 0; gy < gridRes; gy++) {
    const y0 = ((gy / gridRes) - 0.5) * 2.0;
    const y1 = (((gy + 1) / gridRes) - 0.5) * 2.0;
    const py0 = cy + y0 * halfPlate;
    const py1 = cy + y1 * halfPlate;

    for (let gx = 0; gx < gridRes; gx++) {
      const x0 = ((gx / gridRes) - 0.5) * 2.0;
      const x1 = (((gx + 1) / gridRes) - 0.5) * 2.0;
      const px0 = cx + x0 * halfPlate;
      const px1 = cx + x1 * halfPlate;

      const v00 = chladni(x0, y0, m, n, a, b);
      const v10 = chladni(x1, y0, m, n, a, b);
      const v01 = chladni(x0, y1, m, n, a, b);
      const v11 = chladni(x1, y1, m, n, a, b);

      // Check if zero contour crosses cell
      const minV = min(v00, min(v10, min(v01, v11)));
      const maxV = max(v00, max(v10, max(v01, v11)));

      if (minV <= 0.0 && maxV >= 0.0) {
        const d = (abs(v00) + abs(v10) + abs(v01) + abs(v11)) * 0.25;
        const normR = sqrt(x0 * x0 + y0 * y0);
        const col = hsl((hue + normR * 0.35) % 1.0, 0.88, 0.65, 0.35 + 0.45 * (1.0 - min(1.0, d * 2.0)));
        strokeWeight(stroke_w);
        stroke(col);

        // Interpolated zero line segment
        const mx = (px0 + px1) * 0.5;
        const my = (py0 + py1) * 0.5;
        point(mx, my);
      }
    }
  }

  // Initialize grain quanta
  const count = floor(particles);
  if (!grains || grains.length !== count) {
    grains = [];
    for (let i = 0; i < count; i++) {
      grains.push({
        x: (random() - 0.5) * 2.0,
        y: (random() - 0.5) * 2.0,
        vx: 0,
        vy: 0
      });
    }
  }

  // Simulate sand grains migrating toward nodal lines (regions of zero vibration)
  const dt = 0.025;
  const eps = 0.01;
  fill(1.0, 0.42, 0.21, 0.85); // brand ember
  noStroke();

  for (let i = 0; i < grains.length; i++) {
    const g = grains[i];
    const val = chladni(g.x, g.y, m, n, a, b);
    const gradX = (chladni(g.x + eps, g.y, m, n, a, b) - chladni(g.x - eps, g.y, m, n, a, b)) / (2.0 * eps);
    const gradY = (chladni(g.x, g.y + eps, m, n, a, b) - chladni(g.x, g.y - eps, m, n, a, b)) / (2.0 * eps);

    // Vibration force pushes particles away from antinodes toward nodal zero curves
    const accel = -val * resonance * 4.2;
    g.vx = (g.vx + gradX * accel * dt + (random() - 0.5) * 0.008) * 0.92;
    g.vy = (g.vy + gradY * accel * dt + (random() - 0.5) * 0.008) * 0.92;

    g.x += g.vx;
    g.y += g.vy;

    // Bounds clamp inside plate
    if (g.x < -0.98 || g.x > 0.98) { g.x = (random() - 0.5) * 1.8; g.vx = 0; }
    if (g.y < -0.98 || g.y > 0.98) { g.y = (random() - 0.5) * 1.8; g.vy = 0; }

    const screenX = cx + g.x * halfPlate;
    const screenY = cy + g.y * halfPlate;
    const speed = sqrt(g.vx * g.vx + g.vy * g.vy);

    // Color particles by stillness (settled grains are bright ember, moving grains signal)
    if (speed < 0.005) {
      fill(1.0, 0.42, 0.21, 0.9); // ember
    } else {
      fill(0.655, 0.694, 1.0, 0.6); // signal
    }
    ellipse(screenX, screenY, 2.8, 2.8);
  }
}
