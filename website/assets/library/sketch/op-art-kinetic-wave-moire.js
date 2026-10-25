// Op Art Kinetic Wave Moire - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Bridget Riley inspired optical illusion with interfering harmonic wave lines, dynamic moire nodes, and perceived kinetic vibration.
param("line_count", 36.0, 12.0, 72.0);
param("wave_frequency", 4.0, 1.0, 12.0);
param("wave_amplitude", 38.0, 5.0, 80.0);
param("phase_velocity", 0.6, 0.1, 2.5);
param("moire_interference", 0.55, 0.0, 1.0);

function draw(t) {
  // Midnight ground #151930
  background(0.082, 0.098, 0.188);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const numLines = floor(line_count);
  const time = t * phase_velocity;

  noFill();

  // 1. Primary horizontal undulating wave system
  const stepY = height / (numLines + 1);
  const ptsPerLine = 120;
  const stepX = width / ptsPerLine;

  for (let l = 0; l < numLines; l++) {
    const baseNorm = (l + 1) / (numLines + 1);
    const baseY = baseNorm * height;

    // Modulate amplitude across vertical profile (pinched center for moire)
    const waist = sin(baseNorm * 3.14159265);
    const amp = wave_amplitude * (0.4 + 0.6 * waist);

    // Primary wave line
    strokeWeight(1.8);
    const bright = 0.55 + 0.45 * sin(baseNorm * 6.28 + time);
    stroke(0.933 * bright, 0.945 * bright, 0.980 * bright, 0.9);

    beginShape();
    for (let i = 0; i <= ptsPerLine; i++) {
      const x = i * stepX;
      const u = x / width;
      const k = u * wave_frequency * 6.2831853;

      // Travelling wave with non-linear phase warping
      const yShift = sin(k + time + baseNorm * 3.0) * amp +
                    sin(k * 2.0 - time * 0.7) * (amp * 0.3 * moire_interference);

      vertex(x, baseY + yShift);
    }
    endShape();

    // 2. Counter-propagating moire interference ribbon
    if (moire_interference > 0.05 && l % 2 === 0) {
      strokeWeight(1.0);
      stroke(0.655, 0.694, 1.0, 0.45 * moire_interference); // Signal accent

      beginShape();
      for (let i = 0; i <= ptsPerLine; i++) {
        const x = i * stepX;
        const u = x / width;
        const k = u * (wave_frequency * 1.5) * 6.2831853;

        const yShift2 = sin(-k + time * 1.2 - baseNorm * 4.0) * (amp * 0.75);
        vertex(x, baseY + yShift2);
      }
      endShape();
    }
  }

  // 3. Central optical focus disc border
  strokeWeight(2.0);
  stroke(1.0, 0.42, 0.21, 0.65); // Ember accent ring
  ellipse(cx, cy, min(width, height) * 0.42, min(width, height) * 0.42);
}
