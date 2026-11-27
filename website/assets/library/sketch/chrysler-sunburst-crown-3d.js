// Chrysler Sunburst Crown 3D - Infinite Library (Sketch 3D)
param("tiers", 6, 3, 8);
param("rays", 14, 8, 22);
param("spin", 0.16, 0.0, 0.6);

function draw(t) {
  const nT = floor(tiers);
  const nR = floor(rays);

  push();
  translate(0, -0.18, 0);
  scale(0.76);
  rotateX(-0.18);
  rotateY(0.45 + t * spin);

  // Stepped Art Deco setback podium
  for (let b = 0; b < 3; b++) {
    const bw = 1.85 - b * 0.34;
    push();
    translate(0, -1.05 + b * 0.24, 0);
    fill(0.22 + b * 0.05, 0.25 + b * 0.05, 0.30 + b * 0.05);
    box(bw, 0.22, bw);
    pop();
  }

  // Terraced stainless-steel & brass sunburst tiers
  for (let k = 0; k < nT; k++) {
    const u = k / max(1, nT - 1);
    const yBase = -0.35 + k * 0.30;
    const rad = 0.78 * (1.0 - u * 0.66);

    // Brass collar ring
    push();
    translate(0, yBase - 0.04, 0);
    fill(0.831, 0.659, 0.310);
    torus(rad * 0.94, 0.028);
    pop();

    // Fluted steel arch drum
    push();
    translate(0, yBase + 0.08, 0);
    fill(0.78, 0.81, 0.85);
    cylinder(rad * 0.84, 0.24);
    pop();

    // Radiating triangular lancet fins around the tier
    const tierRays = max(6, nR - k);
    for (let r = 0; r < tierRays; r++) {
      const a = (r / tierRays) * TAU + k * 0.15;
      push();
      rotateY(a);
      translate(rad * 0.94, yBase + 0.09, 0);
      rotateZ(-0.22);
      fill(r % 2 === 0 ? 0.86 : 0.62, r % 2 === 0 ? 0.88 : 0.67, r % 2 === 0 ? 0.91 : 0.73);
      cone(0.055 * (1.0 - u * 0.4), 0.24);
      pop();
    }
  }

  // Telescoping needle spire
  push();
  translate(0, -0.35 + nT * 0.30 + 0.32, 0);
  fill(0.88, 0.90, 0.93);
  cone(0.08, 0.82);
  pop();

  pop();
}
