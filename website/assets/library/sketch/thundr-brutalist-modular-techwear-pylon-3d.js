// Thundr Brutalist Modular Techwear Pylon 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Monolithic postmodern techwear architectural pylon with interlocking precast concrete slabs, high-visibility hazard orange cantilever trusses, and modular industrial techwear bracing.
param("spin", 0.36, 0.1, 1.2);
param("scale_factor", 1.15, 0.5, 2.0);
param("pylon_height", 2.4, 1.0, 3.6);
param("cantilevers", 4.0, 2.0, 8.0);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.50 + sin(time * 0.12) * 0.04);
  rotateY(time * 0.28 + 0.65);

  // 1. Heavy stepped asphalt foundation slab
  push();
  translate(0, 1.25 * scale_factor, 0);
  fill(0.12, 0.13, 0.16); // Dark gunmetal asphalt
  box(2.8 * scale_factor, 0.14, 2.8 * scale_factor);

  // Stepped sub-plinth with hazard orange perimeter
  translate(0, -0.09, 0);
  fill(1.0, 0.33, 0.0); // Hazard Orange (#ff5500)
  box(2.2 * scale_factor, 0.05, 2.2 * scale_factor);

  translate(0, -0.05, 0);
  fill(0.78, 0.80, 0.84); // Precast concrete deck
  box(1.8 * scale_factor, 0.06, 1.8 * scale_factor);
  pop();

  // 2. Monolithic Central Concrete Techwear Pylon
  push();
  translate(0, (1.25 - pylon_height * 0.5) * scale_factor, 0);
  fill(0.82, 0.84, 0.86); // Raw precast concrete
  box(0.55 * scale_factor, pylon_height * scale_factor, 0.55 * scale_factor);

  // Recessed dark mechanical equipment core cavities
  fill(0.08, 0.09, 0.11);
  box(0.38 * scale_factor, pylon_height * 0.70 * scale_factor, 0.60 * scale_factor);
  box(0.60 * scale_factor, pylon_height * 0.50 * scale_factor, 0.38 * scale_factor);

  // Vertical acid-lime telemetry conduit strip
  push();
  translate(0.29 * scale_factor, 0, 0);
  fill(0.78, 1.0, 0.0); // Electric Acid Lime
  box(0.04 * scale_factor, pylon_height * 0.85 * scale_factor, 0.08 * scale_factor);
  pop();
  pop();

  // 3. Interlocking Asymmetric Hazard Cantilever Beams
  const nBeams = floor(cantilevers);
  for (let b = 0; b < nBeams; b++) {
    const frac = b / nBeams;
    const yPos = (0.8 - frac * 1.6) * scale_factor;
    const ang = frac * Math.PI * 1.5 + time * 0.15;
    const len = (1.4 + sin(b * 1.7) * 0.3) * scale_factor;

    push();
    translate(0, yPos, 0);
    rotateY(ang);

    // Hazard orange heavy cantilever spar
    push();
    translate(len * 0.5, 0, 0);
    fill(1.0, 0.33, 0.0);
    box(len, 0.12 * scale_factor, 0.18 * scale_factor);

    // Suspended modular pod at spar tip
    translate(len * 0.45, 0.14 * scale_factor, 0);
    fill(0.18, 0.19, 0.22);
    box(0.24 * scale_factor, 0.20 * scale_factor, 0.24 * scale_factor);

    // Acid lime status beacon pip
    fill(0.78, 1.0, 0.0);
    box(0.08 * scale_factor, 0.08 * scale_factor, 0.26 * scale_factor);
    pop();

    // Diagonal structural tension truss
    push();
    translate(len * 0.35, 0.25 * scale_factor, 0);
    rotateZ(-0.45);
    fill(0.28, 0.31, 0.36);
    box(len * 0.7, 0.05 * scale_factor, 0.06 * scale_factor);
    pop();

    pop();
  }

  // 4. Crown Communications Rig and Mast Antennae
  push();
  translate(0, (1.25 - pylon_height) * scale_factor - 0.2 * scale_factor, 0);
  fill(0.18, 0.19, 0.22);
  box(0.70 * scale_factor, 0.12 * scale_factor, 0.70 * scale_factor);

  // Mast needle
  translate(0, -0.4 * scale_factor, 0);
  fill(0.78, 0.80, 0.84);
  box(0.06 * scale_factor, 0.8 * scale_factor, 0.06 * scale_factor);

  // Mast beacon
  translate(0, -0.42 * scale_factor, 0);
  fill(1.0, 0.33, 0.0);
  box(0.14 * scale_factor, 0.14 * scale_factor, 0.14 * scale_factor);
  pop();

  pop();
}
