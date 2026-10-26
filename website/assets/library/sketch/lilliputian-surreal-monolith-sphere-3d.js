// Lilliputian Surreal Monolith Sphere 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Colossal floating bone-ivory sphere hovering over slate indigo stepped plinths with miniature terracotta sentinel pedestals.
param("elevation", 0.25, -1.0, 1.5);
param("pedestal_scale", 1.0, 0.4, 2.0);
param("ring_radius", 0.85, 0.4, 1.6);
param("rotation_speed", 0.4, 0.1, 1.2);
param("sentinel_count", 8.0, 4.0, 16.0);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.42 + sin(time * 0.25) * 0.06);
  rotateY(time * 0.3);

  // Deep slate indigo base plinths (stepped square podiums)
  fill(0.102, 0.165, 0.227);
  for (let s = 0; s < 3; s++) {
    const pw = (1.4 - s * 0.3) * pedestal_scale;
    const py = -0.7 + s * 0.08;
    push();
    translate(0, py, 0);
    box(pw, 0.08, pw);
    pop();
  }

  // Ring of miniature sentinel pedestals around the podium
  const n = floor(sentinel_count);
  for (let i = 0; i < n; i++) {
    const a = (i / n) * TAU;
    const rx = cos(a) * 0.95 * pedestal_scale;
    const rz = sin(a) * 0.95 * pedestal_scale;
    push();
    translate(rx, -0.46, rz);
    fill(0.102, 0.165, 0.227); // Slate column
    box(0.04, 0.22, 0.04);

    translate(0, 0.14, 0);
    fill(0.722, 0.290, 0.224); // Terracotta miniature beacon head
    sphere(0.035, 12);
    pop();
  }

  // Colossal floating bone-ivory sphere
  push();
  translate(0, elevation + sin(time * 0.6) * 0.08, 0);
  fill(0.910, 0.847, 0.784); // Warm bone ivory
  sphere(0.48, 36);

  // Terracotta red equatorial celestial ring
  fill(0.722, 0.290, 0.224);
  rotateX(HALF_PI + sin(time * 0.4) * 0.2);
  rotateZ(time * 0.2);
  torus(ring_radius, 0.025, 28);
  pop();

  pop();
}
