// Vector Wireframe Topological Hypercube 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Four-dimensional tesseract hypercube projection rotating in 3D vector space with inner and outer concentric vertex lattices, phosphor green connection struts, and bright beam junctions.
param("tesseract_scale", 1.15, 0.5, 2.0);
param("rotation_speed", 0.42, 0.1, 1.2);
param("strut_thickness", 0.05, 0.02, 0.12);
param("inner_ratio", 0.52, 0.3, 0.8);

function draw(t) {
  push();
  const time = t * rotation_speed;

  rotateX(0.42 + sin(time * 0.18) * 0.05);
  rotateY(time * 0.32 + 0.7);

  // Deep CRT vector phosphor plinth (#06120a)
  push();
  translate(0, 1.15, 0);
  fill(0.024, 0.071, 0.039);
  box(2.3 * tesseract_scale, 0.14, 2.3 * tesseract_scale);
  pop();

  const outerSize = 0.95 * tesseract_scale;
  const innerSize = outerSize * inner_ratio;

  // 1. Inner rotating tesseract core (phosphor green with bright white vertices)
  push();
  rotateY(-time * 0.5);
  rotateX(time * 0.35);

  // Inner cube vertices
  for (let x = -1; x <= 1; x += 2) {
    for (let y = -1; y <= 1; y += 2) {
      for (let z = -1; z <= 1; z += 2) {
        push();
        translate(x * innerSize, y * innerSize, z * innerSize);
        fill(1.0, 1.0, 1.0); // Bright beam vertex
        box(0.12 * tesseract_scale, 0.12 * tesseract_scale, 0.12 * tesseract_scale);
        pop();
      }
    }
  }

  // Inner cube struts
  fill(0.0, 1.0, 0.416); // Phosphor green
  for (let x = -1; x <= 1; x += 2) {
    for (let y = -1; y <= 1; y += 2) {
      push();
      translate(x * innerSize, y * innerSize, 0);
      box(strut_thickness * 1.2, strut_thickness * 1.2, innerSize * 2.0);
      pop();
      push();
      translate(x * innerSize, 0, y * innerSize);
      box(strut_thickness * 1.2, innerSize * 2.0, strut_thickness * 1.2);
      pop();
      push();
      translate(0, x * innerSize, y * innerSize);
      box(innerSize * 2.0, strut_thickness * 1.2, strut_thickness * 1.2);
      pop();
    }
  }
  pop();

  // 2. Outer tesseract boundary frame
  push();
  rotateY(time * 0.25);

  // Outer cube struts
  fill(0.0, 0.85, 0.35);
  for (let x = -1; x <= 1; x += 2) {
    for (let y = -1; y <= 1; y += 2) {
      push();
      translate(x * outerSize, y * outerSize, 0);
      box(strut_thickness, strut_thickness, outerSize * 2.0);
      pop();
      push();
      translate(x * outerSize, 0, y * outerSize);
      box(strut_thickness, outerSize * 2.0, strut_thickness);
      pop();
      push();
      translate(0, x * outerSize, y * outerSize);
      box(outerSize * 2.0, strut_thickness, strut_thickness);
      pop();
    }
  }

  // Outer corner vertex nodes
  for (let x = -1; x <= 1; x += 2) {
    for (let y = -1; y <= 1; y += 2) {
      for (let z = -1; z <= 1; z += 2) {
        push();
        translate(x * outerSize, y * outerSize, z * outerSize);
        fill(0.7, 1.0, 0.85);
        box(0.14 * tesseract_scale, 0.14 * tesseract_scale, 0.14 * tesseract_scale);
        pop();
      }
    }
  }

  // 3. Diagonal 4D projection interconnects (from inner to outer vertices)
  fill(0.0, 1.0, 0.416);
  for (let x = -1; x <= 1; x += 2) {
    for (let y = -1; y <= 1; y += 2) {
      for (let z = -1; z <= 1; z += 2) {
        const midX = (x * outerSize + x * innerSize) * 0.5;
        const midY = (y * outerSize + y * innerSize) * 0.5;
        const midZ = (z * outerSize + z * innerSize) * 0.5;
        push();
        translate(midX, midY, midZ);
        box(strut_thickness * 0.9, strut_thickness * 0.9, (outerSize - innerSize) * 1.4);
        pop();
      }
    }
  }

  pop();
  pop();
}
