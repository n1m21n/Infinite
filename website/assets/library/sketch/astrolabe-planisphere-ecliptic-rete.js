// Astrolabe Planisphere Ecliptic Rete - Infinite Library (Sketch 2D)
param("rete_spin", 0.12, 0.0, 0.5);
param("star_count", 14, 6, 22);
param("hour_arcs", 12, 6, 18);
param("eccentricity", 0.21, 0.10, 0.32);

function draw(t) {
  // Deep midnight slate ground (#141926)
  background(0.078, 0.098, 0.149);

  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const R = min(w, h) * 0.41;

  // Outer engraved mater ring
  noFill();
  stroke(0.780, 0.678, 0.373, 0.82);
  strokeWeight(2.4);
  circle(cx, cy, R * 2.02);
  stroke(0.780, 0.678, 0.373, 0.42);
  strokeWeight(1.2);
  circle(cx, cy, R * 1.88);

  // Unequal planetary hour arcs in the lower tympan hemisphere
  const nHr = floor(hour_arcs);
  for (let i = 1; i < nHr; i++) {
    const u = i / nHr - 0.5;
    stroke(0.231, 0.400, 0.353, 0.45);
    strokeWeight(1.1);
    beginShape();
    for (let s = 0; s <= 24; s++) {
      const f = s / 24;
      const rr = R * (0.25 + f * 0.62);
      const a = 1.5708 + u * 2.35 * (0.45 + 0.55 * f);
      vertex(cx + cos(a) * rr, cy + sin(a) * rr);
    }
    endShape();
  }

  // Equinoctial colure hairlines
  stroke(0.780, 0.678, 0.373, 0.25);
  strokeWeight(1.0);
  line(cx - R * 0.88, cy, cx + R * 0.88, cy);
  line(cx, cy - R * 0.88, cx, cy + R * 0.88);

  // Rotating eccentric ecliptic rete & zodiac ring
  const spin = t * rete_spin + 0.35;
  push();
  translate(cx, cy);
  rotate(spin);

  // Capricorn outer rete rim & equinoctial strap
  stroke(0.780, 0.678, 0.373, 0.72);
  strokeWeight(2.0);
  circle(0, 0, R * 1.66);
  line(-R * 0.83, 0, R * 0.83, 0);
  line(0, -R * 0.83, 0, R * 0.83);

  // Off-center stereographic ecliptic circle
  const ecY = -R * eccentricity;
  const ecR = R * 0.54;
  stroke(0.902, 0.843, 0.698, 0.92);
  strokeWeight(3.2);
  circle(0, ecY, ecR * 2.0);
  stroke(0.780, 0.678, 0.373, 0.75);
  strokeWeight(1.4);
  circle(0, ecY, (ecR - R * 0.075) * 2.0);

  // 12 Zodiac house dividers & degree ticks on the ecliptic band
  for (let i = 0; i < 72; i++) {
    const a = (i / 72) * TAU;
    const isHouse = i % 6 === 0;
    const rIn = ecR - R * 0.075;
    const rOut = ecR;
    stroke(0.902, 0.843, 0.698, isHouse ? 0.95 : 0.42);
    strokeWeight(isHouse ? 2.0 : 1.0);
    line(cos(a) * rIn, ecY + sin(a) * rIn, cos(a) * rOut, ecY + sin(a) * rOut);
  }

  // Curved flame-like star pointers (daggers) & bright star jewels
  const nStars = floor(star_count);
  for (let i = 0; i < nStars; i++) {
    const baseA = (i / nStars) * TAU + 0.22;
    const baseR = (i % 2 === 0) ? ecR : R * 0.78;
    const bx = cos(baseA) * baseR;
    const by = (i % 2 === 0 ? ecY : 0) + sin(baseA) * baseR;
    const tipR = R * (0.22 + ((i * 37) % 11) * 0.045);
    const tipA = baseA + 0.18 * sin(i * 1.7);
    const tx = cos(tipA) * tipR;
    const ty = sin(tipA) * tipR;
    const mx = (bx + tx) * 0.5 + cos(tipA + 1.57) * R * 0.045;
    const my = (by + ty) * 0.5 + sin(tipA + 1.57) * R * 0.045;

    stroke(0.780, 0.678, 0.373, 0.86);
    strokeWeight(1.8);
    beginShape();
    vertex(bx, by);
    vertex(mx, my);
    vertex(tx, ty);
    endShape();

    fill(0.902, 0.843, 0.698);
    noStroke();
    circle(tx, ty, R * 0.022);
    noFill();
  }

  // Central pole pin boss
  fill(0.078, 0.098, 0.149);
  stroke(0.902, 0.843, 0.698, 0.95);
  strokeWeight(2.2);
  circle(0, 0, R * 0.095);
  fill(0.780, 0.678, 0.373);
  noStroke();
  circle(0, 0, R * 0.036);
  pop();
}
