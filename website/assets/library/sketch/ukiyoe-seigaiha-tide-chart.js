// Ukiyo-e Seigaiha Tide Chart - Infinite Library (Sketch 2D)
param("rows", 11, 6, 16);
param("arcs", 5, 3, 8);
param("tideAmp", 0.28, 0.0, 0.7);
param("drift", 0.18, 0.0, 0.6);

function draw(t) {
  // Warm aged washi ground (#E4DFCE)
  background(0.894, 0.875, 0.808);
  const w = width;
  const h = height;
  const margin = min(w, h) * 0.09;
  const span = min(w, h) - margin * 2.0;
  const x0 = (w - span) * 0.5;
  const y0 = (h - span) * 0.5;

  // Ochre woodblock sky gradient band (#C6A76C)
  noStroke();
  for (let i = 0; i < 10; i++) {
    fill(0.776, 0.655, 0.424, 0.24 * (1.0 - i / 10.0));
    rect(x0, y0 + i * (span * 0.025), span, span * 0.026);
  }

  // Seigaiha fan lattice
  const nRows = floor(rows);
  const nCols = nRows;
  const cellW = span / nCols;
  const cellH = (span * 0.76) / nRows;
  const maxR = cellW * 0.68;
  const nArcs = floor(arcs);

  for (let r = 0; r < nRows; r++) {
    const shift = (r % 2 === 1) ? cellW * 0.5 : 0.0;
    const cyBase = y0 + span * 0.24 + r * cellH;
    for (let c = -1; c <= nCols; c++) {
      const cx = x0 + c * cellW + shift;
      if (cx < x0 - cellW * 0.35 || cx > x0 + span + cellW * 0.35) continue;

      const phase = sin(c * 0.55 + r * 0.42 - t * drift * 2.2) * tideAmp;
      const cy = cyBase + phase * cellH * 0.55;

      // Solid mask behind each fan to create authentic overlapping scales
      noStroke();
      fill(0.894, 0.875, 0.808);
      beginShape();
      for (let s = 0; s <= 18; s++) {
        const ang = PI + (s / 18.0) * PI;
        vertex(cx + cos(ang) * maxR, cy + sin(ang) * maxR);
      }
      endShape();

      // Concentric Prussian blue (#163759) & celadon (#8FB5AC) arcs
      noFill();
      for (let a = 0; a < nArcs; a++) {
        const u = 1.0 - a / nArcs;
        const rad = maxR * u;
        if (rad < 3.0) continue;
        if (a % 2 === 0) {
          stroke(0.086, 0.216, 0.349, 0.92);
          strokeWeight(a === 0 ? 2.2 : 1.5);
        } else {
          stroke(0.561, 0.710, 0.675, 0.90);
          strokeWeight(1.8);
        }
        beginShape();
        for (let s = 0; s <= 18; s++) {
          const ang = PI + (s / 18.0) * PI;
          vertex(cx + cos(ang) * rad, cy + sin(ang) * rad);
        }
        endShape();
      }

      // Vermilion crest accent on resonant peaks (#B83A2A)
      if (phase > tideAmp * 0.45 && (r + c) % 3 === 0) {
        stroke(0.722, 0.227, 0.165, 0.88);
        strokeWeight(1.8);
        beginShape();
        for (let s = 0; s <= 10; s++) {
          const ang = PI * 1.15 + (s / 10.0) * (PI * 0.70);
          vertex(cx + cos(ang) * maxR * 0.22, cy + sin(ang) * maxR * 0.22);
        }
        endShape();
      }
    }
  }

  // Crisp frame mask & woodblock border
  noFill();
  stroke(0.098, 0.118, 0.141, 0.94);
  strokeWeight(3.0);
  rect(x0, y0, span, span);
  strokeWeight(1.1);
  rect(x0 + 8, y0 + 8, span - 16, span - 16);

  // Registration ticks along bottom datum
  stroke(0.722, 0.227, 0.165, 0.85);
  strokeWeight(1.4);
  for (let i = 1; i < 12; i++) {
    const tx = x0 + (i / 12.0) * span;
    line(tx, y0 + span + 6, tx, y0 + span + 14);
  }
}
