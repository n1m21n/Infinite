// Molnar Concentric Quadrics - Infinite Library (Sketch 2D)
param("cells", 7, 4, 11);
param("rings", 8, 4, 14);
param("perturb", 0.18, 0.02, 0.35);
param("tempo", 0.22, 0.0, 0.8);

function hash2(x, y) {
  const s = sin(x * 91.3458 + y * 47.53) * 43758.5453;
  return s - floor(s);
}

function draw(t) {
  // Archival plotter paper (#E5E6E1)
  background(0.898, 0.902, 0.882);
  const w = width;
  const h = height;
  const margin = min(w, h) * 0.085;
  const span = min(w, h) - margin * 2.0;
  const x0 = (w - span) * 0.5;
  const y0 = (h - span) * 0.5;

  const n = floor(cells);
  const step = span / n;
  const rCount = floor(rings);

  noFill();
  for (let iy = 0; iy < n; iy++) {
    for (let ix = 0; ix < n; ix++) {
      const cx = x0 + (ix + 0.5) * step;
      const cy = y0 + (iy + 0.5) * step;
      const cellChaos = ((ix + iy) / max(1.0, (n - 1) * 2.0)) * 0.75 + 0.25;

      for (let r = 0; r < rCount; r++) {
        const frac = (r + 1) / rCount;
        const half = step * 0.44 * frac;
        const j = half * perturb * cellChaos;

        // Skip occasional inner rings for Molnar rhythm
        if (hash2(ix * 13 + r, iy * 29 + r) < 0.14) continue;

        const isAccent = (r === rCount - 1 && (ix + iy) % 5 === 0);
        if (isAccent) {
          stroke(0.702, 0.227, 0.180, 0.88);
          strokeWeight(1.55);
        } else {
          stroke(0.094, 0.094, 0.090, 0.82);
          strokeWeight(1.25);
        }

        beginShape();
        for (let c = 0; c <= 4; c++) {
          const ci = c % 4;
          const sx = (ci === 0 || ci === 3) ? -1 : 1;
          const sy = (ci === 0 || ci === 1) ? -1 : 1;
          const n1 = (hash2(ix * 7 + ci * 3 + r * 11, iy * 17 + r) - 0.5) * 2.0;
          const n2 = (hash2(ix * 19 + r, iy * 11 + ci * 5 + r * 7) - 0.5) * 2.0;
          const anim = sin(t * tempo + ix * 0.4 + iy * 0.3 + r * 0.5) * 0.22;
          const vx = cx + sx * half + (n1 + anim) * j;
          const vy = cy + sy * half + (n2 - anim) * j;
          vertex(vx, vy);
        }
        endShape();
      }
    }
  }
}
