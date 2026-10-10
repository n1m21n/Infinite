// Lotka-Volterra - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Predator-prey: dx = a x - b x y, dy = d x y - c y.
param("growth", 1.0, 0.3, 2);
param("hunt", 0.4, 0.1, 1);
param("span", 24, 8, 60);

function draw(t) {
  background(0.96, 0.95, 0.9);
  const a = growth, b = hunt, c = 1.0, d = 0.25, dt = 0.01;
  let x = 3, y = 1.2;
  const n = floor(t * 100) + 1;
  const X = [], Y = [];
  for (let i = 0; i < n; i++) {
    const dx = a * x - b * x * y, dy = d * x * y - c * y;
    x += dx * dt; y += dy * dt;
    X.push(x); Y.push(y);
  }
  const W = width, H = height, top = H * 0.46;
  noFill(); strokeWeight(3);
  const first = max(0, n - floor(span * 100));
  stroke(0.2, 0.45, 0.75);
  beginShape();
  for (let i = first; i < n; i += 2) vertex(W * 0.06 + (i - first) / (n - first) * W * 0.88, top - X[i] * H * 0.035);
  endShape();
  stroke(0.85, 0.35, 0.25);
  beginShape();
  for (let i = first; i < n; i += 2) vertex(W * 0.06 + (i - first) / (n - first) * W * 0.88, top - Y[i] * H * 0.07);
  endShape();
  stroke(0.15, 0.15, 0.2);
  strokeWeight(2);
  beginShape();
  for (let i = max(0, n - 700); i < n; i += 2) vertex(W / 2 + (X[i] - 4) * H * 0.07, H * 0.9 - Y[i] * H * 0.12);
  endShape();
  noStroke(); fill(0.85, 0.35, 0.25);
  circle(W / 2 + (X[n - 1] - 4) * H * 0.07, H * 0.9 - Y[n - 1] * H * 0.12, 14);
}
