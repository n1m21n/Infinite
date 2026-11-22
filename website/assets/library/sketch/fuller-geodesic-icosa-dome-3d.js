// Fuller Geodesic Icosa Dome 3D - Infinite Library (Sketch 3D)
param("dome_radius", 1.55, 1.0, 2.1);
param("strut_thick", 0.032, 0.015, 0.065);
param("hub_size", 0.055, 0.025, 0.10);
param("orbit", 0.28, 0.0, 1.0);

function drawStrut(ax, ay, az, bx, by, bz, thick) {
  const dx = bx - ax;
  const dy = by - ay;
  const dz = bz - az;
  const len = sqrt(dx * dx + dy * dy + dz * dz);
  if (len < 0.001) return;
  const mx = (ax + bx) * 0.5;
  const my = (ay + by) * 0.5;
  const mz = (az + bz) * 0.5;
  const horiz = sqrt(dx * dx + dz * dz);
  const yaw = atan2(dz, dx);
  const pitch = atan2(dy, horiz);
  push();
  translate(mx, my, mz);
  rotateY(-yaw);
  rotateZ(pitch);
  box(len, thick, thick);
  pop();
}

function normPt(x, y, z, R) {
  const l = sqrt(x * x + y * y + z * z);
  return [(x / l) * R, (y / l) * R, (z / l) * R];
}

function draw(t) {
  scale(0.56);
  rotateX(0.30 + 0.04 * sin(t * 0.35));
  rotateY(0.55 + t * orbit * 0.28);
  translate(0, -0.58, 0);

  const R = dome_radius;

  // Deep architectural slate-navy plinth (#181F28)
  fill(0.094, 0.122, 0.157);
  push();
  translate(0, -0.07, 0);
  cylinder(R * 1.18, 0.14);
  pop();

  // Inner glowing amber floor oculus
  fill(0.961, 0.722, 0.180);
  push();
  translate(0, 0.01, 0);
  cylinder(R * 0.36, 0.03);
  pop();

  // Generate 12 icosahedron vertices
  const phi = (1.0 + sqrt(5.0)) * 0.5;
  const rawV = [
    [-1, phi, 0], [1, phi, 0], [-1, -phi, 0], [1, -phi, 0],
    [0, -1, phi], [0, 1, phi], [0, -1, -phi], [0, 1, -phi],
    [phi, 0, -1], [phi, 0, 1], [-phi, 0, -1], [-phi, 0, 1]
  ];
  const V = [];
  for (let i = 0; i < 12; i++) {
    V.push(normPt(rawV[i][0], rawV[i][1], rawV[i][2], R));
  }

  const faces = [
    [0,11,5],[0,5,1],[0,1,7],[0,7,10],[0,10,11],
    [1,5,9],[5,11,4],[11,10,2],[10,7,6],[7,1,8],
    [3,9,4],[3,4,2],[3,2,6],[3,6,8],[3,8,9],
    [4,9,5],[2,4,11],[6,2,10],[8,6,7],[9,8,1]
  ];

  // 2-frequency geodesic subdivision for upper hemisphere
  for (let f = 0; f < faces.length; f++) {
    const a = V[faces[f][0]];
    const b = V[faces[f][1]];
    const c = V[faces[f][2]];
    if (a[1] < -0.25 && b[1] < -0.25 && c[1] < -0.25) continue;

    const ab = normPt(a[0] + b[0], a[1] + b[1], a[2] + b[2], R);
    const bc = normPt(b[0] + c[0], b[1] + c[1], b[2] + c[2], R);
    const ca = normPt(c[0] + a[0], c[1] + a[1], c[2] + a[2], R);

    const subTris = [
      [a, ab, ca],
      [ab, b, bc],
      [ca, bc, c],
      [ab, bc, ca]
    ];

    for (let s = 0; s < 4; s++) {
      const p0 = subTris[s][0];
      const p1 = subTris[s][1];
      const p2 = subTris[s][2];
      if (p0[1] < -0.05 && p1[1] < -0.05 && p2[1] < -0.05) continue;

      const q0 = [p0[0], max(0.0, p0[1]), p0[2]];
      const q1 = [p1[0], max(0.0, p1[1]), p1[2]];
      const q2 = [p2[0], max(0.0, p2[1]), p2[2]];

      // Warm brass struts (#C47D1A)
      fill(0.769, 0.490, 0.102);
      drawStrut(q0[0], q0[1], q0[2], q1[0], q1[1], q1[2], strut_thick);
      drawStrut(q1[0], q1[1], q1[2], q2[0], q2[1], q2[2], strut_thick);
      drawStrut(q2[0], q2[1], q2[2], q0[0], q0[1], q0[2], strut_thick);

      // Gold-ochre hub nodes (#F5B82E)
      fill(0.961, 0.722, 0.180);
      push(); translate(q0[0], q0[1], q0[2]); sphere(hub_size); pop();
      push(); translate(q1[0], q1[1], q1[2]); sphere(hub_size); pop();
      push(); translate(q2[0], q2[1], q2[2]); sphere(hub_size); pop();

      // Translucent warm amber membrane facet tile at centroid
      const cx = (q0[0] + q1[0] + q2[0]) / 3.0;
      const cy = (q0[1] + q1[1] + q2[1]) / 3.0;
      const cz = (q0[2] + q1[2] + q2[2]) / 3.0;
      fill(0.98, 0.82, 0.34, 0.36);
      push();
      translate(cx, cy, cz);
      sphere(strut_thick * 2.1);
      pop();
    }
  }
}
