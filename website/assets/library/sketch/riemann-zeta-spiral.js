// Riemann Zeta Spiral - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Polar Dirichlet and Riemann zeta spiral harmonics with continuous critical-strip phase rotation and harmonic orbital nodes.
param("harmonics", 20, 6, 36);
param("critical_s", 0.5, 0.1, 0.9);
param("speed", 0.75, 0.1, 3.0);
param("zoom", 1.25, 0.4, 3.0);
param("hue_shift", 0.62, 0.0, 1.0);

function draw(t) {
  background(0.043, 0.051, 0.098); // #0B0D19 midnight ground
  const cx = width * 0.5;
  const cy = height * 0.5;
  const numN = floor(harmonics);
  const time = t * speed;
  const baseScale = min(width, height) * 0.18 * zoom;

  // Draw background orbital phase guides
  noFill();
  strokeWeight(1.0);
  for (let r = 1; r <= 5; r++) {
    stroke(0.12, 0.15, 0.28, 0.35);
    ellipse(cx, cy, baseScale * r * 0.9, baseScale * r * 0.9);
  }

  // Trace continuous partial sum trajectory of sum n^(-s - i*t)
  const steps = 380;
  let prevX = cx;
  let prevY = cy;

  for (let step = 0; step <= steps; step++) {
    const frac = step / steps;
    const currentT = time + frac * 4.5;
    let sumX = 0;
    let sumY = 0;

    for (let n = 1; n <= numN; n++) {
      const mag = Math.pow(n, -critical_s);
      const phase = -currentT * Math.log(n);
      sumX += mag * Math.cos(phase);
      sumY += mag * Math.sin(phase);
    }

    const px = cx + sumX * baseScale;
    const py = cy + sumY * baseScale;

    if (step > 0) {
      const hue = (hue_shift + frac * 0.45) % 1.0;
      const alpha = 0.25 + 0.75 * frac;
      strokeWeight(1.4 + 1.8 * frac);
      stroke(hsl(hue, 0.85, 0.65, alpha));
      line(prevX, prevY, px, py);
    }
    prevX = px;
    prevY = py;
  }

  // Draw orbital harmonic nodal satellites
  const leadT = time + 4.5;
  let accumX = 0;
  let accumY = 0;
  for (let n = 1; n <= numN; n++) {
    const mag = Math.pow(n, -critical_s);
    const phase = -leadT * Math.log(n);
    const nx = accumX + mag * Math.cos(phase);
    const ny = accumY + mag * Math.sin(phase);

    if (n % 2 === 0 || n === numN) {
      const px = cx + nx * baseScale;
      const py = cy + ny * baseScale;
      fill(hsl((hue_shift + n / numN * 0.5) % 1.0, 0.9, 0.75));
      noStroke();
      const dotR = 3.5 + 2.5 * Math.sin(t * 2.0 + n);
      circle(px, py, dotR);
    }
    accumX = nx;
    accumY = ny;
  }

  // Glowing current critical tip
  fill(1.0, 0.42, 0.21, 0.95); // ember glow
  noStroke();
  circle(prevX, prevY, 7.0 + 3.0 * Math.sin(t * 4.0));
  fill(1.0, 0.95, 0.9, 1.0);
  circle(prevX, prevY, 3.5);
}
