// Demoscene Plasma Rotozoom Helix Twister 3D - Infinite Library (Sketch 3D)
param("twist_speed", 0.75, 0.2, 2.0);
param("ribbon_scale", 0.85, 0.4, 1.5);
param("color_cycle", 1.25, 0.4, 3.0);
param("column_height", 2.5, 1.5, 3.8);

function clamp(x, a, b) {
  return Math.max(a, Math.min(b, x));
}

function draw(t) {
  push();
  const time = t * twist_speed;

  rotateY(time * 0.85);
  rotateX(0.40 + sin(time * 0.45) * 0.18);
  rotateZ(sin(time * 0.3) * 0.15);

  const slices = 52;
  const dy = column_height / slices;
  const halfH = column_height * 0.5;

  // Interweaving 4-ribbon classic Amiga twister
  for (let i = 0; i < slices; i++) {
    const y0 = -halfH + i * dy;
    const y1 = y0 + dy;

    const twist0 = y0 * 3.4 + time * 1.8;
    const twist1 = y1 * 3.4 + time * 1.8;

    const r0 = (0.52 + 0.16 * sin(y0 * 4.2 - time * 2.2)) * ribbon_scale;
    const r1 = (0.52 + 0.16 * sin(y1 * 4.2 - time * 2.2)) * ribbon_scale;

    for (let ribbon = 0; ribbon < 4; ribbon++) {
      const phiOffset = ribbon * (PI * 0.5);
      const a0 = twist0 + phiOffset;
      const a1 = twist1 + phiOffset;

      const x0A = cos(a0) * r0;
      const z0A = sin(a0) * r0;
      const x0B = cos(a0 + 0.35) * (r0 * 0.95);
      const z0B = sin(a0 + 0.35) * (r0 * 0.95);

      const x1A = cos(a1) * r1;
      const z1A = sin(a1) * r1;
      const x1B = cos(a1 + 0.35) * (r1 * 0.95);
      const z1B = sin(a1 + 0.35) * (r1 * 0.95);

      // Ribbon color: alternating fiery orange/copper (#ff3b00) and electric cyan (#00e0ff)
      if (ribbon % 2 === 0) {
        const fire = 0.5 + 0.5 * sin(y0 * color_cycle * 2.0 + time * 2.5);
        fill(1.0, 0.22 + fire * 0.26, 0.0);
      } else {
        const cyan = 0.5 + 0.5 * cos(y0 * color_cycle * 2.0 - time * 2.0);
        fill(0.0, 0.85 + cyan * 0.15, 1.0);
      }

      // Render quad strip facet
      beginShape();
      vertex(x0A, y0, z0A);
      vertex(x0B, y0, z0B);
      vertex(x1B, y1, z1B);
      vertex(x1A, y1, z1A);
      endShape();
    }
  }

  // Outer orbiting plasma particle rings
  const numParticles = 24;
  for (let p = 0; p < numParticles; p++) {
    const pAng = (p / numParticles) * PI * 2.0 + time * 1.4;
    const pr = 1.25 + 0.15 * sin(pAng * 3.0 + time);
    const py = sin(pAng * 2.0 - time) * 0.85;
    const px = cos(pAng) * pr;
    const pz = sin(pAng) * pr;

    push();
    translate(px, py, pz);
    fill(0.0, 0.88, 1.0);
    sphere(0.045, 6, 6);
    pop();
  }

  pop();
}
