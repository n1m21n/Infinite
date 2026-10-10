// Rule 110 - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Elementary cellular automaton. Change `rule` for 30, 90, 110, 184...
param("rule", 110, 0, 255);
param("cell", 6, 3, 16);
param("speed", 12, 1, 40);

function draw(t) {
  background(0.97, 0.96, 0.93);
  const cols = floor(width / cell), rows = floor(height / cell);
  const gen = floor(t * speed);
  let row = [];
  for (let i = 0; i < cols; i++) row.push(i == cols - 1 ? 1 : 0);
  noStroke();
  for (let g = 0; g < gen + rows; g++) {
    const y = g - gen;
    if (y >= 0 && y < rows) {
      for (let i = 0; i < cols; i++) if (row[i]) {
        fill(hsl(0.6 - 0.15 * (y / rows), 0.55, 0.3));
        rect(i * cell, y * cell, cell - 1, cell - 1);
      }
    }
    const nx = [];
    for (let i = 0; i < cols; i++) {
      const l = row[(i + cols - 1) % cols], c = row[i], r = row[(i + 1) % cols];
      nx.push((rule >> (l * 4 + c * 2 + r)) & 1);
    }
    row = nx;
  }
}
