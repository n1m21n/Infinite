// Sample studio: the Generate Samples engine as an instrument. Generate a take, then move the dots and the same
// sample shifts (tempo, scale, hats, filters, tails, space), re-rendered by the real engine in a worker.
(function () {
  var $ = function (id) { return document.getElementById(id); };
  var PARTS = [['', 'full mix', 0], ['synths', 'synths', 1], ['keys', 'keys', 9], ['leads', 'leads', 10], ['pads', 'pads', 2], ['plucks', 'plucks', 3],
    ['bass', 'bass', 6], ['textures', 'textures', 11], ['drums', 'drums', 5], ['kicks', 'kicks', 12], ['snares', 'snares', 13], ['hats', 'hats', 14],
    ['toms', 'toms', 15], ['percussion', 'percussion', 4], ['fx', 'fx', 7], ['foley', 'foley', 8]];
  var KEYLESS = { 4: 1, 5: 1, 7: 1, 8: 1, 12: 1, 13: 1, 14: 1, 15: 1 };
  var NOTES = ['c', 'c#', 'd', 'd#', 'e', 'f', 'f#', 'g', 'g#', 'a', 'a#', 'b'];
  var IC_PLAY = '<svg viewBox="0 0 20 20" fill="currentColor" aria-hidden="true"><path d="M7 4.6v10.8a.6.6 0 0 0 .9.5l8.6-5.4a.6.6 0 0 0 0-1L7.9 4.1a.6.6 0 0 0-.9.5Z"/></svg>';
  var IC_PAUSE = '<svg viewBox="0 0 20 20" fill="currentColor" aria-hidden="true"><rect x="5" y="4" width="3.6" height="12" rx="1"/><rect x="11.4" y="4" width="3.6" height="12" rx="1"/></svg>';
  function esc(s) { return String(s == null ? '' : s).replace(/[&<>"]/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]; }); }
  function clamp(v, a, b) { return Math.max(a, Math.min(b, v)); }
  function r2(v) { return Math.round(v * 100) / 100; }

  // ---------------------------------------------------------------- state
  var info = null;                  // { styles, scales } from the engine
  var pick = { style: -1, part: 0, key: -1, scale: -1, bars: 2 };
  var dots = { tone: 0, energy: 0, space: 0, mood: 0, variation: 0.5 };
  var edit = { a: 0, b: 1, reverse: false, fade: false };
  var take = null;                  // the current render: { styleIdx, partIdx, seed, pcm, rate, bpm, label, patch, nodes, ... }
  var takes = [], takeN = 0;        // history of renders
  var anchor = null;                // the plain take a dot move is compared against (same seed, dots at zero)

  // ---------------------------------------------------------------- engine
  var worker = null, jobs = {}, jobId = 0;
  function ask(msg) {
    if (!worker) {
      worker = new Worker('../gen/gen-worker.js?v=9');
      worker.onmessage = function (e) { var j = jobs[e.data.id]; delete jobs[e.data.id]; if (j) (e.data.error ? j[1](new Error(e.data.error)) : j[0](e.data)); };
      worker.onerror = function () { Object.keys(jobs).forEach(function (k) { jobs[k][1](new Error('worker')); delete jobs[k]; }); worker = null; };
    }
    return new Promise(function (ok, no) { msg.id = ++jobId; jobs[msg.id] = [ok, no]; worker.postMessage(msg); });
  }
  function ready() { return info ? Promise.resolve(info) : ask({ cmd: 'info' }).then(function (d) { info = d; buildPickers(); return d; }); }

  // One render in flight; while it runs, only the newest wish waits. A dot drag therefore always ends on its last position.
  var inflight = false, queued = null;
  function render(job) {
    if (inflight) { queued = job; return; }
    inflight = true; setBusy(true);
    var t0 = performance.now();
    ask(job).then(function (r) {
      r.job = job; r.ms = performance.now() - t0;
      land(r);
    }).catch(function () {
      status(job.pinned ? 'That shape would not render; kept the last one.' : 'No loop came out. Try generate again.');
    }).then(function () {
      inflight = false;
      if (queued) { var q = queued; queued = null; render(q); } else setBusy(false);
    });
  }
  function jobFor(pinned) {
    var part = pick.part, keyless = KEYLESS[part];
    return { cmd: 'run', style: take && pinned ? take.styleIdx : pick.style >= 0 ? pick.style : Math.floor(Math.random() * info.styles.length),
      part: part, seed: take && pinned ? take.seed : Math.floor(Math.random() * 99999), bars: pick.bars,
      key: keyless ? -1 : pick.key, scale: keyless ? -1 : pick.scale,
      tone: dots.tone, energy: dots.energy, space: dots.space, mood: dots.mood, variation: dots.variation, pinned: !!(take && pinned) };
  }
  function generate() { ready().then(function () { anchor = null; render(jobFor(false)); }); }
  function reshape() { if (take) render(jobFor(true)); }

  // A finished render becomes the take; the seed it landed on is pinned so every later move shifts this same sample.
  function land(r) {
    var j = r.job;
    var seed = j.pinned ? j.seed : j.seed + r.attempt * 7919;
    var t = { styleIdx: j.style, partIdx: j.part, seed: seed, pcm: r.pcm, rate: r.rate, frames: r.frames, bpm: r.bpm, label: r.label,
      patch: r.patch, nodes: parsePatch(r.patch), dots: { tone: j.tone, energy: j.energy, space: j.space, mood: j.mood, variation: j.variation }, ms: r.ms,
      key: j.key, scale: j.scale, bars: j.bars };
    if (!j.pinned) { edit.a = 0; edit.b = 1; anchor = null; }
    var prev = take;
    take = t;
    if (!anchor || anchor.seed !== t.seed || anchor.styleIdx !== t.styleIdx || anchor.partIdx !== t.partIdx) anchor = t;
    t.n = ++takeN; takes.unshift(t); if (takes.length > 16) takes.pop();
    swapAudio(prev);
    paint();
  }

  // ---------------------------------------------------------------- audio
  var ac = null, out = null, an = null, src = null, srcGain = null, startedAt = 0, playing = false, buf = null;
  function wake() {
    var AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return false;
    if (!ac) { ac = new AC(); out = ac.createGain(); an = ac.createAnalyser(); an.fftSize = 512; out.connect(an); an.connect(ac.destination); }
    if (ac.state === 'suspended') ac.resume();
    return true;
  }
  // The edit as the listener hears it and the file carries it: the trimmed span, reversed, with short or long fades.
  function edited(t) {
    var n = t.frames, a = Math.floor(edit.a * n), b = Math.max(a + 64, Math.floor(edit.b * n)), len = b - a, L = new Float32Array(len), R = new Float32Array(len), i;
    for (i = 0; i < len; i++) { var s = edit.reverse ? b - 1 - i : a + i; L[i] = t.pcm[s * 2]; R[i] = t.pcm[s * 2 + 1]; }
    var f = edit.fade ? Math.floor(len * 0.12) : Math.min(len >> 2, Math.floor(t.rate * 0.004));
    for (i = 0; i < f; i++) { var g = i / f; g = g * g; L[i] *= g; R[i] *= g; L[len - 1 - i] *= g; R[len - 1 - i] *= g; }
    return { L: L, R: R, len: len };
  }
  function makeBuffer() {
    var e = edited(take), b = ac.createBuffer(2, e.len, take.rate);
    b.getChannelData(0).set(e.L); b.getChannelData(1).set(e.R);
    return b;
  }
  function phase() { return playing && buf ? ((ac.currentTime - startedAt) % buf.duration) / buf.duration : 0; }
  // A new render or edit takes over at the same place in the bar, with a short crossfade: moving a dot never restarts the loop.
  function startBuffer(at) {
    var old = src, oldGain = srcGain, now = ac.currentTime, X = 0.04;
    buf = makeBuffer();
    src = ac.createBufferSource(); src.buffer = buf; src.loop = true;
    srcGain = ac.createGain(); srcGain.gain.setValueAtTime(old ? 0 : 1, now); if (old) srcGain.gain.linearRampToValueAtTime(1, now + X);
    src.connect(srcGain); srcGain.connect(out);
    var off = (at || 0) * buf.duration;
    src.start(now, off); startedAt = now - off;
    if (old) { oldGain.gain.setValueAtTime(oldGain.gain.value, now); oldGain.gain.linearRampToValueAtTime(0, now + X); old.stop(now + X + 0.01); }
  }
  function swapAudio(prev) {
    if (!wake()) return;
    var keep = prev && playing ? phase() : 0;
    if (playing || !prev) { playing = true; startBuffer(keep); }
    else buf = makeBuffer();
  }
  function togglePlay() {
    if (!take) { generate(); return; }
    if (!wake()) return;
    if (playing) { playing = false; if (src) { src.stop(); src = null; } }
    else { playing = true; src = null; startBuffer(0); }
    paintPlay();
  }
  function reEdit() { if (!take || !ac) return; if (playing) startBuffer(phase()); else buf = makeBuffer(); paintWave(); }

  // ---------------------------------------------------------------- patch
  // The .inf the engine wrote: nodes with their params, and the cables between them.
  function parsePatch(txt) {
    var nodes = {}, edges = [], cur = null;
    String(txt).split('\n').forEach(function (ln) {
      var m;
      if ((m = /^node (\d+) (\S+) (.+)$/.exec(ln))) { cur = nodes[m[1]] = { id: +m[1], cat: m[2], type: m[3], p: {} }; return; }
      if (ln === 'end') { cur = null; return; }
      if (cur && (m = /^  (\w) (\S+) (.*)$/.exec(ln))) { cur.p[m[2]] = m[3]; return; }
      if ((m = /^(aud|note) (\d+) (\d+) (\d+)$/.exec(ln))) edges.push({ kind: m[1], dst: +m[2], src: +m[4] });
    });
    return { nodes: nodes, edges: edges };
  }
  var NICE = { cutoff: 'cutoff', 'a.cutoff': 'cutoff', 'b.cutoff': 'cutoff B', filterCutoff: 'cutoff', release: 'release', 'a.ampRelease': 'release', 'b.ampRelease': 'release B',
    ampRelease: 'release', mix: 'wet', decay: 'decay', 'p.cutoff': 'cutoff' };
  // What the dots moved: every param that differs from the plain take of this seed, largest change first.
  function diff(a, b) {
    var out = [], moved = {};
    if (!a || !b || a === b) return { list: out, moved: moved };
    Object.keys(b.nodes.nodes).forEach(function (id) {
      var nb = b.nodes.nodes[id], na = a.nodes.nodes[id];
      if (!na || na.type !== nb.type) return;
      var vels = 0, velN = 0, steps = 0;
      Object.keys(nb.p).forEach(function (k) {
        var x = parseFloat(na.p[k]), y = parseFloat(nb.p[k]);
        if (!(k in na.p) || isNaN(x) || isNaN(y) || x === y) return;
        moved[id] = 1;
        if (/^vel\d+$/.test(k)) { vels += y / (x || 1); velN++; return; }
        if (/_step\d+$/.test(k)) { steps++; return; }
        if (/^(en|note)\d+$/.test(k)) return;
        out.push({ type: nb.type, k: k, x: x, y: y, w: Math.abs(Math.log((Math.abs(y) + 1e-3) / (Math.abs(x) + 1e-3))) });
      });
      if (velN) out.push({ type: nb.type, k: 'velocity', x: 1, y: vels / velN, w: Math.abs(Math.log(vels / velN)), pct: true });
      if (steps) out.push({ type: nb.type, k: 'pattern', w: 1 + steps / 8, text: steps + ' steps rewritten' });
    });
    out.sort(function (p, q) { return q.w - p.w; });
    return { list: out, moved: moved };
  }
  function fmt(v) { var a = Math.abs(v); return a >= 1000 ? (v / 1000).toFixed(1) + 'k' : a >= 100 ? Math.round(v) : a >= 10 ? v.toFixed(1) : v.toFixed(2); }

  function drawPatch(t, moved) {
    var N = t.nodes.nodes, E = t.nodes.edges, ids = Object.keys(N).map(Number), depth = {}, i, changed = true, guard = 0;
    ids.forEach(function (id) { depth[id] = 0; });
    while (changed && guard++ < 64) { changed = false; E.forEach(function (e) { if (N[e.src] && N[e.dst] && depth[e.dst] < depth[e.src] + 1) { depth[e.dst] = depth[e.src] + 1; changed = true; } }); }
    var cols = {}; ids.forEach(function (id) { (cols[depth[id]] = cols[depth[id]] || []).push(id); });
    var W = 132, H = 36, GX = 46, GY = 12, pos = {}, maxRows = 0, ncol = 0;
    Object.keys(cols).forEach(function (c) { ncol = Math.max(ncol, +c + 1); maxRows = Math.max(maxRows, cols[c].length); });
    var totalH = maxRows * (H + GY) + GY, totalW = ncol * (W + GX) + GX - 20;
    Object.keys(cols).forEach(function (c) {
      var col = cols[c], off = (totalH - col.length * (H + GY)) / 2;
      col.forEach(function (id, r) { pos[id] = [GX / 2 + c * (W + GX), off + GY / 2 + r * (H + GY)]; });
    });
    var CAT = { Synths: 'synths', Notes: 'notes', AudioEffects: 'audioeffects', Utility: 'utility', Modulators: 'modulators' };
    var s = '<svg viewBox="0 0 ' + totalW + ' ' + totalH + '" style="min-width:' + Math.min(totalW, 900) + 'px" role="img" aria-label="The patch that made this sample">';
    E.forEach(function (e) {
      var a = pos[e.src], b = pos[e.dst]; if (!a || !b) return;
      var x1 = a[0] + W, y1 = a[1] + H / 2, x2 = b[0], y2 = b[1] + H / 2, mx = (x1 + x2) / 2;
      s += '<path class="' + e.kind + '" d="M' + x1 + ' ' + y1 + 'C' + mx + ' ' + y1 + ' ' + mx + ' ' + y2 + ' ' + x2 + ' ' + y2 + '"/>';
    });
    ids.forEach(function (id) {
      var n = N[id], p = pos[id], c = 'var(--cat-' + (CAT[n.cat] || 'utility') + ')';
      s += '<g class="nd' + (moved[id] ? ' moved' : '') + '" transform="translate(' + p[0] + ' ' + p[1] + ')"><rect width="' + W + '" height="' + H + '" rx="9" stroke="' + c + '"/>' +
        '<rect width="4" height="' + (H - 14) + '" x="7" y="7" rx="2" fill="' + c + '" stroke="none"/>' +
        '<text x="18" y="16">' + esc(n.type.length > 16 ? n.type.slice(0, 15) + '…' : n.type) + '</text><text class="c" x="18" y="28">' + esc(n.cat.toLowerCase()) + (moved[id] ? ' · moved' : '') + '</text></g>';
    });
    $('sto-graph').innerHTML = s + '</svg>';
    $('sto-nodecount').textContent = ids.length + ' nodes, ' + E.length + ' cables';
  }

  // ---------------------------------------------------------------- paint
  function status(msg) { $('sto-status').textContent = msg; }
  function setBusy(b) {
    document.querySelector('.sto-console').classList.toggle('sto-busy', b);
    var g = $('sto-gen'); g.disabled = b && !take;
    if (b && !take) g.lastChild.textContent = info ? ' Generating…' : ' Loading engine…';
    else g.lastChild.textContent = ' Generate';
  }
  function paintPlay() { $('sto-play').innerHTML = playing ? IC_PAUSE : IC_PLAY; $('sto-play').setAttribute('aria-label', playing ? 'Pause' : 'Play'); }
  function paint() {
    var t = take;
    $('sto-title').textContent = t.label.replace(/ \d+(?=,|$)/, '') + ' · ' + info.styles[t.styleIdx].replace(/_/g, ' ');
    var stats = [Math.round(t.bpm * 10) / 10 + ' bpm', t.bars + (t.bars > 1 ? ' bars' : ' bar')];
    var k = /, (.+)$/.exec(t.label); if (k) stats.unshift(k[1]);
    var d = diff(anchor, t);
    var base = anchor && anchor !== t ? anchor : null;
    var chips = stats.map(function (x) { return '<span class="sto-chip">' + esc(x) + '</span>'; });
    if (base && Math.abs(base.bpm - t.bpm) > 0.05) chips.push('<span class="sto-chip up">tempo ' + (t.bpm > base.bpm ? '+' : '') + (Math.round((t.bpm / base.bpm - 1) * 1000) / 10) + '%</span>');
    if (base) { var bk = /, (.+)$/.exec(base.label); if (bk && k && bk[1] !== k[1]) chips.push('<span class="sto-chip up">' + esc(bk[1].split(' ').slice(1).join(' ')) + ' → ' + esc(k[1].split(' ').slice(1).join(' ')) + '</span>'); }
    chips.push('<span class="sto-chip">' + Math.round(t.ms) + ' ms render</span>');
    $('sto-stats').innerHTML = chips.join('');
    var top = d.list.slice(0, 5);
    $('sto-diff').innerHTML = !base ? '<li><span>The first take of this seed. Move a dot and every part it reaches shows up here.</span></li>' :
      !top.length ? '<li><span>Same params as the first take; the notes or scale may still differ.</span></li>' :
      top.map(function (x) { return '<li>' + esc(x.type) + ' <span>' + esc(NICE[x.k] || x.k) + '</span> ' + (x.text ? x.text : x.pct ? (x.y > 1 ? '+' : '') + Math.round((x.y - 1) * 100) + '%' : fmt(x.x) + ' → ' + fmt(x.y)) + '</li>'; }).join('') +
      (d.list.length > 5 ? '<li><span>and ' + (d.list.length - 5) + ' more on ' + Object.keys(d.moved).length + ' nodes</span></li>' : '');
    drawPatch(t, d.moved);
    $('sto-takes').innerHTML = takes.map(function (x, i) {
      return '<button type="button" class="sto-take' + (x === t ? ' on' : '') + '" data-take="' + i + '">' + (x.n || '') + ' · ' + esc(x.label.replace(/ \d+(?=,|$)/, '').replace(/^all, /, '')) + (x.dots.tone || x.dots.energy || x.dots.space || x.dots.mood ? ' ◦' : '') + '</button>';
    }).join('');
    Array.prototype.forEach.call(document.querySelectorAll('[data-need-take]'), function (b) { b.disabled = false; });
    $('sto-empty').hidden = true;
    paintWave(); paintPlay(); status('');
    var code = [t.styleIdx, t.partIdx, t.seed, t.key, t.scale, t.bars, r2(t.dots.tone), r2(t.dots.energy), r2(t.dots.space), r2(t.dots.mood), r2(t.dots.variation)].join('_');
    history.replaceState(null, '', location.pathname + '?t=' + code);
  }
  var wavePeaks = null, wavePeaksFor = null;
  function paintWave() {
    var cv = $('sto-wavecv'), t = take; if (!t) return;
    var dpr = window.devicePixelRatio || 1, w = cv.clientWidth, h = cv.clientHeight; if (!w) return;
    if (cv.width !== Math.round(w * dpr)) { cv.width = Math.round(w * dpr); cv.height = Math.round(h * dpr); }
    var bins = Math.floor(w / 3);
    if (wavePeaksFor !== t || !wavePeaks || wavePeaks.length !== bins) {
      wavePeaks = []; var n = t.frames, m = 0;
      for (var i = 0; i < bins; i++) { var a = Math.floor(i * n / bins), b = Math.floor((i + 1) * n / bins), pk = 0; for (var j = a; j < b; j += 2) pk = Math.max(pk, Math.abs(t.pcm[j * 2]), Math.abs(t.pcm[j * 2 + 1])); wavePeaks.push(pk); m = Math.max(m, pk); }
      wavePeaks = wavePeaks.map(function (x) { return m ? x / m : 0; }); wavePeaksFor = t;
    }
    var g = cv.getContext('2d'), st = getComputedStyle(cv), ink = st.color, on = st.getPropertyValue('--on').trim() || ink;
    g.setTransform(dpr, 0, 0, dpr, 0, 0); g.clearRect(0, 0, w, h);
    // the bar grid: one faint line per beat
    var beats = t.bars * 4; g.fillStyle = ink; g.globalAlpha = 0.14;
    for (var bt = 1; bt < beats; bt++) g.fillRect(Math.round(bt / beats * w), bt % 4 ? h * 0.42 : 0, 1, bt % 4 ? h * 0.16 : h);
    g.globalAlpha = 1;
    var bw = w / wavePeaks.length;
    for (var k = 0; k < wavePeaks.length; k++) {
      var f = k / wavePeaks.length, inside = f >= edit.a && f <= edit.b, bh = Math.max(2, wavePeaks[k] * (h - 14));
      g.fillStyle = inside ? on : ink; g.globalAlpha = inside ? 0.95 : 0.35;
      g.fillRect(k * bw + 0.5, (h - bh) / 2, Math.max(1, bw - 1), bh);
    }
    g.globalAlpha = 1;
    $('sto-cutl').style.width = (edit.a * 100) + '%';
    $('sto-cutr').style.left = (edit.b * 100) + '%'; $('sto-cutr').style.right = '0';
    $('sto-ha').style.left = (edit.a * 100) + '%'; $('sto-hb').style.left = (edit.b * 100) + '%';
    var len = (edit.b - edit.a) * t.bars * 4;
    $('sto-len').textContent = (Math.round(len * 4) / 4) + ' beats' + (edit.reverse ? ', reversed' : '') + (edit.fade ? ', faded' : '');
  }
  function tick() {
    requestAnimationFrame(tick);
    var ph = $('sto-ph');
    if (!take || !playing || !buf) { ph.hidden = true; return; }
    var f = phase(), x = edit.reverse ? edit.b - f * (edit.b - edit.a) : edit.a + f * (edit.b - edit.a);
    ph.hidden = false; ph.style.left = (x * 100) + '%';
  }
  requestAnimationFrame(tick);

  // ---------------------------------------------------------------- dots
  // Each dot re-renders while it moves (the newest position wins) and once more where it lands.
  var dragT = 0;
  function moved(final) {
    var now = performance.now();
    if (final || now - dragT > 90) { dragT = now; reshape(); }
  }
  function drag(el, onMove, onEnd) {
    el.addEventListener('pointerdown', function (e) {
      if (e.button) return;
      el.setPointerCapture(e.pointerId); el.classList.add('is-drag'); onMove(e);
      var mv = function (ev) { onMove(ev); }, up = function (ev) {
        el.classList.remove('is-drag'); el.removeEventListener('pointermove', mv); el.removeEventListener('pointerup', up); el.removeEventListener('pointercancel', up);
        if (onEnd) onEnd(ev);
      };
      el.addEventListener('pointermove', mv); el.addEventListener('pointerup', up); el.addEventListener('pointercancel', up);
    });
  }
  // the feel field: energy across, mood up
  var field = $('sto-field'), fdot = $('sto-fdot'), trail = $('sto-trail'), trailPts = [];
  function paintField() {
    fdot.style.left = ((dots.energy + 1) / 2 * 100) + '%';
    fdot.style.top = ((1 - (dots.mood + 1) / 2) * 100) + '%';
    $('sto-fval').textContent = 'energy ' + (dots.energy >= 0 ? '+' : '') + dots.energy.toFixed(2) + ' · mood ' + (dots.mood >= 0 ? '+' : '') + dots.mood.toFixed(2);
  }
  function paintTrail() {
    var w = field.clientWidth, h = field.clientHeight;
    trail.setAttribute('viewBox', '0 0 ' + w + ' ' + h);
    trail.innerHTML = trailPts.map(function (p, i) { return '<circle cx="' + (p[0] + 1) / 2 * w + '" cy="' + (1 - (p[1] + 1) / 2) * h + '" r="' + (2 + i * 0.25) + '" fill="currentColor" opacity="' + (0.08 + i / trailPts.length * 0.35) + '"/>'; }).join('');
  }
  drag(field, function (e) {
    var b = field.getBoundingClientRect();
    var x = clamp((e.clientX - b.left) / b.width * 2 - 1, -1, 1), y = clamp(1 - (e.clientY - b.top) / b.height * 2, -1, 1);
    if (Math.abs(x) < 0.05) x = 0; if (Math.abs(y) < 0.05) y = 0;
    dots.energy = r2(x); dots.mood = r2(y); paintField(); moved(false);
  }, function () { trailPts.push([dots.energy, dots.mood]); if (trailPts.length > 12) trailPts.shift(); paintTrail(); moved(true); });
  field.addEventListener('keydown', function (e) {
    var d = { ArrowLeft: [-0.1, 0], ArrowRight: [0.1, 0], ArrowUp: [0, 0.1], ArrowDown: [0, -0.1] }[e.key]; if (!d) return;
    e.preventDefault(); dots.energy = r2(clamp(dots.energy + d[0], -1, 1)); dots.mood = r2(clamp(dots.mood + d[1], -1, 1)); paintField(); moved(true);
  });
  field.addEventListener('dblclick', function () { dots.energy = 0; dots.mood = 0; paintField(); moved(true); });
  // the pill dots
  var PILLS = [
    { k: 'tone', name: 'Tone', note: 'filters · measured', lo: 'dark', hi: 'bright', min: -1 },
    { k: 'space', name: 'Space', note: 'tails, reverb · measured', lo: 'close', hi: 'wide', min: -1 },
    { k: 'variation', name: 'Wildness', note: 'how far voices stray', lo: 'tame', hi: 'wild', min: 0 }
  ];
  function pillHtml(p) {
    return '<div class="sto-pill"><b>' + p.name + '<small>' + p.note + '</small></b>' +
      '<div class="sto-track" id="sto-p-' + p.k + '" role="slider" tabindex="0" aria-label="' + p.name + '" aria-valuemin="' + p.min + '" aria-valuemax="1">' +
      '<span class="ends"><span>' + p.lo + '</span><span>' + p.hi + '</span></span><i></i><span class="sto-dot"></span></div>' +
      '<span class="sto-val" id="sto-v-' + p.k + '"></span></div>';
  }
  $('sto-pills').innerHTML = PILLS.map(pillHtml).join('');
  function paintPill(p) {
    var el = $('sto-p-' + p.k), v = dots[p.k], f = (v - p.min) / (1 - p.min), zero = (0 - p.min) / (1 - p.min);
    var inner = 'calc(17px + (100% - 34px) * ';
    el.querySelector('.sto-dot').style.left = inner + f + ')';
    var bar = el.querySelector('i'); bar.style.left = inner + Math.min(f, zero) + ')'; bar.style.width = 'calc((100% - 34px) * ' + Math.abs(f - zero) + ')';
    el.setAttribute('aria-valuenow', v);
    $('sto-v-' + p.k).textContent = p.min < 0 ? (v > 0 ? '+' : '') + v.toFixed(2) : Math.round(v * 100) + '%';
  }
  PILLS.forEach(function (p) {
    var el = $('sto-p-' + p.k);
    drag(el, function (e) {
      var b = el.getBoundingClientRect(), f = clamp((e.clientX - b.left - 17) / (b.width - 34), 0, 1), v = p.min + f * (1 - p.min);
      if (p.min < 0 && Math.abs(v) < 0.06) v = 0;
      dots[p.k] = r2(v); paintPill(p); moved(false);
    }, function () { moved(true); });
    el.addEventListener('keydown', function (e) {
      var d = { ArrowLeft: -0.1, ArrowDown: -0.1, ArrowRight: 0.1, ArrowUp: 0.1 }[e.key]; if (d == null) return;
      e.preventDefault(); dots[p.k] = r2(clamp(dots[p.k] + d, p.min, 1)); paintPill(p); moved(true);
    });
    el.addEventListener('dblclick', function () { dots[p.k] = p.k === 'variation' ? 0.5 : 0; paintPill(p); moved(true); });
    paintPill(p);
  });
  paintField();
  $('sto-reset').addEventListener('click', function () {
    dots.tone = dots.energy = dots.space = dots.mood = 0; dots.variation = 0.5;
    PILLS.forEach(paintPill); paintField(); moved(true);
  });

  // ---------------------------------------------------------------- pickers
  function sel(id, label, opts, val, onchange) {
    var w = $(id);
    w.innerHTML = '<small>' + label + '</small><span></span><select aria-label="' + label + '">' + opts.map(function (o) { return '<option value="' + o[0] + '"' + (String(o[0]) === String(val) ? ' selected' : '') + '>' + esc(o[1]) + '</option>'; }).join('') + '</select>';
    var s = w.querySelector('select'), show = function () { w.querySelector('span').textContent = s.options[s.selectedIndex].text; };
    s.addEventListener('change', function () { show(); onchange(s.value); }); show();
  }
  function buildPickers() {
    sel('sto-part', 'make', PARTS.map(function (p) { return [p[2], p[1]]; }), pick.part, function (v) { pick.part = +v; reshape(); });
    sel('sto-style', 'genre', [[-1, 'any']].concat(info.styles.map(function (s, i) { return [i, s.replace(/_/g, ' ')]; })), pick.style, function (v) { pick.style = +v; if (take && pick.style >= 0 && pick.style !== take.styleIdx) generate(); });
    sel('sto-key', 'key', [[-1, 'any']].concat(NOTES.map(function (n, i) { return [i, n]; })), pick.key, function (v) { pick.key = +v; reshape(); });
    sel('sto-scale', 'scale', [[-1, 'any']].concat(info.scales.map(function (s, i) { return [i, s.toLowerCase()]; })), pick.scale, function (v) { pick.scale = +v; reshape(); });
    sel('sto-bars', 'length', [[1, '1 bar'], [2, '2 bars'], [4, '4 bars'], [8, '8 bars']], pick.bars, function (v) { pick.bars = +v; edit.a = 0; edit.b = 1; reshape(); });
  }

  // ---------------------------------------------------------------- edit + export
  var wave = $('sto-wave');
  function waveF(e) { var b = wave.getBoundingClientRect(); return clamp((e.clientX - b.left) / b.width, 0, 1); }
  function snap(f) { if (!take) return f; var steps = take.bars * 16; return Math.round(f * steps) / steps; }
  drag($('sto-ha'), function (e) { edit.a = Math.min(snap(waveF(e)), edit.b - 1 / (take.bars * 16)); paintWave(); }, reEdit);
  drag($('sto-hb'), function (e) { edit.b = Math.max(snap(waveF(e)), edit.a + 1 / (take.bars * 16)); paintWave(); }, reEdit);
  wave.addEventListener('click', function (e) {
    if (e.target.closest('.sto-h') || !take || !playing) return;
    var f = waveF(e); if (f < edit.a || f > edit.b) return;
    var at = (f - edit.a) / (edit.b - edit.a); startBuffer(edit.reverse ? 1 - at : at);
  });
  function tog(id, key) {
    $(id).addEventListener('click', function () { edit[key] = !edit[key]; $(id).setAttribute('aria-pressed', edit[key]); reEdit(); });
  }
  tog('sto-rev', 'reverse'); tog('sto-fade', 'fade');
  $('sto-trim0').addEventListener('click', function () { edit.a = 0; edit.b = 1; reEdit(); });

  function wavOf(L, R, rate, name, comment) {
    var tags = [['INAM', name], ['IART', 'Infinite (Naman Soni, n1m21n)'], ['ICOP', 'Free for commercial use if credited: Sample from Infinite by Naman Soni (n1m21n), infinitedevices.org'], ['ICMT', comment], ['ISFT', 'Infinite']]
      .map(function (t) { var b = new TextEncoder().encode(t[1] + '\0'); return [t[0], b, b.length + (b.length & 1)]; });
    var infoLen = 4 + tags.reduce(function (a, t) { return a + 8 + t[2]; }, 0);
    var n = L.length, v = new DataView(new ArrayBuffer(44 + n * 4 + 8 + infoLen)), o = 44;
    function str(at, t) { for (var i = 0; i < t.length; i++) v.setUint8(at + i, t.charCodeAt(i)); }
    str(0, 'RIFF'); v.setUint32(4, 36 + n * 4 + 8 + infoLen, true); str(8, 'WAVEfmt '); v.setUint32(16, 16, true); v.setUint16(20, 1, true);
    v.setUint16(22, 2, true); v.setUint32(24, rate, true); v.setUint32(28, rate * 4, true); v.setUint16(32, 4, true); v.setUint16(34, 16, true);
    str(36, 'data'); v.setUint32(40, n * 4, true);
    for (var i = 0; i < n; i++) { var a = clamp(L[i], -1, 1), b = clamp(R[i], -1, 1); v.setInt16(o, a < 0 ? a * 32768 : a * 32767, true); v.setInt16(o + 2, b < 0 ? b * 32768 : b * 32767, true); o += 4; }
    str(o, 'LIST'); v.setUint32(o + 4, infoLen, true); str(o + 8, 'INFO'); o += 12;
    tags.forEach(function (t) { str(o, t[0]); v.setUint32(o + 4, t[1].length, true); new Uint8Array(v.buffer).set(t[1], o + 8); o += 8 + t[2]; });
    return new Blob([v], { type: 'audio/wav' });
  }
  function saveAs(blob, name) { var u = URL.createObjectURL(blob), a = document.createElement('a'); a.href = u; a.download = name; document.body.appendChild(a); a.click(); a.remove(); setTimeout(function () { URL.revokeObjectURL(u); }, 4000); }
  function fileBase() { return 'infinite-' + take.label.replace(/#/g, 's').replace(/\W+/g, '-') + '-' + Math.round(take.bpm) + 'bpm'; }
  function dotsLine() { var d = take.dots; return 'tone ' + d.tone + ', energy ' + d.energy + ', space ' + d.space + ', mood ' + d.mood + ', wildness ' + d.variation; }
  $('sto-wav').addEventListener('click', function () {
    var e = edited(take);
    saveAs(wavOf(e.L, e.R, take.rate, take.label, take.label + ', ' + Math.round(take.bpm) + ' bpm, ' + dotsLine() + '. Generated by Infinite, https://infinitedevices.org/samples/'), fileBase() + '.wav');
  });
  $('sto-inf').addEventListener('click', function () {
    var txt = take.patch.replace('\n# Made with', '\n# Studio dots: ' + dotsLine() + '\n# Made with');
    saveAs(new Blob([txt], { type: 'text/plain' }), fileBase() + '.inf');
  });
  $('sto-share').addEventListener('click', function () {
    var link = location.href, b = $('sto-share');
    var done = function () { b.querySelector('small').textContent = 'copied'; setTimeout(function () { b.querySelector('small').textContent = 'link'; }, 1600); };
    if (navigator.share) { navigator.share({ title: 'Infinite sample', url: link }).catch(function () {}); return; }
    if (navigator.clipboard) navigator.clipboard.writeText(link).then(done, function () { window.prompt('Copy this link', link); });
    else window.prompt('Copy this link', link);
  });

  $('sto-gen').addEventListener('click', function () { wake(); generate(); });
  $('sto-play').addEventListener('click', togglePlay);
  $('sto-takes').addEventListener('click', function (e) {
    var b = e.target.closest('[data-take]'); if (!b) return;
    var t = takes[+b.getAttribute('data-take')]; if (!t || t === take) return;
    var prev = take; take = t;
    ['tone', 'energy', 'space', 'mood', 'variation'].forEach(function (k) { dots[k] = t.dots[k]; });
    PILLS.forEach(paintPill); paintField();
    if (!anchor || anchor.seed !== t.seed || anchor.styleIdx !== t.styleIdx || anchor.partIdx !== t.partIdx) anchor = t;
    swapAudio(prev); paint();
  });
  window.addEventListener('keydown', function (e) {
    if (e.target.closest && e.target.closest('input, select, textarea, [role="slider"], #sto-field')) return;
    if (e.key === ' ') { e.preventDefault(); togglePlay(); }
    else if (e.key === 'g' || e.key === 'G') generate();
  });
  window.addEventListener('resize', function () { paintWave(); paintTrail(); });

  // A shared take: ?t=style_part_seed_key_scale_bars_tone_energy_space_mood_wild renders exactly that sample.
  (function () {
    var q = new URLSearchParams(location.search).get('t'), m = q && /^(\d+)_(\d+)_(\d+)_(-?\d+)_(-?\d+)_(\d)_(-?[\d.]+)_(-?[\d.]+)_(-?[\d.]+)_(-?[\d.]+)_([\d.]+)$/.exec(q);
    if (!m) return;
    pick.part = +m[2]; pick.key = +m[4]; pick.scale = +m[5]; pick.bars = +m[6];
    dots.tone = +m[7]; dots.energy = +m[8]; dots.space = +m[9]; dots.mood = +m[10]; dots.variation = +m[11];
    PILLS.forEach(paintPill); paintField();
    ready().then(function () {
      take = { styleIdx: +m[1], partIdx: +m[2], seed: +m[3] };
      render(Object.assign(jobFor(true), { style: +m[1], seed: +m[3] }));
      take = null;
    });
  })();
  if (typeof Worker === 'undefined' || typeof WebAssembly === 'undefined') { $('sto-gen').disabled = true; status('This browser cannot run the engine.'); }
  else ready().catch(function () { status('The engine did not load. Reload the page.'); });
})();
