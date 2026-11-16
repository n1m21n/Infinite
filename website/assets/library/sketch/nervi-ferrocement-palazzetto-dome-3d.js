// Nervi Ferrocement Palazzetto Dome 3D - Infinite Library (Sketch 3D)
param("rib_count", 18, 12, 24);
param("dome_rise", 0.85, 0.45, 1.35);
param("tilt", 0.52, 0.2, 0.9);
param("orbit", 0.18, 0.0, 0.6);

function draw(t) {
  rotateX(tilt);
  rotateY(t * orbit + 0.35);

  const n = floor(rib_count);
  const rOuter = 1.65;
  const rInner = 0.28;
  const rings = 6;

  // Dark cast-iron plinth ring (#1F2124)
  fill(0.122, 0.129, 0.141);
  push();
  translate(0, -0.18, 0);
  torus(rOuter * 1.04, 0.055, 24, 10);
  pop();

  // Perimeter Y-buttress piers (#4B4842)
  fill(0.294, 0.282, 0.259);
  for (let i = 0; i < n; i++) {
    const a = (i / n) * TAU;
    push();
    rotateY(a);
    translate(rOuter * 1.02, -0.35, 0);
    rotateZ(-0.28);
    box(0.06, 0.48, 0.08);
    pop();
  }

  // Concentric ferrocement compression rings (#D8D2C7)
  fill(0.847, 0.824, 0.780);
  for (let k = 0; k < rings; k++) {
    const u = k / (rings - 1);
    const rr = rOuter * (1.0 - u * 0.82);
    const yy = dome_rise * sin(u * 1.48);
    push();
    translate(0, yy, 0);
    rotateX(1.5708);
    torus(rr, 0.026, 28, 8);
    pop();
  }

  // Intersecting clockwise & counter-clockwise diagrid spiral ribs
  for (let i = 0; i < n; i++) {
    const a0 = (i / n) * TAU;
    for (let k = 0; k < rings - 1; k++) {
      const u0 = k / (rings - 1);
      const u1 = (k + 1) / (rings - 1);
      const r0 = rOuter * (1.0 - u0 * 0.82);
      const r1 = rOuter * (1.0 - u1 * 0.82);
      const y0 = dome_rise * sin(u0 * 1.48);
      const y1 = dome_rise * sin(u1 * 1.48);
      const rm = (r0 + r1) * 0.5;
      const ym = (y0 + y1) * 0.5;
      const segLen = sqrt((r0 - r1) * (r0 - r1) + (y1 - y0) * (y1 - y0)) * 1.18;
      const pitch = atan2(y1 - y0, r0 - r1);

      // CW spiral rib
      fill(0.847, 0.824, 0.780);
      push();
      rotateY(a0 + u0 * 0.52);
      translate(rm, ym, 0);
      rotateZ(pitch);
      rotateY(0.42);
      box(segLen, 0.042, 0.036);
      pop();

      // CCW spiral rib
      fill(0.765, 0.737, 0.686);
      push();
      rotateY(a0 - u0 * 0.52);
      translate(rm, ym, 0);
      rotateZ(pitch);
      rotateY(-0.42);
      box(segLen, 0.042, 0.036);
      pop();
    }
  }

  // Warm brass oculus lantern ring (#C49A5B)
  fill(0.769, 0.604, 0.357);
  push();
  translate(0, dome_rise * sin(1.48) + 0.03, 0);
  rotateX(1.5708);
  torus(rInner, 0.038, 24, 10);
  pop();
}
