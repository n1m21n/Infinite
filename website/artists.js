/* Artists page: a live blob you steer with the pointer or a finger, three bubbles that change what it is made of,
   and a swipe strip that lifts the card in the middle. Plain WebGL, no libraries. Reduced motion draws one still frame. */
(function () {
  'use strict';
  var still = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  var stage = document.querySelector('.ax-stage');
  if (!stage) return;
  var canvas = stage.querySelector('.ax-gl');
  var hero = stage.getAttribute('data-hero') || 'blob';   // 'cables' hands the canvas to musicians.js and only shares the bubbles and strips
  var caps = (stage.getAttribute('data-caps') || '32 blend modes|3D with real materials|A picture from a formula').split('|');

  var FRAG = '#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n' +
    'uniform vec2 u_res; uniform float u_t; uniform vec2 u_m; uniform float u_p; uniform float u_k; uniform float u_scroll; uniform vec3 u_view; uniform float u_lt; uniform vec3 u_bg;\n' +
    'const vec3 DEEP = vec3(0.051, 0.063, 0.125); const vec3 GROUND = vec3(0.082, 0.098, 0.188); const vec3 SIG5 = vec3(0.412, 0.455, 0.863); const vec3 SIG = vec3(0.655, 0.694, 1.0);\n' +
    'float field(vec2 p, vec2 c, float t) {\n' +
    ' float f = 0.0; float pr = 0.15 * (1.0 + 0.4 * u_p); f += pr * pr / (dot(p - c, p - c) + 0.0008);\n' +
    ' for (int i = 0; i < 5; i++) { float fi = float(i); float a = t * (0.32 + 0.07 * fi) + fi * 1.9; float rr = 0.2 + 0.055 * fi + 0.05 * sin(t * 0.5 + fi);\n' +
    '  vec2 q = vec2(cos(a), sin(a * 1.13)) * rr * vec2(1.25, 1.0); float r0 = 0.105 - 0.009 * fi; f += r0 * r0 / (dot(p - q, p - q) + 0.0008); }\n' +
    ' return f; }\n' +
    'float height(float f) { return f / (f + 3.0); }\n' +
    'void main() {\n' +
    ' vec2 res = u_res; vec2 uv = (gl_FragCoord.xy - 0.5 * res) / res.y; float asp = res.x / res.y; float t = u_t;\n' +
    ' vec2 c = (u_m - 0.5) * vec2(1.0, 0.8) * 0.3; vec2 p = (uv - u_view.xy) / u_view.z;\n' +
    ' float e = 1.2 / res.y; float f = field(p, c, t);\n' +
    ' float hx = height(field(p + vec2(e, 0.0), c, t)) - height(field(p - vec2(e, 0.0), c, t));\n' +
    ' float hy = height(field(p + vec2(0.0, e), c, t)) - height(field(p - vec2(0.0, e), c, t));\n' +
    ' vec3 n = normalize(vec3(-hx, -hy, 2.0 * e * 0.5));\n' +
    ' float inside = smoothstep(0.985, 1.04, f);\n' +
    ' float fres = pow(max(1.0 - n.z, 0.0001), 1.6);\n' +
    ' float ph = fres * 1.1 + n.x * 0.35 + t * 0.04 + u_scroll * 0.6;\n' +
    ' vec3 film = 0.5 + 0.5 * cos(6.2832 * (vec3(0.0, 0.33, 0.67) + ph));\n' +
    ' film = mix(vec3(0.80, 0.83, 1.0), film, 0.42);\n' +
    ' float kA = clamp(1.0 - abs(u_k), 0.0, 1.0), kB = clamp(1.0 - abs(u_k - 1.0), 0.0, 1.0), kC = clamp(1.0 - abs(u_k - 2.0), 0.0, 1.0);\n' +
    ' /* layers: translucent contours, like picture stacked on picture */\n' +
    ' float band = 0.5 + 0.5 * sin(height(f) * 14.0 - t * 0.6);\n' +
    ' vec3 cA = film * (0.82 + 0.22 * band); cA = mix(cA, SIG, 0.18 * (1.0 - band));\n' +
    ' /* solid: lit, glossy, with a window reflection */\n' +
    ' vec3 L = normalize(vec3(-0.45, 0.6, 0.65)); float dif = clamp(dot(n, L), 0.0, 1.0); vec3 H = normalize(L + vec3(0.0, 0.0, 1.0)); float sp = pow(clamp(dot(n, H), 0.0, 1.0), 48.0);\n' +
    ' vec3 cB = mix(SIG5 * 0.55, film, 0.6) * (0.35 + 0.9 * dif) + vec3(1.0) * sp * 0.95 + film * fres * 0.5;\n' +
    ' /* formula: the same blob drawn as a field of dots */\n' +
    ' vec2 g = p * 46.0; vec2 gi = fract(g) - 0.5; float dotr = smoothstep(0.46, 0.18, length(gi) / (0.25 + 0.75 * height(f)));\n' +
    ' vec3 cC = mix(DEEP, SIG, dotr) * (0.55 + 0.6 * height(f)) + film * 0.22 * dotr;\n' +
    ' vec3 body = cA * kA + cB * kB + cC * kC;\n' +
    ' vec3 bg = mix(DEEP, GROUND, smoothstep(-0.6, 0.7, uv.y + 0.2 * sin(uv.x * 2.0 + t * 0.1)));\n' +
    ' bg += SIG5 * 0.16 * smoothstep(0.15, 1.2, f) * (0.6 + 0.4 * sin(t * 0.4));\n' +
    ' bg *= 1.0 - 0.35 * dot(uv, uv);\n' +
    ' if (u_lt > 0.5) {\n' +
    /* light modes: the glossy object on a pale ground (refs: the soft 3D objects on light grounds), a soft contact shadow, the dots in ink */
    '  bg = u_bg * (1.0 - 0.05 * dot(uv, uv)) - vec3(0.09, 0.085, 0.04) * smoothstep(0.35, 1.0, f) * (1.0 - inside);\n' +
    '  cC = mix(u_bg, vec3(0.235, 0.275, 0.62), dotr * (0.5 + 0.6 * height(f)));\n' +
    '  body = cA * kA + cB * kB + cC * kC; }\n' +
    ' gl_FragColor = vec4(mix(bg, body, inside), 1.0);\n' +
    '}\n';

  var g = hero === 'blob' ? canvas.getContext('webgl', { antialias: false, alpha: false, powerPreference: 'low-power' }) : null;
  var st = { m: [0.5, 0.5], tm: [0.5, 0.5], p: 0, tp: 0, k: 0, tk: 0, last: 0, idle: 0, vis: true, scroll: 0 };
  var U = {};
  function compile(type, src) { var s = g.createShader(type); g.shaderSource(s, src); g.compileShader(s); if (!g.getShaderParameter(s, g.COMPILE_STATUS)) { if (window.console) console.warn(g.getShaderInfoLog(s)); return null; } return s; }
  function init() {
    if (!g) return false;
    var vs = compile(g.VERTEX_SHADER, 'attribute vec2 a; void main() { gl_Position = vec4(a, 0.0, 1.0); }'), fs = compile(g.FRAGMENT_SHADER, FRAG);
    if (!vs || !fs) return false;
    var pr = g.createProgram(); g.attachShader(pr, vs); g.attachShader(pr, fs); g.linkProgram(pr); g.useProgram(pr);
    var buf = g.createBuffer(); g.bindBuffer(g.ARRAY_BUFFER, buf); g.bufferData(g.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), g.STATIC_DRAW);
    var loc = g.getAttribLocation(pr, 'a'); g.enableVertexAttribArray(loc); g.vertexAttribPointer(loc, 2, g.FLOAT, false, 0, 0);
    ['u_res', 'u_t', 'u_m', 'u_p', 'u_k', 'u_scroll', 'u_view', 'u_lt', 'u_bg'].forEach(function (k) { U[k] = g.getUniformLocation(pr, k); });
    return true;
  }
  var ok = hero === 'blob' ? init() : false;
  if (ok && window.InfMode) window.InfMode.watch(function () { if (still) draw(performance.now()); });
  if (hero === 'blob' && !ok) stage.classList.add('no-gl');

  /* the blob lives in the free area: below the headline and above the bubbles on phones, right of the headline on desktop */
  var view = [0, 0, 0.8];
  function fit() {
    var W = stage.clientWidth, H = stage.clientHeight; if (!W || !H) return;
    var sr = stage.getBoundingClientRect(), copy = stage.querySelector('.ax-copy'), low = stage.querySelector('.ax-low'), cta = stage.querySelector('.ax-cta');
    var x0 = 12, x1 = W - 12, y0, y1;
    if (W >= 900) { x0 = W * 0.42; y0 = 64; y1 = (cta ? cta.getBoundingClientRect().top - sr.top : H) - 16; }
    else { y0 = (copy ? copy.getBoundingClientRect().bottom - sr.top : 0) + 8; y1 = (low ? low.getBoundingClientRect().top - sr.top : H) - 8; }
    y1 = Math.max(y1, y0 + 120);
    view[0] = ((x0 + x1) / 2 - W / 2) / H; view[1] = (H / 2 - (y0 + y1) / 2) / H;
    view[2] = Math.max(0.2, Math.min(((x1 - x0) / H) * 0.96 / 1.3, ((y1 - y0) / H) * 0.96 / 0.95));
    if (still && ok) draw(performance.now());
  }
  addEventListener('resize', fit); if (document.fonts && document.fonts.ready) document.fonts.ready.then(fit);
  function draw(now) {
    var dt = Math.min(0.05, (now - st.last) / 1000 || 0.016); st.last = now;
    var a = 1 - Math.exp(-dt * 5);                       // ease toward the target: the blob trails the finger like something with weight
    if (!still && st.idle > 2.2) { var tt = now / 1000; st.tm = [0.5 + 0.22 * Math.sin(tt * 0.45), 0.5 + 0.16 * Math.sin(tt * 0.31 + 1)]; }
    st.idle += dt;
    st.m[0] += (st.tm[0] - st.m[0]) * a; st.m[1] += (st.tm[1] - st.m[1]) * a;
    st.p += (st.tp - st.p) * (1 - Math.exp(-dt * 8)); st.k += (st.tk - st.k) * (1 - Math.exp(-dt * 6));
    var dpr = Math.min(window.devicePixelRatio || 1, 1.5), w = Math.max(2, Math.round(canvas.clientWidth * dpr)), h = Math.max(2, Math.round(canvas.clientHeight * dpr));
    if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; g.viewport(0, 0, w, h); }
    g.uniform2f(U.u_res, w, h); g.uniform1f(U.u_t, still ? 3.2 : now / 1000); g.uniform2f(U.u_m, st.m[0], st.m[1]);
    g.uniform1f(U.u_p, st.p); g.uniform1f(U.u_k, st.k); g.uniform1f(U.u_scroll, st.scroll);
    var md = window.InfMode ? window.InfMode.get() : { lt: 0, bg: [1, 1, 1] }; g.uniform1f(U.u_lt, md.lt); g.uniform3f(U.u_bg, md.bg[0], md.bg[1], md.bg[2]);
    g.uniform3f(U.u_view, view[0], view[1], view[2]);
    g.drawArrays(g.TRIANGLES, 0, 3);
  }
  function loop(now) { if (st.vis && !document.hidden) draw(now); requestAnimationFrame(loop); }
  fit();
  if (ok) {
    if (still) { draw(performance.now()); }
    else requestAnimationFrame(loop);
    if ('IntersectionObserver' in window) new IntersectionObserver(function (es) { st.vis = es[0].isIntersecting; }).observe(stage);
    canvas.addEventListener('webglcontextlost', function (e) { e.preventDefault(); stage.classList.add('no-gl'); }, false);
  }

  /* steer: pointer or a finger. Vertical swipes on a phone still scroll the page (touch-action: pan-y in the CSS). */
  function aim(e) { var r = canvas.getBoundingClientRect(); st.tm = [(e.clientX - r.left) / r.width, 1 - (e.clientY - r.top) / r.height]; st.idle = 0; stage.classList.add('touched'); if (still && ok) draw(performance.now()); }
  stage.addEventListener('pointermove', function (e) { aim(e); }, { passive: true });
  stage.addEventListener('pointerdown', function (e) { if (e.target.closest('button, a')) return; st.tp = 1; aim(e); }, { passive: true });
  addEventListener('pointerup', function () { st.tp = 0; }, { passive: true });
  addEventListener('pointercancel', function () { st.tp = 0; }, { passive: true });
  addEventListener('scroll', function () { st.scroll = Math.min(1, scrollY / 900); }, { passive: true });

  /* bubbles */
  var bubs = Array.prototype.slice.call(stage.querySelectorAll('.ax-bub')), cap = stage.querySelector('.ax-cap');
  function pick(k) {
    st.tk = k; stage.setAttribute('data-k', k); st.idle = 0;
    bubs.forEach(function (b, i) { var on = i === k; b.classList.toggle('on', on); b.setAttribute('aria-selected', on ? 'true' : 'false'); });
    if (cap) { cap.classList.remove('swap'); void cap.offsetWidth; cap.textContent = caps[k]; cap.classList.add('swap'); }
    stage.dispatchEvent(new CustomEvent('ax:pick', { detail: k }));
    if (still) { st.k = k; if (ok) draw(performance.now()); }
  }
  bubs.forEach(function (b, i) {
    b.addEventListener('click', function () { pick(i); });
    b.addEventListener('keydown', function (e) { var d = e.key === 'ArrowRight' ? 1 : e.key === 'ArrowLeft' ? -1 : 0; if (d) { var k = (i + d + bubs.length) % bubs.length; pick(k); bubs[k].focus(); } });
  });
  /* a horizontal flick on the hero changes the bubble too */
  var fx = null;
  stage.addEventListener('pointerdown', function (e) { if (e.pointerType === 'touch' && !e.target.closest('button, a')) fx = { x: e.clientX, y: e.clientY }; }, { passive: true });
  stage.addEventListener('pointerup', function (e) {
    if (!fx) return; var dx = e.clientX - fx.x, dy = e.clientY - fx.y; fx = null;
    if (Math.abs(dx) > 70 && Math.abs(dx) > Math.abs(dy) * 1.6) pick((Math.round(st.tk) + (dx < 0 ? 1 : 2)) % 3);
  }, { passive: true });

  /* swipe strips: the card in the middle lifts, nothing dims */
  document.querySelectorAll('.ax-nodes, .ax-reel').forEach(function (row) {
    if (still) return;
    var raf = 0;
    function upd() {
      raf = 0; var r = row.getBoundingClientRect(), mid = r.left + r.width / 2;
      Array.prototype.forEach.call(row.children, function (c) {
        var b = c.getBoundingClientRect(), d = Math.abs(b.left + b.width / 2 - mid) / (r.width * 0.6);
        c.style.setProperty('--f', Math.max(0, 1 - d).toFixed(3));
      });
    }
    function sched() { if (!raf) raf = requestAnimationFrame(upd); }
    row.addEventListener('scroll', sched, { passive: true }); addEventListener('resize', sched); upd();
  });
})();
/* glass sliders: --p is how far the lit part of the track runs */
(function () {
  var ranges = [].slice.call(document.querySelectorAll('.ax-lab input[type=range]')); if (!ranges.length) return;
  var last = ranges.map(function () { return null; });
  function sync(i) { var r = ranges[i], v = r.value + '|' + r.min + '|' + r.max; if (v === last[i]) return; last[i] = v; var lo = +r.min || 0, hi = +r.max || 100, p = hi > lo ? (r.value - lo) / (hi - lo) : 0; r.parentNode.style.setProperty('--p', (p * 100).toFixed(1) + '%'); }
  function tick() { for (var i = 0; i < ranges.length; i++) sync(i); requestAnimationFrame(tick); }
  requestAnimationFrame(tick);
})();
