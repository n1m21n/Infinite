// Ukiyo-e Wave Strata - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Traditional woodblock wave strata with tiered bokashi indigo gradients, curling sea-claw foam filigrees, and rhythmic cartographic tidal swells.
param("wave_layers", 7.0, 3.0, 12.0);
param("claw_curl", 1.4, 0.5, 3.0);
param("foam_density", 32.0, 10.0, 64.0);
param("swell_speed", 0.65, 0.1, 2.5);
param("hue_shift", 0.58, 0.0, 1.0);

function draw(t) {
  // Midnight paper ground #151930
  background(0.082, 0.098, 0.188);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const minDim = min(width, height);
  const time = t * swell_speed;
  const layers = floor(wave_layers);

  // Background bokashi cartographic contour lines
  strokeWeight(1.0);
  noFill();
  for (let c = 1; c <= 8; c++) {
    const ringRadius = minDim * (0.12 * c) * (1.0 + 0.04 * sin(time * 0.8 + c));
    stroke(0.655, 0.694, 1.0, 0.12 + 0.03 * c);
    ellipse(cx, cy * 0.9, ringRadius * 2.0, ringRadius * 1.3);
  }

  // Tiered ukiyo-e wave strata layers from horizon downward
  for (let l = 0; l < layers; l++) {
    const layerFrac = l / layers;
    const baseY = height * (0.35 + layerFrac * 0.55);

    // Bokashi woodblock pigment palette: deep prussian blue to indigo to seafoam
    const blueVal = 0.25 + layerFrac * 0.65;
    const greenVal = 0.35 + layerFrac * 0.5;
    fill(0.06 * blueVal, 0.14 * greenVal, 0.42 * blueVal, 0.88);
    stroke(0.96, 0.94, 0.91, 0.45);
    strokeWeight(1.5);

    beginShape();
    vertex(0, height);
    vertex(0, baseY);

    const steps = 60;
    for (let s = 0; s <= steps; s++) {
      const x = (s / steps) * width;
      const phase = (s / steps) * TAU * 2.5 + time * (1.0 + layerFrac * 0.5) + l * 1.2;

      // Asymmetric Hokusai crest profile
      const crest = pow(sin(phase) * 0.5 + 0.5, claw_curl);
      const waveH = minDim * (0.08 + layerFrac * 0.06);
      const y = baseY - crest * waveH + sin(x * 0.02 + time) * (minDim * 0.02);
      vertex(x, y);
    }
    vertex(width, baseY);
    vertex(width, height);
    endShape();

    // Curling sea-claw foam nodes along the wave crests
    noStroke();
    fill(0.96, 0.94, 0.91, 0.92);
    const numClaws = floor(foam_density * 0.5);
    for (let c = 0; c < numClaws; c++) {
      const u = c / numClaws;
      const x = u * width;
      const phase = u * TAU * 2.5 + time * (1.0 + layerFrac * 0.5) + l * 1.2;
      const crest = pow(sin(phase) * 0.5 + 0.5, claw_curl);

      if (crest > 0.55) {
        const waveH = minDim * (0.08 + layerFrac * 0.06);
        const y = baseY - crest * waveH + sin(x * 0.02 + time) * (minDim * 0.02);
        const clawSize = (crest - 0.55) * minDim * 0.045;
        ellipse(x, y - 2, clawSize * 1.4, clawSize * 1.8);

        // Splashing spray droplet
        const dropOffset = (sin(c * 7.1 + time * 3.0) * 0.5 + 0.5) * minDim * 0.035;
        ellipse(x + sin(c * 3.3) * 6.0, y - clawSize - dropOffset, 2.5, 2.5);
      }
    }
  }

  // Vermillion sunset / sunrise seal stamp in top corner
  push();
  translate(width * 0.84, height * 0.2);
  fill(1.0, 0.42, 0.21, 0.9);
  ellipse(0, 0, minDim * 0.12, minDim * 0.12);
  stroke(0.96, 0.94, 0.91, 0.85);
  strokeWeight(1.2);
  noFill();
  rect(-minDim * 0.045, -minDim * 0.045, minDim * 0.09, minDim * 0.09);
  pop();
}
