// Chladni Brass Nodal Harmonics - Infinite Library (Sketch 2D)
param("mode_m", 3, 2, 7);
param("mode_n", 5, 2, 8);
param("sand_density", 52, 24, 68);
param("phase_drift", 0.25, 0.0, 1.0);

function draw(t) {
  // Obsidian studio ground (#141210)
  background(0.078, 0.071, 0.063);
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const R = min(w, h) * 0.42;

  // Mahogany bench shadow ring (#6E3F24)
  noFill();
  stroke(0.431, 0.247, 0.141, 0.65);
  strokeWeight(4.0);
  circle(cx, cy, R * 2.14);

  // Octagonal brushed brass resonant plate (#C9A449)
  fill(0.788, 0.643, 0.286, 0.22);
  stroke(0.788, 0.643, 0.286, 0.96);
  strokeWeight(2.8);
  beginShape();
  for (let i = 0; i <= 8; i++) {
    const a = (i / 8.0) * TAU + PI * 0.125;
    vertex(cx + cos(a) * R, cy + sin(a) * R);
  }
  endShape();

  // Concentric brushed-brass lathe hairlines
  noFill();
  stroke(0.788, 0.643, 0.286, 0.24);
  strokeWeight(1.1);
  for (let r = 1; r <= 9; r++) {
    circle(cx, cy, (R * 1.85 * r) / 10.0);
  }

  // Standing wave nodal contours & quartz sand accumulation (#F5EFE0)
  const grid = floor(sand_density);
  const m = floor(mode_m);
  const n = floor(mode_n);
  const morph = 0.22 * sin(t * phase_drift);

  noStroke();
  for (let iy = 0; iy < grid; iy++) {
    const ny = (iy / (grid - 1)) * 2.0 - 1.0;
    for (let ix = 0; ix < grid; ix++) {
      const nx = (ix / (grid - 1)) * 2.0 - 1.0;
      const rad = sqrt(nx * nx + ny * ny);
      if (rad > 0.90 || rad < 0.07) continue;

      const f1 = cos(m * PI * nx) * cos(n * PI * ny) - cos(n * PI * nx) * cos(m * PI * ny);
      const f2 = cos((m + 1) * PI * nx) * cos((n - 1) * PI * ny) - cos((n - 1) * PI * nx) * cos((m + 1) * PI * ny);
      const z = abs(f1 * (1.0 - abs(morph)) + f2 * morph);

      if (z < 0.24) {
        const weight = 1.0 - z / 0.24;
        const jx = sin(ix * 19.3 + iy * 7.1) * (R / grid) * 0.35;
        const jy = cos(iy * 23.7 - ix * 11.4) * (R / grid) * 0.35;
        const px = cx + nx * R * 0.92 + jx;
        const py = cy + ny * R * 0.92 + jy;
        fill(0.961, 0.937, 0.878, 0.45 + 0.55 * weight);
        circle(px, py, 2.2 + 3.6 * weight);
      }
    }
  }

  // Radial vernier tick marks around outer brass bezel
  stroke(0.788, 0.643, 0.286, 0.55);
  strokeWeight(1.1);
  for (let k = 0; k < 48; k++) {
    const a = (k / 48.0) * TAU;
    const r0 = R * 1.03;
    const r1 = r0 + (k % 6 === 0 ? 14 : 6);
    line(cx + cos(a) * r0, cy + sin(a) * r0, cx + cos(a) * r1, cy + sin(a) * r1);
  }

  // Central knurled brass mounting boss
  fill(0.078, 0.071, 0.063);
  stroke(0.788, 0.643, 0.286, 0.95);
  strokeWeight(2.2);
  circle(cx, cy, R * 0.11);
  fill(0.788, 0.643, 0.286, 0.85);
  noStroke();
  circle(cx, cy, R * 0.035);
}
