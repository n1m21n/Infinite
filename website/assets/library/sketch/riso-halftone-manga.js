// Riso Halftone Manga - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Risograph manga screen-tone halftone lattice with mechanical dot clusters, CMYK spot ink misregistration jitter, and dynamic action speedlines.
param("dot_density", 28.0, 10.0, 60.0);
param("ink_bleed", 1.25, 0.2, 3.0);
param("speedlines", 36.0, 12.0, 72.0);
param("cmyk_jitter", 4.5, 0.0, 15.0);
param("spin", 0.45, 0.0, 2.0);

function draw(t) {
  // Midnight ground #151930
  background(0.082, 0.098, 0.188);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const minDim = min(width, height);
  const time = t * spin;

  // Background radial manga action speedlines
  push();
  translate(cx, cy);
  const numLines = floor(speedlines);
  strokeWeight(1.2);
  for (let i = 0; i < numLines; i++) {
    const angle = (i / numLines) * TAU + time * 0.1;
    const len1 = minDim * (0.28 + 0.12 * sin(i * 13.7 + time * 3.0));
    const len2 = minDim * 0.75;
    const alphaLine = 0.18 + 0.15 * sin(i * 7.3 - time * 2.0);

    // Warm paper tone highlight lines
    stroke(0.96, 0.94, 0.91, alphaLine);
    line(cos(angle) * len1, sin(angle) * len1, cos(angle) * len2, sin(angle) * len2);
  }
  pop();

  // Halftone screen tone dot lattice with CMYK misregistration
  const spacing = minDim / dot_density;
  const jitterX = sin(t * 1.5) * cmyk_jitter;
  const jitterY = cos(t * 1.2) * cmyk_jitter;

  // Fluorescent Pink (Magenta plate)
  noStroke();
  fill(1.0, 0.0, 0.5, 0.75);
  for (let x = spacing * 0.5; x < width; x += spacing) {
    for (let y = spacing * 0.5; y < height; y += spacing) {
      const dx = x - cx + jitterX;
      const dy = y - cy + jitterY;
      const dist = sqrt(dx * dx + dy * dy) / (minDim * 0.45);
      const wave = sin(dist * 12.0 - time * 2.5 + atan2(dy, dx) * 4.0);
      const rad = max(0.0, (1.0 - dist * 0.7) * (0.5 + 0.5 * wave)) * spacing * 0.48 * inkBleed;
      if (rad > 0.5) {
        ellipse(x + jitterX, y + jitterY, rad * 2.0, rad * 2.0);
      }
    }
  }

  // Electric Cyan (Cyan plate offset)
  fill(0.0, 0.85, 0.95, 0.65);
  for (let x = spacing * 0.5; x < width; x += spacing) {
    for (let y = spacing * 0.5; y < height; y += spacing) {
      const dx = x - cx - jitterX * 0.8;
      const dy = y - cy - jitterY * 0.8;
      const dist = sqrt(dx * dx + dy * dy) / (minDim * 0.45);
      const wave = cos(dist * 14.0 - time * 2.0 + atan2(dy, dx) * 3.0);
      const rad = max(0.0, (1.0 - dist * 0.75) * (0.5 + 0.5 * wave)) * spacing * 0.44 * inkBleed;
      if (rad > 0.5) {
        ellipse(x - jitterX * 0.8, y - jitterY * 0.8, rad * 2.0, rad * 2.0);
      }
    }
  }

  // Warm Gold / Ember accent starburst in center
  push();
  translate(cx, cy);
  rotate(time * 0.5);
  fill(1.0, 0.42, 0.21, 0.85);
  beginShape();
  const starSpikes = 16;
  for (let i = 0; i <= starSpikes * 2; i++) {
    const a = (i * PI) / starSpikes;
    const r = i % 2 === 0 ? minDim * 0.16 : minDim * 0.06;
    vertex(cos(a) * r, sin(a) * r);
  }
  endShape();

  // Crisp central core
  fill(0.96, 0.94, 0.91, 0.95);
  ellipse(0, 0, minDim * 0.05, minDim * 0.05);
  pop();
}
