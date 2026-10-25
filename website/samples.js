(function () {
  var listEl = document.getElementById('smp-list'), countEl = document.getElementById('smp-count'), emptyEl = document.getElementById('smp-empty');
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

  var IC_PATCH = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="3.5" y="3.5" width="9" height="9" rx="2"/><rect x="7.5" y="7.5" width="9" height="9" rx="2"/></svg>';

  function row(it) {
    var thumb = it.poster ? '<img src="../' + esc(it.poster) + '" alt="" loading="lazy" decoding="async">' : '<span class="smp-glyph">' + IC_PLAY.replace('<svg', '<svg class="smp-g"') + '</span>';
    return '<div class="smp-row" data-id="' + esc(it.id) + '" data-kind="' + it.kind + '">' +
      '<button class="smp-play" type="button" aria-label="Play ' + esc(it.name) + '">' + IC_PLAY + '</button>' +
      '<div class="smp-thumb">' + (it.poster ? thumb : '') + '</div>' +
      '<div class="smp-id"><b>' + esc(it.name) + '</b></div>' +
      (it.audio ? '<canvas class="smp-wave"></canvas>' : '<div class="smp-track"><i></i></div>') +
      '<div class="smp-acts">' +
        (it.library ? '<a class="smp-dl smp-lib" href="../library/' + esc(it.library) + '.html" aria-label="Open the patch in the Library" title="Patch in the Library">' + IC_PATCH + '</a>' : '') +
        '<a class="smp-dl" href="../' + esc(it.download) + '" download="' + esc(it.name.replace(/\W+/g, '_')) + (it.audio ? '.wav' : '.mp4') + '" aria-label="Download ' + (it.audio ? 'wav' : 'mp4') + ' ' + esc(it.name) + '" title="Download ' + (it.audio ? '.wav' : '.mp4') + '">' + IC_DL + '</a>' +
      '</div>' +
    '</div>';
  }

  function render() {
    stop();
    listEl.innerHTML = items.map(row).join('');
    countEl.textContent = items.length + ' samples';
    emptyEl.hidden = items.length > 0;
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
    var it = items.filter(function (x) { return x.id === r.getAttribute('data-id'); })[0], media, el = null;
    if (it.video) {
      el = media = document.createElement('video');
      media.src = '../' + it.video; media.loop = true; media.playsInline = true; media.className = 'smp-vid';
      if (it.kind === 'V') media.muted = true;
      r.querySelector('.smp-thumb').appendChild(el);
    } else {
      media = new Audio('../' + it.audio); media.loop = true;
    }
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
  document.getElementById('smp-shuffle').addEventListener('click', function () { shuffle(items); render(); });
  window.addEventListener('resize', function () {
    Array.prototype.forEach.call(listEl.querySelectorAll('canvas'), function (cv) {
      var r = cv.closest('.smp-row'), it = items.filter(function (x) { return x.id === r.getAttribute('data-id'); })[0];
      drawWave(cv, it.peaks, playing && playing.row === r ? playing.media.currentTime / (playing.media.duration || 1) : 0);
    });
  });
  window.addEventListener('keydown', function (e) { if (e.key === ' ' && playing && e.target === document.body) { e.preventDefault(); play(playing.row); } });

  fetch('../data.json?v=' + Date.now()).then(function (r) { return r.json(); }).then(function (d) { items = d.samples.filter(function (x) { return x.kind === 'A'; }); shuffle(items); render(); var q = new URLSearchParams(location.search).get('s'), qs = q && q.replace(/[^\w-]/g, ''), tr = qs && (listEl.querySelector('.smp-row[data-id="' + qs + '"]')); if (tr) { tr.scrollIntoView({ block: 'center' }); tr.classList.add('is-hit'); } })
    .catch(function () { emptyEl.hidden = false; emptyEl.textContent = 'Could not load the samples.'; });
})();
