// Fuller Synergetic Octet Truss 3D - Infinite Library (Sketch 3D)
param("cells_x", 3, 2, 4);
param("cells_y", 2, 1, 3);
param("strut_r", 0.026, 0.014, 0.05);
param("orbit", 0.25, 0.0, 1.0);

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

function draw(t) {
  scale(0.55);
  rotateX(0.36 + 0.05 * sin(t * 0.3));
  rotateY(0.72 + t * orbit * 0.26);
  translate(0, -0.42, 0);

  const nx = floor(cells_x);
  const ny = floor(cells_y);
  const nz = floor(cells_x);
  const step = 0.72;

  // Dark slate base plinth
  fill(0.094, 0.122, 0.157);
  push();
  translate(0, -0.10, 0);
  box(nx * step + 1.1, 0.16, nz * step + 1.1);
  pop();

  // Build FCC lattice nodes (i+j+k even)
  const nodes = [];
  for (let i = 0; i <= nx * 2; i++) {
    for (let j = 0; j <= ny * 2; j++) {
      for (let k = 0; k <= nz * 2; k++) {
        if ((i + j + k) % 2 !== 0) continue;
        const x = (i - nx) * (step * 0.5);
        const y = j * (step * 0.5) + 0.08;
        const z = (k - nz) * (step * 0.5);
        nodes.push([i, j, k, x, y, z]);
      }
    }
  }

  // Connect nearest FCC neighbors (distance squared == 2 in index space)
  for (let a = 0; a < nodes.length; a++) {
    const na = nodes[a];
    const isTop = na[1] === ny * 2;

    // Joint sphere
    if (isTop) {
      fill(0.961, 0.722, 0.180);
    } else {
      fill(0.580, 0.659, 0.749);
    }
    push();
    translate(na[3], na[4], na[5]);
    sphere(strut_r * 1.9);
    pop();

    for (let b = a + 1; b < nodes.length; b++) {
      const nb = nodes[b];
      const di = nb[0] - na[0];
      const dj = nb[1] - na[1];
      const dk = nb[2] - na[2];
      if (di * di + dj * dj + dk * dk === 2) {
        if (dj === 0) {
          // Horizontal brass chord
          fill(0.84, 0.58, 0.16);
          drawStrut(na[3], na[4], na[5], nb[3], nb[4], nb[5], strut_r * 1.15);
        } else {
          // Diagonal steel tetrahedral web
          fill(0.55, 0.64, 0.74);
          drawStrut(na[3], na[4], na[5], nb[3], nb[4], nb[5], strut_r * 0.85);
        }
      }
    }
  }

  // Translucent amber skylight deck tiles on top chord
  fill(0.98, 0.82, 0.34, 0.32);
  const topY = ny * 2 * (step * 0.5) + 0.11;
  for (let i = 0; i < nx; i++) {
    for (let k = 0; k < nz; k++) {
      const tx = (i - nx * 0.5 + 0.5) * step;
      const tz = (k - nz * 0.5 + 0.5) * step;
      push();
      translate(tx, topY, tz);
      box(step * 0.82, 0.016, step * 0.82);
      pop();
    }
  }
}
