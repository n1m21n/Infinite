/* Programmed field: the brand signature (BRAND.md section 16, brand.json lineage.signature).
   A grid of unit marks (dot, ring, square, cross, plus, a 5x7 digit) chosen and sized by a signal: a seeded noise field
   plus the pointer. Signal on the mode ground, one Ember cell (the look-here). Cells change on the beat (a 1/16 note at
   120 BPM, 125 ms) and never faster; the same seed gives the same frame. Still under reduced motion; paused offscreen.
     <section class="pfield" data-seed="7" data-cell="16" data-strength="0.06"> ... </section>
   data-strength: 0.06 for texture behind text (the 6% rule), 1 for a hero field. */
(function () {
  var reduced = matchMedia('(prefers-reduced-motion: reduce)').matches;
  var STEP = 125;  // 1/16 note at 120 BPM
  var DIGITS = ['01110100011001110101110011000101110', '00100011000010000100001000010001110', '01110100010000100010001000100011111',
    '11111000100010000010000011000101110', '00010001100101010010111110001000010', '11111100001111000001000011000101110',
    '00110010001000011110100011000101110', '11111000010001000100010000100001000', '01110100011000101110100011000101110',
    '01110100011000101111000010001001100'];

  function rng(seed) { return function () { seed = (seed * 1664525 + 1013904223) >>> 0; return seed / 4294967296; }; }
  function css(el, name, fb) { return getComputedStyle(el).getPropertyValue(name).trim() || fb; }

  function Field(host) {
    var cv = document.createElement('canvas');
    cv.className = 'pfield-canvas'; cv.setAttribute('aria-hidden', 'true');
    host.prepend(cv);
    var ctx = cv.getContext('2d'), cell = +host.dataset.cell || 16, seed = +host.dataset.seed || 1;
    var strength = host.dataset.strength ? +host.dataset.strength : 0.06;
    var px = -1e4, py = -1e4, t = 0, last = 0, raf = 0, visible = false, W = 0, H = 0, cols = 0, rows = 0, ember = -1;
    var r = rng(seed), phase = [];

    function size() {
      var b = host.getBoundingClientRect(), d = Math.min(devicePixelRatio || 1, 2);
      W = b.width; H = b.height; cv.width = W * d; cv.height = H * d; ctx.setTransform(d, 0, 0, d, 0, 0);
      cols = Math.ceil(W / cell); rows = Math.ceil(H / cell);
      r = rng(seed); phase = [];
      for (var i = 0; i < cols * rows; i++) phase.push(r() * Math.PI * 2);
      ember = Math.floor(r() * cols * rows);
      draw();
    }

    function mark(kind, x, y, s, col) {
      ctx.fillStyle = col; ctx.strokeStyle = col; ctx.lineWidth = Math.max(1, s * 0.18);
      var h = s / 2;
      if (kind === 0) { ctx.beginPath(); ctx.arc(x, y, h * 0.5, 0, 6.283); ctx.fill(); }
      else if (kind === 1) { ctx.beginPath(); ctx.arc(x, y, h * 0.7, 0, 6.283); ctx.stroke(); }
      else if (kind === 2) { ctx.fillRect(x - h * 0.6, y - h * 0.6, h * 1.2, h * 1.2); }
      else if (kind === 3) { ctx.beginPath(); ctx.moveTo(x - h * 0.6, y - h * 0.6); ctx.lineTo(x + h * 0.6, y + h * 0.6); ctx.moveTo(x + h * 0.6, y - h * 0.6); ctx.lineTo(x - h * 0.6, y + h * 0.6); ctx.stroke(); }
      else if (kind === 4) { ctx.beginPath(); ctx.moveTo(x - h * 0.7, y); ctx.lineTo(x + h * 0.7, y); ctx.moveTo(x, y - h * 0.7); ctx.lineTo(x, y + h * 0.7); ctx.stroke(); }
      else {  // a 5x7 digit
        var g = DIGITS[kind - 5], u = s / 8;
        for (var k = 0; k < 35; k++) if (g[k] === '1') ctx.fillRect(x - 2.5 * u + (k % 5) * u, y - 3.5 * u + Math.floor(k / 5) * u, u * 0.9, u * 0.9);
      }
    }

    function draw() {
      ctx.clearRect(0, 0, W, H);
      var sig = css(host, '--signal', '#A7B1FF'), emb = css(host, '--accent', '#F57F66');
      ctx.globalAlpha = Math.min(1, strength);
      for (var j = 0; j < rows; j++) for (var i = 0; i < cols; i++) {
        var n = j * cols + i, x = i * cell + cell / 2, y = j * cell + cell / 2;
        // the signal: slow seeded waves (phase law: every cell its own phase) plus a bump around the pointer
        var v = 0.5 + 0.25 * Math.sin(phase[n] + t * 0.9) + 0.25 * Math.sin(i * 0.31 + j * 0.17 + t * 0.6);
        var dp = Math.hypot(x - px, y - py); v += Math.max(0, 1 - dp / (cell * 6)) * 0.8;
        v = Math.max(0, Math.min(1.2, v));
        var kind = v > 1 ? 5 + (n + Math.floor(t)) % 10 : Math.min(4, Math.floor(v * 5));
        mark(kind, x, y, cell * (0.35 + 0.55 * Math.min(1, v)), n === ember ? emb : sig);
      }
      ctx.globalAlpha = 1;
    }

    function loop(now) {
      raf = requestAnimationFrame(loop);
      if (now - last < STEP) return;  // quantised to the beat
      last = now; t += STEP / 1000; draw();
    }
    function play() { if (!raf && visible && !reduced && !document.hidden) raf = requestAnimationFrame(loop); }
    function pause() { cancelAnimationFrame(raf); raf = 0; }

    new ResizeObserver(size).observe(host);
    new IntersectionObserver(function (e) { visible = e[0].isIntersecting; visible ? play() : pause(); }).observe(host);
    document.addEventListener('visibilitychange', function () { document.hidden ? pause() : play(); });
    if (!reduced) host.addEventListener('pointermove', function (e) { var b = host.getBoundingClientRect(); px = e.clientX - b.left; py = e.clientY - b.top; });
    host.addEventListener('pointerleave', function () { px = py = -1e4; });
    new MutationObserver(draw).observe(document.documentElement, { attributes: true, attributeFilter: ['data-mode'] });
  }

  function init() { document.querySelectorAll('.pfield').forEach(Field); }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init); else init();
})();
