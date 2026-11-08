// Brutalist Architecture Cantilevered Stele 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Heroic brutalist monument stele with dramatic asymmetrical cantilever beams, interlocking precast concrete slabs, vertical shadow reveal slits, and raw stone massing.
param("spin", 0.35, 0.1, 1.2);
param("scale_factor", 1.15, 0.5, 2.0);
param("reach", 1.55, 0.8, 2.5);
param("beams", 5.0, 3.0, 8.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.50 + sin(time * 0.12) * 0.03);
  rotateY(time * 0.27 + 0.55);

  // 1. Massive stepped polygonal concrete podium
  push();
  translate(0, 1.25 * scale_factor, 0);
  fill(0.12, 0.12, 0.14); // Deep basalt plinth
  box(2.8 * scale_factor, 0.14, 2.8 * scale_factor);

  translate(0, -0.09, 0);
  fill(0.48, 0.48, 0.47); // Weathered aggregate sub-plinth
  box(2.2 * scale_factor, 0.06, 2.2 * scale_factor);

  translate(0, -0.05, 0);
  fill(0.86, 0.85, 0.82); // Poured lime terrace deck
  box(1.7 * scale_factor, 0.05, 1.7 * scale_factor);
  pop();

  // 2. Monolithic Central Core Stele Tower
  push();
  translate(0, (1.25 - 2.5 * 0.5) * scale_factor, 0);
  fill(0.86, 0.85, 0.82); // Pale raw concrete
  box(0.58 * scale_factor, 2.5 * scale_factor, 0.58 * scale_factor);

  // Deep vertical shadow reveal embrasures
  fill(0.10, 0.10, 0.12);
  box(0.40 * scale_factor, 2.4 * scale_factor, 0.64 * scale_factor);
  box(0.64 * scale_factor, 2.4 * scale_factor, 0.40 * scale_factor);
  pop();

  // 3. Asymmetric Heroic Cantilevered Wings (Alternating Concrete & Shadow Recesses)
  const nBeams = floor(beams);
  for (let b = 0; b < nBeams; b++) {
    const frac = b / nBeams;
    const yPos = (0.90 - frac * 1.75) * scale_factor;
    const rot = frac * Math.PI * 1.45 + time * 0.12;
    const len = (reach * (1.15 - frac * 0.25) + sin(b * 1.7) * 0.2) * scale_factor;

    push();
    translate(0, yPos, 0);
    rotateY(rot);

    // Cantilever post-tensioned beam in weathered aggregate
    push();
    translate(len * 0.5, 0, 0);
    fill(0.48, 0.48, 0.47);
    box(len, 0.15 * scale_factor, 0.26 * scale_factor);

    // Repeated modular coffered window cavities along beam
    const nWindows = 3;
    for (let w = 0; w < nWindows; w++) {
      push();
      translate(-len * 0.35 + w * (len * 0.32), 0, 0);
      fill(0.10, 0.10, 0.12);
      box(len * 0.18, 0.10 * scale_factor, 0.30 * scale_factor);
      pop();
    }

    // Terminal hanging modular pod block
    translate(len * 0.44, 0.16 * scale_factor, 0);
    fill(0.86, 0.85, 0.82);
    box(0.32 * scale_factor, 0.24 * scale_factor, 0.32 * scale_factor);

    // Deep window embrasure inside pod
    fill(0.10, 0.10, 0.12);
    box(0.18 * scale_factor, 0.14 * scale_factor, 0.34 * scale_factor);
    pop();

    // Diagonal structural support corbel / brace underneath
    push();
    translate(len * 0.32, 0.22 * scale_factor, 0);
    rotateZ(-0.46);
    fill(0.12, 0.12, 0.14);
    box(len * 0.65, 0.07 * scale_factor, 0.14 * scale_factor);
    pop();

    pop();
  }

  // 4. Diagonal Structural Exoskeleton Bracing Columns
  push();
  translate(0.65 * scale_factor, 0.25 * scale_factor, 0.40 * scale_factor);
  rotateZ(0.52);
  fill(0.48, 0.48, 0.47);
  box(0.10 * scale_factor, 2.1 * scale_factor, 0.10 * scale_factor);
  pop();

  push();
  translate(-0.65 * scale_factor, 0.25 * scale_factor, -0.40 * scale_factor);
  rotateZ(-0.52);
  fill(0.48, 0.48, 0.47);
  box(0.10 * scale_factor, 2.1 * scale_factor, 0.10 * scale_factor);
  pop();

  // 5. Heroic Monument Crown Finial & Stepped Parapet Array
  push();
  translate(0, (1.25 - 2.5) * scale_factor - 0.14 * scale_factor, 0);
  fill(0.86, 0.85, 0.82);
  box(0.85 * scale_factor, 0.14 * scale_factor, 0.85 * scale_factor);

  // Parapet slit embrasures
  translate(0, -0.10 * scale_factor, 0);
  fill(0.10, 0.10, 0.12);
  box(0.70 * scale_factor, 0.08 * scale_factor, 0.70 * scale_factor);

  // Spomenik cantilever crest finial
  translate(0.15 * scale_factor, -0.12 * scale_factor, 0.15 * scale_factor);
  fill(0.48, 0.48, 0.47);
  box(0.50 * scale_factor, 0.16 * scale_factor, 0.50 * scale_factor);
  pop();

  pop();
}
