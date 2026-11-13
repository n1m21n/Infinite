// Liminal Void Infinite Colonnade Water Mirror - Infinite Library (Sketch 2D)
param("pillars", 8.0, 4.0, 16.0);
param("water_ripples", 0.45, 0.1, 1.0);
param("horizon_pos", 0.56, 0.4, 0.7);
param("mist_density", 0.60, 0.2, 1.0);

function draw(t) {
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const hy = h * horizon_pos;

  // 1. Dusk Twilight Sky Gradient (#00192A -> #D62828 -> #F77F00 -> #EAE2B7)
  const skySteps = 90;
  for (let i = 0; i < skySteps; i++) {
    const f = i / skySteps;
    const y1 = (i / skySteps) * hy;
    const y2 = ((i + 1) / skySteps) * hy;

    let r, g, b;
    if (f < 0.45) {
      const u = f / 0.45;
      r = 0.01 + u * 0.35;
      g = 0.08 + u * 0.05;
      b = 0.18 - u * 0.08;
    } else if (f < 0.82) {
      const u = (f - 0.45) / 0.37;
      r = 0.36 + u * 0.48;
      g = 0.13 + u * 0.32;
      b = 0.10 - u * 0.08;
    } else {
      const u = (f - 0.82) / 0.18;
      r = 0.84 + u * 0.12;
      g = 0.45 + u * 0.38;
      b = 0.02 + u * 0.55;
    }
    fill(r, g, b);
    noStroke();
    rect(0, y1, w, y2 - y1 + 1.0);
  }

  // 2. Distant Low-Slung Solar Disc on Horizon
  fill(0.96, 0.92, 0.78, 0.90);
  circle(cx, hy - h * 0.035, w * 0.09);
  fill(0.97, 0.50, 0.00, 0.40);
  circle(cx, hy - h * 0.035, w * 0.16);

  // 3. Flooded Reflective Water Plane
  const waterSteps = 90;
  for (let j = 0; j < waterSteps; j++) {
    const f = j / waterSteps;
    const y1 = hy + f * (h - hy);
    const y2 = hy + ((j + 1) / waterSteps) * (h - hy);

    const r = (0.75 * (1.0 - f) * 0.65) + 0.02;
    const g = (0.40 * (1.0 - f) * 0.50) + 0.04;
    const b = (0.15 * (1.0 - f) * 0.70) + 0.12;

    fill(r, g, b);
    noStroke();
    rect(0, y1, w, y2 - y1 + 1.0);
  }

  // Water Surface Horizontal Perspective Wave Tics
  stroke(0.97, 0.50, 0.00, 0.28);
  strokeWeight(1.2);
  const nRipples = 38;
  for (let r = 1; r < nRipples; r++) {
    const rf = Math.pow(r / nRipples, 2.2);
    const ry = hy + rf * (h - hy);
    const ripW = w * (0.15 + rf * 0.85);
    const xOff = sin(ry * 0.18 + t * 0.4) * (w * 0.02);
    line(cx - ripW * 0.5 + xOff, ry, cx + ripW * 0.5 + xOff, ry);
  }

  // 4. Perspective Colonnade of Concrete Monoliths
  const nCols = floor(pillars);
  const vpX = cx;

  for (let side = -1; side <= 1; side += 2) {
    for (let k = nCols; k >= 1; k--) {
      const zNorm = k / nCols;
      const zDist = Math.pow(zNorm, 2.0);
      const colX = vpX + side * (w * 0.18 + zDist * (w * 0.32));
      const colY = hy + zDist * (h * 0.38);
      const colW = (w * 0.032) + zDist * (w * 0.095);
      const colH = (h * 0.14) + zDist * (h * 0.65);

      const topY = colY - colH;

      fill(0.18, 0.19, 0.24);
      noStroke();
      rect(colX - colW * 0.5, topY, colW, colH);

      if (side < 0) {
        fill(0.97, 0.50, 0.00, 0.75);
        rect(colX + colW * 0.35, topY, colW * 0.15, colH);
      } else {
        fill(0.97, 0.50, 0.00, 0.75);
        rect(colX - colW * 0.5, topY, colW * 0.15, colH);
      }

      fill(0.08, 0.09, 0.12);
      if (side < 0) {
        rect(colX - colW * 0.5, topY, colW * 0.25, colH);
      } else {
        rect(colX + colW * 0.25, topY, colW * 0.25, colH);
      }

      stroke(0.06, 0.07, 0.09);
      strokeWeight(1.4);
      const nJoints = 6;
      for (let j = 1; j < nJoints; j++) {
        const jy = topY + (j / nJoints) * colH;
        line(colX - colW * 0.5, jy, colX + colW * 0.5, jy);
      }

      const reflH = colH * 0.62;
      fill(0.06, 0.07, 0.10, 0.65);
      noStroke();
      rect(colX - colW * 0.48, colY, colW * 0.96, reflH);

      stroke(0.97, 0.50, 0.00, 0.45);
      strokeWeight(1.2);
      for (let q = 4; q < reflH; q += 9) {
        const qy = colY + q;
        const qW = colW * (1.0 - q / reflH) * 0.85;
        const qOff = sin(qy * 0.25 + t) * (w * 0.01);
        line(colX - qW * 0.5 + qOff, qy, colX + qW * 0.5 + qOff, qy);
      }
    }
  }

  // 5. Central Solitary Floating Stele on Horizon
  const steleW = w * 0.024;
  const steleH = h * 0.12;
  fill(0.05, 0.06, 0.08);
  noStroke();
  rect(cx - steleW * 0.5, hy - steleH * 0.85, steleW, steleH);

  stroke(0.97, 0.50, 0.00, 0.85);
  strokeWeight(1.2);
  line(cx - steleW * 0.5, hy - steleH * 0.85, cx - steleW * 0.5, hy + steleH * 0.15);
  line(cx + steleW * 0.5, hy - steleH * 0.85, cx + steleW * 0.5, hy + steleH * 0.15);

  // 6. Horizon Atmospheric Fog Mist Layer
  noStroke();
  fill(0.92, 0.45, 0.12, 0.22 * mist_density);
  rect(0, hy - h * 0.04, w, h * 0.08);
}
