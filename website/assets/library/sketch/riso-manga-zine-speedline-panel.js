// Riso Manga Zine Speedline Panel - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Deconstructed manga panel layout with high-velocity radial speedlines, offset riso ink misregistration, isometric sound-effect chevron geometry, and layered halftone gradient grids.
param("focal_warp", 1.15, 0.6, 2.0);
param("panel_split", 0.52, 0.3, 0.7);
param("speed", 0.40, 0.1, 1.5);
param("screentone_scale", 24.0, 12.0, 40.0);

function draw(t) {
  // Rich aged riso paper background (#ede7d5)
  background(0.885, 0.860, 0.795);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;
  const time = t * speed;

  push();
  translate(cx, cy);

  // 1. Asymmetric Manga Comic Panels (Diagonal Division)
  const pW = span * 0.94;
  const pH = span * 0.94;

  // Panel 1: Top Dynamic Vortex Box
  stroke(0.10, 0.10, 0.14);
  strokeWeight(3.8);
  noFill();
  rect(-pW * 0.5, -pH * 0.5, pW, pH * panel_split);

  // Panel 2: Bottom Technical Screentone Box
  rect(-pW * 0.5, -pH * 0.5 + pH * panel_split + span * 0.025, pW, pH * (1.0 - panel_split) - span * 0.025);

  // 2. Convergent High-Velocity Action Speedlines (Top Panel)
  push();
  const topH = pH * panel_split;
  const focalX = sin(time * 1.5) * span * 0.12;
  const focalY = -pH * 0.5 + topH * 0.45;

  stroke(0.0, 0.20, 0.75, 0.88); // Riso Blue
  const lines = 72;
  for (let i = 0; i < lines; i++) {
    const ang = (i / lines) * Math.PI * 2.0;
    const len1 = span * 0.07 * focal_warp;
    const len2 = span * 0.60;
    const x1 = focalX + cos(ang) * len1;
    const y1 = focalY + sin(ang) * len1;
    const x2 = focalX + cos(ang) * len2;
    const y2 = focalY + sin(ang) * len2;

    if (y1 > -pH * 0.5 && y1 < -pH * 0.5 + topH && y2 > -pH * 0.5 && y2 < -pH * 0.5 + topH) {
      strokeWeight((i % 4 === 0) ? 3.5 : (i % 2 === 0) ? 2.0 : 1.2);
      line(x1, y1, x2, y2);
    }
  }

  // Focal explosive impact point
  fill(0.96, 0.16, 0.38); // Fluorescent Pink
  noStroke();
  circle(focalX, focalY, span * 0.05);
  fill(1.0, 0.78, 0.0); // Yellow spark
  circle(focalX, focalY, span * 0.024);
  pop();

  // 3. Dense Risograph Halftone Screentone Grid (Bottom Panel)
  const botY = -pH * 0.5 + pH * panel_split + span * 0.025;
  const botH = pH * (1.0 - panel_split) - span * 0.025;
  const dotsX = 26;
  const dotsY = 11;
  const stepX = pW / dotsX;
  const stepY = botH / dotsY;

  noStroke();
  for (let ix = 1; ix < dotsX; ix++) {
    for (let iy = 1; iy < dotsY; iy++) {
      const px = -pW * 0.5 + ix * stepX;
      const py = botY + iy * stepY;
      const wave = sin(ix * 0.45 + iy * 0.6 + time * 2.0);

      if ((ix + iy) % 2 === 0) {
        fill(0.96, 0.16, 0.38, 0.85);
        circle(px, py, (wave * 0.5 + 0.5) * (stepX * 0.44) + 1.8);
      } else {
        fill(1.0, 0.78, 0.0, 0.88);
        circle(px + 2.0, py + 2.0, (cos(wave) * 0.5 + 0.5) * (stepX * 0.46) + 1.8);
      }
    }
  }

  // 4. Giant Manga Sound-Effect Geometric Letterforms / Chevrons
  fill(0.10, 0.10, 0.14); // Ink black
  const chevs = 3;
  for (let k = 0; k < chevs; k++) {
    const kx = -pW * 0.35 + k * (pW * 0.32);
    const ky = botY + botH * 0.5;
    push();
    translate(kx, ky);
    rotate(-0.15 + sin(time * 2.0 + k) * 0.08);

    // Offset fluorescent shadow
    fill(0.96, 0.16, 0.38);
    rect(-span * 0.06 - 4.0, -span * 0.04 - 3.0, span * 0.12, span * 0.08);

    // Main ink slab
    fill(0.10, 0.10, 0.14);
    rect(-span * 0.06, -span * 0.04, span * 0.12, span * 0.08);

    // Interior yellow lightning slash
    stroke(1.0, 0.78, 0.0);
    strokeWeight(2.5);
    line(-span * 0.04, span * 0.02, 0, -span * 0.02);
    line(0, -span * 0.02, span * 0.04, 0);
    pop();
  }

  // 5. Margin CMYK / Riso color calibration pills
  noStroke();
  fill(0.96, 0.16, 0.38); circle(-pW * 0.45, pH * 0.48, span * 0.020);
  fill(1.0, 0.78, 0.0);  circle(-pW * 0.41, pH * 0.48, span * 0.020);
  fill(0.0, 0.20, 0.75); circle(-pW * 0.37, pH * 0.48, span * 0.020);
  fill(0.10, 0.10, 0.14); circle(-pW * 0.33, pH * 0.48, span * 0.020);

  pop();
}
