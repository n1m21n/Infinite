(function () {
  var grid = document.getElementById('grid'), chipsEl = document.getElementById('chips'),
      countEl = document.getElementById('count'), emptyEl = document.getElementById('empty');
  var DL = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M12 3v12m0 0l-4.5-4.5M12 15l4.5-4.5M4 20h16"/></svg>';
  var KIND = { Filter: { c: '#F5866B', hand: '#C2593F', cap: 'filters a picture' }, Generator: { c: '#8B6CFF', hand: '#7A5CE0', cap: 'makes its own' } };
  var items = [], active = 'All';

  function esc(s) { return String(s).replace(/[&<>"]/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]; }); }

  function card(it, i) {
    var k = KIND[it.kind] || KIND.Generator;
    var tags = it.tags.map(function (t) { return '<span class="lib-tag">' + esc(t) + '</span>'; }).join('');
    var price = it.price > 0 ? '$' + it.price : 'Free for now';
    return '<article class="lib-card reveal in" style="--cat:' + k.c + '; --hand:' + k.hand + '; --i:' + i + '">' +
      '<p class="hand-cap">' + k.cap + '</p>' +
      '<div class="node">' +
      (it.kind === 'Filter' ? '<span class="port port-in"></span>' : '') + '<span class="port port-out"></span>' +
      '<header class="node-head"><span class="cat-dot"></span><h2 class="node-name">' + esc(it.name) + '</h2><span class="node-cat">' + esc(it.kind) + '</span></header>' +
      '<div class="node-view"><img src="' + esc(it.preview) + '" alt="' + esc(it.name) + ' preview" loading="lazy" width="900" height="600"></div>' +
      '<p class="lib-desc">' + esc(it.description) + '</p>' +
      '<div class="lib-tags">' + tags + '</div>' +
      '<footer class="lib-foot"><div class="lib-price"><b>' + price + '</b><span>' + it.params + ' params · ' + (it.bytes / 1024).toFixed(1) + ' KB</span></div>' +
      '<a class="btn btn-sm lib-dl" href="' + esc(it.file) + '" download aria-label="Download ' + esc(it.name) + '">' + DL + 'Download</a></footer>' +
      '</div></article>';
  }

  function chips() {
    var set = ['All', 'Filter', 'Generator'], seen = {};
    items.forEach(function (i) { i.tags.forEach(function (t) { if (!seen[t]) { seen[t] = 1; set.push(t); } }); });
    chipsEl.innerHTML = set.map(function (s, n) {
      return '<button class="lib-chip' + (n === 3 ? ' sep' : '') + '" type="button" aria-pressed="' + (s === active) + '" data-f="' + esc(s) + '">' + esc(s) + '</button>';
    }).join('');
  }

  function apply() {
    var n = 0;
    Array.prototype.forEach.call(grid.children, function (el, i) {
      var it = items[i], show = active === 'All' || it.kind === active || it.tags.indexOf(active) >= 0;
      el.hidden = !show; if (show) n++;
    });
    countEl.textContent = n + ' of ' + items.length;
    emptyEl.hidden = n > 0;
    Array.prototype.forEach.call(chipsEl.querySelectorAll('.lib-chip'), function (c) { c.setAttribute('aria-pressed', c.dataset.f === active); });
  }

  chipsEl.addEventListener('click', function (e) {
    var b = e.target.closest('.lib-chip'); if (!b) return;
    active = b.dataset.f; apply();
  });

  fetch('assets/library/index.json?v=2').then(function (r) { return r.json(); }).then(function (d) {
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
