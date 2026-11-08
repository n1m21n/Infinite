// Suprematist Blocks Geometric Axis - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Dynamic constructivist balance of intersecting orthogonal coordinate bars, bold diagonal red cantilever beams, floating counterweight rectangles, and rhythmic cross-line markers.
param("axis_tilt", 0.18, -0.6, 0.6);
param("beam_width", 28.0, 12.0, 50.0);
param("drift", 0.42, 0.1, 1.2);
param("balance", 0.85, 0.3, 1.6);

function draw(t) {
  // Warm Russian constructivist canvas (#f4efe6)
  background(0.957, 0.937, 0.902);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;
  const time = t * drift;

  push();
  translate(cx, cy);

  // 1. Dominant vertical black pylon axis
  push();
  rotate(axis_tilt + sin(time * 0.2) * 0.02);
  fill(0.067, 0.067, 0.067);
  noStroke();
  rect(-beam_width * 0.5, -span * 0.48, beam_width, span * 0.96);

  // Horizontal intersecting black beam
  rect(-span * 0.38, -span * 0.08, span * 0.76, beam_width * 0.65);
  pop();

  // 2. Bold diagonal revolutionary red cantilever beam
  push();
  rotate(axis_tilt - 0.72 + cos(time * 0.25) * 0.03);
  fill(0.851, 0.137, 0.122);
  noStroke();
  rect(-span * 0.42, -beam_width * 0.8, span * 0.84, beam_width * 1.6);

  // White negative space slit
  fill(0.957, 0.937, 0.902);
  rect(-span * 0.20, -beam_width * 0.2, span * 0.40, beam_width * 0.4);
  pop();

  // 3. Floating geometric counterweights (asymmetric black & cobalt slabs)
  push();
  translate(-span * 0.25 + sin(time * 0.5) * span * 0.02, span * 0.22);
  rotate(axis_tilt + 0.35);
  fill(0.067, 0.067, 0.067);
  noStroke();
  rect(-span * 0.16, -span * 0.10, span * 0.32, span * 0.20);
  pop();

  push();
  translate(span * 0.24, -span * 0.26 + cos(time * 0.4) * span * 0.02);
  rotate(axis_tilt - 0.40);
  fill(0.851, 0.137, 0.122);
  noStroke();
  circle(0, 0, span * 0.16);

  // Black central pupil
  fill(0.067, 0.067, 0.067);
  circle(0, 0, span * 0.045);
  pop();

  // 4. Rhythmic tick marks and cross-axis calibration pips
  stroke(0.067, 0.067, 0.067, 0.85);
  strokeWeight(2.2);
  for (let k = -5; k <= 5; k++) {
    const p = (k / 5.0) * (span * 0.38);
    line(p, -span * 0.42, p, -span * 0.38);
    line(-span * 0.42, p, -span * 0.38, p);
  }

  // Accent cadmium yellow marker
  noStroke();
  fill(0.988, 0.749, 0.286);
  circle(span * 0.32, span * 0.28, span * 0.055);

  pop();
}
