// Astrolabe Planisphere Almucantar Projection - Infinite Library (Sketch 2D)
param("latitude", 0.56, 0.25, 0.85);
param("almucantars", 12, 6, 18);
param("azimuths", 16, 8, 24);
param("alidade_speed", 0.14, 0.0, 0.6);

function draw(t) {
  // Deep midnight slate ground (#141926)
  background(0.078, 0.098, 0.149);

  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const R = min(w, h) * 0.41;

  // Subtle lathe-turned concentric brass plate grain
  noFill();
  for (let i = 0; i < 28; i++) {
    const rr = R * (0.14 + (i / 28) * 0.88);
    stroke(0.780, 0.678, 0.373, 0.045);
    strokeWeight(1.0);
    circle(cx, cy, rr * 2.0);
  }

  // Outer brass mater limb & throne crown lug at top
  stroke(0.780, 0.678, 0.373, 0.88);
  strokeWeight(2.4);
  circle(cx, cy, R * 2.04);
  stroke(0.780, 0.678, 0.373, 0.55);
  strokeWeight(1.4);
  circle(cx, cy, R * 1.90);
  circle(cx, cy, R * 1.76);

  // Throne (kursi) suspension ring & bracket at 12 o'clock
  stroke(0.780, 0.678, 0.373, 0.78);
  strokeWeight(2.0);
  circle(cx, cy - R * 1.11, R * 0.11);
  line(cx - R * 0.16, cy - R * 1.01, cx - R * 0.06, cy - R * 1.09);
  line(cx + R * 0.16, cy - R * 1.01, cx + R * 0.06, cy - R * 1.09);

  // 360-degree / 5-degree / 15-degree vernier calibration ticks on outer limb
  for (let i = 0; i < 180; i++) {
    const a = (i / 180) * TAU;
    const major = i % 15 === 0;
    const med = i % 5 === 0;
    const r0 = R * 0.95;
    const r1 = r0 + (major ? R * 0.07 : (med ? R * 0.045 : R * 0.025));
    stroke(0.902, 0.843, 0.698, major ? 0.85 : (med ? 0.52 : 0.24));
    strokeWeight(major ? 1.8 : 1.0);
    line(cx + cos(a) * r0, cy + sin(a) * r0, cx + cos(a) * r1, cy + sin(a) * r1);
  }

  // Tropic of Cancer, Equator, and Tropic of Capricorn base circles in verdigris
  stroke(0.231, 0.400, 0.353, 0.65);
  strokeWeight(1.3);
  circle(cx, cy, R * 1.24);
  circle(cx, cy, R * 0.76);

  // Cardinal meridian & horizon crosshairs
  stroke(0.780, 0.678, 0.373, 0.32);
  strokeWeight(1.1);
  line(cx - R * 0.88, cy, cx + R * 0.88, cy);
  line(cx, cy - R * 0.88, cx, cy + R * 0.88);

  // Stereographic almucantar altitude circles converging toward zenith
  const zenithY = cy - R * (0.28 + latitude * 0.22);
  const nAlm = floor(almucantars);
  for (let k = 1; k <= nAlm; k++) {
    const u = k / nAlm;
    const rr = R * (0.09 + 0.74 * pow(u, 1.15));
    const oy = zenithY + (cy - zenithY) * (u * 0.55);
    stroke(0.780, 0.678, 0.373, k % 3 === 0 ? 0.68 : 0.32);
    strokeWeight(k % 3 === 0 ? 1.5 : 0.95);
    circle(cx, oy, rr * 2.0);
  }

  // Curved azimuth arcs radiating from the zenith pole
  const nAz = floor(azimuths);
  for (let i = 0; i < nAz; i++) {
    const theta = (i / nAz) * TAU;
    stroke(0.231, 0.400, 0.353, i % 4 === 0 ? 0.62 : 0.30);
    strokeWeight(i % 4 === 0 ? 1.3 : 0.85);
    beginShape();
    for (let s = 0; s <= 24; s++) {
      const f = s / 24;
      const dist = f * R * 0.86;
      const bend = sin(f * 3.14159) * 0.36 * cos(theta);
      const px = cx + cos(theta + bend) * dist * 0.92;
      const py = zenithY + sin(theta + bend) * dist * 0.78 + f * f * R * 0.26;
      if ((px - cx) * (px - cx) + (py - cy) * (py - cy) <= R * R * 0.77) {
        vertex(px, py);
      }
    }
    endShape();
  }

  // Rotating brass alidade sighting rule
  const rot = t * alidade_speed + 0.48;
  push();
  translate(cx, cy);
  rotate(rot);
  stroke(0.902, 0.843, 0.698, 0.92);
  strokeWeight(2.2);
  line(-R * 0.94, 0, R * 0.94, 0);
  stroke(0.780, 0.678, 0.373, 0.65);
  strokeWeight(1.4);
  rect(-R * 0.84, -R * 0.025, R * 1.68, R * 0.05);
  fill(0.078, 0.098, 0.149);
  circle(0, 0, R * 0.09);
  fill(0.902, 0.843, 0.698);
  circle(0, 0, R * 0.032);
  pop();
}
