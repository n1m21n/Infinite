// Technical Blueprint Aerospace Telemetry Grid - Infinite Library (Sketch 2D)
param("radar_rings", 7.0, 4.0, 12.0);
param("sweep_angle", 0.45, 0.1, 1.0);
param("trajectory_tilt", 0.38, 0.1, 0.8);
param("reticle_size", 0.80, 0.4, 1.2);

function draw(t) {
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;

  // 1. Deep Midnight Prussian Cyan Ground (#001528 -> #002b49)
  const bgSteps = 60;
  for (let i = 0; i < bgSteps; i++) {
    const frac = i / bgSteps;
    const y1 = frac * h;
    const y2 = ((i + 1) / bgSteps) * h;
    const r = 0.0 + frac * 0.01;
    const g = 0.08 + frac * 0.08;
    const b = 0.16 + frac * 0.12;
    fill(r, g, b);
    noStroke();
    rect(0, y1, w, y2 - y1 + 1.0);
  }

  // 2. Cartesian Precision Graticule Lines
  stroke(0.0, 0.83, 1.0, 0.12);
  strokeWeight(0.8);
  const minorStep = 20.0;
  for (let x = 0; x <= w; x += minorStep) {
    line(x, 0, x, h);
  }
  for (let y = 0; y <= h; y += minorStep) {
    line(0, y, w, y);
  }

  // Major Coordinate Grids with Subdivided Hash Marks
  stroke(0.0, 0.83, 1.0, 0.28);
  strokeWeight(1.2);
  const majorStep = 100.0;
  for (let x = 0; x <= w; x += majorStep) {
    line(x, 0, x, h);
    for (let y = 0; y <= h; y += 20) {
      line(x - 3, y, x + 3, y);
    }
  }
  for (let y = 0; y <= h; y += majorStep) {
    line(0, y, w, y);
    for (let x = 0; x <= w; x += 20) {
      line(x, y - 3, x, y + 3);
    }
  }

  // 3. Polar Coordinate Radar Rings
  const nRings = floor(radar_rings);
  const maxR = min(w, h) * 0.42 * reticle_size;

  for (let r = 1; r <= nRings; r++) {
    const rad = (r / nRings) * maxR;
    const isMajor = (r % 2 === 1);
    stroke(0.0, 0.83, 1.0, isMajor ? 0.70 : 0.35);
    strokeWeight(isMajor ? 1.6 : 0.9);
    noFill();
    circle(cx, cy, rad * 2.0);

    // Range Marker Ticks
    fill(1.0, 1.0, 1.0, 0.65);
    noStroke();
    rect(cx + rad - 3, cy - 2, 6, 4);
    rect(cx - rad - 3, cy - 2, 6, 4);
    rect(cx - 2, cy + rad - 3, 4, 6);
    rect(cx - 2, cy - rad - 3, 4, 6);
  }

  // Radial Azimuth Spokes (24 Radial Sectors)
  const nSectors = 24;
  for (let s = 0; s < nSectors; s++) {
    const ang = (s / nSectors) * 6.2831853;
    const isQuarter = (s % 6 === 0);
    stroke(isQuarter ? 1.0 : 0.0, isQuarter ? 1.0 : 0.83, 1.0, isQuarter ? 0.75 : 0.30);
    strokeWeight(isQuarter ? 1.4 : 0.8);
    line(cx, cy, cx + cos(ang) * maxR, cy + sin(ang) * maxR);

    // Outer Graticule Perimeter Compass Ticks
    const outerR = maxR * 1.04;
    const tickLen = isQuarter ? 12 : 6;
    line(
      cx + cos(ang) * maxR, cy + sin(ang) * maxR,
      cx + cos(ang) * (maxR + tickLen), cy + sin(ang) * (maxR + tickLen)
    );
  }

  // 4. Nested Orbital Trajectory Ellipses
  const time = t * sweep_angle;
  stroke(1.0, 1.0, 1.0, 0.65);
  strokeWeight(1.4);
  noFill();

  // Primary Orbital Ellipse (rotated)
  for (let e = 0; e < 3; e++) {
    const rot = trajectory_tilt + e * 0.62;
    const rx = maxR * (0.85 - e * 0.15);
    const ry = maxR * (0.42 - e * 0.08);

    stroke(e === 0 ? 0.0 : 1.0, 0.83, 1.0, 0.75 - e * 0.15);
    strokeWeight(e === 0 ? 1.6 : 1.1);

    const steps = 72;
    beginShape();
    for (let k = 0; k <= steps; k++) {
      const th = (k / steps) * 6.2831853;
      const ex = cos(th) * rx;
      const ey = sin(th) * ry;
      const rxRot = cx + ex * cos(rot) - ey * sin(rot);
      const ryRot = cy + ex * sin(rot) + ey * cos(rot);
      vertex(rxRot, ryRot);
    }
    endShape();

    // Satellite / Telemetry Node Position
    const satTh = time * (1.2 + e * 0.5) + e * 2.0;
    const satX0 = cos(satTh) * rx;
    const satY0 = sin(satTh) * ry;
    const satX = cx + satX0 * cos(rot) - satY0 * sin(rot);
    const satY = cy + satX0 * sin(rot) + satY0 * cos(rot);

    fill(1.0, 1.0, 1.0, 0.95);
    noStroke();
    circle(satX, satY, 8);
    stroke(0.0, 0.83, 1.0, 0.85);
    strokeWeight(1.2);
    noFill();
    circle(satX, satY, 16);

    // Telemetry Vector Line
    stroke(1.0, 0.3, 0.3, 0.75);
    line(satX, satY, satX + cos(satTh + 1.57) * 28, satY + sin(satTh + 1.57) * 28);
  }

  // 5. Central Telemetry Reticle & Crosshairs
  stroke(1.0, 1.0, 1.0, 0.90);
  strokeWeight(1.8);
  noFill();
  circle(cx, cy, 32);
  circle(cx, cy, 64);
  line(cx - 48, cy, cx + 48, cy);
  line(cx, cy - 48, cx, cy + 48);

  // 6. Corner Vernier Arc Scales (Top Left & Bottom Right)
  const arcCorners = [[80, 80, 0], [w - 80, h - 80, 3.14159]];
  for (let c = 0; c < arcCorners.length; c++) {
    const ac = arcCorners[c];
    stroke(0.0, 0.83, 1.0, 0.65);
    strokeWeight(1.2);
    noFill();
    const arcR = 54;
    for (let k = 0; k <= 18; k++) {
      const a = ac[2] + (k / 18) * 1.570796;
      const l1 = arcR;
      const l2 = arcR + (k % 3 === 0 ? 10 : 5);
      line(ac[0] + cos(a) * l1, ac[1] + sin(a) * l1, ac[0] + cos(a) * l2, ac[1] + sin(a) * l2);
    }
  }

  // 7. Outer Technical Frame with Graticule Coordinates
  stroke(0.0, 0.83, 1.0, 0.85);
  strokeWeight(2.0);
  rect(32, 32, w - 64, h - 64);
  stroke(1.0, 1.0, 1.0, 0.40);
  strokeWeight(1.0);
  rect(40, 40, w - 80, h - 80);
}
