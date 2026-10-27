// Brutalist Concrete Cantilever 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Monumental poured-concrete cantilever structure with interlocking stepped cubic volumes, deep recessed fenestration apertures, and raw structural facets.
param("cantilever_reach", 1.0, 0.5, 2.0);
param("elevation", 0.1, -0.8, 1.2);
param("rotation_speed", 0.35, 0.1, 1.2);
param("aperture_depth", 0.06, 0.02, 0.15);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.48 + sin(time * 0.2) * 0.05);
  rotateY(time * 0.35 + 0.6);

  // Massive stepped foundation plinth (weathered raw concrete)
  fill(0.35, 0.35, 0.34);
  push();
  translate(0, -0.75 + elevation, 0);
  box(1.5, 0.12, 1.5);
  translate(0, 0.08, 0);
  box(1.2, 0.10, 1.2);
  pop();

  // Central vertical brutalist elevator / service core
  fill(0.48, 0.48, 0.47); // Poured concrete grey
  push();
  translate(-0.15, 0.0 + elevation, -0.15);
  box(0.55, 1.3, 0.55);

  // Deep recessed vertical shadow slit in the core
  fill(0.12, 0.12, 0.12);
  translate(0.28, 0, 0);
  box(aperture_depth, 1.1, 0.12);
  pop();

  // Primary dramatic horizontal cantilevered wing (east-west beam)
  fill(0.56, 0.56, 0.54); // Concrete sunlit face
  push();
  translate(0.25 * cantilever_reach, 0.2 + elevation, 0);
  box(0.95 * cantilever_reach, 0.38, 0.65);

  // Row of recessed rhythmic window apertures under the cantilever
  fill(0.12, 0.12, 0.12); // Deep window shadow
  for (let i = -2; i <= 2; i++) {
    push();
    translate(i * 0.16 * cantilever_reach, 0.0, 0.33);
    box(0.10, 0.24, aperture_depth);
    pop();
  }
  pop();

  // Secondary high-level orthogonal cantilever (jutting forward)
  fill(0.44, 0.44, 0.43);
  push();
  translate(-0.1, 0.55 + elevation, 0.25 * cantilever_reach);
  box(0.5, 0.32, 0.85 * cantilever_reach);

  // Structural cantilever overhang teeth
  fill(0.30, 0.30, 0.29);
  translate(0, -0.16, 0.25 * cantilever_reach);
  box(0.46, 0.08, 0.28);
  pop();

  // Base entrance colonnade pilasters
  fill(0.22, 0.22, 0.22);
  for (let c = 0; c < 4; c++) {
    push();
    translate(0.15 + c * 0.2, -0.45 + elevation, 0.35);
    box(0.08, 0.45, 0.08);
    pop();
  }

  pop();
}
