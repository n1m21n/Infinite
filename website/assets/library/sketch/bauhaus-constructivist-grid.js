// Bauhaus Constructivist Grid - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Modular constructivist grid of primary circular discs, quarter-circle wedges, and solid rectangular balance bars.
param("grid_divs", 4.0, 2.0, 8.0);
param("line_weight", 2.0, 0.5, 6.0);
param("rot_speed", 0.35, 0.05, 1.5);
param("margin", 0.08, 0.02, 0.2);

function draw(t) {
  // Warm Bauhaus cream ground #F2E8CF calibrated for luminance 14-228
  background(0.85, 0.82, 0.76);

  const minDim = min(width, height);
  const time = t * rot_speed;
  const divs = floor(grid_divs);
  const span = minDim * (1.0 - margin * 2.0);
  const cell = span / divs;
  const startX = (width - span) * 0.5;
  const startY = (height - span) * 0.5;

  push();

  // Outer framing boundary
  noFill();
  stroke(0.067, 0.067, 0.067);
  strokeWeight(line_weight * 1.5);
  rect(startX, startY, span, span);

  // Constructivist modular cells
  for (let j = 0; j < divs; j++) {
    for (let i = 0; i < divs; i++) {
      const cx = startX + (i + 0.5) * cell;
      const cy = startY + (j + 0.5) * cell;
      const idx = (i + j * divs);

      push();
      translate(cx, cy);

      // Grid line dividers
      noFill();
      stroke(0.067, 0.067, 0.067, 0.25);
      strokeWeight(line_weight * 0.5);
      rect(-cell * 0.5, -cell * 0.5, cell, cell);

      if (idx % 4 === 0) {
        // Primary Cobalt Blue solid circle with cut
        noStroke();
        fill(0.106, 0.231, 0.561); // #1B3B8F
        circle(0, 0, cell * 0.41);
        fill(0.847, 0.188, 0.122); // #D8301F
        rect(-cell * 0.41, -cell * 0.08, cell * 0.82, cell * 0.16);
      } else if (idx % 4 === 1) {
        // Bauhaus Vermilion Red quarter-circle
        noStroke();
        fill(0.847, 0.188, 0.122); // #D8301F
        beginShape();
        vertex(-cell * 0.4, cell * 0.4);
        for (let k = 0; k <= 16; k++) {
          const a = -1.570796 * k / 16.0;
          vertex(-cell * 0.4 + cos(a) * cell * 0.8, cell * 0.4 + sin(a) * cell * 0.8);
        }
        endShape(1);
        fill(0.067, 0.067, 0.067);
        rect(-cell * 0.4, -cell * 0.4, cell * 0.35, cell * 0.35);
      } else if (idx % 4 === 2) {
        // Cadmium Yellow diagonal bar
        push();
        rotate(0.785398 + sin(time * 0.5 + idx) * 0.2);
        noStroke();
        fill(0.949, 0.718, 0.020); // #F2B705
        rect(-cell * 0.45, -cell * 0.12, cell * 0.9, cell * 0.24);
        fill(0.067, 0.067, 0.067);
        circle(0, 0, cell * 0.12);
        pop();
      } else {
        // Intersecting black and red concentric rings
        noFill();
        stroke(0.067, 0.067, 0.067);
        strokeWeight(line_weight);
        ellipse(0, 0, cell * 0.7, cell * 0.7);
        stroke(0.847, 0.188, 0.122);
        ellipse(0, 0, cell * 0.35, cell * 0.35);
      }

      pop();
    }
  }

  pop();
}
