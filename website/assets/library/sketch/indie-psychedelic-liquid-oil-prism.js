// Indie Psychedelic Liquid Oil Prism - Infinite Library (Sketch 2D)
param("ring_count", 11, 5, 18);
param("warp_amp", 0.16, 0.04, 0.32);
param("lobe_freq", 5, 3, 9);
param("drift_speed", 0.45, 0.0, 1.5);

function draw(t) {
  // Deep plum ground (#1b0b2e)
  background(0.106, 0.043, 0.180);

  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const baseR = min(w, h) * 0.44;
  const nr = floor(ring_count);
  const lf = floor(lobe_freq);

  // Soft outer aura halos
  noStroke();
  for (let k = 0; k < 4; k++) {
    const a = (k / 4) * Math.PI * 2.0 + t * drift_speed * 0.4;
    const ox = cx + Math.cos(a) * w * 0.16;
    const oy = cy + Math.sin(a) * h * 0.16;
    if (k % 2 === 0) {
      fill(1.0, 0.42, 0.29, 0.14);
    } else {
      fill(0.18, 0.77, 0.71, 0.14);
    }
    circle(ox, oy, baseR * 1.15);
  }

  // Concentric warped oil-lens contours
  for (let i = 0; i < nr; i++) {
    const u = 1.0 - i / nr;
    const rad = baseR * (0.18 + u * 0.85);
    const phase = t * drift_speed + i * 0.55;

    if (i % 4 === 0) {
      fill(1.0, 0.42, 0.29, 0.78);
      stroke(1.0, 0.72, 0.01, 0.9);
    } else if (i % 4 === 1) {
      fill(1.0, 0.718, 0.012, 0.76);
      stroke(0.784, 0.902, 0.298, 0.9);
    } else if (i % 4 === 2) {
      fill(0.180, 0.769, 0.714, 0.74);
      stroke(0.95, 0.92, 0.98, 0.75);
    } else {
      fill(0.106, 0.043, 0.180, 0.88);
      stroke(1.0, 0.42, 0.29, 0.85);
    }
    strokeWeight(2.5);

    beginShape();
    const segs = 72;
    for (let s = 0; s < segs; s++) {
      const ang = (s / segs) * Math.PI * 2.0;
      const w1 = Math.sin(ang * lf + phase) * warp_amp;
      const w2 = Math.cos(ang * (lf - 2) - phase * 0.7) * (warp_amp * 0.55);
      const r = rad * (1.0 + w1 + w2);
      vertex(cx + Math.cos(ang) * r, cy + Math.sin(ang) * r);
    }
    endShape(CLOSE);
  }

  // Orbiting psychedelic oil droplets
  noStroke();
  for (let d = 0; d < 14; d++) {
    const ang = (d / 14) * Math.PI * 2.0 + t * drift_speed * 0.35;
    const dist = baseR * (0.68 + 0.26 * Math.sin(d * 2.1 + t * 0.5));
    const dx = cx + Math.cos(ang) * dist;
    const dy = cy + Math.sin(ang) * dist;
    const sz = 14 + (d % 3) * 9;
    if (d % 3 === 0) fill(0.784, 0.902, 0.298, 0.92);
    else if (d % 3 === 1) fill(1.0, 0.42, 0.29, 0.92);
    else fill(0.180, 0.769, 0.714, 0.92);
    circle(dx, dy, sz);
  }
}
