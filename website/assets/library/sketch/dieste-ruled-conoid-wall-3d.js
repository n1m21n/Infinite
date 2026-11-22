// Dieste Ruled Conoid Wall 3D - Infinite Library (Sketch 3D)
param("piers", 22, 12, 34);
param("wave_depth", 0.58, 0.2, 0.95);
param("wall_height", 1.85, 1.2, 2.6);
param("sway", 0.22, 0.0, 0.8);

function draw(t) {
  scale(0.62);
  rotateX(0.20 + 0.04 * cos(t * 0.3));
  rotateY(0.32 + 0.22 * sin(t * sway * 0.5));
  translate(0, -wall_height * 0.45, 0);

  const nPiers = floor(piers);
  const spanX = 2.8;
  const courses = 14;

  // Stone foundation plinth (#D9CBB8)
  fill(0.84, 0.78, 0.70);
  push();
  translate(0, -0.07, 0);
  box(spanX + 0.5, 0.14, wave_depth * 2.4 + 0.6);
  pop();

  // Straight upper reinforced ceramic lintel beam (#C8B79F)
  fill(0.78, 0.71, 0.62);
  push();
  translate(0, wall_height + 0.08, 0);
  box(spanX + 0.25, 0.16, 0.24);
  pop();

  // Ruled-surface sinusoidal brick wall: interpolates z from sinusoidal base to straight top lintel
  for (let i = 0; i < nPiers; i++) {
    const u = i / (nPiers - 1);
    const x = (u - 0.5) * spanX;
    const baseZ = sin(u * TAU * 1.5 + 0.3) * wave_depth;

    for (let c = 0; c < courses; c++) {
      const v = (c + 0.5) / courses;
      const y = v * wall_height;
      // Ruled line interpolation between (x, 0, baseZ) and (x, wall_height, 0)
      const z = baseZ * (1.0 - v);
      const tiltX = atan(baseZ / wall_height);

      // Rhythmic vertical clerestory slit gap on every 4th pier in upper third
      if (i % 4 === 2 && v > 0.62 && v < 0.88) {
        continue;
      }

      const brickTone = ((i + c) % 3);
      if (brickTone === 0) fill(0.659, 0.294, 0.165);
      else if (brickTone === 1) fill(0.545, 0.235, 0.125);
      else fill(0.745, 0.365, 0.210);

      push();
      translate(x + ((c % 2) * 0.03 - 0.015), y, z);
      rotateX(-tiltX);
      box((spanX / nPiers) * 0.92, (wall_height / courses) * 0.88, 0.18);
      pop();
    }
  }

  // Rhythmic exterior buttress fins at wave troughs
  fill(0.24, 0.16, 0.12);
  for (let k = 0; k < 4; k++) {
    const u = (k + 0.5) / 4.0;
    const x = (u - 0.5) * spanX;
    push();
    translate(x, wall_height * 0.22, -wave_depth * 0.65);
    box(0.09, wall_height * 0.44, 0.45);
    pop();
  }
}
