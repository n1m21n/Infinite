// Bauhaus Geometry Asymmetric Rotary Composition - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Asymmetric constructivist study with interlocking rotational discs, tensioned diagonal intersecting planes, and primary Bauhaus accents on ivory gesso.
param("disc_count", 5.0, 3.0, 8.0);
param("rotary_speed", 0.42, 0.1, 1.2);
param("accent_scale", 1.15, 0.5, 2.0);
param("axis_angle", 0.785, 0.2, 1.57);

function draw(t) {
  // Ivory gesso ground (#efe7d3)
  background(0.937, 0.906, 0.827);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;

  push();
  translate(cx, cy);

  // Outer framing boundary
  stroke(0.08, 0.08, 0.09);
  strokeWeight(2.8);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  const time = t * rotary_speed;

  // 1. Concentric rotating disc segments in Prussian Blue (#1d3557)
  push();
  translate(-span * 0.12, -span * 0.08);
  rotate(time * 0.35);
  fill(0.114, 0.208, 0.341, 0.90);
  noStroke();
  beginShape();
  const r1 = span * 0.38 * accent_scale;
  for (let a = 0; a <= Math.PI * 0.75 + 0.05; a += 0.08) {
    vertex(cos(a) * r1, sin(a) * r1);
  }
  vertex(0, 0);
  endShape();
  pop();

  // 2. Bauhaus Red wedge (#d62828)
  push();
  translate(span * 0.10, span * 0.12);
  rotate(-time * 0.25);
  fill(0.839, 0.157, 0.157, 0.92);
  noStroke();
  triangle(-span * 0.15, -span * 0.25, span * 0.32, 0, -span * 0.10, span * 0.25);
  pop();

  // 3. Ochre Yellow disk (#fcbf49)
  push();
  translate(-span * 0.18, span * 0.20);
  fill(0.988, 0.749, 0.286, 0.95);
  noStroke();
  circle(0, 0, span * 0.24 * accent_scale);
  pop();

  // 4. Intersecting charcoal axis lines
  stroke(0.08, 0.08, 0.09);
  strokeWeight(3.2);
  line(-span * 0.44, -span * 0.22, span * 0.44, span * 0.22);
  strokeWeight(1.8);
  line(-span * 0.20, -span * 0.44, span * 0.20, span * 0.44);

  // 5. Secondary Bauhaus Red small circle
  push();
  translate(span * 0.26, -span * 0.28);
  fill(0.839, 0.157, 0.157, 0.95);
  noStroke();
  circle(0, 0, span * 0.10 * accent_scale);
  pop();

  // 6. Fine grid of constructivist registration ticks
  stroke(0.08, 0.08, 0.09, 0.6);
  strokeWeight(1.2);
  for (let i = -4; i <= 4; i++) {
    const p = (i / 4.0) * (span * 0.42);
    line(p, -8, p, 8);
    line(-8, p, 8, p);
  }

  pop();
}
