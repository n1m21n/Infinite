// Aalto Viipuri Acoustic Fins 3D - Infinite Library (Sketch 3D)
param("fins", 22, 10, 36);
param("segments", 20, 10, 32);
param("wave_amp", 0.36, 0.1, 0.7);
param("spin", 0.16, 0.0, 0.5);

function draw(t) {
  const nf = floor(fins);
  const ns = floor(segments);
  rotateY(t * spin + 0.48);
  rotateX(0.26);

  // Architectural side wall beams (#264653 slate teal)
  for (let s = -1; s <= 1; s += 2) {
    push();
    translate(s * 1.48, -0.35, 0);
    fill(0.149, 0.275, 0.325);
    box(0.09, 1.35, 2.75);
    pop();
  }

  // Undulating laminated birch acoustic ceiling fins
  for (let i = 0; i < nf; i++) {
    const u = i / (nf - 1);
    const z = (u - 0.5) * 2.55;

    for (let j = 0; j < ns; j++) {
      const v = j / (ns - 1);
      const x = (v - 0.5) * 2.75;

      const wave = wave_amp * (
        sin(v * PI * 2.5 + u * 1.8 + t * 0.35) * 0.65 +
        cos(v * PI * 4.0 - u * 2.4) * 0.35
      );
      const y = 0.25 + wave;
      const slope = cos(v * PI * 2.5 + u * 1.8 + t * 0.35) * wave_amp * 0.55;

      push();
      translate(x, y, z);
      rotateZ(slope);
      const warm = 0.78 + 0.18 * sin(u * PI + v * 2.0);
      // Warm Finnish birch (#D4A373 / #E9C46A)
      fill(warm * 0.94, warm * 0.74, warm * 0.48);
      box(0.17, 0.14, 0.055);
      pop();
    }
  }
}
