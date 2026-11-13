// Y2K Acid Chrome Metaball Lattice 3D - Infinite Library (Sketch 3D)
param("spin", 0.42, 0.1, 1.5);
param("scale_factor", 1.15, 0.5, 2.5);
param("nodes", 6.0, 3.0, 12.0);
param("helix_turns", 3.0, 1.0, 6.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.52 + sin(time * 0.18) * 0.06);
  rotateY(time * 0.32 + 0.35);

  // Central chrome pedestal (#1a1829)
  push();
  translate(0, 1.25 * scale_factor, 0);
  fill(0.12, 0.11, 0.18);
  box(2.2 * scale_factor, 0.12, 2.2 * scale_factor);
  pop();

  // Floating cluster of cybernetic chrome metaball nodes
  const nNodes = floor(nodes);
  for (let i = 0; i < nNodes; i++) {
    const a = (i / nNodes) * Math.PI * 2.0;
    const r = (0.75 + sin(i * 1.5 + time * 0.4) * 0.25) * scale_factor;
    const y = (cos(i * 2.1 + time * 0.3) * 0.45) * scale_factor;

    push();
    translate(Math.cos(a) * r, y, Math.sin(a) * r);

    // Alternate chrome silver, electric cyan (#00ffff), and hot magenta (#ff007f)
    if (i % 3 === 0) {
      fill(0.88, 0.90, 0.96); // Chrome silver
    } else if (i % 3 === 1) {
      fill(0.0, 0.95, 1.0);  // Cyan
    } else {
      fill(1.0, 0.0, 0.55); // Magenta
    }

    box(0.38 * scale_factor, 0.38 * scale_factor, 0.38 * scale_factor);
    pop();
  }

  // Interlocking double-helix chrome strand wraps
  const steps = 36;
  for (let s = 0; s < steps; s++) {
    const frac = s / steps;
    const angle = frac * Math.PI * 2.0 * helix_turns + time * 0.6;
    const rad = 1.05 * scale_factor;
    const yPos = (frac - 0.5) * 2.2 * scale_factor;

    push();
    translate(Math.cos(angle) * rad, yPos, Math.sin(angle) * rad);
    fill(0.46, 1.0, 0.01); // Lime rave accent (#76ff03)
    box(0.09 * scale_factor, 0.09 * scale_factor, 0.09 * scale_factor);
    pop();

    push();
    translate(Math.cos(angle + Math.PI) * rad, yPos, Math.sin(angle + Math.PI) * rad);
    fill(0.95, 0.95, 0.98); // High-specular chrome
    box(0.09 * scale_factor, 0.09 * scale_factor, 0.09 * scale_factor);
    pop();
  }

  pop();
}
