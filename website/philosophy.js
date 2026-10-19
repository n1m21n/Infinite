/* Philosophy page: the mind map that becomes a patch, the loop, the spring demo.
   Motion: a thought crosses one connection in 667 ms (a half-note triplet at 120 BPM); loops are two bars (4 s). Reduced motion keeps everything still. */
(function () {
  'use strict';
  var still = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  function mind() {
    var fig = document.querySelector('.mind'); if (!fig) return;
    var svg = fig.querySelector('.mind-svg'), cap = fig.querySelector('.mind-cap'), nodes = [].slice.call(svg.querySelectorAll('.mnode')), edges = [].slice.call(svg.querySelectorAll('.edge')), dot = svg.querySelector('.thought');
    var idle = cap.textContent, adj = {};
    edges.forEach(function (p) { var a = p.dataset.a, b = p.dataset.b; (adj[a] = adj[a] || []).push([b, p, 1]); (adj[b] = adj[b] || []).push([a, p, -1]); });
    fig.querySelectorAll('.seg button').forEach(function (b) {
      b.addEventListener('click', function () { fig.dataset.view = b.dataset.v; fig.querySelectorAll('.seg button').forEach(function (x) { x.setAttribute('aria-pressed', x === b ? 'true' : 'false'); }); cap.textContent = idle; });
    });
    function focus(n) {
      var id = n.dataset.id, on = {}; on[id] = 1; (adj[id] || []).forEach(function (x) { on[x[0]] = 1; });
      svg.classList.add('focus'); nodes.forEach(function (m) { m.classList.toggle('on', !!on[m.dataset.id]); });
      edges.forEach(function (p) { p.classList.toggle('on', p.dataset.a === id || p.dataset.b === id); });
      var patch = fig.dataset.view === 'patch', lab = n.querySelector(patch ? '.l-patch' : '.l-mind').textContent;
      cap.textContent = lab + ': ' + (patch ? n.dataset.patch : n.dataset.mind) + ' Connects to ' + (adj[id] || []).length + ' others.';
    }
    function blur() { svg.classList.remove('focus'); nodes.forEach(function (m) { m.classList.remove('on'); }); edges.forEach(function (p) { p.classList.remove('on'); }); cap.textContent = idle; }
    nodes.forEach(function (n) {
      n.addEventListener('mouseenter', function () { focus(n); }); n.addEventListener('mouseleave', blur);
      n.addEventListener('focus', function () { focus(n); }); n.addEventListener('blur', blur);
      n.addEventListener('click', function () { focus(n); });
    });
    if (still) return;
    /* one thought hops from node to node along a connection, 667 ms per hop */
    var cur = 'motion', seg = null, vis = true;
    if ('IntersectionObserver' in window) new IntersectionObserver(function (es) { vis = es[0].isIntersecting; }, { rootMargin: '60px' }).observe(fig);
    function pick() {
      var opts = adj[cur]; var o = opts[Math.floor(Math.random() * opts.length)];
      seg = { p: o[1], dir: o[2], to: o[0], t0: performance.now(), len: o[1].getTotalLength() };
    }
    pick();
    function frame(now) {
      if (vis && !document.hidden && seg) {
        var k = Math.min(1, (now - seg.t0) / 667), e = 1 - Math.pow(1 - k, 3), at = seg.dir === 1 ? e : 1 - e, pt = seg.p.getPointAtLength(seg.len * at);
        dot.setAttribute('cx', pt.x.toFixed(1)); dot.setAttribute('cy', pt.y.toFixed(1));
        if (k >= 1) { cur = seg.to; pick(); seg.t0 = now + 125; }
      }
      requestAnimationFrame(frame);
    }
    requestAnimationFrame(frame);
  }

  function loop() {
    var svg = document.querySelector('.loop-svg'); if (!svg) return;
    var dot = svg.querySelector('.loop-dot'), line = svg.querySelector('.loop-line'), len = line.getTotalLength(), vis = true;
    if ('IntersectionObserver' in window) new IntersectionObserver(function (es) { vis = es[0].isIntersecting; }, { rootMargin: '60px' }).observe(svg);
    function at(t) { var pt = line.getPointAtLength(len * ((t % 4000) / 4000)); dot.setAttribute('cx', pt.x.toFixed(1)); dot.setAttribute('cy', pt.y.toFixed(1)); }
    if (still) { at(0); return; }
    function frame(now) { if (vis && !document.hidden) at(now); requestAnimationFrame(frame); }
    requestAnimationFrame(frame);
  }

  function springs() {
    var all = document.querySelectorAll('.spring');
    if (!still && 'IntersectionObserver' in window && all.length) { var done = false; new IntersectionObserver(function (es) { if (es[0].isIntersecting && !done) { done = true; all.forEach(function (b, i) { setTimeout(function () { b.classList.add('go'); }, 400 + i * 125); }); } }, { threshold: 0.6 }).observe(all[0].parentNode); }
    all.forEach(function (b) {
      b.addEventListener('click', function () {
        b.classList.toggle('go');
      });
    });
  }

  function boot() { mind(); loop(); springs(); }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot); else boot();
})();
