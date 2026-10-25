// Liminal Void Corridor 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Infinite hypnagogic brutalist corridor with receding architectural concrete portals, recessed floor lighting troughs, and floating monolithic plinths.
param("portal_count", 14.0, 6.0, 28.0);
param("portal_depth", 0.65, 0.2, 1.5);
param("camera_speed", 0.8, 0.1, 2.5);
param("tunnel_width", 2.2, 1.0, 4.0);
param("fog_density", 0.85, 0.2, 2.0);

function draw(t) {
  push();

  const N = floor(portal_count);
  const time = t * camera_speed;
  const loopZ = portal_depth * N;
  const camZ = (time * 1.5) % loopZ;

  // Receding perspective corridor portals
  for (let i = 0; i < N; i++) {
    // Portal position along Z axis relative to virtual camera
    let pz = (i * portal_depth - camZ);
    if (pz < -portal_depth) {
      pz += loopZ;
    }

    const depthRatio = max(0.01, min(1.0, 1.0 - (pz / loopZ)));

    push();
    translate(0, 0, -pz);

    // Subtle perspective sway
    rotateZ(sin(pz * 0.4 + t * 0.5) * 0.03);

    // 1. Concrete portal frame (Top lintel, Left wall, Right wall, Floor slab)
    const w = tunnel_width;
    const h = 2.4;
    const thick = 0.18;

    // Fog atmospheric color fade towards midnight ground #151930
    const colConcrete = [
      0.082 + 0.12 * depthRatio,
      0.098 + 0.14 * depthRatio,
      0.188 + 0.22 * depthRatio
    ];

    fill(colConcrete[0], colConcrete[1], colConcrete[2]);

    // Top lintel
    push();
    translate(0, h * 0.5, 0);
    box(w + thick * 2, thick, portal_depth * 0.85);
    pop();

    // Bottom floor slab
    push();
    translate(0, -h * 0.5, 0);
    box(w + thick * 2, thick, portal_depth * 0.85);
    pop();

    // Left wall pillar
    push();
    translate(-w * 0.5 - thick * 0.5, 0, 0);
    box(thick, h, portal_depth * 0.85);
    pop();

    // Right wall pillar
    push();
    translate(w * 0.5 + thick * 0.5, 0, 0);
    box(thick, h, portal_depth * 0.85);
    pop();

    // 2. Recessed floor & ceiling luminescent light strips
    if (i % 2 === 0) {
      // Warm Ember floor strip
      fill(1.0, 0.42, 0.21);
      push();
      translate(0, -h * 0.48, 0);
      box(0.12, 0.04, portal_depth * 0.7);
      pop();

      // Signal cyan ceiling strip
      fill(0.655, 0.694, 1.0);
      push();
      translate(0, h * 0.48, 0);
      box(0.12, 0.04, portal_depth * 0.7);
      pop();
    }

    // 3. Floating monolithic plinth in deep perspective
    if (i % 4 === 0) {
      push();
      translate(sin(i * 1.5) * 0.4, 0.1 * sin(t * 2.0 + i), 0);
      rotateY(t * 0.6 + i);
      rotateX(0.3);
      fill(0.306, 0.949, 0.722); // Emerald monolithic plinth
      box(0.35, 0.7, 0.35);
      pop();
    }

    pop();
  }

  pop();
}
