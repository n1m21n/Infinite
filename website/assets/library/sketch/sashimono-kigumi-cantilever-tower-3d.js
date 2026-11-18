// Sashimono Kigumi Cantilever Tower 3D - Infinite Library (Sketch 3D)
param("tiers", 8, 4, 12);
param("span", 1.75, 1.0, 2.4);
param("twist", 0.14, 0.0, 0.35);
param("orbit", 0.22, 0.0, 0.6);

function draw(t) {
  rotateX(0.38);
  rotateY(t * orbit + 0.65);

  const nTiers = floor(tiers);
  const stepY = 0.24;
  const baseY = -nTiers * stepY * 0.5;

  // Dark yakisugi charred cedar plinth
  fill(0.12, 0.10, 0.09);
  push();
  translate(0, baseY - 0.14, 0);
  box(span * 1.15, 0.14, span * 1.15);
  pop();

  // Four vertical hinoki corner posts
  fill(0.76, 0.59, 0.42);
  const postOff = span * 0.26;
  const totalH = nTiers * stepY + 0.35;
  for (let px = -1; px <= 1; px += 2) {
    for (let pz = -1; pz <= 1; pz += 2) {
      push();
      translate(px * postOff, 0, pz * postOff);
      box(0.09, totalH, 0.09);
      pop();
    }
  }

  // Interlocking stepped Kigumi cross-beams
  for (let i = 0; i < nTiers; i++) {
    const u = i / max(1, nTiers - 1);
    const y = baseY + i * stepY + 0.12;
    const len = span * (1.0 - 0.38 * u);
    const rot = i * twist + 0.08 * sin(t * 0.6 + i * 0.5);

    push();
    translate(0, y, 0);
    rotateY(rot);

    // Warm hinoki cypress beam pair along X
    fill(0.80 - 0.10 * u, 0.63 - 0.08 * u, 0.45 - 0.05 * u);
    push(); translate(0, 0, postOff); box(len, 0.085, 0.10); pop();
    push(); translate(0, 0, -postOff); box(len, 0.085, 0.10); pop();

    // Cross-lapped walnut beam pair along Z
    fill(0.56, 0.37, 0.20);
    push(); translate(postOff, 0.07, 0); box(0.10, 0.085, len); pop();
    push(); translate(-postOff, 0.07, 0); box(0.10, 0.085, len); pop();

    // Brushed brass locking dowels at each mortise intersection
    fill(0.86, 0.68, 0.32);
    for (let sx = -1; sx <= 1; sx += 2) {
      for (let sz = -1; sz <= 1; sz += 2) {
        push();
        translate(sx * postOff, 0.035, sz * postOff);
        box(0.04, 0.16, 0.04);
        pop();
      }
    }
    pop();
  }
}