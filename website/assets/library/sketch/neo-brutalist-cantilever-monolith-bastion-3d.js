// Neo-Brutalist Cantilever Monolith Bastion 3D - Infinite Library (Sketch 3D)
param("spin", 0.30, 0.1, 1.0);
param("scale_factor", 1.20, 0.6, 2.0);
param("cantilever_depth", 1.35, 0.6, 2.2);
param("terrace_count", 6.0, 3.0, 9.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.48 + sin(time * 0.12) * 0.03);
  rotateY(time * 0.25 + 0.65);

  // 1. Massive Stepped Foundation Plinth (Deep Structural Concrete #22252A)
  push();
  translate(0, 1.25 * scale_factor, 0);
  fill(0.133, 0.145, 0.165);
  box(2.8 * scale_factor, 0.15 * scale_factor, 2.8 * scale_factor);

  translate(0, -0.12 * scale_factor, 0);
  fill(0.251, 0.267, 0.298);
  box(2.3 * scale_factor, 0.10 * scale_factor, 2.3 * scale_factor);

  translate(0, -0.09 * scale_factor, 0);
  fill(0.420, 0.443, 0.478);
  box(1.8 * scale_factor, 0.08 * scale_factor, 1.8 * scale_factor);
  pop();

  // 2. Central Monolithic Structural Spine (Board-Marked Concrete & Terracotta Reveal)
  push();
  translate(0, 0.18 * scale_factor, 0);
  fill(0.553, 0.576, 0.612); // Medium board-marked concrete
  box(0.65 * scale_factor, 2.2 * scale_factor, 0.55 * scale_factor);

  // Recessed vertical terracotta oxide structural shear wall (#C84B31)
  translate(0.36 * scale_factor, 0, 0);
  fill(0.784, 0.294, 0.192);
  box(0.12 * scale_factor, 2.3 * scale_factor, 0.45 * scale_factor);

  translate(-0.72 * scale_factor, 0, 0);
  fill(0.133, 0.145, 0.165);
  box(0.12 * scale_factor, 1.9 * scale_factor, 0.45 * scale_factor);
  pop();

  // 3. Cantilevered Horizontal Bastion Slabs (Asymmetric Stepped Terraces)
  const nSlabs = floor(terrace_count);
  for (let k = 0; k < nSlabs; k++) {
    const frac = k / nSlabs;
    const yPos = (0.85 - k * 0.38) * scale_factor;
    const shiftX = ((k % 2 === 0) ? 0.42 : -0.42) * cantilever_depth * scale_factor;
    const shiftZ = ((k % 3 === 0) ? 0.32 : -0.32) * scale_factor;
    const slabW = (1.6 - frac * 0.35) * scale_factor;
    const slabD = (1.05 - frac * 0.22) * scale_factor;

    push();
    translate(shiftX, yPos, shiftZ);
    rotateY(k * 0.28 - 0.15);

    // Alternating concrete tones and terracotta accent lintels
    if (k % 3 === 0) {
      fill(0.855, 0.871, 0.898); // Light cast concrete
    } else if (k % 3 === 1) {
      fill(0.420, 0.443, 0.478); // Medium concrete
    } else {
      fill(0.784, 0.294, 0.192); // Terracotta oxide
    }
    box(slabW, 0.16 * scale_factor, slabD);

    // Deep shadow reveal under slab
    push();
    translate(0, 0.09 * scale_factor, 0);
    fill(0.082, 0.090, 0.106);
    box(slabW * 0.90, 0.04 * scale_factor, slabD * 0.90);
    pop();

    pop();
  }

  // 4. Heavy Diagonal Tension Buttreses & Outer Pylon Columns
  push();
  translate(0.55 * scale_factor, -0.10 * scale_factor, 0.35 * scale_factor);
  rotateZ(0.44);
  fill(0.784, 0.294, 0.192); // Terracotta accent column
  box(0.14 * scale_factor, 1.8 * scale_factor, 0.14 * scale_factor);
  pop();

  push();
  translate(-0.55 * scale_factor, -0.05 * scale_factor, -0.30 * scale_factor);
  rotateZ(-0.40);
  fill(0.251, 0.267, 0.298);
  box(0.16 * scale_factor, 1.7 * scale_factor, 0.14 * scale_factor);
  pop();

  // 5. Crown Observation Deck & Stele Finial
  push();
  translate(0, -1.15 * scale_factor, 0);
  fill(0.855, 0.871, 0.898);
  box(0.80 * scale_factor, 0.14 * scale_factor, 0.80 * scale_factor);

  translate(0, -0.12 * scale_factor, 0);
  fill(0.784, 0.294, 0.192);
  box(0.50 * scale_factor, 0.10 * scale_factor, 0.50 * scale_factor);

  translate(0, -0.09 * scale_factor, 0);
  fill(0.133, 0.145, 0.165);
  box(0.24 * scale_factor, 0.08 * scale_factor, 0.24 * scale_factor);
  pop();

  pop();
}
