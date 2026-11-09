// Ukiyo-e Cartography Mountain Strata Relief - Infinite Library (Sketch)
param("contour_levels", 24.0, 10.0, 48.0);
param("steepness", 0.65, 0.2, 1.2);
param("hatch_density", 0.40, 0.1, 0.8);
param("drift", 0.20, 0.05, 0.6);

function draw(t) {
  // Archival washi paper background (#EADEC9)
  background(0.918, 0.871, 0.788);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;
  const time = t * drift;

  push();
  translate(cx, cy);

  // 1. Traditional Woodblock Outer Border
  stroke(0.110, 0.176, 0.259);
  strokeWeight(3.5);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  stroke(0.639, 0.220, 0.153);
  strokeWeight(1.4);
  rect(-span * 0.465, -span * 0.465, span * 0.93, span * 0.93);

  // 2. Base Mountain Ridge Polygon (Ochre Gold Ground #C99E5C)
  fill(0.788, 0.620, 0.361);
  stroke(0.110, 0.176, 0.259);
  strokeWeight(2.2);

  // Hero sacred peak silhouette (Mt. Fuji / Sacred volcanic ridge)
  beginShape();
  vertex(-span * 0.44, span * 0.44);
  vertex(-span * 0.44, span * 0.22);
  vertex(-span * 0.30, span * 0.16);
  vertex(-span * 0.18, span * 0.02);
  vertex(-span * 0.08, -span * 0.32);
  vertex(0.0, -span * 0.35);
  vertex(span * 0.08, -span * 0.32);
  vertex(span * 0.22, span * 0.08);
  vertex(span * 0.35, span * 0.18);
  vertex(span * 0.44, span * 0.26);
  vertex(span * 0.44, span * 0.44);
  endShape(1);

  // 3. Topographical Elevation Strata / Logarithmic Contour Contours
  const levels = floor(contour_levels);
  noFill();

  for (let lvl = 1; lvl < levels; lvl++) {
    const frac = lvl / levels;
    const yBaseline = span * (0.42 - frac * 0.74);
    const amp = span * 0.06 * (1.0 - frac * 0.45) * steepness;
    const peakSpread = span * (0.44 * (1.0 - frac * 0.72));

    stroke(0.110, 0.176, 0.259, 0.85);
    strokeWeight(1.1 + (1.0 - frac) * 0.8);

    beginShape();
    for (let x = -peakSpread; x <= peakSpread; x += 8.0) {
      const normX = x / peakSpread;
      const gaussian = exp(-normX * normX * 3.5);
      const ripple = sin(normX * 8.0 + frac * 2.5 + time) * amp * 0.25;
      const yContour = yBaseline - gaussian * amp * 2.8 + ripple;
      vertex(x, yContour);
    }
    endShape();

    // Woodblock fine slant-hatching on shadow slopes (West face)
    if (lvl % 2 === 0) {
      stroke(0.110, 0.176, 0.259, 0.45);
      strokeWeight(0.9);
      const hCount = floor(12 * (1.0 - frac));
      for (let h = 0; h < hCount; h++) {
        const hx = -peakSpread * (0.3 + 0.6 * (h / max(1, hCount)));
        const hy = yBaseline - exp(-(hx / peakSpread) * (hx / peakSpread) * 3.5) * amp * 2.8;
        line(hx, hy, hx + span * 0.018, hy + span * 0.022);
      }
    }
  }

  // 4. Summit Snow Cap / Sacred Crater in Indigo & Washi relief
  fill(0.918, 0.871, 0.788);
  stroke(0.110, 0.176, 0.259);
  strokeWeight(1.8);
  beginShape();
  vertex(-span * 0.08, -span * 0.32);
  vertex(-span * 0.05, -span * 0.24);
  vertex(-span * 0.02, -span * 0.28);
  vertex(span * 0.01, -span * 0.23);
  vertex(span * 0.04, -span * 0.27);
  vertex(span * 0.08, -span * 0.32);
  vertex(0.0, -span * 0.35);
  endShape(1);

  // 5. Traditional Red Torii / Shrine Waypoint & Cartographic Inscriptions
  noStroke();
  fill(0.639, 0.220, 0.153); // Vermilion red

  // Torii shrine icon on ridge
  const tx = span * 0.20;
  const ty = span * 0.10;
  rect(tx - 12.0, ty - 16.0, 24.0, 3.5);
  rect(tx - 10.0, ty - 11.0, 20.0, 2.5);
  rect(tx - 8.0, ty - 11.0, 3.0, 15.0);
  rect(tx + 5.0, ty - 11.0, 3.0, 15.0);

  // Vermilion signature cartouche in top-left corner
  rect(-span * 0.42, -span * 0.42, span * 0.09, span * 0.18, 2.0);
  fill(0.918, 0.871, 0.788);
  rect(-span * 0.40, -span * 0.38, span * 0.05, span * 0.016);
  rect(-span * 0.38, -span * 0.34, span * 0.016, span * 0.07);
  rect(-span * 0.40, -span * 0.28, span * 0.05, span * 0.016);

  // Ocean wave baseline ripples at foothills
  stroke(0.110, 0.176, 0.259);
  strokeWeight(1.4);
  noFill();
  for (let w = 0; w < 4; w++) {
    const wy = span * (0.34 + w * 0.025);
    beginShape();
    for (let wx = -span * 0.44; wx <= span * 0.44; wx += 12.0) {
      vertex(wx, wy + sin(wx * 0.04 + w * 0.8 + time) * 3.5);
    }
    endShape();
  }

  pop();
}
