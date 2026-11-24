// Scarpa Venetian Water Pavilion 3D - Infinite Library (Sketch 3D)
param("colonnade", 6, 4, 10);
param("step_tiers", 4, 2, 7);
param("cam_tilt", 0.32, 0.15, 0.6);
param("spin", 0.18, 0.0, 0.45);

function draw(t) {
  rotateX(cam_tilt + 0.05 * sin(t * 0.3));
  rotateY(t * spin + 0.48);

  // Dark reflecting water basin (#141B20)
  fill(0.078, 0.106, 0.125);
  push();
  translate(0, -1.02, 0);
  box(3.5, 0.08, 3.5);
  pop();

  // Stepped travertine island plinths (#D9D0BE)
  const tiers = floor(step_tiers);
  for (let i = 0; i < tiers; i++) {
    const u = i / max(1, tiers - 1);
    const sz = 2.4 - i * 0.28;
    fill(0.851 - u * 0.06, 0.816 - u * 0.06, 0.745 - u * 0.05);
    push();
    translate(0, -0.92 + i * 0.11, 0);
    box(sz, 0.10, sz * 0.85);
    pop();
  }

  // Interlocking double-ring concrete portals (Vesica Piscis motif)
  for (let side = -1; side <= 1; side += 2) {
    fill(0.820, 0.780, 0.706);
    push();
    translate(side * 0.34, 0.25, 0.12 * side);
    torus(0.68, 0.065);
    pop();

    // Inner bronze inlay ring (#8C6B3B)
    fill(0.549, 0.420, 0.231);
    push();
    translate(side * 0.34, 0.25, 0.12 * side);
    torus(0.56, 0.022);
    pop();
  }

  // Perimeter stepped colonnade piers
  const nC = floor(colonnade);
  for (let i = 0; i < nC; i++) {
    const u = (i / max(1, nC - 1)) - 0.5;
    const x = u * 2.2;
    fill(0.765, 0.725, 0.655);
    push();
    translate(x, -0.15, -0.95);
    box(0.14, 1.35, 0.18);
    pop();

    // Pier capital notch
    fill(0.549, 0.420, 0.231);
    push();
    translate(x, 0.55, -0.95);
    box(0.18, 0.05, 0.22);
    pop();
  }

  // Overhead cantilevered concrete canopy beam
  fill(0.851, 0.816, 0.745);
  push();
  translate(0, 0.66, -0.95);
  box(2.55, 0.14, 0.32);
  pop();
}
