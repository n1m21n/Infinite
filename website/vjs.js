/* VJs page hero: a "projector window" in the free area of the stage, three scenes (tunnel, kaleidoscope, LED grid) drawn as a glyph
   matrix: every cell a character picked by the scene's brightness, blocks of cells flipping to solid colour on the beat
   (refs: the ASCII colour-block matrix, the dot-matrix glyph field). Ink on paper in the light modes. Scenes run on a 120 BPM clock. Steer it with the pointer or a finger; press to punch. The bubbles (artists.js) send 'ax:pick'
   to switch scene. The beat is a smooth swell, never a hard strobe. Plain WebGL. Reduced motion draws one still frame. */
(function () {
  'use strict';
  var stage = document.querySelector('.ax-stage[data-hero="shader"]'); if (!stage) return;
  var canvas = stage.querySelector('.ax-gl');
  var still = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  var FRAG = '#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n' +
    'uniform vec2 u_res; uniform float u_t; uniform vec2 u_m; uniform float u_p; uniform float u_k; uniform vec4 u_win; uniform float u_lt; uniform vec3 u_bg; uniform sampler2D u_atlas;\n' +
    'const vec3 DEEP = vec3(0.051, 0.063, 0.125); const vec3 SIG5 = vec3(0.412, 0.455, 0.863); const vec3 SIG = vec3(0.655, 0.694, 1.0); const vec3 EMBER = vec3(1.0, 0.42, 0.208);\n' +
    'float hash(vec2 c) { return fract(sin(dot(c, vec2(127.1, 311.7))) * 43758.5453); }\n' +
    'float glyph(float i, vec2 f) { return texture2D(u_atlas, vec2((clamp(i, 0.0, 15.0) + clamp(f.x, 0.02, 0.98)) / 16.0, 1.0 - f.y)).a; }\n' +
    /* the brightness of the three scenes at one point: tunnel, kaleidoscope, LED grid */
    'float scene(vec2 q, float t, float b, vec2 m, float kA, float kB, float kC) {\n' +
    ' vec2 p = q - m * 0.5; float r = length(p) + 0.001; float a = atan(p.y, p.x); float z = 0.35 / r;\n' +
    ' float vA = smoothstep(0.2, 0.95, abs(sin(z * 4.0 - t * 2.0 + b * 1.5) * sin(a * 6.0 + z * 1.2 + t * 0.6 + m.x * 2.0))) * smoothstep(0.0, 0.35, r);\n' +
    ' vec2 k = q - m * 0.4; float ka = atan(k.y, k.x), kr = length(k); float seg = 6.2832 / 8.0; ka = abs(mod(ka, seg) - seg * 0.5);\n' +
    ' vec2 kp = kr * vec2(cos(ka), sin(ka)) * 3.2 + vec2(t * 0.25, 0.0); float f = sin(kp.x * 3.0 + sin(kp.y * 2.4 + t * 0.8)) + sin(kp.y * 3.2 + t * 0.6 + b);\n' +
    ' float vB = smoothstep(-1.0, 1.6, f);\n' +
    ' vec2 gq = q * 4.0 + m * 1.5; vec2 id = floor(gq); vec2 gf = fract(gq) - 0.5; float lv = 0.5 + 0.5 * sin(id.x * 0.9 + id.y * 1.3 + t * 2.0 + hash(id) * 6.0 + b * 3.0);\n' +
    ' float vC = smoothstep(0.5, 0.3, max(abs(gf.x), abs(gf.y)) / (0.2 + 0.35 * lv)) * (0.35 + 0.65 * lv);\n' +
    ' return vA * kA + vB * kB + vC * kC; }\n' +
    'void main() {\n' +
    ' vec2 fc = gl_FragCoord.xy; vec2 hf = u_win.zw; vec2 d = abs(fc - u_win.xy) - hf + vec2(u_win.w * 0.12);\n' +
    ' float sd = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - u_win.w * 0.12;\n' +
    ' float mask = 1.0 - smoothstep(-1.0, 1.0, sd);\n' +
    ' if (mask <= 0.0) { gl_FragColor = vec4(0.0); return; }\n' +
    ' float ch = max(11.0, u_win.w / 15.0); vec2 cs = vec2(ch * 0.62, ch); vec2 o = fc - (u_win.xy - u_win.zw);\n' +
    ' vec2 cid = floor(o / cs); vec2 f = fract(o / cs); vec2 q = ((u_win.xy - u_win.zw) + (cid + 0.5) * cs - u_win.xy) / u_win.w; float t = u_t;\n' +
    ' float bn = floor(t * 2.0); float b = pow(1.0 - fract(t * 2.0), 3.0); b = b * 0.8 + u_p * 0.6;\n' +
    ' q *= 1.0 - 0.12 * u_p; vec2 m = (u_m - 0.5) * vec2(1.4, 1.0);\n' +
    ' float kA = clamp(1.0 - abs(u_k), 0.0, 1.0), kB = clamp(1.0 - abs(u_k - 1.0), 0.0, 1.0), kC = clamp(1.0 - abs(u_k - 2.0), 0.0, 1.0);\n' +
    ' float l = clamp(scene(q, t, b, m, kA, kB, kC) * (0.85 + 0.15 * smoothstep(1.6, 0.2, length(q))), 0.0, 1.0);\n' +
    ' float gl = glyph(floor(l * 10.99), f);\n' +
    /* on each beat a few blocks of cells flip to solid colour with the type knocked out; one of them is Ember */
    ' vec2 bid = floor(cid / vec2(6.0, 2.0)); float blk = step(0.93 - 0.05 * u_p, hash(bid + vec2(bn * 0.731, bn * 0.177))) * step(0.12, l);\n' +
    ' vec2 nb = ceil(u_win.zw * 2.0 / (cs * vec2(6.0, 2.0))); vec2 eb = floor(vec2(hash(vec2(bn, 1.3)), hash(vec2(2.1, bn))) * nb); float emb = 1.0 - step(0.5, length(bid - eb));\n' +
    ' vec3 bg = DEEP, ink = SIG, blc = SIG5; if (u_lt > 0.5) { bg = u_bg; ink = vec3(0.235, 0.275, 0.62); blc = vec3(0.29, 0.33, 0.84); }\n' +
    ' blc = mix(blc, EMBER, emb); blk = max(blk, emb);\n' +
    ' vec3 col = mix(bg, mix(bg, ink, 0.3 + 0.7 * l), gl); col = mix(col, mix(blc, bg, gl), blk);\n' +
    ' gl_FragColor = vec4(col * mask, mask);\n' +
    '}\n';
  var g = canvas.getContext('webgl', { antialias: false, alpha: true, premultipliedAlpha: true, powerPreference: 'low-power' });
  var st = { m: [0.5, 0.5], tm: [0.5, 0.5], p: 0, tp: 0, k: 0, tk: 0, last: 0, idle: 0, vis: true };
  var U = {}, win = [0, 0, 100, 100];
  function compile(type, src) { var s = g.createShader(type); g.shaderSource(s, src); g.compileShader(s); if (!g.getShaderParameter(s, g.COMPILE_STATUS)) { if (window.console) console.warn(g.getShaderInfoLog(s)); return null; } return s; }
  function init() {
    if (!g) return false;
    var vs = compile(g.VERTEX_SHADER, 'attribute vec2 a; void main() { gl_Position = vec4(a, 0.0, 1.0); }'), fs = compile(g.FRAGMENT_SHADER, FRAG); if (!vs || !fs) return false;
    var pr = g.createProgram(); g.attachShader(pr, vs); g.attachShader(pr, fs); g.linkProgram(pr); g.useProgram(pr);
    var buf = g.createBuffer(); g.bindBuffer(g.ARRAY_BUFFER, buf); g.bufferData(g.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), g.STATIC_DRAW);
    var loc = g.getAttribLocation(pr, 'a'); g.enableVertexAttribArray(loc); g.vertexAttribPointer(loc, 2, g.FLOAT, false, 0, 0);
    ['u_res', 'u_t', 'u_m', 'u_p', 'u_k', 'u_win', 'u_lt', 'u_bg', 'u_atlas'].forEach(function (k) { U[k] = g.getUniformLocation(pr, k); });
    if (window.InfMode) { window.InfMode.atlas(g, function () { if (still) draw(performance.now()); }); window.InfMode.watch(function () { if (still) draw(performance.now()); }); }
    return true;
  }
  var ok = init(); if (!ok) { stage.classList.add('no-gl'); return; }

  /* the window sits in the free area: below the headline and above the bubbles on phones, right of the headline on desktop */
  var box = { x0: 0, x1: 1, y0: 0, y1: 1 };
  function fit() {
    var W = stage.clientWidth, H = stage.clientHeight; if (!W || !H) return;
    var sr = stage.getBoundingClientRect(), copy = stage.querySelector('.ax-copy'), low = stage.querySelector('.ax-low'), cta = stage.querySelector('.ax-cta');
    var x0 = 16, x1 = W - 16, y0, y1;
    if (W >= 900) {
      var h1 = copy && copy.querySelector('h1'), rg = document.createRange(); if (h1) rg.selectNodeContents(h1);
      var tr = h1 ? rg.getBoundingClientRect().right - sr.left : 0, lowTop = Math.min(low ? low.getBoundingClientRect().top - sr.top : H, cta ? cta.getBoundingClientRect().top - sr.top : H) - 16;
      if (W - 28 - (tr + 32) >= 340) { x0 = tr + 32; x1 = W - 28; y0 = 72; y1 = lowTop; }                       // beside the headline
      else { x0 = 24; x1 = W - 24; y0 = (copy ? copy.getBoundingClientRect().bottom - sr.top : 0) + 12; y1 = lowTop; }   // headline too wide: under it
    }
    else { y0 = (copy ? copy.getBoundingClientRect().bottom - sr.top : 0) + 10; y1 = (low ? low.getBoundingClientRect().top - sr.top : H) - 12; }
    y1 = Math.max(y1, y0 + 120);
    var fw = x1 - x0, fh = y1 - y0, asp = Math.max(1.2, Math.min(1.8, fw / fh)), w = Math.min(fw, fh * asp), h = w / asp;
    box = { cx: (x0 + x1) / 2, cy: (y0 + y1) / 2, hw: w / 2, hh: h / 2 };
    if (still) draw(performance.now());
  }
  function draw(now) {
    var dt = Math.min(0.05, (now - st.last) / 1000 || 0.016); st.last = now;
    if (!still && st.idle > 2.2) { var tt = now / 1000; st.tm = [0.5 + 0.2 * Math.sin(tt * 0.5), 0.5 + 0.15 * Math.sin(tt * 0.33 + 1)]; }
    st.idle += dt;
    var a = 1 - Math.exp(-dt * 5); st.m[0] += (st.tm[0] - st.m[0]) * a; st.m[1] += (st.tm[1] - st.m[1]) * a;
    st.p += (st.tp - st.p) * (1 - Math.exp(-dt * 10)); st.k += (st.tk - st.k) * (1 - Math.exp(-dt * 6));
    var dpr = Math.min(window.devicePixelRatio || 1, 1.5), cw = canvas.clientWidth, ch = canvas.clientHeight, w = Math.max(2, Math.round(cw * dpr)), h = Math.max(2, Math.round(ch * dpr));
    if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; g.viewport(0, 0, w, h); }
    var s = w / cw;
    g.uniform2f(U.u_res, w, h); g.uniform1f(U.u_t, still ? 3.2 : now / 1000); g.uniform2f(U.u_m, st.m[0], st.m[1]); g.uniform1f(U.u_p, st.p); g.uniform1f(U.u_k, st.k);
    var md = window.InfMode ? window.InfMode.get() : { lt: 0, bg: [1, 1, 1] }; g.uniform1f(U.u_lt, md.lt); g.uniform3f(U.u_bg, md.bg[0], md.bg[1], md.bg[2]); g.uniform1i(U.u_atlas, 0);
    g.uniform4f(U.u_win, box.cx * s, (ch - box.cy) * s, box.hw * s, box.hh * s);   // pixels: centre x, centre y (gl up), half width, half height; the shader uses half height as its unit
    g.clearColor(0, 0, 0, 0); g.clear(g.COLOR_BUFFER_BIT); g.drawArrays(g.TRIANGLES, 0, 3);
  }
  function loop(now) { if (st.vis && !document.hidden) draw(now); requestAnimationFrame(loop); }
  addEventListener('resize', fit); if (document.fonts && document.fonts.ready) document.fonts.ready.then(fit);
  fit();
  if (still) draw(performance.now()); else requestAnimationFrame(loop);
  if ('IntersectionObserver' in window) new IntersectionObserver(function (es) { st.vis = es[0].isIntersecting; }).observe(stage);
  canvas.addEventListener('webglcontextlost', function (e) { e.preventDefault(); stage.classList.add('no-gl'); }, false);

  function aim(e) {
    var r = canvas.getBoundingClientRect(), x = (e.clientX - r.left - box.cx) / (box.hw * 2) + 0.5, y = 1 - ((e.clientY - r.top - box.cy) / (box.hh * 2) + 0.5);
    st.tm = [Math.max(0, Math.min(1, x)), Math.max(0, Math.min(1, y))]; st.idle = 0; stage.classList.add('touched'); if (still) draw(performance.now());
  }
  stage.addEventListener('pointermove', aim, { passive: true });
  stage.addEventListener('pointerdown', function (e) { if (e.target.closest('button, a')) return; st.tp = 1; aim(e); }, { passive: true });
  addEventListener('pointerup', function () { st.tp = 0; }, { passive: true });
  addEventListener('pointercancel', function () { st.tp = 0; }, { passive: true });
  stage.addEventListener('ax:pick', function (e) { st.tk = e.detail; st.idle = 0; if (still) { st.k = e.detail; draw(performance.now()); } });
})();
