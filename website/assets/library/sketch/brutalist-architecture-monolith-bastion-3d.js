// Brutalist Architecture Monolith Bastion 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Monolithic poured concrete bastion megastructure featuring heavy stepped plinths, modular recessed coffer chambers, cantilevered floor plates, and deep architectural shadow relief.
param("spin", 0.32, 0.1, 1.2);
param("scale_factor", 1.15, 0.5, 2.0);
param("mass_height", 2.4, 1.2, 3.8);
param("tiers", 4.0, 2.0, 7.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.48 + sin(time * 0.11) * 0.03);
  rotateY(time * 0.25 + 0.72);

  // 1. Heavy stepped asphalt & rough aggregate podium
  push();
  translate(0, 1.25 * scale_factor, 0);
  fill(0.133, 0.133, 0.133); // Basalt dark shadow floor (#222222)
  box(2.8 * scale_factor, 0.14, 2.8 * scale_factor);

  // Stepped sub-plinth in weathered concrete (#7a7a78)
  translate(0, -0.09, 0);
  fill(0.478, 0.478, 0.471);
  box(2.2 * scale_factor, 0.06, 2.2 * scale_factor);

  // Poured concrete courtyard plinth (#d9d6cf)
  translate(0, -0.05, 0);
  fill(0.851, 0.839, 0.812);
  box(1.8 * scale_factor, 0.05, 1.8 * scale_factor);
  pop();

  // 2. Central Monolithic Brutalist Concrete Core Tower
  push();
  translate(0, (1.25 - mass_height * 0.5) * scale_factor, 0);
  fill(0.851, 0.839, 0.812); // Pale poured cement
  box(0.70 * scale_factor, mass_height * scale_factor, 0.70 * scale_factor);

  // Vertical deep shadow reveal recesses
  fill(0.133, 0.133, 0.133);
  box(0.50 * scale_factor, mass_height * 0.90 * scale_factor, 0.76 * scale_factor);
  box(0.76 * scale_factor, mass_height * 0.70 * scale_factor, 0.50 * scale_factor);
  pop();

  // 3. Cantilevered Stepped Floor Bastions (Alternating Concrete & Shadow Relief)
  const nTiers = floor(tiers);
  for (let k = 0; k < nTiers; k++) {
    const frac = k / nTiers;
    const yPos = (0.95 - k * 0.52) * scale_factor;
    const shiftX = ((k % 2 === 0) ? 0.35 : -0.35) * (1.0 - frac * 0.25) * scale_factor;
    const shiftZ = ((k % 3 === 0) ? 0.25 : -0.25) * scale_factor;
    const slabW = (1.6 - frac * 0.35) * scale_factor;
    const slabD = (1.2 - frac * 0.25) * scale_factor;

    push();
    translate(shiftX, yPos, shiftZ);

    // Weathered aggregate cantilever slab
    fill(0.478, 0.478, 0.471);
    box(slabW, 0.16 * scale_factor, slabD);

    // Recessed modular coffered window cavities (repeated openings)
    push();
    translate(0, 0.12 * scale_factor, 0);
    fill(0.133, 0.133, 0.133);
    box(slabW * 0.82, 0.08 * scale_factor, slabD * 0.82);
    pop();

    // Structural corbel brackets underneath
    push();
    translate(0, 0.16 * scale_factor, 0);
    fill(0.851, 0.839, 0.812);
    box(slabW * 0.45, 0.08 * scale_factor, slabD * 0.45);
    pop();

    pop();
  }

  // 4. Massive Diagonal External Shear Trusses
  push();
  translate(0.55 * scale_factor, 0.25 * scale_factor, 0.35 * scale_factor);
  rotateZ(0.50);
  fill(0.478, 0.478, 0.471);
  box(0.12 * scale_factor, 1.9 * scale_factor, 0.12 * scale_factor);
  pop();

  push();
  translate(-0.55 * scale_factor, 0.25 * scale_factor, -0.35 * scale_factor);
  rotateZ(-0.50);
  fill(0.478, 0.478, 0.471);
  box(0.12 * scale_factor, 1.9 * scale_factor, 0.12 * scale_factor);
  pop();

  // 5. Crown Observation Deck & Heavy Parapet Cap
  push();
  translate(0, (1.25 - mass_height) * scale_factor - 0.15 * scale_factor, 0);
  fill(0.851, 0.839, 0.812);
  box(0.85 * scale_factor, 0.14 * scale_factor, 0.85 * scale_factor);

  // Parapet slit opening
  translate(0, -0.12 * scale_factor, 0);
  fill(0.133, 0.133, 0.133);
  box(0.70 * scale_factor, 0.10 * scale_factor, 0.70 * scale_factor);
  pop();

  pop();
}
