// Holographic Iridescent Prismatic Polytope 3D - Infinite Library (Sketch 3D)
param("spin", 0.40, 0.1, 1.5);
param("scale_factor", 1.25, 0.5, 2.5);
param("facets", 8.0, 4.0, 16.0);
param("ring_density", 4.0, 1.0, 6.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.45 + sin(time * 0.22) * 0.08);
  rotateY(time * 0.38 + 0.5);

  // Central prismatic crystal polytope core (#ffffff)
  fill(0.96, 0.98, 1.0);
  box(0.58 * scale_factor, 0.58 * scale_factor, 0.58 * scale_factor);

  // Radial dispersion crystal facets projecting outward
  const numFacets = floor(facets);
  for (let i = 0; i < numFacets; i++) {
    const angle = (i / numFacets) * Math.PI * 2.0;
    push();
    rotateZ(angle);
    rotateY(i * 0.5 + time * 0.1);
    translate(0.68 * scale_factor, 0, 0);

    if (i % 3 === 0) {
      fill(0.663, 0.941, 1.0); // Soft cyan/pearl (#a9f0ff)
    } else if (i % 3 === 1) {
      fill(1.0, 0.702, 0.961); // Iridescent pink (#ffb3f5)
    } else {
      fill(0.839, 1.0, 0.620); // Mint green (#d6ff9e)
    }
    box(0.62 * scale_factor, 0.12 * scale_factor, 0.12 * scale_factor);
    pop();
  }

  // Interlocking orbital holographic rings
  const nRings = floor(ring_density);
  for (let r = 0; r < nRings; r++) {
    push();
    rotateX(0.8 * r + time * 0.18);
    rotateY(1.1 * r + time * 0.26);

    const rad = (0.98 + r * 0.32) * scale_factor;
    const segments = 24;
    for (let s = 0; s < segments; s++) {
      const a = (s / segments) * Math.PI * 2.0;
      const x = Math.cos(a) * rad;
      const y = Math.sin(a) * rad;

      push();
      translate(x, y, 0);
      rotateZ(a);
      if (r % 2 === 0) {
        fill(1.0, 0.702, 0.961); // Pink
      } else {
        fill(0.663, 0.941, 1.0); // Cyan
      }
      box(0.16 * scale_factor, 0.05 * scale_factor, 0.05 * scale_factor);
      pop();
    }
    pop();
  }

  pop();
}
