// Suprematist Blocks Kinetic Intersecting Cuboids 3D - Infinite Library (Sketch 3D)
param("spin", 0.32, 0.1, 1.2);
param("scale_factor", 1.18, 0.6, 2.0);
param("dispersion", 1.10, 0.5, 2.0);
param("cuboid_tiers", 7.0, 3.0, 10.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.46 + sin(time * 0.12) * 0.04);
  rotateY(time * 0.28 + 0.55);

  // 1. Heavy Stepped Orthogonal Plinth Base (#0E0F12)
  push();
  translate(0, 1.25 * scale_factor, 0);
  fill(0.067, 0.067, 0.067);
  box(2.8 * scale_factor, 0.14 * scale_factor, 2.4 * scale_factor);

  translate(0, -0.10 * scale_factor, 0);
  fill(0.18, 0.19, 0.22);
  box(2.2 * scale_factor, 0.08 * scale_factor, 1.8 * scale_factor);
  pop();

  // 2. Interlocking Orthogonal Cuboids Ring
  const nPylons = 6;
  for (let i = 0; i < nPylons; i++) {
    const angle = (i / nPylons) * Math.PI * 2.0;
    const rDist = 0.88 * dispersion * scale_factor;
    const px = cos(angle) * rDist;
    const pz = sin(angle) * rDist;
    const pylonH = (1.4 + ((i % 3) * 0.32)) * scale_factor;

    push();
    translate(px, 0.25 * scale_factor, pz);

    if (i % 3 === 0) {
      fill(0.957, 0.937, 0.902);
    } else if (i % 3 === 1) {
      fill(0.851, 0.137, 0.122);
    } else {
      fill(0.102, 0.231, 0.545);
    }
    box(0.26 * scale_factor, pylonH, 0.26 * scale_factor);

    translate(0, -pylonH * 0.5 - 0.04 * scale_factor, 0);
    fill(0.957, 0.706, 0.102);
    box(0.38 * scale_factor, 0.08 * scale_factor, 0.38 * scale_factor);
    pop();
  }

  // 3. Central Monolithic Core Slab
  push();
  translate(0, 0.15 * scale_factor, 0);
  fill(0.067, 0.067, 0.067);
  box(0.80 * scale_factor, 2.3 * scale_factor, 0.80 * scale_factor);

  translate(0, 0, 0.42 * scale_factor);
  fill(0.851, 0.137, 0.122);
  box(0.38 * scale_factor, 2.1 * scale_factor, 0.08 * scale_factor);
  pop();

  // 4. Intersecting Transverse Floating Cantilevers
  const nTiers = floor(cuboid_tiers);
  for (let k = 0; k < nTiers; k++) {
    const frac = k / nTiers;
    const yPos = (0.80 - k * 0.35) * scale_factor;
    const rot = k * 0.48;
    const slabW = (2.0 - frac * 0.4) * scale_factor;
    const slabD = (0.60 - frac * 0.12) * scale_factor;

    push();
    translate(0, yPos, 0);
    rotateY(rot);

    if (k % 4 === 0) {
      fill(0.957, 0.937, 0.902);
    } else if (k % 4 === 1) {
      fill(0.851, 0.137, 0.122);
    } else if (k % 4 === 2) {
      fill(0.067, 0.067, 0.067);
    } else {
      fill(0.957, 0.706, 0.102);
    }
    box(slabW, 0.12 * scale_factor, slabD);

    translate(0, 0.07 * scale_factor, 0);
    fill(0.102, 0.231, 0.545);
    box(slabW * 0.4, 0.04 * scale_factor, slabD * 0.8);
    pop();
  }

  // 5. Diagonal Dynamic Struts
  push();
  translate(0.62 * scale_factor, -0.08 * scale_factor, 0.38 * scale_factor);
  rotateZ(0.48);
  fill(0.851, 0.137, 0.122);
  box(0.12 * scale_factor, 2.0 * scale_factor, 0.12 * scale_factor);
  pop();

  push();
  translate(-0.62 * scale_factor, -0.04 * scale_factor, -0.35 * scale_factor);
  rotateZ(-0.45);
  fill(0.957, 0.937, 0.902);
  box(0.14 * scale_factor, 1.9 * scale_factor, 0.14 * scale_factor);
  pop();

  // 6. Crown Finial
  push();
  translate(0, -1.20 * scale_factor, 0);
  fill(0.957, 0.937, 0.902);
  box(0.95 * scale_factor, 0.12 * scale_factor, 0.95 * scale_factor);

  translate(0, -0.10 * scale_factor, 0);
  fill(0.851, 0.137, 0.122);
  box(0.55 * scale_factor, 0.08 * scale_factor, 0.55 * scale_factor);

  translate(0, -0.08 * scale_factor, 0);
  fill(0.067, 0.067, 0.067);
  box(0.25 * scale_factor, 0.08 * scale_factor, 0.25 * scale_factor);
  pop();

  pop();
}
