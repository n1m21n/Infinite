// Brutalist Coffered Oculus 3D - Infinite Library (Sketch 3D)
param("ribs", 16, 8, 24);
param("tiers", 4, 2, 6);
param("radius", 1.35, 0.9, 1.9);
param("spin", 0.12, 0.0, 0.5);

function draw(t) {
  const nRibs = floor(ribs);
  const nTiers = floor(tiers);
  rotateY(t * spin + 0.4);
  rotateX(0.42);

  // Brutalist plinth & perimeter ring (#232A30)
  push();
  translate(0, -0.82, 0);
  fill(0.137, 0.165, 0.188);
  cylinder(radius * 1.08, 0.12);
  pop();

  // Radial concrete structural vault ribs (#7A8284)
  for (let i = 0; i < nRibs; i++) {
    const a = (i * TAU) / nRibs;
    push();
    rotateY(a);
    for (let k = 0; k < nTiers; k++) {
      const u = k / nTiers;
      const r = radius * (0.95 - u * 0.62);
      const y = -0.52 + u * 1.15;
      const tilt = 0.25 + u * 0.72;
      push();
      translate(r, y, 0);
      rotateZ(tilt);
      fill(k % 2 === 0 ? 0.478 : 0.365, k % 2 === 0 ? 0.510 : 0.400, k % 2 === 0 ? 0.518 : 0.415);
      box(0.11, 0.46, 0.16);
      pop();
    }
    pop();
  }

  // Stepped horizontal coffer rings & recessed shadow bays
  for (let k = 0; k < nTiers; k++) {
    const u = k / nTiers;
    const r = radius * (0.92 - u * 0.58);
    const y = -0.35 + u * 0.95;
    for (let i = 0; i < nRibs; i++) {
      const a = ((i + 0.5) * TAU) / nRibs;
      push();
      rotateY(a);
      translate(r, y, 0);
      fill(0.20, 0.23, 0.25);
      box(0.08, 0.08, (TAU * r / nRibs) * 0.85);
      pop();
    }
  }

  // Warm sodium-amber oculus light collar (#D9A84E)
  const oculusR = radius * 0.28;
  for (let i = 0; i < 12; i++) {
    const a = (i * TAU) / 12.0;
    push();
    rotateY(a);
    translate(oculusR, 0.72, 0);
    fill(0.851, 0.659, 0.306);
    box(0.06, 0.08, (TAU * oculusR / 12.0) * 0.92);
    pop();
  }
}
