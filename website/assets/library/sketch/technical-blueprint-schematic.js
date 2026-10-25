// Technical Blueprint Schematic - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Cyanotype drafting schematic with precision architectural millimeter grids, concentric compass coordinate arcs, and isometric mechanical annotation nodes.
param("grid_spacing", 24.0, 10.0, 50.0);
param("compass_rings", 6.0, 2.0, 12.0);
param("dimension_callouts", 8.0, 3.0, 16.0);
param("rotation_speed", 0.25, 0.0, 1.5);
param("lineweight", 1.2, 0.5, 3.0);

function draw(t) {
  // Midnight ground #151930
  background(0.082, 0.098, 0.188);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const minDim = min(width, height);
  const time = t * rotation_speed;

  // 1. Millimeter drafting grid (cyanotype blueprint substrate)
  strokeWeight(0.5);
  for (let x = 0; x <= width; x += grid_spacing) {
    const isMajor = floor(x / grid_spacing) % 4 === 0;
    stroke(0.655, 0.694, 1.0, isMajor ? 0.35 : 0.12);
    line(x, 0, x, height);
  }
  for (let y = 0; y <= height; y += grid_spacing) {
    const isMajor = floor(y / grid_spacing) % 4 === 0;
    stroke(0.655, 0.694, 1.0, isMajor ? 0.35 : 0.12);
    line(0, y, width, y);
  }

  // 2. Concentric compass coordinate rings with vernier tick marks
  push();
  translate(cx, cy);
  noFill();
  strokeWeight(lineweight);

  const numRings = floor(compass_rings);
  for (let r = 1; r <= numRings; r++) {
    const rad = minDim * (0.08 + (r / numRings) * 0.38);
    const ringSpeed = (r % 2 === 0 ? 1 : -1) * (0.2 + r * 0.05);
    const ringAngle = time * ringSpeed;

    // Outer guide circle
    stroke(0.655, 0.694, 1.0, 0.45);
    ellipse(0, 0, rad * 2.0, rad * 2.0);

    // Vernier ticks along circumference
    const ticks = r * 12;
    for (let k = 0; k < ticks; k++) {
      const a = (k / ticks) * TAU + ringAngle;
      const tLen = (k % 4 === 0) ? minDim * 0.022 : minDim * 0.01;
      const x1 = cos(a) * (rad - tLen * 0.5);
      const y1 = sin(a) * (rad - tLen * 0.5);
      const x2 = cos(a) * (rad + tLen * 0.5);
      const y2 = sin(a) * (rad + tLen * 0.5);

      stroke(k % 4 === 0 ? [1.0, 0.42, 0.21, 0.85] : [0.655, 0.694, 1.0, 0.4]);
      line(x1, y1, x2, y2);
    }
  }

  // 3. Precision architectural dimension callouts and crosshairs
  const numNodes = floor(dimension_callouts);
  for (let i = 0; i < numNodes; i++) {
    const a = (i / numNodes) * TAU + time * 0.4;
    const rDist = minDim * (0.18 + 0.18 * sin(i * 1.7 + time * 1.5));
    const px = cos(a) * rDist;
    const py = sin(a) * rDist;

    // Leader line from origin
    stroke(0.306, 0.949, 0.722, 0.5); // Emerald callout
    strokeWeight(1.0);
    line(0, 0, px, py);

    // Dimension cross marker
    stroke(1.0, 0.42, 0.21, 0.9); // Ember marker
    strokeWeight(1.5);
    line(px - 6, py, px + 6, py);
    line(px, py - 6, px, py + 6);

    // Callout badge ring
    noFill();
    ellipse(px, py, 18, 18);
  }

  // 4. Central drafting rosette
  rotate(-time * 0.5);
  stroke(0.96, 0.94, 0.91, 0.85);
  strokeWeight(1.5);
  rect(-minDim * 0.06, -minDim * 0.06, minDim * 0.12, minDim * 0.12);
  rotate(PI * 0.25);
  rect(-minDim * 0.06, -minDim * 0.06, minDim * 0.12, minDim * 0.12);

  fill(0.96, 0.94, 0.91, 0.95);
  ellipse(0, 0, 6, 6);
  pop();
}
