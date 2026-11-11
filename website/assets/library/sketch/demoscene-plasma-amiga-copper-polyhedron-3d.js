// Demoscene Plasma Amiga Copper Polyhedron 3D - Infinite Library (Sketch 3D)
param("rot_speed", 0.65, 0.1, 2.0);
param("strut_thickness", 0.026, 0.01, 0.06);
param("plasma_warp", 1.10, 0.3, 2.5);
param("star_radius", 1.18, 0.6, 1.8);

function clamp(x, a, b) {
  return Math.max(a, Math.min(b, x));
}

function draw(t) {
  push();
  const time = t * rot_speed;

  rotateY(time * 0.8);
  rotateX(time * 0.6 + 0.4);
  rotateZ(sin(time * 0.45) * 0.25);

  const phi = (1.0 + sqrt(5.0)) * 0.5;
  const invNorm = 1.0 / sqrt(1.0 + phi * phi);
  const a = 1.0 * invNorm * star_radius * 0.62;
  const b = phi * invNorm * star_radius * 0.62;

  // 12 base icosahedral vertices
  const V = [
    [-a,  b,  0], [ a,  b,  0], [-a, -b,  0], [ a, -b,  0],
    [ 0, -a,  b], [ 0,  a,  b], [ 0, -a, -b], [ 0,  a, -b],
    [ b,  0, -a], [ b,  0,  a], [-b,  0, -a], [-b,  0,  a]
  ];

  // 20 Icosahedral triangular faces
  const F = [
    [0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11],
    [1, 5, 9], [5, 11, 4], [11, 10, 2], [10, 7, 6], [7, 1, 8],
    [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9],
    [4, 9, 5], [2, 4, 11], [6, 2, 10], [8, 6, 7], [9, 8, 1]
  ];

  // Draw stellation apex pyramids on each face
  for (let i = 0; i < F.length; i++) {
    const f = F[i];
    const v0 = V[f[0]];
    const v1 = V[f[1]];
    const v2 = V[f[2]];

    // Face centroid normal
    const cx = (v0[0] + v1[0] + v2[0]) / 3.0;
    const cy = (v0[1] + v1[1] + v2[1]) / 3.0;
    const cz = (v0[2] + v1[2] + v2[2]) / 3.0;
    const len = sqrt(cx * cx + cy * cy + cz * cz);

    const spikeH = star_radius * (0.95 + 0.18 * sin(time * 2.2 * plasma_warp + i * 0.35));
    const apexX = (cx / len) * spikeH;
    const apexY = (cy / len) * spikeH;
    const apexZ = (cz / len) * spikeH;

    // Alternating copper flame and electric cyan coloring
    if (i % 2 === 0) {
      fill(1.0, 0.25 + 0.20 * sin(i * 0.5 + time), 0.0);
    } else {
      fill(0.0, 0.80, 1.0);
    }

    // Three facets of the stellation pyramid
    beginShape();
    vertex(v0[0], v0[1], v0[2]);
    vertex(v1[0], v1[1], v1[2]);
    vertex(apexX, apexY, apexZ);
    endShape();

    beginShape();
    vertex(v1[0], v1[1], v1[2]);
    vertex(v2[0], v2[1], v2[2]);
    vertex(apexX, apexY, apexZ);
    endShape();

    beginShape();
    vertex(v2[0], v2[1], v2[2]);
    vertex(v0[0], v0[1], v0[2]);
    vertex(apexX, apexY, apexZ);
    endShape();
  }

  // Dual equatorial gimbal plasma rings
  push();
  rotateX(HALF_PI);
  fill(1.0, 0.35 + 0.3 * sin(time * 3.0), 0.0);
  torus(star_radius * 1.35, 0.024, 24);
  pop();

  push();
  rotateY(HALF_PI);
  fill(0.0, 0.90, 1.0);
  torus(star_radius * 1.35, 0.024, 24);
  pop();

  pop();
}
