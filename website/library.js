(function () {
  var grid = document.getElementById('grid'), chipsEl = document.getElementById('chips'),
      countEl = document.getElementById('count'), emptyEl = document.getElementById('empty');
  var DL = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M12 3v12m0 0l-4.5-4.5M12 15l4.5-4.5M4 20h16"/></svg>';
  var items = [], active = 'All';

  function esc(s) { return String(s).replace(/[&<>"]/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]; }); }
  function price(p) { return p > 0 ? '$' + p : 'Free for now'; }

  function card(it) {
    var tags = it.tags.map(function (t) { return '<span class="tag">' + esc(t) + '</span>'; }).join('');
    return '<article class="card" data-id="' + esc(it.id) + '">' +
      '<div class="pv"><img src="' + esc(it.preview) + '" alt="Preview of ' + esc(it.name) + '" loading="lazy" width="900" height="600"></div>' +
      '<div class="body"><div class="row"><h2>' + esc(it.name) + '</h2><span class="price">' + price(it.price) + '</span></div>' +
      '<p class="desc">' + esc(it.description) + '</p>' +
      '<div class="meta"><span class="tag kind">' + esc(it.kind) + '</span>' + tags + '</div>' +
      '<div class="foot"><span class="small">Field Pixel · ' + it.params + ' params · ' + (it.bytes / 1024).toFixed(1) + ' KB</span>' +
      '<a class="dl" href="' + esc(it.file) + '" download aria-label="Download ' + esc(it.name) + '">' + DL + 'Download</a></div></div></article>';
  }

  function chips() {
    var set = ['All', 'Filter', 'Generator'], seen = {};
    items.forEach(function (i) { i.tags.forEach(function (t) { if (!seen[t]) { seen[t] = 1; set.push(t); } }); });
    chipsEl.innerHTML = '<span class="label">Show</span>' + set.map(function (s) {
      return '<button class="chip" type="button" aria-pressed="' + (s === active) + '" data-f="' + esc(s) + '">' + esc(s) + '</button>';
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
    Array.prototype.forEach.call(chipsEl.querySelectorAll('.chip'), function (c) { c.setAttribute('aria-pressed', c.dataset.f === active); });
  }

  chipsEl.addEventListener('click', function (e) {
    var b = e.target.closest('.chip'); if (!b) return;
    active = b.dataset.f; apply();
  });

  fetch('assets/library/index.json?v=1').then(function (r) { return r.json(); }).then(function (d) {
    items = d.items; grid.innerHTML = items.map(card).join(''); chips(); apply();
  }).catch(function () { emptyEl.hidden = false; emptyEl.textContent = 'Could not load the library.'; });
})();
