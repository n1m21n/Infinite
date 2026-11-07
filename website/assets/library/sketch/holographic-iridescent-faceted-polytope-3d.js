// Holographic Iridescent Faceted Polytope 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Complex rotating 3D polytope with crystalline multifaceted geometry, chromatic spectral dispersion facets, and iridescent pastel gradient layers on deep obsidian.
param("facets", 10.0, 6.0, 16.0);
param("rotation_speed", 0.4, 0.1, 1.2);
param("dispersion_scale", 1.2, 0.5, 2.2);
param("facet_depth", 0.85, 0.3, 1.8);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.48 + sin(time * 0.16) * 0.05);
  rotateY(time * 0.32 + 0.6);

  // Deep obsidian pedestal base slab (#0c0c14)
  push();
  translate(0, 1.1, 0);
  fill(0.047, 0.047, 0.078);
  box(2.2 * dispersion_scale, 0.15, 2.2 * dispersion_scale);
  pop();

  // Primary central crystalline dodecahedral core (iridescent white-cyan #e8f9ff)
  push();
  translate(0, 0.05, 0);
  fill(0.91, 0.97, 1.0);
  box(0.95 * dispersion_scale, 0.95 * dispersion_scale, 0.95 * dispersion_scale);
  pop();

  // Multi-faceted chromatic dispersion satellite planes
  const facetCount = floor(facets);
  for (let i = 0; i < facetCount; i++) {
    const ang = (i / facetCount) * Math.PI * 2.0;
    const r = 1.15 * dispersion_scale;
    const yOff = sin(time * 0.8 + i * 0.6) * 0.28;

    push();
    translate(cos(ang) * r, yOff, sin(ang) * r);
    rotateY(-ang + time * 0.4);
    rotateZ(0.35 * sin(time + i));

    // Pastel iridescent spectrum rotation:
    // Cyan (#a9f0ff), Magenta-Pink (#ffb3f5), Lime (#d6ff9e), Lavender (#d4c5ff)
    const phase = (i % 4);
    if (phase === 0) fill(0.663, 0.941, 1.0);
    else if (phase === 1) fill(1.0, 0.702, 0.961);
    else if (phase === 2) fill(0.839, 1.0, 0.620);
    else fill(0.831, 0.773, 1.0);

    box(0.18 * facet_depth, 0.75 * dispersion_scale, 0.48 * dispersion_scale);
    pop();
  }

  // Floating crystalline top crown pyramid
  push();
  translate(0, -0.85 * dispersion_scale, 0);
  rotateY(-time * 0.5);
  fill(1.0, 0.85, 0.95);
  box(0.55 * dispersion_scale, 0.55 * dispersion_scale, 0.55 * dispersion_scale);
  pop();

  // Radiating prismatic edge struts
  for (let j = 0; j < 4; j++) {
    const a = j * Math.PI * 0.5 + time * 0.2;
    push();
    translate(cos(a) * 0.7, 0.05, sin(a) * 0.7);
    rotateY(a);
    fill(0.663, 0.941, 1.0);
    box(0.08, 1.25 * dispersion_scale, 0.08);
    pop();
  }

  pop();
}
