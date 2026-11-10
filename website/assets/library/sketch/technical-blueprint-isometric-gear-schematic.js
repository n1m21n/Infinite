// Technical Blueprint Isometric Gear Schematic - Infinite Library (Sketch 2D)
param("teeth", 16.0, 8.0, 32.0);
param("ring_scale", 0.85, 0.5, 1.5);
param("precision_ticks", 48.0, 24.0, 96.0);
param("draft_opacity", 0.88, 0.4, 1.0);

function draw(t) {
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;

  // 1. Deep Prussian Blue Blueprint Ground (#001a30 -> #002b49)
  const bgSteps = 60;
  for (let i = 0; i < bgSteps; i++) {
    const frac = i / bgSteps;
    const y1 = frac * h;
    const y2 = ((i + 1) / bgSteps) * h;
    const r = 0.0 + frac * 0.01;
    const g = 0.10 + frac * 0.07;
    const b = 0.19 + frac * 0.10;
    fill(r, g, b);
    noStroke();
    rect(0, y1, w, y2 - y1 + 1.0);
  }

  // 2. Fine Drafting Coordinate Grid
  stroke(0.0, 0.83, 1.0, 0.14 * draft_opacity);
  strokeWeight(0.8);
  const minorGrid = 24.0;
  for (let x = 0; x <= w; x += minorGrid) {
    line(x, 0, x, h);
  }
  for (let y = 0; y <= h; y += minorGrid) {
    line(0, y, w, y);
  }

  // Major Coordinate Grid with Axis Accents
  stroke(0.0, 0.83, 1.0, 0.32 * draft_opacity);
  strokeWeight(1.2);
  const majorGrid = 120.0;
  for (let x = 0; x <= w; x += majorGrid) {
    line(x, 0, x, h);
  }
  for (let y = 0; y <= h; y += majorGrid) {
    line(0, y, w, y);
  }

  // Precision Outer Technical Border & Datum Registration Corners
  stroke(1.0, 1.0, 1.0, 0.75 * draft_opacity);
  strokeWeight(1.8);
  rect(36, 36, w - 72, h - 72);
  rect(44, 44, w - 88, h - 88);

  // Corner Crosshairs & Calibration Ticks
  const cornerMarks = [[40, 40], [w - 40, 40], [40, h - 40], [w - 40, h - 40]];
  for (let i = 0; i < cornerMarks.length; i++) {
    const pt = cornerMarks[i];
    line(pt[0] - 16, pt[1], pt[0] + 16, pt[1]);
    line(pt[0], pt[1] - 16, pt[0], pt[1] + 16);
    noFill();
    circle(pt[0], pt[1], 14);
  }

  // 3. Central Planetary Gear Mechanism
  const time = t * 0.35;
  const baseR = min(w, h) * 0.34 * ring_scale;

  // Outer Annular Ring Gear Pitch Circle
  stroke(0.0, 0.83, 1.0, 0.85 * draft_opacity);
  strokeWeight(2.0);
  noFill();
  circle(cx, cy, baseR * 2.0);
  stroke(1.0, 1.0, 1.0, 0.45 * draft_opacity);
  strokeWeight(1.0);
  circle(cx, cy, baseR * 2.14);
  circle(cx, cy, baseR * 1.86);

  // Outer Circular Vernier Angle Scale
  const nTicks = floor(precision_ticks);
  for (let k = 0; k < nTicks; k++) {
    const ang = (k / nTicks) * 6.2831853;
    const isMajor = (k % 4 === 0);
    const rIn = baseR * 2.0;
    const rOut = baseR * 2.0 + (isMajor ? 14 : 7);
    stroke(isMajor ? 1.0 : 0.0, isMajor ? 1.0 : 0.83, 1.0, isMajor ? 0.9 : 0.5);
    strokeWeight(isMajor ? 1.4 : 0.9);
    line(cx + cos(ang) * rIn, cy + sin(ang) * rIn, cx + cos(ang) * rOut, cy + sin(ang) * rOut);
  }

  // Central Sun Gear
  const sunR = baseR * 0.38;
  const nSunTeeth = floor(teeth);
  stroke(0.0, 0.83, 1.0, 0.95);
  strokeWeight(1.8);
  fill(0.0, 0.16, 0.28, 0.75);
  circle(cx, cy, sunR * 1.7);

  // Sun Gear Teeth Profiles
  for (let i = 0; i < nSunTeeth; i++) {
    const a = (i / nSunTeeth) * 6.2831853 + time;
    const a1 = a - 0.08;
    const a2 = a + 0.08;
    const r0 = sunR * 0.88;
    const r1 = sunR * 1.15;
    fill(0.0, 0.83, 1.0, 0.85);
    noStroke();
    quad(
      cx + cos(a1) * r0, cy + sin(a1) * r0,
      cx + cos(a1) * r1, cy + sin(a1) * r1,
      cx + cos(a2) * r1, cy + sin(a2) * r1,
      cx + cos(a2) * r0, cy + sin(a2) * r0
    );
  }

  // Sun Gear Center Bore & Keyway
  fill(0.0, 0.10, 0.20);
  stroke(1.0, 1.0, 1.0, 0.85);
  strokeWeight(1.5);
  circle(cx, cy, sunR * 0.45);
  fill(0.0, 0.83, 1.0, 0.9);
  rect(cx - 4, cy - sunR * 0.32, 8, 12);

  // 4. Three Interlocking Planet Gears
  const nPlanets = 3;
  const orbitR = baseR * 0.62;
  const planetR = baseR * 0.26;
  const nPlanetTeeth = floor(teeth * 0.68);

  for (let p = 0; p < nPlanets; p++) {
    const orbitAng = (p / nPlanets) * 6.2831853 - time * 0.6;
    const px = cx + cos(orbitAng) * orbitR;
    const py = cy + sin(orbitAng) * orbitR;

    // Pitch Circle & Spokes
    stroke(0.0, 0.83, 1.0, 0.85);
    strokeWeight(1.5);
    fill(0.0, 0.15, 0.26, 0.6);
    circle(px, py, planetR * 1.8);

    // Planet Gear Teeth
    const spinAng = time * 1.8 + p * 2.1;
    for (let k = 0; k < nPlanetTeeth; k++) {
      const a = (k / nPlanetTeeth) * 6.2831853 + spinAng;
      const a1 = a - 0.10;
      const a2 = a + 0.10;
      const r0 = planetR * 0.85;
      const r1 = planetR * 1.18;
      fill(1.0, 1.0, 1.0, 0.75);
      noStroke();
      quad(
        px + cos(a1) * r0, py + sin(a1) * r0,
        px + cos(a1) * r1, py + sin(a1) * r1,
        px + cos(a2) * r1, py + sin(a2) * r1,
        px + cos(a2) * r0, py + sin(a2) * r0
      );
    }

    // Planet Hub & Bolt Circle
    stroke(0.0, 0.83, 1.0, 0.9);
    strokeWeight(1.4);
    fill(0.0, 0.12, 0.22);
    circle(px, py, planetR * 0.55);
    for (let b = 0; b < 4; b++) {
      const ba = (b / 4) * 6.2831853;
      fill(1.0, 1.0, 1.0, 0.9);
      noStroke();
      circle(px + cos(ba) * planetR * 0.32, py + sin(ba) * planetR * 0.32, 4);
    }

    // Carrier Arm Lines
    stroke(1.0, 1.0, 1.0, 0.40);
    strokeWeight(1.0);
    line(cx, cy, px, py);
  }

  // 5. Technical Dimension Lines & Dimension Text Annotation
  stroke(1.0, 1.0, 1.0, 0.65 * draft_opacity);
  strokeWeight(1.2);

  // Horizontal Centerline
  line(cx - baseR * 1.25, cy, cx + baseR * 1.25, cy);
  // Vertical Centerline
  line(cx, cy - baseR * 1.25, cx, cy + baseR * 1.25);

  // Diagonal Dimension Leader Lines with Arrowheads
  const dimAng = -0.785398;
  const leadX1 = cx + cos(dimAng) * baseR;
  const leadY1 = cy + sin(dimAng) * baseR;
  const leadX2 = leadX1 + 75;
  const leadY2 = leadY1 - 55;
  line(leadX1, leadY1, leadX2, leadY2);
  line(leadX2, leadY2, leadX2 + 85, leadY2);

  // Tolerance Callout Box
  stroke(0.0, 0.83, 1.0, 0.75);
  strokeWeight(1.0);
  fill(0.0, 0.12, 0.22, 0.85);
  rect(leadX2 + 4, leadY2 - 22, 78, 18);

  // Technical Specification Stamp Block (Bottom Right)
  const stampX = w - 240;
  const stampY = h - 110;
  stroke(0.0, 0.83, 1.0, 0.80);
  strokeWeight(1.2);
  fill(0.0, 0.10, 0.20, 0.90);
  rect(stampX, stampY, 196, 66);
  line(stampX, stampY + 22, stampX + 196, stampY + 22);
  line(stampX, stampY + 44, stampX + 196, stampY + 44);
  line(stampX + 98, stampY + 22, stampX + 98, stampY + 66);

  // Stamp Block Internal Detail Lines
  stroke(1.0, 1.0, 1.0, 0.50);
  strokeWeight(1.0);
  line(stampX + 10, stampY + 11, stampX + 180, stampY + 11);
  line(stampX + 10, stampY + 33, stampX + 88, stampY + 33);
  line(stampX + 108, stampY + 33, stampX + 186, stampY + 33);
  line(stampX + 10, stampY + 55, stampX + 88, stampY + 55);
}
