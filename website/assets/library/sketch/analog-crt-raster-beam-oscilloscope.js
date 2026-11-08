// Analog CRT Raster Beam Oscilloscope - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Cathode ray tube oscilloscope visualizer with dual-trace Lissajous beam deflection, high-frequency harmonic waveforms, horizontal sync pulse blanking, and calibrated green phosphor graticule grid.
param("frequency_x", 3.0, 1.0, 8.0);
param("frequency_y", 4.0, 1.0, 8.0);
param("speed", 0.45, 0.1, 1.5);
param("beam_width", 2.6, 1.0, 4.5);

function draw(t) {
  // Deep CRT glass shadow with dark phosphor tone (#08140c)
  background(0.038, 0.075, 0.048);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;

  push();
  translate(cx, cy);

  // 1. CRT bezel outer frame and corner curves
  stroke(0.0, 0.85, 0.35, 0.85);
  strokeWeight(2.6);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  // 2. Calibrated oscilloscope graticule grid (10x10)
  stroke(0.0, 0.55, 0.22, 0.45);
  strokeWeight(1.4);
  for (let k = -4; k <= 4; k++) {
    const p = (k / 5.0) * (span * 0.44);
    line(p, -span * 0.44, p, span * 0.44);
    line(-span * 0.44, p, span * 0.44, p);
  }

  // Central graticule axis tick marks
  stroke(0.0, 0.90, 0.38, 0.75);
  strokeWeight(1.8);
  line(0, -span * 0.46, 0, span * 0.46);
  line(-span * 0.46, 0, span * 0.46, 0);
  for (let k = -20; k <= 20; k++) {
    const p = (k / 20.0) * (span * 0.44);
    const tick = (k % 5 === 0) ? 10.0 : 5.0;
    line(p, -tick, p, tick);
    line(-tick, p, tick, p);
  }

  const time = t * speed;

  // 3. Dual-trace beam: Secondary beam (magenta reference wave)
  stroke(1.0, 0.0, 0.33, 0.82); // Phosphor red/magenta (#ff0055)
  strokeWeight(beam_width * 0.95);
  noFill();
  beginShape();
  const steps = 320;
  for (let i = 0; i <= steps; i++) {
    const u = (i / steps) * Math.PI * 2.0;
    const x = sin(u * frequency_x + time * 1.8) * (span * 0.40);
    const y = sin(u * (frequency_y + 1.0) - time * 1.2) * (span * 0.38);
    vertex(x, y);
  }
  endShape();

  // 4. Primary electron beam (high-intensity phosphor green #00ff66)
  stroke(0.0, 1.0, 0.42, 0.95);
  strokeWeight(beam_width);
  noFill();
  beginShape();
  for (let i = 0; i <= steps; i++) {
    const u = (i / steps) * Math.PI * 2.0;
    const x = sin(u * frequency_x + time * 1.4) * (span * 0.42);
    const y = sin(u * frequency_y + time * 1.1 + 0.75) * (span * 0.42);
    vertex(x, y);
  }
  endShape();

  // White core beam filament
  stroke(0.92, 1.0, 0.96, 0.90);
  strokeWeight(beam_width * 0.5);
  noFill();
  beginShape();
  for (let i = 0; i <= steps; i += 2) {
    const u = (i / steps) * Math.PI * 2.0;
    const x = sin(u * frequency_x + time * 1.4) * (span * 0.42);
    const y = sin(u * frequency_y + time * 1.1 + 0.75) * (span * 0.42);
    vertex(x, y);
  }
  endShape();

  // 5. Cathode focus electron target spots
  fill(1.0, 1.0, 1.0);
  noStroke();
  circle(sin(time * 1.4) * (span * 0.42), sin(time * 1.1 + 0.75) * (span * 0.42), 6.5);
  fill(0.0, 1.0, 0.42, 0.85);
  circle(sin(time * 1.4) * (span * 0.42), sin(time * 1.1 + 0.75) * (span * 0.42), 14.0);

  pop();
}
