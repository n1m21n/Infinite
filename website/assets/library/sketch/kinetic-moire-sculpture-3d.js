// Kinetic Moire Sculpture 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Concentric nested hyperboloid cages rotating with differential angular velocity, generating optical moiré interference lattices and topological phase shifts.
param("cages", 4.0, 2.0, 8.0);
param("rib_count", 32.0, 12.0, 64.0);
param("twist_angle", 1.6, 0.2, 3.14);
param("spin_rate", 0.6, 0.0, 3.0);
param("scale_3d", 1.2, 0.5, 2.5);

function draw(t) {
  push();
  rotateY(t * spin_rate * 0.4);
  rotateX(0.65 + sin(t * 0.3) * 0.12);
  rotateZ(cos(t * 0.22) * 0.1);

  const numCages = floor(cages);
  const ribs = floor(rib_count);
  const H = 2.0 * scale_3d;
  const halfH = H * 0.5;

  for (let c = 0; c < numCages; c++) {
    const cageFrac = c / numCages;
    const baseR = (0.45 + cageFrac * 0.65) * scale_3d;
    const waistR = baseR * (0.45 + 0.2 * sin(t * 0.8 + c));

    // Differential counter-rotation per cage creates moiré fringes
    const cageSpin = t * spin_rate * (c % 2 === 0 ? 1.0 : -1.0) * (0.8 + cageFrac * 0.5);
    const cageTwist = twist_angle * (c % 2 === 0 ? 1.0 : -1.0);

    // Color gradient across cages: Signal, Emerald, Ember
    if (c % 3 === 0) {
      fill(0.655, 0.694, 1.0); // Signal
    } else if (c % 3 === 1) {
      fill(0.306, 0.949, 0.722); // Emerald
    } else {
      fill(1.0, 0.42, 0.21); // Ember
    }

    for (let i = 0; i < ribs; i++) {
      const u = i / ribs;
      const angleTop = u * TAU + cageSpin + cageTwist;
      const angleBot = u * TAU + cageSpin - cageTwist;

      const x1 = cos(angleTop) * baseR;
      const z1 = sin(angleTop) * baseR;
      const y1 = halfH;

      const x2 = cos(angleBot) * baseR;
      const z2 = sin(angleBot) * baseR;
      const y2 = -halfH;

      tube(x1, y1, z1, x2, y2, z2, 0.014 * scale_3d, 5);
    }

    // Circular rim collar rings at waist, top, and bottom
    fill(0.96, 0.94, 0.91);
    push();
    translate(0, 0, 0);
    // Central waist collar sphere ring nodes
    for (let k = 0; k < 6; k++) {
      const a = (k / 6) * TAU + cageSpin;
      push();
      translate(cos(a) * waistR, 0, sin(a) * waistR);
      sphere(0.035 * scale_3d);
      pop();
    }
    pop();
  }

  pop();
}
