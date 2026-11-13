// Y2K Acid Hyper Starburst Polyhedron 3D - Infinite Library (Sketch 3D)
param("spin", 0.45, 0.1, 1.5);
param("scale_factor", 1.2, 0.5, 2.5);
param("spikes", 8.0, 4.0, 16.0);
param("ring_count", 3.0, 1.0, 6.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.48 + sin(time * 0.2) * 0.08);
  rotateY(time * 0.35 + 0.4);

  // Central polyhedral chrome starburst core (#d4d8e8)
  fill(0.83, 0.85, 0.91);
  box(0.55 * scale_factor, 0.55 * scale_factor, 0.55 * scale_factor);

  // Spiked starburst pylons projecting along 3 axes
  const numSpikes = floor(spikes);
  for (let i = 0; i < numSpikes; i++) {
    const angle = (i / numSpikes) * Math.PI * 2.0;
    push();
    rotateZ(angle);
    rotateY(i * 0.4);
    translate(0.65 * scale_factor, 0, 0);

    // Alternate electric rave accents: neon magenta (#ff007f) and electric cyan (#00f0ff)
    if (i % 2 === 0) {
      fill(1.0, 0.0, 0.5);
    } else {
      fill(0.0, 0.94, 1.0);
    }
    box(0.65 * scale_factor, 0.08 * scale_factor, 0.08 * scale_factor);
    pop();
  }

  // Interlocking orbital cybernetic rings
  const nRings = floor(ring_count);
  for (let r = 0; r < nRings; r++) {
    push();
    rotateX(0.7 * r + time * 0.15);
    rotateY(0.9 * r + time * 0.22);

    const rad = (0.95 + r * 0.35) * scale_factor;
    const segments = 24;
    for (let s = 0; s < segments; s++) {
      const a1 = (s / segments) * Math.PI * 2.0;
      const x1 = Math.cos(a1) * rad;
      const y1 = Math.sin(a1) * rad;

      push();
      translate(x1, y1, 0);
      rotateZ(a1);
      if (r === 0) {
        fill(0.92, 0.94, 0.98); // Chrome silver
      } else if (r === 1) {
        fill(0.46, 1.0, 0.01); // Lime green (#76ff03)
      } else {
        fill(0.75, 0.2, 0.9);  // Electric violet
      }
      box(0.18 * scale_factor, 0.05 * scale_factor, 0.05 * scale_factor);
      pop();
    }
    pop();
  }

  pop();
}
