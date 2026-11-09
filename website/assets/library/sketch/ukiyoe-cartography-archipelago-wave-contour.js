// Ukiyo-e Cartography Archipelago Wave Contour - Infinite Library (Sketch)
param("wave_density", 18.0, 8.0, 36.0);
param("swell_height", 0.045, 0.01, 0.12);
param("gold_accents", 0.45, 0.1, 0.9);
param("speed", 0.25, 0.05, 0.8);

function draw(t) {
  // Archival handmade Japanese Washi paper ground (#EADEC9)
  background(0.918, 0.871, 0.788);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;
  const time = t * speed;

  push();
  translate(cx, cy);

  // 1. Traditional Cartographic Woodblock Border (Indigo & Cinnabar ruled frame)
  stroke(0.110, 0.176, 0.259); // Deep Prussian Indigo
  strokeWeight(3.5);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  stroke(0.639, 0.220, 0.153); // Lacquer Cinnabar Vermilion
  strokeWeight(1.5);
  rect(-span * 0.465, -span * 0.465, span * 0.93, span * 0.93);

  // 2. Archipelagic Island Landforms (Layered Kyoto Ochre Gold #C99E5C)
  fill(0.788, 0.620, 0.361);
  stroke(0.110, 0.176, 0.259);
  strokeWeight(2.0);

  // Main Honshu-inspired mountainous island silhouette
  beginShape();
  const nIslandPts = 60;
  for (let i = 0; i <= nIslandPts; i++) {
    const a = (i / nIslandPts) * Math.PI * 2.0;
    const rBase = span * (0.22 + 0.08 * sin(a * 3.0) + 0.04 * cos(a * 7.0));
    const ix = cos(a) * rBase * 1.35;
    const iy = sin(a) * rBase * 0.75 - span * 0.06;
    vertex(ix, iy);
  }
  endShape(1);

  // Secondary archipelagic islets
  const islets = [
    [-span * 0.30, span * 0.18, span * 0.07],
    [-span * 0.18, span * 0.26, span * 0.05],
    [span * 0.26, span * 0.15, span * 0.08],
    [span * 0.34, span * 0.05, span * 0.04]
  ];
  for (let isl of islets) {
    ellipse(isl[0], isl[1], isl[2] * 2.0, isl[2] * 1.2);
  }

  // 3. Topographic Mountain Ridge Lines inside the islands (Fine Indigo Strata)
  stroke(0.110, 0.176, 0.259, 0.75);
  strokeWeight(1.2);
  noFill();
  for (let r = 1; r <= 6; r++) {
    const scaleFrac = 1.0 - r * 0.14;
    beginShape();
    for (let i = 0; i <= nIslandPts; i++) {
      const a = (i / nIslandPts) * Math.PI * 2.0;
      const rBase = span * (0.22 + 0.08 * sin(a * 3.0) + 0.04 * cos(a * 7.0)) * scaleFrac;
      const ix = cos(a) * rBase * 1.35;
      const iy = sin(a) * rBase * 0.75 - span * 0.06;
      vertex(ix, iy);
    }
    endShape(1);
  }

  // 4. Rhythmic Great Wave Ocean Contours (Traditional Ukiyo-e Seigaiha / Okinami wave ridges)
  const nWaves = floor(wave_density);
  const waveSpanY = span * 0.40;
  for (let w = 0; w < nWaves; w++) {
    const wy = span * 0.05 + (w / nWaves) * waveSpanY;
    const wavePhase = w * 0.45 + time * 1.5;

    stroke(0.110, 0.176, 0.259);
    strokeWeight(1.8 - (w / nWaves) * 0.8);
    noFill();

    beginShape();
    for (let x = -span * 0.46; x <= span * 0.46; x += 10.0) {
      const xFrac = x / span;
      const crest = sin(xFrac * 14.0 + wavePhase) * span * swell_height
                  + cos(xFrac * 28.0 - wavePhase * 1.2) * span * (swell_height * 0.45);
      vertex(x, wy + crest);
    }
    endShape();

    // Occasional foam curl claw crests
    if (w % 3 === 0) {
      fill(0.918, 0.871, 0.788);
      stroke(0.110, 0.176, 0.259);
      strokeWeight(1.0);
      for (let fx = -span * 0.40; fx <= span * 0.40; fx += span * 0.16) {
        const curlY = wy + sin(fx / span * 14.0 + wavePhase) * span * swell_height;
        circle(fx + sin(w) * 12.0, curlY - 3.0, 3.5);
      }
    }
  }

  // 5. Traditional Japanese Cartographic Seal Marks (Hanko / Cinnabar Red Cartouche)
  noStroke();
  fill(0.639, 0.220, 0.153); // Vermilion red seal
  rect(span * 0.32, -span * 0.42, span * 0.11, span * 0.15, 3.0);

  // Seal interior character relief
  fill(0.918, 0.871, 0.788);
  rect(span * 0.34, -span * 0.39, span * 0.07, span * 0.015);
  rect(span * 0.365, -span * 0.40, span * 0.02, span * 0.09);
  rect(span * 0.34, -span * 0.33, span * 0.07, span * 0.015);

  // Compass Rose / Cartographic Cardinal Orientation Star in Indigo & Gold
  push();
  translate(-span * 0.34, -span * 0.34);
  stroke(0.110, 0.176, 0.259);
  strokeWeight(1.4);
  noFill();
  circle(0, 0, span * 0.14);
  circle(0, 0, span * 0.10);

  fill(0.639, 0.220, 0.153);
  for (let pt = 0; pt < 4; pt++) {
    rotate(Math.PI * 0.5);
    triangle(0, -span * 0.065, -span * 0.015, -span * 0.01, span * 0.015, -span * 0.01);
  }
  pop();

  pop();
}
