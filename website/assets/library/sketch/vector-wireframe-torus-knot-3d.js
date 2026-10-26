// Vector Wireframe Torus Knot 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Orthographic vector wireframe trefoil torus knot with segmented glowing green rib cages and parametric twists.
param("rib_count", 48.0, 16.0, 96.0);
param("rib_radius", 0.22, 0.08, 0.45);
param("spin_rate", 0.5, 0.1, 2.0);
param("knot_scale", 0.95, 0.4, 1.8);
param("wire_thickness", 0.022, 0.008, 0.05);

function draw(t) {
  push();
  const time = t * spin_rate;

  rotateY(time * 0.75);
  rotateX(0.65 + sin(time * 0.4) * 0.25);
  rotateZ(time * 0.25);

  const N = floor(rib_count);
  const p = 2.0;
  const q = 3.0;
  const R = 0.85 * knot_scale;
  const r = 0.38 * knot_scale;
  const ribR = rib_radius * knot_scale;

  function knotPoint(phi) {
    const rCurrent = R + r * cos(q * phi);
    return [
      rCurrent * cos(p * phi),
      rCurrent * sin(p * phi),
      -sin(q * phi) * r * 1.5
    ];
  }

  const spine = [];
  for (let i = 0; i < N; i++) {
    const phi = (i / N) * 6.2831853;
    spine.push(knotPoint(phi));
  }

  fill(0.0, 1.0, 0.4);
  for (let i = 0; i < N; i++) {
    const p1 = spine[i];
    const p2 = spine[(i + 1) % N];
    tube(p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], wire_thickness * 1.4, 6);
  }

  const ribSegments = 16;
  for (let i = 0; i < N; i++) {
    const pCenter = spine[i];
    const pNext = spine[(i + 1) % N];

    const tx = pNext[0] - pCenter[0];
    const ty = pNext[1] - pCenter[1];
    const tz = pNext[2] - pCenter[2];
    const tLen = sqrt(tx * tx + ty * ty + tz * tz) || 1.0;

    let nx = -ty / tLen;
    let ny = tx / tLen;
    let nz = 0;
    if (abs(nx) < 0.001 && abs(ny) < 0.001) {
      nx = 1.0; ny = 0; nz = 0;
    }

    const bx = ty * nz - tz * ny;
    const by = tz * nx - tx * nz;
    const bz = tx * ny - ty * nx;

    if (i % 2 === 0) {
      fill(0.502, 1.0, 0.698);
    } else {
      fill(0.0, 0.56, 0.22);
    }

    for (let s = 0; s < ribSegments; s++) {
      const th1 = (s / ribSegments) * 6.2831853;
      const th2 = ((s + 1) / ribSegments) * 6.2831853;

      const x1 = pCenter[0] + (cos(th1) * nx + sin(th1) * bx) * ribR;
      const y1 = pCenter[1] + (cos(th1) * ny + sin(th1) * by) * ribR;
      const z1 = pCenter[2] + (cos(th1) * nz + sin(th1) * bz) * ribR;

      const x2 = pCenter[0] + (cos(th2) * nx + sin(th2) * bx) * ribR;
      const y2 = pCenter[1] + (cos(th2) * ny + sin(th2) * by) * ribR;
      const z2 = pCenter[2] + (cos(th2) * nz + sin(th2) * bz) * ribR;

      tube(x1, y1, z1, x2, y2, z2, wire_thickness * 0.7, 4);
    }

    if (i % 4 === 0) {
      push();
      translate(pCenter[0] + nx * ribR, pCenter[1] + ny * ribR, pCenter[2] + nz * ribR);
      fill(0.502, 1.0, 0.698);
      sphere(0.038, 6, 6);
      pop();
    }
  }

  pop();
}
