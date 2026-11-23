// Chladni Square Superposition Plate - Infinite Library (Sketch 2D)
param("freq_a", 4, 2, 8);
param("freq_b", 7, 3, 9);
param("contour_steps", 36, 18, 56);
param("drift", 0.3, 0.0, 1.0);

function draw(t) {
  // Obsidian background (#141210)
  background(0.078, 0.071, 0.063);
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const S = min(w, h) * 0.72;
  const half = S * 0.5;

  // Mahogany outer mounting frame (#6E3F24)
  noFill();
  stroke(0.431, 0.247, 0.141, 0.85);
  strokeWeight(4.0);
  rect(cx - half - 18, cy - half - 18, S + 36, S + 36);

  // Brass plate surface & chamfered rim (#C9A449)
  fill(0.788, 0.643, 0.286, 0.11);
  stroke(0.788, 0.643, 0.286, 0.94);
  strokeWeight(2.2);
  rect(cx - half, cy - half, S, S);

  // Corner bow-excitation registration ticks
  stroke(0.961, 0.937, 0.878, 0.65);
  strokeWeight(1.4);
  const tick = 16;
  for (let sx = -1; sx <= 1; sx += 2) {
    for (let sy = -1; sy <= 1; sy += 2) {
      const bx = cx + sx * (half + 10);
      const by = cy + sy * (half + 10);
      line(bx, by, bx - sx * tick, by);
      line(bx, by, bx, by - sy * tick);
    }
  }

  // Orthogonal standing-wave nodal web
  const n = floor(contour_steps);
  const a = floor(freq_a);
  const b = floor(freq_b);
  const ph = t * drift * 0.6;

  noStroke();
  for (let j = 0; j < n; j++) {
    const v = (j / (n - 1)) * 2.0 - 1.0;
    for (let i = 0; i < n; i++) {
      const u = (i / (n - 1)) * 2.0 - 1.0;
      const wave = sin(a * PI * u + ph) * sin(b * PI * v) + 0.65 * cos(b * PI * u) * cos(a * PI * v - ph);
      const d = abs(wave);
      if (d < 0.22) {
        const dens = 1.0 - d / 0.22;
        const px = cx + u * (half * 0.94);
        const py = cy + v * (half * 0.94);
        fill(0.961, 0.937, 0.878, 0.30 + 0.68 * dens);
        circle(px, py, 1.6 + 3.2 * dens);
      }
    }
  }

  // Fine brass crosshair & center spindle
  stroke(0.788, 0.643, 0.286, 0.35);
  strokeWeight(1.0);
  line(cx - half, cy, cx + half, cy);
  line(cx, cy - half, cx, cy + half);

  fill(0.788, 0.643, 0.286);
  noStroke();
  circle(cx, cy, 14);
  fill(0.078, 0.071, 0.063);
  circle(cx, cy, 5);
}
