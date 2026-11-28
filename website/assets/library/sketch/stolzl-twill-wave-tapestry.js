// Stolzl Twill Wave Tapestry - Infinite Library (Sketch 2D)
param("bands", 14, 8, 22);
param("twill_step", 12, 6, 20);
param("wave_amp", 0.065, 0.01, 0.14);
param("drift", 0.35, 0.0, 1.2);

function draw(t) {
  // Warm woven canvas ground (#EAE2D0)
  background(0.918, 0.886, 0.816);
  const w = width;
  const h = height;
  const padX = w * 0.10;
  const padY = h * 0.09;
  const spanW = w - padX * 2;
  const spanH = h - padY * 2;
  const nBands = floor(bands);
  const nStep = floor(twill_step);
  const bandH = spanH / nBands;

  // Undulating wave-weft bands
  noStroke();
  for (let b = 0; b < nBands; b++) {
    const yBase = padY + b * bandH;
    const mod = b % 4;
    if (mod === 0) {
      fill(0.769, 0.180, 0.180, 0.92); // Crimson (#C42E2E)
    } else if (mod === 1) {
      fill(0.114, 0.235, 0.420, 0.92); // Cobalt (#1D3C6B)
    } else if (mod === 2) {
      fill(0.831, 0.639, 0.220, 0.92); // Ochre (#D4A338)
    } else {
      fill(0.373, 0.494, 0.337, 0.92); // Sage (#5F7E56)
    }

    beginShape();
    const segs = 40;
    for (let i = 0; i <= segs; i++) {
      const u = i / segs;
      const x = padX + u * spanW;
      const wave = sin(u * TAU * 1.5 + b * 0.65 + t * drift) * spanH * wave_amp;
      vertex(x, yBase + wave);
    }
    for (let i = segs; i >= 0; i--) {
      const u = i / segs;
      const x = padX + u * spanW;
      const wave = sin(u * TAU * 1.5 + (b + 1) * 0.65 + t * drift) * spanH * wave_amp;
      vertex(x, yBase + bandH + wave);
    }
    endShape(CLOSE);
  }

  // Central stepped checkerboard twill panel
  const twillW = spanW * 0.46;
  const twillH = spanH * 0.44;
  const tx0 = w * 0.5 - twillW * 0.5;
  const ty0 = h * 0.5 - twillH * 0.5;
  const cw = twillW / nStep;
  const ch = twillH / nStep;

  for (let ix = 0; ix < nStep; ix++) {
    for (let iy = 0; iy < nStep; iy++) {
      const diag = (ix + iy) % 4;
      if (diag === 0) {
        fill(0.114, 0.235, 0.420, 0.95);
      } else if (diag === 1) {
        fill(0.953, 0.933, 0.878, 0.95);
      } else if (diag === 2) {
        fill(0.769, 0.180, 0.180, 0.95);
      } else {
        fill(0.831, 0.639, 0.220, 0.95);
      }
      rect(tx0 + ix * cw, ty0 + iy * ch, cw * 0.94, ch * 0.94);
    }
  }

  // Woven warp fringe along bottom edge
  stroke(0.55, 0.48, 0.36, 0.72);
  strokeWeight(1.6);
  const fringeN = 56;
  for (let i = 0; i <= fringeN; i++) {
    const fx = padX + (i / fringeN) * spanW;
    line(fx, padY + spanH + 4, fx + sin(i * 1.7) * 2.5, padY + spanH + 28);
  }

  // Outer selvedge frame
  noFill();
  stroke(0.18, 0.16, 0.15, 0.85);
  strokeWeight(2.5);
  rect(padX, padY, spanW, spanH);
}