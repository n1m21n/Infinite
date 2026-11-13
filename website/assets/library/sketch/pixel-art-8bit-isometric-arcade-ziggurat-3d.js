// Pixel Art 8-Bit Isometric Arcade Ziggurat 3D - Infinite Library (Sketch 3D)
param("spin", 0.32, 0.1, 1.2);
param("scale_factor", 1.12, 0.6, 2.0);
param("terrace_steps", 8.0, 4.0, 12.0);
param("satellite_orbit", 1.05, 0.5, 1.8);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.50 + sin(time * 0.14) * 0.03);
  rotateY(time * 0.32 + 0.78);

  // Game Boy DMG 4-Tone Green Palette
  // Shade 0: rgb(0.059, 0.220, 0.059)
  // Shade 1: rgb(0.188, 0.384, 0.188)
  // Shade 2: rgb(0.545, 0.675, 0.059)
  // Shade 3: rgb(0.608, 0.737, 0.059)

  // 1. Massive Terraced Voxel Ziggurat Pyramid
  const nSteps = floor(terrace_steps);
  const baseSize = 2.6 * scale_factor;
  const stepH = (1.8 / nSteps) * scale_factor;

  for (let k = 0; k < nSteps; k++) {
    const frac = k / nSteps;
    const yPos = (0.90 - k * stepH) * scale_factor;
    const curW = baseSize * (1.0 - frac * 0.78);
    const curD = curW;

    push();
    translate(0, yPos, 0);

    // Alternate shading through the 4-tone palette
    if (k % 4 === 0) {
      fill(0.059, 0.220, 0.059);
    } else if (k % 4 === 1) {
      fill(0.188, 0.384, 0.188);
    } else if (k % 4 === 2) {
      fill(0.545, 0.675, 0.059);
    } else {
      fill(0.608, 0.737, 0.059);
    }
    box(curW, stepH * 0.95, curD);

    // Stepped Axial Grand Staircase on North/South Faces
    fill(0.608, 0.737, 0.059);
    const stairW = 0.32 * scale_factor;
    translate(0, 0, curD * 0.51);
    box(stairW, stepH * 0.7, 0.12 * scale_factor);
    translate(0, 0, -curD * 1.02);
    box(stairW, stepH * 0.7, 0.12 * scale_factor);
    pop();
  }

  // 2. Summit Altar & Ceremonial Portal Sanctuary
  const summitY = (0.90 - nSteps * stepH - 0.18) * scale_factor;
  push();
  translate(0, summitY, 0);
  fill(0.188, 0.384, 0.188);
  box(0.72 * scale_factor, 0.32 * scale_factor, 0.72 * scale_factor);

  // Portal Pillars
  translate(-0.24 * scale_factor, -0.25 * scale_factor, 0);
  fill(0.545, 0.675, 0.059);
  box(0.14 * scale_factor, 0.40 * scale_factor, 0.14 * scale_factor);
  translate(0.48 * scale_factor, 0, 0);
  box(0.14 * scale_factor, 0.40 * scale_factor, 0.14 * scale_factor);

  // Lintel Beam
  translate(-0.24 * scale_factor, -0.22 * scale_factor, 0);
  fill(0.608, 0.737, 0.059);
  box(0.65 * scale_factor, 0.12 * scale_factor, 0.22 * scale_factor);

  // Floating Apex Voxel Beacon
  translate(0, -0.25 * scale_factor, 0);
  fill(0.608, 0.737, 0.059);
  box(0.18 * scale_factor, 0.18 * scale_factor, 0.18 * scale_factor);
  pop();

  // 3. Four Orbiting Modular Voxel Satellites
  const orbitRad = 1.65 * satellite_orbit * scale_factor;
  for (let s = 0; s < 4; s++) {
    const ang = time * 0.85 + (s / 4) * 6.2831853;
    const sx = cos(ang) * orbitRad;
    const sz = sin(ang) * orbitRad;
    const sy = (sin(time * 1.2 + s * 1.5) * 0.25 - 0.1) * scale_factor;

    push();
    translate(sx, sy, sz);
    rotateY(ang * 2.0);
    fill(0.608, 0.737, 0.059);
    box(0.24 * scale_factor, 0.24 * scale_factor, 0.24 * scale_factor);

    translate(0, 0.18 * scale_factor, 0);
    fill(0.188, 0.384, 0.188);
    box(0.12 * scale_factor, 0.18 * scale_factor, 0.12 * scale_factor);
    pop();
  }

  pop();
}
