// Thundr Brutalist Hazard Cantilever Tower 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Stepped industrial cantilever megastructure featuring suspended heavy concrete decks, alternating hazard chevron stripes, diagonal structural exoskeletons, and glowing telemetry beacon arrays.
param("spin", 0.34, 0.1, 1.2);
param("scale_factor", 1.15, 0.5, 2.0);
param("tier_offset", 0.42, 0.2, 0.8);
param("deck_tiers", 5.0, 3.0, 8.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.52 + sin(time * 0.13) * 0.04);
  rotateY(time * 0.26 + 0.55);

  // 1. Massive industrial stepped plinth
  push();
  translate(0, 1.25 * scale_factor, 0);
  fill(0.10, 0.11, 0.14); // Asphalt base
  box(2.7 * scale_factor, 0.14, 2.7 * scale_factor);

  translate(0, -0.09, 0);
  fill(0.78, 0.80, 0.84); // Concrete sub-deck
  box(2.1 * scale_factor, 0.06, 2.1 * scale_factor);
  pop();

  // 2. Stepped Cantilever Megastructure Decks (Alternating Concrete & Hazard Orange)
  const nTiers = floor(deck_tiers);
  for (let k = 0; k < nTiers; k++) {
    const frac = k / nTiers;
    const yPos = (1.05 - k * 0.48) * scale_factor;
    const shiftX = ((k % 2 === 0) ? tier_offset : -tier_offset) * (1.0 - frac * 0.3) * scale_factor;
    const shiftZ = ((k % 3 === 0) ? tier_offset * 0.5 : -tier_offset * 0.5) * scale_factor;
    const deckW = (1.5 - frac * 0.3) * scale_factor;
    const deckD = (1.1 - frac * 0.2) * scale_factor;

    push();
    translate(shiftX, yPos, shiftZ);

    // Primary cantilever deck slab
    if (k % 2 === 0) {
      fill(0.82, 0.84, 0.86); // Concrete slab
    } else {
      fill(1.0, 0.33, 0.0); // Hazard Orange accent deck
    }
    box(deckW, 0.14 * scale_factor, deckD);

    // Under-deck structural corbel brackets
    push();
    translate(0, 0.10 * scale_factor, 0);
    fill(0.18, 0.19, 0.22);
    box(deckW * 0.75, 0.08 * scale_factor, deckD * 0.75);
    pop();

    // Perimeter safety barrier with acid lime beacons
    push();
    translate(deckW * 0.46, -0.10 * scale_factor, 0);
    fill(0.78, 1.0, 0.0); // Electric Acid Lime
    box(0.04 * scale_factor, 0.08 * scale_factor, deckD * 0.85);
    pop();

    pop();
  }

  // 3. Exoskeleton Vertical Core Spine & Diagonal Truss Columns
  push();
  translate(-0.15 * scale_factor, 0.1 * scale_factor, -0.15 * scale_factor);
  fill(0.14, 0.15, 0.18); // Dark structural steel
  box(0.35 * scale_factor, 2.5 * scale_factor, 0.35 * scale_factor);
  pop();

  // Diagonal support trusses
  push();
  translate(0.6 * scale_factor, 0.35 * scale_factor, 0.3 * scale_factor);
  rotateZ(0.55);
  fill(0.28, 0.31, 0.36);
  box(0.08 * scale_factor, 1.8 * scale_factor, 0.08 * scale_factor);
  pop();

  push();
  translate(-0.6 * scale_factor, 0.35 * scale_factor, -0.3 * scale_factor);
  rotateZ(-0.55);
  fill(0.28, 0.31, 0.36);
  box(0.08 * scale_factor, 1.8 * scale_factor, 0.08 * scale_factor);
  pop();

  // 4. Rooftop Observation Deck & Beacon Spire
  push();
  translate(0, (1.05 - nTiers * 0.48) * scale_factor - 0.12 * scale_factor, 0);
  fill(0.82, 0.84, 0.86);
  box(0.60 * scale_factor, 0.10 * scale_factor, 0.60 * scale_factor);

  translate(0, -0.35 * scale_factor, 0);
  fill(1.0, 0.33, 0.0);
  box(0.08 * scale_factor, 0.65 * scale_factor, 0.08 * scale_factor);

  // Top flashing beacon
  translate(0, -0.35 * scale_factor, 0);
  fill(0.78, 1.0, 0.0);
  box(0.16 * scale_factor, 0.12 * scale_factor, 0.16 * scale_factor);
  pop();

  pop();
}
