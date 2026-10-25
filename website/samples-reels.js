/* Samples as a reel: the same 9:16 stage as the embeds page, one clip per slide, the slide on screen plays. */
(function () {
  var reels = document.getElementById('reels'), soundOn = false, isLock = false, lockTimer = null, io = null;
  var IC_DL = '<svg class="ic" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M10 3.5v9.5m0 0-3.5-3.5M10 13l3.5-3.5M4 16.5h12"/></svg>';
  var IC_PATCH = '<svg class="ic" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="3.5" y="3.5" width="9" height="9" rx="2"/><rect x="7.5" y="7.5" width="9" height="9" rx="2"/></svg>';
  var IC_MUTE = '<svg class="ic" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M3.5 8v4h3l4 3.5v-11L6.5 8h-3Z"/><path d="m13.5 8 3.5 4M17 8l-3.5 4"/></svg>';
  var IC_SOUND = '<svg class="ic" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M3.5 8v4h3l4 3.5v-11L6.5 8h-3Z"/><path d="M13.5 7.5a3.5 3.5 0 0 1 0 5M15.6 5.4a6.5 6.5 0 0 1 0 9.2"/></svg>';

  function esc(s) { return String(s == null ? '' : s).replace(/[&<>"]/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]; }); }
  function shuffle(a) { for (var i = a.length - 1; i > 0; i--) { var j = Math.floor(Math.random() * (i + 1)), t = a[i]; a[i] = a[j]; a[j] = t; } }

  function slide(it) {
    return '<section class="reel-slide" aria-label="' + esc(it.name) + '" data-id="' + esc(it.id) + '" data-kind="' + it.kind + '">' +
      '<div class="reel-wrap"><div class="reel-card">' +
      '<video muted loop playsinline preload="none" poster="' + esc(it.poster || '') + '" data-src="' + esc(it.video) + '" title="' + esc(it.name) + '"></video>' +
      '<div class="reel-swipe" aria-hidden="true"></div>' +
      '<div class="reel-meta"><h2>' + esc(it.name) + '</h2></div></div>' +
      '<div class="reel-rail">' +
        (it.kind === 'AV' ? '<button type="button" class="rail-btn" data-snd aria-label="Sound">' + IC_MUTE + '<span>Sound</span></button>' : '') +
        (it.library ? '<a class="rail-btn" href="../library/' + esc(it.library) + '.html" aria-label="Open the patch in the Library">' + IC_PATCH + '<span>Patch</span></a>' : '') +
        '<a class="rail-btn" href="' + esc(it.download) + '" download="' + esc(it.name.replace(/\W+/g, '_')) + '.mp4" aria-label="Download ' + esc(it.name) + '">' + IC_DL + '<span>Save</span></a>' +
      '</div></div></section>';
  }

  function setSound() {
    Array.prototype.forEach.call(reels.querySelectorAll('[data-snd]'), function (b) { b.innerHTML = (soundOn ? IC_SOUND : IC_MUTE) + '<span>Sound</span>'; });
    Array.prototype.forEach.call(reels.querySelectorAll('.reel-slide[data-kind="AV"] video'), function (v) { v.muted = !soundOn; });
  }

  function activeIndex() {
    var all = [].slice.call(reels.children), mid = reels.scrollTop + reels.clientHeight / 2, best = 0, min = Infinity;
    all.forEach(function (s, i) { var d = Math.abs(mid - (s.offsetTop + s.clientHeight / 2)); if (d < min) { min = d; best = i; } });
    return best;
  }
  function goTo(i) {
    var all = [].slice.call(reels.children);
    if (i < 0 || i >= all.length) return;
    isLock = true; all[i].scrollIntoView({ behavior: 'smooth', block: 'start' });
    clearTimeout(lockTimer); lockTimer = setTimeout(function () { isLock = false; }, 460);
  }

  function mount(items) {
    reels.innerHTML = items.map(slide).join('');
    io = new IntersectionObserver(function (es) {
      es.forEach(function (e) {
        var v = e.target.querySelector('video'), av = e.target.getAttribute('data-kind') === 'AV';
        if (e.isIntersecting && e.intersectionRatio >= 0.6) {
          if (!v.getAttribute('src')) v.src = v.getAttribute('data-src');
          v.muted = !(soundOn && av);
          v.play().catch(function () { v.muted = true; v.play().catch(function () {}); });
        } else {
          v.pause();
          if (!e.isIntersecting && v.getAttribute('src')) { v.removeAttribute('src'); v.load(); }   // only the slide on screen holds a decoder
        }
      });
    }, { root: reels, threshold: [0, 0.6] });
    Array.prototype.forEach.call(reels.querySelectorAll('.reel-slide'), function (s) { io.observe(s); });
    var q = new URLSearchParams(location.search).get('s'), qs = q && q.replace(/[^\w-]/g, ''), hit = qs && reels.querySelector('.reel-slide[data-id="' + qs + '"]');
    if (hit) { reels.style.scrollBehavior = 'auto'; hit.scrollIntoView({ block: 'start' }); reels.style.scrollBehavior = ''; }
  }

  reels.addEventListener('click', function (e) {
    var b = e.target.closest('[data-snd]');
    if (b) { soundOn = !soundOn; setSound(); return; }
    var c = e.target.closest('.reel-card');
    if (c && !e.target.closest('a,button')) { var v = c.querySelector('video'); v.paused ? v.play() : v.pause(); }
  });
  window.addEventListener('wheel', function (e) {
    if (Math.abs(e.deltaY) < 14) return;
    e.preventDefault();
    if (isLock) return;
    goTo(activeIndex() + (e.deltaY > 0 ? 1 : -1));
  }, { passive: false });
  document.addEventListener('keydown', function (e) {
    var d = { ArrowDown: 1, PageDown: 1, ArrowUp: -1, PageUp: -1, j: 1, k: -1, ' ': 1 }[e.key];
    if (!d || /^(INPUT|TEXTAREA|SELECT)$/.test(document.activeElement.tagName)) return;
    e.preventDefault(); goTo(activeIndex() + d);
  });
  var ty = 0, tx = 0;
  window.addEventListener('touchstart', function (e) { if (e.touches.length) { ty = e.touches[0].clientY; tx = e.touches[0].clientX; } }, { passive: true });
  window.addEventListener('touchend', function (e) {
    if (!e.changedTouches.length || isLock) return;
    var dy = ty - e.changedTouches[0].clientY, dx = tx - e.changedTouches[0].clientX;
    if (Math.abs(dy) > 38 && Math.abs(dy) > Math.abs(dx)) goTo(activeIndex() + (dy > 0 ? 1 : -1));
  }, { passive: true });

  fetch('data.json?v=' + Date.now()).then(function (r) { return r.json(); }).then(function (d) {
    var items = d.samples.filter(function (x) { return x.kind !== 'A' && x.video; });
    shuffle(items); mount(items);
  }).catch(function () { reels.innerHTML = '<p class="smp-none">Could not load the samples.</p>'; });
})();
