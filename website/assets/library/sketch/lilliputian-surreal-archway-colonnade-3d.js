// Lilliputian Surreal Archway Colonnade 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Vast quiet architectural colonnade with towering slate pillars framing tiny floating terracotta beacon orbs.
param("corridor_length", 5.0, 3.0, 8.0);
param("arch_spacing", 0.55, 0.25, 1.0);
param("rotation_speed", 0.35, 0.1, 1.0);
param("pillar_height", 1.35, 0.6, 2.2);
param("orb_float", 0.2, 0.0, 0.6);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.38 + sin(time * 0.2) * 0.05);
  rotateY(0.45 + cos(time * 0.25) * 0.08);

  const count = floor(corridor_length);
  const spacing = arch_spacing;

  // Ground plinth corridor slab
  fill(0.102, 0.165, 0.227); // Slate indigo
  push();
  translate(0, -pillar_height * 0.52, 0);
  box(1.6, 0.08, count * spacing * 1.4);
  pop();

  // Colonnade arches
  for (let i = 0; i < count; i++) {
    const z = (i - count * 0.5) * spacing;
    push();
    translate(0, 0, z);

    // Left pillar
    fill(0.102, 0.165, 0.227);
    push();
    translate(-0.55, 0, 0);
    box(0.1, pillar_height, 0.1);
    pop();

    // Right pillar
    push();
    translate(0.55, 0, 0);
    box(0.1, pillar_height, 0.1);
    pop();

    // Archway lintel beam in warm bone ivory
    fill(0.910, 0.847, 0.784);
    push();
    translate(0, pillar_height * 0.5, 0);
    box(1.3, 0.1, 0.12);
    pop();

    // Tiny floating terracotta sentinel beacon orb
    fill(0.722, 0.290, 0.224);
    push();
    const yOff = sin(time * 1.2 + i * 0.8) * orb_float;
    translate(0, -0.1 + yOff, 0);
    sphere(0.045, 16);

    // Tiny pedestal
    fill(0.910, 0.847, 0.784);
    translate(0, -pillar_height * 0.45, 0);
    cylinder(0.03, 0.12, 12);
    pop();

    pop();
  }

  pop();
}
