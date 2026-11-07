// Demoscene Plasma Rotozoom Raster Bars - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Amiga demoscene study featuring interlocking sinusoidal copper raster bars, rotating geometric rosettes, and electric cyan and flame orange palettes.
param("bars", 9.0, 4.0, 16.0);
param("rosette_petals", 6.0, 3.0, 12.0);
param("speed", 0.5, 0.1, 1.5);
param("bar_spread", 1.1, 0.5, 2.0);

function draw(t) {
  // Deep obsidian void (#07080c)
  background(0.027, 0.031, 0.047);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;

  push();
  translate(cx, cy);

  // Precision framing boundary (#1a2035)
  stroke(0.102, 0.125, 0.208);
  strokeWeight(2.4);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  const time = t * speed;

  // 1. Interlocking multi-frequency copper raster bars
  const barCount = floor(bars);
  for (let i = 0; i < barCount; i++) {
    const norm = (i / (barCount - 1)) * 2.0 - 1.0;
    const yBase = norm * span * 0.38 * bar_spread;
    const wave = sin(time * 2.5 + i * 0.65) * (span * 0.06);
    const y = yBase + wave;
    const barH = span * 0.045 * (1.0 - abs(norm) * 0.3);

    // Dynamic Amiga gradient: Flame Orange (#ff3b00) transitioning to Electric Cyan (#00e0ff)
    const blend = sin(time * 1.8 + i * 0.8) * 0.5 + 0.5;
    const r = 1.0 * (1.0 - blend) + 0.0 * blend;
    const g = 0.231 * (1.0 - blend) + 0.878 * blend;
    const b = 0.0 * (1.0 - blend) + 1.0 * blend;

    fill(r, g, b, 0.88);
    noStroke();
    rect(-span * 0.44, y - barH * 0.5, span * 0.88, barH);

    // High-luminance white core line (#ffffff)
    stroke(1.0, 1.0, 1.0, 0.92);
    strokeWeight(1.8);
    line(-span * 0.44, y, span * 0.44, y);
  }

  // 2. Rotating geometric rosette moire petals at center
  push();
  rotate(time * 0.7);
  const petals = floor(rosette_petals);
  const rad = span * 0.28;
  stroke(0.0, 0.878, 1.0, 0.85); // Electric Cyan
  strokeWeight(2.2);
  noFill();
  for (let p = 0; p < petals; p++) {
    const ang = (p / petals) * Math.PI * 2.0;
    push();
    rotate(ang);
    ellipse(rad * 0.4, 0, rad * 0.8, rad * 0.35);
    pop();
  }

  // 3. Central flame orange solar disk with white registration cross
  fill(1.0, 0.231, 0.0, 0.95);
  noStroke();
  circle(0, 0, span * 0.12);

  stroke(1.0, 1.0, 1.0);
  strokeWeight(2.0);
  noFill();
  circle(0, 0, span * 0.16);
  pop();

  // 4. Outer precision registration crosshairs & scan ticks
  stroke(0.0, 0.878, 1.0, 0.65);
  strokeWeight(1.4);
  const ticks = 6;
  for (let k = -ticks; k <= ticks; k++) {
    const x = (k / ticks) * (span * 0.42);
    line(x, -span * 0.46, x, -span * 0.44);
    line(x, span * 0.44, x, span * 0.46);
    line(-span * 0.46, x, -span * 0.44, x);
    line(span * 0.44, x, span * 0.46, x);
  }

  pop();
}
