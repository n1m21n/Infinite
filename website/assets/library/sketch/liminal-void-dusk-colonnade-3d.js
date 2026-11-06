// Liminal Void Dusk Colonnade 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Minimalist infinite architectural colonnade receding into a crimson twilight sky, featuring warm amber structural pillars and abyssal foundation terraces.
param("colonnade_span", 1.0, 0.5, 1.8);
param("pillar_height", 1.2, 0.6, 2.0);
param("travel_speed", 0.35, 0.05, 1.0);
param("arch_spacing", 0.55, 0.3, 0.9);

function draw(t) {
  push();
  const time = t * travel_speed;

  rotateX(0.32 + sin(time * 0.15) * 0.04);
  rotateY(sin(time * 0.25) * 0.22);

  // Deep floor foundation platform in abyssal navy
  fill(0.000, 0.188, 0.286);
  push();
  translate(0, -0.72, 0);
  box(2.4 * colonnade_span, 0.08, 3.4 * colonnade_span);

  // Central runway pathway in crimson sunset
  fill(0.839, 0.157, 0.157);
  translate(0, 0.05, 0);
  box(0.68 * colonnade_span, 0.04, 3.4 * colonnade_span);

  // Stepped pathway center line in pale warm haze
  fill(0.918, 0.886, 0.718);
  translate(0, 0.03, 0);
  box(0.12 * colonnade_span, 0.03, 3.4 * colonnade_span);
  pop();

  // Colonnade pillars receding along Z-axis
  const count = 7;
  const colW = 0.75 * colonnade_span;
  const pH = pillar_height;

  for (let i = -count; i <= count; i++) {
    const z = i * arch_spacing;

    // Left pillar in amber twilight
    push();
    translate(-colW, -0.68 + pH * 0.5, z);
    fill(0.969, 0.498, 0.000);
    box(0.10 * colonnade_span, pH, 0.10 * colonnade_span);

    // Pillar capital top block in crimson
    translate(0, pH * 0.5 + 0.04, 0);
    fill(0.839, 0.157, 0.157);
    box(0.16 * colonnade_span, 0.08, 0.16 * colonnade_span);
    pop();

    // Right pillar in amber twilight
    push();
    translate(colW, -0.68 + pH * 0.5, z);
    fill(0.969, 0.498, 0.000);
    box(0.10 * colonnade_span, pH, 0.10 * colonnade_span);

    // Pillar capital top block in crimson
    translate(0, pH * 0.5 + 0.04, 0);
    fill(0.839, 0.157, 0.157);
    box(0.16 * colonnade_span, 0.08, 0.16 * colonnade_span);
    pop();

    // Connecting architrave lintel overhead
    push();
    translate(0, -0.68 + pH + 0.08, z);
    fill(0.918, 0.886, 0.718); // Pale warm haze
    box(colW * 2.1, 0.08, 0.12 * colonnade_span);
    pop();
  }

  // Floating focal orb at the far perspective horizon
  push();
  translate(0, -0.15, -1.8);
  fill(0.969, 0.498, 0.000);
  sphere(0.26 * colonnade_span);

  // Pale aura halo around beacon
  fill(0.918, 0.886, 0.718);
  torus(0.42 * colonnade_span, 0.02);
  pop();

  pop();
}
