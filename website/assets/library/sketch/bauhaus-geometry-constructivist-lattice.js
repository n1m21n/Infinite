// Bauhaus Geometry Constructivist Lattice - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Constructivist geometric lattice featuring primary-colored concentric arcs, diagonal structural rules, and balanced asymmetrical quadrant divisions on warm parchment.
param("grid_cells", 6.0, 3.0, 12.0);
param("arc_scale", 1.1, 0.5, 2.0);
param("rotation_speed", 0.45, 0.1, 1.5);
param("rule_thickness", 2.8, 1.0, 6.0);

function draw(t) {
  // Warm parchment paper ground (#efe7d3)
  background(0.937, 0.906, 0.827);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;

  push();
  translate(cx, cy);

  // Structural border frame (#111111)
  stroke(0.08, 0.08, 0.09);
  strokeWeight(3.0);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  // Inner margin boundary
  stroke(0.08, 0.08, 0.09, 0.4);
  strokeWeight(1.2);
  rect(-span * 0.45, -span * 0.45, span * 0.90, span * 0.90);

  const time = t * rotation_speed;

  // 1. Prussian Blue deep semicircle arc (#1d3557)
  push();
  translate(-span * 0.08, span * 0.06);
  rotate(time * 0.25);
  fill(0.114, 0.208, 0.341, 0.92);
  noStroke();
  beginShape();
  const rad = span * 0.32 * arc_scale;
  for (let a = 0; a <= Math.PI + 0.05; a += 0.08) {
    vertex(cos(a) * rad, sin(a) * rad);
  }
  vertex(0, 0);
  endShape();
  pop();

  // 2. Bold Crimson Red solid constructivist wedge & rect (#d62828)
  push();
  translate(span * 0.12, -span * 0.10);
  rotate(-time * 0.3);
  fill(0.839, 0.157, 0.157, 0.94);
  noStroke();
  rect(-span * 0.18, -span * 0.18, span * 0.36, span * 0.36);
  
  // Intersecting dynamic diagonal wedge
  fill(0.839, 0.157, 0.157, 0.85);
  triangle(0, -span * 0.25, span * 0.28, span * 0.15, -span * 0.15, span * 0.20);
  pop();

  // 3. Warm Ochre Yellow solar disk & concentric ring (#fcbf49)
  push();
  translate(span * 0.18, span * 0.16);
  fill(0.988, 0.749, 0.286, 0.95);
  noStroke();
  circle(0, 0, span * 0.28 * arc_scale);

  stroke(0.08, 0.08, 0.09);
  strokeWeight(rule_thickness);
  noFill();
  circle(0, 0, span * 0.42 * arc_scale);
  pop();

  // 4. Charcoal diagonal linear constructivist axis lines & crossbars
  stroke(0.08, 0.08, 0.09);
  strokeWeight(rule_thickness * 1.5);
  line(-span * 0.44, span * 0.36, span * 0.44, -span * 0.36);

  strokeWeight(rule_thickness * 0.8);
  line(-span * 0.36, -span * 0.44, span * 0.36, span * 0.44);

  // 5. Grid of precision registration crosshairs and tick marks
  stroke(0.08, 0.08, 0.09, 0.75);
  strokeWeight(1.4);
  const steps = 5;
  for (let i = -steps; i <= steps; i++) {
    const x = (i / steps) * (span * 0.42);
    line(x, -span * 0.45, x, -span * 0.43);
    line(x, span * 0.43, x, span * 0.45);
    line(-span * 0.45, x, -span * 0.43, x);
    line(span * 0.43, x, span * 0.45, x);
  }

  pop();
}
