// Y2K Acid Starburst - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Spiked acid starburst geometry with counter-rotating chrome star shells, radial holographic orbit tracks, and cybernetic rave HUD telemetry.
param("stars", 8.0, 3.0, 16.0);
param("spikes", 6.0, 3.0, 12.0);
param("spin", 0.45, 0.1, 2.0);
param("chroma_shift", 0.35, 0.0, 1.0);
param("telemetry_alpha", 0.65, 0.1, 1.0);

function draw(t) {
  // Midnight ground #151930
  background(0.082, 0.098, 0.188);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const minDim = min(width, height);
  const time = t * spin;

  push();
  translate(cx, cy);

  // 1. Concentric holographic orbit rings & HUD ticks
  const numRings = 4;
  for (let r = 0; r < numRings; r++) {
    const radius = minDim * (0.16 + r * 0.09);
    noFill();
    strokeWeight(1.0);
    const ringAlpha = (0.2 + 0.15 * sin(t * 2.0 + r)) * telemetry_alpha;
    stroke(0.655, 0.694, 1.0, ringAlpha); // Signal
    ellipse(0, 0, radius * 2, radius * 2);

    // Orbit ticks
    const numTicks = 12 + r * 6;
    for (let k = 0; k < numTicks; k++) {
      const aTick = (k / numTicks) * 6.2831853 + (r % 2 === 0 ? time * 0.4 : -time * 0.4);
      const x1 = cos(aTick) * radius;
      const y1 = sin(aTick) * radius;
      const tickLen = (k % 4 === 0) ? 6 : 3;
      const x2 = cos(aTick) * (radius + tickLen);
      const y2 = sin(aTick) * (radius + tickLen);
      stroke(0.933, 0.945, 0.980, ringAlpha * 1.4);
      line(x1, y1, x2, y2);
    }
  }

  // 2. Multi-layered counter-rotating acid starburst shells
  const numStars = floor(stars);
  const spikeCount = floor(spikes);

  for (let s = numStars - 1; s >= 0; s--) {
    const u = s / numStars;
    const dir = (s % 2 === 0) ? 1.0 : -1.0;
    const aRot = dir * (time * (0.6 + s * 0.15)) + s * 0.35;

    push();
    rotate(aRot);

    const radOuter = minDim * (0.12 + s * 0.038);
    const radInner = radOuter * (0.28 + 0.12 * sin(time * 2.0 + s));

    // Dynamic Y2K iridescent chromatic color gradient
    const pulse = sin(time * 3.0 + s * 1.2) * 0.5 + 0.5;
    if (s % 3 === 0) {
      // Acid magenta / ember chrome
      stroke(1.0, 0.22 + 0.2 * pulse, 0.55 + 0.3 * chroma_shift, 0.9);
      fill(1.0, 0.42, 0.21, 0.12 + 0.08 * pulse);
    } else if (s % 3 === 1) {
      // Emerald / acid cyan
      stroke(0.306, 0.949, 0.722, 0.88);
      fill(0.306, 0.949, 0.722, 0.1 + 0.05 * pulse);
    } else {
      // Signal violet / ice chrome
      stroke(0.655, 0.694, 1.0, 0.92);
      fill(0.655, 0.694, 1.0, 0.08 + 0.06 * pulse);
    }

    strokeWeight(1.5 + (numStars - s) * 0.2);

    beginShape();
    const count = spikeCount * 2;
    for (let i = 0; i <= count; i++) {
      const a = (i * 6.2831853) / count;
      const r = (i % 2 === 0) ? radOuter : radInner;
      vertex(cos(a) * r, sin(a) * r);
    }
    endShape();

    // Orbiting chrome node pearls at star vertices
    if (s === numStars - 1 || s === floor(numStars * 0.5)) {
      noStroke();
      fill(0.933, 0.945, 0.980, 0.95);
      for (let i = 0; i < spikeCount; i++) {
        const a = (i * 2 * 6.2831853) / (spikeCount * 2);
        const px = cos(a) * radOuter;
        const py = sin(a) * radOuter;
        ellipse(px, py, 5.0, 5.0);
      }
    }

    pop();
  }

  // 3. Central glowing chrome nucleus
  noStroke();
  fill(0.933, 0.945, 0.980, 0.95);
  ellipse(0, 0, minDim * 0.045, minDim * 0.045);
  fill(0.306, 0.949, 0.722, 0.5);
  ellipse(0, 0, minDim * 0.085, minDim * 0.085);

  pop();
}
