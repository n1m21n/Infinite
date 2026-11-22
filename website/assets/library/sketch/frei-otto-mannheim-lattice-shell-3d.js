// Frei Otto Mannheim Lattice Shell 3D - Infinite Library (Sketch 3D)
param("arches", 12, 6, 18);
param("dome_rise", 1.35, 0.7, 2.0);
param("flare", 0.28, 0.05, 0.55);
param("orbit", 0.25, 0.0, 1.0);

function draw(t) {
  // Architectural three-quarter perspective
  scale(0.55);
  rotateX(0.26 + 0.04 * sin(t * 0.35));
  rotateY(0.55 + 0.16 * sin(t * orbit * 0.45));
  translate(0, -0.58, 0);

  const n = floor(arches);
  const span = 2.6;
  const half = span * 0.5;

  // Perimeter foundation plinth (#242B33)
  fill(0.141, 0.169, 0.200);
  push();
  translate(0, -0.06, 0);
  box(span + 0.7, 0.12, span + 0.7);
  pop();

  // Form-found inverted catenary timber-and-steel gridshell
  const segs = 16;
  for (let i = 0; i <= n; i++) {
    const u = (i / n) * 2.0 - 1.0;
    const z = u * half;
    const edgeProfile = 1.0 - 0.65 * u * u;

    for (let s = 0; s < segs; s++) {
      const v0 = (s / segs) * 2.0 - 1.0;
      const v1 = ((s + 1) / segs) * 2.0 - 1.0;
      const x0 = v0 * half;
      const x1 = v1 * half;
      const xm = (x0 + x1) * 0.5;

      const y0 = dome_rise * (1.0 - v0 * v0) * edgeProfile + flare * sin(v0 * PI * 2.0) * 0.15;
      const y1 = dome_rise * (1.0 - v1 * v1) * edgeProfile + flare * sin(v1 * PI * 2.0) * 0.15;
      const ym = (y0 + y1) * 0.5;

      const dx = x1 - x0;
      const dy = y1 - y0;
      const len = sqrt(dx * dx + dy * dy);
      const ang = atan2(dy, dx);

      // Primary larch/anodized lattice laths (#D7D3CA)
      fill(0.843, 0.827, 0.792);
      push();
      translate(xm, ym, z);
      rotateZ(ang);
      box(len * 1.02, 0.028, 0.032);
      pop();

      // Orthogonal transverse lattice laths (#6E94B8)
      const xOrtho = u * half;
      const z0 = v0 * half;
      const z1 = v1 * half;
      const zm = (z0 + z1) * 0.5;
      const orthoProf = 1.0 - 0.65 * u * u;
      const yo0 = dome_rise * (1.0 - v0 * v0) * orthoProf + flare * sin(v0 * PI * 2.0) * 0.15;
      const yo1 = dome_rise * (1.0 - v1 * v1) * orthoProf + flare * sin(v1 * PI * 2.0) * 0.15;
      const yom = (yo0 + yo1) * 0.5;
      const lenO = sqrt((z1 - z0) * (z1 - z0) + (yo1 - yo0) * (yo1 - yo0));
      const angO = atan2(yo1 - yo0, z1 - z0);

      fill(0.431, 0.580, 0.722);
      push();
      translate(xOrtho, yom, zm);
      rotateX(-angO);
      box(0.026, 0.024, lenO * 1.02);
      pop();
    }
  }

  // Central compression oculus ring & tensioned tie-rods
  fill(0.231, 0.275, 0.322);
  push();
  translate(0, dome_rise, 0);
  rotateX(PI * 0.5);
  torus(0.28, 0.038);
  pop();
}
