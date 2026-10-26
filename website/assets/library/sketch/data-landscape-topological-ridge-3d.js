// Data Landscape Topological Ridge 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Generative data sculpture with undulating stacked frequency waveform ridges and floating structural datum planes.
param("ridge_count", 9.0, 4.0, 16.0);
param("wave_amplitude", 0.65, 0.2, 1.8);
param("orbit_speed", 0.32, 0.05, 1.0);
param("span_width", 1.4, 0.6, 2.5);

function draw(t) {
  push();
  const time = t * orbit_speed;

  rotateX(0.68 + sin(time * 0.2) * 0.06);
  rotateY(time * 0.38);
  rotateZ(0.08);

  const ridges = floor(ridge_count);
  const totalDepth = 1.35;

  for (let r = 0; r < ridges; r++) {
    const zFrac = (r / ridges) - 0.5;
    const z = zFrac * totalDepth;

    push();
    translate(0, 0, z);

    if (r % 3 === 0) {
      fill(0.043, 0.075, 0.169); // Deep navy
    } else if (r % 3 === 1) {
      fill(0.357, 0.753, 0.745); // Oceanic cyan
    } else {
      fill(0.11, 0.145, 0.255); // Slate indigo
    }

    const segs = 16;
    const segW = span_width / segs;
    for (let s = 0; s < segs; s++) {
      const xFrac = (s / segs) - 0.5;
      const x = xFrac * span_width;

      const bell = exp(-xFrac * xFrac * 12.0);
      const wave = sin(s * 0.75 + r * 1.2 + time * 2.0) * wave_amplitude * bell;
      const h = 0.08 + wave * 0.8;

      push();
      translate(x, wave * 0.5, 0);
      box(segW * 0.88, h + 0.08, 0.06);

      if (h > 0.25) {
        push();
        translate(0, (h + 0.08) * 0.5 + 0.02, 0);
        fill(1.0, 1.0, 1.0);
        box(segW * 0.88, 0.03, 0.06);
        pop();
      }
      pop();
    }

    pop();
  }

  push();
  translate(0, -0.45, 0);
  fill(0.043, 0.075, 0.169);
  box(span_width * 1.08, 0.02, totalDepth * 1.15);
  pop();

  pop();
}
