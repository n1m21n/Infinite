// Liminal Void Submerged Monolith Stairwell - Infinite Library (Sketch 2D)
param("stair_steps", 12.0, 6.0, 20.0);
param("water_level", 0.52, 0.35, 0.7);
param("portal_scale", 0.85, 0.5, 1.4);
param("fog_spread", 0.40, 0.1, 0.8);

function draw(t) {
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const hy = h * water_level;

  // 1. Heavy Dramatic Sunset Atmosphere Gradient (#020D1A -> #9E2A2B -> #E09F3E -> #FFF3B0)
  const skySteps = 90;
  for (let i = 0; i < skySteps; i++) {
    const f = i / skySteps;
    const y1 = (i / skySteps) * hy;
    const y2 = ((i + 1) / skySteps) * hy;

    let r, g, b;
    if (f < 0.40) {
      const u = f / 0.40;
      r = 0.01 + u * 0.30;
      g = 0.04 + u * 0.08;
      b = 0.14 - u * 0.04;
    } else if (f < 0.80) {
      const u = (f - 0.40) / 0.40;
      r = 0.31 + u * 0.48;
      g = 0.12 + u * 0.28;
      b = 0.10 - u * 0.06;
    } else {
      const u = (f - 0.80) / 0.20;
      r = 0.79 + u * 0.20;
      g = 0.40 + u * 0.42;
      b = 0.04 + u * 0.45;
    }
    fill(r, g, b);
    noStroke();
    rect(0, y1, w, y2 - y1 + 1.0);
  }

  // 2. Heavy Brutalist Arch Portal Framing Twilight Sky
  const portalW = w * 0.38 * portal_scale;
  const portalH = h * 0.46 * portal_scale;
  const pTop = hy - portalH;

  fill(0.08, 0.09, 0.12);
  noStroke();
  rect(cx - portalW * 0.5 - w * 0.06, pTop, w * 0.06, portalH);
  rect(cx + portalW * 0.5, pTop, w * 0.06, portalH);
  rect(cx - portalW * 0.5 - w * 0.06, pTop - h * 0.06, portalW + w * 0.12, h * 0.06);

  stroke(0.97, 0.50, 0.00, 0.85);
  strokeWeight(1.5);
  line(cx - portalW * 0.5, pTop, cx - portalW * 0.5, hy);
  line(cx + portalW * 0.5, pTop, cx + portalW * 0.5, hy);
  line(cx - portalW * 0.5, pTop, cx + portalW * 0.5, pTop);

  fill(0.99, 0.95, 0.82);
  circle(cx, hy - portalH * 0.45, w * 0.08);
  fill(0.96, 0.45, 0.10, 0.35);
  circle(cx, hy - portalH * 0.45, w * 0.16);

  // 3. Dark Mirror Flood Floor
  const waterSteps = 90;
  for (let j = 0; j < waterSteps; j++) {
    const f = j / waterSteps;
    const y1 = hy + f * (h - hy);
    const y2 = hy + ((j + 1) / waterSteps) * (h - hy);

    const r = (0.70 * (1.0 - f) * 0.55) + 0.02;
    const g = (0.35 * (1.0 - f) * 0.45) + 0.03;
    const b = (0.12 * (1.0 - f) * 0.60) + 0.09;

    fill(r, g, b);
    noStroke();
    rect(0, y1, w, y2 - y1 + 1.0);
  }

  stroke(0.95, 0.48, 0.10, 0.30);
  strokeWeight(1.2);
  const nRips = 32;
  for (let k = 1; k < nRips; k++) {
    const kf = Math.pow(k / nRips, 2.0);
    const ry = hy + kf * (h - hy);
    const rw = w * (0.2 + kf * 0.8);
    const xOff = sin(ry * 0.15 + t * 0.35) * (w * 0.018);
    line(cx - rw * 0.5 + xOff, ry, cx + rw * 0.5 + xOff, ry);
  }

  // 4. Submerged Brutalist Stepped Stairwells Flanking the Central Abyss
  const nSteps = floor(stair_steps);
  for (let side = -1; side <= 1; side += 2) {
    for (let s = 0; s < nSteps; s++) {
      const frac = s / nSteps;
      const stepY = hy - h * 0.28 + frac * (h * 0.55);
      const stepW = w * 0.32 * (1.0 - frac * 0.35);
      const stepH = (h * 0.55 / nSteps) * 0.85;

      const sx = (side < 0) ? 0 : w - stepW;

      fill(0.24, 0.26, 0.32);
      noStroke();
      rect(sx, stepY, stepW, stepH * 0.45);

      stroke(0.97, 0.50, 0.00, 0.70);
      strokeWeight(1.2);
      line(sx, stepY, sx + stepW, stepY);

      fill(0.09, 0.10, 0.14);
      noStroke();
      rect(sx, stepY + stepH * 0.45, stepW, stepH * 0.55);

      if (stepY > hy) {
        fill(0.04, 0.05, 0.08, 0.85);
        rect(sx, stepY, stepW, stepH);
      }
    }
  }

  fill(0.12, 0.13, 0.18);
  noStroke();
  rect(cx - w * 0.035, hy - h * 0.08, w * 0.07, h * 0.32);

  stroke(0.97, 0.50, 0.00, 0.75);
  strokeWeight(1.4);
  line(cx - w * 0.035, hy - h * 0.08, cx - w * 0.035, hy + h * 0.24);
  line(cx + w * 0.035, hy - h * 0.08, cx + w * 0.035, hy + h * 0.24);

  noStroke();
  fill(0.92, 0.45, 0.15, 0.24 * fog_spread);
  rect(0, hy - h * 0.05, w, h * 0.10);
}
