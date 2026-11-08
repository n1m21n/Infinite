// Lilliputian Surreal Orbital Observatory 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Monumental armillary sphere observatory rising from a stepped travertine plinth, featuring colossal intersecting equatorial rings, towering observation spires, and suspended micro-cube constellations.
param("spin", 0.36, 0.1, 1.2);
param("scale_factor", 1.15, 0.5, 2.0);
param("ring_radius", 1.1, 0.6, 1.8);
param("spire_height", 2.0, 1.0, 3.2);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.52 + sin(time * 0.14) * 0.04);
  rotateY(time * 0.26 + 0.6);

  // 1. Stepped travertine foundation base
  push();
  translate(0, 1.25 * scale_factor, 0);
  fill(0.839, 0.800, 0.737); // Travertine base
  box(2.6 * scale_factor, 0.12, 2.6 * scale_factor);

  translate(0, -0.09, 0);
  fill(0.769, 0.412, 0.333); // Terracotta sub-tier
  box(1.8 * scale_factor, 0.06, 1.8 * scale_factor);
  pop();

  // 2. Colossal vertical observation spire
  push();
  translate(-0.45 * scale_factor, (1.25 - spire_height * 0.5) * scale_factor, -0.3 * scale_factor);
  fill(0.482, 0.549, 0.494); // Dusty sage monolithic needle
  box(0.18 * scale_factor, spire_height * scale_factor, 0.18 * scale_factor);

  // Crown finial cube
  translate(0, -spire_height * 0.52 * scale_factor, 0);
  fill(0.769, 0.412, 0.333);
  box(0.30 * scale_factor, 0.30 * scale_factor, 0.30 * scale_factor);
  pop();

  // 3. Central armillary core sphere and gimbal axes
  push();
  translate(0.15 * scale_factor, 0, 0.1 * scale_factor);

  // Alabaster core cube-sphere
  fill(0.922, 0.894, 0.839);
  box(0.55 * scale_factor, 0.55 * scale_factor, 0.55 * scale_factor);

  // Intersecting colossal armillary rings
  const rRing = ring_radius * scale_factor;
  const nSegs = 20;

  // Horizontal ring (Terracotta)
  push();
  rotateY(time * 0.6);
  for (let s = 0; s < nSegs; s++) {
    const th = (s / nSegs) * Math.PI * 2.0;
    push();
    translate(cos(th) * rRing, 0, sin(th) * rRing);
    rotateY(-th);
    fill(0.769, 0.412, 0.333);
    box(0.06 * scale_factor, 0.08 * scale_factor, (2.0 * Math.PI * rRing / nSegs) * 0.95);
    pop();
  }
  pop();

  // Inclined meridian ring (Travertine)
  push();
  rotateZ(0.65);
  rotateX(-time * 0.5);
  for (let s = 0; s < nSegs; s++) {
    const th = (s / nSegs) * Math.PI * 2.0;
    push();
    translate(0, cos(th) * rRing, sin(th) * rRing);
    rotateX(-th);
    fill(0.839, 0.800, 0.737);
    box(0.08 * scale_factor, 0.06 * scale_factor, (2.0 * Math.PI * rRing / nSegs) * 0.95);
    pop();
  }
  pop();

  pop();

  // 4. Lilliputian orbiting micro-constellation cubes
  for (let k = 0; k < 6; k++) {
    const ang = (k / 6.0) * Math.PI * 2.0 - time * 0.75;
    const rad = 1.45 * scale_factor;
    const yP = sin(k * 2.1 + time * 1.2) * 0.4 * scale_factor - 0.2 * scale_factor;

    push();
    translate(cos(ang) * rad, yP, sin(ang) * rad);
    rotateY(ang);
    fill(0.482, 0.549, 0.494);
    box(0.10 * scale_factor, 0.10 * scale_factor, 0.10 * scale_factor);
    pop();
  }

  pop();
}
