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
      hudPlay = document.getElementById('lib-hud-play'),
      hudDl = document.getElementById('lib-hud-dl'),
      dotsWrap = document.getElementById('lib-dots');
  if (!stage || !grid) return;

  function ic(p, extra) { return '<svg class="lib-ic" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">' + p + (extra || '') + '</svg>'; }
  var IC_PLAY = '<svg class="lib-ic" viewBox="0 0 20 20" fill="currentColor" aria-hidden="true"><path d="M7 4.6v10.8a.6.6 0 0 0 .9.5l8.6-5.4a.6.6 0 0 0 0-1L7.9 4.1a.6.6 0 0 0-.9.5Z"/></svg>';
  var IC_PAUSE = '<svg class="lib-ic" viewBox="0 0 20 20" fill="currentColor" aria-hidden="true"><rect x="5" y="4" width="3.6" height="12" rx="1"/><rect x="11.4" y="4" width="3.6" height="12" rx="1"/></svg>';
  var IC_DL = ic('<path d="M10 3.5v9.5m0 0-3.5-3.5M10 13l3.5-3.5M4 16.5h12"/>');
  var IC_CHEV = '<svg class="select-chev" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M6 8l4 4 4-4"/></svg>';
  var KIND = {
    'All': { label: 'All', ic: ic('<circle cx="5.4" cy="5.4" r="1.1" fill="currentColor" stroke="none"/><circle cx="10" cy="5.4" r="1.1" fill="currentColor" stroke="none"/><circle cx="14.6" cy="5.4" r="1.1" fill="currentColor" stroke="none"/><circle cx="5.4" cy="10" r="1.1" fill="currentColor" stroke="none"/><circle cx="10" cy="10" r="1.1" fill="currentColor" stroke="none"/><circle cx="14.6" cy="10" r="1.1" fill="currentColor" stroke="none"/><circle cx="5.4" cy="14.6" r="1.1" fill="currentColor" stroke="none"/><circle cx="10" cy="14.6" r="1.1" fill="currentColor" stroke="none"/><circle cx="14.6" cy="14.6" r="1.1" fill="currentColor" stroke="none"/>') },
    'A': { label: 'Audio', color: 'var(--cat-synths)', ic: ic('<path d="M4 8v4M7 5v10M10 3v14M13 6v8M16 8.5v3"/>') },
    'V': { label: 'Visual', color: 'var(--accent)', ic: ic('<path d="M10 3 11.7 8.3 17 10 11.7 11.7 10 17 8.3 11.7 3 10 8.3 8.3Z"/>') },
    'AV': { label: 'Audio + visual', color: 'var(--signal)', ic: ic('<rect x="3.5" y="3.5" width="9" height="9" rx="2"/><rect x="7.5" y="7.5" width="9" height="9" rx="2"/>') }
  };
  var FILTERS = ['All', 'A', 'V', 'AV'];

  var items = [], allCards = [], visibleCards = [], activeFilter = 'All', deck = null, playing = null;
  var params = new URLSearchParams(window.location.search);
  var qView = params.get('view');
  var mode = (qView === 'stack' || qView === 'grid' || qView === 'fan') ? qView : 'fan';
  var qTheme = params.get('mode');
  if (qTheme === 'midnight' || qTheme === 'paper' || qTheme === 'mist') document.documentElement.setAttribute('data-mode', qTheme);

  function esc(s) { return String(s == null ? '' : s).replace(/[&<>"]/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]; }); }

  /* an audio sample has no picture: its waveform is the picture */
  function waveImg(peaks) {
    var n = peaks.length, w = 720, h = 450, bw = w / n, bars = '';
    for (var i = 0; i < n; i++) {
      var bh = Math.max(3, peaks[i] * 250);
      bars += '<rect x="' + (i * bw + 1).toFixed(1) + '" y="' + ((h - bh) / 2).toFixed(1) + '" width="' + (bw - 2).toFixed(1) + '" height="' + bh.toFixed(1) + '" rx="2"/>';
    }
    var svg = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ' + w + ' ' + h + '"><defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#0e1330"/><stop offset="1" stop-color="#1c1a3a"/></linearGradient></defs><rect width="' + w + '" height="' + h + '" fill="url(#g)"/><g fill="#f5b3a0">' + bars + '</g></svg>';
    return 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(svg);
  }

  function card(it) {
    var k = KIND[it.kind] || KIND.V;
    var img = it.poster || waveImg(it.peaks);
    return '<article class="lib-card deck-card" data-id="' + esc(it.id) + '" data-kind="' + esc(it.kind) + '" data-name="' + esc(it.name) + '" data-patch="' + esc(it.patch) + '" data-meta="' + esc(it.tags.slice(0, 4).join(' · ')) + '" style="--cat:' + k.color + '">' +
      '<a class="lib-media-link" href="#" data-play aria-label="Play ' + esc(it.name) + '">' +
        '<img src="' + esc(img) + '" alt="" decoding="async" width="720" height="450">' +
      '</a>' +
      '<span class="lib-chip lib-chip-cat" title="' + esc(k.label) + '">' + k.ic + '</span>' +
      '<span class="lib-chip lib-chip-open" aria-hidden="true">' + IC_PLAY + '</span>' +
      '<div class="lib-acts">' +
        '<a class="lib-chip lib-act-btn lib-dl" href="' + esc(it.patch) + '" download="' + esc(it.name.replace(/\W+/g, '_')) + '.inf" aria-label="Download patch ' + esc(it.name) + '" title="Download patch (.inf)">' + IC_DL + '</a>' +
      '</div>' +
      '<h2 class="lib-card-title"><b>' + esc(it.name) + '</b><span>' + esc(it.tags.slice(0, 4).join(' · ')) + '</span></h2>' +
    '</article>';
  }

  /* playback: audio-only via Audio, visual and AV via a <video> laid over the poster */
  function stop() {
    if (!playing) return;
    var p = playing; playing = null;
    p.media.pause();
    if (p.el.parentNode) p.el.parentNode.removeChild(p.el);
    p.card.classList.remove('is-playing');
    syncPlayBtn();
  }
  function play(card) {
    if (playing && playing.card === card) { stop(); return; }
    stop();
    var it = items.filter(function (x) { return x.id === card.getAttribute('data-id'); })[0];
    if (!it) return;
    var link = card.querySelector('.lib-media-link'), el, media;
    if (it.video) {
      el = media = document.createElement('video');
      el.src = it.video; el.loop = true; el.playsInline = true; el.className = 'smp-video';
      if (it.kind === 'V') el.muted = true;
      link.appendChild(el);
    } else {
      media = new Audio(it.audio); media.loop = true;
      el = document.createElement('i'); el.className = 'smp-bar';
      media.addEventListener('timeupdate', function () { el.style.width = (100 * media.currentTime / (media.duration || 1)) + '%'; });
      link.appendChild(el);
    }
    playing = { card: card, media: media, el: el };
    card.classList.add('is-playing');
    media.play().catch(function () { stop(); });
    syncPlayBtn();
  }
  function syncPlayBtn() {
    if (!hudPlay) return;
    var c = visibleCards[curIdx];
    var on = playing && c && playing.card === c;
    hudPlay.innerHTML = on ? IC_PAUSE : IC_PLAY;
    hudPlay.setAttribute('aria-label', on ? 'Pause' : 'Play');
  }

  function renderDropdown() {
    chipsEl.innerHTML =
      '<button class="lib-select" type="button" aria-haspopup="listbox" aria-expanded="false" aria-label="Filter by kind">' +
        '<span class="lib-select-ic">' + KIND[activeFilter].ic + '</span>' +
        '<span class="lib-select-val">' + esc(KIND[activeFilter].label) + '</span>' + IC_CHEV +
      '</button>' +
      '<div class="lib-menu" role="listbox" aria-label="Kind" hidden>' +
        FILTERS.map(function (c) {
          return '<button class="lib-opt" type="button" role="option" aria-selected="' + (c === activeFilter) + '" data-f="' + c + '" aria-label="' + esc(KIND[c].label) + '" title="' + esc(KIND[c].label) + '">' + KIND[c].ic + '<span>' + esc(KIND[c].label) + '</span></button>';
        }).join('') +
      '</div>';
  }
  function setMenuOpen(open) {
    var m = chipsEl.querySelector('.lib-menu'), t = chipsEl.querySelector('.lib-select');
    if (!m || !t) return;
    m.hidden = !open; t.setAttribute('aria-expanded', String(open));
  }

  var curIdx = 0;
  function buildDots() {
    if (!dotsWrap) return;
    dotsWrap.innerHTML = '';
    var total = visibleCards.length;
    if (!total) return;
    var maxDots = Math.min(total, 12);
    for (var i = 0; i < maxDots; i++) {
      (function (slotIdx) {
        var b = document.createElement('button');
        b.type = 'button'; b.className = 'lib-dot'; b.setAttribute('aria-label', 'Jump to item');
        b.addEventListener('click', function () { deck.goTo(Math.round((slotIdx / Math.max(1, maxDots - 1)) * (total - 1))); });
        dotsWrap.appendChild(b);
      })(i);
    }
  }
  function updateHud(idx) {
    curIdx = idx;
    var total = visibleCards.length, c = visibleCards[idx];
    if (!total || !c) return;
    var k = KIND[c.getAttribute('data-kind')] || KIND.V;
    if (hudCatIc) hudCatIc.innerHTML = k.ic;
    if (hudTitle) hudTitle.textContent = c.getAttribute('data-name');
    if (hudMeta) hudMeta.textContent = c.getAttribute('data-meta');
    if (hudDl) {
      hudDl.href = c.getAttribute('data-patch');
      hudDl.setAttribute('download', c.getAttribute('data-name').replace(/\W+/g, '_') + '.inf');
    }
    if (dotsWrap && dotsWrap.children.length) {
      var dots = dotsWrap.children, a = dots.length <= 1 ? 0 : Math.round((idx / Math.max(1, total - 1)) * (dots.length - 1));
      for (var i = 0; i < dots.length; i++) dots[i].classList.toggle('is-on', i === a);
    }
    syncPlayBtn();
  }
  function rebuildVisible() {
    stop();
    visibleCards = [];
    allCards.forEach(function (c) {
      var show = activeFilter === 'All' || c.getAttribute('data-kind') === activeFilter;
      c.hidden = !show;
      if (show) visibleCards.push(c);
    });
    visibleCards.forEach(function (c, k) { c.setAttribute('data-bento', k % 7 === 0 ? 'big' : (k % 7 === 4 ? 'wide' : '')); });
    emptyEl.hidden = visibleCards.length > 0;
    stage.hidden = visibleCards.length === 0;
    var valEl = chipsEl.querySelector('.lib-select-val'), icEl = chipsEl.querySelector('.lib-select-ic');
    if (valEl) valEl.textContent = KIND[activeFilter].label;
    if (icEl) icEl.innerHTML = KIND[activeFilter].ic;
    Array.prototype.forEach.call(chipsEl.querySelectorAll('.lib-opt'), function (o) { o.setAttribute('aria-selected', String(o.getAttribute('data-f') === activeFilter)); });
    buildDots();
    if (deck) { deck.refresh(false); updateHud(0); }
  }
  function setView(m) {
    mode = m;
    if (viewEl) Array.prototype.forEach.call(viewEl.querySelectorAll('[data-view]'), function (b) {
      var on = b.getAttribute('data-view') === m;
      b.classList.toggle('is-on', on); b.setAttribute('aria-pressed', on ? 'true' : 'false');
    });
    if (hudWrap) hudWrap.hidden = (m === 'grid' || !visibleCards.length);
    if (deck) deck.setMode(m);
  }
  function reshuffleDeck() {
    for (var i = allCards.length - 1; i > 0; i--) {
      var j = Math.floor(Math.random() * (i + 1)), t = allCards[i]; allCards[i] = allCards[j]; allCards[j] = t;
    }
    allCards.forEach(function (c) { grid.appendChild(c); });
    rebuildVisible();
  }

  if (viewEl) Array.prototype.forEach.call(viewEl.querySelectorAll('[data-view]'), function (b) {
    b.addEventListener('click', function () { setView(b.getAttribute('data-view') || 'fan'); });
  });
  chipsEl.addEventListener('click', function (e) {
    if (e.target.closest('.lib-select')) { var m = chipsEl.querySelector('.lib-menu'); if (m) setMenuOpen(m.hidden); return; }
    var b = e.target.closest('.lib-opt');
    if (!b) return;
    activeFilter = b.getAttribute('data-f') || 'All';
    setMenuOpen(false); rebuildVisible();
  });
  document.addEventListener('click', function (e) { if (!chipsEl.contains(e.target)) setMenuOpen(false); });
  document.addEventListener('keydown', function (e) { if (e.key === 'Escape') setMenuOpen(false); });
  document.querySelectorAll('[data-lib-shuffle]').forEach(function (b) { b.addEventListener('click', reshuffleDeck); });
  document.querySelectorAll('[data-lib-prev]').forEach(function (b) { b.addEventListener('click', function () { deck.step(-1); }); });
  document.querySelectorAll('[data-lib-next]').forEach(function (b) { b.addEventListener('click', function () { deck.step(1); }); });
  if (hudPlay) hudPlay.addEventListener('click', function () { var c = visibleCards[curIdx]; if (c) play(c); });
  window.addEventListener('keydown', function (e) {
    if (e.metaKey || e.ctrlKey || e.altKey) return;
    var t = e.target && e.target.tagName ? e.target.tagName.toLowerCase() : '';
    if (t === 'input' || t === 'textarea' || t === 'select') return;
    if ((e.key === 'r' || e.key === 'R') && mode !== 'grid') reshuffleDeck();
    if (e.key === ' ' && mode !== 'grid') { e.preventDefault(); var c = visibleCards[curIdx]; if (c) play(c); }
  });

  var hero = stage.closest('.lib-hero') || stage;
  if (!(window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches)) {
    addEventListener('pointermove', function (e) {
      hero.style.setProperty('--px', (e.clientX / innerWidth - 0.5).toFixed(3));
      hero.style.setProperty('--py', (e.clientY / innerHeight - 0.5).toFixed(3));
    }, { passive: true });
  }

  renderDropdown();
  fetch('data.json?v=' + Date.now()).then(function (r) { return r.json(); }).then(function (d) {
    items = d.samples;
    for (var i = items.length - 1; i > 0; i--) { var j = Math.floor(Math.random() * (i + 1)), t = items[i]; items[i] = items[j]; items[j] = t; }
    grid.innerHTML = items.map(card).join('');
    allCards = Array.prototype.slice.call(grid.querySelectorAll('.lib-card'));
    grid.addEventListener('click', function (e) {
      if (e.target.closest('.lib-dl')) { e.stopPropagation(); return; }
      var a = e.target.closest('[data-play]');
      if (!a) return;
      e.preventDefault();
      var c = a.closest('.lib-card');
      if (mode === 'grid' || c === visibleCards[curIdx]) play(c);   // a side card comes forward first (deck handles the click)
    });
    deck = InfiniteDeck.create({ stage: stage, grid: grid, cardSel: '.lib-card', mode: mode, getCards: function () { return visibleCards; }, onChange: function (i) { stop(); updateHud(i); } });
    rebuildVisible();
    setView(mode);
  }).catch(function () { emptyEl.hidden = false; emptyEl.textContent = 'Could not load the samples.'; });
})();
