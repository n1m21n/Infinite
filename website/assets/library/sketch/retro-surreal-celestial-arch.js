// Retro-Surreal Celestial Arch - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Surrealist cutout composition on aged parchment with concentric celestial orbital rings, sage and terracotta archway portals, and midnight slate geometric plinths.
param("zoom", 1.0, 0.5, 2.0);
param("arch_width", 0.58, 0.3, 0.9);
param("orbital_rings", 5.0, 3.0, 9.0);
param("terracotta_opacity", 0.92, 0.4, 1.0);

function draw(t) {
  // Pale cream kraft ground
  background(0.996, 0.980, 0.878);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88 * zoom;

  push();
  translate(cx, cy);

  // Outer framing border in midnight slate
  stroke(0.169, 0.176, 0.259);
  strokeWeight(2.5);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  // Inner margin rule
  stroke(0.290, 0.486, 0.349, 0.45);
  strokeWeight(1.0);
  rect(-span * 0.45, -span * 0.45, span * 0.90, span * 0.90);

  // Foundational slate horizontal plinth at base
  fill(0.169, 0.176, 0.259);
  noStroke();
  rect(-span * 0.42, span * 0.32, span * 0.84, span * 0.12);

  // Stepped pediment block in sage green
  fill(0.290, 0.486, 0.349);
  rect(-span * 0.38, span * 0.26, span * 0.76, span * 0.06);

  // Terracotta central archway portal
  fill(0.831, 0.639, 0.451, terracotta_opacity);
  const wArch = span * arch_width;
  const hArch = span * 0.65;
  const yArch = -span * 0.38;

  beginShape();
  vertex(-wArch * 0.5, yArch + hArch);
  vertex(-wArch * 0.5, yArch + wArch * 0.5);
  // Rounded arch dome top
  for (let a = Math.PI; a <= Math.PI * 2.0; a += 0.1) {
    const rx = (wArch * 0.5) * cos(a);
    const ry = (wArch * 0.5) * sin(a);
    vertex(rx, yArch + wArch * 0.5 + ry);
  }
  vertex(wArch * 0.5, yArch + hArch);
  endShape();

  // Midnight slate inner portal void
  fill(0.169, 0.176, 0.259);
  const wInner = wArch * 0.74;
  const hInner = hArch * 0.84;
  const yInner = yArch + (hArch - hInner);

  beginShape();
  vertex(-wInner * 0.5, yInner + hInner);
  vertex(-wInner * 0.5, yInner + wInner * 0.5);
  for (let a = Math.PI; a <= Math.PI * 2.0; a += 0.1) {
    const rx = (wInner * 0.5) * cos(a);
    const ry = (wInner * 0.5) * sin(a);
    vertex(rx, yInner + wInner * 0.5 + ry);
  }
  vertex(wInner * 0.5, yInner + hInner);
  endShape();

  // Floating celestial sphere inside archway
  push();
  translate(0, -span * 0.08);

  // Concentric orbital rings in sage green and cream
  const rings = floor(orbital_rings);
  for (let r = 1; r <= rings; r++) {
    const rad = (span * 0.18 * r) / rings;
    stroke(r % 2 === 0 ? 0.290 : 0.831, r % 2 === 0 ? 0.486 : 0.639, r % 2 === 0 ? 0.349 : 0.451, 0.75);
    strokeWeight(1.2);
    noFill();
    ellipse(0, 0, rad * 2.0, rad * 1.1);
  }

  // Central glowing terracotta moon/sun disc
  fill(0.831, 0.639, 0.451);
  noStroke();
  circle(0, 0, span * 0.16);

  // Crescent shadow in midnight slate
  fill(0.169, 0.176, 0.259);
  circle(span * 0.035, -span * 0.02, span * 0.13);

  // Astrological radial ticks
  stroke(0.996, 0.980, 0.878, 0.85);
  strokeWeight(1.4);
  for (let i = 0; i < 16; i++) {
    const ang = (i * 6.2831853) / 16;
    const r1 = span * 0.09;
    const r2 = span * 0.11;
    line(cos(ang) * r1, sin(ang) * r1, cos(ang) * r2, sin(ang) * r2);
  }
  pop();

  // Flanking sage green botanical sprig silhouettes
  stroke(0.290, 0.486, 0.349);
  strokeWeight(2.0);
  noFill();

  // Left botanical branch
  push();
  translate(-span * 0.36, span * 0.02);
  line(0, span * 0.22, 0, -span * 0.22);
  for (let b = -4; b <= 4; b++) {
    const by = b * (span * 0.045);
    const bl = span * 0.05 * (1.0 - abs(b) * 0.15);
    line(0, by, -bl, by - span * 0.02);
    fill(0.290, 0.486, 0.349);
    noStroke();
    ellipse(-bl - 4, by - span * 0.02, 7, 12);
    stroke(0.290, 0.486, 0.349);
    strokeWeight(2.0);
    noFill();
  }
  pop();

  // Right botanical branch
  push();
  translate(span * 0.36, span * 0.02);
  line(0, span * 0.22, 0, -span * 0.22);
  for (let b = -4; b <= 4; b++) {
    const by = b * (span * 0.045);
    const bl = span * 0.05 * (1.0 - abs(b) * 0.15);
    line(0, by, bl, by - span * 0.02);
    fill(0.290, 0.486, 0.349);
    noStroke();
    ellipse(bl + 4, by - span * 0.02, 7, 12);
    stroke(0.290, 0.486, 0.349);
    strokeWeight(2.0);
    noFill();
  }
  pop();

  // Corner catalog registration brackets
  stroke(0.169, 0.176, 0.259);
  strokeWeight(2.0);
  noFill();
  const cOff = span * 0.44;
  const cLen = 16;
  line(-cOff, -cOff, -cOff + cLen, -cOff);
  line(-cOff, -cOff, -cOff, -cOff + cLen);
  line(cOff, -cOff, cOff - cLen, -cOff);
  line(cOff, -cOff, cOff, -cOff + cLen);
  line(-cOff, cOff, -cOff + cLen, cOff);
  line(-cOff, cOff, -cOff, cOff - cLen);
  line(cOff, cOff, cOff - cLen, cOff);
  line(cOff, cOff, cOff, cOff - cLen);

  pop();
}
