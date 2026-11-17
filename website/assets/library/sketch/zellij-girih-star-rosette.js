// Zellij Girih Star Rosette - Infinite Library (Sketch 2D)
param("folds", 12, 8, 16);
param("rings", 4, 2, 6);
param("strap_w", 0.018, 0.008, 0.035);
param("breathe", 0.12, 0.0, 0.35);

function draw(t) {
  // Warm limestone plaster ground (#DDD6C8)
  background(0.867, 0.839, 0.784);

  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const R = min(w, h) * 0.44;
  const n = floor(folds);
  const rCount = floor(rings);

  // Subtle hand-chiseled zellij tile mortar grid in background
  stroke(0.78, 0.74, 0.66, 0.38);
  strokeWeight(1.0);
  const gridN = 12;
  for (let i = -gridN; i <= gridN; i++) {
    const off = (i / gridN) * R * 1.15;
    line(cx + off, cy - R * 1.15, cx + off, cy + R * 1.15);
    line(cx - R * 1.15, cy + off, cx + R * 1.15, cy + off);
  }

  // Corner satellite 8-fold medallions (filled triangles)
  for (let c = 0; c < 4; c++) {
    const ca = (c + 0.5) * TAU * 0.25;
    const mx = cx + cos(ca) * R * 0.96;
    const my = cy + sin(ca) * R * 0.96;
    const mr = R * 0.24;
    fill(0.102, 0.204, 0.471, 0.92); // Cobalt indigo
    noStroke();
    for (let k = 0; k < 16; k++) {
      const a0 = (k / 16) * TAU + t * 0.06;
      const a1 = ((k + 1) / 16) * TAU + t * 0.06;
      const r0 = (k % 2 === 0) ? mr : mr * 0.52;
      const r1 = ((k + 1) % 2 === 0) ? mr : mr * 0.52;
      triangle(mx, my, mx + cos(a0) * r0, my + sin(a0) * r0, mx + cos(a1) * r1, my + sin(a1) * r1);
    }
  }

  // Outer octagonal cobalt frame ring
  noFill();
  stroke(0.102, 0.204, 0.471, 0.92);
  strokeWeight(R * 0.036);
  circle(cx, cy, R * 1.98);
  stroke(0.851, 0.643, 0.149, 0.95); // Saffron ochre trim
  strokeWeight(R * 0.012);
  circle(cx, cy, R * 1.88);

  // Concentric star-polygon rosette rings (solid glazed tile wedges)
  for (let r = rCount; r >= 1; r--) {
    const u = r / rCount;
    const radOuter = R * (0.20 + 0.72 * u) * (1.0 + breathe * 0.04 * sin(t * 0.7 + r));
    const radInner = radOuter * (0.58 + 0.06 * sin(t * 0.4 + r * 1.1));
    const rot = t * 0.04 * (r % 2 === 0 ? 1 : -1) + (r % 2) * (TAU / (n * 2));

    noStroke();
    if (r % 3 === 0) {
      fill(0.102, 0.204, 0.471, 0.94); // Cobalt
    } else if (r % 3 === 1) {
      fill(0.851, 0.643, 0.149, 0.92); // Saffron ochre
    } else {
      fill(0.165, 0.420, 0.290, 0.92); // Glazed emerald
    }
    for (let i = 0; i < n * 2; i++) {
      const a0 = rot + (i / (n * 2)) * TAU;
      const a1 = rot + ((i + 1) / (n * 2)) * TAU;
      const r0 = (i % 2 === 0) ? radOuter : radInner;
      const r1 = ((i + 1) % 2 === 0) ? radOuter : radInner;
      triangle(cx, cy, cx + cos(a0) * r0, cy + sin(a0) * r0, cx + cos(a1) * r1, cy + sin(a1) * r1);
    }

    // Crisp bone-ivory mortar outline & interlaced girih strapwork
    noFill();
    stroke(0.937, 0.922, 0.878, 0.96);
    strokeWeight(R * strap_w * 1.4);
    for (let i = 0; i < n * 2; i++) {
      const a0 = rot + (i / (n * 2)) * TAU;
      const a1 = rot + ((i + 1) / (n * 2)) * TAU;
      const r0 = (i % 2 === 0) ? radOuter : radInner;
      const r1 = ((i + 1) % 2 === 0) ? radOuter : radInner;
      line(cx + cos(a0) * r0, cy + sin(a0) * r0, cx + cos(a1) * r1, cy + sin(a1) * r1);
    }
  }

  // Central khatam star seal
  noStroke();
  fill(0.722, 0.298, 0.173, 0.98); // Glazed terracotta
  for (let i = 0; i < n * 2; i++) {
    const a0 = (i / (n * 2)) * TAU - t * 0.05;
    const a1 = ((i + 1) / (n * 2)) * TAU - t * 0.05;
    const r0 = (i % 2 === 0) ? R * 0.20 : R * 0.105;
    const r1 = ((i + 1) % 2 === 0) ? R * 0.20 : R * 0.105;
    triangle(cx, cy, cx + cos(a0) * r0, cy + sin(a0) * r0, cx + cos(a1) * r1, cy + sin(a1) * r1);
  }

  noStroke();
  fill(0.937, 0.922, 0.878, 0.95);
  circle(cx, cy, R * 0.075);
}
