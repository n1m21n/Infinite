// Muqarnas Corbel Projection - Infinite Library (Sketch 2D)
param("tiers", 6, 4, 9);
param("symmetry", 12, 8, 16);
param("twist", 0.08, 0.0, 0.25);
param("line_w", 2.2, 1.0, 4.5);

function draw(t) {
  // Deep lapis-indigo vault ground (#112244)
  background(0.067, 0.133, 0.267);
  const w = width;
  const h = height;
  const cx = w * 0.5;
  const cy = h * 0.5;
  const maxR = min(w, h) * 0.44;
  const nTiers = floor(tiers);
  const sym = floor(symmetry);

  // Outer arched squinch border
  noFill();
  stroke(0.82, 0.68, 0.42, 0.55);
  strokeWeight(line_w * 1.3);
  circle(cx, cy, maxR * 2.06);

  // Radial drafting axes
  stroke(0.82, 0.68, 0.42, 0.22);
  strokeWeight(1.0);
  for (let s = 0; s < sym; s++) {
    const a = (s / sym) * TAU;
    line(cx, cy, cx + cos(a) * maxR * 1.03, cy + sin(a) * maxR * 1.03);
  }

  // Tiered muqarnas stalactite cells from outer squinch to apex
  for (let tier = nTiers; tier >= 1; tier--) {
    const u = tier / nTiers;
    const rOuter = maxR * u;
    const rInner = maxR * (tier - 0.68) / nTiers;
    const phase = (tier % 2 === 0 ? 0.5 : 0.0) * (TAU / sym) + sin(t * 0.4 + tier * 0.5) * twist;

    // Alternating sandstone-gold, turquoise-glaze, and lapis-cobalt muqarnas tiers
    if (tier % 3 === 0) {
      fill(0.82, 0.64, 0.32, 0.96);
    } else if (tier % 3 === 1) {
      fill(0.16, 0.48, 0.58, 0.96);
    } else {
      fill(0.114, 0.243, 0.478, 0.96);
    }
    stroke(0.95, 0.91, 0.82, 0.92);
    strokeWeight(line_w);

    beginShape();
    for (let s = 0; s < sym * 2; s++) {
      const a = (s / (sym * 2)) * TAU + phase;
      const rad = (s % 2 === 0) ? rOuter : rInner;
      vertex(cx + cos(a) * rad, cy + sin(a) * rad);
    }
    endShape(CLOSE);

    // Ribbed corbel highlights & dark shadow pockets
    noFill();
    stroke(0.06, 0.08, 0.14, 0.85);
    strokeWeight(line_w * 0.7);
    for (let s = 0; s < sym; s++) {
      const a0 = (s / sym) * TAU + phase;
      const a1 = ((s + 0.5) / sym) * TAU + phase;
      line(cx + cos(a0) * rOuter, cy + sin(a0) * rOuter,
           cx + cos(a1) * rInner, cy + sin(a1) * rInner);
    }
  }

  // Central Shamsa sunburst medallion
  fill(0.114, 0.243, 0.478, 0.98);
  stroke(0.92, 0.80, 0.48, 0.95);
  strokeWeight(line_w);
  beginShape();
  for (let s = 0; s < sym * 2; s++) {
    const a = (s / (sym * 2)) * TAU;
    const rad = (s % 2 === 0) ? maxR * 0.14 : maxR * 0.07;
    vertex(cx + cos(a) * rad, cy + sin(a) * rad);
  }
  endShape(CLOSE);
}