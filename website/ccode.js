/* Creative coders hero: three maths scenes drawn live on one 2D canvas in the free area of the stage.
   Orbit = Lorenz attractor, Bloom = phyllotaxis, Cells = Rule 110 family. Pointer x / y change the two numbers that matter in each scene;
   press to kick it. The bubbles (artists.js) send 'ax:pick' to switch scene. Reduced motion draws one still frame. */
(function () {
  'use strict';
  var stage = document.querySelector('.ax-stage[data-hero="math"]'); if (!stage) return;
  var canvas = stage.querySelector('.ax-gl'), c = canvas.getContext('2d'); if (!c) return;
  var still = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  var SIG = '167,177,255', SIG5 = '105,116,220', EMBER = '255,122,89';
  var st = { m: [0.5, 0.5], tm: [0.5, 0.5], p: 0, scene: 0, last: 0, idle: 9, vis: true, t: 0 };
  var box = { x: 0, y: 0, w: 100, h: 100 }, dpr = 1;

  /* Lorenz: three nearby starts so the divergence shows */
  var L = [[0.1, 0, 0], [0.1001, 0, 0], [0.1, 0.0001, 0]].map(function (p) { return { p: p, tr: [] }; });
  function lorenzStep(o, sg, rho, dt) {
    var x = o.p[0], y = o.p[1], z = o.p[2], b = 8 / 3;
    o.p = [x + sg * (y - x) * dt, y + (x * (rho - z) - y) * dt, z + (x * y - b * z) * dt];
    o.tr.push(o.p.slice()); if (o.tr.length > 520) o.tr.shift();
  }
  function drawLorenz(dt) {
    var sg = 8 + st.m[1] * 6, rho = 20 + st.m[0] * 20 + st.p * 10, n = (still || !st.warm) ? 3000 : 9; st.warm = 1;
    for (var k = 0; k < n; k++) for (var i = 0; i < L.length; i++) lorenzStep(L[i], sg, rho, 0.006);
    var s = Math.min(box.w / 52, box.h / 56), cx = box.x + box.w / 2, cy = box.y + box.h / 2 + 24 * s, ang = st.t * 0.18, ca = Math.cos(ang), sa = Math.sin(ang);
    c.lineWidth = 1.6; c.lineCap = 'round'; c.lineJoin = 'round';
    for (var j = L.length - 1; j >= 0; j--) {
      var tr = L[j].tr; if (tr.length < 2) continue;
      for (var q = 1; q < tr.length; q++) {
        var a = tr[q - 1], b = tr[q], al = q / tr.length;
        c.strokeStyle = 'rgba(' + (j === 0 ? SIG : SIG5) + ',' + (al * al * (j === 0 ? 0.95 : 0.5)).toFixed(3) + ')';
        c.beginPath(); c.moveTo(cx + (a[0] * ca - a[1] * sa) * s, cy - a[2] * s); c.lineTo(cx + (b[0] * ca - b[1] * sa) * s, cy - b[2] * s); c.stroke();
      }
    }
    var h = L[0].p; c.fillStyle = 'rgb(' + EMBER + ')'; c.beginPath(); c.arc(cx + (h[0] * ca - h[1] * sa) * s, cy - h[2] * s, 4, 0, 6.2832); c.fill();
  }

  function drawBloom() {
    var N = 520, turn = 137.5 + (st.m[0] - 0.5) * 8 + (still ? 0 : Math.sin(st.t * 0.4) * 0.15), seeds = Math.round(160 + st.m[1] * 360 + st.p * 80);
    var R = Math.min(box.w, box.h) / 2 - 6, cx = box.x + box.w / 2, cy = box.y + box.h / 2, ga = turn * Math.PI / 180, rot = still ? 0 : st.t * 0.08;
    for (var i = 1; i < Math.min(N, seeds); i++) {
      var r = R * Math.sqrt(i / seeds), a = i * ga + rot, f = i / seeds;
      c.fillStyle = (i === 1 || i === 2) ? 'rgb(' + EMBER + ')' : 'rgba(' + (f > 0.6 ? SIG : SIG5) + ',' + (0.35 + f * 0.6).toFixed(2) + ')';
      c.beginPath(); c.arc(cx + Math.cos(a) * r, cy + Math.sin(a) * r, 1.2 + f * 3.4 * (R / 150), 0, 6.2832); c.fill();
    }
  }

  /* Rule 110 family: rows scroll upward, x picks the rule */
  var RULES = [110, 30, 90, 150], COLS = 0, ROWS = 0, grid = [], acc = 0, cell = 6;
  function seed() {
    cell = box.w < 330 ? 6 : 7; COLS = Math.max(20, Math.floor(box.w / cell)); ROWS = Math.max(12, Math.floor(box.h / cell)); grid = [];
    var row = new Uint8Array(COLS); row[COLS - 2] = 1; row[COLS >> 1] = 1; grid.push(row);
    for (var i = 1; i < ROWS; i++) grid.push(next(grid[i - 1]));
  }
  function next(prev) {
    var rule = RULES[Math.min(3, Math.floor(st.m[0] * 4))], out = new Uint8Array(COLS);
    for (var i = 0; i < COLS; i++) out[i] = (rule >> ((prev[(i + COLS - 1) % COLS] << 2) | (prev[i] << 1) | prev[(i + 1) % COLS])) & 1;
    if (st.p > 0.5 && Math.random() < 0.5) out[Math.floor(st.m[0] * COLS)] = 1;
    var last = grid.length ? grid[grid.length - 1] : prev, live = 0; for (var k = 0; k < COLS; k++) live += out[k]; if (live === 0) out[Math.floor(Math.random() * COLS)] = 1;
    return out;
  }
  function drawCells(dt) {
    if (!grid.length) seed();
    acc += dt; var rate = 0.07 - st.m[1] * 0.05; while (acc > rate) { acc -= rate; grid.push(next(grid[grid.length - 1])); if (grid.length > ROWS) grid.shift(); if (still) break; }
    var ox = box.x + (box.w - COLS * cell) / 2, oy = box.y + (box.h - ROWS * cell) / 2;
    for (var y = 0; y < grid.length; y++) {
      var al = 0.25 + 0.75 * (y / grid.length), row = grid[y];
      for (var x = 0; x < COLS; x++) if (row[x]) { c.fillStyle = 'rgba(' + (y === grid.length - 1 ? EMBER : SIG) + ',' + (y === grid.length - 1 ? 1 : al).toFixed(2) + ')'; c.fillRect(ox + x * cell + 0.5, oy + y * cell + 0.5, cell - 1.5, cell - 1.5); }
    }
  }

  function fit() {
    var W = stage.clientWidth, H = stage.clientHeight; if (!W || !H) return;
    var sr = stage.getBoundingClientRect(), copy = stage.querySelector('.ax-copy'), low = stage.querySelector('.ax-low'), cta = stage.querySelector('.ax-cta');
    var x0 = 16, x1 = W - 16, y0, y1;
    if (W >= 900) {
      var h1 = copy && copy.querySelector('h1'), rg = document.createRange(); if (h1) rg.selectNodeContents(h1);
      var tr = h1 ? rg.getBoundingClientRect().right - sr.left : 0, lowTop = Math.min(low ? low.getBoundingClientRect().top - sr.top : H, cta ? cta.getBoundingClientRect().top - sr.top : H) - 16;
      if (W - 28 - (tr + 32) >= 340) { x0 = tr + 32; x1 = W - 28; y0 = 72; y1 = lowTop; }                       // beside the headline
      else { x0 = 24; x1 = W - 24; y0 = (copy ? copy.getBoundingClientRect().bottom - sr.top : 0) + 12; y1 = lowTop; }   // headline too wide: under it
    }
    else { y0 = (copy ? copy.getBoundingClientRect().bottom - sr.top : 0) + 10; y1 = (low ? low.getBoundingClientRect().top - sr.top : H) - 12; }
    y1 = Math.max(y1, y0 + 120);
    box = { x: x0, y: y0, w: x1 - x0, h: y1 - y0 }; grid = [];
    if (still) draw(0.016);
  }
  function draw(dt) {
    st.t += dt; st.idle += dt;
    if (!still && st.idle > 2.2) st.tm = [0.5 + 0.35 * Math.sin(st.t * 0.31), 0.5 + 0.3 * Math.sin(st.t * 0.23 + 1)];
    var a = 1 - Math.exp(-dt * 5); st.m[0] += (st.tm[0] - st.m[0]) * a; st.m[1] += (st.tm[1] - st.m[1]) * a;
    st.p += ((st.tp || 0) - st.p) * (1 - Math.exp(-dt * 10));
    dpr = Math.min(window.devicePixelRatio || 1, 2); var cw = canvas.clientWidth, ch = canvas.clientHeight, w = Math.round(cw * dpr), h = Math.round(ch * dpr);
    if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
    c.setTransform(dpr, 0, 0, dpr, 0, 0); c.clearRect(0, 0, cw, ch);
    if (st.scene === 0) drawLorenz(dt); else if (st.scene === 1) drawBloom(); else drawCells(dt);
  }
  function loop(now) { var dt = Math.min(0.05, (now - st.last) / 1000 || 0.016); st.last = now; if (st.vis && !document.hidden) draw(dt); requestAnimationFrame(loop); }
  addEventListener('resize', fit); if (document.fonts && document.fonts.ready) document.fonts.ready.then(fit);
  fit();
  if (still) draw(0.016); else requestAnimationFrame(loop);
  if ('IntersectionObserver' in window) new IntersectionObserver(function (es) { st.vis = es[0].isIntersecting; }).observe(stage);

  function aim(e) {
    var x = (e.clientX - canvas.getBoundingClientRect().left - box.x) / box.w, y = (e.clientY - canvas.getBoundingClientRect().top - box.y) / box.h;
    st.tm = [Math.max(0, Math.min(1, x)), Math.max(0, Math.min(1, 1 - y))]; st.idle = 0; stage.classList.add('touched'); if (still) draw(0.016);
  }
  stage.addEventListener('pointermove', aim, { passive: true });
  stage.addEventListener('pointerdown', function (e) { if (e.target.closest('button, a')) return; st.tp = 1; aim(e); }, { passive: true });
  addEventListener('pointerup', function () { st.tp = 0; }, { passive: true });
  addEventListener('pointercancel', function () { st.tp = 0; }, { passive: true });
  stage.addEventListener('ax:pick', function (e) { st.scene = e.detail; st.idle = 0; grid = []; if (still) draw(0.016); });
})();
