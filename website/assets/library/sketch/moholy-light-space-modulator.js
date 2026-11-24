// Moholy Light-Space Modulator - Infinite Library (Sketch 2D)
param("disc_rings", 5, 3, 9);
param("slit_count", 18, 8, 32);
param("rotor_speed", 0.28, 0.0, 1.0);
param("shadow_angle", 0.65, 0.2, 1.2);

function draw(t) {
  // Charcoal architectural ground (#16181C)
  background(0.086, 0.094, 0.110);
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const R = min(w, h) * 0.38;

  // Cast diagonal light beam cone (#2A2F38)
  noStroke();
  fill(0.165, 0.184, 0.220, 0.75);
  beginShape();
  vertex(0, h * 0.12);
  vertex(w * 0.65, 0);
  vertex(w, h * 0.72);
  vertex(w * 0.28, h);
  endShape();

  // Warm parchment circular spotlight disc (#EAE3D5)
  fill(0.918, 0.890, 0.835, 0.12);
  circle(cx, cy, R * 2.15);

  // Rotating perforated aluminum stator rings
  const rings = floor(disc_rings);
  const rot = t * rotor_speed;
  noFill();
  for (let r = 1; r <= rings; r++) {
    const rad = R * (0.28 + (r / rings) * 0.72);
    stroke(0.820, 0.835, 0.855, 0.35 + 0.1 * (r % 2));
    strokeWeight(r % 2 === 0 ? 2.6 : 1.4);
    circle(cx, cy, rad * 2.0);
  }

  // Kinetic perforated slit ring
  const slits = floor(slit_count);
  for (let i = 0; i < slits; i++) {
    const a = (i / slits) * TAU + rot;
    const r0 = R * 0.42;
    const r1 = R * 0.92;
    const x0 = cx + cos(a) * r0;
    const y0 = cy + sin(a) * r0;
    const x1 = cx + cos(a) * r1;
    const y1 = cy + sin(a) * r1;
    stroke(i % 3 === 0 ? 0.831 : 0.780, i % 3 === 0 ? 0.635 : 0.800, i % 3 === 0 ? 0.306 : 0.820, 0.65);
    strokeWeight(i % 3 === 0 ? 3.2 : 1.6);
    line(x0, y0, x1, y1);
  }

  // Diagonal brass & vermilion optical baffle bars
  push();
  translate(cx, cy);
  rotate(shadow_angle + 0.12 * sin(t * rotor_speed * 1.5));
  noStroke();
  fill(0.086, 0.094, 0.110, 0.88);
  rect(-R * 1.05, -R * 0.09, R * 2.10, R * 0.18);
  fill(0.831, 0.635, 0.306, 0.92);
  rect(-R * 0.85, -R * 0.025, R * 1.70, R * 0.05);
  pop();

  // Counter-rotating vermilion aperture ring & hub
  push();
  translate(cx + cos(rot * 0.7) * R * 0.24, cy + sin(rot * 0.7) * R * 0.24);
  noFill();
  stroke(0.839, 0.243, 0.165, 0.95);
  strokeWeight(4.5);
  circle(0, 0, R * 0.44);
  fill(0.918, 0.890, 0.835, 0.92);
  noStroke();
  circle(0, 0, R * 0.09);
  pop();

  // Outer millimeter vernier tick bezel
  stroke(0.831, 0.635, 0.306, 0.55);
  strokeWeight(1.2);
  for (let k = 0; k < 60; k++) {
    const a = (k / 60.0) * TAU;
    const r0 = R * 1.06;
    const r1 = r0 + (k % 5 === 0 ? 14 : 6);
    line(cx + cos(a) * r0, cy + sin(a) * r0, cx + cos(a) * r1, cy + sin(a) * r1);
  }
}
