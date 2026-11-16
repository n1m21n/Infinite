// Guilloche Intaglio Banknote Rosette - Infinite Library (Sketch 2D)
param("petals", 16, 8, 28);
param("rings", 22, 10, 36);
param("eccentricity", 0.24, 0.05, 0.45);
param("lathe_speed", 0.35, 0.0, 1.5);

function draw(t) {
  // Aged archival cotton-rag paper (#E2DBCC)
  background(0.886, 0.859, 0.800);
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const R = Math.min(w, h) * 0.40;

  // Fine horizontal intaglio security ruling
  strokeWeight(0.8);
  for (let i = 0; i < 90; i++) {
    const y = h * (0.04 + 0.92 * (i / 89));
    stroke(0.102, 0.114, 0.133, (i % 3 === 0) ? 0.09 : 0.04);
    line(w * 0.04, y, w * 0.96, y);
  }

  // Outer engraved double cartouche border
  noFill();
  stroke(0.102, 0.114, 0.133, 0.88);
  strokeWeight(2.4);
  rect(w * 0.04, h * 0.04, w * 0.92, h * 0.92);
  strokeWeight(1.0);
  rect(w * 0.055, h * 0.055, w * 0.89, h * 0.89);

  // Corner registration crosshairs & gilt spandrel rings
  const corners = [
    [w * 0.10, h * 0.10],
    [w * 0.90, h * 0.10],
    [w * 0.10, h * 0.90],
    [w * 0.90, h * 0.90]
  ];
  for (let c = 0; c < 4; c++) {
    const kx = corners[c][0];
    const ky = corners[c][1];
    stroke(0.831, 0.686, 0.216, 0.78);
    strokeWeight(1.4);
    circle(kx, ky, w * 0.032);
    stroke(0.102, 0.114, 0.133, 0.65);
    strokeWeight(1.0);
    line(kx - w * 0.025, ky, kx + w * 0.025, ky);
    line(kx, ky - h * 0.025, kx, ky + h * 0.025);
  }

  // Primary engine-turned guilloche rosette rings
  const nRings = floor(rings);
  const pCount = floor(petals);
  const steps = 240;
  const rot = t * lathe_speed;

  for (let r = 0; r < nRings; r++) {
    const u = r / Math.max(1, nRings - 1);
    const baseR = R * (0.16 + 0.82 * u);
    const amp = R * eccentricity * (0.28 + 0.72 * sin(u * PI));
    const phase = r * 0.26 + rot * ((r % 2 === 0) ? 1.0 : -0.8);

    if (r % 5 === 2) {
      stroke(0.831, 0.686, 0.216, 0.72); // gilt brass
    } else if (r % 7 === 4) {
      stroke(0.557, 0.141, 0.220, 0.68); // ruby jewel ink
    } else {
      stroke(0.102, 0.114, 0.133, 0.76); // intaglio black
    }
    strokeWeight(1.15);

    beginShape();
    for (let i = 0; i <= steps; i++) {
      const a = (i / steps) * TAU;
      const lobe = sin(a * pCount + phase) * 0.65 + cos(a * (pCount - 4) - phase * 1.3) * 0.35;
      const rad = baseR + amp * lobe * 0.36;
      vertex(cx + cos(a) * rad, cy + sin(a) * rad);
    }
    endShape();
  }

  // Central watchmaker sunburst & ruby bearing jewel
  noStroke();
  fill(0.102, 0.114, 0.133, 0.92);
  circle(cx, cy, R * 0.14);
  fill(0.831, 0.686, 0.216, 0.95);
  circle(cx, cy, R * 0.095);
  fill(0.557, 0.141, 0.220, 0.96);
  circle(cx, cy, R * 0.052);
}
