(function () {
  var stage = document.getElementById('lib-stage'),
      grid = document.getElementById('grid'),
      chipsEl = document.getElementById('chips'),
      emptyEl = document.getElementById('empty'),
      viewEl = document.getElementById('view'),
      hudWrap = document.getElementById('lib-hud-wrap'),
      hudCatIc = document.getElementById('lib-hud-cat-ic'),
      hudTitle = document.getElementById('lib-hud-title'),
      hudMeta = document.getElementById('lib-hud-meta'),
      hudCopy = document.getElementById('lib-hud-copy'),
      hudDl = document.getElementById('lib-hud-dl'),
      dotsWrap = document.getElementById('lib-dots');

  if (!stage || !grid) return;

  var IC_COPY = '<svg class="lib-ic" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="7.5" y="7.5" width="9" height="9" rx="2"/><path d="M4.5 12.5V5.5A2 2 0 0 1 6.5 3.5h7"/></svg>';
  var IC_CHECK = '<svg class="lib-ic" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M4.5 10.5 8.2 14.2 15.5 6.5"/></svg>';
  var IC_DL = '<svg class="lib-ic" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M10 3.5v9.5m0 0-3.5-3.5M10 13l3.5-3.5M4 16.5h12"/></svg>';
  var IC_OPEN = '<svg class="lib-ic" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M6.5 13.5 13.5 6.5M8 6.5h5.5V12"/></svg>';
  var IC_CHEV = '<svg class="select-chev" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M6 8l4 4 4-4"/></svg>';

  var CAT_ICONS = {
    'All': '<svg class="lib-ic" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><circle cx="5.4" cy="5.4" r="1.1" fill="currentColor" stroke="none"/><circle cx="10" cy="5.4" r="1.1" fill="currentColor" stroke="none"/><circle cx="14.6" cy="5.4" r="1.1" fill="currentColor" stroke="none"/><circle cx="5.4" cy="10" r="1.1" fill="currentColor" stroke="none"/><circle cx="10" cy="10" r="1.1" fill="currentColor" stroke="none"/><circle cx="14.6" cy="10" r="1.1" fill="currentColor" stroke="none"/><circle cx="5.4" cy="14.6" r="1.1" fill="currentColor" stroke="none"/><circle cx="10" cy="14.6" r="1.1" fill="currentColor" stroke="none"/><circle cx="14.6" cy="14.6" r="1.1" fill="currentColor" stroke="none"/></svg>',
    'Templates': '<svg class="lib-ic" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="3.5" y="3.5" width="9" height="9" rx="2"/><rect x="7.5" y="7.5" width="9" height="9" rx="2"/></svg>',
    'Community': '<svg class="lib-ic" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><circle cx="10" cy="10" r="7.2"/><circle cx="10" cy="10" r="2.5"/><circle cx="10" cy="10" r="0.9" fill="currentColor" stroke="none"/><path d="M5.2 12.4A5.4 5.4 0 0 0 7.6 14.8"/><path d="M14.8 7.6A5.4 5.4 0 0 0 12.4 5.2"/></svg>',
    'Pixel': '<svg class="lib-ic" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M10 3 11.7 8.3 17 10 11.7 11.7 10 17 8.3 11.7 3 10 8.3 8.3Z"/></svg>',
    'Audio FX': '<svg class="lib-ic" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M4 8v4M7 5v10M10 3v14M13 6v8M16 8.5v3"/></svg>',
    'Audio Effects': '<svg class="lib-ic" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M4 8v4M7 5v10M10 3v14M13 6v8M16 8.5v3"/></svg>',
    'Synths': '<svg class="lib-ic" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="3" y="5" width="14" height="10" rx="2"/><path d="M7.7 5v10M12.3 5v10"/></svg>',
    'Notes': '<svg class="lib-ic" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M8.5 14.5V4.5l7-1.5v10"/><circle cx="6.5" cy="14.5" r="2" fill="currentColor" stroke="none"/><circle cx="13.5" cy="13" r="2" fill="currentColor" stroke="none"/></svg>',
    'Sketch': '<svg class="lib-ic" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M9 4.5H5.5A1.5 1.5 0 0 0 4 6v8.5A1.5 1.5 0 0 0 5.5 16H14a1.5 1.5 0 0 0 1.5-1.5V11"/><path d="M15.2 3.8a1.4 1.4 0 0 1 1 2.4L9.4 13l-2.6.7.7-2.6 6.8-6.8a1.4 1.4 0 0 1 .9-.5Z"/></svg>',
    'Sketch 3D': '<svg class="lib-ic" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M10 3.2 16.2 6.7V13.4L10 16.9 3.8 13.4V6.7Z"/><path d="M3.8 6.7 10 10.05 16.2 6.7M10 10.05V16.9"/></svg>',
    'AI Skills': '<svg class="lib-ic" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="3" y="3" width="14" height="14" rx="2"/><path d="M3 10h14M10 3v14"/><circle cx="6.5" cy="6.5" r="1.4" fill="currentColor" stroke="none"/><circle cx="13.5" cy="13.5" r="1.4" fill="currentColor" stroke="none"/></svg>',
    'Extensions': '<svg class="lib-ic" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M3.2 10c1.8-5.5 3.2-5.5 4.4 0s2.8 5.5 4.4 0 3.2-5.5 4.4 0"/></svg>'
  };

  var CATS = ['Templates', 'Community', 'Pixel', 'Audio FX', 'Synths', 'Notes', 'Sketch', 'AI Skills', 'Extensions'];
  var COLOUR = {
    'Templates': 'var(--accent)',
    'Pixel': 'var(--accent)',
    'Audio FX': 'var(--cat-synths)',
    'Audio Effects': 'var(--cat-synths)',
    'Synths': 'var(--cat-3d)',
    'Notes': 'var(--cat-modulators)',
    'Sketch': 'var(--signal)',
    'Sketch 3D': 'var(--cat-3d)',
    'Community': 'var(--signal)',
    'AI Skills': 'var(--signal)',
    'Extensions': 'var(--cat-prediction)'
  };


  var items = [];
  var allCards = [];
  var visibleCards = [];
  var activeFilter = 'All';
  var deck = null;

  var params = new URLSearchParams(window.location.search);
  var qView = params.get('view');
  var mode = (qView === 'stack' || qView === 'grid' || qView === 'fan') ? qView : 'fan';
  var qTheme = params.get('mode');
  if (qTheme === 'midnight' || qTheme === 'paper' || qTheme === 'mist') {
    document.documentElement.setAttribute('data-mode', qTheme);
  }

  function esc(s) {
    return String(s || '').replace(/[&<>"]/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c];
    });
  }
  var ART = { 'Pixel': 1, 'Sketch': 1, 'Sketch 3D': 1 };   /* their own render is the thumbnail; the rest are knobs and cables */
  function normCat(cat) {
    if (!cat) return 'Pixel';
    if (cat === 'Audio Effects') return 'Audio FX';
    if (cat === 'Sketch 3D') return 'Sketch';
    return cat;
  }
  function catIcon(cat) { return CAT_ICONS[cat] || CAT_ICONS[normCat(cat)] || CAT_ICONS['Pixel']; }

  /* A card is a picture: a category chip, an open chip, and (in the bento only) a title bar and two action chips. */
  function card(it, i) {
    var rawCat = it.category || 'Pixel';
    var cat = normCat(rawCat);
    var hasCopy = !(it.kind === 'Template' || it.kind === 'Patch' || it.kind === 'Skill' || it.kind === 'Extension');
    var isExt = it.kind === 'Extension';
    var detailHref = 'library/' + esc(it.id) + '.html';
    return '<article class="lib-card deck-card" data-cat="' + esc(cat) + '" data-raw-cat="' + esc(rawCat) + '" data-name="' + esc(it.name) + '" data-file="' + esc(it.file) + '" data-has-copy="' + (hasCopy ? '1' : '0') + '" data-ext="' + (isExt ? '1' : '0') + '" style="--cat:' + (COLOUR[rawCat] || COLOUR[cat] || 'var(--accent)') + '">' +
      '<a class="lib-media-link" href="' + detailHref + '" aria-label="Open ' + esc(it.name) + '">' +
        '<img data-src="assets/library/thumbs/' + esc(it.id) + (it.preview ? '.webp' : '.svg') + '?v=4" ' + (it.preview ? 'onerror="if(!this.__fb){this.__fb=true;this.src=\'' + esc(it.preview) + '\';}" ' : '') + 'alt="" decoding="async" width="720" height="450">' +
      '</a>' +
      '<span class="lib-chip lib-chip-cat" title="' + esc(rawCat) + '">' + catIcon(rawCat) + '</span>' +
      '<span class="lib-chip lib-chip-open" aria-hidden="true">' + IC_OPEN + '</span>' +
      '<div class="lib-acts">' +
        (hasCopy ? '<button class="lib-chip lib-act-btn lib-copy" type="button" data-file="' + esc(it.file) + '" aria-label="Copy code for ' + esc(it.name) + '" title="Copy code">' + IC_COPY + '</button>' : '') +
        (isExt
          ? '<a class="lib-chip lib-act-btn lib-dl" href="' + esc(it.file) + '" aria-label="Install ' + esc(it.name) + '" title="Install">' + IC_OPEN + '</a>'
          : '<a class="lib-chip lib-act-btn lib-dl" href="' + esc(it.file) + '" download aria-label="Download ' + esc(it.name) + '" title="Download">' + IC_DL + '</a>') +
      '</div>' +
      '<h2 class="lib-card-title"><b>' + esc(it.name) + '</b>' + (it.description ? '<span>' + esc(it.description) + '</span>' : '') + '</h2>' +
    '</article>';
  }

  function renderDropdown() {
    var cs = ['All'].concat(CATS);
    chipsEl.innerHTML =
      '<button class="lib-select" type="button" aria-haspopup="listbox" aria-expanded="false" aria-label="Filter by category">' +
        '<span class="lib-select-ic">' + catIcon(activeFilter) + '</span>' +
        '<span class="lib-select-val">' + esc(activeFilter) + '</span>' +
        IC_CHEV +
      '</button>' +
      '<div class="lib-menu" role="listbox" aria-label="Category" hidden>' +
        cs.map(function (c) {
          return '<button class="lib-opt" type="button" role="option" aria-selected="' + (c === activeFilter) + '" data-f="' + esc(c) + '" aria-label="' + esc(c) + '" title="' + esc(c) + '">' +
            catIcon(c) +
            '<span>' + esc(c) + '</span>' +
          '</button>';
        }).join('') +
      '</div>';
  }
  function setMenuOpen(open) {
    var m = chipsEl.querySelector('.lib-menu'), t = chipsEl.querySelector('.lib-select');
    if (!m || !t) return;
    m.hidden = !open;
    t.setAttribute('aria-expanded', String(open));
  }

  function buildDots() {
    if (!dotsWrap) return;
    dotsWrap.innerHTML = '';
    var total = visibleCards.length;
    if (!total) return;
    var maxDots = Math.min(total, 12);
    for (var i = 0; i < maxDots; i++) {
      (function (slotIdx) {
        var b = document.createElement('button');
        b.type = 'button';
        b.className = 'lib-dot';
        b.setAttribute('aria-label', 'Jump to item');
        b.addEventListener('click', function () { deck.goTo(Math.round((slotIdx / Math.max(1, maxDots - 1)) * (total - 1))); });
        dotsWrap.appendChild(b);
      })(i);
    }
  }

  function updateHud(curIdx) {
    var total = visibleCards.length;
    var c = visibleCards[curIdx];
    if (!total || !c) return;
    var rawCat = c.getAttribute('data-raw-cat') || 'Pixel';
    var name = c.getAttribute('data-name') || '';
    var file = c.getAttribute('data-file') || '';
    var hasCopy = c.getAttribute('data-has-copy') === '1';
    var isExt = c.getAttribute('data-ext') === '1';
    if (hudCatIc) hudCatIc.innerHTML = catIcon(rawCat);
    if (hudTitle) hudTitle.textContent = name;
    if (hudMeta) hudMeta.textContent = rawCat;
    if (hudCopy) {
      hudCopy.hidden = !hasCopy;
      hudCopy.setAttribute('data-file', file);
      hudCopy.setAttribute('aria-label', 'Copy code for ' + name);
    }
    if (hudDl) {
      hudDl.href = file;
      if (isExt) { hudDl.removeAttribute('download'); hudDl.setAttribute('aria-label', 'Install ' + name); }
      else { hudDl.setAttribute('download', ''); hudDl.setAttribute('aria-label', 'Download ' + name); }
    }
    if (dotsWrap && dotsWrap.children.length) {
      var dots = dotsWrap.children;
      var activeDot = dots.length <= 1 ? 0 : Math.round((curIdx / Math.max(1, total - 1)) * (dots.length - 1));
      for (var i = 0; i < dots.length; i++) dots[i].classList.toggle('is-on', i === activeDot);
    }
  }

  function rebuildVisible() {
    visibleCards = [];
    allCards.forEach(function (c) {
      var show = activeFilter === 'All' || c.getAttribute('data-cat') === activeFilter;
      c.hidden = !show;
      if (show) visibleCards.push(c);
    });
    visibleCards.forEach(function (c, k) { c.setAttribute('data-bento', k % 7 === 0 ? 'big' : (k % 7 === 4 ? 'wide' : '')); });
    emptyEl.hidden = visibleCards.length > 0;
    stage.hidden = visibleCards.length === 0;
    var valEl = chipsEl.querySelector('.lib-select-val'), icEl = chipsEl.querySelector('.lib-select-ic');
    if (valEl) valEl.textContent = activeFilter;
    if (icEl) icEl.innerHTML = catIcon(activeFilter);
    Array.prototype.forEach.call(chipsEl.querySelectorAll('.lib-opt'), function (opt) {
      opt.setAttribute('aria-selected', String(opt.getAttribute('data-f') === activeFilter));
    });
    buildDots();
    if (deck) { deck.refresh(false); updateHud(0); }
  }

  function setView(m) {
    mode = m;
    if (viewEl) Array.prototype.forEach.call(viewEl.querySelectorAll('[data-view]'), function (b) {
      var on = b.getAttribute('data-view') === m;
      b.classList.toggle('is-on', on);
      b.setAttribute('aria-pressed', on ? 'true' : 'false');
    });
    if (hudWrap) hudWrap.hidden = (m === 'grid' || !visibleCards.length);
    if (deck) deck.setMode(m);
  }

  function reshuffleDeck() {
    for (var i = allCards.length - 1; i > 0; i--) {
      var j = Math.floor(Math.random() * (i + 1)), tmp = allCards[i];
      allCards[i] = allCards[j]; allCards[j] = tmp;
    }
    allCards.forEach(function (c) { grid.appendChild(c); });
    rebuildVisible();
  }

  function copyDeviceCode(btn) {
    var file = btn.getAttribute('data-file');
    if (!file) return;
    fetch(file).then(function (r) { return r.text(); }).then(function (txt) {
      if (/\.field$/.test(file)) { try { txt = JSON.parse(txt).device.code; } catch (x) {} }
      return navigator.clipboard.writeText(txt);
    }).then(function () {
      btn.classList.add('done');
      btn.innerHTML = IC_CHECK;
      setTimeout(function () { btn.classList.remove('done'); btn.innerHTML = IC_COPY; }, 1400);
    }).catch(function () {});
  }

  if (viewEl) {
    Array.prototype.forEach.call(viewEl.querySelectorAll('[data-view]'), function (btn) {
      btn.addEventListener('click', function () { setView(btn.getAttribute('data-view') || 'fan'); });
    });
  }

  chipsEl.addEventListener('click', function (e) {
    if (e.target.closest('.lib-select')) {
      var m = chipsEl.querySelector('.lib-menu');
      if (m) setMenuOpen(m.hidden);
      return;
    }
    var b = e.target.closest('.lib-opt');
    if (!b) return;
    activeFilter = b.getAttribute('data-f') || 'All';
    setMenuOpen(false);
    rebuildVisible();
  });
  document.addEventListener('click', function (e) { if (!chipsEl.contains(e.target)) setMenuOpen(false); });
  document.addEventListener('keydown', function (e) { if (e.key === 'Escape') setMenuOpen(false); });

  document.querySelectorAll('[data-lib-shuffle]').forEach(function (btn) { btn.addEventListener('click', reshuffleDeck); });
  document.querySelectorAll('[data-lib-prev]').forEach(function (btn) { btn.addEventListener('click', function () { deck.step(-1); }); });
  document.querySelectorAll('[data-lib-next]').forEach(function (btn) { btn.addEventListener('click', function () { deck.step(1); }); });
  if (hudCopy) hudCopy.addEventListener('click', function () { copyDeviceCode(hudCopy); });
  window.addEventListener('keydown', function (e) {
    if ((e.key === 'r' || e.key === 'R') && !e.metaKey && !e.ctrlKey && !e.altKey && mode !== 'grid') {
      var t = e.target && e.target.tagName ? e.target.tagName.toLowerCase() : '';
      if (t !== 'input' && t !== 'textarea' && t !== 'select') reshuffleDeck();
    }
  });

  /* ambient light follows the pointer */
  var hero = stage.closest('.lib-hero') || stage;
  if (!(window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches)) {
    addEventListener('pointermove', function (e) {
      hero.style.setProperty('--px', (e.clientX / innerWidth - 0.5).toFixed(3));
      hero.style.setProperty('--py', (e.clientY / innerHeight - 0.5).toFixed(3));
    }, { passive: true });
  }

  renderDropdown();

  Promise.all([
    fetch('assets/library/index.json?v=4').then(function (r) { return r.json(); }),
    fetch('assets/templates/index.json?v=4').then(function (r) { return r.json(); }).catch(function () { return { items: [] }; }),
    fetch('assets/community/index.json?v=2').then(function (r) { return r.json(); }).catch(function () { return { items: [] }; }),
    fetch('assets/skills/index.json?v=2').then(function (r) { return r.json(); }).catch(function () { return { items: [] }; }),
    fetch('assets/extensions/index.json?v=1').then(function (r) { return r.json(); }).catch(function () { return { items: [] }; })
  ]).then(function (all) {
    items = all[1].items.concat(all[2].items, all[3].items, all[4].items, all[0].items);
    for (var i = items.length - 1; i > 0; i--) {
      var j = Math.floor(Math.random() * (i + 1)), t = items[i];
      items[i] = items[j]; items[j] = t;
    }
    grid.innerHTML = items.map(card).join('');
    allCards = Array.prototype.slice.call(grid.querySelectorAll('.lib-card'));
    renderDropdown();

    // copy and download chips act only on the card in front (or in the bento); on any other card the tap brings it forward
    grid.addEventListener('click', function (e) {
      var copyBtn = e.target.closest('.lib-copy');
      if (copyBtn) { e.preventDefault(); e.stopPropagation(); copyDeviceCode(copyBtn); return; }
      var dlBtn = e.target.closest('.lib-dl');
      if (dlBtn) e.stopPropagation();
    });

    deck = InfiniteDeck.create({
      stage: stage, grid: grid, cardSel: '.lib-card', mode: mode,
      getCards: function () { return visibleCards; },
      onChange: updateHud
    });
    rebuildVisible();
    setView(mode);
  }).catch(function () {
    emptyEl.hidden = false;
    emptyEl.textContent = 'Could not load the library.';
  });
})();
