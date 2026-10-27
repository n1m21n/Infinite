// Brutalist Concrete Megastructure 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Colossal brutalist civic tower complex with repeating heavy precast spandrels, tiered geometric buttresses, and raw textured chamfers.
param("tier_height", 1.0, 0.5, 2.0);
param("spandrel_scale", 1.0, 0.4, 1.8);
param("rotation_speed", 0.35, 0.1, 1.2);
param("buttress_flare", 0.35, 0.1, 0.8);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.42 + sin(time * 0.2) * 0.04);
  rotateY(time * 0.32);

  // Tiered stepped foundation podium
  fill(0.32, 0.32, 0.31);
  push();
  translate(0, -0.85, 0);
  box(1.6, 0.14, 1.6);
  translate(0, 0.10, 0);
  box(1.3, 0.10, 1.3);
  pop();

  // Four monumental angled corner buttresses
  fill(0.42, 0.42, 0.41);
  for (let b = 0; b < 4; b++) {
    const ang = b * HALF_PI;
    const bx = cos(ang) * (0.65 + buttress_flare * 0.3);
    const bz = sin(ang) * (0.65 + buttress_flare * 0.3);
    push();
    translate(bx, -0.3, bz);
    rotateY(ang + 0.785);
    box(0.22, 0.9, 0.35);
    pop();
  }

  // Central monolithic brutalist tower shaft
  fill(0.52, 0.52, 0.50); // Concrete facade grey
  push();
  translate(0, 0.05, 0);
  box(0.72, 1.3 * tier_height, 0.72);

  // Repeating horizontal precast concrete spandrel ribs
  for (let s = -3; s <= 3; s++) {
    const sy = s * (0.17 * tier_height);
    push();
    translate(0, sy, 0);
    fill(0.62, 0.62, 0.60); // Highlight on projecting spandrel
    box(0.82 * spandrel_scale, 0.06, 0.82 * spandrel_scale);

    // Deep recessed shadow window slot beneath spandrel
    fill(0.12, 0.12, 0.12);
    box(0.76, 0.07, 0.76);
    pop();
  }
  pop();

  // Elevated crown observation cantilever pod
  fill(0.46, 0.46, 0.45);
  push();
  translate(0, 0.82 * tier_height, 0);
  box(1.05 * spandrel_scale, 0.22, 1.05 * spandrel_scale);

  // Deep recessed panoramic ribbon window
  fill(0.10, 0.10, 0.10);
  box(1.08 * spandrel_scale, 0.08, 1.08 * spandrel_scale);
  pop();

  pop();
}
