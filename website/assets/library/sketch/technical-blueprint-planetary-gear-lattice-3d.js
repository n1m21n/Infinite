// Technical Blueprint Planetary Gear Lattice 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Axonometric epicyclic planetary gear train assembly with rotating sun gear, orbiting planet pinions, precision annular ring teeth, and mechanical drafting reference plinths.
param("planets", 4.0, 3.0, 8.0);
param("gear_speed", 0.45, 0.1, 1.5);
param("scale", 1.15, 0.5, 2.0);
param("tooth_depth", 0.08, 0.03, 0.18);

function draw(t) {
  push();
  const time = t * gear_speed;

  rotateX(0.55 + sin(time * 0.15) * 0.04);
  rotateY(time * 0.30 + 0.5);

  // Precision blueprint drafting base plinth (#00162b)
  push();
  translate(0, 1.25 * scale, 0);
  fill(0.008, 0.086, 0.169);
  box(2.4 * scale, 0.12, 2.4 * scale);
  pop();

  // 1. Central sun gear (rotating clockwise)
  push();
  rotateY(time * 1.2);

  // Sun gear core body
  fill(1.0, 0.72, 0.01); // Precision brass accent
  box(0.55 * scale, 0.22 * scale, 0.55 * scale);

  // Central axle drive arbor
  fill(1.0, 1.0, 1.0);
  box(0.18 * scale, 1.8 * scale, 0.18 * scale);

  // Sun gear radial teeth
  fill(0.0, 0.83, 1.0); // Drafting cyan
  const sunTeeth = 8;
  for (let i = 0; i < sunTeeth; i++) {
    const a = (i / sunTeeth) * Math.PI * 2.0;
    push();
    rotateY(a);
    translate(0.32 * scale, 0, 0);
    box(tooth_depth * 1.5 * scale, 0.20 * scale, 0.08 * scale);
    pop();
  }
  pop();

  // 2. Orbiting planetary pinions
  const nPlanets = floor(planets);
  const orbitRad = 0.72 * scale;
  for (let p = 0; p < nPlanets; p++) {
    const orbitAngle = (p / nPlanets) * Math.PI * 2.0 + time * 0.45;
    push();
    translate(cos(orbitAngle) * orbitRad, 0, sin(orbitAngle) * orbitRad);
    rotateY(-time * 1.4); // Counter-rotation of planet pinion

    // Planet pinion hub
    fill(0.0, 0.83, 1.0); // Drafting cyan
    box(0.38 * scale, 0.18 * scale, 0.38 * scale);

    // Planet pinion spindle
    fill(1.0, 1.0, 1.0);
    box(0.10 * scale, 0.45 * scale, 0.10 * scale);

    // Planet teeth
    fill(1.0, 0.72, 0.01);
    const planetTeeth = 6;
    for (let j = 0; j < planetTeeth; j++) {
      const theta = (j / planetTeeth) * Math.PI * 2.0;
      push();
      rotateY(theta);
      translate(0.22 * scale, 0, 0);
      box(tooth_depth * 1.2 * scale, 0.16 * scale, 0.06 * scale);
      pop();
    }
    pop();
  }

  // 3. Outer annular ring gear casing
  push();
  rotateY(-time * 0.15);
  const ringSegments = 24;
  const ringRad = 1.05 * scale;
  for (let s = 0; s < ringSegments; s++) {
    const phi = (s / ringSegments) * Math.PI * 2.0;
    push();
    translate(cos(phi) * ringRad, 0, sin(phi) * ringRad);
    rotateY(-phi);
    if (s % 4 === 0) {
      fill(1.0, 1.0, 1.0); // White index markers
    } else {
      fill(0.0, 0.83, 1.0); // Drafting cyan ring
    }
    box(0.06 * scale, 0.26 * scale, (2.0 * Math.PI * ringRad / ringSegments) * 0.95);
    pop();
  }
  pop();

  pop();
}
