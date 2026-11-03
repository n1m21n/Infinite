// Synthwave Grid Neon Monolith 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Low-poly synthwave cyber-temple with fluted neon obelisks, stepped luminous plinths, floating concentric rings, and vibrant chrome retro-futuristic volumes.
param("monolith_scale", 1.0, 0.5, 2.0);
param("height_scale", 1.1, 0.6, 2.0);
param("rotation_speed", 0.35, 0.1, 1.2);
param("orbit_radius", 0.65, 0.3, 1.2);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.40 + sin(time * 0.2) * 0.04);
  rotateY(time * 0.32 + 0.48);

  // Stepped podium base plinth
  fill(0.14, 0.05, 0.25);
  push();
  translate(0, -0.88, 0);
  box(1.8 * monolith_scale, 0.12, 1.8 * monolith_scale);
  translate(0, 0.10, 0);
  fill(0.22, 0.08, 0.38);
  box(1.5 * monolith_scale, 0.10, 1.5 * monolith_scale);
  translate(0, 0.09, 0);
  fill(0.02, 0.85, 0.91); // Cyan trim
  box(1.25 * monolith_scale, 0.06, 1.25 * monolith_scale);
  pop();

  // Central monolith tower
  push();
  translate(0, -0.05, 0);

  // Dark obsidian monolith core
  fill(0.10, 0.04, 0.18);
  box(0.38 * monolith_scale, 1.2 * height_scale, 0.38 * monolith_scale);

  // Monolith neon side ribs
  fill(1.0, 0.165, 0.427); // Magenta ribs
  box(0.42 * monolith_scale, 1.1 * height_scale, 0.06);
  box(0.06, 1.1 * height_scale, 0.42 * monolith_scale);

  // Monolith apex capstone
  fill(1.0, 0.827, 0.098); // Luminous Amber Capstone
  translate(0, 0.68 * height_scale, 0);
  box(0.26 * monolith_scale, 0.16, 0.26 * monolith_scale);
  pop();

  // 4 Corner neon obelisks
  for (let c = 0; c < 4; c++) {
    const ang = c * 1.5707963 + 0.785398;
    const px = cos(ang) * 0.68 * monolith_scale;
    const pz = sin(ang) * 0.68 * monolith_scale;

    push();
    translate(px, -0.25, pz);
    fill(0.20, 0.08, 0.35);
    box(0.12, 0.85 * height_scale, 0.12);

    fill(0.02, 0.85, 0.91); // Cyan beacon
    translate(0, 0.45 * height_scale, 0);
    box(0.16, 0.08, 0.16);
    pop();
  }

  // Floating orbiting neon satellites
  const sCount = 3;
  for (let s = 0; s < sCount; s++) {
    const sAng = time * 1.4 + (s * 6.2831853) / sCount;
    const sx = cos(sAng) * orbit_radius;
    const sz = sin(sAng) * orbit_radius;
    const sy = 0.25 + sin(time * 2.0 + s) * 0.12;

    fill(s % 2 === 0 ? 1.0 : 0.02, s % 2 === 0 ? 0.165 : 0.85, s % 2 === 0 ? 0.427 : 0.91);
    push();
    translate(sx, sy, sz);
    rotateY(time * 2.0);
    box(0.10, 0.10, 0.10);
    pop();
  }

  pop();
}
