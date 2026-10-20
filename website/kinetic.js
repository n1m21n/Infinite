/* Kinetic type and numerals (brand.json type.kinetic, type.numerals; BRAND.md 3.1, 3.2). Every move runs once on first
   view; the swap is the only repeat and pauses when the tab is hidden. Reduced motion: final state, nothing moves.
   Markup:
     <h2 class="kt-slab">Words rise into place</h2>                    split into words, rise on the slab spring
     <span class="kt-draw">instrument</span>                           hand underline draws once
     <span class="kt-swap" data-words="designers,musicians,VJs">designers</span>   one word every 2 bars
     <span class="fig fig-l" data-count="120">120<span class="fig-unit">BPM</span></span>   counts up once */
(function () {
  var reduced = matchMedia('(prefers-reduced-motion: reduce)').matches;
  var BAR = 2000;  // one bar at 120 BPM

  function words(el) {
    if (el.dataset.split) return;
    el.dataset.split = '1';
    var label = el.textContent.replace(/\s+/g, ' ').trim();
    el.setAttribute('aria-label', label);
    var i = 0;
    (function walk(node) {
      Array.prototype.slice.call(node.childNodes).forEach(function (c) {
        if (c.nodeType === 3) {
          var frag = document.createDocumentFragment();
          c.textContent.split(/(\s+)/).forEach(function (w) {
            if (!w) return;
            if (/^\s+$/.test(w)) { frag.appendChild(document.createTextNode(w)); return; }
            var s = document.createElement('span');
            s.className = 'w'; s.setAttribute('aria-hidden', 'true'); s.style.setProperty('--i', i++); s.textContent = w;
            frag.appendChild(s);
          });
          node.replaceChild(frag, c);
        } else if (c.nodeType === 1 && !c.classList.contains('w')) {
          if (c.classList.contains('kt-draw') || c.classList.contains('kt-swap')) { c.classList.add('w'); c.style.setProperty('--i', i++); }
          else walk(c);
        }
      });
    })(el);
  }

  function underline(el) {
    if (el.querySelector('svg')) return;
    el.insertAdjacentHTML('beforeend', '<svg viewBox="0 0 100 10" preserveAspectRatio="none" aria-hidden="true">' +
      '<path pathLength="1" d="M2 7 C 22 3, 48 9, 70 5 S 92 4, 98 6"/></svg>');
  }

  function count(el) {
    var to = parseFloat(el.dataset.count), first = el.firstChild;
    if (isNaN(to) || !first || first.nodeType !== 3) return;
    var dec = (el.dataset.count.split('.')[1] || '').length, t0 = performance.now(), dur = 667;
    (function tick(now) {
      var k = Math.min(1, (now - t0) / dur), e = 1 - Math.pow(1 - k, 3);
      first.textContent = (to * e).toFixed(dec);
      if (k < 1) requestAnimationFrame(tick);
    })(t0);
  }

  function swap(el) {
    var list = (el.dataset.words || '').split(',').map(function (w) { return w.trim(); }).filter(Boolean);
    if (list.length < 2 || el.dataset.ready) return;
    el.dataset.ready = '1';
    el.setAttribute('aria-live', 'off');
    el.textContent = '';
    var spans = list.map(function (w, i) {
      var s = document.createElement('span'); s.textContent = w; if (i) s.setAttribute('aria-hidden', 'true'); else s.className = 'on';
      el.appendChild(s); return s;
    });
    if (reduced) return;
    var n = 0, timer = null;
    function step() {
      var cur = spans[n]; n = (n + 1) % spans.length; var nx = spans[n];
      cur.classList.remove('on'); cur.classList.add('out'); cur.setAttribute('aria-hidden', 'true');
      nx.classList.remove('out'); nx.classList.add('on'); nx.removeAttribute('aria-hidden');
      setTimeout(function () { cur.classList.remove('out'); }, 200);
    }
    function run() { stop(); if (!document.hidden) timer = setInterval(step, 2 * BAR); }
    function stop() { if (timer) clearInterval(timer); timer = null; }
    document.addEventListener('visibilitychange', run);
    run();
  }

  function init() {
    document.querySelectorAll('.kt-slab').forEach(words);
    document.querySelectorAll('.kt-draw').forEach(underline);
    document.querySelectorAll('.kt-swap').forEach(swap);
    var targets = document.querySelectorAll('.kt-slab, .kt-draw, [data-count]');
    if (reduced || !('IntersectionObserver' in window)) { targets.forEach(function (t) { t.classList.add('in'); }); return; }
    var io = new IntersectionObserver(function (es) {
      es.forEach(function (e) {
        if (!e.isIntersecting) return;
        io.unobserve(e.target);
        e.target.classList.add('in');
        if (e.target.dataset.count) count(e.target);
      });
    }, { threshold: 0.4 });
    targets.forEach(function (t) { io.observe(t); });
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init); else init();
})();
