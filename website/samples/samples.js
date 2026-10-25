/* Samples page: swipe view + list view over data.json. Order is shuffled on every load; ratings live in localStorage and export as JSON. */
(function () {
  'use strict';
  var KEY = 'infinite.samples.ratings.v1';
  var $ = function (s, r) { return (r || document).querySelector(s); };
  var all = [], items = [], cur = 0, view = 'swipe', filter = 'all', ratings = {}, audio = new Audio(), playing = null;
  try { ratings = JSON.parse(localStorage.getItem(KEY) || '{}'); } catch (e) { ratings = {}; }
  function save() { try { localStorage.setItem(KEY, JSON.stringify(ratings)); } catch (e) {} }
  function shuffle(a) { for (var i = a.length - 1; i > 0; i--) { var j = Math.floor(Math.random() * (i + 1)); var t = a[i]; a[i] = a[j]; a[j] = t; } return a; }
  function el(tag, cls, html) { var e = document.createElement(tag); if (cls) e.className = cls; if (html != null) e.innerHTML = html; return e; }
  var KIND = { A: 'Audio', V: 'Visual', AV: 'Audio + visual' };

  function wave(canvas, peaks, progress) {
    var dpr = window.devicePixelRatio || 1, w = canvas.clientWidth, h = canvas.clientHeight;
    if (!w || !h) return;
    canvas.width = w * dpr; canvas.height = h * dpr;
    var c = canvas.getContext('2d'); c.scale(dpr, dpr); c.clearRect(0, 0, w, h);
    var ink = getComputedStyle(document.body).getPropertyValue('--ink').trim() || '#12152A';
    var n = peaks.length, bw = w / n;
    for (var i = 0; i < n; i++) {
      var amp = Math.max(0.03, peaks[i]) * h * 0.9;
      c.globalAlpha = (i / n) <= progress ? 1 : 0.28; c.fillStyle = ink;
      c.fillRect(i * bw + 0.5, (h - amp) / 2, Math.max(1, bw - 1.5), amp);
    }
    c.globalAlpha = 1;
  }

  function stop() { audio.pause(); playing = null; refreshPlay(); }
  function toggle(s, videoEl) {
    if (playing === s.id) { videoEl ? videoEl.pause() : audio.pause(); playing = null; return refreshPlay(); }
    stop();
    if (videoEl) { videoEl.currentTime = 0; videoEl.play(); } else if (s.audio) { audio.src = s.audio; audio.currentTime = 0; audio.play(); }
    playing = s.id; refreshPlay();
  }
  audio.addEventListener('ended', function () { playing = null; refreshPlay(); });
  audio.addEventListener('timeupdate', function () {
    var p = audio.duration ? audio.currentTime / audio.duration : 0;
    document.querySelectorAll('canvas[data-id="' + playing + '"]').forEach(function (cv) { var s = byId(playing); if (s) wave(cv, s.peaks, p); });
  });
  function byId(id) { return all.find(function (s) { return s.id === id; }); }
  function refreshPlay() {
    document.querySelectorAll('.smp-play').forEach(function (b) { b.textContent = b.dataset.id === playing ? '❚❚' : '▶'; b.setAttribute('aria-label', b.dataset.id === playing ? 'Pause' : 'Play'); });
  }

  function stars(s) {
    var box = el('div', 'smp-stars'); box.setAttribute('role', 'group'); box.setAttribute('aria-label', 'Rate ' + s.name);
    for (var i = 1; i <= 5; i++) (function (v) {
      var b = el('button', 'smp-star', '★'); b.type = 'button'; b.setAttribute('aria-label', v + ' of 5');
      b.setAttribute('aria-pressed', ratings[s.id] >= v ? 'true' : 'false');
      b.onclick = function () { ratings[s.id] = ratings[s.id] === v ? 0 : v; save(); updateCount(); box.querySelectorAll('.smp-star').forEach(function (x, k) { x.setAttribute('aria-pressed', ratings[s.id] >= k + 1 ? 'true' : 'false'); }); };
      box.appendChild(b);
    })(i);
    return box;
  }

  function card(s) {
    var c = el('article', 'smp-card'), m = el('div', 'smp-media'), video = null;
    if (s.video) {
      video = el('video'); video.src = s.video; video.poster = s.poster || ''; video.loop = true; video.muted = s.kind === 'V'; video.playsInline = true; video.preload = 'metadata';
      m.appendChild(video);
    } else {
      var cv = el('canvas', 'wave-big'); cv.dataset.id = s.id; m.appendChild(cv); requestAnimationFrame(function () { wave(cv, s.peaks, 0); });
    }
    var pb = el('button', 'smp-play', '▶'); pb.type = 'button'; pb.dataset.id = s.id; pb.onclick = function () { toggle(s, video); };
    m.appendChild(pb); c.appendChild(m);
    var b = el('div', 'smp-body');
    b.appendChild(el('h2', 'smp-title', s.name));
    b.appendChild(el('div', 'smp-meta', KIND[s.kind] + ' · ' + s.nodes + ' nodes' + (s.family ? ' · variation' : '')));
    var tg = el('div', 'smp-tags'); (s.tags || []).slice(0, 8).forEach(function (t) { tg.appendChild(el('span', 'smp-tag', t)); }); b.appendChild(tg);
    var r = el('div', 'smp-row'); r.appendChild(stars(s));
    var spacer = el('span'); spacer.style.flex = '1'; r.appendChild(spacer);
    r.appendChild(dl(s)); b.appendChild(r); c.appendChild(b);
    return c;
  }
  function dl(s) {
    var a = el('a', 'smp-dl', 'Download'); a.href = s.video || s.audio; a.download = s.name.replace(/\s+/g, '-').toLowerCase() + (s.video ? '.mp4' : '.m4a'); return a;
  }

  function renderSwipe() {
    var st = $('#smp-stage'); st.innerHTML = '';
    if (!items.length) { st.appendChild(el('p', 'smp-meta', 'No samples match.')); return; }
    stop(); st.appendChild(card(items[cur]));
    $('#smp-pos').textContent = (cur + 1) + ' / ' + items.length;
  }
  function go(d) {
    if (!items.length) return;
    var c = $('.smp-card'); if (c) c.classList.add(d > 0 ? 'out-l' : 'out-r');
    setTimeout(function () { cur = (cur + d + items.length) % items.length; renderSwipe(); }, 200);
  }

  function renderList() {
    var box = $('#smp-list'); box.innerHTML = '';
    items.forEach(function (s, i) {
      var li = el('div', 'smp-li'); li.dataset.id = s.id;
      var pb = el('button', 'smp-play', '▶'); pb.type = 'button'; pb.dataset.id = s.id;
      pb.onclick = function () { if (s.video) { cur = i; setView('swipe'); } else toggle(s); };
      li.appendChild(pb);
      var nm = el('div'); nm.appendChild(el('div', 'name', s.name)); nm.appendChild(el('div', 'sub', KIND[s.kind] + ' · ' + (s.tags || []).slice(0, 3).join(' · '))); li.appendChild(nm);
      var cv = el('canvas'); cv.dataset.id = s.id; li.appendChild(cv);
      li.appendChild(stars(s)); li.appendChild(dl(s)); box.appendChild(li);
      requestAnimationFrame(function () { wave(cv, s.peaks, 0); });
    });
    refreshPlay();
  }

  function updateCount() { var n = Object.keys(ratings).filter(function (k) { return ratings[k] > 0; }).length; $('#smp-rated').textContent = n + ' rated'; }
  function apply() {
    items = all.filter(function (s) { return filter === 'all' || s.kind === filter; }); cur = 0;
    $('#smp-total').textContent = items.length + ' samples'; render();
  }
  function render() { view === 'swipe' ? renderSwipe() : renderList(); }
  function setView(v) {
    view = v; $('#smp-swipe').hidden = v !== 'swipe'; $('#smp-list').hidden = v !== 'list';
    document.querySelectorAll('[data-view]').forEach(function (b) { b.setAttribute('aria-pressed', b.dataset.view === v ? 'true' : 'false'); });
    render();
  }

  function exportRatings() {
    var out = all.filter(function (s) { return ratings[s.id] > 0; }).map(function (s) { return { id: s.id, name: s.name, rating: ratings[s.id], genome: s.genome }; });
    var a = el('a'); a.href = URL.createObjectURL(new Blob([JSON.stringify(out, null, 1)], { type: 'application/json' })); a.download = 'infinite-sample-ratings.json'; a.click();
  }

  fetch('data.json').then(function (r) { return r.json(); }).then(function (d) {
    all = shuffle(d.samples); items = all.slice();
    document.querySelectorAll('[data-view]').forEach(function (b) { b.onclick = function () { setView(b.dataset.view); }; });
    document.querySelectorAll('[data-filter]').forEach(function (b) { b.onclick = function () {
      filter = b.dataset.filter; document.querySelectorAll('[data-filter]').forEach(function (x) { x.setAttribute('aria-pressed', x === b ? 'true' : 'false'); }); apply(); }; });
    $('#smp-shuffle').onclick = function () { shuffle(all); apply(); };
    $('#smp-prev').onclick = function () { go(-1); }; $('#smp-next').onclick = function () { go(1); };
    $('#smp-export').onclick = exportRatings;
    document.addEventListener('keydown', function (e) {
      if (view !== 'swipe' || /input|textarea/i.test(e.target.tagName)) return;
      if (e.key === 'ArrowRight') go(1); else if (e.key === 'ArrowLeft') go(-1);
      else if (e.key === ' ') { e.preventDefault(); var b = $('.smp-card .smp-play'); b && b.click(); }
      else if (/^[1-5]$/.test(e.key) && items[cur]) { ratings[items[cur].id] = +e.key; save(); updateCount(); renderSwipe(); }
    });
    var sx = null, st = $('#smp-stage');
    st.addEventListener('pointerdown', function (e) { if (!e.target.closest('button,a')) sx = e.clientX; });
    st.addEventListener('pointerup', function (e) { if (sx == null) return; var dx = e.clientX - sx; sx = null; if (Math.abs(dx) > 60) go(dx < 0 ? 1 : -1); });
    updateCount(); apply(); setView('swipe');
  }).catch(function () { $('#smp-stage').textContent = 'Could not load the samples.'; });
  addEventListener('resize', function () { if (view === 'list') renderList(); else { var cv = $('canvas.wave-big'); cv && wave(cv, byId(cv.dataset.id).peaks, 0); } });
})();
