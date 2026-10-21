/* The node sphere on nodes/: every node is a real link on a turning sphere. Labels never overlap (the nearest win), drag to turn it, tap a name to open its page. */
(function () {
  var root = document.getElementById('ns'), stage = document.getElementById('ns-stage');
  if (!root || !stage) return;
  document.documentElement.classList.add('js');
  var anchors = Array.prototype.slice.call(stage.querySelectorAll('.ns-a')), n = anchors.length;
  var reduce = window.matchMedia && matchMedia('(prefers-reduced-motion: reduce)').matches;
  var pts = anchors.map(function (_, i) {            // Fibonacci sphere: an even spread
    var y = 1 - (i + 0.5) * 2 / n, r = Math.sqrt(1 - y * y), t = i * Math.PI * (3 - Math.sqrt(5));
    return [Math.cos(t) * r, y, Math.sin(t) * r];
  });
  var wid = anchors.map(function (a) { return a.textContent.length * 7.2 + 16; });
  var fam = anchors.map(function (a) { return a.getAttribute('data-fam'); });
  var on = anchors.map(function () { return 0; });
  var xs = new Float32Array(n), ys = new Float32Array(n), zs = new Float32Array(n), order = [];
  for (var k = 0; k < n; k++) order.push(k);
  var yaw = 0.4, pitch = -0.2, vyaw = 0, vpitch = 0, R = 300, filter = '';
  var AUTO = reduce ? 0 : 0.00011;                    // radians per ms
  var drag = null, moved = 0, hover = false, visible = true, raf = 0, last = 0, rect = [];

  function size() {
    var w = stage.clientWidth, narrow = w < 560;
    R = narrow ? Math.min(w * 0.33, 150) : Math.min(w * 0.34, 380);
    stage.style.setProperty('--ns-size', Math.round(R * 2 + (narrow ? 70 : 90)) + 'px');
    stage.style.setProperty('--ns-r', R + 'px');
  }
  function draw() {
    var cy = Math.cos(yaw), sy = Math.sin(yaw), cp = Math.cos(pitch), sp = Math.sin(pitch), i;
    for (i = 0; i < n; i++) {
      var p = pts[i], x = p[0] * cy + p[2] * sy, z = -p[0] * sy + p[2] * cy;
      ys[i] = p[1] * cp - z * sp; zs[i] = p[1] * sp + z * cp; xs[i] = x;
    }
    order.sort(function (a, b) { return zs[b] - zs[a]; });
    rect.length = 0;
    var h = 22, pad = 6;
    for (var j = 0; j < n; j++) {
      i = order[j];
      var want = 0;
      if (zs[i] > 0.12 && (!filter || fam[i] === filter)) {
        var cx = xs[i] * R, cyy = ys[i] * R, w = wid[i] + pad, ok = true;
        for (var q = 0; q < rect.length; q++) {
          var r = rect[q];
          if (Math.abs(cx - r[0]) < (w + r[2]) / 2 && Math.abs(cyy - r[1]) < h) { ok = false; break; }
        }
        if (ok) { rect.push([cx, cyy, w]); want = zs[i] > 0.62 ? 2 : 1; anchors[i].style.transform = 'translate3d(' + (cx - wid[i] / 2).toFixed(1) + 'px,' + (cyy - h / 2).toFixed(1) + 'px,0)'; }
      }
      if (want !== on[i]) {
        var a = anchors[i];
        if (want && !on[i]) a.style.transform = 'translate3d(' + ((xs[i] * R) - wid[i] / 2).toFixed(1) + 'px,' + ((ys[i] * R) - h / 2).toFixed(1) + 'px,0)';
        a.classList.toggle('on', want > 0); a.classList.toggle('near', want === 2);
        a.tabIndex = want ? 0 : -1;
        on[i] = want;
      }
    }
  }
  function tick(t) {
    raf = 0;
    var dt = last ? Math.min(48, t - last) : 16; last = t;
    if (!drag) {
      yaw += vyaw * dt + (hover ? 0 : AUTO * dt); pitch += vpitch * dt;
      var f = Math.pow(0.0025, dt / 1000);              // inertia decays to nothing in about a second
      vyaw *= f; vpitch *= f;
      if (Math.abs(vyaw) < 1e-6) vyaw = 0; if (Math.abs(vpitch) < 1e-6) vpitch = 0;
    }
    pitch = Math.max(-1.2, Math.min(1.2, pitch));
    draw();
    if (drag || vyaw || vpitch || (visible && !hover && AUTO)) raf = requestAnimationFrame(tick); else last = 0;
  }
  function go() { if (!raf) raf = requestAnimationFrame(tick); }

  stage.addEventListener('pointerdown', function (e) {
    drag = { x: e.clientX, y: e.clientY, id: e.pointerId, t: performance.now() };
    moved = 0; vyaw = 0; vpitch = 0; go();
  });
  window.addEventListener('pointermove', function (e) {
    if (!drag || e.pointerId !== drag.id) return;
    var now = performance.now(), dt = Math.max(1, now - drag.t), dx = e.clientX - drag.x, dy = e.clientY - drag.y;
    moved += Math.abs(dx) + Math.abs(dy);
    if (moved > 6) stage.classList.add('is-drag');
    var k = 1.15 / R;                                    // the surface follows the finger
    yaw += dx * k; pitch += dy * k;
    vyaw = vyaw * 0.6 + (dx * k / dt) * 0.4; vpitch = vpitch * 0.6 + (dy * k / dt) * 0.4;
    drag.x = e.clientX; drag.y = e.clientY; drag.t = now;
  });
  function up() {
    if (!drag) return;
    if (performance.now() - drag.t > 90) { vyaw = 0; vpitch = 0; }   // held still before letting go: no throw
    drag = null; setTimeout(function () { moved = 0; }, 0); stage.classList.remove('is-drag'); last = 0; go();
  }
  window.addEventListener('pointerup', up); window.addEventListener('pointercancel', up);
  stage.addEventListener('click', function (e) { if (moved > 6) { e.preventDefault(); e.stopPropagation(); } }, true);
  stage.addEventListener('mouseover', function (e) { if (e.target.closest && e.target.closest('.ns-a')) hover = true; });
  stage.addEventListener('mouseout', function (e) { if (e.target.closest && e.target.closest('.ns-a')) { hover = false; go(); } });
  stage.addEventListener('focusin', function () { hover = true; });
  stage.addEventListener('focusout', function () { hover = false; go(); });
  stage.addEventListener('keydown', function (e) {
    var d = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] }[e.key];
    if (d && e.target === stage) { yaw += d[0] * 0.2; pitch += d[1] * 0.2; draw(); e.preventDefault(); }
  });
  stage.tabIndex = 0; stage.setAttribute('aria-label', 'Node sphere: drag or use the arrow keys to turn it');
  if ('IntersectionObserver' in window) new IntersectionObserver(function (es) { visible = es[0].isIntersecting; last = 0; go(); }).observe(stage);
  window.addEventListener('resize', function () { size(); draw(); });

  var chips = root.querySelectorAll('.np-chip[data-fam]'), view = root.querySelector('.ns-view');
  Array.prototype.forEach.call(chips, function (c) {
    c.addEventListener('click', function () {
      Array.prototype.forEach.call(chips, function (o) { o.setAttribute('aria-pressed', o === c ? 'true' : 'false'); });
      filter = c.getAttribute('data-fam') || '';
      root.removeAttribute('data-view'); view.setAttribute('aria-pressed', 'false'); view.textContent = 'List';
      draw(); go();
    });
  });
  view.addEventListener('click', function () {
    var list = root.getAttribute('data-view') !== 'list';
    if (list) root.setAttribute('data-view', 'list'); else root.removeAttribute('data-view');
    view.setAttribute('aria-pressed', list ? 'true' : 'false'); view.textContent = list ? 'Sphere' : 'List';
    if (!list) { size(); draw(); go(); }
  });
  size(); draw(); go();
})();
