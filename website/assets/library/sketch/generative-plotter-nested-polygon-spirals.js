// Generative Plotter Nested Polygon Spirals - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Mechanical plotter drawing featuring concentric nested polygon spirals, progressive angular rotation offsets, and precision vermilion geometric crosshairs on warm cream parchment.
param("rings", 22.0, 10.0, 40.0);
param("sides", 6.0, 3.0, 10.0);
param("rotation_delta", 0.08, 0.02, 0.25);
param("speed", 0.35, 0.1, 1.2);

function draw(t) {
  // Warm cream archival parchment (#ede5d2)
  background(0.88, 0.85, 0.79);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;

  push();
  translate(cx, cy);

  // Precision caliper outer frame (#111111)
  stroke(0.067, 0.067, 0.067);
  strokeWeight(2.6);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  const time = t * speed;
  const ringCount = floor(rings);
  const nSides = floor(sides);

  // 1. Concentric nested polygons with angular progression
  for (let r = ringCount; r >= 1; r--) {
    const fr = r / ringCount;
    const rad = fr * span * 0.44;
    const angOffset = r * rotation_delta + time * 0.4;

    push();
    rotate(angOffset);

    const isVermilion = (r % 5 === 0);
    if (isVermilion) {
      stroke(0.820, 0.188, 0.122, 0.95); // Vermilion red (#d1301f)
      strokeWeight(2.4);
    } else {
      stroke(0.067, 0.067, 0.067, 0.85); // Carbon ink (#111111)
      strokeWeight(1.8);
    }
    noFill();

    beginShape();
    for (let s = 0; s <= nSides; s++) {
      const a = (s / nSides) * Math.PI * 2.0;
      vertex(cos(a) * rad, sin(a) * rad);
    }
    endShape();
    pop();
  }

  // 2. Vermilion precision axial crosshairs
  stroke(0.820, 0.188, 0.122, 0.9);
  strokeWeight(2.2);
  line(-span * 0.46, 0, span * 0.46, 0);
  line(0, -span * 0.46, 0, span * 0.46);

  // 3. Central solid carbon ink core with vermilion registration ring
  fill(0.067, 0.067, 0.067);
  noStroke();
  circle(0, 0, span * 0.08);

  stroke(0.820, 0.188, 0.122);
  strokeWeight(2.4);
  noFill();
  circle(0, 0, span * 0.13);

  // 4. Corner mechanical registration brackets
  stroke(0.067, 0.067, 0.067);
  strokeWeight(2.0);
  const bracket = span * 0.06;
  const d = span * 0.48;
  // Top-left
  line(-d, -d + bracket, -d, -d);
  line(-d, -d, -d + bracket, -d);
  // Top-right
  line(d - bracket, -d, d, -d);
  line(d, -d, d, -d + bracket);
  // Bottom-left
  line(-d, d - bracket, -d, d);
  line(-d, d, -d + bracket, d);
  // Bottom-right
  line(d - bracket, d, d, d);
  line(d, d, d, d - bracket);

  pop();
}
