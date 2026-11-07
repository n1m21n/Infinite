// Holographic Iridescent Prismatic Gem 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Floating prismatic crystal cluster with interlocking geometric pyramids, radiant chromatic dispersion planes, and luminescent pastel edge boundaries.
param("cluster_count", 8.0, 4.0, 14.0);
param("spread", 1.15, 0.5, 2.0);
param("spin", 0.38, 0.1, 1.0);
param("gem_aspect", 1.3, 0.5, 2.5);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.44 + sin(time * 0.18) * 0.04);
  rotateY(time * 0.28 + 0.8);

  // Obsidian foundation plinth (#090910)
  push();
  translate(0, 1.15, 0);
  fill(0.035, 0.035, 0.063);
  box(2.4 * spread, 0.14, 2.4 * spread);
  pop();

  // Central monolithic crystal spire
  push();
  translate(0, 0.0, 0);
  rotateY(time * 0.4);
  fill(0.94, 0.96, 1.0);
  box(0.65 * spread, 1.6 * gem_aspect, 0.65 * spread);
  pop();

  // Orbiting crystalline satellite prisms with pastel iridescent dispersion
  const count = floor(cluster_count);
  for (let i = 0; i < count; i++) {
    const ang = (i / count) * Math.PI * 2.0;
    const dist = (0.85 + (i % 2) * 0.35) * spread;
    const yTier = 0.45 - (i / count) * 0.9;

    push();
    translate(cos(ang + time * 0.3) * dist, yTier, sin(ang + time * 0.3) * dist);
    rotateY(ang + time * 0.6);
    rotateZ(0.25 * (i % 2 === 0 ? 1 : -1));

    // Chromatic dispersion tones
    if (i % 3 === 0) fill(0.663, 0.941, 1.0);
    else if (i % 3 === 1) fill(1.0, 0.702, 0.961);
    else fill(0.839, 1.0, 0.620);

    box(0.35 * spread, 0.85 * gem_aspect, 0.35 * spread);
    pop();
  }

  // Interlocking upper cantilever gem cap
  push();
  translate(0, -1.0 * gem_aspect, 0);
  rotateY(-time * 0.6);
  fill(1.0, 0.702, 0.961);
  box(0.85 * spread, 0.22, 0.85 * spread);
  pop();

  pop();
}
