// LeWitt Polychrome Wall Planes - Infinite Library (Sketch 2D)
param("tilt", 0.22, 0.05, 0.45);
param("hatch", 64, 24, 110);
param("plinth", 0.20, 0.12, 0.32);
param("drift", 0.08, 0.0, 0.35);

function draw(t) {
  // Warm gallery concrete floor (#928E88)
  background(0.573, 0.557, 0.533);
  const w = width;
  const h = height;
  const sway = sin(t * drift) * 0.035;

  // Architectural room corner vertex
  const cx = w * (0.46 + sway);
  const cy = h * (0.64 - sway * 0.5);
  const topY = h * 0.08;
  const botY = h * (1.0 - plinth);

  // Left wall upper vermilion plane (#E62A14)
  noStroke();
  fill(0.902, 0.165, 0.078);
  beginShape();
  vertex(0, 0);
  vertex(cx, 0);
  vertex(cx, cy - h * 0.14);
  vertex(0, h * (0.72 + tilt * 0.25));
  endShape(CLOSE);

  // Left wall cobalt blue triangle (#1A82E0)
  fill(0.102, 0.510, 0.878);
  beginShape();
  vertex(0, h * (0.72 + tilt * 0.25));
  vertex(cx, cy - h * 0.14);
  vertex(cx, cy);
  vertex(0, botY + h * 0.09);
  endShape(CLOSE);

  // Center-right vermilion wall (#DC3216)
  fill(0.863, 0.196, 0.086);
  beginShape();
  vertex(cx, 0);
  vertex(w * 0.80, h * 0.12);
  vertex(w * 0.80, cy + h * 0.08);
  vertex(cx, cy);
  endShape(CLOSE);

  // Upper violet ceiling-corner wedge (#58328C)
  fill(0.345, 0.196, 0.549);
  beginShape();
  vertex(cx, 0);
  vertex(w, 0);
  vertex(w, h * 0.09);
  vertex(w * 0.80, h * 0.12);
  endShape(CLOSE);

  // Rightmost emerald green wall (#14961B)
  fill(0.078, 0.588, 0.106);
  beginShape();
  vertex(w * 0.80, h * 0.12);
  vertex(w, h * 0.09);
  vertex(w, cy + h * 0.13);
  vertex(w * 0.80, cy + h * 0.08);
  endShape(CLOSE);

  // Baseboard architectural concrete dado (#8A8680)
  fill(0.541, 0.525, 0.502);
  beginShape();
  vertex(0, botY + h * 0.09);
  vertex(cx, cy);
  vertex(w * 0.80, cy + h * 0.08);
  vertex(w, cy + h * 0.13);
  vertex(w, h);
  vertex(0, h);
  endShape(CLOSE);

  // Architectural exhibition light-track & matte-ivory upper band
  fill(0.955, 0.945, 0.920);
  rect(0, 0, w, h * 0.065);
  stroke(0.08, 0.08, 0.09);
  strokeWeight(4.0);
  line(0, h * 0.065, w, h * 0.065);

  // Fine LeWitt parallel ink hatching across each wall plane
  const nH = floor(hatch);
  // Left vermilion/cobalt wall: crisp ivory diagonal 45-deg drafting lines
  stroke(0.98, 0.97, 0.94, 0.65);
  strokeWeight(1.8);
  for (let i = -nH; i < nH * 2; i += 2) {
    const x0 = (i / nH) * cx;
    line(x0, h * 0.065, x0 + h * 0.45, h * 0.515);
  }
  // Center-right vermilion wall: crisp dark-ink vertical parallel lines
  stroke(0.08, 0.08, 0.09, 0.58);
  strokeWeight(1.8);
  for (let i = 1; i < nH; i += 2) {
    const x = cx + (i / nH) * (w * 0.80 - cx);
    line(x, h * 0.065, x, cy + h * 0.06);
  }
  // Right emerald wall: crisp ivory horizontal parallel lines
  stroke(0.96, 0.97, 0.94, 0.62);
  strokeWeight(1.8);
  for (let i = 1; i < floor(nH * 0.75); i += 2) {
    const y = h * 0.10 + (i / (nH * 0.75)) * (cy - h * 0.02);
    line(w * 0.80, y, w, y);
  }

  // Bold Sol LeWitt charcoal-black architectural corner & plane dividers
  stroke(0.07, 0.07, 0.08);
  strokeWeight(4.5);
  line(cx, h * 0.065, cx, cy);
  line(w * 0.80, h * 0.12, w * 0.80, cy + h * 0.08);
  line(0, botY + h * 0.09, cx, cy);
  line(cx, cy, w * 0.80, cy + h * 0.08);
  line(w * 0.80, cy + h * 0.08, w, cy + h * 0.13);
  line(0, h * (0.72 + tilt * 0.25), cx, cy - h * 0.14);

  // Dark charcoal concrete floor slab expansion joints
  stroke(0.14, 0.14, 0.15, 0.75);
  strokeWeight(2.0);
  for (let i = 1; i <= 5; i++) {
    const u = i / 6.0;
    line(cx + (u - 0.5) * 60.0, cy, u * w, h);
  }
  for (let r = 1; r <= 3; r++) {
    const fy = cy + (r / 4.0) * (h - cy);
    line(0, fy, w, fy);
  }
}