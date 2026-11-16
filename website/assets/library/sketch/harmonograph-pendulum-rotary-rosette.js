// Harmonograph Pendulum Rotary Rosette - Infinite Library (Sketch 2D)
param("petals", 5, 3, 9);
param("decay", 0.015, 0.005, 0.04);
param("eccentricity", 0.46, 0.15, 0.75);
param("drift", 0.35, 0.0, 1.2);

function draw(t) {
  // Warm vellum parchment ground (#DFD6C5)
  background(0.875, 0.839, 0.773);

  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const rad = min(w, h) * 0.38;

  // Polar goniometer reticle rings
  noFill();
  for (let r = 1; r <= 5; r++) {
    stroke(0.553, 0.482, 0.353, r === 5 ? 0.48 : 0.16);
    strokeWeight(r === 5 ? 1.6 : 0.9);
    circle(cx, cy, (rad * 2.08 * r) / 5);
  }

  // 24 radial azimuth hairlines
  for (let i = 0; i < 24; i++) {
    const a = (i / 24) * TAU;
    stroke(0.090, 0.098, 0.137, i % 6 === 0 ? 0.28 : 0.10);
    strokeWeight(i % 6 === 0 ? 1.2 : 0.75);
    line(cx + cos(a) * rad * 0.18, cy + sin(a) * rad * 0.18, cx + cos(a) * rad * 1.06, cy + sin(a) * rad * 1.06);
  }

  // Corner brass index ticks
  stroke(0.090, 0.098, 0.137, 0.65);
  strokeWeight(1.8);
  const m = w * 0.07;
  const tk = w * 0.03;
  line(m, m, m + tk, m); line(m, m, m, m + tk);
  line(w - m, m, w - m - tk, m); line(w - m, m, w - m, m + tk);
  line(m, h - m, m + tk, h - m); line(m, h - m, m, h - m - tk);
  line(w - m, h - m, w - m - tk, h - m); line(w - m, h - m, w - m, h - m - tk);

  const f1 = floor(petals) + 0.014;
  const f2 = 1.007;
  const phase = t * drift * 0.4;

  // Secondary vermilion counter-rotation rosette
  let vx0 = 0;
  let vy0 = 0;
  for (let i = 0; i <= 760; i++) {
    const u = i * 0.045;
    const env = exp(-decay * 1.15 * u);
    const vx = cx + rad * env * (0.55 * cos(f2 * u - phase) + eccentricity * cos(f1 * u + 0.8));
    const vy = cy + rad * env * (0.55 * sin(f2 * u - phase) - eccentricity * sin(f1 * u + 0.8));
    if (i > 0) {
      stroke(0.714, 0.231, 0.157, 0.25 + 0.45 * env);
      strokeWeight(1.0);
      line(vx0, vy0, vx, vy);
    }
    vx0 = vx;
    vy0 = vy;
  }

  // Primary deep charcoal rotary harmonograph ink trace
  let px0 = 0;
  let py0 = 0;
  for (let i = 0; i <= 1350; i++) {
    const u = i * 0.038;
    const env = exp(-decay * u);
    const x = cx + rad * env * ((1.0 - eccentricity * 0.55) * cos(f2 * u + phase) + eccentricity * 0.68 * cos(f1 * u + phase * 0.5));
    const y = cy + rad * env * ((1.0 - eccentricity * 0.55) * sin(f2 * u + phase) + eccentricity * 0.68 * sin(f1 * u + phase * 0.5));
    if (i > 0) {
      stroke(0.090, 0.098, 0.137, 0.30 + 0.64 * env);
      strokeWeight(1.15 + 0.5 * env);
      line(px0, py0, x, y);
    }
    px0 = x;
    py0 = y;
  }
}
