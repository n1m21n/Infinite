// Lilliputian Surreal Celestial Monolith 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Metaphysical surrealist architectural landscape featuring an immense monolithic gateway tower and giant planetary sphere standing in a vast terracotta plain, orbited by miniature geometric satellite cubes.
param("spin", 0.35, 0.1, 1.2);
param("scale_factor", 1.15, 0.5, 2.0);
param("monolith_height", 2.2, 1.0, 3.5);
param("satellites", 6.0, 3.0, 12.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.48 + sin(time * 0.12) * 0.04);
  rotateY(time * 0.24 + 0.7);

  // 1. Vast stepped terracotta desert plinth
  push();
  translate(0, 1.25 * scale_factor, 0);
  fill(0.769, 0.412, 0.333); // Dusty terracotta (#c46955)
  box(2.8 * scale_factor, 0.12, 2.8 * scale_factor);

  // Upper stepped terrace tier
  translate(0, -0.09, 0);
  fill(0.867, 0.835, 0.780); // Warm travertine cream (#ddd5c7)
  box(2.0 * scale_factor, 0.06, 2.0 * scale_factor);
  pop();

  // 2. Colossal surrealist monolith pylon tower (Alabaster monolith)
  push();
  translate(-0.35 * scale_factor, (1.25 - monolith_height * 0.5) * scale_factor, -0.2 * scale_factor);
  fill(0.867, 0.835, 0.780);
  box(0.45 * scale_factor, monolith_height * scale_factor, 0.35 * scale_factor);

  // Monolith interior gateway aperture
  push();
  translate(0, monolith_height * 0.25 * scale_factor, 0);
  fill(0.08, 0.10, 0.14); // Dark void portal
  box(0.22 * scale_factor, monolith_height * 0.45 * scale_factor, 0.38 * scale_factor);
  pop();

  // Terracotta cornice header
  push();
  translate(0, -monolith_height * 0.52 * scale_factor, 0);
  fill(0.769, 0.412, 0.333);
  box(0.55 * scale_factor, 0.10 * scale_factor, 0.45 * scale_factor);
  pop();
  pop();

  // 3. Giant celestial sphere suspended in space
  push();
  translate(0.55 * scale_factor, -0.35 * scale_factor, 0.25 * scale_factor);
  rotateY(-time * 0.5);

  // Sphere approximated by stepped equatorial & polar slabs
  fill(0.867, 0.835, 0.780);
  box(0.85 * scale_factor, 0.85 * scale_factor, 0.85 * scale_factor);

  // Equatorial terracotta ring band
  fill(0.769, 0.412, 0.333);
  box(1.2 * scale_factor, 0.08 * scale_factor, 1.2 * scale_factor);
  pop();

  // 4. Lilliputian micro-cubes & miniature satellite geometry
  const nSats = floor(satellites);
  for (let i = 0; i < nSats; i++) {
    const frac = i / nSats;
    const ang = frac * Math.PI * 2.0 + time * (0.8 + frac * 0.4);
    const rOrbit = (1.2 + sin(i * 1.5) * 0.3) * scale_factor;
    const yOrbit = (-0.8 + frac * 1.6) * scale_factor;

    push();
    translate(cos(ang) * rOrbit, yOrbit, sin(ang) * rOrbit);
    rotateX(ang * 2.0);
    rotateY(ang * 1.5);

    if (i % 2 === 0) {
      fill(0.482, 0.549, 0.494); // Dusty sage micro-cube
      box(0.12 * scale_factor, 0.12 * scale_factor, 0.12 * scale_factor);
    } else {
      fill(0.769, 0.412, 0.333); // Terracotta micro-pylon
      box(0.08 * scale_factor, 0.24 * scale_factor, 0.08 * scale_factor);
    }
    pop();
  }

  // 5. Solitary miniature observer pillar on plinth edge
  push();
  translate(1.0 * scale_factor, 1.15 * scale_factor, 0.9 * scale_factor);
  fill(0.482, 0.549, 0.494);
  box(0.08 * scale_factor, 0.22 * scale_factor, 0.08 * scale_factor);
  pop();

  pop();
}
