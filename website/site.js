/* ==========================================================================
   Infinite website v2: page behaviour for the film language
   (logo, hero word, zoom tour, film, cables, aside, knobs, bird, library, tabs)
   app.js keeps the canvases (cosmic, nature, node views, audio player, modal).
   ========================================================================== */
(() => {
  'use strict';

  const $ = (s, r = document) => r.querySelector(s);
  const $$ = (s, r = document) => Array.from(r.querySelectorAll(s));
  const clamp = (x, a, b) => Math.min(b, Math.max(a, x));
  const lerp = (a, b, t) => a + (b - a) * t;
  const smooth = (t) => t * t * (3 - 2 * t);
  const reduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  const DPR = Math.min(window.devicePixelRatio || 1, 2);

  const CAT = {
    Source: '#4ADE80', '3D': '#38BDF8', Compositing: '#818CF8', Effects: '#FACC15',
    Modulators: '#A3E635', Prediction: '#22D388', Macros: '#FC963C', Utility: '#A3A9BA',
    Notes: '#4CD964', Synths: '#6992F6', AudioEffects: '#53C7E3'
  };
  const CAT_LABEL = { AudioEffects: 'Audio FX' };
  const catLabel = (c) => CAT_LABEL[c] || c;

  function onVisible(el, cb, opts = { threshold: 0.2 }) {
    if (!el) return;
    if (!('IntersectionObserver' in window)) { cb(); return; }
    const io = new IntersectionObserver((es) => {
      es.forEach((e) => { if (e.isIntersecting) { io.disconnect(); cb(); } });
    }, opts);
    io.observe(el);
  }

  function sizeCanvas(cv) {
    const r = cv.getBoundingClientRect();
    cv.width = Math.max(1, Math.round(r.width * DPR));
    cv.height = Math.max(1, Math.round(r.height * DPR));
    return { w: r.width, h: r.height };
  }

  /* ------------------------------------------------------------ OS + download */
  function detectOS() {
    const ua = navigator.userAgent || '';
    const plat = (navigator.userAgentData && navigator.userAgentData.platform) || navigator.platform || '';
    const touchMac = /Mac/.test(plat) && navigator.maxTouchPoints > 1;
    if (/iPhone|iPad|iPod|Android/i.test(ua) || touchMac) return 'mobile';
    if (/Win/i.test(plat) || /Windows/i.test(ua)) return 'win';
    if (/Mac/i.test(plat) || /Macintosh/i.test(ua)) return 'mac';
    if (/Linux|X11/i.test(plat + ua)) return 'linux';
    return 'unknown';
  }
  const OS = detectOS();
  const OS_NAME = { mac: 'macOS', win: 'Windows', linux: 'Linux' };

  function initDownloads() {
    const primary = $(`.btn-dl[data-os="${OS}"]`);
    if (primary) {
      primary.classList.add('primary');
      primary.parentNode.prepend(primary);
    }
    $$('[data-download-link]').forEach((a) => {
      const label = $('[data-download-label]', a);
      if (primary) {
        a.href = primary.href;
        if (OS === 'mac') a.setAttribute('download', 'Infinite.dmg');
        if (label) label.textContent = `Download for ${OS_NAME[OS]}`;
        a.addEventListener('click', () => {
          if (typeof window.gtag === 'function') window.gtag('event', 'download_click', { os: OS_NAME[OS], from: a.id || 'nav' });
        });
      } else {
        a.href = '#download';
        if (OS === 'mobile') a.style.display = 'none'; // a phone cannot install it, so no button at all
        else if (label) label.textContent = 'Download Infinite';
      }
    });
  }

  /* Learn / Create / Build groups: click or hover opens a panel on desktop; Esc and outside click close it.
     On phones every group is shown open inside the sheet, so the triggers are inert labels. */
  function initMenuGroups(header, closeSheet) {
    const groups = $$('.nav-group', header);
    if (!groups.length) return;
    const phone = window.matchMedia('(max-width: 900px)');
    const hover = window.matchMedia('(hover: hover) and (pointer: fine)');
    const trig = (g) => $('.nav-trigger', g);
    const setOpen = (g, open) => trig(g).setAttribute('aria-expanded', String(open));
    const closeAll = (except) => groups.forEach((g) => { if (g !== except) setOpen(g, false); });
    const sync = () => groups.forEach((g) => {
      setOpen(g, phone.matches);   // phone: the side panel shows every group
    });
    sync();
    (phone.addEventListener ? phone.addEventListener('change', sync) : phone.addListener(sync));
    groups.forEach((g) => {
      const t = trig(g);
      let enteredAt = 0;   // a click right after the pointer arrived must not close the panel the hover just opened
      t.addEventListener('click', () => {
        const open = !phone.matches && Date.now() - enteredAt < 600 ? true : t.getAttribute('aria-expanded') !== 'true';
        closeAll(g); setOpen(g, open);
      });
      let closeTimer = 0;   // a short grace period so a pointer drifting off the edge can still reach a link
      g.addEventListener('mouseenter', () => { clearTimeout(closeTimer); if (!phone.matches && hover.matches) { enteredAt = Date.now(); closeAll(g); setOpen(g, true); } });
      g.addEventListener('mouseleave', () => { if (!phone.matches && hover.matches) { clearTimeout(closeTimer); closeTimer = setTimeout(() => setOpen(g, false), 180); } });
      g.addEventListener('focusout', (e) => { if (!phone.matches && !g.contains(e.relatedTarget)) setOpen(g, false); });
      g.addEventListener('keydown', (e) => { if (e.key === 'Escape' && !phone.matches) { setOpen(g, false); t.focus(); e.stopPropagation(); } });
    });
    document.addEventListener('click', (e) => { if (!phone.matches && !e.target.closest('.nav-group')) closeAll(); });
  }

  /* ------------------------------------------------------------ header + menu */
  function initHeader() {
    const header = $('#site-header');
    const btn = $('#menu-btn');
    const links = $('#nav-links');
    if (!header) return;
    const onScroll = () => header.classList.toggle('scrolled', window.scrollY > 8);
    window.addEventListener('scroll', onScroll, { passive: true });
    onScroll();
    if (!btn || !links) return;
    const set = (open) => {
      header.classList.toggle('nav-open', open);
      btn.setAttribute('aria-expanded', String(open));
      btn.setAttribute('aria-label', open ? 'Close menu' : 'Open menu');
    };
    initMenuGroups(header, set);
    btn.addEventListener('click', () => set(!header.classList.contains('nav-open')));
    $$('a', links).forEach((a) => a.addEventListener('click', () => set(false)));
    document.addEventListener('keydown', (e) => { if (e.key === 'Escape' && header.classList.contains('nav-open')) { set(false); btn.focus(); } });
    document.addEventListener('click', (e) => {
      if (header.classList.contains('nav-open') && !header.contains(e.target)) set(false);
    });
    window.addEventListener('resize', () => { if (window.innerWidth > 900) set(false); });
  }


  /* ------------------------------------------------------------ dock: bottom bar on phones, icon rail on desktop */
  function initDock() {
    const dock = $('#dock');
    if (!dock) return;
    const bar = $('.dock-bar', dock), thumb = $('.dock-thumb', dock);
    const stops = $$('.dock-stop', dock), sheets = $$('.dock-sheet', dock);
    const vertical = () => window.matchMedia('(min-width: 901px)').matches;
    let openId = null;
    const resting = () => stops.find((b) => b.hasAttribute('data-current') || b.getAttribute('aria-current') === 'page') || null;
    let hov = null;
    const place = () => {
      const on = hov || (openId && stops.find((b) => b.dataset.group === openId)) || resting();
      stops.forEach((b) => b.classList.toggle('is-on', b === on));
      if (!on) { thumb.style.opacity = '0'; return; }
      if (!vertical()) thumb.style.removeProperty('--y');
      thumb.style.opacity = '';
      thumb.style.setProperty('--x', on.offsetLeft + 'px'); thumb.style.setProperty('--y', on.offsetTop + 'px');
      thumb.style.setProperty('--w', on.offsetWidth + 'px'); thumb.style.setProperty('--h', on.offsetHeight + 'px');
    };
    const show = (id) => {
      openId = id;
      sheets.forEach((sh) => { sh.hidden = sh.dataset.group !== id; });
      stops.forEach((b) => { if (b.dataset.group) b.setAttribute('aria-expanded', String(b.dataset.group === id)); });
      dock.classList.toggle('open', !!id);
      place();
    };
    stops.forEach((b) => {
      b.addEventListener('pointerenter', (e) => { if (vertical() && e.pointerType === 'mouse') { hov = b; show(b.dataset.group || null); } });
      if (!b.dataset.group) return;
      b.addEventListener('click', () => show(openId === b.dataset.group ? null : b.dataset.group));
    });
    dock.addEventListener('pointerleave', (e) => { if (vertical() && e.pointerType === 'mouse') { hov = null; show(null); } });
    dock.addEventListener('keydown', (e) => {
      if (e.key === 'Escape') { show(null); return; }
      const keys = vertical() ? ['ArrowUp', 'ArrowDown'] : ['ArrowLeft', 'ArrowRight'];
      const i = stops.indexOf(document.activeElement);
      if (i < 0 || keys.indexOf(e.key) < 0) return;
      e.preventDefault();
      stops[(i + (e.key === keys[1] ? 1 : stops.length - 1)) % stops.length].focus();
    });
    document.addEventListener('click', (e) => { if (openId && !dock.contains(e.target)) show(null); });
    $$('.dock-sheet a', dock).forEach((a) => a.addEventListener('click', () => show(null)));
    window.addEventListener('resize', place);
    place();
    requestAnimationFrame(() => requestAnimationFrame(() => dock.classList.add('ready')));
    if (document.fonts && document.fonts.ready) document.fonts.ready.then(place);
  }

  /* ------------------------------------------------------------ the exact logo */
  function initLogo() {
    const path = $('#lemni-path');
    const ball = $('#lemni-ball');
    if (!path) return;
    const A = 100, N = 480;
    let d = '';
    for (let k = 0; k <= N; k++) {
      const p = (k / N) * Math.PI * 2;
      const s = Math.sin(p), c = Math.cos(p), q = 1 + c * c;
      const x = (A * s) / q, y = (A * s * c) / q;
      d += (k ? 'L' : 'M') + x.toFixed(3) + ' ' + y.toFixed(3);
    }
    path.setAttribute('d', d + 'Z');
    path.setAttribute('stroke-width', ((87 / 338) * A).toFixed(3));
    const len = path.getTotalLength();

    if (reduced) { if (ball) { const pt = path.getPointAtLength(len * 0.25); ball.setAttribute('cx', pt.x); ball.setAttribute('cy', pt.y); ball.style.opacity = 1; } return; }

    path.style.strokeDasharray = len;
    path.style.strokeDashoffset = len;
    path.getBoundingClientRect();
    path.style.transition = 'stroke-dashoffset 1.3s cubic-bezier(0.65, 0, 0.35, 1) 0.15s';
    path.style.strokeDashoffset = '0';

    if (!ball) return;
    const hero = $('.hero');
    let visible = true, t0 = performance.now() + 1450, raf = 0;
    const tick = (now) => {
      raf = 0;
      if (!visible) return;
      const t = Math.max(0, now - t0) / 1000;
      if (t > 0) ball.style.opacity = Math.min(1, t * 3);
      // ride the loop: 4 s per lap, a little faster through the crossing (arcs, not constant speed)
      const u = (t / 4) % 1;
      const e = u + 0.035 * Math.sin(u * Math.PI * 4);
      const pt = path.getPointAtLength(((e % 1) + 1) % 1 * len);
      ball.setAttribute('cx', pt.x.toFixed(2));
      ball.setAttribute('cy', pt.y.toFixed(2));
      raf = requestAnimationFrame(tick);
    };
    if ('IntersectionObserver' in window && hero) {
      new IntersectionObserver((es) => {
        visible = es[0].isIntersecting;
        if (visible && !raf) raf = requestAnimationFrame(tick);
      }).observe(hero);
    }
    raf = requestAnimationFrame(tick);
  }


  /* ------------------------------------------------------------ hero word */
  function initHeroWord() {
    const word = $('#hero-word');
    const line = $('#hero-underline');
    if (!word) return;
    const EMBER = 'var(--accent-ink)'; // the Ember hand word: brand Ember in each mode (brighter on Midnight)
    const WORDS = [
      ['designers', EMBER], ['artists', EMBER], ['musicians', EMBER],
      ['VJs', EMBER], ['scientists', EMBER], ['creatives', EMBER], ['you', EMBER]
    ];
    const show = (w, col) => {
      word.textContent = w;
      word.style.setProperty('--hand', col);
      if (line) {
        line.style.setProperty('--hand', col);
        line.style.width = (word.offsetWidth + 10) + 'px';
      }
      if (reduced) { if (line) line.classList.add('draw'); return; }
      word.classList.remove('writing');
      if (line) line.classList.remove('draw');
      void word.offsetWidth;
      word.classList.add('writing');
      if (line) line.classList.add('draw');
    };
    const fit = () => { if (line) line.style.width = (word.offsetWidth + 10) + 'px'; };
    window.addEventListener('resize', fit);
    if (document.fonts && document.fonts.ready) document.fonts.ready.then(fit);

    if (reduced) { show('you', '#B53700'); return; }
    let i = 0;
    const next = () => {
      const [w, col] = WORDS[i];
      show(w, col);
      i++;
      if (i < WORDS.length) setTimeout(next, i === 1 ? 1300 : 1050);
    };
    setTimeout(next, 700);
  }

  /* ------------------------------------------------------------ HUD */
  function initHud() {
    const clock = $('#hud-clock');
    const coord = $('.hud-tl');
    const hero = $('.hero');
    if (clock) {
      const BPM = 120, beat = 60 / BPM, t0 = performance.now();
      const upd = () => {
        const b = Math.floor((performance.now() - t0) / 1000 / beat);
        clock.textContent = `bar ${String(Math.floor(b / 4) % 100 + 1).padStart(2, '0')}.${(b % 4) + 1}  ·  ${BPM} bpm`;
      };
      upd();
      if (!reduced) setInterval(upd, beat * 1000);
    }
    if (coord && hero && !reduced) {
      hero.addEventListener('pointermove', (e) => {
        const r = hero.getBoundingClientRect();
        const x = (e.clientX - r.left) / r.width * 2 - 1;
        const y = (e.clientY - r.top) / r.height * 2 - 1;
        coord.textContent = `x ${x >= 0 ? ' ' : ''}${x.toFixed(3)}   y ${y >= 0 ? ' ' : ''}${y.toFixed(3)}`;
      });
    }
  }

  /* ------------------------------------------------------------ film */
  function initFilm() {
    const video = $('#film-video');
    const btn = $('#film-sound');
    if (!video) return;
    // slow or data-saving connections get the 540p cut and no autoplay:
    // the poster stays until the visitor taps, so nothing downloads on its own
    const net = navigator.connection || {};
    const slow = !!net.saveData || /(^|-)2g|3g/.test(net.effectiveType || '');
    let loaded = false;
    const load = () => {
      if (loaded) return;
      loaded = true;
      const small = slow || window.innerWidth < 720;
      video.src = small ? video.dataset.srcSm : video.dataset.srcLg;
      video.preload = slow ? 'metadata' : 'auto';
    };
    const tryPlay = () => { const p = video.play(); if (p && p.catch) p.catch(() => {}); };
    if (slow) {
      video.controls = true;
      video.addEventListener('play', load, { once: true });
      video.addEventListener('click', () => { if (!loaded) { load(); tryPlay(); } }, { once: true });
    } else if ('IntersectionObserver' in window) {
      new IntersectionObserver((es) => {
        const e = es[0];
        if (e.isIntersecting) { load(); if (!reduced || !video.muted) tryPlay(); }
        else video.pause();
      }, { threshold: 0.35, rootMargin: '200px 0px 0px 0px' }).observe(video);
    } else { load(); }

    if (!btn) return;
    const label = $('.film-sound-label', btn);
    btn.addEventListener('click', () => {
      load();
      video.muted = !video.muted;
      btn.setAttribute('aria-pressed', String(!video.muted));
      if (label) label.textContent = video.muted ? 'Sound on' : 'Sound off';
      if (!video.muted) { if (video.currentTime > 1 && video.ended) video.currentTime = 0; tryPlay(); }
    });
    video.addEventListener('click', () => { if (video.controls) return; if (video.paused) tryPlay(); else video.pause(); });
  }

  /* ------------------------------------------------------------ what-if cables */
  function initChain() {
    const chain = $('#chain');
    const svg = $('#chain-cables');
    if (!chain || !svg) return;
    const NS = 'http://www.w3.org/2000/svg';
    const cards = $$('.node-card', chain);

    const draw = () => {
      const cr = chain.getBoundingClientRect();
      svg.setAttribute('viewBox', `0 0 ${cr.width} ${cr.height}`);
      svg.innerHTML = '';
      for (let i = 0; i < cards.length - 1; i++) {
        const out = $('.port-out', cards[i]);
        const inp = $('.port-in', cards[i + 1]);
        if (!out || !inp) continue;
        const a = out.getBoundingClientRect(), b = inp.getBoundingClientRect();
        const x1 = a.left + a.width / 2 - cr.left, y1 = a.top + a.height / 2 - cr.top;
        const x2 = b.left + b.width / 2 - cr.left, y2 = b.top + b.height / 2 - cr.top;
        const reach = Math.max(40, Math.abs(x2 - x1) * 0.45);
        const d = `M${x1.toFixed(1)} ${y1.toFixed(1)} C${(x1 + reach).toFixed(1)} ${y1.toFixed(1)}, ${(x2 - reach).toFixed(1)} ${y2.toFixed(1)}, ${x2.toFixed(1)} ${y2.toFixed(1)}`;
        const p = document.createElementNS(NS, 'path');
        p.setAttribute('d', d);
        p.setAttribute('class', 'cable');
        p.style.setProperty('--i', i);
        svg.appendChild(p);
        const len = p.getTotalLength();
        p.style.setProperty('--len', len.toFixed(1));
        if (!reduced) {
          const c = document.createElementNS(NS, 'circle');
          c.setAttribute('r', '3.2');
          c.setAttribute('class', 'pulse');
          c.setAttribute('opacity', '0');
          const m = document.createElementNS(NS, 'animateMotion');
          m.setAttribute('dur', '1.9s');
          m.setAttribute('begin', `${1.4 + i * 0.3}s`);
          m.setAttribute('repeatCount', 'indefinite');
          m.setAttribute('path', d);
          const o = document.createElementNS(NS, 'animate');
          o.setAttribute('attributeName', 'opacity');
          o.setAttribute('values', '0;1;1;0');
          o.setAttribute('dur', '1.9s');
          o.setAttribute('begin', `${1.4 + i * 0.3}s`);
          o.setAttribute('repeatCount', 'indefinite');
          c.appendChild(m); c.appendChild(o);
          svg.appendChild(c);
        }
      }
    };
    let t;
    window.addEventListener('resize', () => { clearTimeout(t); t = setTimeout(draw, 120); });
    if (document.fonts && document.fonts.ready) document.fonts.ready.then(draw);
    draw();

    if ('IntersectionObserver' in window) {
      new IntersectionObserver((es) => {
        const vis = es[0].isIntersecting;
        if (vis) chain.classList.add('in');
        if (svg.pauseAnimations) vis ? svg.unpauseAnimations() : svg.pauseAnimations();
      }, { threshold: 0.2 }).observe(chain);
    } else chain.classList.add('in');
  }

  /* ------------------------------------------------------------ aside: typewriter + pixel type */
  const FONT5x7 = {
    'A': ['01110', '10001', '10001', '11111', '10001', '10001', '10001'],
    'C': ['01110', '10001', '10000', '10000', '10000', '10001', '01110'],
    'D': ['11110', '10001', '10001', '10001', '10001', '10001', '11110'],
    'E': ['11111', '10000', '10000', '11110', '10000', '10000', '11111'],
    'I': ['01110', '00100', '00100', '00100', '00100', '00100', '01110'],
    'M': ['10001', '11011', '10101', '10101', '10001', '10001', '10001'],
    'N': ['10001', '11001', '10101', '10011', '10001', '10001', '10001'],
    'O': ['01110', '10001', '10001', '10001', '10001', '10001', '01110'],
    'P': ['11110', '10001', '10001', '11110', '10000', '10000', '10000'],
    'R': ['11110', '10001', '10001', '11110', '10100', '10010', '10001'],
    'S': ['01111', '10000', '10000', '01110', '00001', '00001', '11110'],
    'T': ['11111', '00100', '00100', '00100', '00100', '00100', '00100'],
    'U': ['10001', '10001', '10001', '10001', '10001', '10001', '01110'],
    'V': ['10001', '10001', '10001', '10001', '10001', '01010', '00100'],
    'X': ['10001', '10001', '01010', '00100', '01010', '10001', '10001'],
    'Y': ['10001', '10001', '01010', '00100', '00100', '00100', '00100'],
    '.': ['00000', '00000', '00000', '00000', '00000', '01100', '01100'],
    ' ': ['00000', '00000', '00000', '00000', '00000', '00000', '00000']
  };

  function initAside() {
    const type = $('#aside-type');
    const cv = $('#pixel-title');
    if (!type || !cv) return;
    const TEXT = type.dataset.text || '';
    const FULL = 'IT PREDICTS YOUR NEXT MOVE.';
    let shown = reduced ? FULL.length : 0;

    const drawPixels = () => {
      const narrow = cv.parentElement.clientWidth < 620;
      const lines = narrow ? ['IT PREDICTS', 'YOUR NEXT MOVE.'] : [FULL];
      const cols = Math.max(...lines.map((l) => l.length)) * 6 - 1 + 1;   // +1 for the shadow
      const rows = lines.length * 9 - 2 + 1;
      const cssW = cv.parentElement.clientWidth;
      const px = Math.max(2, Math.floor(Math.min(cssW, 880) / cols));
      const w = cols * px, h = rows * px;
      cv.style.width = w + 'px';
      cv.style.height = h + 'px';
      cv.width = Math.round(w * DPR);
      cv.height = Math.round(h * DPR);
      const g = cv.getContext('2d');
      g.setTransform(DPR, 0, 0, DPR, 0, 0);
      g.clearRect(0, 0, w, h);
      let n = 0;
      lines.forEach((ln, li) => {
        const x0 = Math.round((cols - 1 - (ln.length * 6 - 1)) / 2);
        for (let ci = 0; ci < ln.length; ci++, n++) {
          if (n >= shown) return;
          const gl = FONT5x7[ln[ci]] || FONT5x7[' '];
          for (let pass = 0; pass < 2; pass++) {
            g.fillStyle = pass === 0 ? '#22D388' : '#12152A';
            const off = pass === 0 ? 1 : 0;
            for (let r = 0; r < 7; r++) for (let c = 0; c < 5; c++) {
              if (gl[r][c] === '1') g.fillRect((x0 + ci * 6 + c + off) * px, (li * 9 + r + off) * px, px, px);
            }
          }
        }
        n += 0;
      });
    };
    drawPixels();
    window.addEventListener('resize', drawPixels);

    if (reduced) { type.textContent = TEXT; return; }
    type.textContent = ' ';
    onVisible(type, () => {
      let i = 0;
      type.classList.add('typing');
      const step = () => {
        i++;
        type.textContent = TEXT.slice(0, i);
        if (i < TEXT.length) setTimeout(step, TEXT[i - 1] === ',' ? 260 : 67);
        else {
          // the aside rests, then the headline types itself in pixels
          setTimeout(() => {
            type.classList.remove('typing');
            const tick = () => {
              shown++;
              drawPixels();
              if (shown < FULL.length) setTimeout(tick, FULL[shown - 1] === ' ' ? 90 : 45);
            };
            tick();
          }, 450);
        }
      };
      setTimeout(step, 250);
    }, { threshold: 0.6 });
  }

  /* ------------------------------------------------------------ predict knobs */
  function initPredictKnobs() {
    const cv = $('#predict-knobs');
    if (!cv) return;
    const g = cv.getContext('2d');
    const KN = [
      { name: 'cutoff', v: 0.35, vel: 0.004, m: 0.5 },
      { name: 'hue', v: 0.7, vel: -0.003, m: 0.5 },
      { name: 'depth', v: 0.5, vel: 0.002, m: 0.5 }
    ];
    const PHI = 0.985, PULL = 0.0009;
    const A0 = Math.PI * 0.75, SWEEP = Math.PI * 1.5;
    let W = 0, H = 0, drag = null, visible = false, raf = 0, last = 0;
    const resize = () => { const s = sizeCanvas(cv); W = s.w; H = s.h; };

    const stepK = (k, noise) => {
      k.vel = PHI * k.vel + (k.m - k.v) * PULL + noise;
      k.v += k.vel;
      if (k.v < 0) { k.v = 0; k.vel = Math.abs(k.vel) * 0.6; }
      if (k.v > 1) { k.v = 1; k.vel = -Math.abs(k.vel) * 0.6; }
    };
    const gauss = () => (Math.random() + Math.random() + Math.random() - 1.5) * 0.9;

    const draw = () => {
      g.setTransform(DPR, 0, 0, DPR, 0, 0);
      g.clearRect(0, 0, W, H);
      const cell = W / 3;
      const R = Math.min(cell * 0.34, H * 0.3);
      KN.forEach((k, i) => {
        const cx = cell * (i + 0.5), cy = H * 0.44;
        // ghost forecast: the same AR(1) run forward without noise
        const ghost = { v: k.v, vel: k.vel, m: k.m };
        for (let s = 0; s < 50; s++) stepK(ghost, 0);
        g.lineCap = 'round';
        g.beginPath(); g.arc(cx, cy, R + 9, A0, A0 + SWEEP);
        g.strokeStyle = 'rgba(26,31,54,0.08)'; g.lineWidth = 4; g.stroke();
        const aV = A0 + SWEEP * k.v, aG = A0 + SWEEP * ghost.v;
        g.beginPath(); g.arc(cx, cy, R + 9, Math.min(aV, aG), Math.max(aV, aG));
        g.setLineDash([2, 6]); g.strokeStyle = 'rgba(34,211,136,0.9)'; g.lineWidth = 4; g.stroke(); g.setLineDash([]);
        g.beginPath(); g.arc(cx + Math.cos(aG) * (R + 9), cy + Math.sin(aG) * (R + 9), 4.5, 0, Math.PI * 2);
        g.fillStyle = 'rgba(34,211,136,0.35)'; g.fill();
        // body
        g.beginPath(); g.arc(cx, cy, R, 0, Math.PI * 2);
        g.fillStyle = '#181B23'; g.fill();
        g.beginPath(); g.arc(cx, cy, R * 0.78, A0, A0 + SWEEP);
        g.strokeStyle = '#383C47'; g.lineWidth = R * 0.12; g.stroke();
        g.beginPath(); g.arc(cx, cy, R * 0.78, A0, aV);
        g.strokeStyle = '#22D388'; g.stroke();
        g.beginPath(); g.moveTo(cx + Math.cos(aV) * R * 0.2, cy + Math.sin(aV) * R * 0.2);
        g.lineTo(cx + Math.cos(aV) * R * 0.62, cy + Math.sin(aV) * R * 0.62);
        g.strokeStyle = '#EEF0F6'; g.lineWidth = Math.max(2, R * 0.08); g.stroke();
        g.fillStyle = getComputedStyle(cv).color;
        g.font = `500 ${Math.max(11, Math.min(13, W / 40))}px 'Geist Mono', monospace`;
        g.textAlign = 'center';
        g.fillText(`${k.name} ${String(Math.round(k.v * 100)).padStart(3, ' ')}`, cx, cy + R + 30);
        if (drag === k) {
          g.fillStyle = '#B53700';
          g.fillText('you', cx, cy - R - 16);
        } else {
          g.fillStyle = '#22D388';
          g.fillText('predicting', cx, cy - R - 16);
        }
      });
    };

    const loop = (now) => {
      raf = 0;
      if (!visible) return;
      const dt = Math.min(3, (now - (last || now)) / 16.67);
      last = now;
      if (!reduced) KN.forEach((k) => { if (k !== drag) for (let s = 0; s < Math.round(dt) || s < 1; s++) stepK(k, gauss() * 0.0006); });
      draw();
      raf = requestAnimationFrame(loop);
    };

    let py = 0, pv = 0, lastY = 0;
    cv.addEventListener('pointerdown', (e) => {
      const r = cv.getBoundingClientRect();
      const i = clamp(Math.floor((e.clientX - r.left) / (r.width / 3)), 0, 2);
      drag = KN[i]; py = lastY = e.clientY; pv = drag.v; drag.vel = 0;
      cv.setPointerCapture(e.pointerId);
      e.preventDefault();
    });
    cv.addEventListener('pointermove', (e) => {
      if (!drag) return;
      const nv = clamp(pv + (py - e.clientY) / 160, 0, 1);
      drag.vel = lerp(drag.vel, nv - drag.v, 0.5);   // the gesture's velocity is what gets continued
      drag.m = lerp(drag.m, nv, 0.08);
      drag.v = nv; lastY = e.clientY;
      if (reduced) draw();
    });
    const up = () => { if (drag) { drag.vel = clamp(drag.vel, -0.03, 0.03); drag = null; } };
    cv.addEventListener('pointerup', up);
    cv.addEventListener('pointercancel', up);

    resize();
    window.addEventListener('resize', () => { resize(); draw(); });
    draw();
    if ('IntersectionObserver' in window) {
      new IntersectionObserver((es) => {
        visible = es[0].isIntersecting;
        if (visible && !raf) { last = 0; raf = requestAnimationFrame(loop); }
      }).observe(cv);
    }
  }

  /* ------------------------------------------------------------ the digit bird */
  /* ------------------------------------------------------------ reveals */
  function initReveal() {
    const els = $$('.reveal');
    if (reduced || !('IntersectionObserver' in window)) { els.forEach((e) => e.classList.add('in')); return; }
    const io = new IntersectionObserver((es) => {
      es.forEach((e) => { if (e.isIntersecting) { e.target.classList.add('in'); io.unobserve(e.target); } });
    }, { threshold: 0.12, rootMargin: '0px 0px -40px 0px' });
    els.forEach((e) => io.observe(e));
  }

  /* ------------------------------------------------------------ node ticker tapes */
  const CAT_IC = {"Source": "<circle cx=\"6.5\" cy=\"10\" r=\"1.9\" fill=\"currentColor\" stroke=\"none\"/><path d=\"M10.5 6.5a5 5 0 0 1 0 7M13.5 4a8.6 8.6 0 0 1 0 12\"/>", "3D": "<path d=\"M10 3.2 16.2 6.7V13.4L10 16.9 3.8 13.4V6.7Z\"/><path d=\"M3.8 6.7 10 10.05 16.2 6.7M10 10.05V16.9\"/>", "Compositing": "<rect x=\"3.5\" y=\"3.5\" width=\"9\" height=\"9\" rx=\"2\"/><rect x=\"7.5\" y=\"7.5\" width=\"9\" height=\"9\" rx=\"2\"/>", "Effects": "<path d=\"M10 3 11.7 8.3 17 10 11.7 11.7 10 17 8.3 11.7 3 10 8.3 8.3Z\"/>", "Modulators": "<path d=\"M3.2 10c1.8-5.5 3.2-5.5 4.4 0s2.8 5.5 4.4 0 3.2-5.5 4.4 0\"/>", "Prediction": "<path d=\"M3.5 14.5c2.5 0 3-4 5.5-4.5\"/><path d=\"M11.5 8.4 13 7.8\" stroke-dasharray=\"0.1 3\"/><circle cx=\"15.9\" cy=\"6.2\" r=\"1.7\" fill=\"currentColor\" stroke=\"none\"/>", "Macros": "<circle cx=\"10\" cy=\"10\" r=\"6.5\"/><path d=\"M10 10 13 5.6\"/>", "Utility": "<path d=\"M3.5 6h7M14.5 6h2M3.5 14h2M9.5 14h7\"/><circle cx=\"12.5\" cy=\"6\" r=\"1.9\" fill=\"currentColor\" stroke=\"none\"/><circle cx=\"7.5\" cy=\"14\" r=\"1.9\" fill=\"currentColor\" stroke=\"none\"/>", "Notes": "<path d=\"M8.5 14.5V4.5l7-1.5v10\"/><circle cx=\"6.5\" cy=\"14.5\" r=\"2\" fill=\"currentColor\" stroke=\"none\"/><circle cx=\"13.5\" cy=\"13\" r=\"2\" fill=\"currentColor\" stroke=\"none\"/>", "Synths": "<rect x=\"3\" y=\"5\" width=\"14\" height=\"10\" rx=\"2\"/><path d=\"M7.7 5v10M12.3 5v10\"/><path d=\"M7.7 10h0M12.3 10h0\" stroke=\"none\"/>", "AudioEffects": "<path d=\"M4 8v4M7 5v10M10 3v14M13 6v8M16 8.5v3\"/>"};
  function openNode(name, c, opener) {
    const known = typeof ALL_NODES !== 'undefined' && ALL_NODES.find((n) => n.name.toLowerCase() === name.toLowerCase());
    const desc = known ? known.desc : `${name} is one of Infinite's ${catLabel(c)} nodes. Drop it on the canvas and patch it into anything; every control on it can be modulated. The Node Reference PDF lists its inputs and settings.`;
    if (typeof openNodeModal === 'function') openNodeModal(name, catLabel(c), desc);
    const badge = $('#modal-cat');
    if (badge && CAT_IC[c]) badge.insertAdjacentHTML('afterbegin', '<svg class="ic" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">' + CAT_IC[c] + '</svg>');
    const close = $('#modal-close-btn');
    if (close) close.focus();
    lastOpener = opener;
  }

  function initTickers() {
    const wrap = $('#tickers');
    const count = $('#node-count');
    if (!wrap || typeof NODE_LIBRARY === 'undefined') return;
    if (count) count.textContent = NODE_LIBRARY.length;
    // one tape per family of the canvas: visuals, sound, control
    const TAPES = [
      ['ticker-1', ['Source', 'Compositing', 'Effects', '3D']],
      ['ticker-2', ['Synths', 'AudioEffects', 'Notes']],
      ['ticker-3', ['Modulators', 'Prediction', 'Macros', 'Utility']]
    ];
    TAPES.forEach(([id, cats], ti) => {
      const track = document.getElementById(id);
      if (!track) return;
      // interleave families so colours alternate along the tape
      const byCat = cats.map((c) => NODE_LIBRARY.filter((n) => n[1] === c));
      const list = [];
      for (let k = 0; list.length < byCat.reduce((a, b) => a + b.length, 0); k++) byCat.forEach((arr) => { if (arr[k]) list.push(arr[k]); });
      const frag = document.createDocumentFragment();
      [0, 1].forEach((copy) => list.forEach(([n, c]) => {
        const b = document.createElement('button');
        b.type = 'button';
        b.className = 'ticker-pill';
        b.style.setProperty('--cat', CAT[c] || '#A3A9BA');
        b.dataset.cat = c;
        if (copy) { b.tabIndex = -1; b.setAttribute('aria-hidden', 'true'); }
        b.innerHTML = '<span></span>';
        b.querySelector('span').textContent = n;
        frag.appendChild(b);
      }));
      track.appendChild(frag);
      track.style.setProperty('--dur', `${Math.round(list.length * (1.5 + ti * 0.2))}s`);
    });
    // animate only while the tapes are on screen
    if ('IntersectionObserver' in window) {
      wrap.classList.add('is-off');
      new IntersectionObserver((es) => es.forEach((en) => wrap.classList.toggle('is-off', !en.isIntersecting)), { rootMargin: '80px' }).observe(wrap);
    }
    wrap.addEventListener('click', (e) => {
      const b = e.target.closest('.ticker-pill');
      if (b) openNode(b.textContent, b.dataset.cat, b);
    });
  }

  /* ------------------------------------------------------------ modal keyboard */
  let lastOpener = null;
  function initModalKeys() {
    const modal = $('#node-modal');
    if (!modal) return;
    const close = () => {
      if (!modal.classList.contains('active')) return;
      modal.classList.remove('active');
      modal.setAttribute('aria-hidden', 'true');
      if (lastOpener) lastOpener.focus();
    };
    document.addEventListener('keydown', (e) => { if (e.key === 'Escape') close(); });
  }

  /* ------------------------------------------------------------ setup tabs */
  function initTabs() {
    const list = $('.tabs[role="tablist"]');
    if (!list) return;
    const tabs = $$('[role="tab"]', list);
    const pill = $('.tab-pill', list);
    const movePill = (t) => {
      if (!pill) return;
      pill.style.width = t.offsetWidth + 'px';
      pill.style.transform = `translateX(${t.offsetLeft}px)`;
    };
    const select = (t, focus) => {
      tabs.forEach((x) => {
        const on = x === t;
        x.setAttribute('aria-selected', String(on));
        x.tabIndex = on ? 0 : -1;
        const p = document.getElementById(x.getAttribute('aria-controls'));
        if (p) p.hidden = !on;
      });
      movePill(t);
      if (focus) t.focus();
    };
    tabs.forEach((t, i) => {
      t.addEventListener('click', () => select(t));
      t.addEventListener('keydown', (e) => {
        let j = null;
        if (e.key === 'ArrowRight') j = (i + 1) % tabs.length;
        if (e.key === 'ArrowLeft') j = (i - 1 + tabs.length) % tabs.length;
        if (e.key === 'Home') j = 0;
        if (e.key === 'End') j = tabs.length - 1;
        if (j !== null) { e.preventDefault(); select(tabs[j], true); }
      });
    });
    const initial = tabs.find((t) => t.dataset.os === OS) || tabs[0];
    pill && (pill.style.transition = 'none');
    select(initial);
    requestAnimationFrame(() => { if (pill) pill.style.transition = ''; });
    window.addEventListener('resize', () => movePill(tabs.find((t) => t.getAttribute('aria-selected') === 'true')));
    if (document.fonts && document.fonts.ready) document.fonts.ready.then(() => movePill(tabs.find((t) => t.getAttribute('aria-selected') === 'true')));
  }

  /* ------------------------------------------------------------ YouTube click-to-load */
  function initYouTube() {
    document.querySelectorAll('.yt-embed[data-id]').forEach((box) => {
      const btn = $('.yt-poster', box);
      if (!btn) return;
      btn.addEventListener('click', () => {
        const f = document.createElement('iframe');
        f.src = `https://www.youtube-nocookie.com/embed/${box.dataset.id}?autoplay=1&rel=0`;
        f.title = box.dataset.title || 'Getting started with Infinite';
        f.allow = 'accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share';
        f.allowFullscreen = true;
        box.innerHTML = '';
        box.appendChild(f);
      });
    });
  }

  /* ------------------------------------------------------------ listen: time */
  function initWaveTime() {
    const audio = $('#m-audio-element');
    const out = $('#wave-time');
    if (!audio || !out) return;
    const fmt = (s) => (isFinite(s) ? `${Math.floor(s / 60)}:${String(Math.floor(s % 60)).padStart(2, '0')}` : '0:00');
    const upd = () => { out.textContent = audio.duration ? `${fmt(audio.currentTime)} / ${fmt(audio.duration)}` : fmt(audio.currentTime); };
    ['timeupdate', 'loadedmetadata', 'seeked', 'ended'].forEach((ev) => audio.addEventListener(ev, upd));
    upd();
  }

  /* ------------------------------------------------------------ boot */
  const boot = () => {
    initDownloads();
    initHeader();
    initDock();
    initLogo();
    initHeroWord();
    initHud();
    initFilm();
    initChain();
    initAside();
    initPredictKnobs();
    initReveal();
    initTickers();
    initModalKeys();
    initTabs();
    initYouTube();
    initWaveTime();
  };
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot);
  else boot();
})();
