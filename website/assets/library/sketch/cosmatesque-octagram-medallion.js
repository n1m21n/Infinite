// Cosmatesque Octagram Medallion - Infinite Library (Sketch 2D)
param("star_rings", 5, 3, 8);
param("tessera_count", 32, 16, 48);
param("drift_speed", 0.06, 0.0, 0.35);
param("chevron_teeth", 16, 8, 28);

function draw(t) {
  // Deep travertine-slate ground (#242321)
  background(0.141, 0.137, 0.129);
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const R = min(w, h) * 0.43;

  // Outer stepped chevron marble frieze
  const teeth = floor(chevron_teeth);
  for (let i = 0; i < teeth; i++) {
    const a0 = (i / teeth) * TAU;
    const a1 = ((i + 0.5) / teeth) * TAU;
    const a2 = ((i + 1.0) / teeth) * TAU;
    fill(i % 2 === 0 ? 0.431 : 0.141, i % 2 === 0 ? 0.141 : 0.259, i % 2 === 0 ? 0.161 : 0.208, 0.92);
    stroke(0.863, 0.824, 0.737, 0.85);
    strokeWeight(1.5);
    beginShape();
    vertex(cx + cos(a0) * R * 0.88, cy + sin(a0) * R * 0.88);
    vertex(cx + cos(a1) * R * 1.02, cy + sin(a1) * R * 1.02);
    vertex(cx + cos(a2) * R * 0.88, cy + sin(a2) * R * 0.88);
    endShape();
  }

  // Concentric marble opus sectile rings
  const rings = floor(star_rings);
  for (let r = rings; r >= 1; r--) {
    const u = r / rings;
    const rad = R * (0.20 + u * 0.66);
    const spin = t * drift_speed * (r % 2 === 0 ? 1 : -1);

    // Octagram star polygon
    if (r % 2 === 1) fill(0.863, 0.824, 0.737, 0.88);
    else fill(0.180, 0.176, 0.169, 0.96);
    stroke(0.722, 0.573, 0.290, 0.92);
    strokeWeight(2.4);

    beginShape();
    const pts = 16;
    for (let k = 0; k <= pts; k++) {
      const a = (k / pts) * TAU + spin;
      const rr = k % 2 === 0 ? rad : rad * 0.76;
      vertex(cx + cos(a) * rr, cy + sin(a) * rr);
    }
    endShape();

    // Ring of triangular marble tesserae
    const tc = floor(tessera_count * u) + 8;
    for (let j = 0; j < tc; j++) {
      const a = (j / tc) * TAU + spin * 0.5;
      const tr = rad * 0.84;
      const tx = cx + cos(a) * tr;
      const ty = cy + sin(a) * tr;
      const sz = (R / rings) * 0.22;
      fill(j % 3 === 0 ? 0.431 : (j % 3 === 1 ? 0.141 : 0.722),
           j % 3 === 0 ? 0.141 : (j % 3 === 1 ? 0.259 : 0.573),
           j % 3 === 0 ? 0.161 : (j % 3 === 1 ? 0.208 : 0.290), 0.90);
      noStroke();
      push();
      translate(tx, ty);
      rotate(a + PI * 0.25);
      rect(-sz * 0.5, -sz * 0.5, sz, sz);
      pop();
    }
  }

  // Central porphyry omphalos disc (#6E2429)
  fill(0.431, 0.141, 0.161, 0.98);
  stroke(0.863, 0.824, 0.737, 0.96);
  strokeWeight(3.2);
  circle(cx, cy, R * 0.30);

  fill(0.722, 0.573, 0.290, 0.95);
  noStroke();
  circle(cx, cy, R * 0.10);
}