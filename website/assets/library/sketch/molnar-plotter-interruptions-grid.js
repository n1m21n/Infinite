// Molnar Plotter Interruptions Grid - Infinite Library (Sketch 2D)
param("grid_n", 7, 4, 10);
param("rings", 7, 3, 11);
param("disorder", 0.34, 0.05, 0.85);
param("dropout", 0.22, 0.0, 0.55);

function hash2(a, b, c) {
  const s = sin(a * 127.1 + b * 311.7 + c * 74.7) * 43758.5453;
  return s - floor(s);
}

function draw(t) {
  // Warm archival cotton-rag vellum (#D5D1C7)
  background(0.835, 0.820, 0.780);
  const w = width;
  const h = height;
  const s = min(w, h);
  const margin = s * 0.11;
  const span = s - margin * 2.0;
  const n = floor(grid_n);
  const nRings = floor(rings);
  const cell = span / n;
  const ox = (w - span) * 0.5;
  const oy = (h - span) * 0.5;

  // Fine plotter registration corner marks
  stroke(0.333, 0.341, 0.357, 0.55);
  strokeWeight(1.2);
  const tick = s * 0.022;
  line(ox - tick, oy, ox + tick * 0.4, oy);
  line(ox, oy - tick, ox, oy + tick * 0.4);
  line(ox + span + tick, oy, ox + span - tick * 0.4, oy);
  line(ox + span, oy - tick, ox + span, oy + tick * 0.4);
  line(ox - tick, oy + span, ox + tick * 0.4, oy + span);
  line(ox, oy + span + tick, ox, oy + span - tick * 0.4);
  line(ox + span + tick, oy + span, ox + span - tick * 0.4, oy + span);
  line(ox + span, oy + span + tick, ox + span, oy + span - tick * 0.4);

  noFill();
  const halfN = (n - 1) * 0.5;
  const phase = floor(t * 0.4) * 0.0;

  for (let row = 0; row < n; row++) {
    for (let col = 0; col < n; col++) {
      const cx = ox + (col + 0.5) * cell;
      const cy = oy + (row + 0.5) * cell;
      const distCenter = 1.0 - min(1.0, sqrt((col - halfN) * (col - halfN) + (row - halfN) * (row - halfN)) / (halfN * 1.414));
      const localDisorder = disorder * (0.15 + 0.85 * distCenter);

      for (let r = 0; r < nRings; r++) {
        const u = (r + 1) / nRings;
        const rad = cell * 0.44 * u;
        const hSeed = hash2(col, row, r + 1.0);
        const rot = (hSeed - 0.5) * localDisorder * 0.65 * sin(t * 0.25 + r * 0.4 + col * 0.3);

        // Occasional vermilion plotter pen accent (#C84B31)
        const isAccent = (hSeed > 0.91) || (col === floor(n * 0.5) && row === floor(n * 0.5) && r === nRings - 2);
        if (isAccent) {
          stroke(0.784, 0.294, 0.192, 0.90);
          strokeWeight(1.8);
        } else {
          stroke(0.067, 0.071, 0.078, 0.88 - r * 0.04);
          strokeWeight(r === nRings - 1 ? 1.65 : 1.15);
        }

        // Compute 4 perturbed corners
        const pts = [];
        for (let k = 0; k < 4; k++) {
          const baseA = (k * 0.5 + 0.25) * TAU * 0.5 + rot;
          const jx = (hash2(col * 7 + k, row * 13 + r, 2.3) - 0.5) * cell * 0.11 * localDisorder;
          const jy = (hash2(col * 11 + r, row * 5 + k, 4.7) - 0.5) * cell * 0.11 * localDisorder;
          const ang = k * (TAU * 0.25) + (TAU * 0.125) + rot;
          pts.push([cx + cos(ang) * rad * 1.35 + jx, cy + sin(ang) * rad * 1.35 + jy]);
        }

        // Draw each edge unless dropped out
        for (let k = 0; k < 4; k++) {
          const drop = hash2(col * 17 + k, row * 19 + r, 9.1);
          if (drop < dropout * (0.4 + 0.6 * distCenter)) {
            continue;
          }
          const p0 = pts[k];
          const p1 = pts[(k + 1) % 4];
          line(p0[0], p0[1], p1[0], p1[1]);
        }
      }
    }
  }
}
