(function () {
  var listEl = document.getElementById('smp-list'), emptyEl = document.getElementById('smp-empty');
  var items = [], playing = null;
  var IC_PLAY = '<svg viewBox="0 0 20 20" fill="currentColor" aria-hidden="true"><path d="M7 4.6v10.8a.6.6 0 0 0 .9.5l8.6-5.4a.6.6 0 0 0 0-1L7.9 4.1a.6.6 0 0 0-.9.5Z"/></svg>';
  var IC_PAUSE = '<svg viewBox="0 0 20 20" fill="currentColor" aria-hidden="true"><rect x="5" y="4" width="3.6" height="12" rx="1"/><rect x="11.4" y="4" width="3.6" height="12" rx="1"/></svg>';
  var IC_DL = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M10 3.5v9.5m0 0-3.5-3.5M10 13l3.5-3.5M4 16.5h12"/></svg>';

  function esc(s) { return String(s == null ? '' : s).replace(/[&<>"]/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]; }); }
  function shuffle(a) { for (var i = a.length - 1; i > 0; i--) { var j = Math.floor(Math.random() * (i + 1)), t = a[i]; a[i] = a[j]; a[j] = t; } }

  function drawWave(cv, peaks, frac) {
    var dpr = window.devicePixelRatio || 1, w = cv.clientWidth, h = cv.clientHeight;
    if (!w) return;
    cv.width = w * dpr; cv.height = h * dpr;
    var g = cv.getContext('2d'); g.scale(dpr, dpr);
    var st = getComputedStyle(cv), ink = st.color, n = peaks.length, bw = w / n;
    for (var i = 0; i < n; i++) {
      g.globalAlpha = (i / n) <= frac ? 1 : 0.32;
      g.fillStyle = (i / n) <= frac ? st.getPropertyValue('--smp-on') || ink : ink;
      var bh = Math.max(2, peaks[i] * h);
      g.fillRect(i * bw + 0.5, (h - bh) / 2, Math.max(1, bw - 1.5), bh);
    }
  }

  function row(it) {
    var baseName = it.name.replace(/\W+/g, '_');
    var patchBtn = it.patch ? '<a class="smp-dl smp-dl-patch" href="' + esc(it.patch) + '" download="' + esc(baseName) + '.inf" aria-label="Download patch ' + esc(it.name) + '" title="Download .inf patch">' + IC_DL + '<span class="smp-fmt">.inf</span></a>' : '';
    return '<div class="smp-row" data-id="' + esc(it.id) + '" data-kind="' + it.kind + '">' +
      '<button class="smp-play" type="button" aria-label="Play ' + esc(it.name) + '">' + IC_PLAY + '</button>' +
      '<div class="smp-id"><b>' + esc(it.name) + '</b></div>' +
      '<canvas class="smp-wave"></canvas>' +
      '<div class="smp-acts">' +
        '<a class="smp-dl" href="' + esc(it.download) + '" download="' + esc(baseName) + '.wav" aria-label="Download wav ' + esc(it.name) + '" title="Download .wav">' + IC_DL + '<span class="smp-fmt">.wav</span></a>' +
        patchBtn +
      '</div>' +
    '</div>';
  }

  var PAGE = 25, page = 0, pagerEl = document.getElementById('smp-pager');
  pagerEl.addEventListener('click', function (e) {
    var b = e.target.closest('[data-pg]');
    if (!b || b.disabled) return;
    page += +b.getAttribute('data-pg'); render(); window.scrollTo({ top: 0 });
  });

  function render() {
    stop();
    var pages = Math.max(1, Math.ceil(items.length / PAGE));
    page = Math.min(page, pages - 1);
    var shown = items.slice(page * PAGE, page * PAGE + PAGE);
    listEl.innerHTML = shown.map(row).join('');
    emptyEl.hidden = items.length > 0;
    pagerEl.hidden = pages < 2;
    pagerEl.querySelector('.smp-pg-info').textContent = (page + 1) + ' / ' + pages;
    pagerEl.querySelector('[data-pg="-1"]').disabled = page === 0;
    pagerEl.querySelector('[data-pg="1"]').disabled = page >= pages - 1;
    Array.prototype.forEach.call(listEl.querySelectorAll('.smp-row'), function (r) {
      var it = items.filter(function (x) { return x.id === r.getAttribute('data-id'); })[0], cv = r.querySelector('canvas');
      if (cv) drawWave(cv, it.peaks, 0);
    });
  }

  function stop() {
    if (!playing) return;
    var p = playing; playing = null;
    p.media.pause();
    if (p.el) p.el.remove();
    p.row.classList.remove('on');
    p.row.querySelector('.smp-play').innerHTML = IC_PLAY;
    var cv = p.row.querySelector('canvas');
    if (cv) drawWave(cv, p.it.peaks, 0);
    var bar = p.row.querySelector('.smp-track i'); if (bar) bar.style.width = '0';
  }

  function play(r) {
    if (playing && playing.row === r) { stop(); return; }
    stop();
    var it = items.filter(function (x) { return x.id === r.getAttribute('data-id'); })[0], media = new Audio(it.audio), el = null;
    media.loop = true;
    playing = { row: r, it: it, media: media, el: el };
    r.classList.add('on');
    r.querySelector('.smp-play').innerHTML = IC_PAUSE;
    media.addEventListener('timeupdate', function () {
      var f = media.currentTime / (media.duration || 1), cv = r.querySelector('canvas'), bar = r.querySelector('.smp-track i');
      if (cv) drawWave(cv, it.peaks, f);
      if (bar) bar.style.width = (100 * f) + '%';
    });
    media.play().catch(stop);
  }

  listEl.addEventListener('click', function (e) {
    var r = e.target.closest('.smp-row');
    if (!r || e.target.closest('.smp-dl')) return;
    var cv = e.target.closest('canvas');
    if (cv && playing && playing.row === r) {   // click on the waveform seeks
      var b = cv.getBoundingClientRect(); playing.media.currentTime = (e.clientX - b.left) / b.width * (playing.media.duration || 0); return;
    }
    play(r);
  });
  document.getElementById('smp-shuffle').addEventListener('click', function () { shuffle(items); page = 0; render(); });
  window.addEventListener('resize', function () {
    Array.prototype.forEach.call(listEl.querySelectorAll('canvas'), function (cv) {
      var r = cv.closest('.smp-row'), it = items.filter(function (x) { return x.id === r.getAttribute('data-id'); })[0];
      drawWave(cv, it.peaks, playing && playing.row === r ? playing.media.currentTime / (playing.media.duration || 1) : 0);
    });
  });
  window.addEventListener('keydown', function (e) { if (e.key === ' ' && playing && e.target === document.body) { e.preventDefault(); play(playing.row); } });

  fetch('data.json').then(function (r) { return r.json(); }).then(function (d) { items = d.samples.filter(function (x) { return x.kind === 'A'; }); shuffle(items); var q0 = new URLSearchParams(location.search).get('s'); if (q0) { var ix = items.findIndex(function (x) { return x.id === q0.replace(/[^\w-]/g, ''); }); if (ix > -1) page = Math.floor(ix / PAGE); } render(); var q = new URLSearchParams(location.search).get('s'), qs = q && q.replace(/[^\w-]/g, ''), tr = qs && (listEl.querySelector('.smp-row[data-id="' + qs + '"]')); if (tr) { tr.scrollIntoView({ block: 'center' }); tr.classList.add('is-hit'); } })
    .catch(function () { emptyEl.hidden = false; emptyEl.textContent = 'Could not load the samples.'; });
})();
