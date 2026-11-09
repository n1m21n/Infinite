// Chavetta Montage Cantilever Ephemera 3D - Infinite Library (Sketch 3D)
param("spin", 0.35, 0.1, 1.2);
param("scale_factor", 1.35, 0.5, 2.0);
param("offset_angle", 0.45, 0.1, 1.0);
param("tiers", 6.0, 3.0, 8.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.48 + sin(time * 0.12) * 0.03);
  rotateY(time * 0.27 + 0.55);

  // 1. Asymmetrical Stepped Sub-Base
  push();
  translate(0, 1.25 * scale_factor, 0);
  fill(0.078, 0.078, 0.086);
  box(2.8 * scale_factor, 0.14, 2.8 * scale_factor);

  translate(0.15 * scale_factor, -0.09, 0.10 * scale_factor);
  fill(0.243, 0.447, 0.459);
  box(2.2 * scale_factor, 0.07, 2.0 * scale_factor);

  translate(-0.25 * scale_factor, -0.06, -0.15 * scale_factor);
  fill(0.929, 0.890, 0.816);
  box(1.8 * scale_factor, 0.06, 1.7 * scale_factor);
  pop();

  // 2. Central Core Stele Tower with Vertical Slit Cavities
  push();
  translate(0, 0.10 * scale_factor, 0);
  fill(0.929, 0.890, 0.816);
  box(0.65 * scale_factor, 2.4 * scale_factor, 0.65 * scale_factor);

  fill(0.078, 0.078, 0.086);
  box(0.45 * scale_factor, 2.3 * scale_factor, 0.72 * scale_factor);
  box(0.72 * scale_factor, 2.3 * scale_factor, 0.45 * scale_factor);
  pop();

  // 3. Cantilevered Ephemera Plates & Repeated Window Cutouts
  const nTiers = floor(tiers);
  for (let k = 0; k < nTiers; k++) {
    const frac = k / nTiers;
    const yPos = (0.95 - k * 0.42) * scale_factor;
    const rot = frac * Math.PI * 1.45 + time * 0.12;
    const len = (1.5 * (1.15 - frac * 0.20)) * scale_factor;

    push();
    translate(0, yPos, 0);
    rotateY(rot);

    push();
    translate(len * 0.5, 0, 0);
    if (k % 3 === 0) fill(0.816, 0.345, 0.157);
    else if (k % 3 === 1) fill(0.243, 0.447, 0.459);
    else fill(0.929, 0.890, 0.816);
    box(len, 0.15 * scale_factor, 0.32 * scale_factor);

    for (let w = 0; w < 3; w++) {
      push();
      translate(-len * 0.35 + w * (len * 0.32), 0, 0);
      fill(0.078, 0.078, 0.086);
      box(len * 0.18, 0.10 * scale_factor, 0.36 * scale_factor);
      pop();
    }

    translate(len * 0.44, 0.16 * scale_factor, 0);
    fill(0.929, 0.890, 0.816);
    box(0.35 * scale_factor, 0.25 * scale_factor, 0.35 * scale_factor);

    fill(0.078, 0.078, 0.086);
    box(0.20 * scale_factor, 0.15 * scale_factor, 0.38 * scale_factor);
    pop();

    push();
    translate(len * 0.30, 0.22 * scale_factor, 0);
    rotateZ(-0.45);
    fill(0.816, 0.345, 0.157);
    box(len * 0.65, 0.08 * scale_factor, 0.16 * scale_factor);
    pop();

    pop();
  }

  // 4. External Diagonal Exoskeleton Bracing Columns
  push();
  translate(0.65 * scale_factor, 0.20 * scale_factor, 0.40 * scale_factor);
  rotateZ(0.50);
  fill(0.243, 0.447, 0.459);
  box(0.12 * scale_factor, 2.3 * scale_factor, 0.12 * scale_factor);
  pop();

  push();
  translate(-0.65 * scale_factor, 0.20 * scale_factor, -0.40 * scale_factor);
  rotateZ(-0.50);
  fill(0.816, 0.345, 0.157);
  box(0.12 * scale_factor, 2.3 * scale_factor, 0.12 * scale_factor);
  pop();

  // 5. Crown Observation Deck
  push();
  translate(0, -1.25 * scale_factor, 0);
  fill(0.929, 0.890, 0.816);
  box(0.85 * scale_factor, 0.15 * scale_factor, 0.85 * scale_factor);

  translate(0, -0.12 * scale_factor, 0);
  fill(0.078, 0.078, 0.086);
  box(0.65 * scale_factor, 0.10 * scale_factor, 0.65 * scale_factor);
  pop();

  pop();
}
