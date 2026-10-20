/* Philosophy journey. Scroll position is the story: ideas fly in, connect into a mind, become a patch, settle onto a loop and merge into the mark.
   One continuous value s (0..5) is smoothed with a damped spring; every bubble reads its pose from the two scenes either side of s.
   Bubbles can be dragged (they spring home) or tapped (their neighbours light up). Reduced motion keeps the scroll scrub and drops drift and orbit. */
(function () {
  'use strict';
  var sec = document.getElementById('journey'); if (!sec) return;
  var stage = sec.querySelector('.ph-stage'), lines = sec.querySelector('.ph-lines'), flow = sec.querySelector('.ph-flow');
  var loopSvg = sec.querySelector('.ph-loop'), mark = sec.querySelector('.ph-mark'), endBox = sec.querySelector('.ph-end'), hint = sec.querySelector('.ph-hint');
  var caps = [].slice.call(sec.querySelectorAll('.ph-cap')), ball = sec.querySelector('.ph-ball'), lobeA = sec.querySelector('.ph-lobe-a'), lobeB = sec.querySelector('.ph-lobe-b');
  var navs = [].slice.call(sec.querySelectorAll('.pn'));
  var bubs = [].slice.call(sec.querySelectorAll('.pb[data-id]'));
  var N = 8, human = bubs[8], machine = bubs[9];
  var still = matchMedia('(prefers-reduced-motion: reduce)').matches;
  var NS = 'http://www.w3.org/2000/svg';
  var edges = (sec.dataset.edges || '').split(',').filter(Boolean).map(function (x) { var a = x.split('-'); return [+a[0], +a[1]]; });
  var lineEls = edges.map(function () { var l = document.createElementNS(NS, 'line'); lines.appendChild(l); return l; });
  var adj = bubs.slice(0, N).map(function () { return []; });
  edges.forEach(function (e, k) { adj[e[0]].push([e[1], k]); adj[e[1]].push([e[0], k]); });
  // the mind map, in the page's original 600 x 410 coordinates, one point per bubble (same order as the generator's NODES)
  var MIND = [[300, 58], [100, 138], [500, 138], [58, 270], [300, 208], [546, 270], [190, 350], [410, 350]];

  var bs = 1, markLen = 1, portrait = false, W = 0, H = 0, top = 0, cx = 0, cy = 0, A = 0, mindPts = [], scatterPts = [], loopTs = [];
  var s = 0, sT = 0, orbit = 0, time = 0, clock = 0, running = false, vis = true;
  var offs = bubs.map(function () { return { x: 0, y: 0, vx: 0, vy: 0 }; });
  var drag = null, focus = -1, focusT = 0;

  function hash(n) { var x = Math.sin(n * 127.1 + 311.7) * 43758.5453; return x - Math.floor(x); }
  function sm(t) { t = Math.min(1, Math.max(0, t)); return t * t * (3 - 2 * t); }
  function mix(a, b, t) { return a + (b - a) * t; }
  function lem(t) { var c = Math.cos(t), d = 1 + c * c; return [cx + A * 1.0 * Math.sin(t) / d, cy + A * Math.sin(t) * c / d]; }

  function measure() {
    W = stage.clientWidth; H = stage.clientHeight;
    var hd = document.querySelector('.site-header'); top = (hd ? hd.offsetHeight : 60) + (W < 680 ? 140 : 120);
    var cw = sec.querySelector('.ph-caps'); if (cw) { var ch = 0; caps.forEach(function (c) { ch = Math.max(ch, c.offsetHeight); }); top = Math.max(top, cw.offsetTop + ch + 16); }   // the figure starts below the tallest caption, whatever the width
    var hu = H - top - 96; portrait = W < H * 0.9;
    bs = W < 680 ? 0.78 : 1;                                                    // phones: smaller icons
    var yTop = top + 36 * bs + 8, yBot = H - (W < 680 ? 200 : 120), span = Math.max(160, yBot - yTop);   // the figure lives between the caption and the controls below
    cx = W / 2; cy = yTop + span / 2;
    A = portrait ? W * 0.34 : Math.min(W * 0.27, hu * 0.62);
    var sc = portrait ? Math.min(W * 0.78 / 410, span / 500) : Math.min(W * 0.8 / 600, span / 310);
    mindPts = MIND.map(function (p) { var x = p[0] - 300, y = p[1] - 205; return portrait ? [cx + y * sc, cy + x * sc] : [cx + x * sc, cy + y * sc]; });
    scatterPts = bubs.map(function (b, i) { return [W * (0.12 + 0.76 * hash(i + 1)), top + 40 + (H - top - (W < 680 ? 210 : 140)) * hash(i + 21)]; });
    loopTs = bubs.map(function (b, i) { return i * Math.PI * 2 / N + 0.4; });
    // the mark: a lemniscate path sized to the loop
    var d = '', k;
    for (k = 0; k <= 480; k++) { var t = k / 480 * Math.PI * 2, c = Math.cos(t), q = 1 + c * c; d += (k ? 'L' : 'M') + (100 * Math.sin(t) / q).toFixed(3) + ' ' + (100 * Math.sin(t) * c / q).toFixed(3); }
    mark.setAttribute('d', d + 'Z'); mark.setAttribute('stroke-width', (87 / 338 * 100).toFixed(3)); markLen = mark.getTotalLength();
    loopSvg.style.width = (A * 240 / 100) + 'px'; loopSvg.style.height = (A * 96 / 100) + 'px';
    loopSvg.style.transform = 'translate3d(' + (cx - A * 1.2).toFixed(1) + 'px,' + (cy - A * 0.48).toFixed(1) + 'px,0)';
    loopSvg.setAttribute('viewBox', '-120 -48 240 96');
    var sv = sec.querySelector('.ph-edges'); sv.setAttribute('viewBox', '0 0 ' + W + ' ' + H);
  }

  // scene k pose for bubble i: [x, y, scale, opacity]
  function scene(k, i) {
    var a = i / 9 * 6.283 + 0.7, far = Math.max(W, H) * 0.62, x, y, sc = 1, op = 1;
    if (i >= N) {                                                             // the two lobe icons: only in the loop scene
      var lobe = i === N ? 1 : -1, on = k === 4 ? 1 : 0;
      return [k >= 4 ? cx + lobe * A * 0.5 : cx, k >= 4 ? cy : cy, on * 1.1, on];
    }
    if (k === 0) { x = cx + Math.cos(a) * far * 0.9; y = cy + Math.sin(a) * far * 0.7; sc = 0.5; op = 0; }
    else if (k === 1) { x = scatterPts[i][0]; y = scatterPts[i][1]; sc = 0.9 + 0.2 * hash(i + 40); }
    else if (k === 2) { x = mindPts[i][0]; y = mindPts[i][1]; }
    else if (k === 3) { x = mindPts[i][0]; y = mindPts[i][1]; sc = 1.08; }
    else { var p = lem(loopTs[i] + orbit); x = p[0]; y = p[1]; sc = k === 5 ? 0.3 : (portrait ? 0.78 : 1); op = k === 5 ? 0.95 : 1; }
    return [x, y, sc, op];
  }
  function pose(i) {
    var k = Math.min(4, Math.floor(s)), f = s - k, g = Math.min(1, Math.max(0, f * 1.3 - 0.3 * (i / 9))), t = sm(g);
    var p = scene(k, i), q = scene(k + 1, i);
    return [mix(p[0], q[0], t), mix(p[1], q[1], t), mix(p[2], q[2], t), mix(p[3], q[3], t)];
  }

  var cur = [];
  function frame(now) {
    var dt = Math.min(0.05, (now - clock) / 1000 || 0.016); clock = now; time += dt;
    var rect = sec.getBoundingClientRect(), span = Math.max(1, rect.height - innerHeight);
    sT = Math.min(5, Math.max(0, -rect.top / span * 5));
    s += (sT - s) * (1 - Math.exp(-dt * (still ? 60 : 8)));
    if (!still) orbit += dt * 0.32 * sm(s - 3.2);
    var k = Math.round(s);
    if (sec.dataset.s !== String(k)) { sec.dataset.s = String(k); navs.forEach(function (n, j) { n.classList.toggle('is-on', j === k - 1); }); }
    var drift = still ? 0 : 1, sceneMove = s > 0.8 && s < 3.4 ? 1 : 0;
    bubs.forEach(function (b, i) {
      var p = pose(i), o = offs[i];
      if (drag !== i) { var e = Math.exp(-dt * 7); o.x *= e; o.y *= e; }
      var wob = drift * (s < 2 ? 1 - Math.min(1, Math.max(0, s - 1)) : 0);
      var dx = Math.sin(time * 0.8 + i * 1.7) * 9 * wob, dy = Math.cos(time * 0.6 + i * 2.3) * 9 * wob;
      var x = p[0] + dx + o.x, y = p[1] + dy + o.y;
      cur[i] = [x, y];
      var rad = mix(50, 24, sm(1 - Math.abs(s - 3) * 2.4));
      b.style.transform = 'translate3d(' + x.toFixed(1) + 'px,' + y.toFixed(1) + 'px,0) scale(' + (p[2] * bs).toFixed(3) + ')';
      b.style.opacity = p[3].toFixed(3); b.style.borderRadius = rad.toFixed(1) + '%';
      b.style.pointerEvents = p[3] > 0.5 && sceneMove ? 'auto' : 'none';
    });
    // edges: drawn in as the mind forms, kept for the patch, gone when the loop takes over
    var eop = s < 1.2 ? 0 : s < 2 ? (s - 1.2) / 0.8 : s <= 3 ? 1 : s < 3.7 ? (3.7 - s) / 0.7 : 0;
    edges.forEach(function (e, j) {
      var l = lineEls[j], a = cur[e[0]], b = cur[e[1]]; if (!a || !b) return;
      l.setAttribute('x1', a[0].toFixed(1)); l.setAttribute('y1', a[1].toFixed(1)); l.setAttribute('x2', b[0].toFixed(1)); l.setAttribute('y2', b[1].toFixed(1));
      l.style.opacity = (focus >= 0 && l.classList.contains('on') ? 1 : eop * (focus >= 0 ? 0.35 : 0.8)).toFixed(3);
    });
    // a thought travels along the connections
    if (eop > 0.05 && !still) {
      var seg = (time * 0.9) % edges.length, j = Math.floor(seg), tt = seg - j, e2 = edges[(j * 5) % edges.length], a2 = cur[e2[0]], b2 = cur[e2[1]];
      if (a2 && b2) { flow.setAttribute('cx', mix(a2[0], b2[0], tt).toFixed(1)); flow.setAttribute('cy', mix(a2[1], b2[1], tt).toFixed(1)); }
      flow.style.opacity = eop.toFixed(2);
    } else flow.style.opacity = 0;
    // the mark draws itself once the bubbles are on the loop
    var draw = sm((s - 3.35) / 1.1);
    mark.style.strokeDasharray = markLen; mark.style.strokeDashoffset = (markLen * (1 - draw)).toFixed(2);
    if (draw > 0.98) {                       // the original mark's ember ball rides the loop, 4 s a lap
      var u = (time / 4) % 1, e = u + 0.035 * Math.sin(u * Math.PI * 4), pt = mark.getPointAtLength((((e % 1) + 1) % 1) * markLen);
      ball.setAttribute('cx', pt.x.toFixed(2)); ball.setAttribute('cy', pt.y.toFixed(2)); ball.style.opacity = Math.min(1, (draw - 0.98) * 50);
    } else ball.style.opacity = 0;
    caps.forEach(function (c, j) { c.classList.toggle('is-on', j === Math.max(0, Math.min(4, k - 1))); });
    lobeA.style.transform = 'translate3d(' + (cx - A * 0.62 - 40).toFixed(1) + 'px,' + (cy + 62 * (A / 140)).toFixed(1) + 'px,0)';
    lobeB.style.transform = 'translate3d(' + (cx + A * 0.62 - 12).toFixed(1) + 'px,' + (cy + 62 * (A / 140)).toFixed(1) + 'px,0)';
    loopSvg.style.opacity = s < 0.5 ? 0.1 : Math.max(0.1, draw);
    // the dock arrives with the mark
    var eo = sm((s - 4.35) / 0.65); endBox.style.opacity = eo.toFixed(3); endBox.classList.toggle('is-on', eo > 0.6);
    hint.style.visibility = s > 0.3 ? 'hidden' : '';
    if (focus >= 0 && now - focusT > 2600 && !drag && drag !== 0) setFocus(-1);
    if (vis) requestAnimationFrame(frame); else running = false;
  }
  function start() { if (!running) { running = true; clock = performance.now(); requestAnimationFrame(frame); } }

  function setFocus(i) {
    focus = i; focusT = performance.now();
    sec.classList.toggle('has-focus', i >= 0);
    var on = {}; if (i >= 0) { on[i] = 1; adj[i].forEach(function (x) { on[x[0]] = 1; }); }
    bubs.forEach(function (b, j) { b.classList.toggle('on', !!on[j]); });
    lineEls.forEach(function (l, j) { l.classList.toggle('on', i >= 0 && (edges[j][0] === i || edges[j][1] === i)); });
  }
  bubs.slice(0, N).forEach(function (b, i) {
    b.addEventListener('pointerenter', function (e) { if (e.pointerType === 'mouse') setFocus(i); });
    b.addEventListener('pointerleave', function (e) { if (e.pointerType === 'mouse' && drag === null) setFocus(-1); });
    b.addEventListener('focus', function () { setFocus(i); });
    b.addEventListener('blur', function () { setFocus(-1); });
    b.addEventListener('pointerdown', function (e) {
      drag = i; var sx = e.clientX, sy = e.clientY, o0 = { x: offs[i].x, y: offs[i].y }, moved = false;
      try { b.setPointerCapture(e.pointerId); } catch (x) {}
      setFocus(i);
      function mv(ev) { offs[i].x = o0.x + ev.clientX - sx; offs[i].y = o0.y + ev.clientY - sy; moved = true; }
      function up() { drag = null; b.removeEventListener('pointermove', mv); b.removeEventListener('pointerup', up); b.removeEventListener('pointercancel', up); focusT = performance.now(); }
      b.addEventListener('pointermove', mv); b.addEventListener('pointerup', up); b.addEventListener('pointercancel', up);
    });
  });

  // chapter icons jump to a scene; arrow keys step through them
  function go(n) { var span = sec.offsetHeight - innerHeight, y = sec.getBoundingClientRect().top + scrollY + span * (n / 5); scrollTo({ top: y, behavior: still ? 'auto' : 'smooth' }); }
  navs.forEach(function (n, j) { n.addEventListener('click', function () { go(j + 1); }); });
  addEventListener('keydown', function (e) {
    if (e.target && /input|textarea|select/i.test(e.target.tagName)) return;
    var r = sec.getBoundingClientRect(); if (r.top > 0 || r.bottom < innerHeight) return;
    if (e.key === 'ArrowRight' || e.key === 'ArrowDown' || e.key === 'PageDown') { if (Math.round(sT) < 5) { e.preventDefault(); go(Math.floor(sT + 0.01) + 1); } }
    else if (e.key === 'ArrowLeft' || e.key === 'ArrowUp' || e.key === 'PageUp') { if (sT > 0.05) { e.preventDefault(); go(Math.max(0, Math.ceil(sT - 0.01) - 1)); } }
  });

  new IntersectionObserver(function (es) { vis = es[0].isIntersecting; if (vis) start(); }, { rootMargin: '200px' }).observe(sec);
  addEventListener('resize', measure); if (document.fonts && document.fonts.ready) document.fonts.ready.then(measure); if (window.ResizeObserver) new ResizeObserver(measure).observe(sec.querySelector('.ph-caps') || stage);
  measure(); s = sT = 0; start();
})();
