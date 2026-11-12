// Lilliputian Surreal Titanic Staircase Pilgrims - Infinite Library (Sketch 2D)
param("steps", 12, 6, 20);
param("travelers", 14, 4, 28);
param("arch_span", 0.62, 0.4, 0.8);
param("pace", 0.05, 0.0, 0.2);

function draw(t) {
  // Deep slate navy interior (#1a2a3a)
  background(0.102, 0.165, 0.227);

  const w = width;
  const h = height;

  // Giant background portal arch (#e8d8c8)
  noStroke();
  fill(0.18, 0.26, 0.35);
  const archW = w * arch_span;
  const archX = (w - archW) * 0.5;
  rect(archX, h * 0.12, archW, h * 0.88);
  circle(w * 0.5, h * 0.30, archW);

  // Inner warm alabaster celestial disc
  fill(0.910, 0.847, 0.784);
  circle(w * 0.5, h * 0.26, archW * 0.46);

  // Monumental diagonal stepped staircase rising left to right
  const nSteps = floor(steps);
  const stepW = w / nSteps;
  const baseH = h * 0.88;

  for (let i = 0; i < nSteps; i++) {
    const sx = i * stepW;
    const stepTop = baseH - (i + 1) * (h * 0.56 / nSteps);

    // Step riser in warm alabaster
    fill(0.910, 0.847, 0.784);
    rect(sx, stepTop, stepW + 1, h - stepTop);

    // Deep slate shadow side on each riser
    fill(0.14, 0.21, 0.29);
    rect(sx, stepTop + 14, stepW * 0.35, h - stepTop - 14);

    // Crisp horizontal tread highlight
    fill(0.96, 0.92, 0.87);
    rect(sx, stepTop, stepW + 1, 6);
  }

  // Framing colossal foreground columns
  fill(0.07, 0.12, 0.17);
  rect(0, 0, 56, h);
  rect(w - 56, 0, 56, h);

  // Miniature terracotta pilgrims ascending the titanic steps (#b84a39)
  const nTrav = floor(travelers);
  for (let k = 0; k < nTrav; k++) {
    const u = (k / nTrav + t * pace) % 1.0;
    const stepIdx = Math.min(nSteps - 1, floor(u * nSteps));
    const localU = (u * nSteps) - stepIdx;
    const px = stepIdx * stepW + localU * stepW;
    const py = baseH - (stepIdx + 1) * (h * 0.56 / nSteps);

    // Tiny figure
    fill(0.722, 0.290, 0.224);
    rect(px - 3, py - 16, 6, 16);
    circle(px, py - 21, 6.5);
  }
}
