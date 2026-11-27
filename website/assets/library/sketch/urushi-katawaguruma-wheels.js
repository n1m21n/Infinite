// Urushi Katawaguruma Wheels - Infinite Library (Sketch 2D)
param("streams", 22, 10, 36);
param("wheels", 7, 3, 12);
param("gold_dust", 450, 100, 900);
param("flow", 0.25, 0.0, 0.8);

function draw(t) {
  // Deep polished obsidian urushi lacquer (#120F0D)
  background(0.071, 0.059, 0.051);
  const w = width;
  const h = height;
  const side = min(w, h);
  const pad = side * 0.06;
  const x0 = (w - side) * 0.5 + pad;
  const y0 = (h - side) * 0.5 + pad;
  const span = side - pad * 2.0;

  // Nashiji sprinkled gold dust in lacquer depth
  noStroke();
  for (let i = 0; i < gold_dust; i++) {
    const gx = x0 + ((i * 0.6180339887) % 1.0) * span;
    const gy = y0 + ((i * 0.3819660113 + sin(i * 9.7) * 0.04) % 1.0) * span;
    const glint = 0.45 + 0.45 * sin(i * 2.1 + t * 0.6);
    fill(0.79, 0.63, 0.30, glint * 0.28);
    circle(gx, gy, 1.3 + (i % 3) * 0.5);
  }

  // Flowing gold maki-e stream contours
  const rows = floor(streams);
  const segs = 84;
  noFill();
  for (let r = 0; r < rows; r++) {
    const baseY = y0 + (r + 0.5) * (span / rows);
    const alpha = 0.34 + 0.22 * sin(r * 0.7);
    stroke(0.788, 0.635, 0.302, alpha);
    strokeWeight(1.4 + (r % 2) * 0.7);
    beginShape();
    for (let s = 0; s <= segs; s++) {
      const u = s / segs;
      const px = x0 + u * span;
      const wave = sin(u * TAU * 1.5 + r * 0.35 + t * flow) * (span * 0.028)
                 + cos(u * TAU * 3.0 - r * 0.25) * (span * 0.012);
      vertex(px, baseY + wave);
    }
    endShape();
  }

  // Half-submerged Katawaguruma cartwheels with Raden mother-of-pearl rims
  const nW = floor(wheels);
  for (let k = 0; k < nW; k++) {
    const cx = x0 + ((k + 0.5) / nW) * span * 0.9 + sin(k * 2.4) * span * 0.04;
    const cy = y0 + (0.24 + ((k * 0.37) % 0.58)) * span;
    const R = span * (0.085 + (k % 3) * 0.022);
    const rot = t * flow * (k % 2 === 0 ? 0.35 : -0.3) + k * 0.8;
    const arcSpan = 3.55; // half-submerged arc

    // Iridescent raden nacre outer rim
    const hueShift = 0.48 + 0.14 * sin(k * 1.7 + t * 0.4);
    stroke(0.88, 0.90, 0.88, 0.88);
    strokeWeight(R * 0.16);
    noFill();
    beginShape();
    for (let a = 0; a <= 24; a++) {
      const ang = rot + (a / 24) * arcSpan;
      vertex(cx + cos(ang) * R, cy + sin(ang) * R);
    }
    endShape();

    // Tinted abalone highlight arc
    stroke(hsl(hueShift, 0.42, 0.74), 0.75);
    strokeWeight(R * 0.07);
    beginShape();
    for (let a = 0; a <= 24; a++) {
      const ang = rot + (a / 24) * arcSpan;
      vertex(cx + cos(ang) * (R * 0.98), cy + sin(ang) * (R * 0.98));
    }
    endShape();

    // Gold maki-e radial spokes
    stroke(0.82, 0.66, 0.31, 0.85);
    strokeWeight(1.8);
    const spokes = 8;
    for (let sp = 0; sp < spokes; sp++) {
      const ang = rot + (sp / (spokes - 1)) * arcSpan;
      line(cx + cos(ang) * (R * 0.22), cy + sin(ang) * (R * 0.22),
           cx + cos(ang) * (R * 0.86), cy + sin(ang) * (R * 0.86));
    }

    // Center hub
    fill(0.82, 0.66, 0.31);
    noStroke();
    circle(cx, cy, R * 0.24);
  }

  // Lacquer box rim border
  noFill();
  stroke(0.788, 0.635, 0.302, 0.55);
  strokeWeight(2.5);
  rect(x0 - 6, y0 - 6, span + 12, span + 12);
}
