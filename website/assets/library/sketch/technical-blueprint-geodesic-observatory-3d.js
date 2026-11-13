// Technical Blueprint Geodesic Observatory 3D - Infinite Library (Sketch 3D)
param("meridian_ribs", 12, 6, 20);
param("lat_rings", 5, 3, 8);
param("dome_radius", 1.25, 0.8, 1.6);
param("orbit_speed", 0.25, 0.0, 1.0);

function draw(t) {
  rotateX(0.36);
  rotateY(t * orbit_speed);

  const nm = floor(meridian_ribs);
  const nl = floor(lat_rings);

  // Base plinth and azimuth ring
  push();
  translate(0, -0.55, 0);
  fill(0.18, 0.34, 0.52);
  cylinder(dome_radius * 1.12, 0.1, 36);
  pop();

  push();
  translate(0, -0.48, 0);
  rotateX(Math.PI * 0.5);
  fill(0.357, 0.753, 0.745);
  torus(dome_radius * 1.08, 0.026, 40, 10);
  pop();

  // Meridian ribs
  for (let i = 0; i < nm; i++) {
    const ang = (i / nm) * Math.PI;
    push();
    rotateY(ang);
    fill(0.847, 0.925, 1.0);
    torus(dome_radius, 0.016, 36, 8);
    pop();
  }

  // Latitude rings
  for (let j = 1; j <= nl; j++) {
    const phi = (j / (nl + 1)) * Math.PI * 0.48;
    const ry = Math.sin(phi) * dome_radius;
    const rr = Math.cos(phi) * dome_radius;
    push();
    translate(0, ry, 0);
    rotateX(Math.PI * 0.5);
    fill(0.357, 0.753, 0.745);
    torus(rr, 0.015, 36, 8);
    pop();
  }

  // Central collimator instrument
  push();
  fill(0.847, 0.925, 1.0);
  cylinder(0.12, 1.1, 20);
  translate(0, 0.62, 0);
  fill(0.357, 0.753, 0.745);
  sphere(0.18, 16);
  pop();
}
