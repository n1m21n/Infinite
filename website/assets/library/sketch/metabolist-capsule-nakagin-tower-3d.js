// Metabolist Capsule Nakagin Tower 3D - Infinite Library (Sketch 3D)
param("floors", 7, 4, 10);
param("orbit", 0.16, 0.0, 0.6);
param("tilt", 0.22, -0.2, 0.6);
param("cantilever", 0.42, 0.25, 0.65);

function drawCapsule(px, py, pz, yaw, lit) {
  push();
  translate(px, py, pz);
  rotateY(yaw);

  // Precast weathered concrete capsule shell (#D2D3D0)
  fill(0.824, 0.827, 0.816);
  box(0.46, 0.34, 0.62);

  // Dark steel corner trim & structural mounting collar (#2A2C2E)
  fill(0.165, 0.173, 0.180);
  push();
  translate(0, 0, -0.30);
  box(0.48, 0.36, 0.04);
  pop();

  // Circular porthole bezel on front face
  fill(0.20, 0.21, 0.22);
  push();
  translate(0, 0.01, 0.315);
  rotateX(1.5708);
  torus(0.105, 0.016, 18, 8);
  pop();

  // Interior window disc: warm tungsten amber (#E89A3C) or deep slate glass (#181C22)
  if (lit) {
    fill(0.910, 0.604, 0.235);
  } else {
    fill(0.094, 0.110, 0.133);
  }
  push();
  translate(0, 0.01, 0.312);
  rotateX(1.5708);
  cylinder(0.095, 0.014, 18);
  pop();

  pop();
}

function draw(t) {
  rotateX(tilt);
  rotateY(t * orbit + 0.55);

  const nFloors = floor(floors);
  const hTotal = nFloors * 0.40;
  const yBase = -hTotal * 0.5;

  // Dual interconnected concrete service cores (#3A3D40)
  fill(0.227, 0.239, 0.251);
  push();
  translate(-0.24, 0, -0.18);
  box(0.42, hTotal + 0.35, 0.42);
  pop();
  push();
  translate(0.28, 0.12, 0.20);
  box(0.40, hTotal + 0.55, 0.40);
  pop();

  // Dark basalt plinth base
  fill(0.11, 0.12, 0.13);
  push();
  translate(0, yBase - 0.24, 0);
  box(2.2, 0.14, 2.2);
  pop();

  // Plug-in cantilevered capsules around both service shafts
  for (let f = 0; f < nFloors; f++) {
    const yy = yBase + f * 0.40 + 0.22;
    const phase = f % 2 === 0;

    // West core capsules
    drawCapsule(-0.24 - cantilever, yy, -0.18, -1.5708, (f * 3 + 1) % 5 === 0);
    if (phase || f > 1) {
      drawCapsule(-0.24, yy + 0.04, -0.18 - cantilever, 3.14159, (f * 2) % 4 === 0);
    }

    // East core capsules
    drawCapsule(0.28 + cantilever, yy + 0.08, 0.20, 1.5708, (f * 5 + 2) % 6 === 0);
    if (!phase || f % 3 === 0) {
      drawCapsule(0.28, yy + 0.02, 0.20 + cantilever, 0.0, (f + 1) % 3 === 0);
    }
  }
}
