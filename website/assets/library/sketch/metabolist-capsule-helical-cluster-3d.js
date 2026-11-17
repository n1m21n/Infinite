// Metabolist Capsule Helical Cluster 3D - Infinite Library (Sketch 3D)
param("pods", 18, 10, 28);
param("helix_turns", 2.2, 1.0, 3.5);
param("orbit", 0.18, 0.0, 0.6);
param("tilt", 0.28, -0.2, 0.7);

function draw(t) {
  rotateX(tilt);
  rotateY(t * orbit + 0.4);

  const n = floor(pods);
  const heightSpan = 2.6;

  // Central tensioned concrete megastructure spine (#2B2E32)
  fill(0.169, 0.180, 0.196);
  cylinder(0.26, heightSpan + 0.45, 16);

  // Structural steel ring collars along the spine (#5B6066)
  fill(0.357, 0.376, 0.400);
  for (let r = 0; r < 5; r++) {
    const ry = -heightSpan * 0.45 + (r / 4) * heightSpan * 0.9;
    push();
    translate(0, ry, 0);
    rotateX(1.5708);
    torus(0.33, 0.032, 20, 8);
    pop();
  }

  // Helical cantilevered Metabolist capsule pods
  for (let i = 0; i < n; i++) {
    const u = i / (n - 1);
    const angle = u * TAU * helix_turns;
    const yy = (u - 0.5) * heightSpan;
    const arm = 0.68 + 0.12 * sin(i * 1.3);

    push();
    rotateY(angle);

    // Cantilever steel truss bracket
    fill(0.25, 0.27, 0.29);
    push();
    translate(arm * 0.5, yy, 0);
    box(arm * 0.85, 0.07, 0.10);
    pop();

    // Modular concrete pod (#D2D3D0)
    translate(arm, yy, 0);
    fill(0.824, 0.827, 0.816);
    box(0.48, 0.30, 0.42);

    // Recessed circular porthole bezel on outer face
    fill(0.165, 0.173, 0.180);
    push();
    translate(0.245, 0, 0);
    rotateZ(1.5708);
    torus(0.095, 0.015, 16, 8);
    pop();

    // Warm tungsten interior aperture on selected capsules
    if (i % 4 === 1) {
      fill(0.910, 0.604, 0.235);
    } else {
      fill(0.094, 0.110, 0.133);
    }
    push();
    translate(0.242, 0, 0);
    rotateZ(1.5708);
    cylinder(0.085, 0.012, 16);
    pop();

    pop();
  }
}
