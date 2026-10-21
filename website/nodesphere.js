/* The node ring on nodes/: a swipeable carousel of node screenshots. Swipe or drag to turn it, tap the front card (or the button under it) to open that node's page. */
(function () {
  var root = document.getElementById('ns'), stage = document.getElementById('ns-stage');
  if (!root || !stage) return;
  document.documentElement.classList.add('js');
  var all = Array.prototype.slice.call(stage.querySelectorAll('.ns-a')), act = all.slice(), m = act.length;
  var reduce = window.matchMedia && matchMedia('(prefers-reduced-motion: reduce)').matches;
  var pos = 0, target = 0, cw = 260, ch = 380, STEP = 0.5, PX = 140, raf = 0, last = 0, shown = -1;
  var drag = null, suppress = false, wheelAcc = 0;

  var info = document.createElement('div'); info.className = 'ns-info'; info.setAttribute('aria-live', 'polite');
  info.innerHTML = '<span class="ns-fam mono"></span><span class="ns-name"></span><a class="ns-open np-chip" href="#">Open node page</a><input class="ns-range" type="range" min="0" value="0" step="1" aria-label="Browse nodes" autocomplete="off"><span class="ns-count mono"></span>';
  stage.parentNode.insertBefore(info, stage.nextSibling);
  var fam = info.querySelector('.ns-fam'), nm = info.querySelector('.ns-name'), open = info.querySelector('.ns-open'),
      rng = info.querySelector('.ns-range'), cnt = info.querySelector('.ns-count');

  function wrapd(d) { return m > 1 ? d - m * Math.round(d / m) : d; }
  function size() {
    var w = stage.clientWidth, narrow = w < 560;
    cw = narrow ? Math.min(Math.round(w * 0.5), 210) : 270; ch = Math.round(cw * (narrow ? 1.45 : 1.4));
    STEP = narrow ? 0.56 : 0.46;
    stage.style.setProperty('--cw', cw + 'px'); stage.style.setProperty('--ch', ch + 'px');
    stage.style.height = (ch + 40) + 'px';
    PX = (cw / (2 * Math.tan(STEP / 2))) * Math.sin(STEP);
  }
  function load(a) {
    if (a.getAttribute('data-ld')) return;
    a.setAttribute('data-ld', '1');
    var s = a.getAttribute('data-img');
    if (!s) return;
    var im = new Image(); im.alt = ''; im.decoding = 'async'; im.draggable = false; im.src = s;
    a.textContent = ''; a.appendChild(im);
  }
  function render() {
    var R = cw / (2 * Math.tan(STEP / 2)), cur = ((Math.round(pos) % m) + m) % m, i;
    for (i = 0; i < m; i++) {
      var a = act[i], d = wrapd(i - pos), ad = Math.abs(d);
      if (ad > 3.5) { if (a._v !== 0) { a.style.visibility = 'hidden'; a._v = 0; } continue; }
      if (a._v !== 1) { a.style.visibility = 'visible'; a._v = 1; }
      if (ad < 4.5) load(a);
      var th = d * STEP, o = ad < 0.6 ? 1 : Math.max(0, 1 - (ad - 0.6) * 0.36);
      a.style.transform = 'translate3d(' + (R * Math.sin(th)).toFixed(1) + 'px,0,' + (R * (Math.cos(th) - 1)).toFixed(1) + 'px) rotateY(' + (-th * 57.2958).toFixed(2) + 'deg)';
      a.style.opacity = o.toFixed(3); a.style.zIndex = 1000 - Math.round(ad * 20);
    }
    if (cur !== shown) {
      shown = cur;
      var f = act[cur];
      fam.textContent = f.getAttribute('data-fl'); nm.textContent = f.getAttribute('data-n');
      open.href = f.getAttribute('href'); open.setAttribute('aria-label', 'Open the ' + f.getAttribute('data-n') + ' node page');
      cnt.textContent = (cur + 1) + ' / ' + m; rng.value = cur;
      for (i = 0; i < m; i++) act[i].tabIndex = i === cur ? 0 : -1;
    }
  }
  function loop(t) {
    raf = 0;
    var dt = last ? Math.min(t - last, 50) : 16; last = t;
    if (!drag) {
      var diff = target - pos;
      if (reduce || Math.abs(diff) < 0.002) { pos = target; last = 0; }
      else { pos += diff * (1 - Math.exp(-dt / 120)); }
    }
    render();
    if (drag || pos !== target) raf = requestAnimationFrame(loop); else last = 0;
  }
  function go() { if (!raf) raf = requestAnimationFrame(loop); }
  function to(t) {
    if (Math.abs(t - pos) > 5) pos = t - (t > pos ? 5 : -5);
    target = t; go();
  }

  stage.addEventListener('pointerdown', function (e) {
    if (e.pointerType === 'mouse' && e.button !== 0) return;
    drag = { x: e.clientX, y: e.clientY, p: pos, lx: e.clientX, lt: e.timeStamp, v: 0, moved: 0 };
    stage.classList.add('is-drag'); suppress = false;
  });
  window.addEventListener('pointermove', function (e) {
    if (!drag) return;
    var dx = e.clientX - drag.x; drag.moved = Math.max(drag.moved, Math.abs(dx));
    if (drag.moved > 6) suppress = true;
    pos = drag.p - dx / PX; target = pos;
    var dt = e.timeStamp - drag.lt;
    if (dt > 0) { drag.v = 0.8 * drag.v + 0.2 * (-(e.clientX - drag.lx) / PX / dt); drag.lx = e.clientX; drag.lt = e.timeStamp; }
    go();
  });
  function release(e) {
    if (!drag) return;
    var v = drag.v, ended = e && e.timeStamp - drag.lt;
    if (ended > 90) v = 0;
    drag = null; stage.classList.remove('is-drag');
    target = Math.round(pos + Math.max(-12, Math.min(12, v * 260)));
    last = 0; go();
  }
  window.addEventListener('pointerup', release); window.addEventListener('pointercancel', release);
  stage.addEventListener('click', function (e) {
    if (suppress) { e.preventDefault(); suppress = false; return; }
    var a = e.target.closest && e.target.closest('.ns-a');
    if (!a) return;
    var d = wrapd(act.indexOf(a) - pos);
    if (Math.abs(d) > 0.5) { e.preventDefault(); to(Math.round(pos + d)); }
  });
  stage.addEventListener('dragstart', function (e) { e.preventDefault(); });
  stage.addEventListener('wheel', function (e) {
    if (Math.abs(e.deltaX) <= Math.abs(e.deltaY)) return;
    e.preventDefault(); wheelAcc += e.deltaX;
    if (Math.abs(wheelAcc) > 60) { to(Math.round(target) + (wheelAcc > 0 ? 1 : -1)); wheelAcc = 0; }
  }, { passive: false });
  stage.addEventListener('keydown', function (e) {
    var d = { ArrowLeft: -1, ArrowRight: 1 }[e.key];
    if (d) { to(Math.round(target) + d); e.preventDefault(); }
  });
  stage.setAttribute('role', 'group'); stage.setAttribute('aria-label', 'Node screenshots: swipe, drag or use the arrow keys, then open the front one');
  rng.addEventListener('input', function () {
    var v = +rng.value; to(v + m * Math.round((pos - v) / m));
  });
  window.addEventListener('resize', function () { size(); render(); });

  var sel = root.querySelector('.ns-select'), menu = root.querySelector('.ns-menu'), view = root.querySelector('.ns-view'),
      opts = root.querySelectorAll('.ns-opt'), val = root.querySelector('.ns-select-val');
  function refilter(f) {
    act = all.filter(function (a) {
      var ok = !f || a.getAttribute('data-fam') === f;
      a.style.display = ok ? '' : 'none'; a._v = -1; return ok;
    });
    m = act.length; rng.max = Math.max(0, m - 1); rng.disabled = m < 2;
    pos = target = Math.floor(Math.random() * m); shown = -1; render();
  }
  function menuOpen(o) { menu.hidden = !o; sel.setAttribute('aria-expanded', String(o)); }
  sel.addEventListener('click', function () { menuOpen(menu.hidden); });
  document.addEventListener('click', function (e) { if (!e.target.closest('.ns-sel')) menuOpen(false); });
  document.addEventListener('keydown', function (e) { if (e.key === 'Escape') menuOpen(false); });
  Array.prototype.forEach.call(opts, function (c) {
    c.addEventListener('click', function () {
      Array.prototype.forEach.call(opts, function (o) { o.setAttribute('aria-selected', o === c ? 'true' : 'false'); });
      val.textContent = c.firstChild.textContent; menuOpen(false);
      root.removeAttribute('data-view'); view.setAttribute('aria-pressed', 'false');
      refilter(c.getAttribute('data-fam') || '');
    });
  });
  view.addEventListener('click', function () {
    var list = root.getAttribute('data-view') !== 'list';
    if (list) root.setAttribute('data-view', 'list'); else root.removeAttribute('data-view');
    view.setAttribute('aria-pressed', list ? 'true' : 'false');
    view.setAttribute('aria-label', list ? 'Show as a ring' : 'Show as a list'); view.title = list ? 'Ring' : 'List';
    if (!list) { size(); render(); }
  });
  rng.max = m - 1;
  pos = target = Math.floor(Math.random() * m);   // a different node in front on every visit
  size(); render();
})();
