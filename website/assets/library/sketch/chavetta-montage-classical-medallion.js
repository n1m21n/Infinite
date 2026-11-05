// Chavetta Montage Classical Medallion - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Constructed archival montage featuring concentric classical medallion rings, contrasting terracotta and slate geometric framing slabs, and tactile textured paper ephemera.
param("zoom", 1.0, 0.5, 2.0);
param("medallion_scale", 1.0, 0.6, 1.8);
param("ring_count", 6.0, 3.0, 10.0);
param("slab_opacity", 0.9, 0.5, 1.0);

function draw(t) {
  // Deep charcoal background ground
  background(0.17, 0.17, 0.17);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88 * zoom;

  push();
  translate(cx, cy);

  // Main aged kraft archival backing sheet
  fill(0.85, 0.77, 0.71);
  noStroke();
  rect(-span * 0.46, -span * 0.46, span * 0.92, span * 0.92);

  // Border rule
  stroke(0.17, 0.17, 0.17);
  strokeWeight(2.0);
  noFill();
  rect(-span * 0.46, -span * 0.46, span * 0.92, span * 0.92);

  // Terracotta horizontal dominant slab
  fill(0.64, 0.28, 0.22, slab_opacity);
  noStroke();
  rect(-span * 0.46, -span * 0.28, span * 0.92, span * 0.24);

  // Slate blue asymmetric vertical column
  fill(0.31, 0.36, 0.46, slab_opacity);
  rect(-span * 0.14, -span * 0.46, span * 0.28, span * 0.92);

  // Dark charcoal header and footer bars
  fill(0.17, 0.17, 0.17);
  rect(-span * 0.42, -span * 0.42, span * 0.84, span * 0.08);
  rect(-span * 0.42, span * 0.34, span * 0.84, span * 0.08);

  // Fine paper accent inlays
  fill(0.85, 0.77, 0.71);
  rect(-span * 0.38, -span * 0.40, span * 0.32, span * 0.04);
  rect(span * 0.06, -span * 0.40, span * 0.32, span * 0.04);
  rect(-span * 0.38, span * 0.36, span * 0.24, span * 0.04);

  // Central classical circular medallion
  push();
  const rad = span * 0.28 * medallion_scale;

  // Medallion background disc
  fill(0.85, 0.77, 0.71);
  stroke(0.17, 0.17, 0.17);
  strokeWeight(3.0);
  circle(0, 0, rad * 2.0);

  // Concentric decorative rings
  const rings = floor(ring_count);
  for (let r = 1; r <= rings; r++) {
    const curR = (rad * r) / rings;
    stroke(r % 2 === 0 ? 0.64 : 0.17, r % 2 === 0 ? 0.28 : 0.17, r % 2 === 0 ? 0.22 : 0.17);
    strokeWeight(r === rings ? 2.5 : 1.2);
    noFill();
    circle(0, 0, curR * 2.0);
  }

  // Medallion radial dentil / compass ticks
  stroke(0.17, 0.17, 0.17);
  strokeWeight(1.8);
  for (let i = 0; i < 32; i++) {
    const a = (i * 6.2831853) / 32;
    const r1 = rad * 0.82;
    const r2 = rad * 0.98;
    line(cos(a) * r1, sin(a) * r1, cos(a) * r2, sin(a) * r2);
  }

  // Inner terracotta rosette star
  fill(0.64, 0.28, 0.22);
  noStroke();
  circle(0, 0, rad * 0.48);

  fill(0.31, 0.36, 0.46);
  circle(0, 0, rad * 0.26);

  fill(0.85, 0.77, 0.71);
  circle(0, 0, rad * 0.10);

  pop();

  // Corner catalog brackets & crosshairs
  stroke(0.64, 0.28, 0.22);
  strokeWeight(2.0);
  noFill();
  const cOff = span * 0.42;
  const cLen = 18;
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
