// Neo-Brutalist Grid Matrix - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// High-contrast Swiss neo-brutalist architectural poster layout with asymmetric modular concrete grids, diagonal hazard hatchings, and drafting crosshairs.
param("grid_density", 6.0, 3.0, 12.0);
param("hatch_angle", 45.0, 0.0, 90.0);
param("accent_weight", 3.0, 1.0, 6.0);
param("asymmetry", 0.4, 0.1, 0.9);
param("jitter_glitch", 0.2, 0.0, 1.0);

function draw(t) {
  // Midnight ground #151930
  background(0.082, 0.098, 0.188);

  const margin = 36.0;
  const innerW = width - margin * 2;
  const innerH = height - margin * 2;
  const cols = floor(grid_density);
  const rows = floor(grid_density * (innerH / innerW));
  const cellW = innerW / cols;
  const cellH = innerH / rows;

  // 1. Heavy outer framing border with corner architectural markers
  strokeWeight(accent_weight);
  stroke(0.933, 0.945, 0.980, 0.9); // White
  noFill();
  rect(margin, margin, innerW, innerH);

  // Corner brackets
  const cornerLen = 24.0;
  stroke(1.0, 0.420, 0.208, 0.95); // Ember
  line(margin - 6, margin, margin + cornerLen, margin);
  line(margin, margin - 6, margin, margin + cornerLen);
  line(width - margin + 6, margin, width - margin - cornerLen, margin);
  line(width - margin, margin - 6, width - margin, margin + cornerLen);
  line(margin - 6, height - margin, margin + cornerLen, height - margin);
  line(margin, height - margin + 6, margin, height - margin - cornerLen);
  line(width - margin + 6, height - margin, width - margin - cornerLen, height - margin);
  line(width - margin, height - margin + 6, width - margin, height - margin - cornerLen);

  // 2. Asymmetric modular grid cells
  for (let c = 0; c < cols; c++) {
    for (let r = 0; r < rows; r++) {
      const x = margin + c * cellW;
      const y = margin + r * cellH;

      // Deterministic cell archetype based on coordinate hash
      const cellSeed = sin(c * 13.7 + r * 29.3 + 4.1) * 0.5 + 0.5;
      const isAsym = cellSeed > (1.0 - asymmetry);
      const isHeroBlock = (c === floor(cols * 0.5) && r === floor(rows * 0.5));

      // Grid cell outline
      strokeWeight(1.0);
      stroke(0.655, 0.694, 1.0, 0.25); // Subtle signal grid line
      noFill();
      rect(x, y, cellW, cellH);

      if (isHeroBlock) {
        // High-contrast stark hero block (Solid Ember with white crosshair)
        noStroke();
        fill(1.0, 0.420, 0.208, 0.92);
        rect(x + 3, y + 3, cellW - 6, cellH - 6);

        stroke(0.082, 0.098, 0.188, 1.0);
        strokeWeight(2.0);
        line(x + cellW * 0.5, y + 8, x + cellW * 0.5, y + cellH - 8);
        line(x + 8, y + cellH * 0.5, x + cellW - 8, y + cellH * 0.5);
      } else if (isAsym && cellSeed > 0.8) {
        // Diagonal hazard hatching block
        push();
        noFill();
        stroke(0.933, 0.945, 0.980, 0.75);
        strokeWeight(1.5);
        const hatchStep = 8.0;
        for (let h = -cellH; h < cellW + cellH; h += hatchStep) {
          const shift = (sin(t * 2.0 + c + r) * jitter_glitch) * 4.0;
          line(x + max(0, h), y + max(0, -h) + shift, x + min(cellW, h + cellH), y + min(cellH, h + cellH) + shift);
        }
        pop();
      } else if (cellSeed > 0.6) {
        // Concrete tint fill with circular drafting target
        noStroke();
        fill(0.125, 0.149, 0.247, 0.85); // Slate slab
        rect(x + 2, y + 2, cellW - 4, cellH - 4);

        noFill();
        stroke(0.306, 0.949, 0.722, 0.85); // Emerald target
        strokeWeight(1.5);
        const targetRad = min(cellW, cellH) * 0.28;
        ellipse(x + cellW * 0.5, y + cellH * 0.5, targetRad * 2, targetRad * 2);
        fill(0.306, 0.949, 0.722, 0.9);
        ellipse(x + cellW * 0.5, y + cellH * 0.5, 4, 4);
      }
    }
  }

  // 3. Technical typography registration marks & telemetry bars
  noStroke();
  fill(0.933, 0.945, 0.980, 0.85);
  rect(margin + 12, height - margin + 10, 80, 3);
  rect(margin + 100, height - margin + 10, 30, 3);
  rect(margin + 138, height - margin + 10, 14, 3);
}
