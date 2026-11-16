// Vector Wireframe Icosahedron Radar - Infinite Library (Sketch 2D)
param("ring_count", 6, 3, 10);
param("spin_speed", 0.55, 0.0, 2.0);
param("poly_scale", 0.28, 0.14, 0.42);
param("tick_density", 36, 12, 72);

function draw(t) {
  background(0.015, 0.048, 0.028);

  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const maxR = min(w, h) * 0.44;
  const nr = floor(ring_count);
  const nt = floor(tick_density);

  // Subtle CRT phosphor radar screen glow
  noStroke();
  fill(0.0, 0.32, 0.15, 0.22);
  circle(cx, cy, maxR * 2.0);
  fill(0.0, 0.55, 0.24, 0.18);
  circle(cx, cy, maxR * 1.15);

  // Concentric radar range rings
  noFill();
  for (let i = 1; i <= nr; i++) {
    const r = (i / nr) * maxR;
    stroke(0.0, 1.0, 0.416, i === nr ? 0.92 : 0.48);
    strokeWeight(i === nr ? 3.2 : 2.0);
    circle(cx, cy, r * 2.0);
  }

  // Azimuthal ticks around outer ring
  for (let i = 0; i < nt; i++) {
    const ang = (i / nt) * Math.PI * 2.0;
    const isMajor = (i % 3 === 0);
    const r0 = maxR * (isMajor ? 0.92 : 0.96);
    const r1 = maxR * 1.02;
    stroke(isMajor ? 1.0 : 0.0, 1.0, isMajor ? 1.0 : 0.416, isMajor ? 0.9 : 0.5);
    strokeWeight(isMajor ? 2.2 : 1.2);
    line(cx + Math.cos(ang) * r0, cy + Math.sin(ang) * r0, cx + Math.cos(ang) * r1, cy + Math.sin(ang) * r1);
  }

  // Rotating radar sweep line
  const sweepAng = t * spin_speed * 1.4;
  stroke(0.0, 1.0, 0.416, 0.75);
  strokeWeight(2.0);
  line(cx, cy, cx + Math.cos(sweepAng) * maxR, cy + Math.sin(sweepAng) * maxR);

  // 3D-projected icosahedron vertices
  const phi = 1.6180339887;
  const rawVerts = [
    [-1, phi, 0], [1, phi, 0], [-1, -phi, 0], [1, -phi, 0],
    [0, -1, phi], [0, 1, phi], [0, -1, -phi], [0, 1, -phi],
    [phi, 0, -1], [phi, 0, 1], [-phi, 0, -1], [-phi, 0, 1]
  ];
  const edges = [
    [0,11],[0,5],[0,1],[0,7],[0,10],[1,5],[5,11],[11,10],[10,7],[7,1],
    [3,9],[3,4],[3,2],[3,6],[3,8],[4,9],[2,4],[6,2],[8,6],[9,8],
    [1,9],[5,4],[11,2],[10,6],[7,8],[9,5],[4,11],[2,10],[6,7],[8,1]
  ];

  const ay = t * spin_speed + 0.45;
  const ax = t * spin_speed * 0.62 + 0.35;
  const cyA = Math.cos(ay), syA = Math.sin(ay);
  const cxA = Math.cos(ax), sxA = Math.sin(ax);
  const sc = min(w, h) * poly_scale * 0.58;

  const proj = [];
  for (let i = 0; i < rawVerts.length; i++) {
    const vx = rawVerts[i][0], vy = rawVerts[i][1], vz = rawVerts[i][2];
    const x1 = vx * cyA - vz * syA;
    const z1 = vx * syA + vz * cyA;
    const y2 = vy * cxA - z1 * sxA;
    const z2 = vy * sxA + z1 * cxA;
    const persp = 4.2 / (4.2 - z2 * 0.45);
    proj.push([cx + x1 * sc * persp, cy + y2 * sc * persp, z2]);
  }

  // Draw wireframe edges with depth-weighted phosphor intensity
  for (let e = 0; e < edges.length; e++) {
    const a = proj[edges[e][0]];
    const b = proj[edges[e][1]];
    const zAvg = (a[2] + b[2]) * 0.5;
    const front = zAvg > 0.0;
    if (front) {
      stroke(1.0, 1.0, 1.0, 0.95);
      strokeWeight(2.8);
    } else {
      stroke(0.0, 1.0, 0.416, 0.55);
      strokeWeight(1.8);
    }
    line(a[0], a[1], b[0], b[1]);
  }

  // Vertex phosphor nodes
  noStroke();
  for (let i = 0; i < proj.length; i++) {
    const p = proj[i];
    fill(p[2] > 0 ? 1.0 : 0.0, 1.0, p[2] > 0 ? 1.0 : 0.416, 0.95);
    circle(p[0], p[1], p[2] > 0 ? 8 : 5);
  }
}
