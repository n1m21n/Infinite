(function () {
  var listEl = document.getElementById('smp-list'), emptyEl = document.getElementById('smp-empty');
  var all = [], items = [], playing = null;
  // Generate: a new loop from the real Generate Samples engine, rendered here in the browser.
  var gens = [], genEl = document.getElementById('smp-gen'), genBtn = document.getElementById('smp-gen-btn');
  function byId(id) { return items.concat(gens).filter(function (x) { return x.id === id; })[0]; }
  // Filters: what a sample is (the Generate Samples node's parts; a whole mix is only under "all") and its key.
  var PARTS = ['synths', 'keys', 'leads', 'pads', 'plucks', 'textures', 'bass', 'drums', 'kicks', 'snares', 'hats', 'toms', 'percussion', 'fx'];
  var filt = { part: '', scale: '', key: '' };
  var NOTES = ['c', 'c#', 'd', 'd#', 'e', 'f', 'f#', 'g', 'g#', 'a', 'a#', 'b'];
  function noteOf(x) { return x.key ? String(x.key).split(' ')[0] : ''; }
  // A key reads "g natural minor": the note, then the scale. Only the scale filters; 49 keys in one list is too many to scan.
  function scaleOf(x) { return x.key ? String(x.key).split(' ').slice(1).join(' ') : ''; }
  // Drums and percussion have no key, so a key filter never hides them.
  var KEYLESS = { drums: 1, percussion: 1, kicks: 1, snares: 1, hats: 1, toms: 1, fx: 1 };
  function match(x) {
    var keyed = !KEYLESS[x.part];
    keyed = keyed && !!x.key;
    return (!filt.part || x.part === filt.part) && (!keyed || !filt.scale || scaleOf(x) === filt.scale) && (!keyed || !filt.key || noteOf(x) === filt.key);
  }
  var IC_PLAY = '<svg viewBox="0 0 20 20" fill="currentColor" aria-hidden="true"><path d="M7 4.6v10.8a.6.6 0 0 0 .9.5l8.6-5.4a.6.6 0 0 0 0-1L7.9 4.1a.6.6 0 0 0-.9.5Z"/></svg>';
  var IC_PAUSE = '<svg viewBox="0 0 20 20" fill="currentColor" aria-hidden="true"><rect x="5" y="4" width="3.6" height="12" rx="1"/><rect x="11.4" y="4" width="3.6" height="12" rx="1"/></svg>';
  var IC_SH = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M10 12.5V3.5M6.8 6.5L10 3.3l3.2 3.2M5.5 9H5a1.5 1.5 0 00-1.5 1.5v5A1.5 1.5 0 005 17h10a1.5 1.5 0 001.5-1.5v-5A1.5 1.5 0 0015 9h-.5"/></svg>';
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
    if (it.recipe) return rowHtml(it, '<a href="#" data-lazy="wav" role="menuitem"><b>.wav</b><span>sound</span></a><a href="#" data-lazy="inf" role="menuitem"><b>.inf</b><span>patch</span></a>');
    var patchLink = it.patch ? '<a href="' + esc(it.patch) + '" download="' + esc(baseName) + '.inf" role="menuitem"><b>.inf</b><span>patch</span></a>' : '';
    return rowHtml(it, '<a href="' + esc(it.download) + '" download="' + esc(baseName) + '.wav" role="menuitem"><b>.wav</b><span>sound</span></a>' + patchLink);
  }
  function rowHtml(it, links) {
    return '<div class="smp-row" data-id="' + esc(it.id) + '" data-kind="' + it.kind + '">' +
      '<button class="smp-play" type="button" aria-label="Play ' + esc(it.name) + '">' + IC_PLAY + '</button>' +
      '<div class="smp-id"><b>' + esc(it.name) + '</b>' + (it.bpm ? '<span>' + it.bpm + ' bpm</span>' : '') + '</div>' +
      '<canvas class="smp-wave"></canvas>' +
      '<div class="smp-acts">' +
        '<button class="smp-dlb" type="button" aria-haspopup="menu" aria-expanded="false" aria-label="Download ' + esc(it.name) + '" title="Download">' + IC_DL + '</button>' +
        '<div class="smp-menu" role="menu" hidden>' + links + '</div>' +
        '<button class="smp-shb" type="button" data-share="s=' + esc(it.id) + '" aria-label="Share ' + esc(it.name) + '" title="Share">' + IC_SH + '</button>' +
      '</div>' +
    '</div>';
  }

  function closeMenus() {
    Array.prototype.forEach.call(document.querySelectorAll('.smp-menu:not([hidden])'), function (m) {
      m.hidden = true; m.parentNode.querySelector('.smp-dlb').setAttribute('aria-expanded', 'false'); var rw = m.closest('.smp-row'); if (rw) rw.classList.remove('menu-open');
    });
  }
  // The generator is the desktop engine compiled to wasm, run in a worker; it loads on the first press.
  var worker = null, info = null, jobs = {}, jobId = 0, serial = 0, busy = false;
  var SCALES = ['major', 'natural minor', 'harmonic minor', 'melodic minor', 'dorian', 'phrygian', 'lydian', 'mixolydian', 'locrian', 'major pentatonic', 'minor pentatonic', 'blues', 'whole tone', 'chromatic'];
  var PART_INDEX = { '': 0, synths: 1, pads: 2, plucks: 3, percussion: 4, drums: 5, bass: 6, fx: 7, foley: 8, keys: 9, leads: 10, textures: 11, kicks: 12, snares: 13, hats: 14, toms: 15 };
  function ask(msg) {
    if (!worker) {
      worker = new Worker('../gen/gen-worker.js?v=8');
      worker.onmessage = function (e) { var j = jobs[e.data.id]; delete jobs[e.data.id]; var d = e.data; if (j) (d.error || (d.frames && String(d.patch).indexOf('infinite-patch') !== 0) ? j[1](new Error(d.error || 'stale generator')) : j[0](d)); };
      worker.onerror = function () { Object.keys(jobs).forEach(function (k) { jobs[k][1](new Error('worker')); delete jobs[k]; }); worker = null; info = null; };
    }
    return new Promise(function (ok, no) { msg.id = ++jobId; jobs[msg.id] = [ok, no]; worker.postMessage(msg); });
  }
  // 16-bit wav of an interleaved stereo float buffer.
  function wavOf(pcm, rate, name, comment) {
    // LIST/INFO carries name, artist, copyright and comment, so the credit travels with the file into any DAW.
    var tags = [['INAM', name], ['IART', 'Infinite (Naman Soni, n1m21n)'], ['ICOP', 'Free for commercial use if credited: Sample from Infinite by Naman Soni (n1m21n), infinitedevices.org'], ['ICMT', comment], ['ISFT', 'Infinite']]
      .map(function (t) { var b = new TextEncoder().encode(t[1] + '\0'); return [t[0], b, b.length + (b.length & 1)]; });
    var infoLen = 4 + tags.reduce(function (a, t) { return a + 8 + t[2]; }, 0);
    var n = pcm.length / 2, v = new DataView(new ArrayBuffer(44 + n * 4 + 8 + infoLen)), o = 44;
    function str(at, t) { for (var i = 0; i < t.length; i++) v.setUint8(at + i, t.charCodeAt(i)); }
    str(0, 'RIFF'); v.setUint32(4, 36 + n * 4 + 8 + infoLen, true); str(8, 'WAVEfmt '); v.setUint32(16, 16, true); v.setUint16(20, 1, true);
    v.setUint16(22, 2, true); v.setUint32(24, rate, true); v.setUint32(28, rate * 4, true); v.setUint16(32, 4, true); v.setUint16(34, 16, true);
    str(36, 'data'); v.setUint32(40, n * 4, true);
    for (var i = 0; i < pcm.length; i++) { var x = Math.max(-1, Math.min(1, pcm[i])); v.setInt16(o, x < 0 ? x * 32768 : x * 32767, true); o += 2; }
    str(o, 'LIST'); v.setUint32(o + 4, infoLen, true); str(o + 8, 'INFO'); o += 12;
    tags.forEach(function (t) { str(o, t[0]); v.setUint32(o + 4, t[1].length, true); new Uint8Array(v.buffer).set(t[1], o + 8); o += 8 + t[2]; });
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
  // A library take with a recipe has no file: the engine renders the same loop here on first play or download.
  function realize(it) {
    if (it.audio) return Promise.resolve(it);
    if (it.pending) return it.pending;
    it.pending = (info ? Promise.resolve(info) : ask({ cmd: 'info' }).then(function (d) { info = d; return d; })).then(function () {
      var r0 = it.recipe;
      return ask({ cmd: 'run', style: r0.s, part: r0.p, seed: r0.seed, bars: r0.b, key: -1, scale: r0.sc });
    }).then(function (r) {
      it.audio = URL.createObjectURL(wavOf(r.pcm, r.rate, r.label, r.label + ', ' + Math.round(r.bpm) + ' bpm. Generated by Infinite, https://infinitedevices.org/samples/'));
      it.patch = URL.createObjectURL(new Blob([r.patch], { type: 'text/plain' }));
      it.download = it.audio;
      return it;
    }).catch(function (e) { it.pending = null; throw e; });
    return it.pending;
  }
  function saveAs(url, name) { var a = document.createElement('a'); a.href = url; a.download = name; document.body.appendChild(a); a.click(); a.remove(); }
  function genFile(it) { return 'infinite-' + it.name.replace(/#/g, 's').replace(/\W+/g, '-') + '-' + it.bpm + 'bpm'; }
  // Personal taste layer in the browser: Keep (download or >=12s listen) vs Skip (<8s redraw) updates Beta-Bernoulli style weights.
  var taste = (function () {
    try { return JSON.parse(localStorage.getItem('infinite_taste_v1') || '{}'); } catch (e) { return {}; }
  })();
  var pendGen = null;
  function saveTaste() { try { localStorage.setItem('infinite_taste_v1', JSON.stringify(taste)); } catch (e) {} }
  function settleWebTaste(nextNow) {
    if (!pendGen || pendGen.settled) return;
    var age = (Date.now() - pendGen.t) / 1000;
    if (!nextNow && age < 12) return;
    var k = pendGen.part + ':' + pendGen.style;
    var row = taste[k] || [0, 0];
    if (pendGen.kept || age >= 12) row[0] += 1;
    else if (nextNow && age < 8) row[1] += 1;
    else return;
    taste[k] = row; pendGen.settled = true; saveTaste();
  }
  function drawStyle(n, part) {
    var w = [], sum = 0, total = 0, i, k, r;
    for (i = 0; i < n; i++) {
      r = taste[part + ':' + i] || [0, 0];
      total += r[0] + r[1];
    }
    var trust = Math.min(0.9, total / (total + 6));
    for (i = 0; i < n; i++) {
      r = taste[part + ':' + i] || [0, 0];
      var post = (r[0] + 1) / (r[0] + r[1] + 2);
      var wi = (1 - trust) * (1 / n) + trust * post;
      w.push(wi); sum += wi;
    }
    var u = Math.random() * sum;
    for (i = 0; i < n; i++) { u -= w[i]; if (u <= 0) return i; }
    return Math.floor(Math.random() * n);
  }
  setInterval(function () { settleWebTaste(false); }, 2000);
  function generate(fixed) {
    if (busy) return;
    if (!fixed || !fixed.seed) fixed = null;
    settleWebTaste(true);
    busy = true; wakeAudio(); genBtn.disabled = true; genBtn.classList.add('is-busy');
    var label = genBtn.textContent;
    if (!info) genBtn.setAttribute('data-label', label);
    genBtn.textContent = info ? 'Generating…' : 'Loading engine…';
    (info ? Promise.resolve(info) : ask({ cmd: 'info' }).then(function (d) { info = d; return d; })).then(function (d) {
      genBtn.textContent = 'Generating…';
      var scale = gf.scale ? d.scales.indexOf(gf.scale) : -1, part = gf.part, key = gf.key ? NOTES.indexOf(gf.key) : -1;
      // Textures and foley are one choice here: a keyed texture or a keyless found sound, drawn at random.
      var pi = part === 'textures' && Math.random() < 0.4 ? PART_INDEX.foley : PART_INDEX[part] || 0;
      if (pi === PART_INDEX.foley) part = 'foley';
      if (fixed) { pi = fixed.part; part = fixed.partName || part; }
      var stIdx = fixed ? fixed.style : drawStyle(d.styles.length, pi);
      pendGen = { style: stIdx, part: pi, t: Date.now(), kept: false, settled: false };
      var seed = fixed ? fixed.seed : Math.floor(Math.random() * 99999);
      if (fixed) { key = fixed.key; scale = fixed.scale; }
      shareCode = [stIdx, pi, seed, key, scale].join('.');
      return ask({ cmd: 'run', style: stIdx, part: pi, seed: seed,
        bars: 2, key: fixed ? key : KEYLESS[part] || part === 'foley' ? -1 : key, scale: fixed ? scale : KEYLESS[part] || part === 'foley' ? -1 : scale });
    }).then(function (r) {
      stop();
      var url = URL.createObjectURL(wavOf(r.pcm, r.rate, r.label, r.label + ', ' + Math.round(r.bpm) + ' bpm. Generated by Infinite, https://infinitedevices.org/samples/'));
      var infUrl = URL.createObjectURL(new Blob([r.patch], { type: 'text/plain' }));
      if (gens[0] && gens[0].patch) URL.revokeObjectURL(gens[0].patch);
      if (gens[0] && gens[0].audio) URL.revokeObjectURL(gens[0].audio);
      gens = [{ id: 'gen-' + (++serial), gen: true, name: r.label.replace(/ \d+(?=,|$)/, ''), bpm: Math.round(r.bpm), audio: url, patch: infUrl, pcm: r.pcm, rate: r.rate, share: shareCode }];
      genEl.hidden = false;
      var gf0 = genFile(gens[0]);
      genEl.innerHTML = '<div class="smp-id"><b>' + esc(gens[0].name) + '</b><span>' + Math.round(r.bpm) + ' bpm</span></div>' +
        '<div class="smp-acts"><button class="smp-dlb" type="button" aria-haspopup="menu" aria-expanded="false" aria-label="Download ' + esc(gens[0].name) + '" title="Download">' + IC_DL + '</button>' +
        '<div class="smp-menu" role="menu" hidden>' +
          '<a href="' + url + '" download="' + esc(gf0) + '.wav" role="menuitem"><b>.wav</b><span>sound</span></a>' +
          '<a href="' + infUrl + '" download="' + esc(gf0) + '.inf" role="menuitem"><b>.inf</b><span>patch</span></a>' +
        '</div><button class="smp-shb" type="button" data-share="g=' + gens[0].share + '" aria-label="Share this sample" title="Share">' + IC_SH + '</button></div>';
      blobPlay(gens[0]);
    }).catch(function () {
      genEl.hidden = false; genEl.innerHTML = '<p class="lib-empty">Could not generate a loop here. Try again.</p>';
    }).then(function () {
      busy = false; genBtn.disabled = false; genBtn.classList.remove('is-busy'); genBtn.textContent = genBtn.getAttribute('data-label') || label;
    });
  }
  var shareCode = '';
  genBtn.addEventListener('click', function () { generate(); });
  // A shared generated sample: ?g=style.part.seed.key.scale re-renders exactly the same loop.
  (function () {
    var g = new URLSearchParams(location.search).get('g');
    var m = g && /^(\d+)\.(\d+)\.(\d+)\.(-?\d+)\.(-?\d+)$/.exec(g);
    if (m) generate({ style: +m[1], part: +m[2], seed: +m[3], key: +m[4], scale: +m[5] });
  })();
  document.addEventListener('click', function (e) {
    var sb = e.target.closest('.smp-shb');
    if (!sb) return;
    var link = location.origin + location.pathname + '?' + sb.getAttribute('data-share');
    var done = function () { sb.classList.add('copied'); sb.setAttribute('title', 'Link copied'); setTimeout(function () { sb.classList.remove('copied'); sb.setAttribute('title', 'Share'); }, 1600); };
    // Phones: the native share sheet. Elsewhere (or if it is dismissed unsupported): copy the link.
    if (navigator.share) { navigator.share({ title: 'Infinite sample', text: 'A sample made with Infinite', url: link }).catch(function (err) { if (err && err.name !== 'AbortError') copy(); }); return; }
    copy();
    function copy() {
    if (navigator.clipboard && navigator.clipboard.writeText) navigator.clipboard.writeText(link).then(done, function () { window.prompt('Copy this link', link); });
    else window.prompt('Copy this link', link);
    }
  });

  document.addEventListener('click', function (e) {
    if (e.target.closest('#smp-gen .smp-menu a') && pendGen) { pendGen.kept = true; settleWebTaste(true); }
    var lz = e.target.closest('[data-lazy]');
    if (lz) {
      e.preventDefault();
      var lit = byId(lz.closest('.smp-row').getAttribute('data-id')), kind = lz.getAttribute('data-lazy');
      realize(lit).then(function () { saveAs(kind === 'inf' ? lit.patch : lit.audio, 'infinite-' + lit.name.replace(/#/g, 's').replace(/\W+/g, '-') + (lit.bpm ? '-' + lit.bpm + 'bpm' : '') + '.' + kind); closeMenus(); });
      return;
    }
    var b = e.target.closest('.smp-dlb');
    var open = b && b.nextElementSibling.hidden;
    if (!e.target.closest('.smp-menu')) closeMenus();
    if (open) { b.nextElementSibling.hidden = false; b.setAttribute('aria-expanded', 'true'); var rw = b.closest('.smp-row'); if (rw) rw.classList.add('menu-open'); }
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

  // Instrument and scale are always dropdowns: one pair for the generator, one for the saved list.
  var gf = { part: '', scale: '', key: '' };
  var PICK_IC = {
    part: '<path d="M3 10h1.5M7 6v8M10.5 3.5v13M14 7v6M17 10h-1.5"/>',
    key: '<path d="M8 15V4.5l7-1.5v10.5"/><circle cx="6" cy="15" r="2"/><circle cx="13" cy="13.5" r="2"/>',
    scale: '<path d="M3 16h3.5v-3.5H10V9h3.5V5.5H17"/>'
  };
  function pick(state, key, label, vals, onchange) {
    var w = document.createElement('label');
    w.className = 'smp-pickw'; w.title = label;
    var sel = document.createElement('select');
    sel.className = 'smp-pick'; sel.setAttribute('aria-label', label);
    sel.innerHTML = '<option value="">' + label + '</option>' + vals.map(function (v) {
      return '<option value="' + esc(v) + '"' + (state[key] === v ? ' selected' : '') + '>' + esc(v) + '</option>';
    }).join('');
    w.innerHTML = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">' + PICK_IC[key] + '</svg><span class="smp-pickv"></span>';
    w.appendChild(sel);
    var show = function () { var v = w.querySelector('.smp-pickv'); v.textContent = sel.value; w.classList.toggle('is-set', !!sel.value); };
    sel.addEventListener('change', function () { state[key] = sel.value; show(); onchange(); });
    show();
    return w;
  }
  function buildFilters() {
    var bar = document.getElementById('smp-bar'), grow = bar.querySelector('.grow');
    var gpicks = document.getElementById('smp-genpicks');
    var noop = function () {};
    gpicks.appendChild(pick(gf, 'part', 'full mix', PARTS, noop));
    gpicks.appendChild(pick(gf, 'key', 'any note', NOTES, noop));
    gpicks.appendChild(pick(gf, 'scale', 'any scale', SCALES, noop));
    var onList = function () { applyFilters(); render(); };
    bar.insertBefore(pick(filt, 'part', 'all instruments', PARTS, onList), grow);
    bar.insertBefore(pick(filt, 'key', 'any note', NOTES, onList), grow);
    bar.insertBefore(pick(filt, 'scale', 'any scale', SCALES, onList), grow);
  }

  function render() {
    stop();
    var pages = Math.max(1, Math.ceil(items.length / PAGE));
    page = Math.min(page, pages - 1);
    var shown = items.slice(page * PAGE, page * PAGE + PAGE);
    listEl.innerHTML = shown.map(row).join('');
    emptyEl.hidden = items.length > 0;
    if (!items.length && all.length) emptyEl.textContent = 'Nothing in the library matches these filters. Press generate for a new one.';
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
    stop(); blobStop();
    var it = byId(r.getAttribute('data-id')), media, el = null;
    if (it.recipe && !it.audio) {
      var btn = r.querySelector('.smp-play'); btn.disabled = true; btn.classList.add('is-busy');
      realize(it).then(function () { btn.disabled = false; btn.classList.remove('is-busy'); if (document.body.contains(r)) play(r); })
        .catch(function () { btn.disabled = false; btn.classList.remove('is-busy'); });
      return;
    }
    media = new Audio(it.audio);
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
  document.getElementById('smp-shuffle').addEventListener('click', function () { shuffle(all); applyFilters(); render(); });
  window.addEventListener('resize', function () {
    Array.prototype.forEach.call(document.querySelectorAll('.smp-wave'), function (cv) {
      var r = cv.closest('.smp-row'), it = byId(r.getAttribute('data-id'));
      drawWave(cv, it.peaks, playing && playing.row === r ? playing.media.currentTime / (playing.media.duration || 1) : 0);
    });
  });
  window.addEventListener('keydown', function (e) { if (e.key === ' ' && playing && e.target === document.body) { e.preventDefault(); play(playing.row); } });

  // The blob: a soft shape that breathes while idle, churns while the engine works and follows the loop's spectrum while it plays.
  var bcv = document.getElementById('smp-blob'), bctx = null;
  var ac = null, an = null, bin = null, src = null, lvl = [], N = 96, bt = 0, accent = '#e8745a', frame = 0, energy = 0;
  for (var li = 0; li < N; li++) lvl.push(0);
  function wakeAudio() {
    var AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return;
    if (!ac) { ac = new AC(); an = ac.createAnalyser(); an.fftSize = 256; an.smoothingTimeConstant = 0.78; an.connect(ac.destination); bin = new Uint8Array(an.frequencyBinCount); }
    if (ac.state === 'suspended') ac.resume();
  }
  function blobStop() { if (src) { try { src.stop(); } catch (e) {} src.disconnect(); src = null; } }
  function blobPlay(g) {
    if (!ac) wakeAudio();
    if (!ac) return;
    stop(); blobStop();
    var n = g.pcm.length / 2, buf = ac.createBuffer(2, n, g.rate), l = buf.getChannelData(0), r = buf.getChannelData(1);
    for (var i = 0; i < n; i++) { l[i] = g.pcm[i * 2]; r[i] = g.pcm[i * 2 + 1]; }
    src = ac.createBufferSource(); src.buffer = buf; src.loop = true; src.connect(an); src.start();
    bcv.setAttribute('aria-pressed', 'true');
  }
  function blobToggle() {
    if (busy) return;
    if (!gens[0]) { generate(); return; }
    if (src) { blobStop(); bcv.setAttribute('aria-pressed', 'false'); } else blobPlay(gens[0]);
  }
  bcv.addEventListener('click', blobToggle);
  bcv.addEventListener('keydown', function (e) { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); blobToggle(); } });
  function blobDraw2d() {
    requestAnimationFrame(blobDraw2d);
    if (document.hidden) return;
    var dpr = window.devicePixelRatio || 1, w = bcv.clientWidth, h = bcv.clientHeight;
    if (!w) return;
    if (bcv.width !== Math.round(w * dpr) || bcv.height !== Math.round(h * dpr)) { bcv.width = Math.round(w * dpr); bcv.height = Math.round(h * dpr); }
    if (frame++ % 60 === 0) accent = getComputedStyle(bcv).getPropertyValue('--accent').trim() || accent;
    bt += busy ? 0.045 : 0.014;
    var playingNow = !!src, i, k;
    if (playingNow) an.getByteFrequencyData(bin);
    var sum = 0;
    for (i = 0; i < N; i++) {
      var m = Math.abs(i / N * 2 - 1), v = playingNow ? bin[Math.floor(Math.pow(m, 1.5) * 70)] / 255 : 0;
      lvl[i] += (v - lvl[i]) * 0.35; sum += lvl[i];
    }
    energy += (sum / N - energy) * 0.2;
    var cx = w / 2, cy = h / 2, R = Math.min(w, h) * 0.27, pulse = busy ? 0.08 * Math.sin(bt * 5) : 0;
    bctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    bctx.clearRect(0, 0, w, h);
    function shape(scale, amp, phase) {
      var pts = [];
      for (k = 0; k < N; k++) {
        var a = k / N * Math.PI * 2 + bt * 0.3 * phase;
        var wob = 0.05 * Math.sin(a * 3 + bt * 1.7 * phase) + 0.035 * Math.sin(a * 5 - bt * 1.1 * phase) + (busy ? 0.05 * Math.sin(a * 7 + bt * 4) : 0);
        var r = R * scale * (1 + wob + pulse + amp * (lvl[k] * 0.9 + energy * 0.6));
        pts.push([cx + Math.cos(a) * r, cy + Math.sin(a) * r]);
      }
      bctx.beginPath();
      for (k = 0; k < N; k++) {
        var p0 = pts[k], p1 = pts[(k + 1) % N], mx = (p0[0] + p1[0]) / 2, my = (p0[1] + p1[1]) / 2;
        if (!k) bctx.moveTo(mx, my); else bctx.quadraticCurveTo(p0[0], p0[1], mx, my);
      }
      bctx.quadraticCurveTo(pts[0][0], pts[0][1], (pts[0][0] + pts[1][0]) / 2, (pts[0][1] + pts[1][1]) / 2);
      bctx.closePath();
    }
    bctx.save(); bctx.globalAlpha = 0.28; bctx.fillStyle = accent; bctx.shadowColor = accent; bctx.shadowBlur = 40;
    shape(1.18, 1.25, -0.7); bctx.fill(); bctx.restore();
    var g = bctx.createRadialGradient(cx - R * 0.3, cy - R * 0.35, R * 0.1, cx, cy, R * 1.5);
    g.addColorStop(0, '#fff'); g.addColorStop(0.35, accent); g.addColorStop(1, accent);
    bctx.save(); bctx.globalAlpha = 0.92; bctx.fillStyle = g; shape(1, 1, 1); bctx.fill(); bctx.restore();
    bctx.save(); bctx.globalAlpha = 0.35; bctx.strokeStyle = '#fff'; bctx.lineWidth = 1.5; shape(0.78, 0.7, 1.4); bctx.stroke(); bctx.restore();
  }
  // A fragment shader draws the blob: a warped, blurred field in the brand's ember and signal, its edge pushed out by sixteen bands of the loop.
  var FS = 'precision highp float; uniform vec2 uRes; uniform float uT, uE, uBusy; uniform float uB[16]; uniform vec3 uA, uS, uG; uniform float uL;\n' +
    'float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n' +
    'float noise(vec2 p){ vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);\n' +
    '  return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y); }\n' +
    'float fbm(vec2 p){ float v = 0.0, a = 0.5; for (int i = 0; i < 4; i++) { v += a * noise(p); p = p * 2.03 + 7.1; a *= 0.5; } return v; }\n' +
    'float band(float m){ float s = 0.0, w = 0.0; for (int i = 0; i < 16; i++) { float d = m * 15.0 - float(i); float k = exp(-d * d * 0.35); s += uB[i] * k; w += k; } return s / w; }\n' +
    'void main(){\n' +
    '  vec2 p = (gl_FragCoord.xy - 0.5 * uRes) / min(uRes.x, uRes.y) * 2.0;\n' +
    '  float t = uT, a = atan(p.y, p.x), r = length(p);\n' +
    '  vec2 q = vec2(fbm(p * 1.3 + t * 0.12), fbm(p * 1.3 - t * 0.1 + 4.0));\n' +
    '  float wob = (fbm(vec2(cos(a), sin(a)) * 1.4 + t * 0.18) - 0.5) * (0.26 + 0.16 * uBusy);\n' +
    '  float R = 0.62 + wob + 0.24 * band(abs(a) / 3.14159) + 0.1 * uE + 0.04 * uBusy * sin(t * 5.0);\n' +
    '  float d = r - R;\n' +
    '  float body = smoothstep(0.2, -0.22, d);\n' +
    '  float glow = exp(-max(d, 0.0) * 3.4) * 0.5 * smoothstep(1.0, 0.55, r) * smoothstep(1.0, 0.6, abs(p.y));\n' +
    '  float f = fbm(p * 1.1 + q * 1.6 + t * 0.07);\n' +
    '  float g = clamp(f * 1.5 + p.y * 0.35 - p.x * 0.2 + 0.25 * uE, 0.0, 1.0);\n' +
    '  vec3 hot = mix(uA, vec3(1.0, 0.82, 0.62), 0.55);\n' +
    '  vec3 c = mix(uS, uA, smoothstep(0.1, 0.55, g)); c = mix(c, hot, smoothstep(0.55, 1.0, g));\n' +
    '  vec3 cl = mix(uS * 0.8, vec3(0.5, 0.32, 0.88), smoothstep(0.1, 0.55, g)); cl = mix(cl, vec3(0.9, 0.5, 0.82), smoothstep(0.55, 1.0, g));\n' +
    '  c = mix(c, cl, uL);\n' +
    '  c = mix(c, vec3(1.0), mix(0.28, 0.1, uL) * smoothstep(0.6, 0.0, length(p - vec2(-0.18, 0.2))) * body);\n' +
    '  float edge = smoothstep(-0.5, 0.2, d) * body; c = mix(c, uS, edge * 0.45);\n' +
    '  float al = clamp(body * 0.94 + glow * (1.0 - body), 0.0, 1.0);\n' +
    '  vec3 col = mix(mix(uS, uA, 0.5), c, body);\n' +
    '  col += (hash(gl_FragCoord.xy + t) - 0.5) / 120.0;\n' +
    '  gl_FragColor = vec4(col * al, al);\n' +
    '}';
  var gl = null, gp = null, gU = {};
  function glInit() {
    gl = bcv.getContext('webgl', { alpha: true, premultipliedAlpha: true, antialias: false });
    if (!gl) return false;
    function sh(type, src) { var o = gl.createShader(type); gl.shaderSource(o, src); gl.compileShader(o); return gl.getShaderParameter(o, gl.COMPILE_STATUS) ? o : null; }
    var v = sh(gl.VERTEX_SHADER, 'attribute vec2 p; void main(){ gl_Position = vec4(p, 0.0, 1.0); }'), f = sh(gl.FRAGMENT_SHADER, FS);
    if (!v || !f) { gl = null; return false; }
    gp = gl.createProgram(); gl.attachShader(gp, v); gl.attachShader(gp, f); gl.linkProgram(gp);
    if (!gl.getProgramParameter(gp, gl.LINK_STATUS)) { gl = null; return false; }
    gl.useProgram(gp);
    var vb = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, vb);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
    var loc = gl.getAttribLocation(gp, 'p'); gl.enableVertexAttribArray(loc); gl.vertexAttribPointer(loc, 2, gl.FLOAT, false, 0, 0);
    ['uRes', 'uT', 'uE', 'uBusy', 'uB', 'uA', 'uS', 'uG', 'uL'].forEach(function (n) { gU[n] = gl.getUniformLocation(gp, n); });
    gl.enable(gl.BLEND); gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
    return true;
  }
  function rgb(css, d) { var m = /^#([0-9a-f]{6})$/i.exec((css || '').trim()); if (!m) return d; var n = parseInt(m[1], 16); return [(n >> 16) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255]; }
  var cA = [1, 0.42, 0.21], cS = [0.65, 0.69, 1], bands = [], busyK = 0, light = 0;
  for (var bi = 0; bi < 16; bi++) bands.push(0);
  function blobDraw() {
    requestAnimationFrame(blobDraw);
    if (document.hidden) return;
    var dpr = Math.min(window.devicePixelRatio || 1, 2), w = bcv.clientWidth, h = bcv.clientHeight;
    if (!w) return;
    if (bcv.width !== Math.round(w * dpr) || bcv.height !== Math.round(h * dpr)) { bcv.width = Math.round(w * dpr); bcv.height = Math.round(h * dpr); }
    if (frame++ % 60 === 0) {
      var cs = getComputedStyle(bcv);
      cA = rgb(cs.getPropertyValue('--accent'), cA); cS = rgb(cs.getPropertyValue('--signal'), cS);
      var ink = rgb(cs.getPropertyValue('--ink'), [1, 1, 1]); light = (ink[0] + ink[1] + ink[2]) / 3 < 0.5 ? 1 : 0;
    }
    bt += 0.016 + 0.03 * busyK;
    busyK += ((busy ? 1 : 0) - busyK) * 0.08;
    var i, j, sum = 0;
    if (src) an.getByteFrequencyData(bin);
    for (i = 0; i < 16; i++) {
      var v = 0;
      if (src) { var lo = Math.floor(Math.pow(i / 16, 1.6) * 70), hi = Math.max(lo + 1, Math.floor(Math.pow((i + 1) / 16, 1.6) * 70)); for (j = lo; j < hi; j++) v += bin[j]; v = v / (hi - lo) / 255; }
      bands[i] += (v - bands[i]) * 0.3; sum += bands[i];
    }
    energy += (sum / 16 - energy) * 0.2;
    gl.viewport(0, 0, bcv.width, bcv.height);
    gl.clearColor(0, 0, 0, 0); gl.clear(gl.COLOR_BUFFER_BIT);
    gl.uniform2f(gU.uRes, bcv.width, bcv.height); gl.uniform1f(gU.uT, bt); gl.uniform1f(gU.uE, energy); gl.uniform1f(gU.uBusy, busyK); gl.uniform1f(gU.uL, light);
    gl.uniform1fv(gU.uB, new Float32Array(bands)); gl.uniform3fv(gU.uA, cA); gl.uniform3fv(gU.uS, cS); gl.uniform3fv(gU.uG, [0.08, 0.1, 0.19]);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  }
  if (glInit()) requestAnimationFrame(blobDraw);
  else { bctx = bcv.getContext('2d'); requestAnimationFrame(blobDraw2d); }

  genBtn.hidden = typeof Worker === 'undefined' || typeof WebAssembly === 'undefined';
  fetch('data.json?v=24').then(function (r) { return r.json(); }).then(function (d) {
    all = d.samples.filter(function (x) { return x.kind === 'A'; });
    all.forEach(function (x) { if (x.part === 'foley') { x.part = 'textures'; x.name = String(x.name).replace(/^foley/, 'textures'); } });
    shuffle(all);
    var qp = new URLSearchParams(location.search);
    ['part', 'scale', 'key'].forEach(function (k) { if (qp.get(k)) filt[k] = qp.get(k); });
    buildFilters(); applyFilters(); var q0 = new URLSearchParams(location.search).get('s'); if (q0) { var ix = items.findIndex(function (x) { return x.id === q0.replace(/[^\w-]/g, ''); }); if (ix > -1) page = Math.floor(ix / PAGE); } render(); var q = new URLSearchParams(location.search).get('s'), qs = q && q.replace(/[^\w-]/g, ''), tr = qs && (listEl.querySelector('.smp-row[data-id="' + qs + '"]')); if (tr) { tr.scrollIntoView({ block: 'center' }); tr.classList.add('is-hit'); } })
    .catch(function () { emptyEl.hidden = false; emptyEl.textContent = 'Could not load the samples.'; });
})();
