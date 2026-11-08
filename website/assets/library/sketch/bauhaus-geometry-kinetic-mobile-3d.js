// Bauhaus Geometry Kinetic Mobile 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Constructivist kinetic 3D sculpture inspired by Moholy-Nagy and the Bauhaus Light-Space Modulator, featuring rotating primary color planar discs, cantilevered chrome balance arms, and orbital spheres.
param("spin", 0.40, 0.1, 1.2);
param("scale_factor", 1.15, 0.5, 2.0);
param("arms", 4.0, 3.0, 8.0);
param("strut_width", 0.05, 0.02, 0.12);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.52 + sin(time * 0.16) * 0.04);
  rotateY(time * 0.32 + 0.65);

  // Stepped Prussian blue Bauhaus plinth (#1d3557)
  push();
  translate(0, 1.25 * scale_factor, 0);
  fill(0.114, 0.208, 0.341);
  box(2.2 * scale_factor, 0.12, 2.2 * scale_factor);
  // Upper brass plinth tier
  translate(0, -0.09, 0);
  fill(0.988, 0.749, 0.286);
  box(1.5 * scale_factor, 0.06, 1.5 * scale_factor);
  pop();

  // 1. Central vertical brass axle arbor
  fill(0.988, 0.749, 0.286);
  box(strut_width * 1.5 * scale_factor, 2.4 * scale_factor, strut_width * 1.5 * scale_factor);

  const nArms = floor(arms);

  // 2. Counter-rotating horizontal cantilever balance arms with primary color elements
  for (let i = 0; i < nArms; i++) {
    const frac = i / nArms;
    const yOff = (-0.75 + frac * 1.5) * scale_factor;
    const armLength = (0.75 + sin(i * 1.8) * 0.25) * scale_factor;
    const speedMult = (i % 2 === 0 ? 1.0 : -0.85) * (0.35 + frac * 0.25);

    push();
    translate(0, yOff, 0);
    rotateY(time * speedMult + frac * Math.PI * 2.0);

    // Cantilever balance beam (plaster cream #f2e8cf)
    fill(0.949, 0.910, 0.812);
    box(armLength * 2.0, strut_width * scale_factor, strut_width * scale_factor);

    // Tip terminal shapes: alternating primary forms
    // Left tip
    push();
    translate(-armLength, 0, 0);
    if (i % 3 === 0) {
      // Crimson red planar disc/box
      fill(0.839, 0.157, 0.157);
      box(0.28 * scale_factor, 0.28 * scale_factor, 0.06 * scale_factor);
    } else if (i % 3 === 1) {
      // Cadmium yellow cube
      fill(0.988, 0.749, 0.286);
      box(0.24 * scale_factor, 0.24 * scale_factor, 0.24 * scale_factor);
    } else {
      // Prussian blue upright slab
      fill(0.114, 0.208, 0.341);
      box(0.12 * scale_factor, 0.34 * scale_factor, 0.18 * scale_factor);
    }
    pop();

    // Right tip counterweight
    push();
    translate(armLength, 0, 0);
    fill(0.949, 0.910, 0.812);
    box(0.16 * scale_factor, 0.16 * scale_factor, 0.16 * scale_factor);
    // Suspended crimson counter-pip
    translate(0, 0.18 * scale_factor, 0);
    fill(0.839, 0.157, 0.157);
    box(0.08 * scale_factor, 0.22 * scale_factor, 0.08 * scale_factor);
    pop();

    pop();
  }

  // 3. Central orbital kinetic ring
  push();
  rotateY(-time * 0.6);
  rotateZ(0.42);
  const ringSegs = 20;
  const ringR = 0.95 * scale_factor;
  for (let s = 0; s < ringSegs; s++) {
    const th = (s / ringSegs) * Math.PI * 2.0;
    push();
    translate(cos(th) * ringR, 0, sin(th) * ringR);
    rotateY(-th);
    if (s % 5 === 0) {
      fill(0.839, 0.157, 0.157); // Crimson red accent markers
    } else {
      fill(0.949, 0.910, 0.812); // Plaster cream ring
    }
    box(0.05 * scale_factor, 0.05 * scale_factor, (2.0 * Math.PI * ringR / ringSegs) * 0.95);
    pop();
  }
  pop();

  pop();
}
