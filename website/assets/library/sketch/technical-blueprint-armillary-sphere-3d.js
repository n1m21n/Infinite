// Technical Blueprint Armillary Sphere 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Precision axonometric 3D armillary sphere mechanism with nested concentric equatorial rings, polar declination calipers, stepped azimuth pivots, and a central brass terrestrial core.
param("rings", 5.0, 3.0, 10.0);
param("spin", 0.40, 0.1, 1.2);
param("scale", 1.15, 0.5, 2.0);
param("strut_width", 0.045, 0.02, 0.10);

function draw(t) {
  push();
  const time = t * spin;

  rotateX(0.48 + sin(time * 0.18) * 0.05);
  rotateY(time * 0.32 + 0.6);

  // Precision drafting reference plinth (#00162b)
  push();
  translate(0, 1.2 * scale, 0);
  fill(0.008, 0.086, 0.169); // Deep drafting blueprint plinth
  box(2.4 * scale, 0.12, 2.4 * scale);
  pop();

  const ringCount = floor(rings);

  // 1. Concentric rotating armillary meridian and declination rings
  for (let r = 0; r < ringCount; r++) {
    const rad = (0.65 + r * 0.18) * scale;
    const ang = (r / ringCount) * Math.PI;

    push();
    rotateY(time * (0.28 + r * 0.08) + ang);
    rotateX(ang * 0.5);

    const segments = 24;
    for (let s = 0; s < segments; s++) {
      const theta = (s / segments) * Math.PI * 2.0;
      push();
      translate(cos(theta) * rad, 0, sin(theta) * rad);
      rotateY(-theta);

      // Drafting cyan (#00d4ff) or white marker for key ring
      if (r === ringCount - 1) {
        fill(1.0, 1.0, 1.0); // Technical white outer ring
      } else if (r % 2 === 0) {
        fill(0.0, 0.83, 1.0); // Drafting cyan
      } else {
        fill(1.0, 0.72, 0.01); // Precision brass accent
      }
      box(strut_width * scale, strut_width * scale, (2.0 * Math.PI * rad / segments) * 0.95);
      pop();
    }
    pop();
  }

  // 2. Central brass core sphere and gimbal axle
  push();
  rotateY(-time * 0.5);
  rotateZ(time * 0.35);

  // Central brass core cube/sphere
  fill(1.0, 0.72, 0.01); // Precision brass
  box(0.36 * scale, 0.36 * scale, 0.36 * scale);

  // White coordinate indicator pips
  fill(1.0, 1.0, 1.0);
  for (let a = -1; a <= 1; a += 2) {
    push();
    translate(a * 0.22 * scale, 0, 0);
    box(0.10 * scale, 0.10 * scale, 0.10 * scale);
    pop();
    push();
    translate(0, a * 0.22 * scale, 0);
    box(0.10 * scale, 0.10 * scale, 0.10 * scale);
    pop();
    push();
    translate(0, 0, a * 0.22 * scale);
    box(0.10 * scale, 0.10 * scale, 0.10 * scale);
    pop();
  }
  pop();

  // 3. Polar coordinate axis drive shaft
  fill(0.0, 0.83, 1.0);
  box(strut_width * 1.2 * scale, 2.5 * scale, strut_width * 1.2 * scale);

  // Polar end finials
  fill(1.0, 1.0, 1.0);
  for (let s = -1; s <= 1; s += 2) {
    push();
    translate(0, s * 1.25 * scale, 0);
    box(0.14 * scale, 0.14 * scale, 0.14 * scale);
    pop();
  }

  pop();
}
