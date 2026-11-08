// Generative Plotter Curved Flow Lines - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Algorithmic pen plotter study of continuous vector streamlines deflected through a trigonometric vector force field in carbon ink with vermilion accents on archival paper.
param("density", 36.0, 16.0, 64.0);
param("curvature", 1.35, 0.5, 2.5);
param("speed", 0.35, 0.1, 1.2);
param("line_width", 2.2, 1.0, 4.0);

function draw(t) {
  // Archival tinted cream paper ground (#e8e0d0)
  background(0.89, 0.86, 0.80);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;

  push();
  translate(cx, cy);

  // Precision mechanical framing boundary (#111111)
  stroke(0.067, 0.067, 0.067);
  strokeWeight(2.6);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  const time = t * speed;
  const count = floor(density);

  // 1. Carbon ink vector streamlines
  for (let i = 0; i < count; i++) {
    const norm = (i / (count - 1)) * 2.0 - 1.0;
    const y0 = norm * span * 0.44;

    const isAccent = (i % 6 === 0);
    if (isAccent) {
      stroke(0.820, 0.188, 0.122, 0.95); // Vermilion red (#d1301f)
      strokeWeight(line_width * 1.5);
    } else {
      stroke(0.067, 0.067, 0.067, 0.88); // Carbon black (#111111)
      strokeWeight(line_width * 0.9);
    }
    noFill();

    beginShape();
    const steps = 44;
    for (let s = 0; s <= steps; s++) {
      const u = (s / steps) * 2.0 - 1.0;
      const x = u * span * 0.44;
      const angle = sin(u * 2.5 * curvature + time * 1.4) * cos(norm * 3.0 + time * 0.8);
      const warp = sin(angle * Math.PI) * (span * 0.08 * curvature);
      const y = y0 + warp;
      vertex(x, y);
    }
    endShape();
  }

  // 2. Central vermilion plotter focus rosette
  push();
  rotate(time * 0.5);
  stroke(0.820, 0.188, 0.122, 0.92);
  strokeWeight(2.2);
  noFill();
  for (let r = 0; r < 8; r++) {
    const ang = (r / 8.0) * Math.PI * 2.0;
    push();
    rotate(ang);
    line(0, span * 0.08, 0, span * 0.22);
    circle(0, span * 0.24, 7.0);
    pop();
  }
  pop();

  // 3. Precision registration tick marks
  stroke(0.067, 0.067, 0.067);
  strokeWeight(1.6);
  for (let k = -8; k <= 8; k++) {
    const p = (k / 8.0) * (span * 0.44);
    line(p, -span * 0.48, p, -span * 0.46);
    line(p, span * 0.46, p, span * 0.48);
    line(-span * 0.48, p, -span * 0.46, p);
    line(span * 0.46, p, span * 0.48, p);
  }

  pop();
}
