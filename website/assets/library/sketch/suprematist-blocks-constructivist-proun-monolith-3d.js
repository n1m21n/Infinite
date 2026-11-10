// Suprematist Blocks Constructivist Proun Monolith 3D - Infinite Library (Sketch 3D)
param("spin", 0.35, 0.1, 1.2);
param("scale_factor", 1.15, 0.6, 2.0);
param("cantilever", 1.20, 0.5, 2.0);
param("tier_count", 6.0, 3.0, 10.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.42 + sin(time * 0.14) * 0.03);
  rotateY(time * 0.30 + 0.75);

  // 1. Heavy Black Foundation Monolith Plinth (#111111)
  push();
  translate(0, 1.20 * scale_factor, 0);
  fill(0.067, 0.067, 0.067);
  box(2.6 * scale_factor, 0.16 * scale_factor, 1.8 * scale_factor);

  translate(0, -0.11 * scale_factor, 0);
  fill(0.92, 0.90, 0.86);
  box(2.1 * scale_factor, 0.08 * scale_factor, 1.4 * scale_factor);
  pop();

  // 2. Suprematist Vermilion Central Anchor Tower (#D9231F)
  push();
  translate(-0.15 * scale_factor, 0.10 * scale_factor, 0);
  fill(0.851, 0.137, 0.122);
  box(0.55 * scale_factor, 2.2 * scale_factor, 0.55 * scale_factor);

  translate(0.38 * scale_factor, 0.15 * scale_factor, 0.12 * scale_factor);
  fill(0.102, 0.231, 0.545);
  box(0.22 * scale_factor, 2.5 * scale_factor, 0.40 * scale_factor);
  pop();

  // 3. Dynamic Cantilevered Proun Floating Horizontal Slabs
  const nTiers = floor(tier_count);
  for (let k = 0; k < nTiers; k++) {
    const frac = k / nTiers;
    const yPos = (0.75 - k * 0.34) * scale_factor;
    const xOff = (sin(k * 1.7) * 0.45 * cantilever) * scale_factor;
    const zOff = (cos(k * 1.4) * 0.35 * cantilever) * scale_factor;
    const slabW = (1.9 - frac * 0.4) * scale_factor;
    const slabD = (0.65 - frac * 0.15) * scale_factor;

    push();
    translate(xOff, yPos, zOff);
    rotateY(k * 0.52);

    if (k % 3 === 0) {
      fill(0.957, 0.937, 0.902);
    } else if (k % 3 === 1) {
      fill(0.067, 0.067, 0.067);
    } else {
      fill(0.957, 0.706, 0.102);
    }
    box(slabW, 0.14 * scale_factor, slabD);

    translate(0, 0.08 * scale_factor, 0);
    fill(0.851, 0.137, 0.122);
    box(slabW * 0.35, 0.04 * scale_factor, slabD * 0.6);
    pop();
  }

  // 4. Diagonal Structural Tension Beams (El Lissitzky Proun diagonal vectors)
  push();
  translate(0.60 * scale_factor, -0.15 * scale_factor, 0.30 * scale_factor);
  rotateZ(0.48);
  fill(0.851, 0.137, 0.122);
  box(0.12 * scale_factor, 2.1 * scale_factor, 0.12 * scale_factor);
  pop();

  push();
  translate(-0.55 * scale_factor, 0.05 * scale_factor, -0.35 * scale_factor);
  rotateZ(-0.44);
  fill(0.102, 0.231, 0.545);
  box(0.14 * scale_factor, 1.9 * scale_factor, 0.14 * scale_factor);
  pop();

  // 5. Crown Finial Composition
  push();
  translate(0, -1.22 * scale_factor, 0);
  fill(0.957, 0.937, 0.902);
  box(0.90 * scale_factor, 0.12 * scale_factor, 0.90 * scale_factor);

  translate(0, -0.11 * scale_factor, 0);
  fill(0.851, 0.137, 0.122);
  box(0.45 * scale_factor, 0.10 * scale_factor, 0.45 * scale_factor);

  translate(0, -0.09 * scale_factor, 0);
  fill(0.067, 0.067, 0.067);
  box(0.20 * scale_factor, 0.08 * scale_factor, 0.20 * scale_factor);
  pop();

  pop();
}
