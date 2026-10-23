// Apollonian Gasket - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Fractal circle packing governed by the Descartes Circle Theorem and Soddy circles with breathing conformal phase.
param("depth", 4, 1, 6);
param("pulse", 0.35, 0.0, 1.0);
param("spin", 0.15, 0.0, 1.0);
param("scale", 1.05, 0.4, 2.5);
param("hue", 0.60, 0.0, 1.0);
param("stroke_w", 1.5, 0.5, 3.5);

function draw(t) {
  background(0.082, 0.098, 0.188); // brand midnight #151930
  const cx = width * 0.5, cy = height * 0.5;
  const baseR = min(width, height) * 0.46 * scale;
  const rot = t * spin;

  // Outer enclosing circle (curvature -1 / baseR)
  const circles = [];
  circles.push({ x: cx, y: cy, r: baseR, k: -1.0 / baseR, gen: 0 });

  // Breathing modulation for the three symmetric primary interior circles
  const breathe = 1.0 + sin(t * 1.5) * 0.12 * pulse;
  const r1 = baseR / (1.0 + 2.0 / sqrt(3.0)) * breathe;
  const k1 = 1.0 / r1;
  const dist = baseR - r1;

  for (let i = 0; i < 3; i++) {
    const angle = rot + (i * TAU) / 3.0;
    circles.push({
      x: cx + dist * cos(angle),
      y: cy + dist * sin(angle),
      r: r1,
      k: k1,
      gen: 1
    });
  }

  // Central Soddy circle
  const kInner = -circles[0].k + 2.0 * sqrt(3.0) * k1;
  const rInner = abs(1.0 / kInner);
  circles.push({ x: cx, y: cy, r: rInner, k: kInner, gen: 1 });

  // Iterative Descartes circle generation
  const maxGen = floor(depth);
  const queue = [
    [circles[0], circles[1], circles[2]],
    [circles[0], circles[2], circles[3]],
    [circles[0], circles[3], circles[1]],
    [circles[1], circles[2], circles[4]],
    [circles[2], circles[3], circles[4]],
    [circles[3], circles[1], circles[4]]
  ];

  let head = 0;
  while (head < queue.length && circles.length < 800) {
    const triplet = queue[head++];
    const c1 = triplet[0], c2 = triplet[1], c3 = triplet[2];
    const gen = max(c1.gen, max(c2.gen, c3.gen)) + 1;
    if (gen > maxGen) continue;

    // Descartes theorem for curvatures: k4 = k1 + k2 + k3 +/- 2 * sqrt(k1*k2 + k2*k3 + k3*k1)
    const kSum = c1.k + c2.k + c3.k;
    const kProd = c1.k * c2.k + c2.k * c3.k + c3.k * c1.k;
    if (kProd < 0) continue;
    const kRoot = 2.0 * sqrt(kProd);
    const k4a = kSum + kRoot;
    const k4b = kSum - kRoot;
    const k4 = k4a > 0 ? k4a : k4b;

    if (k4 <= 0) continue;
    const r4 = 1.0 / k4;
    if (r4 < 2.0) continue;

    // Complex coordinates for centers: z4 = (z1*k1 + z2*k2 + z3*k3 +/- 2 * sqrt(k1*k2*z1*z2 + ...)) / k4
    // Geometric centroid relaxation approximation
    const wx = (c1.x * c1.k + c2.x * c2.k + c3.x * c3.k) / (c1.k + c2.k + c3.k);
    const wy = (c1.y * c1.k + c2.y * c2.k + c3.y * c3.k) / (c1.k + c2.k + c3.k);
    const angle4 = atan2(wy - cy, wx - cx);
    const dFromCenter = sqrt((wx - cx) * (wx - cx) + (wy - cy) * (wy - cy));

    if (dFromCenter + r4 <= baseR * 1.02) {
      const newCircle = { x: wx, y: wy, r: r4, k: k4, gen: gen };
      circles.push(newCircle);
      queue.push([c1, c2, newCircle]);
      queue.push([c2, c3, newCircle]);
      queue.push([c3, c1, newCircle]);
    }
  }

  // Render all circles with glowing brand strokes
  noFill();
  for (let i = 0; i < circles.length; i++) {
    const c = circles[i];
    const frac = c.gen / maxGen;
    const dRel = sqrt((c.x - cx) * (c.x - cx) + (c.y - cy) * (c.y - cy)) / baseR;
    const h = (hue + frac * 0.45 + dRel * 0.25) % 1.0;
    const alpha = 0.35 + 0.65 * (1.0 - frac * 0.6);
    const wLine = max(0.8, stroke_w * (1.0 - frac * 0.5));

    strokeWeight(wLine);
    stroke(hsl(h, 0.88, 0.62, alpha));
    circle(c.x, c.y, c.r * 2.0);
  }
}
