// Pixel Art 8-Bit Voxel Monolith Citadel 3D - Infinite Library (Sketch 3D)
param("spin", 0.35, 0.1, 1.2);
param("scale_factor", 1.15, 0.6, 2.0);
param("tower_height", 1.25, 0.6, 2.0);
param("cantilever", 0.95, 0.4, 1.6);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.46 + sin(time * 0.12) * 0.04);
  rotateY(time * 0.35 + 0.68);

  // Authentic Game Boy DMG 4-Tone Green Palette
  // Shade 0 (darkest shadow):  #0f380f -> rgb(0.059, 0.220, 0.059)
  // Shade 1 (dark moss):       #306230 -> rgb(0.188, 0.384, 0.188)
  // Shade 2 (phosphor olive):  #8bac0f -> rgb(0.545, 0.675, 0.059)
  // Shade 3 (brightest lime):  #9bbc0f -> rgb(0.608, 0.737, 0.059)

  // 1. Massive Foundation Voxel Plinth & Ground Tier
  push();
  translate(0, 1.15 * scale_factor, 0);
  fill(0.059, 0.220, 0.059);
  box(2.8 * scale_factor, 0.22 * scale_factor, 2.8 * scale_factor);

  translate(0, -0.16 * scale_factor, 0);
  fill(0.188, 0.384, 0.188);
  box(2.3 * scale_factor, 0.18 * scale_factor, 2.3 * scale_factor);

  translate(0, -0.15 * scale_factor, 0);
  fill(0.545, 0.675, 0.059);
  box(1.9 * scale_factor, 0.14 * scale_factor, 1.9 * scale_factor);
  pop();

  // 2. Central Stepped Voxel Monolith Tower
  const nSegments = 7;
  for (let s = 0; s < nSegments; s++) {
    const frac = s / nSegments;
    const yPos = (0.75 - s * 0.30 * tower_height) * scale_factor;
    const segW = (1.55 - frac * 0.75) * scale_factor;
    const segD = (1.55 - frac * 0.75) * scale_factor;
    const segH = 0.24 * scale_factor;

    push();
    translate(0, yPos, 0);

    if (s % 3 === 0) {
      fill(0.188, 0.384, 0.188);
    } else if (s % 3 === 1) {
      fill(0.545, 0.675, 0.059);
    } else {
      fill(0.608, 0.737, 0.059);
    }
    box(segW, segH, segD);

    // Relief Voxel Cornice Trim
    translate(0, -segH * 0.45, 0);
    fill(0.059, 0.220, 0.059);
    box(segW * 1.08, segH * 0.25, segD * 1.08);
    pop();
  }

  // 3. Four Symmetrical Corner Bastion Pylons
  const cornerDist = 0.95 * cantilever * scale_factor;
  const bastionCorners = [
    [-cornerDist, -cornerDist],
    [cornerDist, -cornerDist],
    [cornerDist, cornerDist],
    [-cornerDist, cornerDist]
  ];

  for (let i = 0; i < bastionCorners.length; i++) {
    const bx = bastionCorners[i][0];
    const bz = bastionCorners[i][1];

    push();
    translate(bx, 0.35 * scale_factor, bz);
    fill(0.188, 0.384, 0.188);
    box(0.55 * scale_factor, 1.45 * scale_factor, 0.55 * scale_factor);

    // Cantilevered Upper Arcade Pod
    translate(0, -0.75 * scale_factor, 0);
    fill(0.545, 0.675, 0.059);
    box(0.72 * scale_factor, 0.30 * scale_factor, 0.72 * scale_factor);

    // Crenellation Turret Teeth
    translate(0, -0.18 * scale_factor, 0);
    fill(0.608, 0.737, 0.059);
    box(0.35 * scale_factor, 0.18 * scale_factor, 0.35 * scale_factor);
    pop();
  }

  // 4. Cantilevered Connecting Skybridges
  push();
  translate(0, 0.05 * scale_factor, 0);
  fill(0.545, 0.675, 0.059);
  box(cornerDist * 2.1, 0.12 * scale_factor, 0.28 * scale_factor);
  box(0.28 * scale_factor, 0.12 * scale_factor, cornerDist * 2.1);
  pop();

  // 5. Crown Finial Spire & Apex Voxel Crystal
  push();
  const crownY = (-0.75 - 0.28 * tower_height) * scale_factor;
  translate(0, crownY, 0);
  fill(0.608, 0.737, 0.059);
  box(0.42 * scale_factor, 0.45 * scale_factor, 0.42 * scale_factor);

  translate(0, -0.32 * scale_factor, 0);
  fill(0.545, 0.675, 0.059);
  box(0.22 * scale_factor, 0.38 * scale_factor, 0.22 * scale_factor);

  translate(0, -0.24 * scale_factor, 0);
  fill(0.608, 0.737, 0.059);
  box(0.12 * scale_factor, 0.22 * scale_factor, 0.12 * scale_factor);
  pop();

  pop();
}
