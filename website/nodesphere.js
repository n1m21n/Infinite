/* The node sphere on nodes/: every node is a real link laid on a turning sphere. Drag sideways to turn it, tap a node to open its page. */
(function () {
  var root = document.getElementById('ns'), stage = document.getElementById('ns-stage');
  if (!root || !stage) return;
  document.documentElement.classList.add('js');
  var anchors = Array.prototype.slice.call(stage.querySelectorAll('.ns-a')), n = anchors.length;
  var orb = stage.querySelector('.ns-orb');
  var reduce = window.matchMedia && matchMedia('(prefers-reduced-motion: reduce)').matches;
  var pts = anchors.map(function (_, i) {            // Fibonacci sphere: even spread
    var y = 1 - (i + 0.5) * 2 / n, r = Math.sqrt(1 - y * y), t = i * Math.PI * (3 - Math.sqrt(5));
    return [Math.cos(t) * r, y, Math.sin(t) * r];
  });
  var yaw = 0.4, pitch = -0.25, vy = reduce ? 0 : 0.0016, vp = 0, R = 300, W = 0, H = 0;
  var drag = null, moved = 0, hover = false, visible = true, raf = 0;

  function size() {
    var w = stage.clientWidth, narrow = w < 560;
    R = narrow ? 300 : Math.min(w * 0.36, 420);
    H = Math.round(R * 2 + (narrow ? 60 : 90)); W = w;
    stage.style.setProperty('--ns-size', H + 'px'); stage.style.setProperty('--ns-r', R + 'px');
  }
  function draw() {
    var cy = Math.cos(yaw), sy = Math.sin(yaw), cp = Math.cos(pitch), sp = Math.sin(pitch);
    for (var i = 0; i < n; i++) {
      var p = pts[i], x = p[0] * cy + p[2] * sy, z = -p[0] * sy + p[2] * cy;
      var y = p[1] * cp - z * sp; z = p[1] * sp + z * cp;
      var d = (z + 1) / 2, s = 0.8 + 0.35 * d, a = anchors[i];
      a.style.transform = 'translate(-50%,-50%) translate(' + (x * R).toFixed(1) + 'px,' + (y * R).toFixed(1) + 'px) scale(' + s.toFixed(3) + ')';
      a.style.opacity = (d < 0.4 ? 0.06 : 0.25 + 0.75 * (d - 0.4) / 0.6).toFixed(2);
      a.style.zIndex = Math.round(d * 100);
      a.classList.toggle('is-back', d < 0.32); a.classList.toggle('is-front', d > 0.78);
    }
  }
  function tick() {
    raf = 0;
    if (!drag) { yaw += vy; pitch += vp; vp *= 0.94; if (Math.abs(vy) > 0.0016 || reduce) vy = reduce ? vy * 0.94 : vy + (0.0016 - vy) * 0.03; }
    pitch = Math.max(-1.1, Math.min(1.1, pitch));
    draw();
    if (visible && !hover && !reduce || drag || Math.abs(vy) > 0.0003 || Math.abs(vp) > 0.0003) raf = requestAnimationFrame(tick);
  }
  function go() { if (!raf) raf = requestAnimationFrame(tick); }

  stage.addEventListener('pointerdown', function (e) {
    if (e.target.closest && e.target.closest('.ns-a') && e.pointerType === 'mouse') { drag = { x: e.clientX, y: e.clientY, id: e.pointerId, link: true }; }
    else drag = { x: e.clientX, y: e.clientY, id: e.pointerId };
    moved = 0; vy = 0; vp = 0;
  });
  window.addEventListener('pointermove', function (e) {
    if (!drag || e.pointerId !== drag.id) return;
    var dx = e.clientX - drag.x, dy = e.clientY - drag.y; drag.x = e.clientX; drag.y = e.clientY;
    moved += Math.abs(dx) + Math.abs(dy);
    if (moved > 6) stage.classList.add('is-drag');
    yaw += dx / R * 1.2; pitch += dy / R * 1.0; vy = dx / R * 0.35; vp = dy / R * 0.2; go();
  });
  function up() { drag = null; setTimeout(function () { moved = 0; }, 0); stage.classList.remove('is-drag'); go(); }
  window.addEventListener('pointerup', up); window.addEventListener('pointercancel', up);
  stage.addEventListener('click', function (e) { if (moved > 6) { e.preventDefault(); e.stopPropagation(); moved = 0; } }, true);
  stage.addEventListener('mouseenter', function () { hover = true; });
  stage.addEventListener('mouseleave', function () { hover = false; go(); });
  stage.addEventListener('focusin', function () { hover = true; }); stage.addEventListener('focusout', function () { hover = false; go(); });
  stage.addEventListener('keydown', function (e) { if (e.key === 'ArrowLeft') { yaw -= 0.15; draw(); } else if (e.key === 'ArrowRight') { yaw += 0.15; draw(); } });
  if ('IntersectionObserver' in window) new IntersectionObserver(function (es) { visible = es[0].isIntersecting; go(); }).observe(stage);
  window.addEventListener('resize', function () { size(); draw(); });

  // family filter and sphere / list view
  var chips = root.querySelectorAll('.np-chip[data-fam]'), view = root.querySelector('.ns-view');
  Array.prototype.forEach.call(chips, function (c) {
    c.addEventListener('click', function () {
      Array.prototype.forEach.call(chips, function (o) { o.setAttribute('aria-pressed', o === c ? 'true' : 'false'); });
      var f = c.getAttribute('data-fam');
      anchors.forEach(function (a) { a.classList.toggle('is-dim', !!f && a.getAttribute('data-fam') !== f); });
      root.removeAttribute('data-view'); view.setAttribute('aria-pressed', 'false'); view.textContent = 'List';
      if (f) { var l = document.getElementById(f); l && (l.dataset.on = '1'); }
    });
  });
  view.addEventListener('click', function () {
    var on = root.getAttribute('data-view') !== 'list';
    if (on) root.setAttribute('data-view', 'list'); else root.removeAttribute('data-view');
    view.setAttribute('aria-pressed', on ? 'true' : 'false'); view.textContent = on ? 'Sphere' : 'List'; if (!on) { size(); draw(); go(); }
  });
  size(); draw(); go();
})();
