// Ito Serpentine Lattice 3D - Infinite Library (Sketch 3D)
param("ribs", 16, 8, 28);
param("span", 2.4, 1.5, 3.5);
param("twist", 0.38, 0.1, 0.85);
param("spin", 0.18, 0.0, 0.6);

function draw(t) {
  const n = floor(ribs);
  rotateY(t * spin + 0.52);
  rotateX(0.28);

  // Dark slate architectural plinth
  push();
  translate(0, -0.78, 0);
  fill(0.11, 0.14, 0.17);
  box(span * 1.12, 0.08, span * 1.12);
  pop();

  // Intersecting algorithmic steel-plate roof & wall ribs
  for (let i = 0; i < n; i++) {
    const u = i / n;
    const ang = u * PI * 1.45 + 0.22;
    const off = (u - 0.5) * span * 0.88;

    // Roof lattice beam
    push();
    translate(0, 0.62, 0);
    rotateY(ang);
    translate(0, 0, off * 0.42);
    fill(i % 3 === 0 ? 0.894 : 0.706, i % 3 === 0 ? 0.918 : 0.765, i % 3 === 0 ? 0.937 : 0.808);
    box(span * 1.05, 0.045, 0.065);
    pop();

    // Diagonal folded wall plates
    const wallAng = ang + twist * sin(u * TAU);
    const wx = cos(wallAng) * (span * 0.44);
    const wz = sin(wallAng) * (span * 0.44);
    push();
    translate(wx, -0.08, wz);
    rotateY(-wallAng + PI * 0.5);
    rotateZ(0.32 * cos(u * PI * 3.0));
    fill(i % 2 === 0 ? 0.894 : 0.431, i % 2 === 0 ? 0.918 : 0.545, i % 2 === 0 ? 0.937 : 0.639);
    box(0.05, 1.42, 0.52);
    pop();
  }
}
