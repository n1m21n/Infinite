// Dieste Gaussian Brick Vault 3D - Infinite Library (Sketch 3D)
param("bays", 5, 3, 8);
param("arch_rise", 1.15, 0.6, 1.8);
param("wave_amp", 0.28, 0.05, 0.55);
param("orbit", 0.25, 0.0, 1.0);

function draw(t) {
  // Architectural three-quarter elevation framing
  scale(0.56);
  rotateX(0.28 + 0.04 * sin(t * 0.35));
  rotateY(0.62 + 0.18 * sin(t * orbit * 0.5));
  translate(0, -0.65, 0);

  const nBays = floor(bays);
  const bayLen = 0.62;
  const totalZ = nBays * bayLen;
  const spanX = 2.5;
  const ribsPerBay = 13;

  // Travertine plinth & floor slab (#D9CBB8)
  fill(0.851, 0.796, 0.722);
  push();
  translate(0, -0.06, 0);
  box(spanX + 0.65, 0.12, totalZ + 0.65);
  pop();

  // Flanking dark masonry buttress piers (#2B1D17)
  for (let b = 0; b <= nBays; b++) {
    const z = -totalZ * 0.5 + b * bayLen;
    fill(0.22, 0.15, 0.12);
    push();
    translate(-spanX * 0.5 - 0.14, 0.52, z);
    box(0.24, 1.04, 0.22);
    pop();
    push();
    translate(spanX * 0.5 + 0.14, 0.52, z);
    box(0.24, 1.04, 0.22);
    pop();
  }

  // Undulating Gaussian double-curved ceramic brick vault shells
  for (let b = 0; b < nBays; b++) {
    const zStart = -totalZ * 0.5 + b * bayLen;
    // Mortar/concrete edge arch rib between bays (#D9CBB8)
    fill(0.82, 0.75, 0.66);
    for (let s = 0; s < 14; s++) {
      const u = (s + 0.5) / 14.0;
      const x = (u - 0.5) * spanX;
      const y = 0.85 + arch_rise * (1.0 - pow(2.0 * u - 1.0, 2.0));
      const slope = -arch_rise * 4.0 * (2.0 * u - 1.0) / spanX;
      push();
      translate(x, y, zStart);
      rotateZ(atan(slope));
      box(spanX / 13.0, 0.08, 0.09);
      pop();
    }

    // Brick courses across each Gaussian bay
    for (let r = 1; r < 6; r++) {
      const v = r / 6.0;
      const z = zStart + v * bayLen;
      const crownBoost = sin(v * PI) * wave_amp;
      for (let s = 0; s < ribsPerBay; s++) {
        const u = (s + 0.5 + (r % 2) * 0.15) / ribsPerBay;
        if (u > 0.98) continue;
        const x = (u - 0.5) * spanX;
        const arch = (1.0 - pow(2.0 * u - 1.0, 2.0));
        const y = 0.85 + (arch_rise + crownBoost) * arch;
        const slope = -(arch_rise + crownBoost) * 4.0 * (2.0 * u - 1.0) / spanX;

        // Alternate warm terracotta & burnt sienna brick courses
        const shade = 0.5 + 0.5 * sin(s * 1.7 + r * 2.3 + b);
        if (shade > 0.45) {
          fill(0.659, 0.294, 0.165);
        } else {
          fill(0.471, 0.200, 0.106);
        }
        push();
        translate(x, y, z);
        rotateZ(atan(slope));
        box(spanX / ribsPerBay * 1.12, 0.11, bayLen / 6.0 * 1.12);
        pop();
      }
    }
  }
}
