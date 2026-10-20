/* Blog index: filter, arrangement switch, shuffle and the title bar, on top of the shared deck engine (deck.js). */
(function () {
  var stage = document.getElementById('blog-book-stage');
  var grid = document.getElementById('post-grid');
  if (!stage || !grid || !window.InfiniteDeck) return;
  var $ = function (id) { return document.getElementById(id); };
  var hudWrap = $('deck-hud-wrap'), hudTitle = $('deck-hud-title'), dotsWrap = $('deck-progress-dots');
  var empty = $('blog-empty'), filterWrap = $('blog-filter'), filterBtn = $('blog-select'), filterMenu = $('blog-menu'), filterIc = $('blog-select-ic');

  var all = Array.prototype.slice.call(grid.querySelectorAll('.post-card'));
  var visible = all.slice();
  var filter = 'all';
  var q = new URLSearchParams(location.search);
  var mode = ['fan', 'stack', 'grid'].indexOf(q.get('view')) > -1 ? q.get('view') : 'fan';
  var t = q.get('mode');
  if (t === 'midnight' || t === 'paper' || t === 'mist') document.documentElement.setAttribute('data-mode', t);

  var deck = InfiniteDeck.create({
    stage: stage, grid: grid, cardSel: '.post-card', mode: mode,
    getCards: function () { return visible; },
    onChange: function (i) { hud(i); }
  });

  function hud(i) {
    var c = visible[i]; if (!c) return;
    if (hudTitle) hudTitle.textContent = c.getAttribute('data-title') || '';
    if (dotsWrap) for (var k = 0; k < dotsWrap.children.length; k++) dotsWrap.children[k].classList.toggle('is-on', k === i);
  }
  function dots() {
    if (!dotsWrap) return;
    dotsWrap.innerHTML = '';
    visible.forEach(function (c, i) {
      var b = document.createElement('button');
      b.type = 'button'; b.className = 'deck-dot'; b.setAttribute('aria-label', c.getAttribute('data-title') || 'Note');
      b.addEventListener('click', function () { deck.goTo(i); });
      dotsWrap.appendChild(b);
    });
  }
  function rebuild() {
    visible = all.filter(function (c) {
      var show = filter === 'all' || c.getAttribute('data-cat') === filter;
      c.hidden = !show; return show;
    });
    visible.forEach(function (c, k) { c.setAttribute('data-bento', k % 7 === 0 ? 'big' : (k % 7 === 4 ? 'wide' : '')); });
    empty.hidden = visible.length > 0;
    stage.hidden = visible.length === 0;
    dots(); deck.refresh(false); hud(0);
  }
  function shuffle() {
    for (var i = all.length - 1; i > 0; i--) { var j = Math.floor(Math.random() * (i + 1)), x = all[i]; all[i] = all[j]; all[j] = x; }
    all.forEach(function (c) { grid.appendChild(c); });
    rebuild();
  }
  function setView(m) {
    mode = m;
    document.querySelectorAll('[data-deck-view]').forEach(function (b) {
      var on = b.getAttribute('data-deck-view') === m;
      b.classList.toggle('is-on', on); b.setAttribute('aria-pressed', on ? 'true' : 'false');
    });
    if (hudWrap) hudWrap.hidden = m === 'grid';
    deck.setMode(m);
  }

  // filter menu
  function menu(open) { if (!filterMenu) return; filterMenu.hidden = !open; filterBtn.setAttribute('aria-expanded', open ? 'true' : 'false'); }
  if (filterBtn && filterMenu) {
    filterBtn.addEventListener('click', function (e) { e.stopPropagation(); menu(filterMenu.hidden); });
    var opts = filterMenu.querySelectorAll('.blog-opt');
    opts.forEach(function (o) { o.setAttribute('aria-label', o.getAttribute('data-label') || ''); if (!o.querySelector('span')) { var sp = document.createElement('span'); sp.textContent = o.getAttribute('data-label') || ''; o.appendChild(sp); } });
    opts.forEach(function (o) {
      o.addEventListener('click', function () {
        filter = o.getAttribute('data-filter') || 'all';
        opts.forEach(function (x) { x.setAttribute('aria-selected', x === o ? 'true' : 'false'); });
        var svg = o.querySelector('svg'); if (filterIc && svg) filterIc.innerHTML = svg.outerHTML;
        filterBtn.setAttribute('aria-label', 'Category: ' + (o.getAttribute('data-label') || 'All'));
        menu(false); rebuild();
      });
    });
    document.addEventListener('click', function (e) { if (filterWrap && !filterWrap.contains(e.target)) menu(false); });
    document.addEventListener('keydown', function (e) { if (e.key === 'Escape') menu(false); });
  }
  document.querySelectorAll('[data-deck-view]').forEach(function (b) { b.addEventListener('click', function () { setView(b.getAttribute('data-deck-view') || 'fan'); }); });
  document.querySelectorAll('[data-deck-shuffle]').forEach(function (b) { b.addEventListener('click', shuffle); });
  document.querySelectorAll('[data-deck-prev]').forEach(function (b) { b.addEventListener('click', function () { deck.step(-1); }); });
  document.querySelectorAll('[data-deck-next]').forEach(function (b) { b.addEventListener('click', function () { deck.step(1); }); });

  // ambient light follows the deck and the pointer
  var root = stage.closest('.blog-hero') || stage;
  var rm = matchMedia('(prefers-reduced-motion: reduce)').matches;
  if (!rm) {
    addEventListener('pointermove', function (e) {
      root.style.setProperty('--px', (e.clientX / innerWidth - 0.5).toFixed(3));
      root.style.setProperty('--py', (e.clientY / innerHeight - 0.5).toFixed(3));
    }, { passive: true });
  }

  shuffle();
  setView(mode);
})();
