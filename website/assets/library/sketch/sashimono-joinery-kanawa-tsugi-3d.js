// Sashimono Joinery Kanawa Tsugi 3D - Infinite Library (Sketch 3D)
param("explode_dist", 0.28, 0.02, 0.65);
param("pin_offset", 0.18, 0.0, 0.55);
param("tilt_angle", 0.42, 0.1, 1.0);
param("spin_speed", 0.20, 0.0, 0.8);

function draw(t) {
  rotateX(tilt_angle);
  rotateY(t * spin_speed + 0.72);

  const pulse = 0.5 + 0.5 * Math.sin(t * 0.8);
  const gap = explode_dist * (0.45 + 0.55 * pulse);

  // Left beam: aged cedar (#8C5A32) with stepped scarf tenon
  push();
  translate(-gap * 0.75, 0, 0);
  fill(0.549, 0.353, 0.196);
  // Main beam shank
  push(); translate(-0.85, 0, 0); box(1.10, 0.46, 0.46); pop();
  // Lower scarf tongue
  push(); translate(-0.05, -0.11, 0); box(0.70, 0.22, 0.46); pop();
  // End hook abutment
  push(); translate(0.24, 0.05, 0); box(0.16, 0.34, 0.46); pop();
  // Mortise cheek rails
  push(); translate(-0.18, 0.12, 0.16); box(0.45, 0.20, 0.14); pop();
  push(); translate(-0.18, 0.12, -0.16); box(0.45, 0.20, 0.14); pop();
  pop();

  // Right beam: hinoki cypress (#D6B88A) interlocking partner
  push();
  translate(gap * 0.75, 0, 0);
  fill(0.839, 0.722, 0.541);
  // Main beam shank
  push(); translate(0.85, 0, 0); box(1.10, 0.46, 0.46); pop();
  // Upper scarf tongue
  push(); translate(0.05, 0.11, 0); box(0.70, 0.22, 0.46); pop();
  // End hook abutment
  push(); translate(-0.24, -0.05, 0); box(0.16, 0.34, 0.46); pop();
  // Mortise cheek rails
  push(); translate(0.18, -0.12, 0.16); box(0.45, 0.20, 0.14); pop();
  push(); translate(0.18, -0.12, -0.16); box(0.45, 0.20, 0.14); pop();
  pop();

  // Central charred sugi draw-pin wedge (shachi-sen, #2B2118)
  push();
  translate(0, pin_offset * (0.4 + 0.6 * pulse), 0);
  fill(0.169, 0.129, 0.094);
  box(0.16, 0.82, 0.22);
  // Brass cap trim (#C89D66)
  fill(0.784, 0.616, 0.400);
  translate(0, 0.42, 0);
  box(0.20, 0.05, 0.26);
  pop();

  // Base architectural cradle collar
  push();
  translate(0, -0.58, 0);
  fill(0.169, 0.129, 0.094);
  box(1.65, 0.07, 0.72);
  fill(0.784, 0.616, 0.400);
  translate(-0.72, 0.05, 0);
  box(0.08, 0.08, 0.74);
  translate(1.44, 0, 0);
  box(0.08, 0.08, 0.74);
  pop();
}
