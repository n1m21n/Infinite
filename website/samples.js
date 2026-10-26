(function () {
  var listEl = document.getElementById('smp-list'), emptyEl = document.getElementById('smp-empty');
  var all = [], items = [], playing = null;
  // Generate: a new loop from the real Generate Samples engine, rendered here in the browser.
  var gens = [], genEl = document.getElementById('smp-gen'), genBtn = document.getElementById('smp-gen-btn');
  function byId(id) { return items.concat(gens).filter(function (x) { return x.id === id; })[0]; }
  // Filters: what a sample is (the Generate Samples node's parts; a whole mix is only under "all") and its key.
  var PARTS = ['synths', 'keys', 'leads', 'pads', 'plucks', 'textures', 'bass', 'drums', 'kicks', 'snares', 'hats', 'toms', 'percussion', 'fx', 'foley'];
  var filt = { part: '', scale: '' };
  // A key reads "g natural minor": the note, then the scale. Only the scale filters; 49 keys in one list is too many to scan.
  function scaleOf(x) { return x.key ? String(x.key).split(' ').slice(1).join(' ') : ''; }
  // Drums and percussion have no key, so a key filter never hides them.
  var KEYLESS = { drums: 1, percussion: 1, kicks: 1, snares: 1, hats: 1, toms: 1, fx: 1, foley: 1 };
  function match(x) {
    var keyed = !KEYLESS[x.part];
    return (!filt.part || x.part === filt.part) && (!keyed || !filt.scale || scaleOf(x) === filt.scale);
  }
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
  // The generator is the desktop engine compiled to wasm, run in a worker; it loads on the first press.
  var worker = null, info = null, jobs = {}, jobId = 0, serial = 0, busy = false;
  var SCALES = ['major', 'natural minor', 'harmonic minor', 'melodic minor', 'dorian', 'phrygian', 'lydian', 'mixolydian', 'locrian', 'major pentatonic', 'minor pentatonic', 'blues', 'whole tone', 'chromatic'];
  var PART_INDEX = { '': 0, synths: 1, pads: 2, plucks: 3, percussion: 4, drums: 5, bass: 6, fx: 7, foley: 8, keys: 9, leads: 10, textures: 11, kicks: 12, snares: 13, hats: 14, toms: 15 };
  function ask(msg) {
    if (!worker) {
      worker = new Worker('../gen/gen-worker.js');
      worker.onmessage = function (e) { var j = jobs[e.data.id]; delete jobs[e.data.id]; if (j) (e.data.error ? j[1](new Error(e.data.error)) : j[0](e.data)); };
      worker.onerror = function () { Object.keys(jobs).forEach(function (k) { jobs[k][1](new Error('worker')); delete jobs[k]; }); worker = null; info = null; };
    }
    return new Promise(function (ok, no) { msg.id = ++jobId; jobs[msg.id] = [ok, no]; worker.postMessage(msg); });
  }
  // 16-bit wav of an interleaved stereo float buffer.
  function wavOf(pcm, rate) {
    var n = pcm.length / 2, v = new DataView(new ArrayBuffer(44 + n * 4)), o = 44;
    function str(at, t) { for (var i = 0; i < t.length; i++) v.setUint8(at + i, t.charCodeAt(i)); }
    str(0, 'RIFF'); v.setUint32(4, 36 + n * 4, true); str(8, 'WAVEfmt '); v.setUint32(16, 16, true); v.setUint16(20, 1, true);
    v.setUint16(22, 2, true); v.setUint32(24, rate, true); v.setUint32(28, rate * 4, true); v.setUint16(32, 4, true); v.setUint16(34, 16, true);
    str(36, 'data'); v.setUint32(40, n * 4, true);
    for (var i = 0; i < pcm.length; i++) { var x = Math.max(-1, Math.min(1, pcm[i])); v.setInt16(o, x < 0 ? x * 32768 : x * 32767, true); o += 2; }
    return new Blob([v], { type: 'audio/wav' });
  }
  function peaksOf(pcm, bins) {
    var n = pcm.length / 2, out = [], m = 0, i, j;
    for (i = 0; i < bins; i++) {
      var a = Math.floor(i * n / bins), b = Math.floor((i + 1) * n / bins), pk = 0;
      for (j = a; j < b; j++) pk = Math.max(pk, Math.abs(pcm[j * 2]), Math.abs(pcm[j * 2 + 1]));
      out.push(pk); m = Math.max(m, pk);
    }
    return out.map(function (x) { return m ? x / m : 0; });
  }
  function downloadGen(it) {
    var a = document.createElement('a');
    a.href = it.audio; a.download = 'infinite-' + it.name.replace(/#/g, 's').replace(/\W+/g, '-') + '-' + it.bpm + 'bpm.wav';
    document.body.appendChild(a); a.click(); a.remove();
  }
  function generate() {
    if (busy) return;
    busy = true; genBtn.disabled = true; genBtn.classList.add('is-busy');
    var label = genBtn.textContent;
    if (!info) genBtn.setAttribute('data-label', label);
    genBtn.textContent = info ? 'Generating…' : 'Loading engine…';
    (info ? Promise.resolve(info) : ask({ cmd: 'info' }).then(function (d) { info = d; return d; })).then(function (d) {
      genBtn.textContent = 'Generating…';
      var scale = filt.scale ? d.scales.indexOf(filt.scale) : -1, part = filt.part;
      return ask({ cmd: 'run', style: Math.floor(Math.random() * d.styles.length), part: PART_INDEX[part] || 0, seed: Math.floor(Math.random() * 99999),
        bars: 2, key: -1, scale: KEYLESS[part] ? -1 : scale });
    }).then(function (r) {
      stop();
      var url = URL.createObjectURL(wavOf(r.pcm, r.rate));
      if (gens[0] && gens[0].audio) URL.revokeObjectURL(gens[0].audio);
      gens = [{ id: 'gen-' + (++serial), gen: true, name: r.label, bpm: Math.round(r.bpm), peaks: peaksOf(r.pcm, 64), audio: url }];
      genEl.hidden = false;
      genEl.innerHTML = row(gens[0]);
      var rw = genEl.querySelector('.smp-row');
      drawWave(rw.querySelector('canvas'), gens[0].peaks, 0);
      play(rw);
    }).catch(function () {
      genEl.hidden = false; genEl.innerHTML = '<p class="lib-empty">Could not generate a loop here. Try again.</p>';
    }).then(function () {
      busy = false; genBtn.disabled = false; genBtn.classList.remove('is-busy'); genBtn.textContent = genBtn.getAttribute('data-label') || label;
    });
  }
  genBtn.addEventListener('click', generate);

  document.addEventListener('click', function (e) {
    var g = e.target.closest('[data-gen]');
    if (g) { downloadGen(byId(g.getAttribute('data-gen'))); return; }
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
    items = all.filter(match);
    page = 0;
  }

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
    [['scale', 'any scale', scaleOf]].forEach(function (f) {
      var vals = SCALES;
      if (!vals.length) return;
      var sel = document.createElement('select');
      sel.className = 'smp-pick'; sel.setAttribute('aria-label', f[0]);
      sel.innerHTML = '<option value="">' + f[1] + '</option>' + vals.map(function (v) {
        return '<option value="' + esc(v) + '"' + (filt[f[0]] === v ? ' selected' : '') + '>' + esc(v) + '</option>';
      }).join('');
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
    if (!items.length && all.length) emptyEl.textContent = 'No saved samples match these filters. Press generate for a new one.';
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

  genBtn.hidden = typeof Worker === 'undefined' || typeof WebAssembly === 'undefined';
  fetch('data.json').then(function (r) { return r.json(); }).then(function (d) {
    all = d.samples.filter(function (x) { return x.kind === 'A'; }); shuffle(all);
    var qp = new URLSearchParams(location.search);
    ['part', 'scale'].forEach(function (k) { if (qp.get(k)) filt[k] = qp.get(k); });
    buildFilters(); applyFilters(); var q0 = new URLSearchParams(location.search).get('s'); if (q0) { var ix = items.findIndex(function (x) { return x.id === q0.replace(/[^\w-]/g, ''); }); if (ix > -1) page = Math.floor(ix / PAGE); } render(); var q = new URLSearchParams(location.search).get('s'), qs = q && q.replace(/[^\w-]/g, ''), tr = qs && (listEl.querySelector('.smp-row[data-id="' + qs + '"]')); if (tr) { tr.scrollIntoView({ block: 'center' }); tr.classList.add('is-hit'); } })
    .catch(function () { emptyEl.hidden = false; emptyEl.textContent = 'Could not load the samples.'; });
})();
