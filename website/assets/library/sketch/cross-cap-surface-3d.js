// Cross-Cap Surface 3D - Infinite Library (Sketch 3D). Paste into a Sketch 3D node's code editor.
// Non-orientable cross-cap immersion of the real projective plane RP2 with Whitney pinch point singularities, parametric meridian lines, and orbital rings.
param("segments", 64, 24, 96);
param("pinch_scale", 1.25, 0.5, 2.5);
param("scale_3d", 1.15, 0.5, 2.0);
param("spin", 0.55, 0.0, 3.0);
param("hue", 0.65, 0.0, 1.0);

function draw(t) {
  push();
  rotateY(t * spin);
  rotateX(0.62 + sin(t * 0.3) * 0.1);
  rotateZ(cos(t * 0.22) * 0.12);

  const N = floor(segments);
  const uSteps = floor(N * 0.5);
  const vSteps = N;
  const r = scale_3d;

  for (let i = 0; i < uSteps; i++) {
    const u = (i / uSteps) * PI;
    const sinU = sin(u);
    const cosU = cos(u);
    const sin2U = sin(2.0 * u);
    const cos2U = cos(2.0 * u);
    const fracU = i / uSteps;

    fill(hsl((hue + fracU * 0.45) % 1.0, 0.75, 0.5));

    for (let j = 0; j < vSteps; j += 2) {
      const v1 = (j / vSteps) * PI;
      const v2 = ((j + 1) / vSteps) * PI;

      const cosV1 = cos(v1);
      const cos2V1 = cosV1 * cosV1;
      const sin2V1 = sin(2.0 * v1);

      const cosV2 = cos(v2);
      const cos2V2 = cosV2 * cosV2;
      const sin2V2 = sin(2.0 * v2);

      // Classical cross-cap parametric coordinates:
      // x = r * sin(u) * sin(2v) * 0.5
      // y = r * sin(2u) * cos^2(v)
      // z = r * cos(2u) * cos^2(v)
      const x1 = r * sinU * sin2V1 * 0.5 * pinch_scale;
      const y1 = r * sin2U * cos2V1;
      const z1 = r * cos2U * cos2V1;

      const x2 = r * sinU * sin2V2 * 0.5 * pinch_scale;
      const y2 = r * sin2U * cos2V2;
      const z2 = r * cos2U * cos2V2;

      tube(x1, y1, z1, x2, y2, z2, 0.022, 6);
    }
  }

  // Whitney pinch singularity endpoints
  const pinch1 = [0, r, r];
  const pinch2 = [0, -r, r];
  const pinch3 = [0, 0, -r];
  const pinchPoints = [pinch1, pinch2, pinch3];

  fill(0.306, 0.949, 0.722); // Emerald highlight
  for (let k = 0; k < pinchPoints.length; k++) {
    const pt = pinchPoints[k];
    push();
    translate(pt[0], pt[1], pt[2]);
    sphere(0.045);
    pop();
  }

  // Orbiting equatorial ring
  fill(1.0, 0.42, 0.21); // Ember
  for (let k = 0; k < 32; k++) {
    const a1 = (k / 32) * TAU;
    const a2 = ((k + 1) / 32) * TAU;
    const rx = r * 1.05;
    tube(cos(a1) * rx, 0, sin(a1) * rx, cos(a2) * rx, 0, sin(a2) * rx, 0.015, 4);
  }

  pop();
}
