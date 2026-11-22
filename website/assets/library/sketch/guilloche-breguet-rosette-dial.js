// Guilloche Breguet Rosette Dial - Infinite Library (Sketch 2D)
param("lobes", 12, 6, 24);
param("rings", 18, 8, 28);
param("phase_drift", 0.22, 0.0, 0.8);
param("eccentricity", 0.18, 0.04, 0.35);

function draw(t) {
  // Deep horological slate-navy ground (#141923)
  background(0.078, 0.098, 0.137);
  const w = width;
  const h = height;
  const s = min(w, h);
  const R = s * 0.41;

  translate(w * 0.5, h * 0.5);

  // Outer brushed bezel ring
  noStroke();
  fill(0.118, 0.145, 0.196);
  circle(0, 0, R * 2.12);
  fill(0.094, 0.114, 0.157);
  circle(0, 0, R * 1.98);

  // Fine railway minute track on outer chapter ring
  const ticks = 120;
  for (let i = 0; i < ticks; i++) {
    const a = (i * TAU) / ticks;
    const isMajor = i % 10 === 0;
    const isMid = i % 5 === 0;
    const r0 = R * (isMajor ? 0.855 : isMid ? 0.88 : 0.90);
    const r1 = R * 0.945;
    strokeWeight(isMajor ? 2.2 : 0.9);
    if (isMajor) {
      stroke(0.773, 0.655, 0.431, 0.92); // champagne brass (#C5A76E)
    } else {
      stroke(0.435, 0.482, 0.541, 0.65); // steel blue-grey (#6F7B8A)
    }
    line(cos(a) * r0, sin(a) * r0, cos(a) * r1, sin(a) * r1);
  }

  // Chapter ring boundary hairlines
  noFill();
  strokeWeight(1.4);
  stroke(0.851, 0.871, 0.890, 0.55);
  circle(0, 0, R * 1.90);
  circle(0, 0, R * 1.68);

  // Engine-turned Breguet rosette guilloche (epitrochoid phase-interfering rings)
  const nLobes = floor(lobes);
  const nRings = floor(rings);
  const steps = 220;
  for (let k = 0; k < nRings; k++) {
    const u = (k + 0.5) / nRings;
    const baseR = R * (0.14 + u * 0.66);
    const amp = R * eccentricity * (0.14 + 0.22 * sin(u * PI));
    const phase = k * 0.34 + t * phase_drift * (k % 2 === 0 ? 1.0 : -0.65);

    strokeWeight(0.85);
    if (k % 4 === 0) {
      stroke(0.773, 0.655, 0.431, 0.78); // warm brass accent groove
    } else {
      const lum = 0.72 + 0.16 * sin(u * PI);
      stroke(lum * 0.92, lum * 0.95, lum, 0.68); // sterling silver (#D9DEE3)
    }

    beginShape();
    for (let i = 0; i <= steps; i++) {
      const a = (i * TAU) / steps;
      const mod = sin(a * nLobes + phase) * 0.65 + cos(a * nLobes * 2.0 - phase * 1.3) * 0.35;
      const rr = baseR + amp * mod;
      vertex(cos(a) * rr, sin(a) * rr);
    }
    endShape();
  }

  // Recessed off-center small-seconds subdial at 6 o'clock
  const subY = R * 0.44;
  const subR = R * 0.24;
  noStroke();
  fill(0.078, 0.098, 0.137, 0.92);
  circle(0, subY, subR * 2.05);
  noFill();
  strokeWeight(1.5);
  stroke(0.773, 0.655, 0.431, 0.85);
  circle(0, subY, subR * 2.0);

  for (let i = 0; i < 60; i++) {
    const a = (i * TAU) / 60;
    const rIn = subR * (i % 5 === 0 ? 0.78 : 0.86);
    strokeWeight(i % 5 === 0 ? 1.5 : 0.75);
    stroke(0.851, 0.871, 0.890, i % 5 === 0 ? 0.85 : 0.45);
    line(cos(a) * rIn, subY + sin(a) * rIn, cos(a) * subR * 0.95, subY + sin(a) * subR * 0.95);
  }

  // Orbiting blued-steel / brass regulator hands
  const mainA = t * 0.25 - PI * 0.5;
  strokeWeight(2.4);
  stroke(0.773, 0.655, 0.431, 0.95);
  line(0, 0, cos(mainA) * R * 0.74, sin(mainA) * R * 0.74);
  noFill();
  strokeWeight(1.6);
  circle(cos(mainA) * R * 0.56, sin(mainA) * R * 0.56, R * 0.09);

  const subA = t * 1.4 - PI * 0.5;
  strokeWeight(1.4);
  stroke(0.851, 0.871, 0.890, 0.95);
  line(0, subY, cos(subA) * subR * 0.76, subY + sin(subA) * subR * 0.76);

  // Center jewel arbor
  noStroke();
  fill(0.773, 0.655, 0.431);
  circle(0, 0, R * 0.065);
  fill(0.078, 0.098, 0.137);
  circle(0, 0, R * 0.025);
}
