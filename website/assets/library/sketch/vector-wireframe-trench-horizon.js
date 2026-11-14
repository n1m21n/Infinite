// Vector Wireframe Trench Horizon - Infinite Library (Sketch 2D)
param("grid_depth", 14, 6, 24);
param("trench_width", 0.42, 0.2, 0.7);
param("reticle_size", 0.16, 0.06, 0.3);
param("scroll_speed", 0.85, 0.0, 2.5);

function draw(t) {
  background(0.015, 0.048, 0.028);

  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.48;
  const nd = floor(grid_depth);

  // Subtle CRT phosphor horizon band
  noStroke();
  fill(0.0, 0.35, 0.16, 0.22);
  rect(w * 0.04, cy - h * 0.08, w * 0.92, h * 0.16);
  fill(0.0, 0.55, 0.25, 0.16);
  circle(cx, cy, min(w, h) * 0.42);

  // Outer CRT bezel frame
  noFill();
  stroke(0.0, 1.0, 0.416, 0.7);
  strokeWeight(3.5);
  rect(w * 0.04, h * 0.04, w * 0.92, h * 0.92);

  // Horizon line
  stroke(1.0, 1.0, 1.0, 0.85);
  strokeWeight(2.0);
  line(w * 0.04, cy, w * 0.96, cy);

  // Receding perspective floor & ceiling rays
  const rays = 12;
  for (let i = -rays; i <= rays; i++) {
    const u = i / rays;
    const xEnd = cx + u * w * 0.72;
    const alpha = Math.abs(i) <= 2 ? 0.9 : 0.45;
    stroke(0.0, 1.0, 0.416, alpha);
    strokeWeight(Math.abs(i) % 3 === 0 ? 2.2 : 1.3);
    line(cx + u * w * 0.03, cy, xEnd, h * 0.96);
    line(cx + u * w * 0.03, cy, xEnd, h * 0.04);
  }

  // Scrolling perspective depth ribs
  const phase = (t * scroll_speed) % 1.0;
  for (let d = 0; d < nd; d++) {
    const u = (d + phase) / nd;
    const z = Math.pow(u, 2.1);
    const yOff = z * h * 0.48;
    const xHalf = (0.04 + z * trench_width) * w;

    stroke(0.0, 1.0, 0.416, 0.25 + z * 0.72);
    strokeWeight(1.2 + z * 2.2);

    // Lower floor rib & upper ceiling rib
    line(w * 0.04, cy + yOff, w * 0.96, cy + yOff);
    line(w * 0.04, cy - yOff, w * 0.96, cy - yOff);

    // Trench wall vertical struts
    stroke(1.0, 1.0, 1.0, 0.2 + z * 0.65);
    line(cx - xHalf, cy - yOff, cx - xHalf, cy + yOff);
    line(cx + xHalf, cy - yOff, cx + xHalf, cy + yOff);
  }

  // Central vector targeting reticle
  const rr = min(w, h) * reticle_size;
  noFill();
  stroke(1.0, 1.0, 1.0, 0.95);
  strokeWeight(2.4);
  circle(cx, cy, rr * 2.0);
  stroke(0.0, 1.0, 0.416, 0.95);
  strokeWeight(2.0);
  circle(cx, cy, rr * 1.15);

  // Diamond crosshairs
  const dOff = rr * 1.35;
  line(cx - dOff, cy, cx - rr * 0.45, cy);
  line(cx + rr * 0.45, cy, cx + dOff, cy);
  line(cx, cy - dOff, cx, cy - rr * 0.45);
  line(cx, cy + rr * 0.45, cx, cy + dOff);

  // Corner telemetry ticks
  stroke(0.0, 1.0, 0.416, 0.75);
  strokeWeight(2.0);
  for (let k = 0; k < 6; k++) {
    const ty = h * 0.12 + k * 24;
    const barW = 28 + ((k * 17) % 38);
    line(w * 0.07, ty, w * 0.07 + barW, ty);
    line(w * 0.93 - barW, ty, w * 0.93, ty);
  }
}
