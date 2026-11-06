// Retro-Surreal Botanical Monolith - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Constructed collage featuring floating slate monolith slabs, botanical specimen silhouettes in muted sage, and terracotta astrological disc alignments.
param("zoom", 1.0, 0.5, 2.0);
param("monolith_width", 0.32, 0.15, 0.6);
param("sage_overlay", 0.88, 0.3, 1.0);
param("terracotta_split", 0.5, 0.2, 0.8);

function draw(t) {
  // Midnight slate background
  background(0.169, 0.176, 0.259);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88 * zoom;

  push();
  translate(cx, cy);

  // Primary aged cream kraft backing paper sheet
  fill(0.996, 0.980, 0.878);
  noStroke();
  rect(-span * 0.46, -span * 0.46, span * 0.92, span * 0.92);

  // Border outline rule
  stroke(0.169, 0.176, 0.259);
  strokeWeight(2.0);
  noFill();
  rect(-span * 0.46, -span * 0.46, span * 0.92, span * 0.92);

  // Terracotta geometric field block on top half
  fill(0.831, 0.639, 0.451);
  noStroke();
  rect(-span * 0.46, -span * 0.46, span * 0.92, span * 0.38);

  // Sage green horizontal horizon band
  fill(0.290, 0.486, 0.349, sage_overlay);
  rect(-span * 0.46, -span * 0.08, span * 0.92, span * 0.18);

  // Central floating monolith column in midnight slate
  const mW = span * monolith_width;
  fill(0.169, 0.176, 0.259);
  rect(-mW * 0.5, -span * 0.36, mW, span * 0.72);

  // Split terracotta and cream geometric inlays within the monolith
  fill(0.831, 0.639, 0.451);
  rect(-mW * 0.4, -span * 0.30, mW * 0.8, span * 0.16);
  fill(0.996, 0.980, 0.878);
  rect(-mW * 0.4, -span * 0.08, mW * 0.8, span * 0.04);
  rect(-mW * 0.4, span * 0.02, mW * 0.8, span * 0.18);

  // Astrological alignment disc hovering at top
  push();
  translate(0, -span * 0.22);
  stroke(0.996, 0.980, 0.878);
  strokeWeight(2.2);
  noFill();
  circle(0, 0, span * 0.24);

  // Concentric ring and radial spokes
  stroke(0.169, 0.176, 0.259);
  strokeWeight(1.4);
  circle(0, 0, span * 0.16);

  stroke(0.831, 0.639, 0.451);
  strokeWeight(1.2);
  for (let i = 0; i < 12; i++) {
    const a = (i * 6.2831853) / 12;
    line(cos(a) * span * 0.08, sin(a) * span * 0.08, cos(a) * span * 0.12, sin(a) * span * 0.12);
  }
  pop();

  // Botanical specimen fronds overlapping the monolith base
  stroke(0.290, 0.486, 0.349);
  strokeWeight(2.2);
  noFill();

  // Left curved botanical frond
  beginShape();
  for (let s = 0; s <= 20; s++) {
    const f = s / 20.0;
    const bx = -mW * 0.3 - sin(f * 2.4) * span * 0.12;
    const by = span * 0.30 - f * span * 0.36;
    vertex(bx, by);
  }
  endShape();

  // Leaf sprays along left frond
  for (let k = 0; k < 6; k++) {
    const f = (k + 1) / 7.0;
    const ly = span * 0.30 - f * span * 0.36;
    const lx = -mW * 0.3 - sin(f * 2.4) * span * 0.12;
    fill(0.290, 0.486, 0.349);
    noStroke();
    ellipse(lx - span * 0.035, ly - span * 0.015, span * 0.04, span * 0.018);
    stroke(0.290, 0.486, 0.349);
    strokeWeight(1.5);
    noFill();
    line(lx, ly, lx - span * 0.035, ly - span * 0.015);
  }

  // Right curved botanical frond
  stroke(0.290, 0.486, 0.349);
  strokeWeight(2.2);
  noFill();
  beginShape();
  for (let s = 0; s <= 20; s++) {
    const f = s / 20.0;
    const bx = mW * 0.3 + sin(f * 2.4) * span * 0.12;
    const by = span * 0.30 - f * span * 0.36;
    vertex(bx, by);
  }
  endShape();

  // Leaf sprays along right frond
  for (let k = 0; k < 6; k++) {
    const f = (k + 1) / 7.0;
    const ly = span * 0.30 - f * span * 0.36;
    const lx = mW * 0.3 + sin(f * 2.4) * span * 0.12;
    fill(0.290, 0.486, 0.349);
    noStroke();
    ellipse(lx + span * 0.035, ly - span * 0.015, span * 0.04, span * 0.018);
    stroke(0.290, 0.486, 0.349);
    strokeWeight(1.5);
    noFill();
    line(lx, ly, lx + span * 0.035, ly - span * 0.015);
  }

  // Archival stamps and dimension guidelines
  stroke(0.169, 0.176, 0.259);
  strokeWeight(1.2);
  line(-span * 0.42, span * 0.38, span * 0.42, span * 0.38);
  for (let x = -span * 0.40; x <= span * 0.40; x += span * 0.05) {
    line(x, span * 0.37, x, span * 0.39);
  }

  pop();
}
