/* Audience pages: live shaders, the "try it" labs and small helpers. Plain WebGL, no libraries.
   Colours are the brand tokens (Midnight, Signal, Ember). Ember is switched on only where one thing should be looked at (u_ember).
   Motion: beats are 120 BPM (500 ms), reduced motion draws one still frame. Written for tools/website/gen_audiences.py pages. */
(function () {
  'use strict';
  var still = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  var HEAD = '#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n' +
    'uniform vec2 u_res; uniform float u_t; uniform vec2 u_m; uniform float u_a; uniform float u_b; uniform float u_s; uniform float u_s2; uniform float u_x; uniform float u_ember; uniform float u_lt; uniform vec3 u_bg; uniform sampler2D u_atlas;\n' +
    'const vec3 DEEP = vec3(0.051, 0.063, 0.125); const vec3 GROUND = vec3(0.082, 0.098, 0.188); const vec3 SIG5 = vec3(0.412, 0.455, 0.863); const vec3 SIG = vec3(0.655, 0.694, 1.0); const vec3 EMBER = vec3(1.0, 0.42, 0.208); const vec3 INK = vec3(0.933, 0.945, 0.98);\n' +
    'float hash(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }\n' +
    'float vnoise(vec2 p) { vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f); return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y); }\n' +
    /* light modes: luminance turns over (light-on-dark becomes ink-on-paper) and the hue stays, so Signal reads as indigo ink and Ember stays Ember */
    'vec3 mode(vec3 c) { if (u_lt < 0.5) return c; vec3 W = vec3(0.2126, 0.7152, 0.0722); float y = dot(c, W), yd = dot(DEEP, W); return clamp(u_bg + (c - y - (DEEP - yd)) * 1.15 - (y - yd), 0.0, 1.0); }\n' +
    'float glyph(float i, vec2 f) { return texture2D(u_atlas, vec2((clamp(i, 0.0, 15.0) + clamp(f.x, 0.02, 0.98)) / 16.0, 1.0 - f.y)).a; }\n' +
    'float fbm(vec2 p) { float s = 0.0, a = 0.5; for (int i = 0; i < 5; i++) { s += a * vnoise(p); p = p * 2.03 + 11.7; a *= 0.5; } return s; }\n';

  var SHADERS = {
    /* artists: a halftone print (the Marathon / Matrix poster references). A picture is sampled once per cell of a rotated grid and drawn
       as one dot whose area is the brightness. u_a = dot size, u_b = screen angle, u_s = the picture (0 orb, 1 rings, 2 clouds), u_m = the pointer, a light. */
    print: 'void main() {\n vec2 fc = gl_FragCoord.xy; float aspect = u_res.x / u_res.y; float t = u_t;\n float cell = mix(7.0, 30.0, u_a) * u_res.y / 640.0; float an = mix(0.0, 1.5708, u_b); float ca = cos(an), sa = sin(an);\n vec2 q = vec2(ca * fc.x + sa * fc.y, -sa * fc.x + ca * fc.y); vec2 id = floor(q / cell); vec2 cq = (id + 0.5) * cell;\n vec2 cs = vec2(ca * cq.x - sa * cq.y, sa * cq.x + ca * cq.y); vec2 uv = cs / u_res; vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);\n vec2 m = vec2((u_m.x - 0.5) * aspect, u_m.y - 0.5); float L = 0.0;\n if (u_s < 0.5) { vec2 c = vec2(0.12 * sin(t * 0.4), 0.04 * cos(t * 0.5)); float r = length(p - c) / 0.36;\n  if (r < 1.0) { vec3 n = vec3((p - c) / 0.36, sqrt(1.0 - r * r)); vec3 l = normalize(vec3(m - c + vec2(-0.3, 0.25), 0.55)); L = 0.08 + 0.92 * pow(max(dot(n, l), 0.0), 1.4); }\n  else L = 0.1 * smoothstep(1.6, 1.0, r) * smoothstep(-0.5, 0.2, p.y - c.y + 0.2); }\n else if (u_s < 1.5) { float r = length(p - (m * 0.35)); L = (0.5 + 0.5 * sin(r * 34.0 - t * 2.2)) * smoothstep(0.75, 0.05, r); }\n else { L = smoothstep(0.25, 0.85, fbm(p * 2.6 + vec2(t * 0.08, -t * 0.05))); L += 0.5 * exp(-dot(p - m, p - m) * 18.0); }\n L = clamp(L, 0.0, 1.0); float rad = sqrt(L) * cell * 0.62; float d = length(q - cq); float aa = 1.2;\n float cover = 1.0 - smoothstep(rad - aa, rad, d);\n vec3 col = mix(SIG5, INK, smoothstep(0.35, 1.0, L)); vec3 bg = GROUND;\n if (u_s2 > 0.5) { bg = u_s2 > 1.5 ? vec3(0.894, 0.886, 0.937) : vec3(0.984, 0.980, 0.965); col = mix(SIG5, GROUND, smoothstep(0.35, 1.0, L)); }\n gl_FragColor = vec4(mix(bg, col, cover), 1.0);\n}\n',
    /* artists: domain-warped ink. Two thin passes of fbm folded into Signal on Midnight. */
    ink: 'void main() {\n vec2 uv = (gl_FragCoord.xy - 0.5 * u_res) / u_res.y;\n vec2 m = (u_m - 0.5) * vec2(u_res.x / u_res.y, 1.0);\n float t = u_t * 0.09;\n vec2 p = uv * (1.2 + u_b * 2.4);\n vec2 q = vec2(fbm(p + t), fbm(p + vec2(5.2, 1.3) - t));\n vec2 r = vec2(fbm(p + 3.0 * u_a * q + vec2(1.7, 9.2) + 0.8 * t), fbm(p + 3.0 * u_a * q + vec2(8.3, 2.8) - 0.6 * t));\n float f = fbm(p + 3.5 * u_a * r + 0.6 * m);\n vec3 col = mix(DEEP, GROUND * 1.5, smoothstep(0.2, 0.6, f));\n col = mix(col, SIG5, smoothstep(0.45, 0.85, length(q)) * 0.75);\n col = mix(col, SIG, smoothstep(0.62, 0.95, f * length(r) * 1.5));\n col += EMBER * smoothstep(0.012, 0.0, abs(f - 0.7)) * 0.9 * u_ember;\n col *= 1.0 - 0.45 * dot(uv, uv);\n gl_FragColor = vec4(col, 1.0);\n}\n',

    /* musicians: stacked waves; u_a adds harmonics, u_b sets how many cycles */
    wave: 'float wv(float x, float fi) { return sin(x) + u_a * 0.5 * sin(2.0 * x + u_t * 0.7) + u_a * 0.33 * sin(3.0 * x - u_t * 0.5 + fi); }\nvoid main() {\n vec2 uv = gl_FragCoord.xy / u_res; float asp = u_res.x / u_res.y; float y = uv.y - 0.5;\n vec3 col = DEEP + GROUND * 0.35 * (1.0 - uv.y);\n float th = 1.7 / u_res.y;\n for (int i = 0; i < 6; i++) {\n  float fi = float(i);\n  float x = uv.x * 6.2831 * (1.2 + u_b * 2.6) + u_t * (0.5 + fi * 0.13) + fi * 0.8;\n  float yy = wv(x, fi) * 0.07 * (1.0 - fi * 0.07) + (fi - 2.5) * 0.075;\n  float d = abs(y - yy);\n  float k = 1.0 - fi * 0.11;\n  col += SIG * smoothstep(th * 1.4, 0.0, d) * (0.35 + 0.65 * k) + SIG5 * exp(-d * 55.0) * 0.12 * k;\n }\n float ph = fract(u_t * 0.25);\n float xp = ph * 6.2831 * (1.2 + u_b * 2.6) + u_t * 0.76 + 2.0;\n float yp = wv(xp, 2.0) * 0.07 * 0.86 + (2.0 - 2.5) * 0.075;\n col += EMBER * smoothstep(0.016, 0.006, length(vec2((uv.x - ph) * asp, y - yp))) * u_ember;\n col += INK * smoothstep(0.0015, 0.0, abs(uv.x - ph)) * 0.08;\n gl_FragColor = vec4(col, 1.0);\n}\n',

    /* vjs: beat-driven scenes at the BPM in u_a (0..1 maps 60..180). u_s and u_s2 pick scenes, u_x crossfades them on the bar */
    /* vjs: the scenes drawn as a glyph matrix (refs: the ASCII colour-block matrix, the dot-matrix glyph field). Each cell reads the scene's
       brightness and picks a character from a density ramp; on every beat a few cell blocks flip to solid Signal with the type knocked out,
       one of them Ember. u_a = BPM (60..180), u_s / u_s2 = scenes, u_x = the crossfade on the bar. Ink on paper in the light modes. */
    pulse: 'float env;\nfloat scene(vec2 p, float id, float tt) {\n float r = length(p); float a = atan(p.y, p.x);\n if (id < 0.5) { float z = 1.0 / (r + 0.06); float rings = fract(z * 0.55 - tt * 0.5);\n  float c = smoothstep(0.55, 0.45, abs(rings - 0.5) * 2.0) * smoothstep(0.04, 0.3, r);\n  float sp = smoothstep(0.08, 0.0, abs(fract(a / 6.2831 * 12.0 + tt * 0.1) - 0.5) - 0.42) * smoothstep(0.05, 0.4, r);\n  return c * (0.55 + env * 0.45) + sp * 0.35; }\n if (id < 1.5) { float N = 6.0; float aa = abs(mod(a, 6.2831 / N) - 3.1416 / N); vec2 q = r * vec2(cos(aa), sin(aa)) * 9.0;\n  float v = sin(q.x + tt * 1.2) * sin(q.y - tt * 0.8) + sin(r * 18.0 - tt * 2.0) * 0.5; return smoothstep(-0.2, 1.1, v) * (0.75 + env * 0.25); }\n vec2 g = fract(p * 5.0) - 0.5; vec2 id2 = floor(p * 5.0); float sz = 0.16 + 0.22 * env * (0.5 + 0.5 * sin(id2.x * 1.7 + id2.y * 2.3 + tt));\n return smoothstep(sz + 0.06, sz, max(abs(g.x), abs(g.y)));\n}\nvoid main() {\n float ch = max(11.0, u_res.y / 30.0); vec2 cs = vec2(ch * 0.62, ch);\n vec2 cid = floor(gl_FragCoord.xy / cs); vec2 f = fract(gl_FragCoord.xy / cs); vec2 p = ((cid + 0.5) * cs - 0.5 * u_res) / u_res.y;\n float bps = (60.0 + u_a * 120.0) / 60.0; float bn = floor(u_t * bps); float beat = fract(u_t * bps); env = exp(-beat * 5.0); float tt = u_t * bps * 0.5;\n float l = scene(p, u_s, tt); if (u_x > 0.001) l = mix(l, scene(p, u_s2, tt), u_x);\n l += smoothstep(0.05, 0.0, abs(length(p) - beat * 0.9)) * (1.0 - beat) * 0.7; l = clamp(l * (1.0 - 0.45 * dot(p, p)), 0.0, 1.0);\n float k = floor(l * 10.99); float gl = glyph(k, f);\n vec2 bid = floor(cid / vec2(6.0, 2.0)); float h = hash(bid + vec2(bn * 0.731, bn * 0.177)); float blk = step(0.94, h) * step(0.15, l);\n vec2 nb = ceil(u_res / (cs * vec2(6.0, 2.0))); vec2 eb = floor(vec2(hash(vec2(bn, 1.3)), hash(vec2(2.1, bn))) * nb); float emb = u_ember * (1.0 - step(0.5, length(bid - eb)));\n vec3 bg = DEEP, ink = SIG, blc = SIG5;\n if (u_lt > 0.5) { bg = u_bg; ink = vec3(0.235, 0.275, 0.62); blc = vec3(0.29, 0.33, 0.84); }\n blc = mix(blc, EMBER, emb); blk = max(blk, emb);\n vec3 col = mix(bg, mix(bg, ink, 0.3 + 0.7 * l), gl);\n col = mix(col, mix(blc, bg, gl), blk * (0.85 + 0.15 * env));\n gl_FragColor = vec4(col, 1.0);\n}\n',

    /* creative coders: the Phyllotaxis Bloom preset, every seed drawn as one of the brand's unit marks (dot, ring, square, cross, plus;
       refs: the dot-matrix glyph field, the halftone posters). Rings of seeds share a mark and the marks step on the beat. One Ember seed.
       u_a = turn (-90..90 deg/s), u_b = seeds. Monochrome Signal: light on Midnight, ink on paper. */
    dots: 'void main() {\n vec2 uv = gl_FragCoord.xy / u_res; float aspect = u_res.x / u_res.y; float t = u_t;\n float seeds = mix(60.0, 1500.0, u_b); float turn = mix(-90.0, 90.0, u_a);\n vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * 1.02;\n float sc = 0.46 / sqrt(seeds); float rot = turn * 0.0174533 * t;\n float r = length(p); float phi = fract((atan(p.y, p.x) - rot) / 6.283185); float s = r * r / (sc * sc);\n float fa = 34.0 * phi + s * 0.01315562; float fb = 0.021286236 * s - 21.0 * phi;\n float ra = floor(fa + 0.5); float rb = floor(fb + 0.5);\n float dm = 10.0; float kb = -1.0; vec2 dv = vec2(0.0);\n for (int i = -1; i <= 1; i++) { for (int j = -1; j <= 1; j++) {\n  float kk = 21.0 * (ra + float(i)) + 34.0 * (rb + float(j));\n  if (kk < 0.0 || kk > seeds - 1.0) continue;\n  float sa = kk * 2.399963 + rot; vec2 sp = sc * sqrt(kk) * vec2(cos(sa), sin(sa));\n  float dk = length(p - sp); if (dk < dm) { dm = dk; kb = kk; dv = p - sp; } } }\n vec3 bg = GROUND, ink = SIG, ink2 = SIG5; if (u_lt > 0.5) { bg = u_bg; ink = vec3(0.235, 0.275, 0.62); ink2 = vec3(0.29, 0.33, 0.84); }\n if (kb < 0.0) { gl_FragColor = vec4(bg, 1.0); return; }\n float rad = 0.8 * sc; vec2 d = dv / rad; float aa = 1.6 / (u_res.y * rad);\n float band = floor(7.0 * sqrt(kb / seeds)); float kind = mod(band + floor(t * 2.0), 5.0);\n float m;\n if (kind < 0.5) m = 1.0 - smoothstep(0.62 - aa, 0.62, length(d));\n else if (kind < 1.5) m = 1.0 - smoothstep(0.12 - aa, 0.12, abs(length(d) - 0.6));\n else if (kind < 2.5) m = 1.0 - smoothstep(0.55 - aa, 0.55, max(abs(d.x), abs(d.y)));\n else { vec2 q = kind < 3.5 ? vec2(d.x + d.y, d.x - d.y) * 0.7071 : d; m = (1.0 - smoothstep(0.13 - aa, 0.13, min(abs(q.x), abs(q.y)))) * (1.0 - smoothstep(0.78 - aa, 0.78, max(abs(q.x), abs(q.y)))); }\n vec3 pc = mix(ink, ink2, mod(band, 2.0));\n if (u_ember > 0.5 && kb < 0.5) pc = EMBER;\n gl_FragColor = vec4(mix(bg, pc, m), 1.0);\n}\n',


    /* philosophy: a small graph of drifting points. Lines fade with length; the pointer joins as one more point */
    mind: 'vec2 pt(float i) { float k = i * 2.399; return vec2(0.62 * cos(u_t * (0.08 + 0.05 * fract(i * 0.37)) + k), 0.36 * sin(u_t * (0.07 + 0.05 * fract(i * 0.61)) + k * 1.7)) * (0.55 + 0.5 * fract(i * 0.618)); }\nfloat seg(vec2 p, vec2 a, vec2 b) { vec2 pa = p - a, ba = b - a; float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0); return length(pa - ba * h); }\nvoid main() {\n vec2 p = (gl_FragCoord.xy - 0.5 * u_res) / u_res.y; float asp = u_res.x / u_res.y; float sx = min(asp, 1.0) * 1.15;\n vec2 m = (u_m - 0.5) * vec2(asp, 1.0);\n float th = 1.6 / u_res.y; vec3 col = mix(DEEP, GROUND, 0.5 + 0.5 * p.y);\n for (int i = 0; i < 10; i++) {\n  vec2 a = pt(float(i)) * vec2(sx, 1.0) * 1.5;\n  float d = length(p - a); col += SIG * (0.0014 / (d * d + 0.0016)) * 0.18 + SIG * smoothstep(0.012, 0.0, d);\n  for (int j = 0; j < 10; j++) {\n   if (j <= i) continue;\n   vec2 b = pt(float(j)) * vec2(sx, 1.0) * 1.5; float L = length(a - b);\n   col += SIG5 * smoothstep(th * 1.2, 0.0, seg(p, a, b)) * smoothstep(0.7, 0.2, L) * 0.7; }\n  float L2 = length(a - m); col += SIG * smoothstep(th * 1.4, 0.0, seg(p, a, m)) * smoothstep(0.55, 0.1, L2) * 0.9;\n }\n col += INK * smoothstep(0.016, 0.0, length(p - m)) * 0.9;\n col *= 1.0 - 0.4 * dot(p, p);\n gl_FragColor = vec4(col, 1.0);\n}\n',

    /* artists lab: the Iridescent Orb preset. u_a = size, u_b = wobble */
    orb: 'void main() {\n vec2 uv = gl_FragCoord.xy / u_res; float aspect = u_res.x / u_res.y; float t = u_t;\n float size = mix(0.05, 0.6, u_a); float wobble = u_b * 0.15; float spin = 40.0; float sheen = 0.75; float gloss = 0.9;\n vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5); float r = length(p); float ang = atan(p.y, p.x);\n float rad = size * (1.0 + wobble * sin(3.0 * ang + t * 1.1) + wobble * 0.5 * sin(5.0 * ang - t * 1.3));\n float aa = 1.5 / u_res.y; float edge = 1.0 - smoothstep(rad - aa, rad, r);\n float u = fract(ang / 6.283185 + spin * t / 360.0); float s = u * 5.0;\n float w0 = max(0.0, 1.0 - abs(s)) + max(0.0, 1.0 - abs(s - 5.0)); float w1 = max(0.0, 1.0 - abs(s - 1.0)); float w2 = max(0.0, 1.0 - abs(s - 2.0)); float w3 = max(0.0, 1.0 - abs(s - 3.0)); float w4 = max(0.0, 1.0 - abs(s - 4.0));\n vec3 body = vec3(1.0, 0.702, 0.851) * w0 + vec3(0.78, 0.722, 1.0) * w1 + vec3(0.659, 0.894, 1.0) * w2;\n body += vec3(0.71, 0.961, 0.863) * w3 + vec3(1.0, 0.89, 0.69) * w4;\n vec2 sp = vec2(-0.3, 0.35) * size; float sh = clamp(1.0 - length(p - sp) / (size * 1.2), 0.0, 1.0) * 0.75 * sheen;\n body = mix(body, vec3(1.0), sh);\n float rim = smoothstep(0.7, 1.0, r / (size * 1.05)) * 0.22; body = mix(body, vec3(0.416, 0.361, 0.91), rim);\n vec2 gp = vec2(-0.34, 0.4) * size; float spot = (1.0 - smoothstep(0.0, size * 0.14, length(p - gp))) * gloss; body = mix(body, vec3(1.0), spot);\n gl_FragColor = vec4(mix(GROUND, body, edge), 1.0);\n}\n',

    /* developers: a contact sheet filling tile by tile, one tile per half second. u_s = tiles rendered so far */
    sheet: 'void main() {\n vec2 uv = gl_FragCoord.xy / u_res; float asp = u_res.x / u_res.y;\n float cols = 4.0, rows = 3.0; vec2 g = vec2(uv.x * cols, (1.0 - uv.y) * rows); vec2 id = floor(g); vec2 f = fract(g) - 0.5;\n float n = id.y * cols + id.x; float gut = 0.045; vec2 fs = vec2(f.x * (asp * rows / cols), f.y);\n float inTile = step(max(abs(f.x) * 1.0, abs(f.y)), 0.5 - gut * 2.0);\n float done = step(n + 0.5, u_s); float last = step(abs(n + 1.0 - u_s), 0.5);\n vec2 q = vec2(f.x * asp * rows / cols, f.y) * 5.0; float tt = n * 0.55;\n float v = sin(length(q) * 4.0 - tt * 2.0) + sin(q.x * 2.0 + tt) * sin(q.y * 2.0 - tt * 0.7);\n vec3 tile = mix(GROUND, SIG5, smoothstep(-0.6, 1.0, v)); tile = mix(tile, SIG, smoothstep(1.0, 1.7, v));\n vec3 col = DEEP;\n col = mix(col, GROUND * 0.9, inTile * (1.0 - done));\n col = mix(col, tile, inTile * done);\n float edge = step(0.5 - gut * 2.0, max(abs(f.x), abs(f.y))) * step(max(abs(f.x), abs(f.y)), 0.5 - gut * 2.0 + 0.012 * 3.0);\n col += EMBER * edge * last * u_ember;\n gl_FragColor = vec4(col, 1.0);\n}\n'
  };

  /* the page mode for canvases: lt = 1 in mist and paper, bg = that mode's ground (0..1 rgb). atlas() uploads the glyph ramp to texture unit 0. */
  var Mode = window.InfMode = (function () {
    var html = document.documentElement, cache = null, cv = document.createElement('canvas'); cv.width = cv.height = 1; var cx = cv.getContext('2d');
    function rgb(c) { cx.clearRect(0, 0, 1, 1); cx.fillStyle = '#FBFAF6'; cx.fillStyle = c; cx.fillRect(0, 0, 1, 1); var d = cx.getImageData(0, 0, 1, 1).data; return [d[0] / 255, d[1] / 255, d[2] / 255]; }
    function get() {
      var m = html.getAttribute('data-mode') || 'midnight';
      if (!cache || cache.m !== m) { var bg = getComputedStyle(html).getPropertyValue('--surface-1').trim() || '#FBFAF6'; cache = { m: m, lt: m === 'midnight' ? 0 : 1, bg: rgb(bg) }; cache.css = 'rgb(' + cache.bg.map(function (v) { return Math.round(v * 255); }).join(',') + ')'; }
      return cache;
    }
    function watch(fn) { new MutationObserver(function () { cache = null; fn(get()); }).observe(html, { attributes: true, attributeFilter: ['data-mode'] }); }
    var RAMP = ' .:-=+*o%#@';
    function atlas(g, done) {
      var c = document.createElement('canvas'); c.width = 640; c.height = 64; var x = c.getContext('2d');
      var tex = g.createTexture(); g.activeTexture(g.TEXTURE0); g.bindTexture(g.TEXTURE_2D, tex);
      g.texParameteri(g.TEXTURE_2D, g.TEXTURE_MIN_FILTER, g.LINEAR); g.texParameteri(g.TEXTURE_2D, g.TEXTURE_MAG_FILTER, g.LINEAR);
      g.texParameteri(g.TEXTURE_2D, g.TEXTURE_WRAP_S, g.CLAMP_TO_EDGE); g.texParameteri(g.TEXTURE_2D, g.TEXTURE_WRAP_T, g.CLAMP_TO_EDGE);
      function paint() {
        x.clearRect(0, 0, 640, 64); x.fillStyle = '#fff'; x.textAlign = 'center'; x.textBaseline = 'middle'; x.font = '600 46px "Geist Mono", ui-monospace, monospace';
        for (var i = 0; i < RAMP.length; i++) x.fillText(RAMP[i], i * 40 + 20, 34);
        g.bindTexture(g.TEXTURE_2D, tex); g.texImage2D(g.TEXTURE_2D, 0, g.RGBA, g.RGBA, g.UNSIGNED_BYTE, c);
      }
      paint(); if (document.fonts && document.fonts.load) document.fonts.load('600 46px "Geist Mono"').then(function () { paint(); if (done) done(); });
      return tex;
    }
    return { get: get, watch: watch, atlas: atlas };
  })();

  var VERT = 'attribute vec2 a; void main() { gl_Position = vec4(a, 0.0, 1.0); }';

  function compile(g, type, src) {
    var s = g.createShader(type); g.shaderSource(s, src); g.compileShader(s);
    if (!g.getShaderParameter(s, g.COMPILE_STATUS)) { if (window.console) console.warn(g.getShaderInfoLog(s)); return null; }
    return s;
  }

  /* Stage: one canvas, one shader. opts() is read every frame and returns uniforms. */
  function Stage(canvas, name, opts) {
    var g = canvas.getContext('webgl', { antialias: false, alpha: false, powerPreference: 'low-power' });
    if (!g) { canvas.parentNode.classList.add('no-gl'); return null; }
    var src = SHADERS[name]; if (src.indexOf('u_lt') < 0 && name !== 'print') src = src.replace(/gl_FragColor = vec4\(([^\n]*), 1\.0\);/, 'gl_FragColor = vec4(mode($1), 1.0);');
    var vs = compile(g, g.VERTEX_SHADER, VERT), fs = compile(g, g.FRAGMENT_SHADER, HEAD + src);
    if (!vs || !fs) { canvas.parentNode.classList.add('no-gl'); return null; }
    var pr = g.createProgram(); g.attachShader(pr, vs); g.attachShader(pr, fs); g.linkProgram(pr); g.useProgram(pr);
    var buf = g.createBuffer(); g.bindBuffer(g.ARRAY_BUFFER, buf);
    g.bufferData(g.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), g.STATIC_DRAW);
    var loc = g.getAttribLocation(pr, 'a'); g.enableVertexAttribArray(loc); g.vertexAttribPointer(loc, 2, g.FLOAT, false, 0, 0);
    var U = {}; ['u_res', 'u_t', 'u_m', 'u_a', 'u_b', 's', 'u_s', 'u_s2', 'u_x', 'u_ember', 'u_lt', 'u_bg', 'u_atlas'].forEach(function (k) { U[k] = g.getUniformLocation(pr, k); });
    if (name === 'pulse') Mode.atlas(g, function () { if (still) draw(); });
    var st = { m: [0.5, 0.5], visible: true, t0: performance.now(), tFix: 3.2 };
    function size() {
      var dpr = Math.min(window.devicePixelRatio || 1, 1.5), w = Math.max(2, Math.round(canvas.clientWidth * dpr)), h = Math.max(2, Math.round(canvas.clientHeight * dpr));
      if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; g.viewport(0, 0, w, h); }
    }
    function draw(now) {
      size();
      var o = opts ? opts() : {}, t = still ? (o.t !== undefined ? o.t : st.tFix) : (o.t !== undefined ? o.t : (now - st.t0) / 1000);
      g.uniform2f(U.u_res, canvas.width, canvas.height); g.uniform1f(U.u_t, t); g.uniform2f(U.u_m, st.m[0], st.m[1]);
      g.uniform1f(U.u_a, o.a !== undefined ? o.a : 0.5); g.uniform1f(U.u_b, o.b !== undefined ? o.b : 0.5);
      g.uniform1f(U.u_s, o.s || 0); g.uniform1f(U.u_s2, o.s2 || 0); g.uniform1f(U.u_x, o.x || 0); g.uniform1f(U.u_ember, o.ember || 0);
      var md = Mode.get(); g.uniform1f(U.u_lt, md.lt); g.uniform3f(U.u_bg, md.bg[0], md.bg[1], md.bg[2]); g.uniform1i(U.u_atlas, 0);
      g.drawArrays(g.TRIANGLES, 0, 3);
    }
    st.draw = draw;
    function loop(now) { if (st.visible && !document.hidden) draw(now); st.raf = requestAnimationFrame(loop); }
    var host = canvas.parentNode;
    host.addEventListener('pointermove', function (e) { var r = canvas.getBoundingClientRect(); st.m = [(e.clientX - r.left) / r.width, 1 - (e.clientY - r.top) / r.height]; if (still) draw(); }, { passive: true });
    if ('IntersectionObserver' in window) new IntersectionObserver(function (es) { st.visible = es[0].isIntersecting; }, { rootMargin: '80px' }).observe(canvas);
    window.addEventListener('resize', function () { draw(performance.now()); });
    canvas.addEventListener('webglcontextlost', function (e) { e.preventDefault(); host.classList.add('no-gl'); }, false);
    if (still) draw(); else st.raf = requestAnimationFrame(loop);
    Mode.watch(function () { if (still) draw(); });
    return st;
  }

  /* hero: 3D cutout floats and leans toward the pointer */
  function lean(stage) {
    var img = stage.querySelector('.vstage-3d'); if (!img || still) return;
    stage.addEventListener('pointermove', function (e) {
      var r = stage.getBoundingClientRect(), x = (e.clientX - r.left) / r.width - 0.5, y = (e.clientY - r.top) / r.height - 0.5;
      img.style.setProperty('--lx', (x * 16).toFixed(1) + 'px'); img.style.setProperty('--ly', (y * 12).toFixed(1) + 'px');
      img.style.setProperty('--ry', (x * 8).toFixed(1) + 'deg'); img.style.setProperty('--rx', (-y * 6).toFixed(1) + 'deg');
    }, { passive: true });
    stage.addEventListener('pointerleave', function () { ['--lx', '--ly', '--ry', '--rx'].forEach(function (k) { img.style.removeProperty(k); }); });
  }

  function range(el, fn) { var f = function () { fn(parseFloat(el.value) / parseFloat(el.max || 1)); var o = el.parentNode.querySelector('output'); if (o) o.textContent = el.dataset.fmt ? fmt(el) : ''; }; el.addEventListener('input', f); f(); }
  function fmt(el) {
    var v = parseFloat(el.value), lo = parseFloat(el.dataset.lo), hi = parseFloat(el.dataset.hi), x = lo + (hi - lo) * v / parseFloat(el.max || 1);
    return (el.dataset.dp ? x.toFixed(parseInt(el.dataset.dp, 10)) : Math.round(x)) + (el.dataset.unit || '');
  }

  /* ---- labs ---- */
  var labs = {
    /* artists: the halftone print. Three pictures as numbered tiles (one is live), two sliders, the pointer is the light */
    print: function (root) {
      var c = root.querySelector('canvas'), sl = root.querySelectorAll('input[type=range]'), tiles = root.querySelectorAll('.pr-tile'), cap = root.querySelector('.pr-cap');
      var v = { a: 0.5, b: 0.5 }, s = 0, html = document.documentElement, mode = function () { var m = html.getAttribute('data-mode'); return m === 'midnight' ? 0 : m === 'mist' ? 2 : 1; };   // the print takes the page: ink on paper in the light modes
      var stage = Stage(c, 'print', function () { return { a: v.a, b: v.b, s: s, s2: mode() }; });
      new MutationObserver(function () { if (still && stage) stage.draw(); }).observe(html, { attributes: true, attributeFilter: ['data-mode'] });
      sl.forEach(function (el, i) { range(el, function (x) { v[i ? 'b' : 'a'] = x; if (still && stage) stage.draw(); }); });
      tiles.forEach(function (b, i) {
        b.addEventListener('click', function () {
          s = i; tiles.forEach(function (o, j) { o.setAttribute('aria-checked', j === i ? 'true' : 'false'); });
          if (cap) cap.textContent = b.dataset.name;
          if (still && stage) stage.draw();
        });
      });
    },
    /* a lab with two sliders driving a shader; the sliders are the Field params */
    shader: function (root) {
      var c = root.querySelector('canvas'), sl = root.querySelectorAll('input[type=range]'), v = { a: 0.5, b: 0.5 };
      var name = root.dataset.shader, s = +(root.dataset.s || 0);
      var stage = Stage(c, name, function () { return { a: v.a, b: v.b, s: s, ember: 1 }; });
      sl.forEach(function (el, i) { range(el, function (x) { v[i ? 'b' : 'a'] = x; if (still && stage) stage.draw(); }); });
    },
    /* VJ: BPM, three scenes, a switch that waits for the next bar */
    show: function (root) {
      var c = root.querySelector('canvas'), bpm = root.querySelector('input[type=range]'), pads = root.querySelectorAll('.pad'), beats = root.querySelectorAll('.beats i'), bar = root.querySelector('.bar-state');
      var cur = 0, next = -1, fadeStart = -1, a = 0.5, t0 = performance.now(), lastBeat = -1;
      var stage = Stage(c, 'pulse', function () {
        var t = (performance.now() - t0) / 1000, bps = (60 + a * 120) / 60, pos = t * bps, beatN = Math.floor(pos), inBar = beatN % 4, o = { a: a, s: cur, ember: 1, t: t };
        if (still) { o.t = 3.2; return o; }
        if (beatN !== lastBeat) {
          lastBeat = beatN;
          beats.forEach(function (b, i) { b.classList.toggle('on', i === inBar); });
          if (inBar === 0 && next >= 0) { fadeStart = pos; }
        }
        if (fadeStart >= 0) {
          var x = Math.min(1, (pos - fadeStart) / 1.0); o.s2 = next; o.x = x;
          if (x >= 1) { cur = next; next = -1; fadeStart = -1; o.s = cur; o.x = 0; pads.forEach(function (p, i) { p.classList.toggle('live', i === cur); p.classList.remove('queued'); }); if (bar) bar.textContent = 'Playing scene ' + (cur + 1); }
        }
        return o;
      });
      range(bpm, function (x) { a = x; });
      pads.forEach(function (p, i) {
        p.addEventListener('click', function () {
          if (i === cur && next < 0) return;
          next = i; pads.forEach(function (q, j) { q.classList.toggle('queued', j === i && i !== cur); });
          if (bar) bar.textContent = 'Scene ' + (i + 1) + ' lands on the next bar';
          if (still) { cur = i; next = -1; pads.forEach(function (q, j) { q.classList.toggle('live', j === cur); q.classList.remove('queued'); }); if (stage) stage.draw(); if (bar) bar.textContent = 'Playing scene ' + (cur + 1); }
        });
      });
    },
    /* musicians: a saw through a low-pass, an LFO on the cutoff, and sound if you ask for it */
    synth: function (root) {
      var c = root.querySelector('canvas'), ctx2 = c.getContext('2d'), play = root.querySelector('.play'), sl = root.querySelectorAll('input[type=range]');
      var cut = 0.55, lfo = 0.4, au = null, nodes = null, f0 = 110, t0 = performance.now();
      var css = getComputedStyle(document.documentElement);
      function col(n, d) { return (css.getPropertyValue(n) || d).trim() || d; }
      function fc(t) { var base = 80 * Math.pow(2, cut * 6.5), mod = Math.pow(2, Math.sin(t * (0.3 + lfo * 3)) * lfo * 2.2); return base * mod; }
      function draw() {
        var dpr = Math.min(window.devicePixelRatio || 1, 2), w = c.clientWidth, h = c.clientHeight;
        if (c.width !== w * dpr) { c.width = w * dpr; c.height = h * dpr; }
        ctx2.setTransform(dpr, 0, 0, dpr, 0, 0); ctx2.clearRect(0, 0, w, h);
        var t = still ? 3.2 : (performance.now() - t0) / 1000, f = fc(t), cycles = 2.2;
        ctx2.fillStyle = '#0D1020'; ctx2.fillRect(0, 0, w, h);
        ctx2.strokeStyle = 'rgba(167,177,255,0.14)'; ctx2.lineWidth = 1; ctx2.beginPath(); ctx2.moveTo(0, h / 2); ctx2.lineTo(w, h / 2); ctx2.stroke();
        var pts = []; ctx2.beginPath(); ctx2.lineWidth = 2.2; ctx2.strokeStyle = '#A7B1FF'; ctx2.lineJoin = 'round';
        var amp = 0; for (var n = 1; n <= 40; n++) amp += 1 / n * 1 / Math.sqrt(1 + Math.pow(n * f0 / f, 4));
        for (var x = 0; x <= w; x += 2) {
          var ph = x / w * cycles, y = 0;
          for (var k = 1; k <= 40; k++) y += Math.sin(6.2831 * k * ph) / k / Math.sqrt(1 + Math.pow(k * f0 / f, 4));
          y = y / Math.max(amp, 1) * -1.55; var py = h / 2 + y * h * 0.36;
          if (x === 0) ctx2.moveTo(x, py); else ctx2.lineTo(x, py); pts.push(py);
        }
        ctx2.stroke();
        var ix = Math.floor((((t * 0.25) % 1) * w) / 2), px = ix * 2;
        ctx2.fillStyle = '#FF6B35'; ctx2.beginPath(); ctx2.arc(px, pts[Math.min(ix, pts.length - 1)], 5, 0, 6.2832); ctx2.fill();
        var cutX = Math.max(0, Math.min(1, Math.log(f / 80) / Math.log(2) / 9)) ;
        ctx2.fillStyle = 'rgba(167,177,255,.75)'; ctx2.font = '500 12px "Geist Mono", ui-monospace, monospace';
        ctx2.fillText('cutoff ' + Math.round(f) + ' Hz', 14, h - 14);
      }
      var vis = true; if ('IntersectionObserver' in window) new IntersectionObserver(function (es) { vis = es[0].isIntersecting; }, { rootMargin: '80px' }).observe(c);
      function loop() { if (vis && !document.hidden) draw(); requestAnimationFrame(loop); }
      if (still) draw(); else requestAnimationFrame(loop);
      window.addEventListener('resize', draw);
      range(sl[0], function (x) { cut = x; var out = sl[0].parentNode.querySelector('output'); if (out) out.textContent = Math.round(80 * Math.pow(2, x * 6.5)) + ' Hz'; if (nodes) nodes.f.frequency.value = 80 * Math.pow(2, cut * 6.5); if (still) draw(); });
      range(sl[1], function (x) { lfo = x; if (nodes) { nodes.l.frequency.value = 0.3 + lfo * 3; nodes.lg.gain.value = (80 * Math.pow(2, cut * 6.5)) * lfo * 0.9; } if (still) draw(); });
      function stop() { if (au) { try { nodes.o.stop(); nodes.l.stop(); au.close(); } catch (e) {} au = null; nodes = null; } play.setAttribute('aria-pressed', 'false'); play.textContent = 'Play a note'; }
      play.addEventListener('click', function () {
        if (au) { stop(); return; }
        var AC = window.AudioContext || window.webkitAudioContext; if (!AC) { play.textContent = 'No audio here'; return; }
        au = new AC(); var o = au.createOscillator(), f = au.createBiquadFilter(), g = au.createGain(), l = au.createOscillator(), lg = au.createGain();
        o.type = 'sawtooth'; o.frequency.value = f0; f.type = 'lowpass'; f.Q.value = 4; f.frequency.value = 80 * Math.pow(2, cut * 6.5);
        l.frequency.value = 0.3 + lfo * 3; lg.gain.value = f.frequency.value * lfo * 0.9; g.gain.value = 0.0;
        l.connect(lg); lg.connect(f.frequency); o.connect(f); f.connect(g); g.connect(au.destination); o.start(); l.start();
        g.gain.setTargetAtTime(0.12, au.currentTime, 0.05); nodes = { o: o, f: f, l: l, lg: lg, g: g };
        play.setAttribute('aria-pressed', 'true'); play.textContent = 'Stop';
      });
      document.addEventListener('visibilitychange', function () { if (document.hidden) stop(); });
    },
    /* developers: command tabs and a contact sheet that fills as if a render were running */
    term: function (root) {
      var tabs = root.querySelectorAll('[role=tab]'), panels = root.querySelectorAll('[role=tabpanel]'), run = root.querySelector('.run'), c = root.querySelector('canvas');
      var n = 0, timer = null, stage = Stage(c, 'sheet', function () { return { s: n, ember: 1 }; });
      function show(i) { tabs.forEach(function (t, j) { t.setAttribute('aria-selected', j === i ? 'true' : 'false'); t.tabIndex = j === i ? 0 : -1; }); panels.forEach(function (p, j) { p.hidden = j !== i; }); }
      tabs.forEach(function (t, i) {
        t.addEventListener('click', function () { show(i); });
        t.addEventListener('keydown', function (e) { var d = e.key === 'ArrowRight' ? 1 : e.key === 'ArrowLeft' ? -1 : 0; if (d) { var k = (i + d + tabs.length) % tabs.length; show(k); tabs[k].focus(); } });
      });
      function go() {
        clearInterval(timer); n = 0; if (run) { run.classList.add('busy'); setTimeout(function () { run.classList.remove('busy'); }, 500); } if (still) { n = 12; stage && stage.draw(); return; }
        timer = setInterval(function () { n++; if (n >= 12) clearInterval(timer); }, 500);
      }
      if (run) run.addEventListener('click', go);
      if ('IntersectionObserver' in window) { var seen = false; new IntersectionObserver(function (es) { if (es[0].isIntersecting && !seen) { seen = true; go(); } }, { threshold: 0.3 }).observe(root); } else { n = 12; }
      show(0);
      root.querySelectorAll('.copy').forEach(function (b) {
        b.addEventListener('click', function () {
          var code = b.parentNode.querySelector('code'); if (!code) return;
          var done = function () { var o = b.textContent; b.textContent = 'Copied'; setTimeout(function () { b.textContent = o; }, 1500); };
          if (navigator.clipboard) navigator.clipboard.writeText(code.textContent).then(done, function () {}); else { var r = document.createRange(); r.selectNodeContents(code); var s = window.getSelection(); s.removeAllRanges(); s.addRange(r); try { document.execCommand('copy'); done(); } catch (e) {} }
        });
      });
    }
  };

  function boot() {
    document.querySelectorAll('.vstage canvas[data-shader]').forEach(function (cv) {
      var st = cv.parentNode, o = { a: +(cv.dataset.a || 0.5), b: +(cv.dataset.b || 0.5), s: +(cv.dataset.s || 0) };
      Stage(cv, cv.dataset.shader, function () { if (cv.dataset.shader === 'sheet') o.s = still ? 12 : Math.floor(((performance.now() / 1000) % 7) * 2); return o; }); lean(st);
    });
    document.querySelectorAll('[data-lab]').forEach(function (el) { var f = labs[el.dataset.lab]; if (f) f(el); });
    /* the reel scrolls by buttons as well as by touch */
    document.querySelectorAll('.reel-wrap').forEach(function (w) {
      var r = w.querySelector('.reel'), sec = w.parentNode, prev = sec.querySelector('.reel-prev'), nxt = sec.querySelector('.reel-next');
      if (!r) return;
      function step(d) { r.scrollBy({ left: d * Math.max(240, r.clientWidth * 0.8), behavior: still ? 'auto' : 'smooth' }); }
      function upd() { if (prev) prev.disabled = r.scrollLeft < 8; if (nxt) nxt.disabled = r.scrollLeft + r.clientWidth >= r.scrollWidth - 8; }
      if (prev) prev.addEventListener('click', function () { step(-1); }); if (nxt) nxt.addEventListener('click', function () { step(1); });
      r.addEventListener('scroll', upd, { passive: true }); window.addEventListener('resize', upd); upd();
    });
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot); else boot();
})();
