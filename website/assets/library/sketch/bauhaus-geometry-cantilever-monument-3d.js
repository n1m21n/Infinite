// Bauhaus Geometry Cantilever Monument 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Monumental architectural 3D study combining asymmetric stepped cantilever slabs, intersecting geometric pylons, cylindrical structural pivots, and bold primary color blocking.
param("spin", 0.38, 0.1, 1.2);
param("scale_factor", 1.15, 0.5, 2.0);
param("tiers", 5.0, 3.0, 8.0);
param("cantilever_depth", 0.45, 0.1, 1.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.55 + sin(time * 0.14) * 0.04);
  rotateY(time * 0.28 + 0.55);

  // Deep architectural plinth (#1d3557)
  push();
  translate(0, 1.25 * scale_factor, 0);
  fill(0.114, 0.208, 0.341);
  box(2.4 * scale_factor, 0.12, 2.4 * scale_factor);
  pop();

  const nTiers = floor(tiers);

  // 1. Asymmetric stepped cantilever tower blocks
  for (let i = 0; i < nTiers; i++) {
    const frac = i / (nTiers - 1);
    const yPos = (0.85 - frac * 1.8) * scale_factor;
    const w = (1.4 - frac * 0.6) * scale_factor;
    const h = (0.24) * scale_factor;
    const d = (1.2 - frac * 0.4) * scale_factor;

    const xOffset = sin(i * 1.6 + 0.4) * cantilever_depth * scale_factor;
    const zOffset = cos(i * 1.4) * cantilever_depth * 0.7 * scale_factor;

    push();
    translate(xOffset, yPos, zOffset);

    // Primary slab color: alternating Bauhaus palette
    if (i % 3 === 0) {
      fill(0.839, 0.157, 0.157); // Crimson red cantilever (#d62828)
    } else if (i % 3 === 1) {
      fill(0.949, 0.910, 0.812); // Plaster cream (#f2e8cf)
    } else {
      fill(0.988, 0.749, 0.286); // Brass cadmium yellow (#fcbf49)
    }
    box(w, h, d);

    // Cantilever overhang balcony railing
    push();
    translate(0, -h * 0.6, d * 0.45);
    fill(0.114, 0.208, 0.341); // Prussian blue
    box(w * 0.95, h * 0.35, 0.04 * scale_factor);
    pop();

    pop();
  }

  // 2. Intersecting vertical structural pylon in Prussian blue
  push();
  translate(-0.35 * scale_factor, 0, -0.25 * scale_factor);
  fill(0.114, 0.208, 0.341);
  box(0.28 * scale_factor, 2.2 * scale_factor, 0.28 * scale_factor);

  // Vertical cream accent fin
  translate(0.18 * scale_factor, 0, 0);
  fill(0.949, 0.910, 0.812);
  box(0.04 * scale_factor, 2.0 * scale_factor, 0.24 * scale_factor);
  pop();

  // 3. Cadmium yellow sun finial cube floating at peak
  push();
  translate(0.20 * scale_factor, -1.25 * scale_factor, 0.15 * scale_factor);
  rotateY(-time * 0.6);
  fill(0.988, 0.749, 0.286);
  box(0.32 * scale_factor, 0.32 * scale_factor, 0.32 * scale_factor);

  // Crimson core
  fill(0.839, 0.157, 0.157);
  box(0.14 * scale_factor, 0.14 * scale_factor, 0.14 * scale_factor);
  pop();

  pop();
}
