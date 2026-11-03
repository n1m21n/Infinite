// Vaporwave Collage Pedestal Temple 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Monumental stepped temple pavilion with cantilevered entablature beams, hollow archways, tiered ceremonial plinths, and geometric sun disc accents.
param("temple_scale", 1.0, 0.5, 2.0);
param("arch_height", 1.0, 0.6, 1.8);
param("rotation_speed", 0.35, 0.1, 1.2);
param("disc_offset", 0.45, 0.1, 0.9);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.40 + sin(time * 0.2) * 0.04);
  rotateY(time * 0.32 + 0.5);

  fill(0.92, 0.90, 0.86);
  push();
  translate(0, -0.88, 0);
  box(1.7 * temple_scale, 0.12, 1.7 * temple_scale);
  translate(0, 0.10, 0);
  box(1.4 * temple_scale, 0.10, 1.4 * temple_scale);
  translate(0, 0.09, 0);
  box(1.15 * temple_scale, 0.08, 1.15 * temple_scale);
  pop();

  fill(0.95, 0.94, 0.92);
  for (let c = 0; c < 4; c++) {
    const ang = c * 1.5707963 + 0.785398;
    const px = cos(ang) * 0.55 * temple_scale;
    const pz = sin(ang) * 0.55 * temple_scale;
    push();
    translate(px, -0.22, pz);
    box(0.14, 0.95 * arch_height, 0.14);
    fill(1.0, 0.44, 0.81);
    translate(0, 0.5 * arch_height, 0);
    box(0.20, 0.06, 0.20);
    pop();
  }

  fill(0.88, 0.86, 0.82);
  push();
  translate(0, -0.15, 0);
  push();
  translate(-0.25, 0, 0);
  box(0.12, 0.8 * arch_height, 0.25);
  pop();
  push();
  translate(0.25, 0, 0);
  box(0.12, 0.8 * arch_height, 0.25);
  pop();
  push();
  translate(0, 0.44 * arch_height, 0);
  box(0.62, 0.12, 0.25);
  pop();
  pop();

  fill(0.93, 0.92, 0.89);
  push();
  translate(0, 0.45 * arch_height, 0);
  box(1.45 * temple_scale, 0.14, 1.45 * temple_scale);
  translate(0, 0.10, 0);
  box(1.25 * temple_scale, 0.08, 1.25 * temple_scale);
  pop();

  fill(0.02, 0.80, 0.99);
  push();
  translate(0, 0.75 * arch_height + disc_offset * 0.3, 0);
  sphere(0.22, 16, 16);
  pop();

  fill(0.02, 1.0, 0.63);
  push();
  translate(0.65, 0.1, 0.35);
  sphere(0.10, 12, 12);
  pop();

  pop();
}
