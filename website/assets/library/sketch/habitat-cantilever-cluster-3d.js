// Habitat Cantilever Cluster 3D - Infinite Library (Sketch 3D)
param("tiers", 4, 2, 6);
param("cantilever", 0.48, 0.2, 0.85);
param("orbit", 0.14, 0.0, 0.5);

function draw(t) {
  const nTiers = floor(tiers);

  push();
  scale(0.62, 0.62, 0.62);
  rotateX(-0.34 + sin(t * 0.2) * 0.05);
  rotateY(0.58 + t * orbit);

  // Brutalist concrete promenade plinth
  push();
  translate(0, -1.05, 0);
  fill(0.32, 0.30, 0.28);
  box(3.8, 0.14, 3.4);
  pop();

  // Clustered precast concrete housing modules
  for (let tier = 0; tier < nTiers; tier++) {
    const y = -0.68 + tier * 0.52;
    const count = max(2, 5 - tier);
    for (let i = 0; i < count; i++) {
      const u = (i - (count - 1) * 0.5);
      const x = u * 0.84 + ((tier % 2 === 0) ? 0.22 : -0.22) * cantilever;
      const z = sin(tier * 1.7 + i * 2.1) * 0.65;
      const rot = ((i + tier) % 2 === 0) ? 0.0 : 1.5708;

      push();
      translate(x, y, z);
      rotateY(rot);

      // Main precast limestone-concrete module (#9C9488 -> #6E685F)
      const shade = 0.64 - tier * 0.03 + (i % 2) * 0.06;
      fill(shade, shade * 0.96, shade * 0.90);
      box(0.88, 0.44, 0.58);

      // Recessed dark glazing bay
      push();
      translate(0, 0.02, 0.28);
      fill(0.13, 0.15, 0.18);
      box(0.56, 0.24, 0.05);
      pop();

      // Cantilevered terrace parapet cap
      push();
      translate(0, 0.24, 0);
      fill(0.74, 0.71, 0.66);
      box(0.92, 0.04, 0.62);
      pop();

      pop();
    }
  }

  // Elevated concrete skywalk bridge
  push();
  translate(0, 0.12, 0.15);
  fill(0.54, 0.51, 0.47);
  box(2.6, 0.10, 0.26);
  pop();

  pop();
}