// Fourier Epicycles - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Multi-frequency complex epicyclic planetary drawing synthesizing harmonic rosettes.
param("harmonics", 4.0, 1.0, 8.0);
param("ratio", 2.6, 1.1, 5.0);
param("decay", 0.65, 0.2, 0.95);
param("points", 3600, 1000, 8000);
param("zoom", 1.25, 0.5, 3.0);
param("speed", 0.45, 0.0, 2.0);

function draw(t) {
  background(0.082, 0.098, 0.188); // brand midnight #151930
  const cx = width / 2;
  const cy = height / 2;
  const scale = min(width, height) * 0.18 * zoom;

  const steps = floor(points);
  const totalRot = Math.PI * 2 * harmonics;
  const pts = [];

  const baseSpeed = t * speed;

  for (let i = 0; i <= steps; i++) {
    const theta = (i / steps) * totalRot;

    // 4-stage nested epicyclic vector sum
    let x = Math.cos(theta + baseSpeed);
    let y = Math.sin(theta + baseSpeed);

    let r = decay;
    let freq = ratio;

    x += Math.cos(theta * freq - baseSpeed * 1.5) * r;
    y += Math.sin(theta * freq - baseSpeed * 1.5) * r;

    r *= decay;
    freq *= ratio;
    x += Math.cos(theta * freq + baseSpeed * 2.2) * r;
    y += Math.sin(theta * freq + baseSpeed * 2.2) * r;

    r *= decay;
    freq *= ratio;
    x += Math.cos(theta * freq - baseSpeed * 3.1) * r;
    y += Math.sin(theta * freq - baseSpeed * 3.1) * r;

    pts.push([cx + x * scale, cy - y * scale]);
  }

  noFill();
  strokeWeight(2.0);

  for (let i = 1; i < pts.length; i++) {
    const frac = i / pts.length;
    // Radiant brand gradient: signal #A7B1FF -> emerald #4EF2B8 -> ember #FF6B35
    stroke(hsl(0.62 - 0.58 * frac, 0.88, 0.5 + 0.3 * Math.sin(frac * Math.PI)));
    line(pts[i - 1][0], pts[i - 1][1], pts[i][0], pts[i][1]);
  }
}
