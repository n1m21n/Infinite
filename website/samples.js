(function () {
  var listEl = document.getElementById('smp-list'), emptyEl = document.getElementById('smp-empty');
  var all = [], items = [], playing = null;
  // Generate: a loop from the pool the real Generate Samples node rendered (pool.json), picked to match the filters.
  var pool = [], gens = [], genEl = document.getElementById('smp-gen'), genBtn = document.getElementById('smp-gen-btn');
  function byId(id) { return items.concat(gens).filter(function (x) { return x.id === id; })[0]; }
  // Filters: what a sample is (the Generate Samples node's parts; a whole mix is only under "all") and its key.
  var PARTS = ['synths', 'pads', 'plucks', 'percussion', 'drums'], filt = { part: '', key: '' };
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
    if (it.gen) return '<div class="smp-row" data-id="' + esc(it.id) + '" data-kind="A">' +
      '<button class="smp-play" type="button" aria-label="Play ' + esc(it.name) + '">' + IC_PLAY + '</button>' +
      '<div class="smp-id"><b>' + esc(it.name) + '</b><span>' + it.bpm + ' bpm</span></div>' +
      '<canvas class="smp-wave"></canvas>' +
      '<div class="smp-acts"><button class="smp-dlb" type="button" data-gen="' + esc(it.id) + '" aria-label="Download ' + esc(it.name) + ' as wav" title="Download .wav">' + IC_DL + '</button></div>' +
    '</div>';
    var baseName = it.name.replace(/\W+/g, '_');
    var patchLink = it.patch ? '<a href="' + esc(it.patch) + '" download="' + esc(baseName) + '.inf" role="menuitem"><b>.inf</b><span>patch</span></a>' : '';
    return '<div class="smp-row" data-id="' + esc(it.id) + '" data-kind="' + it.kind + '">' +
      '<button class="smp-play" type="button" aria-label="Play ' + esc(it.name) + '">' + IC_PLAY + '</button>' +
      '<div class="smp-id"><b>' + esc(it.name) + '</b></div>' +
      '<canvas class="smp-wave"></canvas>' +
      '<div class="smp-acts">' +
        '<button class="smp-dlb" type="button" aria-haspopup="menu" aria-expanded="false" aria-label="Download ' + esc(it.name) + '" title="Download">' + IC_DL + '</button>' +
        '<div class="smp-menu" role="menu" hidden>' +
          '<a href="' + esc(it.download) + '" download="' + esc(baseName) + '.wav" role="menuitem"><b>.wav</b><span>sound</span></a>' + patchLink +
        '</div>' +
      '</div>' +
    '</div>';
  }

  function closeMenus() {
    Array.prototype.forEach.call(document.querySelectorAll('.smp-menu:not([hidden])'), function (m) {
      m.hidden = true; m.parentNode.querySelector('.smp-dlb').setAttribute('aria-expanded', 'false'); m.closest('.smp-row').classList.remove('menu-open');
    });
  }
  // The pool is m4a; the .wav download is decoded and written here, 16-bit at the file's rate.
  function wavOf(buf) {
    var ch = buf.numberOfChannels, n = buf.length, v = new DataView(new ArrayBuffer(44 + n * ch * 2)), o = 44, d = [];
    function str(at, s) { for (var i = 0; i < s.length; i++) v.setUint8(at + i, s.charCodeAt(i)); }
    str(0, 'RIFF'); v.setUint32(4, 36 + n * ch * 2, true); str(8, 'WAVEfmt '); v.setUint32(16, 16, true); v.setUint16(20, 1, true);
    v.setUint16(22, ch, true); v.setUint32(24, buf.sampleRate, true); v.setUint32(28, buf.sampleRate * ch * 2, true);
    v.setUint16(32, ch * 2, true); v.setUint16(34, 16, true); str(36, 'data'); v.setUint32(40, n * ch * 2, true);
    for (var c = 0; c < ch; c++) d.push(buf.getChannelData(c));
    for (var i = 0; i < n; i++) for (c = 0; c < ch; c++) { var x = Math.max(-1, Math.min(1, d[c][i])); v.setInt16(o, x < 0 ? x * 32768 : x * 32767, true); o += 2; }
    return new Blob([v], { type: 'audio/wav' });
  }
  function downloadGen(it, btn) {
    btn.disabled = true;
    fetch(it.audio).then(function (r) { return r.arrayBuffer(); }).then(function (a) {
      var Ctx = window.AudioContext || window.webkitAudioContext, ctx = new Ctx();
      return new Promise(function (ok, no) { ctx.decodeAudioData(a, ok, no); }).then(function (buf) { ctx.close(); return buf; });
    }).then(function (buf) {
      var url = URL.createObjectURL(wavOf(buf)), a = document.createElement('a');
      a.href = url; a.download = 'infinite-' + it.name.replace(/#/g, 's').replace(/\W+/g, '-') + '-' + it.bpm + 'bpm.wav';
      document.body.appendChild(a); a.click(); a.remove();
      setTimeout(function () { URL.revokeObjectURL(url); }, 4000);
    }).catch(function () {}).then(function () { btn.disabled = false; });
  }
  function generate() {
    var drumsy = filt.part === 'drums' || filt.part === 'percussion';
    var fit = pool.filter(function (x) {
      return (!filt.part || x.part === filt.part) && (drumsy || !filt.key || x.key === filt.key);
    });
    if (!fit.length) { genEl.hidden = false; genEl.innerHTML = '<p class="lib-empty">No loop for these filters yet.</p>'; return; }
    var last = gens[0], pick = fit[Math.floor(Math.random() * fit.length)];
    if (fit.length > 1 && last && last.f === pick.f) pick = fit[(fit.indexOf(pick) + 1) % fit.length];
    stop();
    gens = [{ id: 'gen-' + pick.f, gen: true, f: pick.f, name: pick.label, bpm: pick.bpm, peaks: pick.peaks, audio: 'pool/' + pick.f + '.m4a' }];
    genEl.hidden = false;
    genEl.innerHTML = row(gens[0]);
    var r = genEl.querySelector('.smp-row');
    drawWave(r.querySelector('canvas'), gens[0].peaks, 0);
    play(r);
  }
  genBtn.addEventListener('click', generate);

  document.addEventListener('click', function (e) {
    var g = e.target.closest('[data-gen]');
    if (g) { downloadGen(byId(g.getAttribute('data-gen')), g); return; }
    var b = e.target.closest('.smp-dlb');
    var open = b && b.nextElementSibling.hidden;
    if (!e.target.closest('.smp-menu')) closeMenus();
    if (open) { b.nextElementSibling.hidden = false; b.setAttribute('aria-expanded', 'true'); b.closest('.smp-row').classList.add('menu-open'); }
  });
  document.addEventListener('keydown', function (e) { if (e.key === 'Escape') closeMenus(); });

  var PAGE = 25, page = 0, pagerEl = document.getElementById('smp-pager');
  pagerEl.addEventListener('click', function (e) {
    var b = e.target.closest('[data-pg]');
    if (!b || b.disabled) return;
    page += +b.getAttribute('data-pg'); render(); window.scrollTo({ top: 0 });
  });

  function applyFilters() {
    items = all.filter(function (x) {
      return (!filt.part || x.part === filt.part) && (!filt.key || x.key === filt.key);
    });
    page = 0;
  }

  function count(field, val) { return all.filter(function (x) { return x[field] === val; }).length; }
  function buildFilters() {
    var bar = document.querySelector('.smp-bar'), grow = bar.querySelector('.grow');
    var seg = document.createElement('div');
    seg.className = 'smp-seg'; seg.setAttribute('role', 'group'); seg.setAttribute('aria-label', 'Part');
    seg.innerHTML = [''].concat(PARTS).map(function (p) {
      return '<button type="button" data-part="' + p + '" aria-pressed="' + (filt.part === p) + '"' + (filt.part === p ? ' class="is-on"' : '') + '>' + (p || 'all') + '</button>';
    }).join('');
    seg.addEventListener('click', function (e) {
      var b = e.target.closest('[data-part]'); if (!b) return;
      filt.part = b.getAttribute('data-part');
      Array.prototype.forEach.call(seg.children, function (c) { var on = c === b; c.classList.toggle('is-on', on); c.setAttribute('aria-pressed', on); });
      applyFilters(); render();
    });
    bar.insertBefore(seg, grow);
    [['key', 'any key']].forEach(function (f) {
      var vals = all.concat(pool).map(function (x) { return x[f[0]]; }).filter(function (v, i, a) { return v && a.indexOf(v) === i; }).sort();
      if (!vals.length) return;
      var sel = document.createElement('select');
      sel.className = 'smp-pick'; sel.setAttribute('aria-label', f[0]);
      sel.innerHTML = '<option value="">' + f[1] + '</option>' + vals.map(function (v) { return '<option value="' + esc(v) + '"' + (filt[f[0]] === v ? ' selected' : '') + '>' + esc(v) + ' (' + count(f[0], v) + ')</option>'; }).join('');
      sel.addEventListener('change', function () { filt[f[0]] = sel.value; applyFilters(); render(); });
      bar.insertBefore(sel, grow);
    });
  }

  function render() {
    stop();
    var pages = Math.max(1, Math.ceil(items.length / PAGE));
    page = Math.min(page, pages - 1);
    var shown = items.slice(page * PAGE, page * PAGE + PAGE);
    listEl.innerHTML = shown.map(row).join('');
    emptyEl.hidden = items.length > 0;
    if (!items.length && all.length) emptyEl.textContent = 'No samples match these filters.';
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
    var it = byId(r.getAttribute('data-id')), media = new Audio(it.audio), el = null;
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

  function rowClick(e) {
    var r = e.target.closest('.smp-row');
    if (!r || e.target.closest('.smp-acts')) return;
    var cv = e.target.closest('canvas');
    if (cv && playing && playing.row === r) {   // click on the waveform seeks
      var b = cv.getBoundingClientRect(); playing.media.currentTime = (e.clientX - b.left) / b.width * (playing.media.duration || 0); return;
    }
    play(r);
  }
  listEl.addEventListener('click', rowClick);
  genEl.addEventListener('click', rowClick);
  document.getElementById('smp-shuffle').addEventListener('click', function () { shuffle(all); applyFilters(); render(); });
  window.addEventListener('resize', function () {
    Array.prototype.forEach.call(document.querySelectorAll('.smp-wave'), function (cv) {
      var r = cv.closest('.smp-row'), it = byId(r.getAttribute('data-id'));
      drawWave(cv, it.peaks, playing && playing.row === r ? playing.media.currentTime / (playing.media.duration || 1) : 0);
    });
  });
  window.addEventListener('keydown', function (e) { if (e.key === ' ' && playing && e.target === document.body) { e.preventDefault(); play(playing.row); } });

  var poolReady = fetch('pool.json').then(function (r) { return r.ok ? r.json() : { pool: [] }; })
    .then(function (d) { pool = d.pool || []; genBtn.hidden = !pool.length; }).catch(function () {});
  Promise.all([fetch('data.json').then(function (r) { return r.json(); }), poolReady]).then(function (res) { var d = res[0];
    all = d.samples.filter(function (x) { return x.kind === 'A'; }); shuffle(all);
    var qp = new URLSearchParams(location.search);
    ['part', 'key'].forEach(function (k) { if (qp.get(k)) filt[k] = qp.get(k); });
    buildFilters(); applyFilters(); var q0 = new URLSearchParams(location.search).get('s'); if (q0) { var ix = items.findIndex(function (x) { return x.id === q0.replace(/[^\w-]/g, ''); }); if (ix > -1) page = Math.floor(ix / PAGE); } render(); var q = new URLSearchParams(location.search).get('s'), qs = q && q.replace(/[^\w-]/g, ''), tr = qs && (listEl.querySelector('.smp-row[data-id="' + qs + '"]')); if (tr) { tr.scrollIntoView({ block: 'center' }); tr.classList.add('is-hit'); } })
    .catch(function () { emptyEl.hidden = false; emptyEl.textContent = 'Could not load the samples.'; });
})();
