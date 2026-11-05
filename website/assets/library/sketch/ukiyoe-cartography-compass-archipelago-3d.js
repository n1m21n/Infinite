// Ukiyo-e Cartography Compass Archipelago 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Procedural 3D nautical compass rose and floating archipelago islands rendered with indigo ink contours, seafoam wave ridges, and cinnabar navigational markers.
param("compass_radius", 1.0, 0.5, 1.8);
param("island_height", 0.9, 0.4, 1.8);
param("rotation_speed", 0.32, 0.05, 1.0);
param("wave_motion", 0.45, 0.1, 1.0);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.52 + sin(time * 0.22) * 0.04);
  rotateY(time * 0.38 + 0.6);

  // Outer ocean basin disc in deep indigo ink
  fill(0.114, 0.208, 0.341);
  push();
  translate(0, -0.68, 0);
  cylinder(1.7 * compass_radius, 0.08);

  // Seafoam navigation ring
  translate(0, 0.06, 0);
  fill(0.659, 0.855, 0.863);
  cylinder(1.48 * compass_radius, 0.05);

  // Inner indigo dial
  translate(0, 0.04, 0);
  fill(0.114, 0.208, 0.341);
  cylinder(1.28 * compass_radius, 0.04);
  pop();

  // 8-Point nautical compass rose star
  push();
  translate(0, -0.52, 0);
  for (let p = 0; p < 8; p++) {
    const pAng = (p * 6.2831853) / 8;
    const isCardinal = (p % 2 === 0);
    const pLen = (isCardinal ? 0.95 : 0.65) * compass_radius;

    push();
    rotateY(pAng);
    translate(pLen * 0.5, 0, 0);

    // Cardinal rays in cinnabar red, inter-cardinal in rice paper cream
    if (isCardinal) {
      fill(0.902, 0.224, 0.275); // Cinnabar Red
      box(pLen, 0.06, 0.08);
    } else {
      fill(0.945, 0.980, 0.933); // Rice paper cream
      box(pLen, 0.04, 0.06);
    }
    pop();
  }

  // Central compass pivot dome
  fill(0.902, 0.224, 0.275);
  sphere(0.14 * compass_radius);
  translate(0, 0.12, 0);
  fill(0.945, 0.980, 0.933);
  cylinder(0.06, 0.18);
  pop();

  // Floating archipelago islands array
  const islandCount = 5;
  for (let i = 0; i < islandCount; i++) {
    const iAng = (i * 6.2831853) / islandCount + 0.3;
    const iDist = 1.25 * compass_radius;
    const ix = cos(iAng) * iDist;
    const iz = sin(iAng) * iDist;
    const iy = -0.42 + sin(time * 1.5 + i * 1.2) * (0.06 * wave_motion);

    push();
    translate(ix, iy, iz);

    // Stepped island tiers
    fill(0.114, 0.208, 0.341); // Indigo base
    cylinder(0.24, 0.16 * island_height);

    translate(0, 0.12 * island_height, 0);
    fill(0.659, 0.855, 0.863); // Seafoam mid tier
    cylinder(0.17, 0.12 * island_height);

    translate(0, 0.10 * island_height, 0);
    fill(0.902, 0.224, 0.275); // Cinnabar summit peak
    cone(0.12, 0.16 * island_height);

    pop();
  }

  pop();
}
