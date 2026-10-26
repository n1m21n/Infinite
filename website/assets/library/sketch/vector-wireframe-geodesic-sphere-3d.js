// Vector Wireframe Geodesic Sphere 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Rotating vector display geodesic icosahedral sphere with luminescent green strut edges and pulsing nodal vertices.
param("strut_thickness", 0.024, 0.008, 0.06);
param("pulse_rate", 0.8, 0.2, 2.5);
param("tumble_speed", 0.6, 0.1, 2.0);
param("vertex_scale", 0.055, 0.02, 0.12);
param("latitude_rings", 5.0, 1.0, 10.0);

function draw(t) {
  push();
  const time = t * tumble_speed;

  rotateY(time * 0.7);
  rotateX(0.55 + sin(time * 0.5) * 0.25);
  rotateZ(time * 0.3);

  const R = 1.15;
  const phi = (1.0 + sqrt(5.0)) * 0.5;
  const norm = R / sqrt(1.0 + phi * phi);
  const a = 1.0 * norm;
  const b = phi * norm;

  const V = [
    [-a,  b,  0], [ a,  b,  0], [-a, -b,  0], [ a, -b,  0],
    [ 0, -a,  b], [ 0,  a,  b], [ 0, -a, -b], [ 0,  a, -b],
    [ b,  0, -a], [ b,  0,  a], [-b,  0, -a], [-b,  0,  a]
  ];

  const E = [
    [0, 1], [0, 5], [0, 7], [0, 10], [0, 11],
    [1, 5], [1, 7], [1, 8], [1, 9],
    [2, 3], [2, 4], [2, 6], [2, 10], [2, 11],
    [3, 4], [3, 6], [3, 8], [3, 9],
    [4, 5], [4, 9], [4, 11],
    [5, 9], [5, 11],
    [6, 7], [6, 8], [6, 10],
    [7, 8], [7, 10],
    [8, 9], [10, 11]
  ];

  for (let i = 0; i < E.length; i++) {
    const p1 = V[E[i][0]];
    const p2 = V[E[i][1]];
    fill(0.0, 1.0, 0.4);
    tube(p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], strut_thickness, 6);
  }

  const pulse = sin(t * pulse_rate * 3.14159) * 0.25 + 0.75;
  for (let i = 0; i < V.length; i++) {
    push();
    translate(V[i][0], V[i][1], V[i][2]);
    fill(0.502, 1.0, 0.698);
    sphere(vertex_scale * pulse, 8, 8);
    pop();
  }

  const numRings = floor(latitude_rings);
  const ringSteps = 32;
  for (let r = 1; r <= numRings; r++) {
    const lat = ((r / (numRings + 1)) - 0.5) * 3.1415926 * 0.7;
    const ringY = sin(lat) * R;
    const ringR = cos(lat) * R;

    fill(0.0, 0.56, 0.22);
    for (let s = 0; s < ringSteps; s++) {
      const th1 = (s / ringSteps) * 6.2831853;
      const th2 = ((s + 1) / ringSteps) * 6.2831853;
      tube(cos(th1) * ringR, ringY, sin(th1) * ringR,
           cos(th2) * ringR, ringY, sin(th2) * ringR,
           strut_thickness * 0.6, 6);
    }
  }

  pop();
}
