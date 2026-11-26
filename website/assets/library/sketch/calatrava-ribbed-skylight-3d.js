// Calatrava Ribbed Skylight 3D - Infinite Library (Sketch 3D)
param("arches", 14, 8, 24);
param("span", 2.5, 1.6, 3.6);
param("archHeight", 1.35, 0.8, 2.0);
param("spin", 0.14, 0.0, 0.5);

function draw(t) {
  const n = floor(arches);
  rotateY(t * spin + 0.48);
  rotateX(0.24);

  // Dark slate concourse plinth
  push();
  translate(0, -0.76, 0);
  fill(0.078, 0.094, 0.114);
  box(span * 1.18, 0.08, span * 1.18);
  pop();

  // Rhythmic Calatrava-style white steel ribs & sea-glass skylight coffers
  for (let i = 0; i < n; i++) {
    const u = i / (n - 1);
    const z = (u - 0.5) * span * 0.92;
    const wave = 0.12 * sin(u * PI * 2.0 + t * 0.6);

    // Primary chalk-white parabolic arch rib (built from angled segments)
    const segs = 9;
    for (let s = 0; s < segs; s++) {
      const f = (s / (segs - 1)) * 2.0 - 1.0;
      const x = f * (span * 0.48);
      const y = -0.62 + (1.0 - f * f) * (archHeight + wave);
      const slope = -2.0 * f * (archHeight + wave) / (span * 0.48);
      const tilt = atan2(slope, 1.0);

      push();
      translate(x, y, z);
      rotateZ(tilt);
      fill(0.957, 0.961, 0.949);
      box(span * 0.14, 0.055, 0.055);
      pop();

      // Recessed sea-glass coffer panes between ribs
      if (i < n - 1 && s > 0 && s < segs - 1 && s % 2 === 1) {
        push();
        translate(x, y - 0.05, z + (span * 0.92) / (n - 1) * 0.5);
        rotateZ(tilt);
        fill(0.369, 0.533, 0.580);
        box(span * 0.11, 0.025, (span * 0.92) / (n - 1) * 0.72);
        pop();
      }
    }

    // Diagonal tension tie-struts
    push();
    translate(-span * 0.48, -0.68, z);
    fill(0.149, 0.231, 0.267);
    box(0.06, 0.18, 0.06);
    pop();
    push();
    translate(span * 0.48, -0.68, z);
    fill(0.149, 0.231, 0.267);
    box(0.06, 0.18, 0.06);
    pop();
  }
}