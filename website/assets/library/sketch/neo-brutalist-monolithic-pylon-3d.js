// Neo-Brutalist Monolithic Pylon 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Stark architectural composition featuring heavy chamfered concrete pylons with bold ultramarine accents, deep cast shadows, and cantilevered slabs.
param("pylon_scale", 1.0, 0.5, 2.0);
param("cantilever_span", 1.25, 0.5, 2.0);
param("rotation_speed", 0.35, 0.05, 1.0);
param("fluting_count", 5.0, 3.0, 8.0);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.46 + sin(time * 0.2) * 0.04);
  rotateY(time * 0.3 + 0.65);

  // Stepped foundation plinth in raw cast concrete
  push();
  translate(0, 0.88, 0);
  fill(0.78, 0.77, 0.74);
  box(2.2 * pylon_scale, 0.16, 2.2 * pylon_scale);
  pop();

  push();
  translate(0, 0.72, 0);
  fill(0.18, 0.19, 0.22); // Deep graphite shadow tier
  box(1.8 * pylon_scale, 0.16, 1.8 * pylon_scale);
  pop();

  // Central monolithic vertical pylon tower
  push();
  translate(0, -0.05, 0);
  fill(0.85, 0.84, 0.81);
  box(0.75 * pylon_scale, 1.4 * pylon_scale, 0.75 * pylon_scale);
  pop();

  // Fluting vertical slabs attached to the pylon
  const flutes = floor(fluting_count);
  for (let i = 0; i < flutes; i++) {
    const xOff = (i - (flutes - 1) * 0.5) * 0.15 * pylon_scale;
    push();
    translate(xOff, -0.05, 0.40 * pylon_scale);
    fill(i % 2 === 0 ? 0.72 : 0.88, i % 2 === 0 ? 0.71 : 0.87, i % 2 === 0 ? 0.68 : 0.84);
    box(0.08 * pylon_scale, 1.35 * pylon_scale, 0.12 * pylon_scale);
    pop();
  }

  // Heavy projecting cantilever slab in electric ultramarine
  push();
  translate(0.35 * cantilever_span, -0.42, 0);
  fill(0.0, 0.22, 0.95); // Iconic Neo-Brutalist Ultramarine punch
  box(1.4 * cantilever_span, 0.22, 0.85 * pylon_scale);
  pop();

  // Secondary cantilever slab in industrial charcoal
  push();
  translate(-0.40 * cantilever_span, 0.18, 0.12);
  fill(0.14, 0.15, 0.18);
  box(1.2 * cantilever_span, 0.20, 0.75 * pylon_scale);
  pop();

  // Top capping cornice beam with hazard orange datum accent
  push();
  translate(0, -0.80 * pylon_scale, 0);
  fill(1.0, 0.22, 0.0); // Industrial hazard orange stripe
  box(1.1 * pylon_scale, 0.08, 1.1 * pylon_scale);
  pop();

  push();
  translate(0, -0.88 * pylon_scale, 0);
  fill(0.85, 0.84, 0.81);
  box(1.2 * pylon_scale, 0.10, 1.2 * pylon_scale);
  pop();

  pop();
}
