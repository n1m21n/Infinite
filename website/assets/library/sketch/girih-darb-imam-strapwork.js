// Girih Darb-i Imam Strapwork - Infinite Library (Sketch 2D)
param("rosettes", 3, 2, 5);
param("strap_w", 4.5, 2.0, 9.0);
param("breathe", 0.08, 0.0, 0.25);
param("grain", 420, 100, 900);

function draw(t) {
  // Warm sandstone-ivory ground (#E0DBD0)
  background(0.880, 0.860, 0.810);
  const w = width;
  const h = height;
  const side = min(w, h);
  const cx = w * 0.5;
  const cy = h * 0.5;
  const n = floor(rosettes);
  const span = side * 0.82;
  const step = span / n;

  // Fine terracotta & mineral glaze stipple
  noStroke();
  for (let i = 0; i < grain; i++) {
    const gx = ((i * 0.6180339887) % 1.0) * w;
    const gy = ((i * 0.3819660113 + sin(i * 9.3) * 0.04) % 1.0) * h;
    fill(0.55, 0.45, 0.30, 0.14);
    circle(gx, gy, 1.5);
  }

  // Outer architectural frame
  noFill();
  stroke(0.114, 0.243, 0.478, 0.85);
  strokeWeight(3.0);
  rect(cx - span * 0.53, cy - span * 0.53, span * 1.06, span * 1.06);
  stroke(0.784, 0.608, 0.259, 0.85);
  strokeWeight(1.5);
  rect(cx - span * 0.51, cy - span * 0.51, span * 1.02, span * 1.02);

  // Decagonal girih rosette lattice
  for (let ix = 0; ix < n; ix++) {
    for (let iy = 0; iy < n; iy++) {
      const rx = cx - span * 0.5 + (ix + 0.5) * step;
      const ry = cy - span * 0.5 + (iy + 0.5) * step;
      const pulse = 1.0 + breathe * sin(t * 0.7 + ix * 1.1 + iy * 0.9);
      const R = step * 0.46 * pulse;

      // Ochre decagonal backing plate (#C89B42)
      fill(0.784, 0.608, 0.259, 0.92);
      stroke(0.102, 0.106, 0.118, 0.95);
      strokeWeight(strap_w * 0.45);
      beginShape();
      for (let k = 0; k < 10; k++) {
        const a = (k / 10) * TAU + 0.157;
        vertex(rx + cos(a) * R, ry + sin(a) * R);
      }
      endShape(CLOSE);

      // 10-pointed girih star polygon in lapis cobalt (#1D3E7A)
      fill(0.114, 0.243, 0.478, 0.96);
      stroke(0.937, 0.925, 0.886, 0.95);
      strokeWeight(strap_w * 0.55);
      beginShape();
      for (let k = 0; k < 20; k++) {
        const a = (k / 20) * TAU;
        const rad = (k % 2 === 0) ? R * 0.82 : R * 0.42;
        vertex(rx + cos(a) * rad, ry + sin(a) * rad);
      }
      endShape(CLOSE);

      // Inner bone-ivory & gold core
      fill(0.937, 0.925, 0.886, 0.95);
      stroke(0.102, 0.106, 0.118, 0.95);
      strokeWeight(strap_w * 0.4);
      beginShape();
      for (let k = 0; k < 10; k++) {
        const a = (k / 10) * TAU;
        const rad = (k % 2 === 0) ? R * 0.28 : R * 0.16;
        vertex(rx + cos(a) * rad, ry + sin(a) * rad);
      }
      endShape(CLOSE);

      // Interlaced strapwork ribs radiating to neighbors
      noFill();
      stroke(0.102, 0.106, 0.118, 0.92);
      strokeWeight(strap_w);
      for (let k = 0; k < 10; k++) {
        const a = (k / 10) * TAU;
        line(rx + cos(a) * R * 0.42, ry + sin(a) * R * 0.42,
             rx + cos(a) * R * 1.04, ry + sin(a) * R * 1.04);
      }
      stroke(0.937, 0.925, 0.886, 0.88);
      strokeWeight(strap_w * 0.36);
      for (let k = 0; k < 10; k++) {
        const a = (k / 10) * TAU;
        line(rx + cos(a) * R * 0.42, ry + sin(a) * R * 0.42,
             rx + cos(a) * R * 1.04, ry + sin(a) * R * 1.04);
      }
    }
  }
}