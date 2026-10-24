// Ulam Spiral Primes - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Ulam prime number polar spiral constellation highlighting diagonal prime alignments, quadratic polynomial trajectories, and breathing harmonic nodes.
param("zoom", 1.25, 0.4, 3.0);
param("speed", 0.6, 0.1, 2.5);
param("max_n", 1200, 300, 2400);
param("dot_size", 3.5, 1.0, 8.0);
param("hue_shift", 0.65, 0.0, 1.0);

function isPrime(n) {
  if (n < 2) return false;
  if (n === 2 || n === 3) return true;
  if (n % 2 === 0 || n % 3 === 0) return false;
  for (let i = 5, w = 2; i * i <= n; i += w, w = 6 - w) {
    if (n % i === 0) return false;
  }
  return true;
}

function draw(t) {
  background(0.043, 0.051, 0.098); // #0B0D19 midnight ground
  const cx = width * 0.5;
  const cy = height * 0.5;
  const limit = floor(max_n);
  const time = t * speed;
  const scale = min(width, height) * 0.42 * zoom;

  // Background polar rings
  noFill();
  strokeWeight(1.0);
  for (let r = 1; r <= 5; r++) {
    stroke(0.12, 0.15, 0.28, 0.35);
    ellipse(cx, cy, scale * (r / 5) * 2.0, scale * (r / 5) * 2.0);
  }

  // Faint Archimedean spiral trajectory
  stroke(0.14, 0.17, 0.32, 0.25);
  strokeWeight(1.0);
  let prevX = cx;
  let prevY = cy;
  const maxSqrt = Math.sqrt(limit);

  for (let n = 1; n <= limit; n += 2) {
    const rFrac = Math.sqrt(n) / maxSqrt;
    const theta = Math.sqrt(n) * 2.5 + time * 0.15;
    const r = rFrac * scale;
    const px = cx + r * Math.cos(theta);
    const py = cy + r * Math.sin(theta);
    if (n > 1) {
      line(prevX, prevY, px, py);
    }
    prevX = px;
    prevY = py;
  }

  // Draw prime constellation nodes and Euler polynomial diagonals
  noStroke();
  for (let n = 2; n <= limit; n++) {
    if (isPrime(n)) {
      const frac = n / limit;
      const theta = Math.sqrt(n) * 2.5 + time * 0.15;
      const r = (Math.sqrt(n) / maxSqrt) * scale;
      const px = cx + r * Math.cos(theta);
      const py = cy + r * Math.sin(theta);

      // Check Euler's prime polynomial n = x^2 - x + 41
      const isEuler = ((Math.sqrt(4 * n - 163) + 1) * 0.5) % 1.0 < 0.001;

      if (isEuler) {
        fill(1.0, 0.42, 0.21, 0.95); // Ember
        ellipse(px, py, dot_size * 2.2, dot_size * 2.2);
        fill(1.0, 0.95, 0.9, 1.0);
        ellipse(px, py, dot_size * 0.9, dot_size * 0.9);
      } else {
        const hue = (hue_shift + frac * 0.35) % 1.0;
        fill(hsl(hue, 0.85, 0.65, 0.75 + 0.25 * sin(time * 2.0 + frac * 6.28)));
        ellipse(px, py, dot_size * 1.4, dot_size * 1.4);
      }
    }
  }

  // Center glowing core
  fill(1.0, 0.42, 0.21, 0.9);
  ellipse(cx, cy, 14, 14);
  fill(1.0, 1.0, 1.0, 1.0);
  ellipse(cx, cy, 6, 6);
}
