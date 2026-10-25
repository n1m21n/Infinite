// Retro Surreal Monolith 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Metaphysical surrealist architectural plinth with checkered perspective plane, hovering metallic polyhedral relic, and dramatic cast shadow arches.
param("monolith_scale", 1.25, 0.5, 3.0);
param("levitation_height", 0.85, 0.2, 2.0);
param("arch_count", 5.0, 2.0, 10.0);
param("spin_rate", 0.45, 0.0, 2.0);
param("tilt_angle", 0.38, -0.8, 0.8);

function draw(t) {
  push();
  rotateY(t * spin_rate);
  rotateX(tilt_angle + sin(t * 0.3) * 0.08);

  // 1. Surrealist Metaphysical Checkered Floor Grid
  const gridN = 8;
  const cellSize = 0.55;
  const floorY = -1.2;

  for (let x = -gridN / 2; x < gridN / 2; x++) {
    for (let z = -gridN / 2; z < gridN / 2; z++) {
      const isBlack = (abs(x + z) % 2 === 0);
      if (isBlack) {
        fill(0.12, 0.14, 0.24); // Dark basalt midnight tile
      } else {
        fill(0.655, 0.694, 1.0); // Signal marble tile
      }
      push();
      translate(x * cellSize + cellSize * 0.5, floorY, z * cellSize + cellSize * 0.5);
      box(cellSize * 0.95, 0.04, cellSize * 0.95);
      pop();
    }
  }

  // 2. Classical Metaphysical Colonnade Arches (de Chirico arcade perspective)
  const arches = floor(arch_count);
  for (let a = 0; a < arches; a++) {
    const archZ = (a - arches * 0.5) * 0.9;
    const archX = -1.8;

    fill(0.24, 0.28, 0.42); // Shadowed terracotta slate

    // Left arcade column
    push();
    translate(archX, floorY + 0.8, archZ);
    box(0.15, 1.6, 0.15);
    pop();

    // Right arcade column
    push();
    translate(archX, floorY + 0.8, archZ + 0.6);
    box(0.15, 1.6, 0.15);
    pop();

    // Arch header lintel
    push();
    translate(archX, floorY + 1.65, archZ + 0.3);
    box(0.18, 0.14, 0.75);
    pop();
  }

  // 3. Central Monumental Stepped Pedestal
  fill(0.18, 0.21, 0.35);
  push();
  translate(0, floorY + 0.15, 0);
  box(1.4, 0.25, 1.4);
  translate(0, 0.2, 0);
  box(1.0, 0.2, 1.0);
  translate(0, 0.18, 0);
  box(0.65, 0.18, 0.65);
  pop();

  // 4. Hovering Metallic Polyhedral Relic (Surrealist Focus)
  const hoverY = floorY + 0.65 + levitation_height + sin(t * 1.8) * 0.12;

  push();
  translate(0, hoverY, 0);
  rotateY(t * 1.2);
  rotateZ(t * 0.8);
  rotateX(0.4);

  // Outer faceted frame
  fill(1.0, 0.42, 0.21); // Ember core facets
  box(0.55 * monolith_scale, 0.55 * monolith_scale, 0.55 * monolith_scale);

  // Cross-rotated metallic prism
  rotateY(PI * 0.25);
  rotateX(PI * 0.25);
  fill(0.306, 0.949, 0.722); // Emerald interlocking prism
  box(0.4 * monolith_scale, 0.75 * monolith_scale, 0.4 * monolith_scale);

  pop();

  // 5. Projected Metaphysical Ground Shadow
  fill(0.04, 0.05, 0.1, 0.85); // High-contrast cast shadow
  push();
  translate(0.3, floorY + 0.05, 0.4);
  box(0.8, 0.01, 0.8);
  pop();

  pop();
}
