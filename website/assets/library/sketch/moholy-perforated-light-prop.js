// Moholy Perforated Light Prop - Infinite Library (Sketch 2D)
param("holes", 8, 4, 14);
param("gratings", 22, 10, 36);
param("speed", 0.32, 0.05, 1.0);

function draw(t) {
  // Deep projection-chamber slate (#0B0C10)
  background(0.043, 0.047, 0.063);
  const w = width;
  const h = height;
  const side = min(w, h);
  const cx = w * 0.5;
  const cy = h * 0.5;
  const ph = t * speed;

  // Diagonal tungsten light-cone wash (#E8A850)
  noStroke();
  for (let i = 0; i < 5; i++) {
    const u = i / 4.0;
    fill(0.910, 0.659, 0.314, 0.045 - u * 0.007);
    beginShape();
    vertex(cx - side * 0.38, cy - side * 0.42);
    vertex(cx + side * (0.12 + u * 0.14), cy - side * 0.12);
    vertex(cx + side * (0.44 - u * 0.06), cy + side * 0.44);
    vertex(cx - side * (0.10 + u * 0.08), cy + side * 0.40);
    endShape();
  }

  // Cast shadow fan of parallel steel rod gratings
  push();
  translate(cx + side * 0.08, cy + side * 0.05);
  rotate(-0.42 + sin(ph * 0.6) * 0.06);
  const gSpan = side * 0.66;
  const nG = floor(gratings);
  for (let i = 0; i < nG; i++) {
    const u = i / max(1, nG - 1);
    const x = (u - 0.5) * gSpan;
    stroke(0.02, 0.024, 0.035, 0.55);
    strokeWeight(4.5 * (side / 1080.0));
    line(x + 14, -gSpan * 0.55, x + 28, gSpan * 0.55);
  }
  pop();

  // Primary perforated brushed-steel disc (#C8CCD4)
  push();
  translate(cx - side * 0.04, cy - side * 0.02);
  rotate(ph * 0.35);
  const discR = side * 0.34;
  fill(0.784, 0.800, 0.831, 0.18);
  stroke(0.784, 0.800, 0.831, 0.78);
  strokeWeight(2.6 * (side / 1080.0));
  circle(0, 0, discR * 2.0);

  // Perforated hole matrix punched in concentric rings
  const nH = floor(holes);
  for (let rIdx = 1; rIdx <= 3; rIdx++) {
    const rr = discR * (0.28 + rIdx * 0.20);
    const cnt = nH * rIdx;
    const hr = side * (0.024 - rIdx * 0.003);
    for (let j = 0; j < cnt; j++) {
      const a = (j / cnt) * TAU + rIdx * 0.22;
      const hx = cos(a) * rr;
      const hy = sin(a) * rr;
      fill(0.043, 0.047, 0.063, 0.92);
      stroke(0.910, 0.659, 0.314, 0.42);
      strokeWeight(1.4 * (side / 1080.0));
      circle(hx, hy, hr * 2.0);
    }
  }
  pop();

  // Parallel steel tension rods & emerald specular rim (#58A688)
  push();
  translate(cx, cy);
  rotate(0.32 - ph * 0.18);
  for (let i = 0; i < nG; i++) {
    const u = i / max(1, nG - 1);
    const x = (u - 0.5) * side * 0.52;
    const yLen = side * (0.36 - abs(u - 0.5) * 0.24);
    stroke(0.784, 0.800, 0.831, 0.72);
    strokeWeight(2.0 * (side / 1080.0));
    line(x, -yLen, x, yLen);
    if (i % 3 === 0) {
      stroke(0.345, 0.651, 0.533, 0.55);
      strokeWeight(1.2 * (side / 1080.0));
      line(x + 2, -yLen * 0.8, x + 2, yLen * 0.8);
    }
  }
  pop();

  // Diagonal glass-helix spine
  push();
  translate(cx, cy);
  rotate(-0.62);
  noFill();
  stroke(0.910, 0.659, 0.314, 0.85);
  strokeWeight(2.2 * (side / 1080.0));
  beginShape();
  for (let k = 0; k <= 64; k++) {
    const u = k / 64.0;
    const x = (u - 0.5) * side * 0.72;
    const y = sin(u * TAU * 4.5 + ph * 1.4) * side * 0.036;
    vertex(x, y);
  }
  endShape();
  pop();
}
