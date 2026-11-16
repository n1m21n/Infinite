// Cyanotype Photogram Botanical Frond - Infinite Library (Sketch 2D)
param("pinnae", 24, 12, 36);
param("curvature", 0.34, 0.05, 0.65);
param("exposure", 0.88, 0.4, 1.0);
param("breeze", 0.18, 0.0, 0.6);

function draw(t) {
  // Prussian blue ferric-oxalate cyanotype ground (#103A7A)
  background(0.063, 0.227, 0.478);
  const w = width;
  const h = height;

  // Subtle solar vignette wash around margins (#071A3C)
  noStroke();
  for (let i = 0; i < 6; i++) {
    const f = i / 5.0;
    fill(0.027, 0.102, 0.235, 0.14 * (1.0 - f));
    rect(0, 0, w, h * (0.04 + f * 0.03));
    rect(0, h * (0.96 - f * 0.03), w, h * (0.04 + f * 0.03));
  }

  // Fine chemical stipple & silver-blue paper fibers
  for (let i = 0; i < 180; i++) {
    const px = ((sin(i * 12.9898) * 43758.5453) % 1 + 1) % 1;
    const py = ((sin(i * 78.233 + 1.7) * 29431.13) % 1 + 1) % 1;
    fill(0.718, 0.796, 0.890, 0.07 + 0.05 * sin(i * 3.1));
    circle(px * w, py * h, 1.5 + (i % 3));
  }

  // Contact-plate inner hairline border
  noFill();
  stroke(0.847, 0.894, 0.941, 0.42);
  strokeWeight(1.4);
  rect(w * 0.045, h * 0.045, w * 0.91, h * 0.91);

  // Draw two overlapping cyanotype fern fronds (primary + secondary specimen)
  const nPin = floor(pinnae);
  for (let frond = 0; frond < 2; frond++) {
    const isMain = frond === 0;
    const x0 = isMain ? w * 0.24 : w * 0.44;
    const y0 = isMain ? h * 0.91 : h * 0.89;
    const spanY = isMain ? h * 0.80 : h * 0.62;
    const archX = (isMain ? w * 0.46 : w * 0.34) * curvature * 2.2;
    const sway = sin(t * 0.65 + frond * 1.4) * breeze * w * 0.025;
    const maxLen = isMain ? w * 0.145 : w * 0.105;

    // Central rachis stem
    noFill();
    stroke(0.949, 0.965, 0.980, (isMain ? 0.88 : 0.72) * exposure);
    strokeWeight(isMain ? 2.4 : 1.7);
    beginShape();
    for (let s = 0; s <= 40; s++) {
      const u = s / 40.0;
      const sx = x0 + Math.pow(u, 1.35) * archX + sway * u * u;
      const sy = y0 - u * spanY;
      vertex(sx, sy);
    }
    endShape();

    // Pinnate leaflets with soft exposure halo + crisp inner silhouette
    for (let i = 1; i < nPin; i++) {
      const u = i / nPin;
      const sx = x0 + Math.pow(u, 1.35) * archX + sway * u * u;
      const sy = y0 - u * spanY;
      const taper = sin(Math.pow(u, 0.82) * PI);
      const len = maxLen * (0.22 + 0.88 * taper);
      const wid = len * 0.22;
      const baseAngle = -0.35 + u * 0.45 + sway * 0.002;

      for (let side = -1; side <= 1; side += 2) {
        const ang = baseAngle + side * (1.05 - u * 0.28);
        const mx = sx + cos(ang) * len * 0.52;
        const my = sy + sin(ang) * len * 0.52;

        // Soft ferric diffusion halo
        push();
        translate(mx, my);
        rotate(ang);
        noStroke();
        fill(0.718, 0.796, 0.890, 0.28 * exposure);
        ellipse(0, 0, len * 1.12, wid * 1.35);

        // Crisp paper-white contact-print pinna
        fill(0.949, 0.965, 0.980, (isMain ? 0.88 : 0.70) * exposure);
        ellipse(0, 0, len * 0.96, wid * 0.86);
        pop();
      }
    }
  }

  // Herbarium specimen cartouche in bottom-right
  noFill();
  stroke(0.847, 0.894, 0.941, 0.55);
  strokeWeight(1.1);
  rect(w * 0.64, h * 0.845, w * 0.27, h * 0.07);
  fill(0.847, 0.894, 0.941, 0.22);
  noStroke();
  rect(w * 0.655, h * 0.862, w * 0.20, 2.5);
  rect(w * 0.655, h * 0.884, w * 0.14, 2.0);
}
