// Suprematist Geometric Cantilever 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Interlocking constructivist cantilever beams, offset rectangular planes, and hovering geometric volumes.
param("cantilever_reach", 1.1, 0.4, 2.0);
param("beam_depth", 0.16, 0.05, 0.35);
param("tumble_speed", 0.35, 0.05, 1.2);
param("red_plane_scale", 1.0, 0.4, 1.8);
param("grid_steps", 5.0, 2.0, 8.0);

function draw(t) {
  push();
  const time = t * tumble_speed;

  rotateY(time * 0.5);
  rotateX(0.42 + cos(time * 0.35) * 0.18);
  rotateZ(0.2 + sin(time * 0.28) * 0.1);

  const L = cantilever_reach;

  // 1. Long Primary Transverse Cantilever (Carbon Black #111111)
  push();
  translate(0, 0.1, 0);
  rotateZ(0.15);
  fill(0.067, 0.067, 0.067);
  box(L * 1.5, beam_depth, beam_depth * 1.4);
  pop();

  // 2. Vertical Piercing Pillar
  push();
  translate(L * 0.25, -0.15, 0);
  fill(0.067, 0.067, 0.067);
  box(beam_depth * 1.2, L * 1.3, beam_depth * 1.2);
  pop();

  // 3. Suprematist Dominant Red Floating Plane (#D9231F)
  push();
  translate(-L * 0.35, 0.32, L * 0.25);
  rotateX(0.55);
  rotateY(-0.35);
  rotateZ(0.785);
  fill(0.851, 0.137, 0.122);
  box(0.85 * red_plane_scale, 0.04, 0.65 * red_plane_scale);
  pop();

  // 4. Secondary Counter-Plane (Light Neutral White/Gray)
  push();
  translate(L * 0.45, -0.4, -L * 0.2);
  rotateY(0.8);
  rotateZ(-0.3);
  fill(0.2, 0.22, 0.25);
  box(0.55, 0.03, 0.75);
  pop();

  // 5. Intersecting Structural Coordinate Rods
  const steps = floor(grid_steps);
  for (let i = 0; i < steps; i++) {
    const u = (i / (steps - 1) - 0.5) * L * 1.2;
    push();
    fill(0.851, 0.137, 0.122);
    tube(u, -0.6, -0.3, u, 0.6, 0.3, 0.014, 6);
    pop();
  }

  // 6. Geometric Anchor Cube and Nodal Sphere
  push();
  translate(-L * 0.55, -0.25, -0.15);
  rotateX(0.785);
  fill(0.92, 0.72, 0.12);
  box(0.22, 0.22, 0.22);
  pop();

  push();
  translate(L * 0.6, 0.35, 0.2);
  fill(0.851, 0.137, 0.122);
  sphere(0.12, 16, 16);
  pop();

  pop();
}
