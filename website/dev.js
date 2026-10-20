/* Developers page: highlight the contents entry for the section on screen, and copy the one-line install. */
(function () {
  var links = [].slice.call(document.querySelectorAll('.dv-toc a')); if (!links.length) return;
  var map = {}; links.forEach(function (a) { map[a.getAttribute('href').slice(1)] = a; });
  var secs = Object.keys(map).map(function (id) { return document.getElementById(id); }).filter(Boolean);
  function on(id) { links.forEach(function (a) { a.classList.toggle('on', a === map[id]); }); var a = map[id]; if (a && a.parentNode.scrollWidth > a.parentNode.clientWidth && getComputedStyle(a.parentNode).flexDirection === 'row') a.parentNode.scrollTo({ left: a.offsetLeft - 16, behavior: 'smooth' }); }
  if ('IntersectionObserver' in window) {
    var seen = {}; var io = new IntersectionObserver(function (es) { es.forEach(function (e) { seen[e.target.id] = e.isIntersecting ? e.boundingClientRect.top : null; }); var best = null; secs.forEach(function (s) { if (seen[s.id] !== null && seen[s.id] !== undefined && (best === null || seen[s.id] < seen[best])) best = s.id; }); if (best) on(best); }, { rootMargin: '-20% 0px -60% 0px' });
    secs.forEach(function (s) { io.observe(s); });
  }
  on(secs[0] ? secs[0].id : 'start');
  var c = document.querySelector('.dv-cmd .copy');
  if (c) c.addEventListener('click', function () { var t = c.getAttribute('data-copy'), l = c.querySelector('span'); function done() { if (l) { l.textContent = 'Copied'; setTimeout(function () { l.textContent = 'Copy'; }, 1400); } } if (navigator.clipboard) navigator.clipboard.writeText(t).then(done, done); else done(); });
})();
/* Binary rain: 0s and 1s fall from the pointer as it moves, then fade. Decorative, behind the content. */
(function () {
  if (matchMedia('(prefers-reduced-motion: reduce)').matches) return;
  var cv = document.createElement('canvas'); cv.className = 'dv-rain'; cv.setAttribute('aria-hidden', 'true'); document.body.insertBefore(cv, document.body.firstChild);
  var cx = cv.getContext('2d'), W = 0, H = 0, dpr = Math.min(devicePixelRatio || 1, 2), S = 16, st = [], last = 0, run = false, lx = -99, ly = -99;
  function size() { W = innerWidth; H = innerHeight; cv.width = W * dpr; cv.height = H * dpr; cx.setTransform(dpr, 0, 0, dpr, 0, 0); cx.font = '500 14px "Geist Mono", ui-monospace, monospace'; cx.textAlign = 'center'; }
  size(); addEventListener('resize', size);
  function spawn(x, y) {
    if (Math.hypot(x - lx, y - ly) < 14) return; lx = x; ly = y;
    var col = Math.round(x / S) * S; if (st.length > 140) st.shift();
    st.push({ x: col, y: y, v: 60 + Math.random() * 90, n: 5 + (Math.random() * 7 | 0), t: 0, life: 1.6 + Math.random() * 0.8, c: [] });
    if (!run) { run = true; last = performance.now(); requestAnimationFrame(tick); }
  }
  function tick(now) {
    var dt = Math.min(0.05, (now - last) / 1000); last = now; cx.clearRect(0, 0, W, H);
    for (var i = st.length - 1; i >= 0; i--) {
      var s = st[i]; s.t += dt; s.y += s.v * dt; if (s.t > s.life) { st.splice(i, 1); continue; }
      var k = 1 - s.t / s.life;
      for (var j = 0; j < s.n; j++) {
        var yy = s.y - j * S; if (yy < -S || yy > H + S) continue;
        if (s.c[j] === undefined || Math.random() < 0.02) s.c[j] = Math.random() < 0.5 ? '0' : '1';
        cx.fillStyle = j === 0 ? 'rgba(124,138,255,' + (0.9 * k) + ')' : 'rgba(124,138,255,' + (0.5 * k * (1 - j / s.n)) + ')';
        cx.fillText(s.c[j], s.x, yy);
      }
    }
    if (st.length) requestAnimationFrame(tick); else { run = false; cx.clearRect(0, 0, W, H); }
  }
  addEventListener('pointermove', function (e) { spawn(e.clientX, e.clientY); }, { passive: true });
  addEventListener('touchmove', function (e) { var t = e.touches[0]; if (t) spawn(t.clientX, t.clientY); }, { passive: true });
})();
