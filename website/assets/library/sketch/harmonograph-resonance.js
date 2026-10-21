// Harmonograph Resonance - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Rotary three-pendulum harmonograph with decaying elliptical Lissajous interference.
param("damping", 0.0018, 0.0002, 0.008);
param("freq_ratio", 2.01, 1.0, 4.0);
param("phase_shift", 0.25, 0.0, 1.0);
param("speed", 1.2, 0.2, 3.0);

function draw(t) {
  background(0.04, 0.05, 0.09);
  const cx = width / 2;
  const cy = height / 2;
  const scale = min(width, height) * 0.42;

  const f1 = 2.001;
  const f2 = f1 * freq_ratio;
  const f3 = 1.000;
  const f4 = f3 * (freq_ratio * 0.5);

  const p1 = t * speed * 0.1;
  const p2 = p1 + phase_shift * TAU;
  const p3 = p1 * 0.7;
  const p4 = p2 * 1.1;

  const steps = 1800;
  const dt = 0.045;
  let prevX = 0, prevY = 0;

  noFill();
  for (let i = 0; i < steps; i++) {
    const s = i * dt;
    const decay = exp(-damping * i);

    // Lateral + rotary oscillation superposition
    const x = (sin(f1 * s + p1) * 0.55 + sin(f2 * s + p2) * 0.45) * decay;
    const y = (cos(f3 * s + p3) * 0.55 + cos(f4 * s + p4) * 0.45) * decay;

    const px = cx + x * scale;
    const py = cy + y * scale;

    if (i > 1) {
      const frac = i / steps;
      // Brand color gradient: signal blue into ember orange
      const hue = (0.62 + frac * 0.42) % 1.0;
      const alpha = 0.25 + 0.65 * (1.0 - frac);
      stroke(hsl(hue, 0.85, 0.68, alpha));
      strokeWeight(1.2 + (1.0 - frac) * 1.0);
      line(prevX, prevY, px, py);
    }
    prevX = px;
    prevY = py;
  }
}
