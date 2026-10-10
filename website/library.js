(function () {
  var grid = document.getElementById('grid'), chipsEl = document.getElementById('chips'),
      countEl = document.getElementById('count'), emptyEl = document.getElementById('empty'),
      viewEl = document.getElementById('view');
  var DL = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M12 3v12m0 0l-4.5-4.5M12 15l4.5-4.5M4 20h16"/></svg>';
  // The library grows by category: every one is listed now, the empty ones say so.
  var CATS = ['Pixel', 'Audio FX', 'Synths', 'Notes', 'Sketch'];
  var COLOUR = { 'Pixel': '#F5866B', 'Audio FX': '#6992F6', 'Synths': '#38BDF8', 'Notes': '#A3E635', 'Sketch': '#8B6CFF' };
  var HAND = { 'Pixel': '#C2593F', 'Audio FX': '#3D63C9', 'Synths': '#1F8FBF', 'Notes': '#5C8F10', 'Sketch': '#7A5CE0' };
  var items = [], active = 'All', cols = 4;

  function esc(s) { return String(s).replace(/[&<>"]/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]; }); }
  function store(k, v) { try { if (v === undefined) return localStorage.getItem(k); localStorage.setItem(k, v); } catch (e) { return null; } }

  function card(it, i) {
    var cat = it.category || 'Pixel';
    return '<article class="lib-card reveal in" style="--cat:' + (COLOUR[cat] || '#F5866B') + '; --hand:' + (HAND[cat] || '#C2593F') + '; --i:' + Math.min(i, 8) + '">' +
      '<p class="hand-cap">' + esc(it.kind === 'Filter' ? 'filters a picture' : 'makes its own') + '</p>' +
      '<div class="node">' +
      (it.kind === 'Filter' ? '<span class="port port-in"></span>' : '') + '<span class="port port-out"></span>' +
      '<header class="node-head"><span class="cat-dot"></span><h2 class="node-name">' + esc(it.name) + '</h2><span class="node-cat">' + esc(cat) + '</span></header>' +
      '<div class="node-view"><img src="' + esc(it.preview) + '" alt="' + esc(it.name) + ' preview" loading="lazy" width="900" height="600"></div>' +
      '<p class="lib-desc">' + esc(it.description) + '</p>' +
      '<footer class="lib-foot"><span class="lib-price">' + (it.price > 0 ? '$' + it.price : 'Free') + '</span>' +
      '<a class="btn btn-sm lib-dl" href="' + esc(it.file) + '" download aria-label="Download ' + esc(it.name) + '">' + DL + '<span class="lib-dl-text">Download</span></a></footer>' +
      '</div></article>';
  }

  function chips() {
    chipsEl.innerHTML = ['All'].concat(CATS).map(function (c) {
      return '<button class="lib-chip" type="button" aria-pressed="' + (c === active) + '" data-f="' + esc(c) + '">' + esc(c) + '</button>';
    }).join('');
  }

  function apply() {
    var n = 0;
    Array.prototype.forEach.call(grid.children, function (el, i) {
      var show = active === 'All' || (items[i].category || 'Pixel') === active;
      el.hidden = !show; if (show) n++;
    });
    countEl.textContent = n + (n === 1 ? ' device' : ' devices');
    emptyEl.hidden = n > 0;
    emptyEl.textContent = active + ' devices are coming to the library.';
    Array.prototype.forEach.call(chipsEl.querySelectorAll('.lib-chip'), function (c) { c.setAttribute('aria-pressed', c.dataset.f === active); });
  }

  function setCols(n) {
    cols = n; grid.dataset.cols = n; store('lib-cols', String(n));
    Array.prototype.forEach.call(viewEl.querySelectorAll('button'), function (b) { b.setAttribute('aria-pressed', b.dataset.cols === String(n)); });
  }

  chipsEl.addEventListener('click', function (e) {
    var b = e.target.closest('.lib-chip'); if (!b) return;
    active = b.dataset.f; apply();
  });
  viewEl.addEventListener('click', function (e) {
    var b = e.target.closest('button'); if (b) setCols(+b.dataset.cols);
  });
  setCols(store('lib-cols') === '8' ? 8 : 4);

  fetch('assets/library/index.json?v=3').then(function (r) { return r.json(); }).then(function (d) {
    items = d.items; grid.innerHTML = items.map(card).join(''); chips(); apply();
  }).catch(function () { emptyEl.hidden = false; emptyEl.textContent = 'Could not load the library.'; });

  /* header: shadow on scroll + mobile menu (same behaviour as the main page) */
  var header = document.getElementById('site-header'), btn = document.getElementById('menu-btn');
  if (header) {
    var onScroll = function () { header.classList.toggle('scrolled', window.scrollY > 8); };
    window.addEventListener('scroll', onScroll, { passive: true }); onScroll();
    if (btn) {
      var set = function (o) { header.classList.toggle('nav-open', o); btn.setAttribute('aria-expanded', String(o)); };
      btn.addEventListener('click', function () { set(!header.classList.contains('nav-open')); });
      document.addEventListener('keydown', function (e) { if (e.key === 'Escape') set(false); });
      document.addEventListener('click', function (e) { if (header.classList.contains('nav-open') && !header.contains(e.target)) set(false); });
    }
  }
})();
