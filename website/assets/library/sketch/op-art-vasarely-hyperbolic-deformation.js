// Op Art Vasarely Hyperbolic Deformation - Infinite Library (Sketch 2D)
param("grid_size", 22.0, 12.0, 36.0);
param("bulge_radius", 360.0, 150.0, 500.0);
param("bulge_power", 1.8, 0.6, 3.2);
param("contrast", 1.2, 0.8, 1.8);

function draw(t) {
  // Midnight ground (#0c0d12)
  background(0.047, 0.051, 0.071);

  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const margin = 64;
  const innerW = w - margin * 2;
  const innerH = h - margin * 2;

  // Framing boundary
  stroke(0.92, 0.92, 0.95);
  strokeWeight(2.0);
  noFill();
  rect(margin, margin, innerW, innerH);

  // Hyperbolic spherical transformation function
  function warpPoint(px, py) {
    const dx = px - cx;
    const dy = py - cy;
    const dist = Math.sqrt(dx * dx + dy * dy);
    if (dist < bulge_radius && dist > 0.001) {
      const normDist = dist / bulge_radius;
      const factor = Math.sin((normDist * Math.PI) * 0.5);
      const warpedDist = Math.pow(factor, 1.0 / bulge_power) * bulge_radius;
      const scale = warpedDist / dist;
      return [cx + dx * scale, cy + dy * scale];
    }
    return [px, py];
  }

  const n = floor(grid_size);
  const stepX = innerW / n;
  const stepY = innerH / n;

  noStroke();
  for (let y = 0; y < n; y++) {
    for (let x = 0; x < n; x++) {
      const p0 = warpPoint(margin + x * stepX, margin + y * stepY);
      const p1 = warpPoint(margin + (x + 1) * stepX, margin + y * stepY);
      const p2 = warpPoint(margin + (x + 1) * stepX, margin + (y + 1) * stepY);
      const p3 = warpPoint(margin + x * stepX, margin + (y + 1) * stepY);

      const isEven = (x + y) % 2 === 0;
      if (isEven) {
        fill(0.95, 0.95, 0.97);
      } else {
        const cellDist = Math.hypot(p0[0] - cx, p0[1] - cy);
        if (cellDist < bulge_radius * 0.65) {
          fill(0.92, 0.22, 0.16); // Optical vermilion accent (#EB3829)
        } else {
          fill(0.12, 0.13, 0.18); // Deep charcoal
        }
      }

      triangle(p0[0], p0[1], p1[0], p1[1], p2[0], p2[1]);
      triangle(p0[0], p0[1], p2[0], p2[1], p3[0], p3[1]);
    }
  }

  // Precision concentric graticule rings
  stroke(0.95, 0.95, 0.97, 0.65);
  strokeWeight(1.2);
  noFill();
  ellipse(cx, cy, bulge_radius * 0.7, bulge_radius * 0.7);
  ellipse(cx, cy, bulge_radius * 1.4, bulge_radius * 1.4);
  ellipse(cx, cy, bulge_radius * 2.0, bulge_radius * 2.0);

  // Crosshair focal alignment
  line(cx - 36, cy, cx + 36, cy);
  line(cx, cy - 36, cx, cy + 36);
}
