// Ukiyo-e Cartography Topographic Peak 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Edo-period woodblock cartographic mountain relief with concentric stepped topographical strata, indigo marine foundations, and a vibrant cinnabar seal accent.
param("strata_count", 9.0, 5.0, 16.0);
param("peak_height", 1.1, 0.6, 2.0);
param("rotation_speed", 0.3, 0.05, 1.0);
param("contour_scale", 1.0, 0.5, 1.8);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.48 + sin(time * 0.18) * 0.05);
  rotateY(time * 0.35 + 0.5);

  // Base marine foundation plinth in deep indigo ink
  fill(0.114, 0.208, 0.341);
  push();
  translate(0, -0.72, 0);
  box(1.9 * contour_scale, 0.08, 1.9 * contour_scale);

  // Seafoam coastal wave contour shelf
  translate(0, 0.07, 0);
  fill(0.659, 0.855, 0.863);
  box(1.65 * contour_scale, 0.06, 1.65 * contour_scale);
  pop();

  // Stepped topographical mountain contour terraces
  const count = floor(strata_count);
  for (let i = 0; i < count; i++) {
    const f = i / count;
    const y = -0.58 + f * (1.1 * peak_height);
    const radius = (1.4 - f * 1.15) * contour_scale;

    push();
    translate(0, y, 0);

    // Alternating indigo and seafoam cartographic strata
    if (i % 2 === 0) {
      fill(0.114 + f * 0.08, 0.208 + f * 0.12, 0.341 + f * 0.15); // Indigo to ocean gradient
    } else {
      fill(0.659 - f * 0.15, 0.855 - f * 0.12, 0.863 - f * 0.10); // Seafoam contour
    }

    // Cylindrical stepped contour ring
    cylinder(radius, 0.08);

    // Decorative quadrant marker blocks
    for (let q = 0; q < 4; q++) {
      const qAng = (q * 1.5707963) + (i * 0.2);
      const qx = cos(qAng) * radius * 0.98;
      const qz = sin(qAng) * radius * 0.98;
      push();
      translate(qx, 0.02, qz);
      fill(0.945, 0.980, 0.933); // Rice paper cream marker
      box(0.06, 0.09, 0.06);
      pop();
    }

    pop();
  }

  // Apex cinnabar red Hanko seal stamp
  push();
  translate(0, -0.58 + 1.15 * peak_height, 0);
  fill(0.902, 0.224, 0.275); // Cinnabar Red Seal
  box(0.24 * contour_scale, 0.16, 0.24 * contour_scale);

  // Floating cinnabar crest ornament
  translate(0, 0.15, 0);
  fill(0.945, 0.980, 0.933);
  sphere(0.08 * contour_scale);
  pop();

  // Floating nautical archipelago markers in the surrounding waters
  for (let k = 0; k < 6; k++) {
    const kAng = (k * 6.2831853) / 6 + time * 0.5;
    const kDist = 1.15 * contour_scale;
    const kx = cos(kAng) * kDist;
    const kz = sin(kAng) * kDist;
    const ky = -0.62 + sin(time * 1.8 + k) * 0.04;

    push();
    translate(kx, ky, kz);
    fill(k % 2 === 0 ? 0.902 : 0.114, k % 2 === 0 ? 0.224 : 0.208, k % 2 === 0 ? 0.275 : 0.341);
    box(0.12, 0.14, 0.12);
    pop();
  }

  pop();
}
