// Tensegrity Pavilion Geodesic Cantilever 3D - Infinite Library (Sketch 3D)
param("bays", 6, 4, 10);
param("span", 2.2, 1.4, 3.0);
param("tilt", 0.22, -0.4, 0.5);
param("orbit", 0.22, 0.0, 1.0);

function draw(t) {
  const n = floor(bays);
  rotateY(t * orbit + 0.45);
  rotateZ(tilt + sin(t * 0.8) * 0.04);

  // Dark anchor pylon (#222830)
  push();
  translate(-span * 0.48, -0.45, 0);
  fill(0.133, 0.157, 0.188);
  box(0.32, 1.5, 0.52);
  pop();

  // Suspended counterweight torus (#6E7885)
  push();
  translate(-span * 0.48, 0.42, 0);
  rotateX(PI * 0.5);
  fill(0.431, 0.471, 0.522);
  torus(0.36, 0.035);
  pop();

  // Cantilevered triangulated space-frame truss
  const stepX = span / n;
  for (let i = 0; i < n; i++) {
    const x = -span * 0.4 + i * stepX;
    const taper = 1.0 - (i / n) * 0.45;
    const h = 0.46 * taper;
    const zOff = 0.26 * taper;

    // Upper and lower chord struts (#B8C0C8)
    push();
    translate(x + stepX * 0.5, h, 0);
    rotateZ(PI * 0.5);
    fill(0.722, 0.753, 0.784);
    cylinder(0.026, stepX);
    pop();

    push();
    translate(x + stepX * 0.5, -h * 0.6, zOff);
    rotateZ(PI * 0.5);
    fill(0.722, 0.753, 0.784);
    cylinder(0.024, stepX);
    pop();

    push();
    translate(x + stepX * 0.5, -h * 0.6, -zOff);
    rotateZ(PI * 0.5);
    fill(0.722, 0.753, 0.784);
    cylinder(0.024, stepX);
    pop();

    // Cross ribs & diagonal tension cables
    push();
    translate(x, -h * 0.6, 0);
    rotateX(PI * 0.5);
    fill(0.431, 0.471, 0.522);
    cylinder(0.016, zOff * 2.0);
    pop();

    push();
    translate(x, 0.1 * h, zOff * 0.5);
    rotateX(-0.48);
    fill(0.431, 0.471, 0.522);
    cylinder(0.012, h * 1.75);
    pop();

    push();
    translate(x, 0.1 * h, -zOff * 0.5);
    rotateX(0.48);
    fill(0.431, 0.471, 0.522);
    cylinder(0.012, h * 1.75);
    pop();

    // Node sphere joints
    push();
    translate(x, h, 0);
    fill(0.133, 0.157, 0.188);
    sphere(0.045);
    pop();
  }
}
