// Brutalist Architecture Stepped Cantilever Monument 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Spomenik-inspired civic monument of interlocking cantilevered concrete slabs with angular shadow recesses and monumental stepped plinths.
param("monument_scale", 1.05, 0.5, 2.0);
param("cantilever_spread", 1.25, 0.5, 2.2);
param("rotation_speed", 0.35, 0.05, 1.2);
param("tier_count", 5.0, 3.0, 8.0);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.44 + sin(time * 0.18) * 0.04);
  rotateY(time * 0.28 + 0.85);

  // Stepped foundation plinth in basalt (#1e1f24)
  push();
  translate(0, 0.92, 0);
  fill(0.12, 0.12, 0.14);
  box(2.5 * monument_scale, 0.16, 2.5 * monument_scale);
  pop();

  push();
  translate(0, 0.76, 0);
  fill(0.42, 0.42, 0.41);
  box(2.1 * monument_scale, 0.16, 2.1 * monument_scale);
  pop();

  // Monumental angular cantilever wings radiating outward
  const tiers = floor(tier_count);
  for (let i = 0; i < tiers; i++) {
    const yTier = 0.55 - i * 0.28 * monument_scale;
    const w = (1.5 - i * 0.18) * monument_scale;
    const overhang = (0.2 + i * 0.18) * cantilever_spread;

    // Wing Left
    push();
    translate(-overhang, yTier, 0);
    rotateZ(-0.12 * i);
    fill(i % 2 === 0 ? 0.82 : 0.74, i % 2 === 0 ? 0.81 : 0.73, i % 2 === 0 ? 0.78 : 0.70);
    box(w, 0.18 * monument_scale, 1.1 * monument_scale);
    pop();

    // Wing Right
    push();
    translate(overhang, yTier, 0);
    rotateZ(0.12 * i);
    fill(i % 2 === 0 ? 0.84 : 0.76, i % 2 === 0 ? 0.83 : 0.75, i % 2 === 0 ? 0.80 : 0.72);
    box(w, 0.18 * monument_scale, 1.1 * monument_scale);
    pop();
  }

  // Central dark recess spine
  push();
  translate(0, 0.10, 0);
  fill(0.18, 0.18, 0.21);
  box(0.55 * monument_scale, 1.35 * monument_scale, 0.65 * monument_scale);
  pop();

  // Raw rust primer red cantilever datum line (#d64527)
  push();
  translate(0, -0.62 * monument_scale, 0);
  fill(0.85, 0.25, 0.14);
  box(1.6 * monument_scale, 0.08, 0.85 * monument_scale);
  pop();

  // Top monolithic capping block
  push();
  translate(0, -0.74 * monument_scale, 0);
  fill(0.88, 0.87, 0.84);
  box(0.95 * monument_scale, 0.16 * monument_scale, 0.95 * monument_scale);
  pop();

  pop();
}
