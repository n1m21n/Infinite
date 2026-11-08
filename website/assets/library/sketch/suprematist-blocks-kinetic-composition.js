// Suprematist Blocks Kinetic Composition - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Russian avant-garde geometric abstraction study featuring floating cantilevered black quadrilaterals, dynamic diagonal red wedges, intersecting linear axes, and orbital tension points on warm canvas.
param("angle", -0.32, -1.0, 1.0);
param("drift", 0.40, 0.1, 1.2);
param("scale_factor", 1.05, 0.5, 2.0);
param("tension", 0.75, 0.2, 1.5);

function draw(t) {
  // Russian avant-garde parchment ground (#f4efe6)
  background(0.957, 0.937, 0.902);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88 * scale_factor;
  const time = t * drift;

  push();
  translate(cx, cy);

  // 1. Dynamic background diagonal construction guideline (faint charcoal)
  stroke(0.2, 0.2, 0.2, 0.25);
  strokeWeight(1.8);
  line(-span * 0.55, span * 0.45, span * 0.55, -span * 0.45);
  line(-span * 0.45, -span * 0.52, span * 0.45, span * 0.52);

  // 2. Heavy black anchor quadrilateral (Malevich black square in motion)
  push();
  translate(sin(time * 0.5) * span * 0.03 - span * 0.08, cos(time * 0.4) * span * 0.02 - span * 0.05);
  rotate(angle + sin(time * 0.25) * 0.04);
  fill(0.067, 0.067, 0.067);
  noStroke();
  rect(-span * 0.24, -span * 0.22, span * 0.48, span * 0.44);

  // High-contrast interior cutout bar
  fill(0.957, 0.937, 0.902);
  rect(-span * 0.18, span * 0.12, span * 0.36, span * 0.04);
  pop();

  // 3. Revolutionary cadmium red wedge (El Lissitzky wedge penetrating space)
  push();
  translate(cos(time * 0.6) * span * 0.04 + span * 0.10, sin(time * 0.5) * span * 0.03 + span * 0.08);
  rotate(angle + 0.78 + sin(time * 0.3) * 0.06);
  fill(0.851, 0.137, 0.122);
  noStroke();
  beginShape();
  vertex(-span * 0.36, -span * 0.08);
  vertex(span * 0.38, -span * 0.02);
  vertex(span * 0.28, span * 0.12);
  vertex(-span * 0.32, span * 0.06);
  endShape();
  pop();

  // 4. Secondary floating black and cobalt counterweight bars
  push();
  translate(span * 0.18, -span * 0.26);
  rotate(angle - 0.45);
  fill(0.067, 0.067, 0.067);
  rect(-span * 0.18, -span * 0.035, span * 0.36, span * 0.07);

  // Cobalt accent dot
  fill(0.12, 0.25, 0.65);
  circle(span * 0.22, 0, span * 0.035);
  pop();

  push();
  translate(-span * 0.22, span * 0.28);
  rotate(angle + 1.2);
  fill(0.067, 0.067, 0.067);
  rect(-span * 0.15, -span * 0.025, span * 0.30, span * 0.05);
  pop();

  // 5. Cadmium red circular tension focal point & black orbital pip
  fill(0.851, 0.137, 0.122);
  circle(-span * 0.26, -span * 0.18, span * 0.13);

  fill(0.067, 0.067, 0.067);
  circle(-span * 0.26 + cos(time * 1.5) * span * 0.11, -span * 0.18 + sin(time * 1.5) * span * 0.11, span * 0.028);

  // 6. Fine linear vector rules anchoring canvas margins
  stroke(0.067, 0.067, 0.067, 0.85);
  strokeWeight(2.4);
  line(-span * 0.44, -span * 0.44, -span * 0.44, span * 0.44);
  stroke(0.851, 0.137, 0.122, 0.90);
  strokeWeight(3.0);
  line(span * 0.44, -span * 0.35, span * 0.44, span * 0.35);

  pop();
}
