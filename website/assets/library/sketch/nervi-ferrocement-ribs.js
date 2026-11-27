// Nervi Ferrocement Ribs - Infinite Library (Sketch 2D)
param("bays", 11, 6, 18);
param("rib_freq", 3.5, 1.5, 6.0);
param("phase_shift", 0.25, 0.0, 1.0);
param("grain_count", 720, 100, 1400);

function draw(t) {
  // Silver-gelatin drafting ground (#EAE8E2)
  background(0.918, 0.910, 0.886);
  const w = width;
  const h = height;
  const side = min(w, h);
  const margin = side * 0.095;
  const x0 = (w - side) * 0.5 + margin;
  const y0 = (h - side) * 0.5 + margin;
  const span = side - margin * 2.0;

  // Fine architectural vellum grain
  noStroke();
  for (let i = 0; i < grain_count; i++) {
    const gx = x0 + ((i * 0.6180339887) % 1.0) * span;
    const gy = y0 + ((i * 0.3819660113 + sin(i * 7.3) * 0.04) % 1.0) * span;
    fill(0.22, 0.23, 0.25, (i % 4 === 0) ? 0.16 : 0.07);
    circle(gx, gy, 1.2);
  }

  // Outer structural frame
  noFill();
  stroke(0.14, 0.15, 0.17, 0.82);
  strokeWeight(2.2);
  rect(x0, y0, span, span);

  // Secondary transverse diaphragm arches (underlay)
  const nb = floor(bays);
  const segs = 72;
  for (let b = 0; b <= nb; b++) {
    const u = b / nb;
    const cy = y0 + u * span;
    stroke(0.36, 0.38, 0.42, 0.35);
    strokeWeight(1.1);
    beginShape();
    for (let s = 0; s <= segs; s++) {
      const v = s / segs;
      const px = x0 + v * span;
      const arch = -sin(v * PI) * span * 0.042;
      const corr = sin(v * PI * rib_freq * 2.0 + t * phase_shift) * 5.0;
      vertex(px, cy + arch + corr);
    }
    endShape();
  }

  // Primary corrugated ferrocement vault ribs & clerestory apertures
  for (let b = 0; b < nb; b++) {
    const u0 = b / nb;
    const u1 = (b + 1) / nb;
    const yA = y0 + u0 * span;
    const yB = y0 + u1 * span;
    const yMid = 0.5 * (yA + yB);
    const bandH = (yB - yA);

    // Clerestory daylight band
    noStroke();
    const lightBoost = 0.94 + 0.04 * sin(u0 * PI + t * phase_shift);
    fill(lightBoost, lightBoost * 0.99, lightBoost * 0.96, 0.55);
    beginShape();
    for (let s = 0; s <= 36; s++) {
      const v = s / 36;
      const px = x0 + v * span;
      const wave = sin(v * PI * rib_freq * 2.0 + b * 0.55 + t * phase_shift) * bandH * 0.24;
      vertex(px, yMid + wave - bandH * 0.18);
    }
    for (let s = 36; s >= 0; s--) {
      const v = s / 36;
      const px = x0 + v * span;
      const wave = sin(v * PI * rib_freq * 2.0 + b * 0.55 + t * phase_shift) * bandH * 0.24;
      vertex(px, yMid + wave + bandH * 0.18);
    }
    endShape();

    // Ribbed concrete soffit shadow & highlight lines
    noFill();
    stroke(0.11, 0.12, 0.14, 0.85);
    strokeWeight(2.4);
    beginShape();
    for (let s = 0; s <= segs; s++) {
      const v = s / segs;
      const px = x0 + v * span;
      const wave = sin(v * PI * rib_freq * 2.0 + b * 0.55 + t * phase_shift) * bandH * 0.26;
      vertex(px, yMid + wave - bandH * 0.20);
    }
    endShape();

    stroke(0.28, 0.30, 0.33, 0.65);
    strokeWeight(1.4);
    beginShape();
    for (let s = 0; s <= segs; s++) {
      const v = s / segs;
      const px = x0 + v * span;
      const wave = sin(v * PI * rib_freq * 2.0 + b * 0.55 + t * phase_shift) * bandH * 0.26;
      vertex(px, yMid + wave + bandH * 0.20);
    }
    endShape();

    // Diagonal precast truss struts
    stroke(0.18, 0.20, 0.23, 0.55);
    strokeWeight(1.0);
    const struts = floor(rib_freq * 4);
    for (let k = 1; k < struts; k++) {
      const v = k / struts;
      const px = x0 + v * span;
      const wave = sin(v * PI * rib_freq * 2.0 + b * 0.55 + t * phase_shift) * bandH * 0.26;
      line(px - 6, yMid + wave - bandH * 0.18, px + 6, yMid + wave + bandH * 0.18);
    }
  }
}
