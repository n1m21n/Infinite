"""Sketch devices for the Infinite Library: p5-style JS for a Sketch node's code editor.
Each is a pure function of (t, params, size) like the built-in presets (src/core/sketch/SketchPresets.h)."""

SKETCHES = [
{"id": "lorenz-attractor", "name": "Lorenz Attractor", "kind": "Generator", "tags": ["chaos", "attractor", "math"], "t": 14.0,
 "blurb": "The butterfly of chaos, integrated live. A glowing trail winds between two lobes and never repeats.",
 "code": r'''// Lorenz attractor: dx=s(y-x), dy=x(r-z)-y, dz=xy-bz, integrated from t=0 each frame.
param("rho", 28, 10, 40);
param("trail", 1400, 200, 4000);
param("zoom", 1, 0.5, 2);

function draw(t) {
  background(0.04, 0.04, 0.08);
  const s = 10, b = 8 / 3, dt = 0.006;
  let x = 0.1, y = 0, z = 0;
  const steps = floor(t * 120) + 400;
  const pts = [];
  for (let i = 0; i < steps; i++) {
    const dx = s * (y - x), dy = x * (rho - z) - y, dz = x * y - b * z;
    x += dx * dt; y += dy * dt; z += dz * dt;
    pts.push([x, y, z]);
  }
  const from = max(0, pts.length - trail);
  const k = min(width, height) / 60 * zoom;
  noFill();
  strokeWeight(2.2);
  for (let i = from + 1; i < pts.length; i++) {
    const a = pts[i - 1], p = pts[i];
    const f = (i - from) / (pts.length - from);
    stroke(hsl(0.58 - 0.5 * f, 0.8, 0.35 + 0.35 * f));
    line(width / 2 + a[0] * k, height * 0.9 - a[2] * k, width / 2 + p[0] * k, height * 0.9 - p[2] * k);
  }
}
'''},
{"id": "lotka-volterra", "name": "Lotka-Volterra", "kind": "Generator", "tags": ["ecology", "math", "phase"], "t": 20.0,
 "blurb": "Predators and prey chasing each other: the population curves on top, the phase loop they trace below.",
 "code": r'''// Predator-prey: dx = a x - b x y, dy = d x y - c y.
param("growth", 1.0, 0.3, 2);
param("hunt", 0.4, 0.1, 1);
param("span", 24, 8, 60);

function draw(t) {
  background(0.96, 0.95, 0.9);
  const a = growth, b = hunt, c = 1.0, d = 0.25, dt = 0.01;
  let x = 3, y = 1.2;
  const n = floor(t * 100) + 1;
  const X = [], Y = [];
  for (let i = 0; i < n; i++) {
    const dx = a * x - b * x * y, dy = d * x * y - c * y;
    x += dx * dt; y += dy * dt;
    X.push(x); Y.push(y);
  }
  const W = width, H = height, top = H * 0.46;
  noFill(); strokeWeight(3);
  const first = max(0, n - floor(span * 100));
  stroke(0.2, 0.45, 0.75);
  beginShape();
  for (let i = first; i < n; i += 2) vertex(W * 0.06 + (i - first) / (n - first) * W * 0.88, top - X[i] * H * 0.035);
  endShape();
  stroke(0.85, 0.35, 0.25);
  beginShape();
  for (let i = first; i < n; i += 2) vertex(W * 0.06 + (i - first) / (n - first) * W * 0.88, top - Y[i] * H * 0.07);
  endShape();
  stroke(0.15, 0.15, 0.2);
  strokeWeight(2);
  beginShape();
  for (let i = max(0, n - 700); i < n; i += 2) vertex(W / 2 + (X[i] - 4) * H * 0.07, H * 0.9 - Y[i] * H * 0.12);
  endShape();
  noStroke(); fill(0.85, 0.35, 0.25);
  circle(W / 2 + (X[n - 1] - 4) * H * 0.07, H * 0.9 - Y[n - 1] * H * 0.12, 14);
}
'''},
{"id": "scale-free-network", "name": "Scale-Free Network", "kind": "Generator", "tags": ["network", "graph", "growth"], "t": 12.0,
 "blurb": "A network that grows by preferential attachment: the rich get richer, hubs emerge, and the graph breathes.",
 "code": r'''// Barabasi-Albert growth with a fixed hash, so scrubbing is exact. A new node every `rate` seconds.
param("nodes", 70, 10, 160);
param("rate", 6, 1, 20);
param("pull", 1.0, 0.2, 3);

function h(i) { const s = sin(i * 127.1 + 311.7) * 43758.5453; return s - floor(s); }

function draw(t) {
  background(0.05, 0.05, 0.09);
  const n = min(nodes, 3 + floor(t * rate));
  const deg = [], E = [];
  for (let i = 0; i < n; i++) deg.push(0);
  E.push([0, 1]); deg[0]++; deg[1]++;
  for (let i = 2; i < n; i++) {
    let tot = 0; for (let j = 0; j < i; j++) tot += deg[j];
    let r = h(i) * tot, k = 0;
    while (k < i - 1 && r > deg[k]) { r -= deg[k]; k++; }
    E.push([i, k]); deg[i]++; deg[k]++;
  }
  const px = [], py = [];
  const R = min(width, height) * 0.42;
  for (let i = 0; i < n; i++) {
    const a = h(i + 9) * TAU, r = R * sqrt(h(i + 31)) * (1 - 0.5 * min(1, deg[i] / 10)) ;
    px.push(width / 2 + r * cos(a + 0.12 * sin(t * 0.6 + i)));
    py.push(height / 2 + r * sin(a + 0.12 * cos(t * 0.5 + i)));
  }
  strokeWeight(1.4); stroke(0.55, 0.6, 0.85, 0.5);
  for (const e of E) line(px[e[0]], py[e[0]], px[e[1]], py[e[1]]);
  noStroke();
  for (let i = 0; i < n; i++) {
    fill(hsl(0.08 + 0.08 * min(1, deg[i] / 12), 0.85, 0.55 + 0.1 * pull * min(1, deg[i] / 12)));
    circle(px[i], py[i], 6 + 5 * pull * sqrt(deg[i]));
  }
}
'''},
{"id": "rule-110", "name": "Rule 110", "kind": "Generator", "tags": ["cellular", "automaton", "math"], "t": 4.0,
 "blurb": "A one-dimensional cellular automaton that is Turing-complete. Gliders scroll down the page, one generation at a time.",
 "code": r'''// Elementary cellular automaton. Change `rule` for 30, 90, 110, 184...
param("rule", 110, 0, 255);
param("cell", 6, 3, 16);
param("speed", 12, 1, 40);

function draw(t) {
  background(0.97, 0.96, 0.93);
  const cols = floor(width / cell), rows = floor(height / cell);
  const gen = floor(t * speed);
  let row = [];
  for (let i = 0; i < cols; i++) row.push(i == cols - 1 ? 1 : 0);
  noStroke();
  for (let g = 0; g < gen + rows; g++) {
    const y = g - gen;
    if (y >= 0 && y < rows) {
      for (let i = 0; i < cols; i++) if (row[i]) {
        fill(hsl(0.6 - 0.15 * (y / rows), 0.55, 0.3));
        rect(i * cell, y * cell, cell - 1, cell - 1);
      }
    }
    const nx = [];
    for (let i = 0; i < cols; i++) {
      const l = row[(i + cols - 1) % cols], c = row[i], r = row[(i + 1) % cols];
      nx.push((rule >> (l * 4 + c * 2 + r)) & 1);
    }
    row = nx;
  }
}
'''},
{"id": "clifford-attractor", "name": "Clifford Attractor", "kind": "Generator", "tags": ["chaos", "attractor", "generative"], "t": 6.0,
 "blurb": "A strange attractor drawn as a cloud of 20,000 points. Slowly drifting parameters keep reshaping the lace.",
 "code": r'''// x' = sin(a y) + c cos(a x), y' = sin(b x) + d cos(b y).
param("points", 9000, 1000, 30000);
param("drift", 0.15, 0, 1);
param("size", 1.6, 0.5, 4);

function draw(t) {
  background(0.03, 0.03, 0.06);
  const a = -1.4 + 0.25 * sin(t * drift), b = 1.6 + 0.2 * cos(t * drift * 0.8);
  const c = 1.0 + 0.2 * sin(t * drift * 0.6), d = 0.7;
  let x = 0.1, y = 0.1;
  const k = min(width, height) * 0.2;
  noStroke();
  for (let i = 0; i < points; i++) {
    const nx = sin(a * y) + c * cos(a * x), ny = sin(b * x) + d * cos(b * y);
    x = nx; y = ny;
    if (i > 20) {
      fill(hsl(0.55 + 0.25 * (i / points), 0.7, 0.62, 0.35));
      circle(width / 2 + x * k, height / 2 + y * k, size);
    }
  }
}
'''},
]
