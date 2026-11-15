// Origami Folded Vault Pleated Rotunda 3D - Infinite Library (Sketch 3D)
param("pleats", 20, 10, 32);
param("pitch", 0.58, 0.25, 0.95);
param("oculus", 0.34, 0.18, 0.60);
param("orbit", 0.24, 0.0, 1.0);

function draw(t) {
  rotateY(t * orbit + 0.32);
  rotateX(-0.22);

  // Stepped dark slate foundation (#1B1D20)
  push();
  translate(0, -1.05, 0);
  fill(0.106, 0.114, 0.125);
  cylinder(1.55, 0.12);
  pop();

  push();
  translate(0, -0.94, 0);
  fill(0.220, 0.228, 0.242);
  cylinder(1.36, 0.08);
  pop();

  // Brass oculus compression ring (#C7A254)
  push();
  translate(0, 0.58, 0);
  fill(0.780, 0.635, 0.329);
  torus(oculus, 0.028);
  pop();

  // Base perimeter tension ring
  push();
  translate(0, -0.48, 0);
  fill(0.427, 0.416, 0.392);
  torus(1.24, 0.018);
  pop();

  const n = floor(pleats);
  const breath = pitch + 0.06 * sin(t * 0.95);

  // Radial accordion-pleated origami cupola fins
  for (let i = 0; i < n; i++) {
    const a = (i / n) * TAU;
    const isPeak = i % 2 === 0;
    const midR = (oculus + 1.24) * 0.5;
    const yMid = isPeak ? 0.08 : -0.02;

    push();
    rotateY(a);
    translate(midR, yMid, 0);
    rotateZ(breath);
    rotateX(isPeak ? 0.38 : -0.38);
    if (isPeak) {
      fill(0.918, 0.902, 0.871); // sunlit travertine
    } else {
      fill(0.620, 0.605, 0.575); // folded shadow facet
    }
    box(1.12, 0.032, 0.24);
    pop();

    // Vertical slate perimeter pier every 2 pleats
    if (isPeak) {
      push();
      rotateY(a);
      translate(1.22, -0.70, 0);
      fill(0.160, 0.168, 0.180);
      box(0.065, 0.48, 0.065);
      pop();
    }
  }
}
