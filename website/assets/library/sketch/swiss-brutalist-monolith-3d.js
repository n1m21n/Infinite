// Swiss Brutalist Monolith 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Modular constructivist concrete monolith with cantilevered architectural slabs, tension beam matrices, and recessed negative-space light channels.
param("floors", 9.0, 3.0, 16.0);
param("cantilever", 1.35, 0.4, 2.8);
param("beam_spread", 1.2, 0.5, 2.5);
param("spin", 0.5, 0.0, 2.5);
param("elevation", 0.45, -1.0, 1.0);

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(0.55 + sin(t * 0.25) * 0.1);
  rotateZ(sin(t * 0.18) * 0.08);

  const N = floor(floors);
  const totalH = 2.4;
  const floorH = totalH / N;

  // Heavy central concrete tower core
  fill(0.18, 0.21, 0.32); // Concrete deep slate
  box(0.5, totalH, 0.5);

  // Cantilevered interlocking architectural slabs
  for (let i = 0; i < N; i++) {
    const u = i / N;
    const y = (u - 0.5) * totalH;

    // Asymmetric Swiss constructivist shift per floor
    const angle = i * 1.570796 + t * 0.1;
    const shiftMag = sin(i * 1.8 + t * 0.4) * cantilever * 0.45;
    const sx = cos(angle) * shiftMag;
    const sz = sin(angle) * shiftMag;

    push();
    translate(sx, y, sz);

    // Alternating concrete tone
    if (i % 2 === 0) {
      fill(0.655, 0.694, 1.0); // Signal concrete tone
    } else {
      fill(0.28, 0.32, 0.46); // Medium slate
    }

    const slabW = 0.9 + 0.35 * sin(i * 2.3);
    const slabD = 0.9 + 0.35 * cos(i * 2.3);
    box(slabW, floorH * 0.72, slabD);

    // Accent ember structural tension beam
    if (i % 3 === 0) {
      fill(1.0, 0.42, 0.21); // Ember accent
      push();
      translate(slabW * 0.45, 0, 0);
      box(0.08, floorH * 0.85, 0.08);
      pop();
    }
    pop();
  }

  // Tension rod cables connecting floor cantilevers
  fill(0.306, 0.949, 0.722); // Emerald tension lines
  for (let k = 0; k < 4; k++) {
    const a = (k * PI * 0.5) + 0.35;
    const rx = cos(a) * beam_spread * 0.75;
    const rz = sin(a) * beam_spread * 0.75;

    tube(rx, -totalH * 0.5, rz, rx * 0.5, totalH * 0.5, rz * 0.5, 0.015, 6);
  }

  // Ground plinth base
  fill(0.12, 0.14, 0.24);
  push();
  translate(0, -totalH * 0.55, 0);
  box(2.2, 0.15, 2.2);
  pop();

  pop();
}
