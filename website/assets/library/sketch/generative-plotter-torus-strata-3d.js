// Generative Plotter Torus Strata 3D - Infinite Library (Sketch 3D)
param("ring_count", 11, 5, 16);
param("tilt_amt", 0.35, 0.05, 0.75);
param("radius_span", 1.35, 0.8, 1.8);
param("spin_rate", 0.22, 0.0, 1.0);

function draw(t) {
  rotateX(0.62);
  rotateY(t * spin_rate + 0.4);

  const nr = floor(ring_count);

  // Central plotter calibration axis
  push();
  fill(0.820, 0.188, 0.122);
  cylinder(0.035, 2.2, 16);
  pop();

  for (let i = 0; i < nr; i++) {
    const u = i / max(1, nr - 1);
    const y = (u - 0.5) * 1.45;
    const rad = radius_span * (0.28 + 0.72 * Math.sin(u * Math.PI));
    const tilt = Math.sin(u * 4.5 + t * 0.4) * tilt_amt;

    push();
    translate(0, y, 0);
    rotateX(Math.PI * 0.5 + tilt);
    rotateY(u * 1.2);
    if (i % 4 === 1) {
      fill(0.820, 0.188, 0.122);
    } else {
      fill(0.067, 0.067, 0.067);
    }
    torus(rad, 0.024, 48, 10);
    pop();
  }
}
