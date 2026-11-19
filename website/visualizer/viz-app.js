// Infinite Audio-Reactive Music Visualizer & Image FX Studio
// 8 procedural & image-reactive WebGL shaders inspired by Infinite's FormulaNode & optical shaders
// with live 128-bin FFT + waveform DataTexture, image upload FX, and Infinite WASM beat generation.
(function () {
  'use strict';

  const $ = (id) => document.getElementById(id);

  const canvas = $('viz-canvas');
  const stageBox = $('stage-box');
  const statusEl = $('viz-status');
  const hudPill = $('viz-hud');
  const hudText = $('viz-hud-text');
  const hudMeta = $('viz-hud-meta');
  const meterBars = Array.from(document.querySelectorAll('#viz-meters i'));

  const gl = canvas.getContext('webgl', { preserveDrawingBuffer: true, antialias: true });

  const state = {
    style: 0,
    pal: 0,
    seed: 1.42,
    react: 1.25,
    speed: 1.0,
    comp: 0.60,
    glow: 0.65,
    aspect: '16:9',
    bass: 0.15,
    mid: 0.12,
    high: 0.10,
    beat: 0.0,
    hasImg: false,
    imgAspect: 1.0
  };

  const PALETTES = [
    [[0.04, 0.05, 0.11], [0.65, 0.70, 1.00], [1.00, 0.42, 0.21]], // 0: Ember
    [[0.05, 0.03, 0.11], [0.55, 0.34, 1.00], [1.00, 0.32, 0.68]], // 1: Dusk
    [[0.03, 0.04, 0.06], [0.88, 0.91, 0.98], [0.46, 0.54, 0.68]], // 2: Silver
    [[0.06, 0.04, 0.02], [1.00, 0.78, 0.30], [0.98, 0.34, 0.14]], // 3: Gold
    [[0.02, 0.06, 0.06], [0.16, 0.94, 0.66], [0.36, 0.74, 1.00]]  // 4: Emerald
  ];

  const VS = `attribute vec2 p; void main(){ gl_Position = vec4(p, 0.0, 1.0); }`;

  const FS = `
    precision highp float;
    uniform vec2 u_res;
    uniform float u_time, u_bass, u_mid, u_high, u_beat, u_seed, u_comp, u_glow;
    uniform float u_hasImg, u_imgAspect;
    uniform int u_style;
    uniform vec3 u_c0, u_c1, u_c2;
    uniform sampler2D u_audioTex;
    uniform sampler2D u_imgTex;

    float hash21(vec2 p) {
      p = fract(p * vec2(234.34, 435.345));
      p += dot(p, p + 34.23);
      return fract(p.x * p.y);
    }

    vec2 hash22(vec2 p) {
      return fract(sin(vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)))) * 43758.5453);
    }

    float noise(vec2 p) {
      vec2 i = floor(p), f = fract(p);
      f = f * f * (3.0 - 2.0 * f);
      return mix(
        mix(hash21(i), hash21(i + vec2(1.0, 0.0)), f.x),
        mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), f.x),
        f.y
      );
    }

    float fbm(vec2 p) {
      float v = 0.0, a = 0.5;
      mat2 m = mat2(0.80, -0.60, 0.60, 0.80);
      for (int i = 0; i < 5; i++) {
        v += a * noise(p);
        p = m * p * 2.02 + vec2(17.1, 31.7);
        a *= 0.5;
      }
      return v;
    }

    mat2 rot(float a) { float c = cos(a), s = sin(a); return mat2(c, -s, s, c); }

    float fft(float x) {
      return texture2D(u_audioTex, vec2(clamp(x, 0.0, 1.0), 0.25)).r;
    }

    float smin(float a, float b, float k) {
      float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
      return mix(b, a, h) - k * h * (1.0 - h);
    }

    // Cover-fit UV sampling with optional chromatic RGB split
    vec2 coverUV(vec2 fragUV) {
      float canvasAspect = u_res.x / max(1.0, u_res.y);
      vec2 u = fragUV - 0.5;
      if (canvasAspect > u_imgAspect) u.y *= u_imgAspect / canvasAspect;
      else u.x *= canvasAspect / u_imgAspect;
      return clamp(u + 0.5, 0.001, 0.999);
    }

    vec3 sampleImgRGB(vec2 imgUV, vec2 chroma) {
      float r = texture2D(u_imgTex, clamp(imgUV + chroma, 0.001, 0.999)).r;
      float g = texture2D(u_imgTex, clamp(imgUV, 0.001, 0.999)).g;
      float b = texture2D(u_imgTex, clamp(imgUV - chroma, 0.001, 0.999)).b;
      return vec3(r, g, b);
    }

    vec3 aces(vec3 x) {
      const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
      return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
    }

    // ---- 0: Eclipse (3D SDF Raymarching + refractive glass orb over image) ----
    float mapEclipse(vec3 p, float t) {
      vec3 q = p;
      q.xz *= rot(t * 0.45 + u_seed);
      q.xy *= rot(t * 0.28);
      float disp = sin(q.x * (3.5 + u_comp * 4.0) + t * 2.0)
                 * sin(q.y * (3.5 + u_comp * 4.0) - t * 1.6)
                 * sin(q.z * (3.5 + u_comp * 4.0) + t * 1.2);
      float core = length(q) - (0.72 + 0.18 * u_bass + 0.09 * u_beat + disp * (0.09 + 0.12 * u_mid));
      vec3 tr = q;
      tr.yz *= rot(0.9 + sin(t * 0.5) * 0.4);
      vec2 tq = vec2(length(tr.xz) - (1.18 + 0.12 * u_bass), tr.y);
      float ring = length(tq) - (0.055 + 0.04 * u_high);
      vec3 s1 = q - vec3(sin(t * 1.1) * 1.15, cos(t * 0.9) * 0.65, cos(t * 1.1) * 1.15);
      vec3 s2 = q + vec3(cos(t * 0.8) * 1.05, sin(t * 1.3) * 0.75, sin(t * 0.8) * 1.05);
      float d = smin(core, length(s1) - 0.24 - 0.08 * u_mid, 0.35);
      d = smin(d, length(s2) - 0.20 - 0.06 * u_high, 0.35);
      return smin(d, ring, 0.18);
    }

    vec3 renderEclipse(vec2 uv, vec2 imgUV, float t) {
      vec3 ro = vec3(0.0, 0.0, 3.15);
      vec3 rd = normalize(vec3(uv, -1.65));
      float d = 0.0, minD = 1e4;
      bool hit = false;
      for (int i = 0; i < 46; i++) {
        vec3 p = ro + rd * d;
        float h = mapEclipse(p, t);
        minD = min(minD, h);
        if (h < 0.0025) { hit = true; break; }
        if (d > 7.5) break;
        d += h * 0.85;
      }

      float r = length(uv);
      float ang = atan(uv.y, uv.x);
      float spec = fft(abs(ang) / 3.14159);
      vec3 bg = u_hasImg > 0.5
        ? sampleImgRGB(imgUV, uv * (0.006 + 0.018 * u_beat))
        : u_c0 * (0.65 + 0.5 * smoothstep(1.2, 0.0, r));
      float corona = (0.025 + 0.055 * u_glow) / (minD + 0.028);
      vec3 col = bg + mix(u_c1, u_c2, 0.5 + 0.5 * sin(ang * 2.0 + t + spec * 3.0)) * corona * (0.28 + 0.55 * u_bass);
      float specRing = smoothstep(0.018, 0.0, abs(r - (0.56 + spec * 0.22 * u_comp)));
      col += mix(u_c1, u_c2, spec) * specRing * (0.40 + 0.7 * u_glow);

      if (hit) {
        vec3 p = ro + rd * d;
        vec2 e = vec2(0.003, 0.0);
        vec3 n = normalize(vec3(
          mapEclipse(p + e.xyy, t) - mapEclipse(p - e.xyy, t),
          mapEclipse(p + e.yxy, t) - mapEclipse(p - e.yxy, t),
          mapEclipse(p + e.yyx, t) - mapEclipse(p - e.yyx, t)
        ));
        vec3 v = -rd;
        float fres = pow(1.0 - max(dot(n, v), 0.0), 3.0);
        vec3 l1 = normalize(vec3(1.2, 1.4, 1.5));
        float diff1 = max(dot(n, l1), 0.0);
        float spec1 = pow(max(dot(reflect(-l1, n), v), 0.0), 36.0);
        vec3 irid = 0.5 + 0.5 * cos(6.2831 * (fres * 1.4 + t * 0.18 + vec3(0.0, 0.33, 0.67)));
        if (u_hasImg > 0.5) {
          vec3 refr = sampleImgRGB(imgUV + n.xy * (0.14 + 0.08 * u_bass), n.xy * 0.018);
          col = mix(refr, irid, fres * 0.65) + vec3(1.0) * spec1 * 0.95;
        } else {
          vec3 base = mix(u_c0 * 1.4, mix(u_c1, u_c2, diff1), 0.65);
          col = base * (0.35 + 0.75 * diff1) + mix(u_c1, irid, 0.65) * fres * (1.2 + u_glow) + vec3(1.0) * spec1 * 1.15;
        }
      }
      return col;
    }

    // ---- 1: Chrome (Domain-Warped FBM Liquid Metal / Liquid Image Warp) ----
    float fluidH(vec2 p, float t) {
      vec2 q = vec2(
        fbm(p + t * 0.22 + u_seed),
        fbm(p + vec2(5.2, 1.3) - t * 0.18)
      );
      vec2 r = vec2(
        fbm(p + 3.6 * q + vec2(1.7, 9.2) + t * 0.15 + u_bass * 0.45),
        fbm(p + 3.6 * q + vec2(8.3, 2.8) - t * 0.14 + u_mid * 0.40)
      );
      return fbm(p + (2.4 + u_comp * 2.2) * r);
    }

    vec3 renderChrome(vec2 uv, vec2 imgUV, float t) {
      vec2 p = uv * (1.8 + u_comp * 1.8);
      float eps = 0.015;
      float h  = fluidH(p, t);
      float hx = fluidH(p + vec2(eps, 0.0), t);
      float hy = fluidH(p + vec2(0.0, eps), t);
      vec3 n = normalize(vec3((h - hx) / eps, (h - hy) / eps, 1.8));
      vec3 l1 = normalize(vec3(0.7, 0.9, 1.2));
      float d1 = max(dot(n, l1), 0.0);
      float s1 = pow(max(dot(reflect(-l1, n), vec3(0.0, 0.0, 1.0)), 0.0), 42.0);
      float fres = pow(1.0 - max(n.z, 0.0), 2.5);
      if (u_hasImg > 0.5) {
        vec2 warp = n.xy * (0.045 + 0.08 * u_bass + 0.05 * u_comp);
        vec3 img = sampleImgRGB(imgUV + warp, n.xy * (0.01 + 0.02 * u_beat));
        return img + mix(vec3(1.0), u_c1, 0.4) * s1 * (0.65 + u_glow) + mix(u_c1, u_c2, h) * fres * 0.35;
      }
      float band = fft(clamp(h, 0.0, 1.0));
      vec3 col = mix(u_c0, u_c1, smoothstep(0.22, 0.78, h));
      col = mix(col, u_c2, smoothstep(0.55, 0.95, h + u_mid * 0.25));
      col *= (0.45 + 0.85 * d1);
      col += mix(vec3(1.0), u_c1, 0.35) * s1 * (1.25 + u_beat * 1.4);
      col += mix(u_c1, u_c2, band) * fres * (0.8 + u_glow * 0.9);
      return col;
    }

    // ---- 2: Caustics (FormulaNode Interference Waves + Dichroic Prismatic Caustics) ----
    vec3 renderCaustics(vec2 uv, vec2 imgUV, float t) {
      float v = 0.0;
      vec2 grad = vec2(0.0);
      float freq = 14.0 + u_comp * 28.0;
      for (int i = 0; i < 6; i++) {
        float fi = float(i);
        vec2 src = (0.38 + 0.14 * u_bass) * vec2(
          cos(fi * 1.7 + t * 0.35 + u_seed),
          sin(fi * 2.3 - t * 0.28 + u_seed * 0.7)
        );
        vec2 d = uv - src;
        float dist = length(d) + 1e-4;
        float ph = dist * freq - t * (2.6 + u_mid * 1.8) + fi * 1.1;
        v += sin(ph);
        grad += (d / dist) * cos(ph);
      }
      v /= 6.0;
      grad /= 6.0;
      float ridge = pow(max(0.0, 1.0 - abs(v) * 1.8), 3.0);
      float spec = fft(clamp(abs(v), 0.0, 1.0));
      vec3 prism = 0.5 + 0.5 * cos(6.2831 * (v * 0.85 + t * 0.12 + vec3(0.0, 0.33, 0.67)));
      if (u_hasImg > 0.5) {
        vec2 refr = grad * (0.025 + 0.045 * u_bass);
        vec3 img = sampleImgRGB(imgUV + refr, grad * (0.012 + 0.016 * u_glow));
        return img + mix(u_c1, prism, 0.65) * ridge * (0.55 + u_glow * 0.8 + u_beat * 0.6);
      }
      vec3 col = mix(u_c0, mix(u_c1, u_c2, 0.5 + 0.5 * v), 0.68);
      col += mix(u_c2, prism, 0.6) * ridge * (0.95 + u_glow * 1.2 + spec * 0.8);
      return col;
    }

    // ---- 3: Julia (FormulaNode Audio-Reactive Smooth-Escape Julia Fractal) ----
    vec3 renderJulia(vec2 uv, vec2 imgUV, float t) {
      vec2 z = uv * (2.4 - u_comp * 0.85) * rot(t * 0.08);
      vec2 c = vec2(
        -0.76 + sin(t * 0.32 + u_seed) * (0.14 + 0.06 * u_bass),
         0.16 + cos(t * 0.27 + u_seed) * (0.14 + 0.06 * u_mid)
      );
      float it = 0.0;
      float minOrbit = 1e4;
      for (int i = 0; i < 64; i++) {
        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        float m2 = dot(z, z);
        minOrbit = min(minOrbit, m2);
        if (m2 > 16.0) break;
        it += 1.0;
      }
      float smoothIt = it - log2(max(1.0, log2(max(dot(z, z), 1.001))));
      float norm = clamp(smoothIt / 42.0, 0.0, 1.0);
      float trap = exp(-minOrbit * (2.5 + u_high * 3.0));
      if (u_hasImg > 0.5) {
        vec2 warp = normalize(z + 1e-4) * norm * (0.05 + 0.08 * u_bass);
        vec3 img = sampleImgRGB(imgUV + warp, warp * 0.35);
        return mix(img, mix(u_c1, u_c2, norm), trap * 0.55) + u_c2 * trap * u_glow * 0.65;
      }
      vec3 col = mix(u_c0, u_c1, smoothstep(0.05, 0.55, norm));
      col = mix(col, u_c2, smoothstep(0.45, 0.95, norm + trap * 0.4));
      col += mix(vec3(1.0), u_c2, 0.5) * trap * (0.65 + u_glow + u_beat * 0.8);
      return col;
    }

    // ---- 4: Voronoi (FormulaNode Crystalline Cells & Glowing Fissures) ----
    vec3 renderVoronoi(vec2 uv, vec2 imgUV, float t) {
      vec2 q = uv * (3.5 + u_comp * 7.5);
      vec2 ip = floor(q), fp = fract(q);
      float d1 = 8.0, d2 = 8.0;
      vec2 bestCell = vec2(0.0), bestOff = vec2(0.0);
      for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
          vec2 g = vec2(float(x), float(y));
          vec2 o = hash22(ip + g);
          o = 0.5 + 0.44 * sin(t * (0.7 + u_mid * 0.6) + 6.2831 * o + u_seed);
          vec2 diff = g + o - fp;
          float d = length(diff);
          if (d < d1) { d2 = d1; d1 = d; bestCell = ip + g; bestOff = diff; }
          else if (d < d2) { d2 = d; }
        }
      }
      float edge = smoothstep(0.09 + 0.06 * u_bass, 0.0, d2 - d1);
      float cellHash = hash21(bestCell);
      float spec = fft(cellHash);
      if (u_hasImg > 0.5) {
        vec2 cellUV = imgUV + bestOff * (0.04 + 0.06 * u_bass);
        vec3 img = sampleImgRGB(cellUV, bestOff * 0.015);
        return img * (0.85 + 0.35 * spec) + mix(u_c1, u_c2, cellHash) * edge * (0.9 + u_glow + u_beat);
      }
      vec3 cellCol = mix(u_c0 * 1.4, mix(u_c1, u_c2, cellHash), 0.35 + 0.65 * spec);
      vec3 col = cellCol * (1.0 - d1 * 0.55);
      col += mix(u_c2, vec3(1.0), 0.4) * edge * (1.1 + u_glow * 1.2 + u_beat * 0.9);
      return col;
    }

    // ---- 5: Gasket (FormulaNode Apollonian Circle-Inversion Fractal) ----
    vec3 renderGasket(vec2 uv, vec2 imgUV, float t) {
      vec2 z = uv * rot(t * 0.14) * (1.15 + u_comp * 1.35);
      float scale = 1.0;
      float best = 1e9;
      float kBase = 1.15 + 0.25 * sin(t * 0.45 + u_seed) + 0.18 * u_bass;
      for (int i = 0; i < 8; i++) {
        z = -1.0 + 2.0 * fract(0.5 * z + 0.5);
        float k = kBase / max(dot(z, z), 0.05);
        z *= k;
        scale *= k;
        best = min(best, min(abs(z.x), abs(z.y)) / scale);
      }
      float line = smoothstep(0.014 + 0.01 * u_glow, 0.0, best);
      float glow = 0.008 / (best + 0.006);
      float band = fft(fract(log(scale) * 0.18));
      if (u_hasImg > 0.5) {
        vec2 invUV = fract(z * 0.25 + 0.5);
        vec3 img = sampleImgRGB(mix(imgUV, invUV, 0.45 + 0.25 * u_bass), vec2(0.006 * u_beat));
        return img + mix(u_c1, u_c2, band) * (line + glow * 0.15) * (0.65 + u_glow);
      }
      vec3 col = mix(u_c0, mix(u_c1, u_c2, fract(log(scale) * 0.22 + t * 0.1)), 0.35 + 0.5 * band);
      col *= (0.4 + 0.6 * smoothstep(0.08, 0.0, best));
      col += mix(u_c1, u_c2, band) * (line * 1.1 + glow * 0.22 * (0.5 + u_glow + u_beat * 0.8));
      return col;
    }

    // ---- 6: Kaleido (FormulaNode Polar-Folded Mirror Symmetry) ----
    vec3 renderKaleido(vec2 uv, vec2 imgUV, float t) {
      float a = atan(uv.y, uv.x) + t * 0.18;
      float r = length(uv);
      float seg = 6.2831853 / floor(4.0 + u_comp * 10.0);
      a = abs(mod(a + seg * 0.5, seg) - seg * 0.5);
      vec2 fp = vec2(cos(a), sin(a)) * r;
      if (u_hasImg > 0.5) {
        vec2 kuv = fract(fp * rot(t * 0.22) * (1.0 + 0.25 * u_bass) + 0.5);
        vec3 img = sampleImgRGB(kuv, fp * (0.012 + 0.025 * u_beat));
        float ring = smoothstep(0.02, 0.0, abs(r - (0.35 + 0.2 * fft(a / seg))));
        return img + mix(u_c1, u_c2, r) * ring * (0.6 + u_glow);
      }
      vec2 q = fp * (3.0 + u_comp * 5.0) + vec2(t * 0.35, -t * 0.25) + u_seed;
      float v = sin(q.x * 3.0 + sin(q.y * 2.5 + t)) + cos(length(q) * 4.0 - t * 2.0 + u_bass * 3.0);
      float spec = fft(clamp(r, 0.0, 1.0));
      vec3 col = mix(u_c0, u_c1, smoothstep(-1.4, 0.6, v));
      col = mix(col, u_c2, smoothstep(0.3, 1.6, v + spec * 0.8));
      float rib = exp(-abs(v) * (4.5 - u_glow * 2.0));
      col += mix(u_c2, vec3(1.0), 0.35) * rib * (0.85 + u_beat);
      return col * smoothstep(1.15, 0.05, r);
    }

    // ---- 7: Dither (8x8 Ordered Bayer Brutalist / Ditherpunk Image FX) ----
    float bayer8(vec2 p) {
      vec2 q = mod(floor(p), 8.0);
      float b = 0.0, v = 32.0;
      for (int i = 0; i < 3; i++) {
        vec2 bit = mod(floor(q / pow(2.0, float(2 - i))), 2.0);
        float idx = bit.x * 2.0 + bit.y;
        float m = (idx < 0.5) ? 0.0 : (idx < 1.5) ? 2.0 : (idx < 2.5) ? 3.0 : 1.0;
        b += m * v / 4.0;
        v *= 0.25;
      }
      return b / 64.0;
    }

    vec3 renderDither(vec2 uv, vec2 imgUV, float t) {
      float px = 2.0 + (1.0 - u_comp) * 3.5;
      vec2 q = floor(gl_FragCoord.xy / px);
      float thresh = bayer8(q);
      float lum;
      if (u_hasImg > 0.5) {
        vec2 snapUV = coverUV((q * px) / u_res);
        vec3 img = sampleImgRGB(snapUV, vec2(0.005 * u_beat, 0.0));
        lum = dot(img, vec3(0.299, 0.587, 0.114)) + u_bass * 0.15;
      } else {
        float r = length(uv);
        float a = atan(uv.y, uv.x);
        float spec = fft(abs(a) / 3.14159);
        vec2 wp = uv * rot(t * 0.25 + u_seed);
        float waveField = sin(wp.x * 6.0 + t * 1.8) * cos(wp.y * 6.0 - t * 1.4);
        float ring = smoothstep(0.14, 0.0, abs(r - (0.36 + 0.14 * spec + 0.08 * u_beat)));
        lum = clamp(0.45 - r * 0.55 + waveField * 0.28 + ring * 0.85 + u_bass * 0.22, 0.0, 1.0);
      }
      float d1 = step(thresh, lum);
      float d2 = step(thresh, max(0.0, lum - 0.38));
      vec3 col = mix(u_c0, u_c1, d1);
      return mix(col, u_c2, d2 * (0.75 + 0.25 * sin(t + u_mid * 4.0)));
    }

    void main() {
      vec2 fragUV = gl_FragCoord.xy / u_res;
      vec2 uv = (gl_FragCoord.xy - 0.5 * u_res) / min(u_res.x, u_res.y);
      vec2 imgUV = coverUV(vec2(fragUV.x, 1.0 - fragUV.y));
      float t = u_time;

      vec3 col;
      if (u_style == 0) col = renderEclipse(uv, imgUV, t);
      else if (u_style == 1) col = renderChrome(uv, imgUV, t);
      else if (u_style == 2) col = renderCaustics(uv, imgUV, t);
      else if (u_style == 3) col = renderJulia(uv, imgUV, t);
      else if (u_style == 4) col = renderVoronoi(uv, imgUV, t);
      else if (u_style == 5) col = renderGasket(uv, imgUV, t);
      else if (u_style == 6) col = renderKaleido(uv, imgUV, t);
      else col = renderDither(uv, imgUV, t);

      float vig = smoothstep(1.5, 0.25, length(uv));
      col *= mix(0.74, 1.04, vig);
      col = aces(col);
      float grain = (hash21(gl_FragCoord.xy + fract(t) * 137.0) - 0.5) * 0.04 * u_glow;
      gl_FragColor = vec4(clamp(col + grain, 0.0, 1.0), 1.0);
    }
  `;

  function compileShader(type, src) {
    const s = gl.createShader(type);
    gl.shaderSource(s, src);
    gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
      console.error('Shader error:', gl.getShaderInfoLog(s));
    }
    return s;
  }

  let prog, loc = {}, audioTex, imgTex;
  const AUDIO_BINS = 128;
  const audioTexData = new Uint8Array(AUDIO_BINS * 2);

  if (gl) {
    prog = gl.createProgram();
    gl.attachShader(prog, compileShader(gl.VERTEX_SHADER, VS));
    gl.attachShader(prog, compileShader(gl.FRAGMENT_SHADER, FS));
    gl.linkProgram(prog);
    gl.useProgram(prog);

    const buf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, buf);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]), gl.STATIC_DRAW);
    const pLoc = gl.getAttribLocation(prog, 'p');
    gl.enableVertexAttribArray(pLoc);
    gl.vertexAttribPointer(pLoc, 2, gl.FLOAT, false, 0, 0);

    ['u_res', 'u_time', 'u_bass', 'u_mid', 'u_high', 'u_beat', 'u_seed', 'u_comp', 'u_glow',
      'u_hasImg', 'u_imgAspect', 'u_style', 'u_c0', 'u_c1', 'u_c2', 'u_audioTex', 'u_imgTex']
      .forEach((k) => { loc[k] = gl.getUniformLocation(prog, k); });

    // Unit 0: Audio FFT + waveform texture
    audioTex = gl.createTexture();
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, audioTex);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.LUMINANCE, AUDIO_BINS, 2, 0, gl.LUMINANCE, gl.UNSIGNED_BYTE, audioTexData);
    gl.uniform1i(loc.u_audioTex, 0);

    // Unit 1: User uploaded image texture
    imgTex = gl.createTexture();
    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D, imgTex);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array([0, 0, 0, 255]));
    gl.uniform1i(loc.u_imgTex, 1);
  }

  // ---------------- Image Upload ----------------
  const clearImgBtn = $('btn-clear-img');
  function loadImageFile(file) {
    if (!file || !gl) return;
    const img = new Image();
    img.onload = () => {
      state.hasImg = true;
      state.imgAspect = (img.naturalWidth || 1) / Math.max(1, img.naturalHeight || 1);
      gl.activeTexture(gl.TEXTURE1);
      gl.bindTexture(gl.TEXTURE_2D, imgTex);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, img);
      if (clearImgBtn) clearImgBtn.hidden = false;
      hudPill.hidden = false;
      hudText.textContent = file.name.slice(0, 22);
    };
    img.src = URL.createObjectURL(file);
  }

  $('img-file')?.addEventListener('change', (e) => loadImageFile(e.target.files[0]));
  clearImgBtn?.addEventListener('click', () => {
    state.hasImg = false;
    clearImgBtn.hidden = true;
    if (!audioActive) hudPill.hidden = true;
  });

  // ---------------- Web Audio + Infinite WASM Beat Generator ----------------
  let actx, analyser, destNode, freqData, waveData;
  let audioEl, mediaSrc, micStream, wasmSrc;
  let audioActive = false;
  let prevBass = 0;

  let worker = null, engineInfo = null, jobId = 0;
  const jobs = {};
  function askWasm(msg) {
    if (!worker) {
      worker = new Worker('../gen/gen-worker.js?v=9');
      worker.onmessage = (e) => {
        const j = jobs[e.data.id];
        delete jobs[e.data.id];
        if (j) (e.data.error ? j[1](new Error(e.data.error)) : j[0](e.data));
      };
    }
    return new Promise((ok, no) => { msg.id = ++jobId; jobs[msg.id] = [ok, no]; worker.postMessage(msg); });
  }

  function ensureAudio() {
    if (!actx) {
      actx = new (window.AudioContext || window.webkitAudioContext)();
      analyser = actx.createAnalyser();
      analyser.fftSize = 256;
      analyser.smoothingTimeConstant = 0.80;
      freqData = new Uint8Array(analyser.frequencyBinCount);
      waveData = new Uint8Array(analyser.frequencyBinCount);
      destNode = actx.createMediaStreamDestination();
      analyser.connect(actx.destination);
      analyser.connect(destNode);
    }
    if (actx.state === 'suspended') actx.resume();
  }

  function setBtnLabel(id, text) {
    const el = $(id);
    const sp = el?.querySelector('span');
    if (sp) sp.textContent = text;
    else if (el) el.textContent = text;
  }

  function stopAllAudio() {
    audioActive = false;
    if (wasmSrc) { try { wasmSrc.stop(); } catch (_) {} wasmSrc = null; }
    if (audioEl) { audioEl.pause(); }
    if (micStream) { micStream.getTracks().forEach((t) => t.stop()); micStream = null; }
    setBtnLabel('btn-demo', 'Beat');
    $('btn-demo').classList.remove('is-active');
    $('btn-mic').classList.remove('is-active');
    if (!state.hasImg) hudPill.hidden = true;
  }

  const BEAT_STYLES = ['deep_house', 'synthwave', 'electro', 'lofi_beats', 'detroit_techno', 'liquid_dnb', 'dub_techno', 'downtempo'];

  async function generateInfiniteBeat() {
    ensureAudio();
    if (wasmSrc) {
      stopAllAudio();
      return;
    }
    stopAllAudio();
    const btn = $('btn-demo');
    btn.disabled = true;
    setBtnLabel('btn-demo', 'Making...');
    hudPill.hidden = false;
    hudPill.classList.add('is-busy');
    hudText.textContent = 'Making beat...';
    try {
      if (!engineInfo) engineInfo = await askWasm({ cmd: 'info' });
      const pickName = BEAT_STYLES[Math.floor(Math.random() * BEAT_STYLES.length)];
      let styleIdx = engineInfo.styles.indexOf(pickName);
      if (styleIdx < 0) styleIdx = Math.floor(Math.random() * engineInfo.styles.length);
      const r = await askWasm({
        cmd: 'run',
        style: styleIdx,
        part: 0,
        seed: Math.floor(Math.random() * 99999) + 1,
        bars: 2,
        key: -1,
        scale: -1,
        tone: 0, energy: 0.25, space: 0.15, mood: 0, variation: 0.55, bpm: 0, pinned: false
      });
      const buf = actx.createBuffer(2, r.frames, r.rate);
      const L = buf.getChannelData(0), R = buf.getChannelData(1);
      for (let i = 0; i < r.frames; i++) { L[i] = r.pcm[i * 2]; R[i] = r.pcm[i * 2 + 1]; }
      wasmSrc = actx.createBufferSource();
      wasmSrc.buffer = buf;
      wasmSrc.loop = true;
      wasmSrc.connect(analyser);
      wasmSrc.start();
      audioActive = true;
      const nice = (engineInfo.styles[styleIdx] || 'infinite').replace(/_/g, ' ');
      setBtnLabel('btn-demo', 'Stop');
      btn.classList.add('is-active');
      hudText.textContent = `${nice} · ${Math.round(r.bpm)} bpm`;
    } catch (_) {
      setBtnLabel('btn-demo', 'Beat');
      if (!state.hasImg) hudPill.hidden = true;
    } finally {
      btn.disabled = false;
      hudPill.classList.remove('is-busy');
    }
  }

  $('btn-demo').addEventListener('click', generateInfiniteBeat);

  $('btn-mic').addEventListener('click', async () => {
    if (micStream) {
      stopAllAudio();
      return;
    }
    try {
      ensureAudio();
      stopAllAudio();
      micStream = await navigator.mediaDevices.getUserMedia({ audio: true });
      const src = actx.createMediaStreamSource(micStream);
      src.connect(analyser);
      audioActive = true;
      $('btn-mic').classList.add('is-active');
      hudPill.hidden = false;
      hudText.textContent = 'Mic live';
    } catch (_) {}
  });

  function loadAudioFile(file) {
    if (!file) return;
    ensureAudio();
    stopAllAudio();
    if (!audioEl) {
      audioEl = new Audio();
      audioEl.loop = true;
      mediaSrc = actx.createMediaElementSource(audioEl);
      mediaSrc.connect(analyser);
    }
    audioEl.src = URL.createObjectURL(file);
    audioEl.play();
    audioActive = true;
    hudPill.hidden = false;
    hudText.textContent = file.name.slice(0, 24);
  }

  $('audio-file').addEventListener('change', (e) => loadAudioFile(e.target.files[0]));
  const dropEl = $('drop-audio');
  dropEl.addEventListener('dragover', (e) => { e.preventDefault(); });
  dropEl.addEventListener('drop', (e) => {
    e.preventDefault();
    const f = e.dataTransfer.files[0];
    if (!f) return;
    if (f.type.startsWith('image/')) loadImageFile(f);
    else if (f.type.startsWith('audio/')) loadAudioFile(f);
  });

  // ---------------- Controls & Slider Fill Sync ----------------
  function setSliderFill(el) {
    if (!el) return;
    const min = parseFloat(el.min) || 0, max = parseFloat(el.max) || 1, v = parseFloat(el.value);
    el.style.setProperty('--p', (((v - min) / (max - min || 1)) * 100).toFixed(2) + '%');
  }

  function setChips(containerId, attr, cb) {
    const box = $(containerId);
    box.querySelectorAll('.st-chip').forEach((btn) => {
      btn.addEventListener('click', () => {
        box.querySelectorAll('.st-chip').forEach((b) => b.classList.remove('is-active'));
        btn.classList.add('is-active');
        cb(btn.getAttribute(attr), btn);
      });
    });
  }

  $('sel-style').addEventListener('change', (e) => { state.style = parseInt(e.target.value, 10); });
  setChips('pal-chips', 'data-pal', (v) => { state.pal = parseInt(v, 10); });
  setChips('aspect-chips', 'data-aspect', (v) => {
    state.aspect = v;
    stageBox.setAttribute('data-aspect', v);
    if (v === '16:9') { canvas.width = 1280; canvas.height = 720; }
    else if (v === '9:16') { canvas.width = 720; canvas.height = 1280; }
    else { canvas.width = 1080; canvas.height = 1080; }
  });

  [['ctrl-react', 'val-react', 'react'], ['ctrl-speed', 'val-speed', 'speed'], ['ctrl-comp', 'val-comp', 'comp'], ['ctrl-glow', 'val-glow', 'glow']]
    .forEach(([id, vid, key]) => {
      const el = $(id), vEl = $(vid);
      setSliderFill(el);
      el.addEventListener('input', () => {
        state[key] = parseFloat(el.value);
        vEl.textContent = state[key].toFixed(2);
        setSliderFill(el);
      });
    });

  $('btn-generate').addEventListener('click', () => {
    state.style = Math.floor(Math.random() * 8);
    state.pal = Math.floor(Math.random() * 5);
    state.seed = +(Math.random() * 20).toFixed(2);
    state.comp = +(0.30 + Math.random() * 0.65).toFixed(2);
    state.glow = +(0.35 + Math.random() * 0.60).toFixed(2);
    $('sel-style').value = String(state.style);
    document.querySelectorAll('#pal-chips .st-chip').forEach((b, i) => b.classList.toggle('is-active', i === state.pal));
    $('ctrl-comp').value = state.comp; $('val-comp').textContent = state.comp.toFixed(2); setSliderFill($('ctrl-comp'));
    $('ctrl-glow').value = state.glow; $('val-glow').textContent = state.glow.toFixed(2); setSliderFill($('ctrl-glow'));
  });

  $('btn-fs').addEventListener('click', () => {
    if (document.fullscreenElement) document.exitFullscreen();
    else stageBox.requestFullscreen?.();
  });

  // ---------------- Video, PNG & .inf Exports ----------------
  let recorder, chunks = [];
  const recBtn = $('btn-rec');
  recBtn.addEventListener('click', () => {
    if (recorder && recorder.state === 'recording') {
      recorder.stop();
      setBtnLabel('btn-rec', 'Record');
      recBtn.classList.remove('is-active');
      hudPill.classList.remove('is-rec');
      if (!audioActive && !state.hasImg) hudPill.hidden = true;
      return;
    }
    ensureAudio();
    const stream = canvas.captureStream(60);
    if (destNode && destNode.stream.getAudioTracks().length) {
      destNode.stream.getAudioTracks().forEach((t) => stream.addTrack(t));
    }
    chunks = [];
    const mime = MediaRecorder.isTypeSupported('video/webm;codecs=vp9') ? 'video/webm;codecs=vp9' : 'video/webm';
    recorder = new MediaRecorder(stream, { mimeType: mime, videoBitsPerSecond: 10_000_000 });
    recorder.ondataavailable = (e) => { if (e.data.size) chunks.push(e.data); };
    recorder.onstop = () => {
      const blob = new Blob(chunks, { type: 'video/webm' });
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = `infinite-visualizer-${state.aspect.replace(':', 'x')}.webm`;
      a.click();
    };
    recorder.start();
    setBtnLabel('btn-rec', 'Stop');
    recBtn.classList.add('is-active');
    hudPill.hidden = false;
    hudPill.classList.add('is-rec');
    hudText.textContent = 'Recording...';
  });

  $('btn-png').addEventListener('click', () => {
    const a = document.createElement('a');
    a.href = canvas.toDataURL('image/png');
    a.download = 'infinite-visual.png';
    a.click();
  });

  $('btn-inf').addEventListener('click', () => {
    const patch = [
      '# Made with Infinite Visualizer (https://infinitedevices.org/visualizer/), by Naman Soni (n1m21n)',
      'infinite-patch 1',
      'node 1 Audio Audio In',
      '  id audio',
      'end',
      'node 2 Visuals Field Pixel',
      '  id visual',
      `  f uReact ${state.react}`,
      `  f uSpeed ${state.speed}`,
      `  f uComplexity ${state.comp}`,
      `  f uGlow ${state.glow}`,
      `  f uSeed ${state.seed}`,
      'end',
      'node 3 Output Video Out',
      '  id out',
      'end',
      'cable 1 1 2 1',
      'cable 2 1 3 1',
      ''
    ].join('\n');
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([patch], { type: 'text/plain' }));
    a.download = 'infinite-visualizer.inf';
    a.click();
  });

  // ---------------- Render Loop ----------------
  let simT = 0;
  function frame(now) {
    simT = now * 0.001 * state.speed;

    if (audioActive && analyser && freqData && waveData) {
      analyser.getByteFrequencyData(freqData);
      analyser.getByteTimeDomainData(waveData);
      let b = 0, m = 0, h = 0;
      for (let i = 0; i < 10; i++) b += freqData[i];
      for (let i = 10; i < 42; i++) m += freqData[i];
      for (let i = 42; i < 100; i++) h += freqData[i];
      const rawBass = (b / (10 * 255)) * state.react;
      const rawMid = (m / (32 * 255)) * state.react;
      const rawHigh = (h / (58 * 255)) * state.react;
      const delta = Math.max(0, rawBass - prevBass);
      prevBass = rawBass;
      state.beat = Math.max(state.beat * 0.86, Math.min(1.5, delta * 3.8));
      state.bass += (rawBass - state.bass) * 0.28;
      state.mid += (rawMid - state.mid) * 0.22;
      state.high += (rawHigh - state.high) * 0.22;

      for (let i = 0; i < AUDIO_BINS; i++) {
        audioTexData[i] = Math.min(255, Math.round(freqData[i] * state.react));
        audioTexData[AUDIO_BINS + i] = waveData[i];
      }
    } else {
      state.bass = 0.22 + 0.14 * Math.pow(0.5 + 0.5 * Math.sin(simT * 2.5), 2.0);
      state.mid = 0.16 + 0.10 * Math.cos(simT * 1.7);
      state.high = 0.12 + 0.08 * Math.sin(simT * 3.4);
      state.beat = Math.pow(Math.max(0, Math.sin(simT * 2.5)), 6.0) * 0.65;
      for (let i = 0; i < AUDIO_BINS; i++) {
        const f = i / AUDIO_BINS;
        const spec = Math.exp(-f * 3.2) * (0.4 + 0.45 * Math.sin(simT * 2.5 - f * 9.0));
        audioTexData[i] = Math.max(0, Math.min(255, Math.round(spec * 180)));
        const w = 128 + Math.round(Math.sin(f * 18.0 + simT * 4.5) * 32 * (0.5 + state.bass));
        audioTexData[AUDIO_BINS + i] = Math.max(0, Math.min(255, w));
      }
    }

    for (let i = 0; i < meterBars.length; i++) {
      const v = audioTexData[i * 12] / 255;
      meterBars[i].style.height = Math.max(3, Math.round(v * 16)) + 'px';
    }

    if (gl && prog) {
      gl.viewport(0, 0, canvas.width, canvas.height);
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, audioTex);
      gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, AUDIO_BINS, 2, gl.LUMINANCE, gl.UNSIGNED_BYTE, audioTexData);

      const pal = PALETTES[state.pal];
      gl.uniform2f(loc.u_res, canvas.width, canvas.height);
      gl.uniform1f(loc.u_time, simT);
      gl.uniform1f(loc.u_bass, state.bass);
      gl.uniform1f(loc.u_mid, state.mid);
      gl.uniform1f(loc.u_high, state.high);
      gl.uniform1f(loc.u_beat, state.beat);
      gl.uniform1f(loc.u_seed, state.seed);
      gl.uniform1f(loc.u_comp, state.comp);
      gl.uniform1f(loc.u_glow, state.glow);
      gl.uniform1f(loc.u_hasImg, state.hasImg ? 1.0 : 0.0);
      gl.uniform1f(loc.u_imgAspect, state.imgAspect);
      gl.uniform1i(loc.u_style, state.style);
      gl.uniform3fv(loc.u_c0, pal[0]);
      gl.uniform3fv(loc.u_c1, pal[1]);
      gl.uniform3fv(loc.u_c2, pal[2]);
      gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
    }

    requestAnimationFrame(frame);
  }

  requestAnimationFrame(frame);
})();
