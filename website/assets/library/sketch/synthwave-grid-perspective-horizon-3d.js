// Synthwave Grid Perspective Horizon 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Retro-futuristic 3D perspective grid terrain with segmented glowing sun discs, neon geometric pylons, wireframe horizon mesh, and floating platonic octahedra.
param("road_width", 1.0, 0.5, 2.0);
param("sun_scale", 1.1, 0.5, 2.0);
param("rotation_speed", 0.35, 0.1, 1.2);
param("pylon_height", 1.0, 0.5, 2.0);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.42 + sin(time * 0.2) * 0.04);
  rotateY(time * 0.28 + 0.35);

  // Receding ground grid floor slabs
  fill(0.12, 0.04, 0.22); // Deep midnight violet
  push();
  translate(0, -0.85, 0);
  box(2.2 * road_width, 0.08, 2.2);
  pop();

  // Grid transversal road slats (perspective stripes)
  const slats = 9;
  for (let s = 0; s < slats; s++) {
    const z = -0.9 + (s / (slats - 1)) * 1.8;
    fill(0.02, 0.85, 0.91); // Electric cyan
    push();
    translate(0, -0.80, z);
    box(1.8 * road_width, 0.03, 0.06);
    pop();
  }

  // Flanking neon highway light pylons along the road
  const pylons = 5;
  for (let p = 0; p < pylons; p++) {
    const z = -0.8 + (p / (pylons - 1)) * 1.6;
    const xL = -0.95 * road_width;
    const xR = 0.95 * road_width;

    // Left pylon
    fill(0.20, 0.08, 0.35);
    push();
    translate(xL, -0.4 * pylon_height, z);
    box(0.08, 0.8 * pylon_height, 0.08);
    // Neon beacon top
    fill(1.0, 0.165, 0.427); // Hot Magenta
    translate(0, 0.45 * pylon_height, 0);
    box(0.14, 0.10, 0.14);
    pop();

    // Right pylon
    fill(0.20, 0.08, 0.35);
    push();
    translate(xR, -0.4 * pylon_height, z);
    box(0.08, 0.8 * pylon_height, 0.08);
    // Neon beacon top
    fill(0.02, 0.85, 0.91); // Electric Cyan
    translate(0, 0.45 * pylon_height, 0);
    box(0.14, 0.10, 0.14);
    pop();
  }

  // Horizon tiered sunset disc
  push();
  translate(0, 0.35, -1.0);
  fill(1.0, 0.827, 0.098); // Amber yellow
  sphere(0.48 * sun_scale, 16, 16);

  fill(1.0, 0.165, 0.427); // Magenta lower ring
  translate(0, -0.22 * sun_scale, 0);
  box(1.2 * sun_scale, 0.06, 0.2);
  translate(0, -0.12 * sun_scale, 0);
  box(1.0 * sun_scale, 0.05, 0.2);
  pop();

  // Floating central rotating diamond crystal
  fill(0.02, 0.85, 0.91); // Cyan
  push();
  translate(0, 0.05 + sin(time * 1.5) * 0.08, 0);
  rotateY(time * 1.2);
  rotateZ(0.785398);
  box(0.26, 0.26, 0.26);

  fill(1.0, 0.827, 0.098); // Amber inner core
  box(0.12, 0.12, 0.12);
  pop();

  pop();
}
