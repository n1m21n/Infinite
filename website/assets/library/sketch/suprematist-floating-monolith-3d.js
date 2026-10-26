// Suprematist Floating Monolith 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Suspended carbon black rectangular monolith flanked by dynamic red diagonal wedges and interlocking cantilever planes.
param("monolith_scale", 1.0, 0.4, 2.0);
param("cantilever_spread", 0.65, 0.2, 1.5);
param("orbit_speed", 0.4, 0.05, 1.5);
param("wedge_tilt", 0.52, 0.1, 1.2);
param("strut_density", 4.0, 2.0, 8.0);

function draw(t) {
  push();
  const time = t * orbit_speed;

  // Gentle floating spatial drift
  rotateY(time * 0.45);
  rotateX(0.38 + sin(time * 0.3) * 0.15);
  rotateZ(sin(time * 0.25) * 0.12);

  // 1. Central Hero Monolith (Dense Carbon Black #111111)
  push();
  fill(0.067, 0.067, 0.067);
  box(0.42 * monolith_scale, 1.35 * monolith_scale, 0.32 * monolith_scale);
  pop();

  // 2. Suprematist Vermilion Red Angled Wedge Plane (#D9231F)
  push();
  translate(-0.45 * cantilever_spread, 0.25, 0.35);
  rotateZ(wedge_tilt);
  rotateY(0.25);
  fill(0.851, 0.137, 0.122);
  box(0.12, 0.95, 0.55);
  pop();

  // 3. Counterbalance Horizontal Slabs
  push();
  translate(0.55 * cantilever_spread, -0.32, -0.2);
  rotateX(-0.35);
  rotateZ(-0.25);
  fill(0.067, 0.067, 0.067);
  box(0.75, 0.08, 0.45);
  pop();

  // 4. Cadmium Golden Yellow Accent Bar (#E5A912)
  push();
  translate(0.15, 0.65 * monolith_scale, -0.35);
  rotateY(0.785);
  fill(0.92, 0.72, 0.12);
  box(0.08, 0.08, 0.85);
  pop();

  // 5. Tension Axis Rods (Interconnecting coordinate spines)
  const rods = floor(strut_density);
  for (let i = 0; i < rods; i++) {
    const frac = (i / rods) - 0.5;
    const y = frac * 1.4;
    push();
    fill(0.15, 0.15, 0.15);
    tube(-0.8 * cantilever_spread, y, frac * 0.4, 0.8 * cantilever_spread, y + 0.1, -frac * 0.4, 0.012, 6);
    pop();
  }

  // 6. Floating Satellite Discs/Spheres
  push();
  translate(-0.65 * cantilever_spread, -0.55, 0.45);
  fill(0.851, 0.137, 0.122);
  sphere(0.14, 16, 16);
  pop();

  push();
  translate(0.7 * cantilever_spread, 0.5, -0.4);
  fill(0.067, 0.067, 0.067);
  sphere(0.09, 12, 12);
  pop();

  pop();
}
