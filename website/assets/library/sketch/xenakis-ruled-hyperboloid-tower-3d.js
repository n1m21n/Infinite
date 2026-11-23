// Xenakis Ruled Hyperboloid Tower 3D - Infinite Library (Sketch 3D)
param("struts", 18, 10, 32);
param("twist_rad", 1.15, 0.4, 2.0);
param("waist", 0.55, 0.25, 0.95);
param("spin", 0.22, 0.0, 0.6);

function draw(t) {
  rotateX(0.36 + 0.06 * cos(t * 0.35));
  rotateY(t * spin + 0.3);

  const nS = floor(struts);
  const H = 2.4;
  const R = 1.12;

  // Compression ring collars (#2C2A29 & #B85D3B)
  for (let r = 0; r < 5; r++) {
    const u = r / 4.0;
    const yy = (u - 0.5) * H;
    const rad = R * sqrt(waist * waist + (1.0 - waist * waist) * pow(u * 2.0 - 1.0, 2));
    push();
    translate(0, yy, 0);
    fill(r === 2 ? 0.722 : 0.88, r === 2 ? 0.365 : 0.85, r === 2 ? 0.231 : 0.79);
    torus(rad, r === 2 ? 0.042 : 0.026);
    pop();
  }

  // Counter-rotating families of straight ruled struts (#EAE3D5 & #B85D3B)
  for (let dir = -1; dir <= 1; dir += 2) {
    for (let i = 0; i < nS; i++) {
      const a0 = (i / nS) * TAU;
      const a1 = a0 + dir * twist_rad;
      const x0 = cos(a0) * R;
      const z0 = sin(a0) * R;
      const x1 = cos(a1) * R;
      const z1 = sin(a1) * R;

      const mx = (x0 + x1) * 0.5;
      const mz = (z0 + z1) * 0.5;
      const dx = x1 - x0;
      const dz = z1 - z0;
      const tilt = atan2(sqrt(dx * dx + dz * dz), H);
      const yaw = atan2(dz, dx);

      push();
      translate(mx, 0, mz);
      rotateY(-yaw);
      rotateZ(tilt);
      fill(dir > 0 ? 0.918 : 0.65, dir > 0 ? 0.890 : 0.62, dir > 0 ? 0.835 : 0.58);
      cylinder(0.020, sqrt(H * H + dx * dx + dz * dz));
      pop();
    }
  }
}
