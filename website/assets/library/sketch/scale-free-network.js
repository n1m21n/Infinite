// Scale-Free Network - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Barabasi-Albert growth with a fixed hash, so scrubbing is exact. A new node every `rate` seconds.
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
