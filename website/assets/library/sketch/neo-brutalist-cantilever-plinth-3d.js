// Neo-Brutalist Cantilever Plinth 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Monumental stepped cantilever plinths in raw cast stone, framed with industrial hazard orange datum lines and stark geometric projections.
param("tier_count", 6.0, 3.0, 10.0);
param("overhang_shift", 0.32, 0.1, 0.6);
param("plinth_width", 1.4, 0.6, 2.2);
param("rotation_speed", 0.38, 0.05, 1.2);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.48 + sin(time * 0.18) * 0.05);
  rotateY(time * 0.32 + 0.60);

  // Stepped foundation base
  push();
  translate(0, 0.92, 0);
  fill(0.18, 0.19, 0.22);
  box(2.4, 0.14, 2.4);
  pop();

  push();
  translate(0, 0.78, 0);
  fill(0.88, 0.87, 0.84);
  box(2.0, 0.14, 2.0);
  pop();

  const tiers = 6;
  const hTier = 0.22;
  for (let i = 0; i < tiers; i++) {
    const y = 0.58 - i * hTier * 1.15;
    const sign = (i % 2 === 0 ? 1 : -1);
    const xOff = sign * (i * overhang_shift * 0.42);
    const zOff = ((i % 3) - 1) * (i * overhang_shift * 0.28);
    const w = plinth_width - i * 0.08;

    push();
    translate(xOff, y, zOff);

    if (i === 1) {
      fill(1.0, 0.20, 0.0); // Hazard orange accent slab
    } else if (i === 3) {
      fill(0.0, 0.22, 0.95); // Ultramarine slab
    } else if (i % 2 === 0) {
      fill(0.92, 0.91, 0.88); // Crisp bright stone
    } else {
      fill(0.22, 0.23, 0.28); // Shadow graphite
    }
    box(w, hTier, w * 0.85);

    // Contrasting structural ribs
    for (let r = -2; r <= 2; r++) {
      push();
      translate(r * 0.22, 0, w * 0.44);
      fill(i % 2 === 0 ? 0.20 : 0.90, i % 2 === 0 ? 0.21 : 0.89, i % 2 === 0 ? 0.25 : 0.86);
      box(0.05, hTier * 1.05, 0.08);
      pop();
    }

    pop();
  }

  // Flanking vertical pylon
  push();
  translate(-plinth_width * 0.85, 0.0, -0.3);
  fill(0.88, 0.87, 0.84);
  box(0.24, 1.8, 0.65);
  pop();

  // Top cantilever cornice with orange datum
  push();
  translate(0.35, -0.85, 0);
  fill(1.0, 0.20, 0.0);
  box(1.2, 0.08, 0.9);
  pop();

  pop();
}
