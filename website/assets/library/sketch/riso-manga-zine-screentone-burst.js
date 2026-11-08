// Riso Manga Zine Screentone Burst - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Multi-layered risograph halftone screentone composition featuring concentric micro-dot matrices, radiating action impact stars, dynamic speed chevron accents, and bold riso ink separation.
param("burst_spikes", 16.0, 8.0, 32.0);
param("halftone_density", 22.0, 10.0, 36.0);
param("spin", 0.35, 0.1, 1.5);
param("riso_offset", 8.0, 2.0, 20.0);

function draw(t) {
  // Rich aged riso paper background (#ede7d5)
  background(0.885, 0.860, 0.795);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const span = min(width, height) * 0.88;
  const time = t * spin;

  push();
  translate(cx, cy);

  // 1. Deconstructed Zine Outer Frame & Corner Registration Marks
  stroke(0.10, 0.10, 0.14);
  strokeWeight(3.5);
  noFill();
  rect(-span * 0.48, -span * 0.48, span * 0.96, span * 0.96);

  // Registration crosshairs
  strokeWeight(1.8);
  const regOff = span * 0.45;
  const regLen = span * 0.025;
  const corners = [[-regOff, -regOff], [regOff, -regOff], [-regOff, regOff], [regOff, regOff]];
  for (let c of corners) {
    line(c[0] - regLen, c[1], c[0] + regLen, c[1]);
    line(c[0], c[1] - regLen, c[0], c[1] + regLen);
    circle(c[0], c[1], regLen * 1.2);
  }

  // 2. Riso Halftone Screentone Grid (Fluorescent Coral/Pink & Sunflower Yellow)
  const dots = floor(halftone_density);
  const step = (span * 0.88) / dots;
  noStroke();

  // Sunflower Yellow pass (#ffcc00)
  fill(1.0, 0.78, 0.0, 0.78);
  for (let i = -dots / 2; i <= dots / 2; i++) {
    for (let j = -dots / 2; j <= dots / 2; j++) {
      const d = Math.hypot(i, j);
      if (d < dots * 0.48) {
        const dotR = (sin(d * 0.45 - time * 2.0) * 0.5 + 0.5) * (step * 0.48) + 2.0;
        circle(i * step + 4.0, j * step + 3.0, dotR * 2.0);
      }
    }
  }

  // Fluorescent Pink pass (#ff3366) with misregistration shift
  fill(0.96, 0.16, 0.38, 0.85);
  for (let i = -dots / 2; i <= dots / 2; i++) {
    for (let j = -dots / 2; j <= dots / 2; j++) {
      const d = Math.hypot(i, j);
      if (d < dots * 0.45) {
        const dotR = (cos(d * 0.50 + time * 1.8) * 0.5 + 0.5) * (step * 0.44) + 1.8;
        circle(i * step - riso_offset * 0.5, j * step + riso_offset * 0.4, dotR * 2.0);
      }
    }
  }

  // 3. Radial Action Manga Speedlines (Deep Riso Blue #0033cc)
  push();
  rotate(time * 0.25);
  stroke(0.0, 0.20, 0.75, 0.88);
  const nLines = 54;
  for (let l = 0; l < nLines; l++) {
    const ang = (l / nLines) * Math.PI * 2.0;
    const r1 = span * (0.22 + sin(l * 12.0 + time * 3.0) * 0.08);
    const r2 = span * 0.46;
    strokeWeight((l % 3 === 0) ? 3.5 : (l % 2 === 0) ? 2.2 : 1.4);
    line(cos(ang) * r1, sin(ang) * r1, cos(ang) * r2, sin(ang) * r2);
  }
  pop();

  // 4. Explosive Starburst Impact Silhouette (Ink Black with Riso Pink Shadow)
  const spikes = floor(burst_spikes);

  // Pink offset shadow
  push();
  translate(riso_offset, riso_offset * 0.8);
  fill(0.96, 0.16, 0.38, 0.92);
  noStroke();
  beginShape();
  for (let s = 0; s <= spikes * 2; s++) {
    const ang = (s / (spikes * 2)) * Math.PI * 2.0 - time * 0.5;
    const r = (s % 2 === 0) ? span * 0.34 : span * 0.15;
    vertex(cos(ang) * r, sin(ang) * r);
  }
  endShape();
  pop();

  // Charcoal Black Main Starburst
  fill(0.10, 0.10, 0.14);
  stroke(0.10, 0.10, 0.14);
  strokeWeight(2.0);
  beginShape();
  for (let s = 0; s <= spikes * 2; s++) {
    const ang = (s / (spikes * 2)) * Math.PI * 2.0 - time * 0.5;
    const r = (s % 2 === 0) ? span * 0.34 : span * 0.15;
    vertex(cos(ang) * r, sin(ang) * r);
  }
  endShape();

  // Central Sunflower Yellow core spark
  fill(1.0, 0.78, 0.0);
  noStroke();
  circle(0, 0, span * 0.09);

  // Sound-effect dynamic graphic chevron accents
  fill(0.885, 0.860, 0.795);
  for (let k = 0; k < 4; k++) {
    const ang = (k / 4.0) * Math.PI * 2.0 + time * 0.8;
    const rk = span * 0.21;
    push();
    translate(cos(ang) * rk, sin(ang) * rk);
    rotate(ang + Math.PI * 0.5);
    triangle(-span * 0.022, span * 0.022, span * 0.022, span * 0.022, 0, -span * 0.035);
    pop();
  }

  pop();
}
