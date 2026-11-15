// Cyanotype Photogram Solar Plate - Infinite Library (Sketch 2D)
param("rings", 14, 6, 24);
param("ticks", 48, 24, 96);
param("eclipse", 0.32, 0.05, 0.65);
param("drift", 0.22, 0.0, 1.0);

function draw(t) {
  // Deep Prussian blue cyanotype plate (#0E346E)
  background(0.055, 0.204, 0.431);
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const R = Math.min(w, h) * 0.41;

  // Fine blueprint reticle grid
  stroke(0.718, 0.796, 0.890, 0.11);
  strokeWeight(0.8);
  const gridN = 12;
  for (let i = 1; i < gridN; i++) {
    const p = i / gridN;
    line(w * 0.06, h * p, w * 0.94, h * p);
    line(w * p, h * 0.06, w * p, h * 0.94);
  }

  // Outer blueprint frame & corner registration targets
  noFill();
  stroke(0.949, 0.965, 0.980, 0.65);
  strokeWeight(1.8);
  rect(w * 0.05, h * 0.05, w * 0.90, h * 0.90);

  // Translucent overlapping Fresnel exposure discs (contact-printed glass lenses)
  const phase = t * drift;
  const ex = cos(phase) * R * eclipse * 0.55;
  const ey = sin(phase * 0.7) * R * eclipse * 0.42;

  noStroke();
  fill(0.718, 0.796, 0.890, 0.22);
  circle(cx - ex * 0.6, cy - ey * 0.6, R * 1.12);
  fill(0.847, 0.894, 0.941, 0.26);
  circle(cx + ex * 0.65, cy + ey * 0.65, R * 0.88);
  fill(0.949, 0.965, 0.980, 0.34);
  circle(cx, cy, R * 0.34);

  // Concentric ephemeris rings
  const nRings = floor(rings);
  noFill();
  for (let i = 0; i < nRings; i++) {
    const u = (i + 1) / nRings;
    const rad = R * (0.14 + 0.86 * u);
    stroke(0.949, 0.965, 0.980, i % 3 === 0 ? 0.78 : 0.34);
    strokeWeight(i % 4 === 0 ? 1.8 : 0.9);
    circle(cx, cy, rad * 2.0);
  }

  // Radial degree ticks on outer azimuth ring
  const nTicks = floor(ticks);
  for (let i = 0; i < nTicks; i++) {
    const a = (i / nTicks) * TAU + phase * 0.15;
    const isMajor = i % 4 === 0;
    const r0 = R * (isMajor ? 0.92 : 0.96);
    const r1 = R * 1.03;
    stroke(0.949, 0.965, 0.980, isMajor ? 0.85 : 0.42);
    strokeWeight(isMajor ? 1.5 : 0.8);
    line(cx + cos(a) * r0, cy + sin(a) * r0, cx + cos(a) * r1, cy + sin(a) * r1);
  }

  // Diagonal meridian hairs & inner solar occultation ring
  stroke(0.847, 0.894, 0.941, 0.48);
  strokeWeight(1.0);
  line(cx - R * 1.06, cy, cx + R * 1.06, cy);
  line(cx, cy - R * 1.06, cx, cy + R * 1.06);

  stroke(0.949, 0.965, 0.980, 0.90);
  strokeWeight(2.2);
  circle(cx + ex * 0.4, cy + ey * 0.4, R * 0.52);
}
