// Hopf Fibration 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Stereographic projection of 4D 3-sphere fibers into nested Villarceau circles in 3D.
param("circles", 24, 8, 48);
param("radius", 1.4, 0.5, 3.0);
param("tube_r", 0.035, 0.01, 0.12);
param("spin", 0.6, 0.0, 3.0);
param("warp", 0.45, 0.0, 1.0);

function draw(t) {
  push();
  rotateY(t * spin * 0.7);
  rotateX(t * spin * 0.4);
  rotateZ(sin(t * 0.3) * 0.25);

  const N = floor(circles);
  const segments = 64;
  const R = radius * 0.9;
  const r = radius * 0.45 * (0.8 + 0.2 * sin(t * 0.8));

  for (let c = 0; c < N; c++) {
    const alpha = (c / N) * TAU;
    const pts = [];

    // Each fiber is a Villarceau circle forming a dense nested congruence
    for (let i = 0; i <= segments; i++) {
      const phi = (i / segments) * TAU;
      const theta = alpha + (warp * sin(phi * 2.0 + t * 0.5));

      // Coordinate parameterization of the Villarceau fiber
      const x = (R * cos(theta) - r * sin(phi) * sin(theta));
      const y = (R * sin(theta) + r * sin(phi) * cos(theta));
      const z = r * cos(phi);

      pts.push([x, y, z]);
    }

    // Render continuous luminous tubular fiber
    const frac = c / N;
    const hue = (0.62 + frac * 0.4 + sin(t * 0.2) * 0.1) % 1.0;
    fill(hsl(hue, 0.82, 0.60));

    for (let i = 0; i < segments; i++) {
      const p1 = pts[i];
      const p2 = pts[i + 1];
      tube(p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], tube_r, 8);
    }
  }

  // Glowing central focal core
  fill(hsl(0.08, 0.95, 0.75));
  sphere(tube_r * 4.5, 12);

  pop();
}
