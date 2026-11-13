// Brutalist Architecture Spomenik Monolithic Pylon 3D - Infinite Library (Sketch 3D)
param("spin", 0.35, 0.1, 1.2);
param("scale_factor", 1.2, 0.5, 2.2);
param("pylon_count", 6.0, 3.0, 12.0);
param("cantilever", 1.3, 0.6, 2.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.48 + sin(time * 0.2) * 0.06);
  rotateY(time * 0.35 + 0.6);

  // Central stepped monolithic concrete plinth core (#7a7a78)
  fill(0.48, 0.48, 0.47);
  box(0.52 * scale_factor, 0.72 * scale_factor, 0.52 * scale_factor);

  // Heavy stepped cantilevered base tiers
  for (let b = 1; b <= 3; b++) {
    push();
    translate(0, -0.36 * scale_factor - b * 0.10 * scale_factor, 0);
    fill(0.32 - b * 0.04, 0.32 - b * 0.04, 0.31 - b * 0.04);
    box((0.52 + b * 0.35) * scale_factor, 0.09 * scale_factor, (0.52 + b * 0.35) * scale_factor);
    pop();
  }

  // Radial cantilevered monolithic pylon fins (Spomenik wing structures)
  const nPylons = floor(pylon_count);
  for (let i = 0; i < nPylons; i++) {
    const angle = (i / nPylons) * Math.PI * 2.0;
    push();
    rotateY(angle);
    translate(0.55 * scale_factor * cantilever, 0.15 * scale_factor, 0);
    rotateZ(0.35);

    if (i % 2 === 0) {
      fill(0.85, 0.84, 0.81); // Warm stone / pale concrete (#d9d6cf)
    } else {
      fill(0.42, 0.42, 0.41); // Raw weathered aggregate concrete (#6b6b69)
    }

    box(0.18 * scale_factor, 0.85 * scale_factor, 0.38 * scale_factor);

    push();
    translate(0, 0.45 * scale_factor, 0);
    fill(0.24, 0.24, 0.24);
    box(0.32 * scale_factor, 0.12 * scale_factor, 0.46 * scale_factor);
    pop();

    pop();
  }

  // Top structural crown slab
  push();
  translate(0, 0.42 * scale_factor, 0);
  fill(0.85, 0.84, 0.81);
  box(0.68 * scale_factor, 0.08 * scale_factor, 0.68 * scale_factor);
  pop();

  pop();
}
