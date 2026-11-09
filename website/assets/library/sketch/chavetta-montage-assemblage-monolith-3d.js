// Chavetta Montage Assemblage Monolith 3D - Infinite Library (Sketch 3D)
param("spin", 0.32, 0.1, 1.2);
param("scale_factor", 1.15, 0.5, 2.0);
param("cantilever_reach", 1.4, 0.6, 2.4);
param("slab_count", 6.0, 3.0, 10.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.48 + sin(time * 0.11) * 0.03);
  rotateY(time * 0.25 + 0.65);

  // 1. Heavy Base Plinth in Raw Cardboard & Charcoal
  push();
  translate(0, 1.20 * scale_factor, 0);
  fill(0.106, 0.106, 0.114);
  box(2.6 * scale_factor, 0.12, 2.6 * scale_factor);

  translate(0, -0.08, 0);
  fill(0.824, 0.773, 0.690);
  box(2.1 * scale_factor, 0.06, 2.1 * scale_factor);
  pop();

  // 2. Central Monolithic Assemblage Spine (Burnt Sienna & Raw Board)
  push();
  translate(0, 0.15 * scale_factor, 0);
  fill(0.784, 0.353, 0.196);
  box(0.55 * scale_factor, 2.1 * scale_factor, 0.45 * scale_factor);

  translate(0.32 * scale_factor, 0, 0);
  fill(0.824, 0.773, 0.690);
  box(0.15 * scale_factor, 2.2 * scale_factor, 0.35 * scale_factor);

  translate(-0.64 * scale_factor, 0, 0);
  fill(0.290, 0.420, 0.424);
  box(0.15 * scale_factor, 1.8 * scale_factor, 0.35 * scale_factor);
  pop();

  // 3. Cantilevered Mixed-Media Slabs (Stepped & Angled Collage Plates)
  const nSlabs = floor(slab_count);
  for (let k = 0; k < nSlabs; k++) {
    const frac = k / nSlabs;
    const yPos = (0.90 - k * 0.42) * scale_factor;
    const shiftX = ((k % 2 === 0) ? 0.38 : -0.38) * cantilever_reach * scale_factor;
    const shiftZ = ((k % 3 === 0) ? 0.28 : -0.28) * scale_factor;
    const slabW = (1.5 - frac * 0.35) * scale_factor;
    const slabD = (0.95 - frac * 0.20) * scale_factor;

    push();
    translate(shiftX, yPos, shiftZ);
    rotateY((k * 0.35) - 0.2);

    if (k % 3 === 0) {
      fill(0.784, 0.353, 0.196);
    } else if (k % 3 === 1) {
      fill(0.290, 0.420, 0.424);
    } else {
      fill(0.824, 0.773, 0.690);
    }
    box(slabW, 0.14 * scale_factor, slabD);

    push();
    translate(0, 0.08 * scale_factor, 0);
    fill(0.106, 0.106, 0.114);
    box(slabW * 0.85, 0.04 * scale_factor, slabD * 0.85);
    pop();

    pop();
  }

  // 4. Diagonal Bracing & Asymmetrical Relief Steles
  push();
  translate(0.48 * scale_factor, -0.15 * scale_factor, 0.25 * scale_factor);
  rotateZ(0.42);
  fill(0.784, 0.353, 0.196);
  box(0.10 * scale_factor, 1.6 * scale_factor, 0.12 * scale_factor);
  pop();

  push();
  translate(-0.48 * scale_factor, -0.10 * scale_factor, -0.22 * scale_factor);
  rotateZ(-0.38);
  fill(0.290, 0.420, 0.424);
  box(0.12 * scale_factor, 1.5 * scale_factor, 0.10 * scale_factor);
  pop();

  // 5. Crown Ephemera Finial (Stacked Offset Rectangles)
  push();
  translate(0, -1.05 * scale_factor, 0);
  fill(0.824, 0.773, 0.690);
  box(0.75 * scale_factor, 0.12 * scale_factor, 0.75 * scale_factor);

  translate(0, -0.10 * scale_factor, 0);
  fill(0.784, 0.353, 0.196);
  box(0.45 * scale_factor, 0.08 * scale_factor, 0.45 * scale_factor);

  translate(0, -0.08 * scale_factor, 0);
  fill(0.106, 0.106, 0.114);
  box(0.22 * scale_factor, 0.06 * scale_factor, 0.22 * scale_factor);
  pop();

  pop();
}
