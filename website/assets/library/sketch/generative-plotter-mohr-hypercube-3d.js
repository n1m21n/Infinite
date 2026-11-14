// Generative Plotter Mohr Hypercube 3D - Infinite Library (Sketch 3D)
param("subdivisions", 3, 2, 5);
param("beam_thick", 0.045, 0.02, 0.09);
param("phase_shift", 0.45, 0.0, 1.5);
param("spin_speed", 0.30, 0.0, 1.0);

function draw(t) {
  rotateX(0.48 + Math.sin(t * 0.3) * 0.12);
  rotateY(t * spin_speed + 0.78);

  const ns = floor(subdivisions);
  const span = 1.35;
  const bt = beam_thick;

  // Outer hypercube skeleton
  for (let sx = -1; sx <= 1; sx += 2) {
    for (let sy = -1; sy <= 1; sy += 2) {
      push();
      fill(0.067, 0.067, 0.067);
      translate(sx * span * 0.5, sy * span * 0.5, 0);
      box(bt, bt, span);
      pop();

      push();
      fill(0.067, 0.067, 0.067);
      translate(sx * span * 0.5, 0, sy * span * 0.5);
      box(bt, span, bt);
      pop();

      push();
      fill(0.067, 0.067, 0.067);
      translate(0, sx * span * 0.5, sy * span * 0.5);
      box(span, bt, bt);
      pop();
    }
  }

  // Inner algorithmic subdivisions & vermilion nodes
  for (let i = 0; i < ns; i++) {
    const u = (i + 0.5) / ns - 0.5;
    const p = u * span * 0.85;
    const rot = phase_shift * (i + 1) + t * 0.2;

    push();
    rotateZ(rot * 0.35);
    fill(0.067, 0.067, 0.067);
    translate(p, 0, 0);
    box(bt * 0.85, span * 0.75, bt * 0.85);
    pop();

    push();
    fill(0.820, 0.188, 0.122); // #d1301f vermilion node
    translate(p, Math.sin(rot) * 0.32, Math.cos(rot) * 0.32);
    sphere(0.11, 16, 12);
    pop();
  }

  // Central diagonal axis strut
  push();
  rotateZ(0.785);
  rotateX(0.615);
  fill(0.820, 0.188, 0.122);
  cylinder(0.032, span * 1.45, 16);
  pop();
}
