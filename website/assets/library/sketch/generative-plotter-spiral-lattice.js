// Generative Plotter Spiral Lattice - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Concentric logarithmic Archimedean spiral lattice with geometric pen hatching and harmonic angle offsets.
param("spiral_turns", 8.0, 3.0, 16.0);
param("spoke_chords", 32.0, 12.0, 64.0);
param("twist_warp", 0.55, 0.1, 1.5);
param("pen_weight", 1.8, 0.5, 4.0);
param("vermilion_density", 0.35, 0.0, 1.0);

function draw(t) {
  // Archival cream stock #F4EEDD
  background(0.957, 0.933, 0.867);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const time = t * 0.3;
  const maxR = min(width, height) * 0.44;

  noFill();

  // Calibration graticule border
  stroke(0.129, 0.129, 0.129, 0.4);
  strokeWeight(1.0);
  rect(36.0, 36.0, width - 72.0, height - 72.0);
  rect(44.0, 44.0, width - 88.0, height - 88.0);

  // Outer concentric tick ring
  const numTicks = 72;
  for (let k = 0; k < numTicks; k++) {
    const a = (k / numTicks) * 6.2831853;
    const r1 = maxR * 0.98;
    const r2 = maxR * 1.03;
    line(cx + cos(a) * r1, cy + sin(a) * r1, cx + cos(a) * r2, cy + sin(a) * r2);
  }

  // Dual interleaved logarithmic/Archimedean spirals
  const turns = floor(spiral_turns);
  const steps = 600;

  for (let s = 0; s < 3; s++) {
    const angleOffset = (s / 3.0) * 6.2831853 + time * 0.2;
    if (s === 1) {
      stroke(0.549, 0.114, 0.251, 0.92);
      strokeWeight(pen_weight * 1.2);
    } else if (s === 2) {
      stroke(0.157, 0.325, 0.420, 0.88);
      strokeWeight(pen_weight * 1.1);
    } else {
      stroke(0.129, 0.129, 0.129, 0.92);
      strokeWeight(pen_weight);
    }

    beginShape();
    for (let i = 0; i <= steps; i++) {
      const frac = i / steps;
      const theta = frac * turns * 6.2831853 + angleOffset;
      const r = pow(frac, 0.85) * maxR * (1.0 + sin(theta * 3.0 + time) * 0.05 * twist_warp);
      vertex(cx + cos(theta) * r, cy + sin(theta) * r);
    }
    endShape();
  }

  // Geometric cross-hatch chords connecting spiral arms
  const chords = floor(spoke_chords);
  strokeWeight(0.9);
  for (let i = 0; i < chords; i++) {
    const f1 = (i + 1) / (chords + 2);
    const th1 = f1 * turns * 6.2831853 + time * 0.2;
    const r1 = pow(f1, 0.85) * maxR;

    const f2 = min(1.0, f1 + 0.08);
    const th2 = f2 * turns * 6.2831853 + (2.0 / 3.0) * 6.2831853 + time * 0.2;
    const r2 = pow(f2, 0.85) * maxR;

    const isVerm = (i % 5 === 2) && (vermilion_density > 0.2);
    if (isVerm) {
      stroke(0.549, 0.114, 0.251, 0.7);
    } else {
      stroke(0.129, 0.129, 0.129, 0.45);
    }

    line(cx + cos(th1) * r1, cy + sin(th1) * r1, cx + cos(th2) * r2, cy + sin(th2) * r2);
  }

  // Central hub compass rings
  stroke(0.549, 0.114, 0.251, 0.85);
  strokeWeight(1.4);
  ellipse(cx, cy, 32.0, 32.0);
  stroke(0.129, 0.129, 0.129, 0.7);
  ellipse(cx, cy, 64.0, 64.0);
}
