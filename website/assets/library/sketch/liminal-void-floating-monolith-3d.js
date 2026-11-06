// Liminal Void Floating Monolith 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Atmospheric liminal anime landscape featuring floating geometric monolith blocks above an abyssal navy horizon plane with crimson dusk gradients.
param("monolith_scale", 1.0, 0.5, 2.0);
param("float_height", 0.35, 0.1, 0.8);
param("rotation_speed", 0.4, 0.05, 1.2);
param("terrace_levels", 5.0, 3.0, 8.0);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.42 + sin(time * 0.2) * 0.06);
  rotateY(time * 0.3 + 0.6);

  // Stepped foundation floor terraces in abyssal navy and crimson
  const levels = floor(terrace_levels);
  for (let l = 0; l < levels; l++) {
    const fl = l / levels;
    const w = (2.2 - fl * 0.7) * monolith_scale;
    const y = -0.75 + l * 0.05;

    push();
    translate(0, y, 0);
    if (l % 2 === 0) {
      fill(0.000, 0.188, 0.286); // Abyssal navy
    } else {
      fill(0.839, 0.157, 0.157); // Crimson sunset
    }
    box(w, 0.045, w);
    pop();
  }

  // Floating central rectangular monolith
  push();
  const hoverY = 0.05 + sin(time * 0.8) * float_height * 0.2;
  translate(0, hoverY, 0);
  rotateY(time * 0.5);

  // Main monolith block in amber twilight
  fill(0.969, 0.498, 0.000);
  box(0.38 * monolith_scale, 1.35 * monolith_scale, 0.38 * monolith_scale);

  // Crimson core accent stripe
  translate(0, 0, 0);
  fill(0.839, 0.157, 0.157);
  box(0.42 * monolith_scale, 0.18 * monolith_scale, 0.42 * monolith_scale);

  // Pale warm haze top cap
  translate(0, 0.72 * monolith_scale, 0);
  fill(0.918, 0.886, 0.718);
  box(0.32 * monolith_scale, 0.10 * monolith_scale, 0.32 * monolith_scale);
  pop();

  // Flanking satellite monolith shards hovering in orbit
  const shards = 4;
  for (let s = 0; s < shards; s++) {
    const ang = (s * 6.2831853) / shards + time * 0.35;
    const dist = 0.95 * monolith_scale;
    const sx = cos(ang) * dist;
    const sz = sin(ang) * dist;
    const sy = -0.15 + sin(time * 1.1 + s * 1.5) * 0.14;

    push();
    translate(sx, sy, sz);
    rotateY(ang + 0.8);
    if (s % 2 === 0) {
      fill(0.839, 0.157, 0.157); // Crimson
      box(0.14 * monolith_scale, 0.75 * monolith_scale, 0.14 * monolith_scale);
    } else {
      fill(0.918, 0.886, 0.718); // Pale haze
      box(0.12 * monolith_scale, 0.55 * monolith_scale, 0.12 * monolith_scale);
    }
    pop();
  }

  // Outer orbital halo ring
  push();
  translate(0, 0.1, 0);
  rotateX(1.5707963);
  rotateZ(time * 0.4);
  fill(0.969, 0.498, 0.000);
  torus(1.15 * monolith_scale, 0.025);
  pop();

  pop();
}
