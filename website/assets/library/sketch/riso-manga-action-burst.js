// Riso Manga Action Burst - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// High-energy manga speedline radial explosion with fluorescent pink dot screens and cyan kinetic ink accents.
param("burst_intensity", 1.0, 0.2, 2.5);
param("ray_count", 36.0, 12.0, 72.0);
param("misregister", 4.0, 0.0, 12.0);
param("focus_x", 0.45, 0.2, 0.8);
param("focus_y", 0.48, 0.2, 0.8);

function draw(t) {
  // Warm riso newsprint cream background
  background(0.89, 0.86, 0.80);

  const cx = width * focus_x;
  const cy = height * focus_y;
  const maxR = min(width, height) * 0.75 * burst_intensity;
  const rays = floor(ray_count);
  const off = misregister;

  // Background concentric shockwave rings in Fluorescent Pink
  noFill();
  stroke(1.0, 0.0, 0.467, 0.55);
  strokeWeight(3.0);
  for (let r = 80; r < maxR; r += 70) {
    const rad = r + sin(t * 1.5 + r * 0.02) * 12.0;
    circle(cx + off, cy - off, rad);
  }

  // Cyan misregistration ghost rays
  noStroke();
  fill(0.0, 0.941, 1.0, 0.45);
  for (let i = 0; i < rays; i++) {
    if (i % 2 === 0) {
      const a1 = (i / rays) * TAU;
      const a2 = ((i + 0.6) / rays) * TAU;
      const len = maxR * (0.85 + 0.3 * sin(i * 1.7 + t));
      beginShape();
      vertex(cx - off, cy + off);
      vertex(cx - off + cos(a1) * len, cy + off + sin(a1) * len);
      vertex(cx - off + cos(a2) * len, cy + off + sin(a2) * len);
      endShape();
    }
  }

  // Carbon black keyline kinetic speedline triangles
  fill(0.067, 0.067, 0.067, 0.95);
  for (let i = 0; i < rays; i++) {
    const a1 = (i / rays) * TAU;
    const a2 = ((i + 0.45) / rays) * TAU;
    const len = maxR * (0.9 + 0.25 * cos(i * 2.3 + t * 0.8));
    beginShape();
    vertex(cx, cy);
    vertex(cx + cos(a1) * len, cy + sin(a1) * len);
    vertex(cx + cos(a2) * len, cy + sin(a2) * len);
    endShape();
  }

  // Central impact core: pure white flash aperture with yellow starburst
  fill(0.98, 0.96, 0.90);
  circle(cx, cy, 140.0);

  fill(1.0, 0.933, 0.0); // Process Yellow impact star
  beginShape();
  const starPts = 12;
  for (let s = 0; s < starPts; s++) {
    const sa1 = (s / starPts) * TAU;
    const sa2 = ((s + 0.5) / starPts) * TAU;
    const sr1 = 65.0;
    const sr2 = 24.0;
    vertex(cx + cos(sa1) * sr1, cy + sin(sa1) * sr1);
    vertex(cx + cos(sa2) * sr2, cy + sin(sa2) * sr2);
  }
  endShape();

  // Central black pinpoint
  fill(0.067, 0.067, 0.067);
  circle(cx, cy, 18.0);
}
