// Data Landscape Contour Mesh 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Peter Saville inspired procedural 3D topological contour mesh with stepped elevation strata and oceanic cyan depth shading.
param("elevation_scale", 1.0, 0.2, 2.5);
param("strata_layers", 14.0, 6.0, 24.0);
param("rotation_speed", 0.35, 0.05, 1.2);
param("terrain_spread", 1.2, 0.5, 2.0);
param("ridge_steepness", 0.65, 0.1, 1.5);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.72 + sin(time * 0.2) * 0.08);
  rotateY(time * 0.35);

  const layers = floor(strata_layers);
  const spread = terrain_spread;

  for (let i = 0; i < layers; i++) {
    const norm = i / layers;
    const y = (norm - 0.5) * 1.4 * elevation_scale;
    const layerScale = (1.0 - norm * 0.45) * spread;

    push();
    translate(0, y, 0);
    rotateY(norm * 0.4 + sin(time * 0.3 + norm * 2.0) * 0.15);

    if (i < layers * 0.35) {
      fill(0.043, 0.075, 0.169); // Deep navy
    } else if (i < layers * 0.75) {
      fill(0.357, 0.753, 0.745); // Oceanic cyan
    } else {
      fill(0.11, 0.145, 0.255); // Slate indigo
    }

    const w = layerScale * (1.2 + sin(norm * 4.0 + time * 0.5) * 0.25);
    const d = layerScale * (0.9 + cos(norm * 3.5 + time * 0.4) * 0.2);
    box(w, 0.06 * elevation_scale, d);

    if (i % 3 === 0) {
      push();
      translate(w * 0.45, 0.12, d * 0.45);
      fill(1.0, 1.0, 1.0);
      box(0.05, 0.22, 0.05);
      pop();
      push();
      translate(-w * 0.45, 0.12, -d * 0.45);
      fill(0.357, 0.753, 0.745);
      box(0.05, 0.22, 0.05);
      pop();
    }

    pop();
  }

  push();
  translate(0, 0.85 * elevation_scale, 0);
  fill(0.043, 0.075, 0.169);
  sphere(0.09, 16, 16);
  pop();

  pop();
}
