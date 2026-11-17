// Hilma Svanen Orbital Diagram - Infinite Library (Sketch 2D)
param("rings", 5, 3, 7);
param("orbits", 12, 6, 20);
param("drift", 0.10, 0.0, 0.45);
param("compass_alpha", 0.38, 0.1, 0.8);

function draw(t) {
  // Vermilion terracotta oil ground (#C83E2B)
  background(0.784, 0.243, 0.169);

  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const R = min(w, h) * 0.36;

  // Fine architectural plate border & subtle corner quadrant arcs
  noFill();
  stroke(0.949, 0.937, 0.914, 0.38);
  strokeWeight(1.2);
  rect(w * 0.05, h * 0.05, w * 0.90, h * 0.90);
  rect(w * 0.065, h * 0.065, w * 0.87, h * 0.87);
  for (let i = 0; i < 4; i++) {
    const cx0 = (i % 2 === 0) ? w * 0.065 : w * 0.935;
    const cy0 = (i < 2) ? h * 0.065 : h * 0.935;
    circle(cx0, cy0, R * 0.42);
    circle(cx0, cy0, R * 0.26);
  }

  // Fine architectural compass construction lines & radiating spokes
  noFill();
  stroke(0.949, 0.937, 0.914, compass_alpha * 0.75);
  strokeWeight(1.2);
  line(cx - R * 1.32, cy, cx + R * 1.32, cy);
  line(cx, cy - R * 1.32, cx, cy + R * 1.32);
  for (let d = 0; d < 24; d++) {
    const da = (d / 24) * TAU;
    line(cx + cos(da) * R * 0.18, cy + sin(da) * R * 0.18, cx + cos(da) * R * 1.30, cy + sin(da) * R * 1.30);
  }
  for (let r = 0; r < 6; r++) {
    circle(cx, cy, R * (2.06 + r * 0.11));
  }

  // Outer botanical-astronomical degree ticks & vesica arcs
  const nOrb = floor(orbits);
  for (let i = 0; i < nOrb * 3; i++) {
    const a = (i / (nOrb * 3)) * TAU + t * drift * 0.3;
    const major = (i % 3 === 0);
    const x1 = cx + cos(a) * R * 1.04;
    const y1 = cy + sin(a) * R * 1.04;
    const x2 = cx + cos(a) * R * (major ? 1.20 : 1.11);
    const y2 = cy + sin(a) * R * (major ? 1.20 : 1.11);
    stroke(0.949, 0.937, 0.914, major ? 0.78 : 0.42);
    strokeWeight(major ? 1.8 : 1.0);
    line(x1, y1, x2, y2);
  }

  // Diagonal spiritualist petal arcs around outer ring
  stroke(0.898, 0.737, 0.220, 0.45);
  strokeWeight(1.3);
  for (let q = 0; q < 4; q++) {
    const qa = q * 1.570796 + 0.785398;
    circle(cx + cos(qa) * R * 0.88, cy + sin(qa) * R * 0.88, R * 0.56);
  }

  // Split-hemisphere concentric rings (triangle fans for solid fill)
  const nRings = floor(rings);
  const segs = 36;
  noStroke();
  for (let k = nRings; k >= 1; k--) {
    const u = k / nRings;
    const rr = R * u;

    // Left hemisphere (x <= cx): Chalk white & Obsidian black
    if (k % 2 === 1) {
      fill(0.949, 0.937, 0.914, 0.98);
    } else {
      fill(0.078, 0.082, 0.094, 0.98);
    }
    for (let s = 0; s < segs; s++) {
      const a0 = 1.570796 + (s / segs) * 3.141593;
      const a1 = 1.570796 + ((s + 1) / segs) * 3.141593;
      triangle(cx, cy, cx + cos(a0) * rr, cy + sin(a0) * rr, cx + cos(a1) * rr, cy + sin(a1) * rr);
    }

    // Right hemisphere (x >= cx): Cerulean, Cadmium, Dusty Rose
    if (k === nRings) {
      fill(0.282, 0.557, 0.784, 0.98);
    } else if (k === nRings - 1) {
      fill(0.898, 0.737, 0.220, 0.98);
    } else if (k % 2 === 1) {
      fill(0.910, 0.616, 0.620, 0.98);
    } else {
      fill(0.282, 0.557, 0.784, 0.96);
    }
    for (let s = 0; s < segs; s++) {
      const a0 = -1.570796 + (s / segs) * 3.141593;
      const a1 = -1.570796 + ((s + 1) / segs) * 3.141593;
      triangle(cx, cy, cx + cos(a0) * rr, cy + sin(a0) * rr, cx + cos(a1) * rr, cy + sin(a1) * rr);
    }
  }

  // Crisp meridian divider & subtle gold ring outlines
  stroke(0.078, 0.082, 0.094, 0.85);
  strokeWeight(2.2);
  line(cx, cy - R, cx, cy + R);

  noFill();
  for (let k = 1; k <= nRings; k++) {
    const rr = R * (k / nRings);
    stroke(0.078, 0.082, 0.094, 0.35);
    strokeWeight(1.2);
    circle(cx, cy, rr * 2.0);
  }

  // Orbiting dual-polarity satellites on outer ring
  for (let s = 0; s < 2; s++) {
    const a = t * drift + s * 3.141593;
    const sx = cx + cos(a) * R * 1.10;
    const sy = cy + sin(a) * R * 1.10;
    fill(s === 0 ? 0.949 : 0.078, s === 0 ? 0.937 : 0.082, s === 0 ? 0.914 : 0.094, 0.96);
    stroke(0.898, 0.737, 0.220, 0.85);
    strokeWeight(1.8);
    circle(sx, sy, R * 0.11);
  }

  // Central vermilion seed triangle
  noStroke();
  fill(0.784, 0.243, 0.169, 1.0);
  circle(cx, cy, R * 0.065);
}
