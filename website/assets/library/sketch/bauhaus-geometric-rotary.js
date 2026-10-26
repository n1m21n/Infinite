// Bauhaus Geometric Rotary - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Concentric circular arcs, rotating triangular wedges, and cantilever balance bars in dynamic equilibrium.
param("disc_radius", 0.42, 0.15, 0.85);
param("rot_speed", 0.35, 0.05, 1.5);
param("bars_count", 6.0, 3.0, 12.0);
param("line_weight", 2.2, 0.5, 6.0);
param("accent_scale", 1.0, 0.4, 2.0);

function draw(t) {
  // Warm Bauhaus cream ground #F2E8CF calibrated for luminance 14-228
  background(0.85, 0.82, 0.76);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const minDim = min(width, height);
  const R = minDim * disc_radius;
  const time = t * rot_speed;

  push();
  translate(cx, cy);

  // Outer framing square
  noFill();
  stroke(0.067, 0.067, 0.067);
  strokeWeight(line_weight * 1.5);
  rect(-minDim * 0.44, -minDim * 0.44, minDim * 0.88, minDim * 0.88);

  // 1. Large Cobalt Blue background balance crescent
  push();
  rotate(time * 0.2);
  noStroke();
  fill(0.106, 0.231, 0.561); // #1B3B8F
  circle(-R * 0.15, -R * 0.1, R * 1.35);
  fill(0.85, 0.82, 0.76);
  circle(R * 0.2, R * 0.1, R * 1.15);
  pop();

  // 2. Vermilion Red central dynamic sector
  push();
  rotate(-time * 0.4 + 0.5);
  fill(0.847, 0.188, 0.122); // #D8301F
  beginShape();
  vertex(0, 0);
  for (let k = 0; k <= 24; k++) {
    const a = -1.2 + (2.4 * k / 24.0);
    vertex(cos(a) * R * 0.95, sin(a) * R * 0.95);
  }
  endShape(1);
  pop();

  // 3. Cadmium Yellow cantilever balance bars
  const bars = floor(bars_count);
  for (let b = 0; b < bars; b++) {
    const angle = (b / bars) * 6.2831853 + time * 0.15;
    push();
    rotate(angle);
    noStroke();
    if (b % 2 === 0) {
      fill(0.949, 0.718, 0.020); // #F2B705
      rect(R * 0.35, -minDim * 0.025, R * 0.55, minDim * 0.05);
    } else {
      fill(0.067, 0.067, 0.067);
      rect(R * 0.4, -minDim * 0.015, R * 0.45, minDim * 0.03);
    }
    pop();
  }

  // 4. Heavy Carbon black central hub and concentric precision rings
  noFill();
  stroke(0.067, 0.067, 0.067);
  strokeWeight(line_weight);
  ellipse(0, 0, R * 1.8, R * 1.8);
  ellipse(0, 0, R * 1.2, R * 1.2);
  ellipse(0, 0, R * 0.6, R * 0.6);

  // Central solid core
  noStroke();
  fill(0.067, 0.067, 0.067);
  circle(0, 0, R * 0.28);
  fill(0.949, 0.718, 0.020);
  circle(0, 0, R * 0.1);

  pop();
}
