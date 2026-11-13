// Holographic Iridescent Prism Blob - Infinite Library (Sketch 2D)
param("shell_count", 16, 6, 28);
param("lobe_order", 4, 2, 8);
param("warp_depth", 0.18, 0.04, 0.35);
param("spin_speed", 0.45, 0.0, 1.5);

function draw(t) {
  background(0.055, 0.067, 0.141);
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const maxR = min(w, h) * 0.43;
  const ns = floor(shell_count);
  const lo = floor(lobe_order);

  // Outer prismatic bloom rings
  for (let i = 0; i < ns; i++) {
    const u = 1.0 - i / ns;
    const rad = maxR * (0.14 + u * 0.86);
    const phase = t * spin_speed + i * 0.32;

    if (i % 4 === 0) {
      fill(0.663, 0.941, 1.0, 0.55);
      stroke(1.0, 1.0, 1.0, 0.85);
    } else if (i % 4 === 1) {
      fill(1.0, 0.702, 0.961, 0.55);
      stroke(0.663, 0.941, 1.0, 0.85);
    } else if (i % 4 === 2) {
      fill(0.839, 1.0, 0.620, 0.52);
      stroke(1.0, 0.702, 0.961, 0.85);
    } else {
      fill(0.11, 0.14, 0.28, 0.82);
      stroke(0.839, 1.0, 0.620, 0.90);
    }
    strokeWeight(2.4);

    beginShape();
    const segs = 80;
    for (let s = 0; s < segs; s++) {
      const a = (s / segs) * Math.PI * 2.0;
      const mod1 = Math.sin(a * lo + phase) * warp_depth;
      const mod2 = Math.cos(a * (lo + 1) - phase * 0.8) * (warp_depth * 0.5);
      const r = rad * (1.0 + mod1 + mod2);
      vertex(cx + Math.cos(a) * r, cy + Math.sin(a) * r);
    }
    endShape(CLOSE);
  }

  // Specular foil glints
  noStroke();
  for (let g = 0; g < 12; g++) {
    const ga = (g / 12) * Math.PI * 2.0 + t * spin_speed * 0.5;
    const gr = maxR * (0.42 + 0.32 * Math.sin(g * 1.7 + t * 0.6));
    fill(1.0, 1.0, 1.0, 0.85);
    circle(cx + Math.cos(ga) * gr, cy + Math.sin(ga) * gr, 10 + (g % 3) * 5);
  }
}
