// Neo-Brutalist Stepped Colonnade Plinth 3D - Infinite Library (Sketch 3D)
param("spin", 0.35, 0.1, 1.2);
param("scale_factor", 1.20, 0.6, 2.0);
param("colonnade_span", 1.25, 0.6, 2.2);
param("tiers", 6.0, 3.0, 9.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.48 + sin(time * 0.12) * 0.03);
  rotateY(time * 0.28 + 0.62);

  // 1. Massive Stepped Base Foundation Plinths (#22252A to #DCE0E8)
  push();
  translate(0, 1.22 * scale_factor, 0);
  fill(0.122, 0.133, 0.153);
  box(2.8 * scale_factor, 0.14 * scale_factor, 2.8 * scale_factor);

  translate(0, -0.10 * scale_factor, 0);
  fill(0.243, 0.259, 0.290);
  box(2.3 * scale_factor, 0.10 * scale_factor, 2.3 * scale_factor);

  translate(0, -0.08 * scale_factor, 0);
  fill(0.420, 0.443, 0.478);
  box(1.8 * scale_factor, 0.08 * scale_factor, 1.8 * scale_factor);
  pop();

  // 2. Colonnade of Monolithic Pylons with Heavy Concrete Lintels
  const nPylons = 8;
  for (let i = 0; i < nPylons; i++) {
    const angle = (i / nPylons) * Math.PI * 2.0;
    const rDist = 0.95 * colonnade_span * scale_factor;
    const px = cos(angle) * rDist;
    const pz = sin(angle) * rDist;
    const pylonH = (1.5 + ((i % 3) * 0.35)) * scale_factor;

    push();
    translate(px, 0.30 * scale_factor, pz);

    if (i % 2 === 0) {
      fill(0.855, 0.871, 0.898); // Light concrete pylon
    } else {
      fill(0.784, 0.294, 0.192); // Terracotta accent pylon
    }
    box(0.24 * scale_factor, pylonH, 0.24 * scale_factor);

    // Overhanging bracket block on top
    translate(0, -pylonH * 0.5 - 0.05 * scale_factor, 0);
    fill(0.122, 0.133, 0.153);
    box(0.36 * scale_factor, 0.10 * scale_factor, 0.36 * scale_factor);
    pop();
  }

  // 3. Central Monolithic Bastion Core
  push();
  translate(0, 0.12 * scale_factor, 0);
  fill(0.420, 0.443, 0.478);
  box(0.85 * scale_factor, 2.1 * scale_factor, 0.85 * scale_factor);

  // Terracotta vertical shear wall
  translate(0, 0, 0.44 * scale_factor);
  fill(0.784, 0.294, 0.192);
  box(0.40 * scale_factor, 2.2 * scale_factor, 0.10 * scale_factor);
  pop();

  // 4. Interlocking Cantilevered Terrace Decks & Perimeter Lintels
  const nTiers = floor(tiers);
  for (let k = 0; k < nTiers; k++) {
    const frac = k / nTiers;
    const yPos = (0.80 - k * 0.36) * scale_factor;
    const rot = k * 0.45;
    const deckW = (1.8 - frac * 0.35) * scale_factor;
    const deckD = (0.65 - frac * 0.15) * scale_factor;

    push();
    translate(0, yPos, 0);
    rotateY(rot);

    if (k % 2 === 0) {
      fill(0.855, 0.871, 0.898); // Crisp pale concrete
    } else {
      fill(0.784, 0.294, 0.192); // Terracotta lintel
    }
    box(deckW, 0.12 * scale_factor, deckD);

    // Shadow bracket underneath
    translate(0, 0.08 * scale_factor, 0);
    fill(0.122, 0.133, 0.153);
    box(deckW * 0.88, 0.04 * scale_factor, deckD * 0.88);
    pop();
  }

  // 5. Diagonal Structural Flying Buttresses
  push();
  translate(0.65 * scale_factor, -0.10 * scale_factor, 0.45 * scale_factor);
  rotateZ(0.46);
  fill(0.784, 0.294, 0.192);
  box(0.14 * scale_factor, 1.9 * scale_factor, 0.14 * scale_factor);
  pop();

  push();
  translate(-0.65 * scale_factor, -0.05 * scale_factor, -0.40 * scale_factor);
  rotateZ(-0.42);
  fill(0.855, 0.871, 0.898);
  box(0.16 * scale_factor, 1.8 * scale_factor, 0.14 * scale_factor);
  pop();

  // 6. Crown Observation Stele & Canopy
  push();
  translate(0, -1.18 * scale_factor, 0);
  fill(0.855, 0.871, 0.898);
  box(1.05 * scale_factor, 0.12 * scale_factor, 1.05 * scale_factor);

  translate(0, -0.10 * scale_factor, 0);
  fill(0.784, 0.294, 0.192);
  box(0.60 * scale_factor, 0.08 * scale_factor, 0.60 * scale_factor);

  translate(0, -0.08 * scale_factor, 0);
  fill(0.122, 0.133, 0.153);
  box(0.28 * scale_factor, 0.08 * scale_factor, 0.28 * scale_factor);
  pop();

  pop();
}
