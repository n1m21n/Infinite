// Vaporwave Collage Ionic Colonnade 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Neoclassical postmodern architectural colonnade with fluted marble columns, tiered stepped podiums, floating platonic spheres, and pastel aesthetic volumes.
param("colonnade_width", 1.0, 0.5, 2.0);
param("column_height", 1.0, 0.5, 2.0);
param("rotation_speed", 0.35, 0.1, 1.2);
param("sphere_orbit", 1.0, 0.4, 2.0);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.38 + sin(time * 0.2) * 0.05);
  rotateY(time * 0.35 + 0.45);

  fill(0.92, 0.90, 0.86);
  push();
  translate(0, -0.85, 0);
  box(1.7 * colonnade_width, 0.12, 1.2);
  translate(0, 0.09, 0);
  box(1.4 * colonnade_width, 0.09, 0.95);
  pop();

  const count = 4;
  for (let c = 0; c < count; c++) {
    const x = ((c / (count - 1)) - 0.5) * 1.1 * colonnade_width;

    push();
    translate(x, -0.22, 0);

    fill(0.88, 0.86, 0.82);
    push();
    translate(0, -0.48 * column_height, 0);
    box(0.18, 0.08, 0.18);
    pop();

    fill(0.95, 0.94, 0.92);
    cylinder(0.065, 0.9 * column_height, 14);

    fill(0.90, 0.88, 0.84);
    push();
    translate(0, 0.48 * column_height, 0);
    box(0.20, 0.08, 0.20);
    fill(1.0, 0.44, 0.81);
    push();
    translate(-0.09, 0, 0);
    cylinder(0.045, 0.18, 10);
    pop();
    push();
    translate(0.09, 0, 0);
    cylinder(0.045, 0.18, 10);
    pop();
    pop();

    pop();
  }

  fill(0.91, 0.89, 0.85);
  push();
  translate(0, 0.35 * column_height, 0);
  box(1.5 * colonnade_width, 0.16, 0.45);

  translate(0, 0.11, 0);
  fill(0.94, 0.93, 0.90);
  box(1.6 * colonnade_width, 0.08, 0.52);
  pop();

  fill(0.02, 0.80, 0.99);
  push();
  translate(0.55 * sphere_orbit, 0.65, 0.25);
  sphere(0.16, 16, 16);
  pop();

  fill(0.02, 1.0, 0.63);
  push();
  translate(-0.5 * sphere_orbit, -0.15, 0.4);
  sphere(0.12, 14, 14);
  pop();

  pop();
}
