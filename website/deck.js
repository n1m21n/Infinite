/* Infinite deck: one gesture engine for the blog and the library.
   A critically damped spring drives a continuous position, so drags, flings, trackpad swipes and arrow keys all
   share one motion. Three arrangements, each its own picture:
     fan    a hand of cards pivoting from below, the centre card lifted
     stack  a pile in depth, the top card is thrown aside to reveal the next
     grid   a bento of flat tiles, every tile at full brightness
   API: const deck = InfiniteDeck.create({stage, grid, cardSel, getCards, onChange, onMode});
        deck.setMode('fan'|'stack'|'grid'), deck.refresh(), deck.goTo(i), deck.step(n), deck.index, deck.mode */
(function () {
  'use strict';
  var reduce = matchMedia('(prefers-reduced-motion: reduce)').matches;
  var mod = function (n, m) { return m > 0 ? ((n % m) + m) % m : 0; };
  var clamp = function (v, a, b) { return Math.min(b, Math.max(a, v)); };

  function create(o) {
    var stage = o.stage, grid = o.grid, cardSel = o.cardSel || '.deck-card';
    var mode = o.mode || 'fan';
    var pos = 0, target = 0, vel = 0;   // continuous position in card units (unwrapped), its target, its velocity
    var raf = 0, last = 0, glide = false;   // glide: a finger just let go, so settle more softly
    var drag = null, moved = false, wheelT = 0, wheelIdle = 0;
    var lastIdx = -1;
    var io = 'IntersectionObserver' in window ? new IntersectionObserver(function (es) {
      es.forEach(function (en) { if (en.isIntersecting) { wake(en.target); io.unobserve(en.target); } });
    }, { rootMargin: '600px 0px' }) : null;

    var cards = function () { return o.getCards() || []; };
    var coarse = function () { return window.innerWidth < 680; };
    var index = function () { var n = cards().length; return n ? mod(Math.round(pos), n) : 0; };

    // signed circular distance from the (fractional) centre to card i, in -n/2..n/2
    function delta(i, n) {
      var d = i - mod(pos, n);
      if (d > n / 2) d -= n; else if (d < -n / 2) d += n;
      return d;
    }

    function pose(d) {
      var ab = Math.abs(d), s = d < 0 ? -1 : (d > 0 ? 1 : 0), m = coarse();
      var tx = 0, ty = 0, tz = 0, rz = 0, ry = 0, sc = 1, op = 1, dim = 0;
      if (mode === 'fan') {
        // hand of cards: each card rotates about a point far below the stage
        var th = d * (m ? 0.24 : 0.155);               // radians per card
        var R = m ? 380 : 660;
        tx = Math.sin(th) * R;
        ty = (1 - Math.cos(th)) * R * 0.9 - (ab < 1 ? (1 - ab) * 14 : 0);
        rz = th * 57.2958;
        tz = -ab * 18;
        sc = 1 - Math.min(ab, 3) * (m ? 0.07 : 0.035);
        op = clamp((3.6 - ab) / 0.9, 0, 1);
        dim = Math.min(0.2, ab * 0.07);
      } else { // stack
        var W = m ? 300 : 520;
        if (d >= 0) {                                   // waiting behind: a pile receding upward in depth
          var k = Math.min(d, 5);
          ty = 40 - k * (m ? 26 : 34);
          tz = -k * 48;
          sc = 1 - k * 0.05;
          op = clamp((3.4 - d) / 0.9, 0, 1);
          dim = Math.min(0.24, d * 0.08);
        } else {                                        // thrown aside
          var f = Math.min(-d, 1.6);
          tx = -f * W;
          ty = 40 + f * 26;
          rz = -f * 16;
          ry = f * 24;
          op = clamp(1 + d * 1.15, 0, 1);
        }
      }
      return { tx: tx, ty: ty, tz: tz, rz: rz, ry: ry, sc: sc, op: op, dim: dim, ab: ab, s: s };
    }

    function paint() {
      var list = cards(), n = list.length;
      stage.setAttribute('data-deck-mode', mode);
      if (mode === 'grid' || !n) {
        list.forEach(function (c) {
          c.style.display = ''; c.__far = 0; c.__off = undefined;
          c.style.transform = ''; c.style.opacity = ''; c.style.zIndex = ''; c.style.pointerEvents = '';
          c.style.setProperty('--card-dim', '0');
          c.removeAttribute('data-slot'); c.removeAttribute('aria-hidden'); c.removeAttribute('inert');
          if (io) io.observe(c); else wake(c);
        });
        return;
      }
      list.forEach(function (c, i) {
        var d = delta(i, n);
        if (Math.abs(d) > 4.6) {                         // far away: hide once, then skip all the per-frame style writes
          if (c.__far !== mode) { c.style.display = 'none'; c.setAttribute('aria-hidden', 'true'); c.setAttribute('inert', ''); c.__far = mode; }
          return;
        }
        c.__far = 0;
        var p = pose(d);
        c.style.transform = 'translate3d(' + p.tx.toFixed(1) + 'px,' + p.ty.toFixed(1) + 'px,' + p.tz.toFixed(1) + 'px) rotateY(' + p.ry.toFixed(2) + 'deg) rotateZ(' + p.rz.toFixed(2) + 'deg) scale(' + p.sc.toFixed(3) + ')';
        c.style.display = p.ab > 4.6 ? 'none' : '';
        c.style.opacity = p.op.toFixed(3);
        c.style.zIndex = String(Math.round(200 - (mode === 'stack' ? d : p.ab) * 20));
        c.style.pointerEvents = p.op < 0.15 ? 'none' : 'auto';
        c.style.setProperty('--card-dim', p.dim.toFixed(3));
        var slot = p.ab < 0.35 ? 'active' : (p.ab < 1.35 ? 'near' : (p.ab < 2.45 ? 'mid' : 'far'));
        if (c.getAttribute('data-slot') !== slot) c.setAttribute('data-slot', slot);
        var off = p.ab > 3.4;
        if (c.__off !== off) {
          c.__off = off; c.setAttribute('aria-hidden', off ? 'true' : 'false');
          if (off) c.setAttribute('inert', ''); else c.removeAttribute('inert');
        }
        if (p.ab <= 4.4) wake(c, p.ab < 0.35);
      });
      var ix = index();
      if (ix !== lastIdx) { lastIdx = ix; if (o.onChange) o.onChange(ix); }
    }

    function wake(c, first) {
      var im = c.querySelector('img[data-src]');
      if (im) {
        if (first) im.setAttribute('fetchpriority', 'high'); im.src = im.getAttribute('data-src'); im.removeAttribute('data-src'); }
      var lz = c.querySelector('img[loading="lazy"]');
      if (lz) lz.setAttribute('loading', 'eager');
    }

    // critically damped spring (Unity SmoothDamp): smooth start and stop, interruptible, never overshoots
    function tick(now) {
      var dt = Math.min(0.05, (now - last) / 1000 || 0.016); last = now;
      if (drag) { pos = target; vel = 0; }
      else {
        var om = 2 / (glide && coarse() ? 0.24 : 0.17), x = om * dt, ex = 1 / (1 + x + 0.48 * x * x + 0.235 * x * x * x);
        var ch = pos - target, tmp = (vel + om * ch) * dt;
        vel = (vel - om * tmp) * ex;
        pos = target + (ch + tmp) * ex;
        if (Math.abs(pos - target) < 0.0008 && Math.abs(vel) < 0.002) { pos = target; vel = 0; }
      }
      paint();
      if (drag || pos !== target) raf = requestAnimationFrame(tick); else { raf = 0; glide = false; }
    }
    function run() { if (!raf) { last = performance.now(); raf = requestAnimationFrame(tick); } }

    function goTo(i) {
      var n = cards().length; if (!n) return;
      // shortest way round from where we are now
      var cur = Math.round(pos), d = mod(i, n) - mod(cur, n);
      if (d > n / 2) d -= n; else if (d < -n / 2) d += n;
      target = cur + d;
      if (reduce) { pos = target; vel = 0; paint(); } else run();
    }
    function step(k) { target = Math.round(target) + k; if (reduce) { pos = target; paint(); } else run(); }

    function setMode(m) {
      mode = m;
      if (m !== 'grid') { var r = Math.round(pos); pos = target = r; vel = 0; }
      lastIdx = -1;
      paint();
    }
    function refresh(keepIndex) {
      var n = cards().length;
      if (!keepIndex) { pos = target = 0; vel = 0; }
      else { pos = target = clamp(Math.round(pos), 0, Math.max(0, n - 1)); }
      lastIdx = -1; paint();
    }

    // ---- gestures ----
    var stepPx = function () { return coarse() ? 105 : 200; };
    grid.addEventListener('pointerdown', function (e) {
      if (mode === 'grid' || cards().length <= 1) return;
      if (e.pointerType === 'mouse' && e.button !== 0) return;
      drag = { x: e.clientX, y: e.clientY, t: performance.now(), base: pos, vx: 0, lx: e.clientX, lt: performance.now(), live: false, id: e.pointerId };
      moved = false;
    }, { passive: true });
    window.addEventListener('pointermove', function (e) {
      if (!drag || e.pointerId !== drag.id) return;
      var dx = e.clientX - drag.x, dy = e.clientY - drag.y;
      if (!drag.live) {
        if (Math.abs(dy) > Math.abs(dx) * 1.2 && Math.abs(dy) > 10) { drag = null; return; } // a page scroll, not a swipe
        if (Math.abs(dx) < 6) return;
        drag.live = true; moved = true; stage.classList.add('is-dragging');
        drag.x = e.clientX; dx = 0; drag.lx = e.clientX; drag.lt = performance.now();
        try { grid.setPointerCapture(e.pointerId); } catch (x) {}
      }
      var now = performance.now();
      drag.vx = 0.8 * drag.vx + 0.2 * ((e.clientX - drag.lx) / Math.max(1, now - drag.lt));
      drag.lx = e.clientX; drag.lt = now;
      target = drag.base - dx / stepPx();
      run();
    }, { passive: true });
    function end() {
      if (!drag) return;
      var was = drag; drag = null; stage.classList.remove('is-dragging');
      if (!was.live) return;
      // release: carry the flick, settle on a card
      var fling = -was.vx * 0.32;                  // px/ms to card units
      target = Math.round(clamp(target + fling * 2.2, was.base - 4, was.base + 4));
      glide = true; vel = reduce ? 0 : -was.vx / stepPx() * 1000 * 0.35;
      setTimeout(function () { moved = false; }, 60);
      run();
    }
    window.addEventListener('pointerup', end, { passive: true });
    window.addEventListener('pointercancel', end, { passive: true });

    // trackpad: two-finger sideways swipe follows the fingers, then settles on a card when they lift
    grid.addEventListener('wheel', function (e) {
      if (mode === 'grid' || cards().length <= 1) return;
      var h = Math.abs(e.deltaX) > Math.abs(e.deltaY) * 0.85;
      if (!h && !e.shiftKey) return;
      e.preventDefault();
      var dlt = Math.abs(e.deltaX) >= Math.abs(e.deltaY) ? e.deltaX : e.deltaY;
      target += clamp(dlt, -90, 90) / (stepPx() * 1.6);
      clearTimeout(wheelIdle);
      wheelIdle = setTimeout(function () { target = Math.round(target); run(); }, 110);
      run();
    }, { passive: false });

    window.addEventListener('keydown', function (e) {
      if (mode === 'grid' || e.defaultPrevented || o.keys === false) return;
      var t = e.target && e.target.tagName ? e.target.tagName.toLowerCase() : '';
      if (t === 'input' || t === 'textarea' || t === 'select') return;
      if (e.key === 'ArrowRight' || e.key === 'ArrowDown') { e.preventDefault(); step(1); }
      else if (e.key === 'ArrowLeft' || e.key === 'ArrowUp') { e.preventDefault(); step(-1); }
    });

    // tap a card that is not in front: bring it forward; tap the front card: follow its link
    grid.addEventListener('click', function (e) {
      if (mode === 'grid') return;
      var c = e.target.closest(cardSel);
      if (!c && e.clientX != null) {                    // 3D hit-testing can report the stage; ask for the whole stack under the pointer
        var under = document.elementsFromPoint(e.clientX, e.clientY);
        for (var u = 0; u < under.length && !c; u++) c = under[u].closest && under[u].closest(cardSel);
      }
      if (!c) return;
      if (moved) { e.preventDefault(); return; }
      var list = cards(), i = list.indexOf(c); if (i < 0) return;
      var d = delta(i, list.length);
      if (Math.abs(d) > 0.3) { e.preventDefault(); e.stopPropagation(); step(Math.round(d)); }
    }, true);

    window.addEventListener('resize', function () { paint(); });

    return {
      get index() { return index(); },
      get mode() { return mode; },
      setMode: setMode, refresh: refresh, goTo: goTo, step: step,
      get position() { return pos; }
    };
  }
  window.InfiniteDeck = { create: create };
})();
