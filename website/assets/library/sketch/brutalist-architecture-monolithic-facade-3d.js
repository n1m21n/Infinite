// Brutalist Architecture Monolithic Facade 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Monumental raw concrete facade with deep recessed fenestrations, fluted monolithic pilasters, and industrial red primer datum lines.
param("pylon_scale", 1.0, 0.5, 2.0);
param("cantilever_depth", 1.2, 0.5, 2.5);
param("rotation_speed", 0.32, 0.05, 1.0);
param("fenestration_count", 6.0, 3.0, 10.0);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.42 + sin(time * 0.15) * 0.05);
  rotateY(time * 0.25 + 0.55);

  // Deep graphite base foundation slab (#222222)
  push();
  translate(0, 0.95, 0);
  fill(0.13, 0.13, 0.15);
  box(2.4 * pylon_scale, 0.18, 2.4 * pylon_scale);
  pop();

  // Tier 2: Weathered cast aggregate concrete plinth (#7a7a78)
  push();
  translate(0, 0.78, 0);
  fill(0.48, 0.48, 0.47);
  box(2.0 * pylon_scale, 0.18, 2.0 * pylon_scale);
  pop();

  // Massive vertical monolithic facade towers (raw light aggregate stone #d9d6cf)
  push();
  translate(0, -0.05, 0);
  fill(0.85, 0.84, 0.81);
  box(1.2 * pylon_scale, 1.45 * pylon_scale, 0.85 * pylon_scale);
  pop();

  // Fluted pilaster ribs along the facade
  const ribs = floor(fenestration_count);
  for (let i = 0; i < ribs; i++) {
    const xOff = (i - (ribs - 1) * 0.5) * 0.22 * pylon_scale;
    push();
    translate(xOff, -0.05, 0.46 * pylon_scale);
    fill(i % 2 === 0 ? 0.76 : 0.88, i % 2 === 0 ? 0.75 : 0.87, i % 2 === 0 ? 0.72 : 0.84);
    box(0.09 * pylon_scale, 1.40 * pylon_scale, 0.14 * pylon_scale);
    pop();
  }

  // Heavy projecting cantilever slab (dramatic architectural overhang)
  push();
  translate(0.45 * cantilever_depth, -0.45, 0.15);
  fill(0.62, 0.61, 0.59);
  box(1.5 * cantilever_depth, 0.24, 1.1 * pylon_scale);
  pop();

  // Industrial rust / red primer datum beam accent (#d64527)
  push();
  translate(0, -0.82 * pylon_scale, 0);
  fill(0.84, 0.27, 0.15); // Raw industrial primer red
  box(1.3 * pylon_scale, 0.08, 0.95 * pylon_scale);
  pop();

  // Heavy top cornice slab capping the structure
  push();
  translate(0, -0.92 * pylon_scale, 0);
  fill(0.88, 0.87, 0.84);
  box(1.45 * pylon_scale, 0.12, 1.1 * pylon_scale);
  pop();

  pop();
}
