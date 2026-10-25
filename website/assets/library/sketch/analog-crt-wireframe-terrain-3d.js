// Analog CRT Wireframe Terrain 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Retro vector arcade perspective terrain grid scrolling towards the horizon with glowing phosphor vectors and radar beacon monoliths.
param("grid_size", 20.0, 8.0, 36.0);
param("flight_speed", 0.8, 0.1, 3.0);
param("undulation", 0.45, 0.1, 1.2);
param("horizon_curve", 0.35, 0.0, 1.0);
param("beacon_count", 6.0, 2.0, 12.0);

function draw(t) {
  push();
  const time = t * flight_speed;

  // Camera perspective tilt over terrain
  rotateX(0.75);
  translate(0, -0.4, 0);

  const N = floor(grid_size);
  const span = 3.6;
  const step = span / N;

  // 1. Perspective wireframe grid with elevation undulation
  for (let z = 0; z < N; z++) {
    const pz1 = -span * 0.5 + z * step;
    const pz2 = -span * 0.5 + (z + 1) * step;

    // Longitudinal grid lines
    for (let x = 0; x < N; x++) {
      const px1 = -span * 0.5 + x * step;
      const px2 = -span * 0.5 + (x + 1) * step;

      // Calculate undulating terrain height
      const dist1 = sqrt(px1 * px1 + pz1 * pz1);
      const dist2 = sqrt(px2 * px1 + pz1 * pz1);

      const y1 = sin(px1 * 2.5 + pz1 * 1.8 + time * 1.5) * undulation * 0.35 - (dist1 * dist1) * horizon_curve * 0.08;
      const y2 = sin(px2 * 2.5 + pz1 * 1.8 + time * 1.5) * undulation * 0.35 - (dist2 * dist2) * horizon_curve * 0.08;
      const y3 = sin(px1 * 2.5 + pz2 * 1.8 + time * 1.5) * undulation * 0.35;

      // Phosphor vector gradient (brighter near camera, cyan towards horizon)
      const depthFog = (z / N);
      fill(
        0.2 + 0.15 * depthFog,
        0.92 - 0.2 * depthFog,
        0.58 + 0.35 * depthFog
      );

      // Draw lateral line segment
      tube(px1, y1, pz1, px2, y2, pz1, 0.018, 6);

      // Draw longitudinal line segment
      tube(px1, y1, pz1, px1, y3, pz2, 0.018, 6);
    }
  }

  // 2. Vector radar beacon monoliths along the horizon
  const numB = floor(beacon_count);
  for (let b = 0; b < numB; b++) {
    const u = b / numB;
    const a = u * 3.14159265 - 1.570796;
    const bx = sin(a) * (span * 0.45);
    const bz = -span * 0.42;
    const by = 0.2 + sin(time * 2.0 + b) * 0.1;

    push();
    translate(bx, by, bz);

    // Glowing radar beacon pyramid
    fill(0.933, 0.945, 0.980); // Specular white beacon top
    box(0.06, 0.35, 0.06);

    // Pulsing radar ring
    fill(1.0, 0.42, 0.21); // Ember warning beacon
    sphere(0.045, 8, 8);
    pop();
  }

  pop();
}
