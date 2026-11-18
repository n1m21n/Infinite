// Sashimono Kumiko Asanoha Screen 3D - Infinite Library (Sketch 3D)
param("cols", 4, 2, 6);
param("rows", 4, 2, 6);
param("rib_depth", 0.14, 0.06, 0.28);
param("sway", 0.18, 0.0, 0.5);

function draw(t) {
  rotateX(0.18 + 0.06 * sin(t * 0.4));
  rotateY(0.32 * sin(t * sway + 0.5));

  const nC = floor(cols);
  const nR = floor(rows);
  const cell = 0.56;
  const spanW = nC * cell;
  const spanH = nR * cell;

  // Backlit shoji rice-paper screen plane (#F5F2EB)
  fill(0.95, 0.93, 0.89);
  push();
  translate(0, 0, -0.09);
  box(spanW + 0.18, spanH + 0.18, 0.03);
  pop();

  // Deep cedar outer frame (#1D1712)
  fill(0.14, 0.11, 0.08);
  const ft = 0.09;
  const fd = rib_depth + 0.10;
  push(); translate(0, spanH * 0.5 + ft * 0.5, 0); box(spanW + ft * 2.0, ft, fd); pop();
  push(); translate(0, -spanH * 0.5 - ft * 0.5, 0); box(spanW + ft * 2.0, ft, fd); pop();
  push(); translate(-spanW * 0.5 - ft * 0.5, 0, 0); box(ft, spanH, fd); pop();
  push(); translate(spanW * 0.5 + ft * 0.5, 0, 0); box(ft, spanH, fd); pop();

  // Primary vertical & horizontal hinoki cypress kumiko grid (#C79E74)
  fill(0.78, 0.62, 0.45);
  for (let i = 0; i <= nC; i++) {
    const x = -spanW * 0.5 + i * cell;
    push();
    translate(x, 0, 0);
    box(0.032, spanH, rib_depth);
    pop();
  }
  for (let j = 0; j <= nR; j++) {
    const y = -spanH * 0.5 + j * cell;
    push();
    translate(0, y, 0);
    box(spanW, 0.032, rib_depth);
    pop();
  }

  // Diagonal Asanoha (hemp-leaf) diamond & cross-star infill ribs
  for (let ix = 0; ix < nC; ix++) {
    for (let iy = 0; iy < nR; iy++) {
      const cx = -spanW * 0.5 + (ix + 0.5) * cell;
      const cy = -spanH * 0.5 + (iy + 0.5) * cell;
      const diagLen = cell * 1.36;

      fill(0.82, 0.66, 0.49);
      push();
      translate(cx, cy, 0.01);
      rotateZ(0.785398);
      box(diagLen, 0.022, rib_depth * 0.85);
      pop();

      push();
      translate(cx, cy, 0.01);
      rotateZ(-0.785398);
      box(diagLen, 0.022, rib_depth * 0.85);
      pop();

      // Inner walnut joinery diamond boss (#8B5A2B)
      fill(0.54, 0.35, 0.17);
      push();
      translate(cx, cy, 0.02);
      rotateZ(0.785398);
      box(cell * 0.36, cell * 0.36, rib_depth * 0.92);
      pop();
    }
  }
}