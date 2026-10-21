/* The digit bird: a small bird made of 0s and 1s that perches on the headings and flies between them. One script, every page.
   It makes its own canvas, picks its perches (.perch slabs, else main h2 and the footer line), and takes its colours from the mode. */
(function () {
  'use strict';
  const $ = (s, r = document) => r.querySelector(s);
  const $$ = (s, r = document) => Array.from(r.querySelectorAll(s));
  const clamp = (x, a, b) => Math.min(b, Math.max(a, x));
  const lerp = (a, b, t) => a + (b - a) * t;
  const smooth = (t) => t * t * (3 - 2 * t);
  const reduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  const DPR = Math.min(window.devicePixelRatio || 1, 2);
  const st = document.createElement('style');
  st.textContent = '#bird{position:absolute;left:0;top:0;z-index:60;width:68px;height:52px;pointer-events:none;opacity:0;transition:opacity 333ms ease;-webkit-tap-highlight-color:transparent}#bird.ready{opacity:1}html.over-bird body{cursor:pointer}';
  document.head.appendChild(st);
  function initBird() {
    let cv = $('#bird');
    if (!cv) { cv = document.createElement('canvas'); cv.id = 'bird'; cv.setAttribute('aria-hidden', 'true'); cv.title = 'hello'; document.body.appendChild(cv); }
    /* the slabs of the home page, else the section headings of any other page, and the footer line */
    const perches = $$('.perch').concat($$('main h2, .aud-section h2, .footer-line, main h1')).filter((el, i, a) => a.indexOf(el) === i && !el.closest('[hidden], .deck-card, .lib-card, .post-card, .ax-bento, .ax-shot, .nd-card, .reel-card, .ph-stage, [class*="-card"]'));
    if (!perches.length) return;
    const g = cv.getContext('2d');
    const COLS = 34, ROWS = 24, X0 = -1.25, X1 = 1.25, Y0 = -0.95, Y1 = 0.85, FX = 0.08, FY = 0.80;
    // the bird takes its colours from the active mode (brand roles --ink and --accent-ink)
    let INK = '#12152A', EYE = '#D9430E';
    const roles = () => {
      const cs = getComputedStyle(document.documentElement);
      INK = cs.getPropertyValue('--ink').trim() || INK; EYE = cs.getPropertyValue('--accent-ink').trim() || EYE;
    };

    const inEll = (px, py, cx, cy, rx, ry, rot) => {
      const c = Math.cos(rot), s = Math.sin(rot), dx = px - cx, dy = py - cy;
      const u = dx * c + dy * s, v = -dx * s + dy * c;
      return (u / rx) ** 2 + (v / ry) ** 2;
    };
    const inTri = (px, py, a, b, c) => {
      const sg = (p1, p2, p3) => (p1[0] - p3[0]) * (p2[1] - p3[1]) - (p2[0] - p3[0]) * (p1[1] - p3[1]);
      const p = [px, py], d1 = sg(p, a, b), d2 = sg(p, b, c), d3 = sg(p, c, a);
      return !((d1 < 0 || d2 < 0 || d3 < 0) && (d1 > 0 || d2 > 0 || d3 > 0));
    };
    const segD = (px, py, a, b) => {
      const vx = b[0] - a[0], vy = b[1] - a[1];
      const u = clamp(((px - a[0]) * vx + (py - a[1]) * vy) / (vx * vx + vy * vy), 0, 1);
      return Math.hypot(px - a[0] - u * vx, py - a[1] - u * vy);
    };
    const cache = new Map();
    const cells = (wingRot, perched) => {
      const key = perched ? 'p' : wingRot.toFixed(2);
      if (cache.has(key)) return cache.get(key);
      const out = [];
      const sx = (X1 - X0) / COLS, sy = (Y1 - Y0) / ROWS;
      const d = [-Math.cos(wingRot), -Math.sin(wingRot)];
      const wc = [d[0] * 0.42, -0.05 + d[1] * 0.42], wr = Math.atan2(d[1], d[0]);
      for (let j = 0; j < ROWS; j++) for (let i = 0; i < COLS; i++) {
        const px = X0 + (i + 0.5) * sx, py = Y0 + (j + 0.5) * sy;
        const body = inEll(px, py, -0.1, 0.2, 0.64, 0.42, -0.18);
        const head = ((px - 0.46) ** 2 + (py + 0.4) ** 2) / 0.34 ** 2;
        const tail = inTri(px, py, [-0.55, 0.08], [-1.22, -0.12], [-1.2, 0.1]) || inTri(px, py, [-0.55, 0.08], [-1.2, 0.1], [-0.5, 0.4]);
        const beak = inTri(px, py, [0.76, -0.52], [1.16, -0.4], [0.76, -0.28]);
        const wing = perched ? inEll(px, py, -0.2, 0.06, 0.5, 0.2, -0.3) < 1 : inEll(px, py, wc[0], wc[1], 0.52, 0.17, wr) < 1;
        const leg = perched && (segD(px, py, [-0.02, 0.52], [-0.02, 0.8]) < 0.05 || segD(px, py, [0.2, 0.5], [0.2, 0.8]) < 0.05);
        const eye = (px - 0.56) ** 2 + (py + 0.47) ** 2 < 0.075 ** 2;
        const inside = body < 1 || head < 1 || tail;
        let kind = null;
        if (eye) kind = 'eye';
        else if (beak) kind = 'beak';
        else if (wing) kind = 'wing';
        else if (inside) kind = ((body > 0.74 && head > 1) || (head > 0.6 && head < 1 && body > 0.8) || (tail && body > 1)) ? 'edge' : 'body';
        else if (leg) kind = 'leg';
        if (kind) out.push([i, j, kind]);
      }
      cache.set(key, out);
      return out;
    };

    // canvas box: bird width S, extra head-room for the wing and the hop squash
    let S = 0, CW = 0, CH = 0, PAD = 0;
    const layout = () => {
      S = window.innerWidth < 640 ? 84 : 112;
      PAD = Math.round(S * 0.3);
      CW = S + PAD * 2;
      CH = Math.round(S * 0.75) + PAD * 2;
      cv.style.width = CW + 'px'; cv.style.height = CH + 'px';
      cv.width = Math.round(CW * DPR); cv.height = Math.round(CH * DPR);
    };

    const draw = (tick, flap, face, squash, tilt) => {
      g.setTransform(DPR, 0, 0, DPR, 0, 0);
      g.clearRect(0, 0, CW, CH);
      const perched = flap === null;
      const wingRot = perched ? 0 : 0.4 + 0.8 * Math.sin(2 * Math.PI * flap);
      const cl = cells(Math.round(wingRot * 100) / 100, perched);
      const cw = S / COLS;
      const ch = cw * ((Y1 - Y0) / ROWS) / ((X1 - X0) / COLS);
      const footX = CW / 2, footY = CH - PAD * 0.5;
      g.save();
      g.translate(footX, footY);
      g.rotate(tilt);
      g.scale(1 + squash * 0.25, 1 - squash * 0.3);
      const ox = -((FX - X0) / (X1 - X0)) * S;
      const oy = -((FY - Y0) / (Y1 - Y0)) * ch * ROWS;
      g.font = `620 ${(cw * 1.18).toFixed(2)}px 'Geist Mono', ui-monospace, monospace`;
      g.textAlign = 'center';
      g.textBaseline = 'middle';
      roles();
      for (const [i, j, kind] of cl) {
        const h = ((i * 73856093) ^ (j * 19349663) ^ (tick * 83492791)) & 0xFFFF;
        let ch1 = '1', a = kind === 'leg' ? 0.8 : 1;
        if (kind === 'body') { ch1 = h % 7 === 0 ? '1' : '0'; a = ch1 === '1' ? 0.55 : 0.3; }
        g.globalAlpha = a;
        g.fillStyle = kind === 'eye' ? EYE : INK;
        g.fillText(ch1, (ox + (i + 0.5) * cw) * face, oy + (j + 0.5) * ch);
      }
      g.restore();
      g.globalAlpha = 1;
    };

    // state
    let cur = null;          // current perch element
    let pos = { x: 0, y: 0 };  // feet point in page coords
    let face = 1, flight = null, hop = null, lastTick = -1, raf = 0;

    // the bird sits in the far corner of a heading's row (the right edge of the column), never mid-content
    const perchPoint = (el) => {
      const r = el.getBoundingClientRect();
      const x = Math.min(r.right - 14, document.documentElement.clientWidth - 40);
      return { x: x + window.scrollX, y: r.top + window.scrollY + 1 };
    };
    const place = () => {
      cv.style.transform = `translate3d(${Math.max(0, Math.min(pos.x - CW / 2, window.scrollX + document.documentElement.clientWidth - CW)).toFixed(1)}px, ${(pos.y - (CH - PAD * 0.5)).toFixed(1)}px, 0)`;
    };
    const flyTo = (el) => {
      const p1 = perchPoint(el);
      if (reduced || !cur) { cur = el; pos = p1; place(); kick(); return; }
      cur = el;
      const dist = Math.hypot(p1.x - pos.x, p1.y - pos.y);
      if (dist < 4) return;
      face = p1.x >= pos.x ? 1 : -1;
      flight = { t0: performance.now(), dur: clamp(dist / 1.4, 650, 1300), p0: { ...pos }, p1, h: clamp(dist * 0.28, 50, 180) };
      kick();
    };

    let audio = null;
    const chirp = () => {
      try {
        audio = audio || new (window.AudioContext || window.webkitAudioContext)();
        const t = audio.currentTime;
        const o = audio.createOscillator(), m = audio.createOscillator(), mg = audio.createGain(), gn = audio.createGain();
        o.type = 'sine'; m.type = 'sine';
        o.frequency.setValueAtTime(2800, t); o.frequency.exponentialRampToValueAtTime(4200, t + 0.07);
        m.frequency.value = 58; mg.gain.value = 180;
        m.connect(mg); mg.connect(o.frequency);
        gn.gain.setValueAtTime(0.0001, t); gn.gain.exponentialRampToValueAtTime(0.05, t + 0.01); gn.gain.exponentialRampToValueAtTime(0.0001, t + 0.12);
        o.connect(gn); gn.connect(audio.destination);
        o.start(t); m.start(t); o.stop(t + 0.14); m.stop(t + 0.14);
      } catch (e) { /* no audio, no chirp */ }
    };
    // the canvas never takes clicks: buttons and links under it always win.
    // a tap on empty space over the bird's body still makes it chirp.
    const HIT = 'a, button, input, select, textarea, label, summary, video, [role="button"], [tabindex]';
    const onBird = (x, y) => {
      if (!cur) return false;
      const fx = pos.x - window.scrollX, fy = pos.y - window.scrollY;
      return Math.abs(x - fx) < S * 0.42 && y < fy - 2 && y > fy - S * 0.7;
    };
    document.addEventListener('click', (e) => {
      if (!onBird(e.clientX, e.clientY) || (e.target.closest && e.target.closest(HIT))) return;
      chirp();
      if (!reduced && !flight) { hop = { t0: performance.now() }; kick(); }
    });
    if (window.matchMedia('(pointer: fine)').matches) {
      document.addEventListener('mousemove', (e) => {
        const over = onBird(e.clientX, e.clientY) && !(e.target.closest && e.target.closest(HIT));
        document.documentElement.classList.toggle('over-bird', over);
      }, { passive: true });
    }

    const BEAT = 60 / 120 * 1000;
    const frame = (now) => {
      raf = 0;
      let flap = null, squash = 0, tilt = 0, busy = false;
      if (flight) {
        const u = clamp((now - flight.t0) / flight.dur, 0, 1);
        const e = smooth(u);
        pos = {
          x: lerp(flight.p0.x, flight.p1.x, e),
          y: lerp(flight.p0.y, flight.p1.y, e) - flight.h * 4 * e * (1 - e)
        };
        flap = (now - flight.t0) / 1000 * 7;
        tilt = (0.5 - e) * 0.25 * face;
        busy = true;
        if (u >= 1) { flight = null; hop = { t0: now, land: true }; pos = perchPoint(cur); }
      }
      if (hop && !flight) {
        const d = (now - hop.t0) / 1000;
        if (!hop.land) {
          // a tiny hop in place when tapped
          const u = clamp(d / 0.32, 0, 1);
          const base = perchPoint(cur);
          pos = { x: base.x, y: base.y - 22 * 4 * u * (1 - u) };
          if (u < 1) flap = d * 9;
          if (u >= 1) { hop = { t0: now, land: true }; }
        } else {
          squash = d < 0.5 ? Math.exp(-d * 10) * Math.sin(d * 30) * 0.5 : 0;
          if (d >= 0.5) hop = null;
        }
        busy = true;
      }
      const tick = reduced ? 0 : Math.floor(now / (BEAT / 4));
      if (busy || tick !== lastTick) {
        lastTick = tick;
        place();
        draw(tick, flap, face, squash, tilt);
      }
      // breathing runs at 1/16 note only while the bird is on screen
      const r = cv.getBoundingClientRect();
      const onScreen = r.bottom > 0 && r.top < window.innerHeight;
      if (busy) raf = requestAnimationFrame(frame);
      else if (onScreen && !reduced) setTimeout(() => { if (!raf) raf = requestAnimationFrame(frame); }, BEAT / 4);
    };
    function kick() { if (!raf) raf = requestAnimationFrame(frame); }

    // pick the perch nearest 40% of the viewport among those in view
    const choose = () => {
      const vh = window.innerHeight;
      let best = null, bd = Infinity;
      perches.forEach((el) => {
        const r = el.getBoundingClientRect();
        if (r.top < vh * 0.08 || r.top > vh * 0.9 || r.width === 0) return;
        const dd = Math.abs(r.top - vh * 0.4);
        if (dd < bd) { bd = dd; best = el; }
      });
      if (best && best !== cur) flyTo(best);
      else if (!flight) kick();
    };
    let sch = 0;
    window.addEventListener('scroll', () => { clearTimeout(sch); sch = setTimeout(choose, 140); }, { passive: true });
    window.addEventListener('resize', () => { layout(); if (cur && !flight) { pos = perchPoint(cur); place(); } lastTick = -1; kick(); });

    layout();
    const start = () => {
      cur = null;
      flyTo(perches[0]);
      cv.classList.add('ready');
      lastTick = -1; kick();
      choose();
    };
    if (document.fonts && document.fonts.ready) document.fonts.ready.then(start); else start();
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', initBird); else initBird();
})();
