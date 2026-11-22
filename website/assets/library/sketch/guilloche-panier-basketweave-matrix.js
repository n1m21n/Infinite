// Guilloche Panier Basketweave Matrix - Infinite Library (Sketch 2D)
param("cuts", 28, 12, 42);
param("weave_freq", 9.0, 4.0, 16.0);
param("wave_amp", 0.032, 0.01, 0.06);
param("drift", 0.18, 0.0, 0.6);

function draw(t) {
  // Deep horological slate-navy ground (#141923)
  background(0.078, 0.098, 0.137);
  const w = width;
  const h = height;
  const s = min(w, h);
  const R = s * 0.40;

  translate(w * 0.5, h * 0.5);

  // Outer silvered dial plate & bevel
  noStroke();
  fill(0.114, 0.141, 0.192);
  circle(0, 0, R * 2.16);
  fill(0.090, 0.110, 0.153);
  circle(0, 0, R * 2.02);

  // Orthogonal panier (basketweave) rose-engine hairlines clipped to dial disc
  const nCuts = floor(cuts);
  const segs = 90;
  noFill();

  // Horizontal & vertical crossed sinusoidal passes
  for (let pass = 0; pass < 2; pass++) {
    const rot = pass * (PI * 0.5) + 0.12 * sin(t * drift * 0.4);
    push();
    rotate(rot);
    for (let i = -nCuts; i <= nCuts; i++) {
      const y0 = (i / nCuts) * R * 0.92;
      const maxX = sqrt(max(0.0, R * R * 0.86 - y0 * y0));
      if (maxX < 4.0) continue;
      const phase = (i % 2 === 0 ? 0.0 : PI) + t * drift * (pass === 0 ? 1.0 : -0.8);

      strokeWeight(0.95);
      if (abs(i) % 5 === 0) {
        stroke(0.773, 0.655, 0.431, 0.62); // champagne brass guide cut
      } else {
        stroke(0.851, 0.871, 0.890, 0.52); // sterling silver (#D9DEE3)
      }

      beginShape();
      for (let k = 0; k <= segs; k++) {
        const u = (k / segs) * 2.0 - 1.0;
        const x = u * maxX;
        const dy = sin((x / R) * weave_freq * PI + phase) * (s * wave_amp);
        vertex(x, y0 + dy);
      }
      endShape();
    }
    pop();
  }

  // Twin cartouche apertures (power reserve arc + twin-barrel balance ring)
  noStroke();
  fill(0.078, 0.098, 0.137, 0.94);
  circle(0, -R * 0.42, R * 0.46);
  circle(0, R * 0.42, R * 0.46);

  noFill();
  strokeWeight(1.8);
  stroke(0.773, 0.655, 0.431, 0.90);
  circle(0, -R * 0.42, R * 0.46);
  circle(0, R * 0.42, R * 0.46);

  // Fine radial engine-turned sunray inside the two cartouches
  for (let c = -1; c <= 1; c += 2) {
    const cy = c * R * 0.42;
    const cr = R * 0.21;
    for (let i = 0; i < 36; i++) {
      const a = (i * TAU) / 36 + t * drift * 0.35 * c;
      strokeWeight(i % 3 === 0 ? 1.1 : 0.6);
      stroke(0.435, 0.482, 0.541, i % 3 === 0 ? 0.85 : 0.45);
      line(cos(a) * cr * 0.25, cy + sin(a) * cr * 0.25, cos(a) * cr * 0.94, cy + sin(a) * cr * 0.94);
    }
    noStroke();
    fill(0.773, 0.655, 0.431);
    circle(0, cy, R * 0.04);
    noFill();
  }

  // Outer brass chapter frame & cardinal index pips
  strokeWeight(2.2);
  stroke(0.851, 0.871, 0.890, 0.80);
  circle(0, 0, R * 1.88);
  strokeWeight(1.1);
  stroke(0.773, 0.655, 0.431, 0.75);
  circle(0, 0, R * 2.02);

  for (let i = 0; i < 12; i++) {
    const a = (i * TAU) / 12;
    const r0 = R * 0.95;
    const r1 = R * 1.00;
    strokeWeight(i % 3 === 0 ? 3.0 : 1.4);
    stroke(i % 3 === 0 ? 0.773 : 0.851, i % 3 === 0 ? 0.655 : 0.871, i % 3 === 0 ? 0.431 : 0.890, 0.92);
    line(cos(a) * r0, sin(a) * r0, cos(a) * r1, sin(a) * r1);
  }
}
