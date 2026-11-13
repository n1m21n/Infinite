// Indie Psychedelic Chromatic Mycelium - Infinite Library (Sketch 2D)
param("branch_count", 18, 8, 32);
param("curl_amp", 0.38, 0.1, 0.8);
param("node_rings", 7, 3, 12);
param("flow_rate", 0.55, 0.0, 1.5);

function draw(t) {
  // Deep violet background (#140924)
  background(0.078, 0.035, 0.141);

  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const nb = floor(branch_count);
  const nr = floor(node_rings);

  // Subtle concentric harmonic wave bands
  noFill();
  for (let r = 1; r <= nr; r++) {
    const rad = (r / nr) * min(w, h) * 0.46;
    stroke(0.28, 0.79, 0.89, 0.22);
    strokeWeight(1.8);
    circle(cx, cy, rad * 2.0);
  }

  // Sinuous radiating mycelium tendrils
  for (let i = 0; i < nb; i++) {
    const baseAng = (i / nb) * Math.PI * 2.0;
    const steps = 28;
    const maxLen = min(w, h) * 0.46;

    if (i % 3 === 0) stroke(0.902, 0.224, 0.459, 0.88);      // magenta
    else if (i % 3 === 1) stroke(1.0, 0.549, 0.259, 0.88);   // tangerine
    else stroke(0.282, 0.792, 0.894, 0.88);                  // seafoam

    strokeWeight(3.2);
    noFill();
    beginShape();
    for (let s = 0; s <= steps; s++) {
      const u = s / steps;
      const d = u * maxLen;
      const swirl = Math.sin(u * 5.0 - t * flow_rate + i * 0.7) * curl_amp * u;
      const ang = baseAng + swirl;
      vertex(cx + Math.cos(ang) * d, cy + Math.sin(ang) * d);
    }
    endShape();

    // Glowing spore node at tip and midpoint
    noStroke();
    for (let m = 1; m <= 2; m++) {
      const u = m * 0.46;
      const d = u * maxLen;
      const swirl = Math.sin(u * 5.0 - t * flow_rate + i * 0.7) * curl_amp * u;
      const ang = baseAng + swirl;
      const nx = cx + Math.cos(ang) * d;
      const ny = cy + Math.sin(ang) * d;
      if (m === 1) fill(1.0, 0.82, 0.25, 0.85);
      else fill(0.902, 0.224, 0.459, 0.92);
      ellipse(nx, ny, 14 + m * 5, 10 + m * 4);
    }
  }

  // Central luminous core
  noStroke();
  fill(1.0, 0.549, 0.259, 0.35);
  circle(cx, cy, 120);
  fill(1.0, 0.84, 0.35, 0.9);
  circle(cx, cy, 48);
}
