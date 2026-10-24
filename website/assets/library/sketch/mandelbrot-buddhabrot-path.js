// Buddhabrot Orbit Trajectory - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Buddhabrot escaping orbit trajectory probability filaments in the complex plane with rotating critical traps and glowing probability nodes.
param("zoom", 1.15, 0.4, 3.0);
param("speed", 0.7, 0.1, 2.5);
param("samples", 120, 40, 300);
param("max_iter", 60, 20, 120);
param("hue", 0.45, 0.0, 1.0);

function draw(t) {
  background(0.043, 0.051, 0.098); // #0B0D19 midnight ground
  const cx = width * 0.46;
  const cy = height * 0.5;
  const scale = min(width, height) * 0.38 * zoom;
  const time = t * speed;
  const N = floor(samples);
  const iters = floor(max_iter);

  // Background complex coordinate axes
  stroke(0.12, 0.15, 0.28, 0.4);
  strokeWeight(1.0);
  line(cx - scale * 2.0, cy, cx + scale * 1.5, cy);
  line(cx, cy - scale * 1.4, cx, cy + scale * 1.4);

  // Trace Buddhabrot orbit trajectories
  for (let i = 0; i < N; i++) {
    // Seed points near Mandelbrot boundary
    const ang = (i / N) * TAU + time * 0.1;
    const rad = 0.5 + 0.8 * Math.sin(i * 1.37);
    const cr = -0.7 + Math.cos(ang) * rad * 1.2;
    const ci = Math.sin(ang) * rad * 1.2;

    let zr = 0;
    let zi = 0;
    const orbit = [];
    let escaped = false;

    for (let k = 0; k < iters; k++) {
      const zr_new = zr * zr - zi * zi + cr;
      const zi_new = 2.0 * zr * zi + ci;
      zr = zr_new;
      zi = zi_new;
      orbit.push([zr, zi]);
      if (zr * zr + zi * zi > 4.0) {
        escaped = true;
        break;
      }
    }

    if (escaped && orbit.length > 5) {
      const frac = orbit.length / iters;
      const col = hsl((hue + frac * 0.4) % 1.0, 0.8, 0.6, 0.35 + 0.45 * frac);
      stroke(col);
      strokeWeight(1.2);

      let prevX = cx + orbit[0][0] * scale;
      let prevY = cy + orbit[0][1] * scale;

      for (let k = 1; k < orbit.length; k++) {
        const px = cx + orbit[k][0] * scale;
        const py = cy + orbit[k][1] * scale;
        line(prevX, prevY, px, py);

        // Imaginary conjugate reflection for symmetric Buddhabrot
        line(prevX, 2 * cy - prevY, px, 2 * cy - py);

        prevX = px;
        prevY = py;
      }
    }
  }

  // Critical attractor nodes
  noStroke();
  const traps = [
    [-0.75, 0],
    [-0.12, 0.74],
    [-0.12, -0.74],
    [-1.25, 0],
    [0.28, 0.48],
    [0.28, -0.48]
  ];

  for (let k = 0; k < traps.length; k++) {
    const px = cx + traps[k][0] * scale;
    const py = cy + traps[k][1] * scale;
    fill(1.0, 0.42, 0.21, 0.85); // Ember
    ellipse(px, py, 10, 10);
    fill(1.0, 0.95, 0.9, 1.0);
    ellipse(px, py, 4, 4);
  }
}
