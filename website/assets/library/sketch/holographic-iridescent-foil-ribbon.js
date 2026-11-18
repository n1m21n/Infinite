// Holographic Iridescent Foil Ribbon - Infinite Library (Sketch 2D)
param("ribbon_count", 14, 6, 24);
param("twist_freq", 3.2, 1.0, 6.0);
param("wave_amp", 0.22, 0.06, 0.38);
param("drift_speed", 0.5, 0.0, 1.5);

function draw(t) {
  background(0.063, 0.078, 0.157);
  const w = width;
  const h = height;
  const nr = floor(ribbon_count);

  // Soft iridescent halo glow
  noStroke();
  for (let k = 0; k < 5; k++) {
    const ang = (k / 5) * Math.PI * 2.0 + t * drift_speed * 0.3;
    const gx = w * 0.5 + Math.cos(ang) * w * 0.18;
    const gy = h * 0.5 + Math.sin(ang) * h * 0.18;
    if (k % 3 === 0) fill(0.663, 0.941, 1.0, 0.12);
    else if (k % 3 === 1) fill(1.0, 0.702, 0.961, 0.12);
    else fill(0.839, 1.0, 0.620, 0.12);
    circle(gx, gy, w * 0.52);
  }

  // Twisting holographic foil ribbons
  for (let i = 0; i < nr; i++) {
    const u = i / max(1, nr - 1);
    const yBase = h * (0.14 + u * 0.72);
    const phase = t * drift_speed + i * 0.42;

    if (i % 3 === 0) {
      fill(0.663, 0.941, 1.0, 0.42);
      stroke(1.0, 1.0, 1.0, 0.85);
    } else if (i % 3 === 1) {
      fill(1.0, 0.702, 0.961, 0.42);
      stroke(0.663, 0.941, 1.0, 0.85);
    } else {
      fill(0.839, 1.0, 0.620, 0.40);
      stroke(1.0, 0.702, 0.961, 0.85);
    }
    strokeWeight(2.2);

    const segs = 64;
    const bandH = h * (0.045 + 0.025 * Math.sin(i * 1.1 + t * 0.4));
    beginShape();
    for (let s = 0; s <= segs; s++) {
      const sx = (s / segs) * w;
      const nx = s / segs;
      const dy = Math.sin(nx * Math.PI * twist_freq + phase) * h * wave_amp * 0.45
               + Math.cos(nx * Math.PI * 1.5 - phase * 0.6) * h * 0.06;
      vertex(sx, yBase + dy - bandH * 0.5);
    }
    for (let s = segs; s >= 0; s--) {
      const sx = (s / segs) * w;
      const nx = s / segs;
      const dy = Math.sin(nx * Math.PI * twist_freq + phase) * h * wave_amp * 0.45
               + Math.cos(nx * Math.PI * 1.5 - phase * 0.6) * h * 0.06;
      const pinch = 0.5 + 0.5 * Math.cos(nx * Math.PI * 2.0 + i * 0.5);
      vertex(sx, yBase + dy + bandH * (0.3 + 0.7 * pinch));
    }
    endShape(CLOSE);
  }
}
