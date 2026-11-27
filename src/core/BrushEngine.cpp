#include "BrushEngine.h"
#include "GLUtil.h"

#include "gl3.h"
#include <algorithm>
#include <cmath>

namespace BrushEngine
{
   namespace
   {
      // One dab, drawn scissored to its own square and blended into the
      // coverage buffer (ONE, ONE_MINUS_SRC_ALPHA): cov = a + cov (1 - a).
      const char* kDabFrag =
         "#version 150\n"
         "out vec4 fragColor;\n"
         "uniform vec2 uCenter;\n"
         "uniform float uRadius;\n"
         "uniform float uHard;\n"
         "uniform float uFlow;\n"
         "uniform int uTip;\n"
         "uniform float uSeed;\n"
         "uniform float uAngle;\n"
         "uniform float uRound;\n"
         "uniform int uPencil;\n"
         "uniform vec3 uCol;\n"
         "uniform vec2 uDir;\n"
         "uniform float uAlong;\n"
         "uniform int uMat;\n"
         "uniform int uProfile;\n"
         "uniform float uDepth;\n"
         "uniform vec3 uLight;\n"
         "uniform int uGrain;\n"
         "uniform float uGrainScale;\n"
         "uniform float uGrainDepth;\n"
         "uniform int uGrainMode;\n"
         "uniform int uDual;\n"
         "uniform float uDualScale;\n"
         "uniform vec2 uDualOff;\n"
         "uniform vec3 uColB;\n"
         "uniform int uColAlong;\n"
         "uniform float uColPeriod;\n"
         "uniform sampler2D uCarry;\n"
         "uniform int uWet;\n"
         "uniform sampler2D uTipTex;\n"
         "uniform int uHasTipTex;\n"
         "uniform sampler2D uGrainTex;\n"
         "uniform int uHasGrainTex;\n"
         "uniform vec2 uGrainTexSize;\n"
         "uniform sampler2D uColTex;\n"
         "uniform int uHasColTex;\n"
         "uniform vec2 uSurfSize;\n"
         "uniform int uWrap;\n"
         "float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7)) + uSeed) * 43758.5453); }\n"
         "float h1(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n"
         "float vn(vec2 p) { vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
         "   return mix(mix(h1(i), h1(i + vec2(1.0, 0.0)), f.x), mix(h1(i + vec2(0.0, 1.0)), h1(i + vec2(1.0, 1.0)), f.x), f.y); }\n"
         "float rnd(float k) { return fract(sin(uSeed * 12.9898 + k * 78.233) * 43758.5453); }\n"
         "vec2 turn(vec2 p, float t) { float c = cos(t), s = sin(t); return vec2(c * p.x + s * p.y, -s * p.x + c * p.y); }\n"
         "// One tip's mask at p (the tip's own frame, px from its centre), fc the surface pixel.\n"
         "float Tip(int tip, vec2 p, vec2 fc, float rad, float hard) {\n"
         "   float r = length(p) / rad;\n"
         "   float aa = min(1.0, 1.0 / rad);\n"
         // Hard edge on a soft round or square tip: fully on wherever the soft tip would show at all, so it keeps the
         // soft tip's reach (PhotoCraft replace_anti_alias_tests); a hard tip is the plain aliased disc or square.
         "   if (uPencil == 1) {\n"
         "      if ((tip == 0 || tip == 1 || tip == 3) && hard < 1.0 - aa) {\n"
         "         float m = tip == 3 ? max(abs(p.x), abs(p.y)) / rad : r;\n"
         // "Would show": at least half an 8-bit level (1/510), the smallest change a soft dab leaves in the picture.
         "         return step(0.002, 1.0 - smoothstep(hard, 1.0, m));\n"
         "      }\n"
         "      return (tip == 3) ? step(max(abs(p.x), abs(p.y)), rad) : step(length(p), rad);\n"
         "   }\n"
         "   if (tip == 0 || tip == 1) return 1.0 - smoothstep(min(hard, 1.0 - aa), 1.0, r);\n"
         "   if (tip == 2) return 1.0 - smoothstep(min(0.94, 1.0 - aa), 1.0, r);\n"
         "   if (tip == 3) { float m = max(abs(p.x), abs(p.y)) / rad; return 1.0 - smoothstep(min(hard, 1.0 - aa), 1.0, m); }\n"
         "   if (tip == 4) return (1.0 - smoothstep(0.0, 1.0, r)) * step(0.55, hash(floor(fc)));\n"
         "   if (tip == 5) { float n = hash(floor(fc * 0.5)) * 0.55 + 0.45; return (1.0 - smoothstep(hard * 0.6, 1.0, r)) * n; }\n"
         "   if (tip == 6) { float n = hash(floor(fc * 0.125)) * 0.35; return 1.0 - smoothstep(hard - n, 1.0 + n, r); }\n"
         "   float disc = 1.0 - smoothstep(max(hard, 0.55), 1.0, r);\n"
         "   if (tip == 7) {   // bristle: hair lines along the stroke\n"
         "      float y = p.y / rad;\n"
         "      float hair = h1(vec2(floor(y * 16.0), 3.0)) * 0.55 + vn(vec2(y * 7.0, 0.5)) * 0.45;\n"
         "      return (1.0 - smoothstep(0.7, 1.0, r)) * smoothstep(0.38, 0.62, hair);\n"
         "   }\n"
         "   if (tip == 8) {   // paper grain: the tooth stays put on the page\n"
         "      float g = vn(fc * 0.35) * 0.6 + vn(fc * 0.9) * 0.4;\n"
         "      return (1.0 - smoothstep(hard * 0.8, 1.0, r)) * smoothstep(0.3, 0.72, g);\n"
         "   }\n"
         "   if (tip == 9) {   // halftone: dots on a fixed grid grow with coverage\n"
         "      float cov = 1.0 - smoothstep(hard * 0.7, 1.0, r);\n"
         "      float d = length(fract(fc / 9.0) - 0.5);\n"
         "      float R = sqrt(cov) * 0.62;\n"
         "      return (1.0 - smoothstep(R - 0.07, R + 0.03, d)) * step(0.02, cov);\n"
         "   }\n"
         "   if (tip == 10) {  // watercolour: ragged edge, pigment pooled at the rim\n"
         "      float n = vn(fc * 0.05 + uSeed) * 0.5 + vn(fc * 0.2) * 0.5;\n"
         "      float rr = r * (0.82 + 0.36 * n);\n"
         "      return (1.0 - smoothstep(0.86, 1.0, rr)) * (0.3 + 0.7 * smoothstep(0.5, 0.95, rr)) * (0.75 + 0.25 * vn(fc * 0.6));\n"
         "   }\n"
         "   if (tip == 11) {  // brushed metal: fine streaks along the stroke\n"
         "      float s = h1(vec2(floor(p.y * 1.3), 7.0)) * 0.6 + vn(vec2(p.y * 0.5, 1.0)) * 0.4;\n"
         "      return disc * (0.25 + 0.75 * s * s);\n"
         "   }\n"
         "   if (tip == 12) {  // cloth: an over-under weave fixed on the page\n"
         "      vec2 w = fc / 6.0;\n"
         "      float chk = mod(floor(w.x) + floor(w.y), 2.0);\n"
         "      float thread = mix(0.5 + 0.5 * sin(w.y * 6.2832), 0.5 + 0.5 * sin(w.x * 6.2832), chk);\n"
         "      return (1.0 - smoothstep(hard * 0.8, 1.0, r)) * (0.25 + 0.75 * thread) * (0.85 + 0.15 * vn(fc * 0.5));\n"
         "   }\n"
         "   if (tip == 13) {  // leaf: a turned, sized leaf with a vein per dab\n"
         "      vec2 q = turn(p, rnd(1.0) * 6.2832) / (rad * (0.55 + 0.45 * rnd(2.0)));\n"
         "      float wdt = 0.42 * (1.0 - q.x * q.x);\n"
         "      float a = (1.0 - smoothstep(wdt - 0.05, wdt, abs(q.y))) * step(abs(q.x), 1.0);\n"
         "      a *= 1.0 - 0.4 * (1.0 - smoothstep(0.0, 0.04, abs(q.y))) * step(q.x, 0.85);\n"
         "      return a * (0.75 + 0.25 * rnd(3.0));\n"
         "   }\n"
         "   if (tip == 14) {  // grass: a blade rising from the dab, leaning and bending\n"
         "      vec2 q = turn(p, (rnd(1.0) - 0.5) * 0.9) / rad;\n"
         "      float y = q.y / (0.6 + 0.4 * rnd(2.0));\n"
         "      float bend = (rnd(3.0) - 0.5) * 0.7 * y * y;\n"
         "      float wdt = 0.1 * (1.0 - y) + 0.005;\n"
         "      return step(0.0, y) * step(y, 1.0) * (1.0 - smoothstep(wdt * 0.6, wdt, abs(q.x - bend)));\n"
         "   }\n"
         "   if (tip == 15) {  // sparkle: a four-point star with a soft core\n"
         "      vec2 q = turn(p, rnd(1.0) * 1.5708) / (rad * (0.35 + 0.65 * rnd(2.0)));\n"
         "      float d = sqrt(abs(q.x)) + sqrt(abs(q.y));\n"
         "      return max(1.0 - smoothstep(0.8, 1.0, d), 0.6 * (1.0 - smoothstep(0.0, 0.3, length(q))));\n"
         "   }\n"
         "   // sponge: open cells that change every dab\n"
         "   float c = vn(fc * 0.12 + uSeed * 3.1) * 0.65 + vn(fc * 0.33) * 0.35;\n"
         "   return (1.0 - smoothstep(0.7, 1.0, r)) * smoothstep(0.42, 0.56, c);\n"
         "}\n"
         "// The canvas texture: sampled at the surface pixel, so it stays put under every dab and every stroke.\n"
         "float Grain(vec2 fc) {\n"
         "   if (uHasGrainTex == 1) {\n"
         "      vec4 t = texture(uGrainTex, fract(fc / max(uGrainTexSize * uGrainScale, vec2(1.0))));\n"
         "      return dot(t.rgb, vec3(0.299, 0.587, 0.114)) * t.a;\n"
         "   }\n"
         "   vec2 g = fc / max(uGrainScale, 0.05);\n"
         "   if (uGrain == 1) return vn(g * 0.35) * 0.6 + vn(g * 0.9) * 0.4;\n"
         "   if (uGrain == 2) {\n"
         "      vec2 w = g / 6.0;\n"
         "      float chk = mod(floor(w.x) + floor(w.y), 2.0);\n"
         "      return mix(0.5 + 0.5 * sin(w.y * 6.2832), 0.5 + 0.5 * sin(w.x * 6.2832), chk);\n"
         "   }\n"
         "   return h1(floor(g)) * 0.5 + vn(g * 0.5) * 0.5;\n"
         "}\n"
         "vec3 hueTurn(vec3 c, float t) {\n"
         "   const vec3 k = vec3(0.57735);\n"
         "   float cs = cos(t), sn = sin(t);\n"
         "   return c * cs + cross(k, c) * sn + k * dot(k, c) * (1.0 - cs);\n"
         "}\n"
         "void main() {\n"
         "   vec2 fc = gl_FragCoord.xy;\n"
         "   vec2 w = fc - uCenter;\n"
         "   if (uWrap == 1) w -= uSurfSize * floor(w / uSurfSize + 0.5);   // the nearest copy of the dab on the tiled page\n"
         "   float c = cos(uAngle), s = sin(uAngle);\n"
         "   vec2 p = vec2(c * w.x + s * w.y, -s * w.x + c * w.y);\n"
         "   p.y /= max(uRound, 0.05);\n"
         "   float rad = max(uRadius, 0.5);\n"
         "   float a;\n"
         "   if (uHasTipTex == 1) {\n"
         "      vec2 uv = p / (2.0 * rad) + 0.5;\n"
         "      vec4 t = texture(uTipTex, uv);\n"
         "      float inside = step(0.0, uv.x) * step(uv.x, 1.0) * step(0.0, uv.y) * step(uv.y, 1.0);\n"
         "      a = dot(t.rgb, vec3(0.299, 0.587, 0.114)) * t.a * inside;\n"
         "   } else\n"
         "      a = Tip(uTip, p, fc, rad, uHard);\n"
         "   if (uDual >= 0) {\n"
         "      float r2 = max(rad * uDualScale, 0.5);\n"
         "      a *= Tip(uDual, w - uDualOff, fc, r2, 0.5);\n"
         "   }\n"
         "   float g = 1.0;\n"
         "   if (uGrain > 0) {\n"
         "      g = Grain(fc);\n"
         "      if (uGrainMode == 0) a *= mix(1.0, g, uGrainDepth);\n"
         "      else if (uGrainMode == 1) a = max(0.0, a - uGrainDepth * (1.0 - g));\n"
         "   }\n"
         "   // The stroke frame: across runs -1..1 over the width, along is px down the stroke.\n"
         "   vec2 perp = vec2(-uDir.y, uDir.x);\n"
         "   float across = clamp(dot(w, perp) / rad, -1.0, 1.0);\n"
         "   float along = uAlong + dot(w, uDir);\n"
         "   vec3 base = uCol;\n"
         "   if (uColAlong == 1) {\n"
         "      float t = fract(along / max(uColPeriod, 1.0));\n"
         "      base = mix(uCol, uColB, 1.0 - abs(2.0 * t - 1.0));\n"
         "   } else if (uColAlong == 2) base = mix(uCol, uColB, across * 0.5 + 0.5);\n"
         "   if (uHasColTex == 1) base = texture(uColTex, fc / max(uSurfSize, vec2(1.0))).rgb;\n"
         "   if (uWet == 1) { vec4 cw = texture(uCarry, vec2(0.5)); if (cw.a < 0.5) discard; base = cw.rgb; }\n"
         "   if (uMat >= 0) {\n"
         "      // Height across the stroke and its slope (per unit of across).\n"
         "      float x = across, sl = 0.0;\n"
         "      if (uProfile == 1) { float h = sqrt(max(1.0 - x * x, 0.0)); sl = -x / max(h, 0.2); }\n"
         "      else if (uProfile == 2) { sl = 3.14159 * cos(3.14159 * abs(x)) * sign(x); }\n"
         "      else if (uProfile == 3) { sl = 1.6 * sin(along * 6.2832 / max(rad * 8.0, 4.0)); }\n"
         "      // Fibre tips: each fibre has its own ridge and a small value offset, so light lands between them.\n"
         "      if (uTip == 7 || uTip == 11 || uTip == 12) {\n"
         "         float u = (x * 0.5 + 0.5) * 14.0;\n"
         "         float fib = h1(vec2(floor(u), 5.0));\n"
         "         sl += -(fract(u) - 0.5) * 2.4;\n"
         "         base *= 0.82 + 0.36 * fib;\n"
         "      }\n"
         "      vec2 grad = perp * sl * uDepth;\n"
         "      if (uGrain > 0 && uGrainMode == 2) {\n"
         "         float gx = Grain(fc + vec2(1.0, 0.0)) - Grain(fc - vec2(1.0, 0.0));\n"
         "         float gy = Grain(fc + vec2(0.0, 1.0)) - Grain(fc - vec2(0.0, 1.0));\n"
         "         grad += vec2(gx, gy) * 4.0 * uGrainDepth;\n"
         "      }\n"
         "      vec3 n = normalize(vec3(-grad, 1.0));\n"
         "      vec3 L = normalize(uLight);\n"
         "      vec3 Hv = normalize(L + vec3(0.0, 0.0, 1.0));\n"
         "      float diff = max(dot(n, L), 0.0), nh = max(dot(n, Hv), 0.0);\n"
         "      vec3 col;\n"
         "      if (uMat == 0) col = base * (0.35 + 0.75 * diff) + vec3(0.22) * pow(nh, 18.0);\n"
         "      else if (uMat == 1) col = base * (0.18 + 0.32 * diff) + mix(base, vec3(1.0), 0.35) * pow(nh, 42.0) * 1.3;\n"
         "      else if (uMat == 2) {\n"
         "         vec3 rf = reflect(vec3(0.0, 0.0, -1.0), n);\n"
         "         float t = dot(normalize(rf.xy + 1e-4), normalize(L.xy + 1e-4)) * length(rf.xy);\n"
         "         float sky = mix(0.06, 1.0, smoothstep(-0.25, 0.55, t)) - 0.35 * smoothstep(0.0, 0.08, abs(t - 0.2)) + 0.35;\n"
         "         col = mix(vec3(sky), base * sky, 0.25) + vec3(1.0) * pow(nh, 160.0);\n"
         "      } else if (uMat == 3) {\n"
         "         float lit = clamp(0.25 + 0.85 * diff, 0.0, 1.0);\n"
         "         vec2 f = fract(fc / 3.0) - 0.5;\n"
         "         a *= step(lit, h1(floor(fc / 3.0))) * (1.0 - smoothstep(0.3, 0.5, length(f)));\n"
         "         col = base;\n"
         "      } else if (uMat == 4) {   // satin: a sheen stretched along the threads (the stroke's heading)\n"
         "         vec3 T = vec3(uDir, 0.0);\n"
         "         float th = dot(T, Hv);\n"
         "         float aniso = pow(sqrt(max(1.0 - th * th, 0.0)), 24.0);\n"
         "         col = base * (0.3 + 0.6 * diff) + mix(base, vec3(1.0), 0.5) * aniso * 0.7;\n"
         "      } else {                  // pearl: the hue turns with the surface's lean\n"
         "         float ph = dot(n.xy, normalize(L.xy + 1e-4)) * 2.2 + along * 0.004;\n"
         "         col = hueTurn(base, ph) * (0.45 + 0.55 * diff) + vec3(0.9, 0.95, 1.0) * pow(nh, 60.0) * 0.8;\n"
         "      }\n"
         "      base = clamp(col, 0.0, 1.0);\n"
         "   }\n"
         "   a = clamp(a, 0.0, 1.0) * uFlow;\n"
         // Dither only partial coverage: full coverage must stay exactly opaque (a hard dab, a bucket fill).
         "   if (a > 1e-4 && a < 1.0) {\n"
         "      float d = (h1(fc + uSeed) + h1(fc * 1.37 + uSeed + 7.3) - 1.0) / 255.0;\n"
         "      a = clamp(a + d, 0.0, 1.0);\n"
         "   }\n"
         "   fragColor = vec4(base * a, a);\n"
         "}\n";

      // Smudge: the pixels under the dab take the colour from where the last dab sat (copied into uSrc first).
      const char* kSmudgeFrag =
         "#version 150\n"
         "out vec4 fragColor;\n"
         "uniform sampler2D uSrc;\n"
         "uniform sampler2D uSel;\n"
         "uniform int uHasSel;\n"
         "uniform vec2 uTexel;\n"
         "uniform vec2 uCenter;\n"
         "uniform vec2 uOff;\n"
         "uniform float uRadius;\n"
         "uniform float uHard;\n"
         "uniform float uStrength;\n"
         "uniform int uLock;\n"
         "uniform sampler2D uComp;\n"   // Sample All Layers: the picture as it stood, under the layer's own paint
         "uniform int uHasComp;\n"
         "uniform int uSmudgeMode;\n"   // 0 normal, 1 darken, 2 lighten, 3 hue, 4 saturation, 5 color, 6 luminosity
         "float lumOf(vec3 c) { return dot(c, vec3(0.3, 0.59, 0.11)); }\n"
         "vec3 clipC(vec3 c) { float l = lumOf(c); float n = min(c.r, min(c.g, c.b)); float x = max(c.r, max(c.g, c.b));\n"
         "   if (n < 0.0) c = l + (c - l) * l / max(l - n, 1e-5); if (x > 1.0) c = l + (c - l) * (1.0 - l) / max(x - l, 1e-5); return c; }\n"
         "vec3 setLum(vec3 c, float l) { return clipC(c + (l - lumOf(c))); }\n"
         "float satOf(vec3 c) { return max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b)); }\n"
         "vec3 setSat(vec3 c, float s) { float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b));\n"
         "   return mx > mn ? (c - mn) * s / (mx - mn) : vec3(0.0); }\n"
         "vec3 smBlend(vec3 b, vec3 s) {\n"   // b the pixel as it was, s the smudged colour (the separable / non-separable modes)
         "   if (uSmudgeMode == 1) return min(b, s);\n"
         "   if (uSmudgeMode == 2) return max(b, s);\n"
         "   if (uSmudgeMode == 3) return setLum(setSat(s, satOf(b)), lumOf(b));\n"
         "   if (uSmudgeMode == 4) return setLum(setSat(b, satOf(s)), lumOf(b));\n"
         "   if (uSmudgeMode == 5) return setLum(s, lumOf(b));\n"
         "   if (uSmudgeMode == 6) return setLum(b, lumOf(s));\n"
         "   return s; }\n"
         "void main() {\n"
         "   vec2 uv = gl_FragCoord.xy * uTexel;\n"
         "   vec4 d = texture(uSrc, uv);\n"
         "   vec4 s = texture(uSrc, uv + uOff * uTexel);\n"
         "   if (uHasComp == 1) { vec4 c = texture(uComp, uv + uOff * uTexel); float oa = s.a + c.a * (1.0 - s.a);\n"
         "      s = vec4((s.rgb * s.a + c.rgb * c.a * (1.0 - s.a)) / max(oa, 1e-4), oa); }\n"
         "   float r = length(gl_FragCoord.xy - uCenter) / max(uRadius, 0.5);\n"
         "   float m = (1.0 - smoothstep(uHard, 1.0, r)) * uStrength;\n"
         "   if (uHasSel == 1) m *= texture(uSel, uv).r;\n"
         "   float a = mix(d.a, s.a, m);\n"
         "   vec3 rgb = mix(d.rgb * d.a, s.rgb * s.a, m) / max(a, 1e-4);\n"
         "   if (a <= 1e-4) rgb = d.rgb;\n"
         "   vec3 n = uLock == 1 ? mix(d.rgb, s.rgb, m * s.a) : rgb;\n"
         "   if (uSmudgeMode > 0 && d.a > 0.0) n = mix(n, smBlend(d.rgb, n), d.a);\n"
         "   fragColor = uLock == 1 ? vec4(n, d.a) : vec4(n, a);\n"
         "}\n";

      // The surface with the stroke's coverage applied, straight alpha.
      const char* kMergeFrag =
         "#version 150\n"
         "in vec2 vUv;\n"
         "out vec4 fragColor;\n"
         "uniform sampler2D uSurf;\n"
         "uniform sampler2D uCov;\n"
         "uniform sampler2D uSel;\n"
         "uniform int uHasSel;\n"
         "uniform vec3 uColor;\n"
         "uniform float uOpacity;\n"
         "uniform int uMode;\n"
         "uniform int uLock;\n"
         "uniform vec2 uTexel;\n"
         "uniform vec2 uOff;\n"
         "uniform float uSpread;\n"
         "uniform int uRange;\n"
         "uniform int uProtectHue;\n"
         "uniform int uSharpMode;\n"
         "uniform int uSrcBlend;\n"
         "uniform int uWetEdges;\n"
         "uniform float uWetReach;\n"
         "uniform float uAccum;\n"
         "uniform vec2 uAnchor;\n"
         "uniform mat2 uSrcM;\n"
         "uniform int uSponge;\n"
         "uniform vec3 uKey;\n"
         "uniform float uKeyTol;\n"
         "uniform int uProtectFg;\n"
         "uniform int uDither;\n"   // 8-bit soft paint: noise under one level, so a low-flow falloff does not band
         "uniform int uFinal;\n"
         "uniform sampler2D uSrcTex;\n"
         "uniform int uHasSrc;\n"
         "uniform int uGrain;\n"
         "uniform float uGrainScale;\n"
         "uniform float uGrainDepth;\n"
         "uniform int uGrainMode;\n"
         "uniform sampler2D uGrainTex;\n"
         "uniform int uHasGrainTex;\n"
         "uniform vec2 uGrainTexSize;\n"
         "uniform float uPatScale;\n"
         "uniform vec2 uPatPhase;\n"
         "float h1(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n"
         "float vn(vec2 p) { vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
         "   return mix(mix(h1(i), h1(i + vec2(1.0, 0.0)), f.x), mix(h1(i + vec2(0.0, 1.0)), h1(i + vec2(1.0, 1.0)), f.x), f.y); }\n"
         // The same canvas texture as the dab shader's: on the whole stroke, so overlapping dabs do not fill it in.
         "float Grain(vec2 fc) {\n"
         "   if (uHasGrainTex == 1) { vec4 t = texture(uGrainTex, fract(fc / max(uGrainTexSize * uGrainScale, vec2(1.0))));\n"
         "      return dot(t.rgb, vec3(0.299, 0.587, 0.114)) * t.a; }\n"
         "   vec2 g = fc / max(uGrainScale, 0.05);\n"
         "   if (uGrain == 1) return vn(g * 0.35) * 0.6 + vn(g * 0.9) * 0.4;\n"
         "   if (uGrain == 2) { vec2 w = g / 6.0; float chk = mod(floor(w.x) + floor(w.y), 2.0);\n"
         "      return mix(0.5 + 0.5 * sin(w.y * 6.2832), 0.5 + 0.5 * sin(w.x * 6.2832), chk); }\n"
         "   return h1(floor(g)) * 0.5 + vn(g * 0.5) * 0.5;\n"
         "}\n"
         "uniform int uWrap;\n"
         // Bilinear with wrapped texel indices: the source reads across the seam, as the tiled page would.
         "vec4 wrapTex(sampler2D t, vec2 uv) {\n"
         "   vec2 sz = vec2(textureSize(t, 0)); vec2 p = uv * sz - 0.5; vec2 i = floor(p); vec2 f = p - i;\n"
         "   ivec2 a = ivec2(mod(i, sz)), b = ivec2(mod(i + 1.0, sz));\n"
         "   return mix(mix(texelFetch(t, a, 0), texelFetch(t, ivec2(b.x, a.y), 0), f.x),\n"
         "              mix(texelFetch(t, ivec2(a.x, b.y), 0), texelFetch(t, b, 0), f.x), f.y);\n"
         "}\n"
         "vec4 srcAt(vec2 uv) {\n"
         "   if (uWrap == 1) return uHasSrc == 1 ? wrapTex(uSrcTex, uv) : wrapTex(uSurf, uv);\n"
         "   return uHasSrc == 1 ? texture(uSrcTex, uv) : texture(uSurf, uv);\n"
         "}\n"
         "vec3 softSrc(vec2 uv) {\n"
         "   vec3 s = vec3(0.0); float w = 0.0;\n"
         "   for (int i = -2; i <= 2; i++) for (int j = -2; j <= 2; j++) {\n"
         "      float k = exp(-float(i * i + j * j) * 0.25);\n"
         "      s += srcAt(uv + vec2(float(i), float(j)) * uTexel * uSpread).rgb * k; w += k;\n"
         "   }\n"
         "   return s / w;\n"
         "}\n"
         "vec3 soft(vec2 uv) {\n"
         "   vec3 s = vec3(0.0); float w = 0.0;\n"
         "   for (int i = -2; i <= 2; i++) for (int j = -2; j <= 2; j++) {\n"
         "      float k = exp(-float(i * i + j * j) * 0.25);\n"
         "      s += texture(uSurf, uv + vec2(float(i), float(j)) * uTexel * uSpread).rgb * k; w += k;\n"
         "   }\n"
         "   return s / w;\n"
         "}\n"
         // Protect Detail (PhotoCraft local_sharpen, Apache 2.0): a sigma 2.5 Gaussian and a soft limit on the
         // difference, so an edge gets a low, wide halo and a lone pixel a gentle lift.
         "vec3 gaussPD(sampler2D t, vec2 uv) {\n"
         "   vec3 s = vec3(0.0); float w = 0.0;\n"
         "   for (int i = -8; i <= 8; i++) for (int j = -8; j <= 8; j++) {\n"
         "      float k = exp(-float(i * i + j * j) / 12.5);\n"
         "      s += texture(t, uv + vec2(float(i), float(j)) * uTexel).rgb * k; w += k;\n"
         "   }\n"
         "   return s / w;\n"
         "}\n"
         "vec3 protectPD(vec3 v, vec3 bl) { vec3 d = v - bl; return v + d * (6.0 / 255.0) / (6.0 / 255.0 + abs(d)); }\n"
         "float lum(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }\n"
         // Keep d's hue and saturation, take r's lightness.
         "vec3 keepHue(vec3 d, vec3 r) { float ld = lum(d), lr = lum(r);\n"
         "   if (ld < 1e-4) return vec3(lr); vec3 o = d * (lr / ld); float m = max(o.r, max(o.g, o.b));\n"
         "   return m > 1.0 ? mix(vec3(lr), o, (1.0 - lr) / max(m - lr, 1e-4)) : o; }\n"
         "vec3 cloneBlend(vec3 b, vec3 s) {\n"
         "   if (uSrcBlend == 1) return b * s;\n"
         "   if (uSrcBlend == 2) return 1.0 - (1.0 - b) * (1.0 - s);\n"
         "   if (uSrcBlend == 3) return mix(2.0 * b * s, 1.0 - 2.0 * (1.0 - b) * (1.0 - s), step(0.5, b));\n"
         "   if (uSrcBlend == 4) return min(b, s);\n"
         "   if (uSrcBlend == 5) return max(b, s);\n"
         "   if (uSrcBlend == 6) return abs(b - s);\n"
         "   return s; }\n"
         "void main() {\n"
         "   vec4 d = texture(uSurf, vUv);\n"
         "   vec4 c = texture(uCov, vUv);\n"
         "   float cov = c.a;\n"
         "   if (uAccum < 0.999) cov = 1.0 - pow(1.0 - clamp(cov, 0.0, 0.9999), mix(0.2, 1.0, uAccum));\n"
         "   if (uWetEdges == 1 && cov > 1e-4) {\n"   // the middle thins, the rim keeps its paint
         "      vec2 o = uTexel * uWetReach; float m = min(min(texture(uCov, vUv + vec2(o.x, 0.0)).a, texture(uCov, vUv - vec2(o.x, 0.0)).a),\n"
         "         min(texture(uCov, vUv + vec2(0.0, o.y)).a, texture(uCov, vUv - vec2(0.0, o.y)).a));\n"
         "      cov *= 0.45 + 0.55 * clamp(1.0 - m / cov, 0.0, 1.0);\n"
         "   }\n"
         "   float a = clamp(cov * uOpacity, 0.0, 1.0);\n"
         "   vec3 col = c.a > 1e-5 ? clamp(c.rgb / c.a, 0.0, 1.0) : uColor;\n"
         "   if (uHasSel == 1) a *= texture(uSel, vUv).r;\n"
         "   if (uGrain > 0 && uGrainMode < 2) {\n"
         "      float g = Grain(gl_FragCoord.xy);\n"
         "      a = uGrainMode == 0 ? a * mix(1.0, g, uGrainDepth) : max(0.0, a - uGrainDepth * (1.0 - g));\n"
         "   }\n"
         "   if (a <= 0.0) { fragColor = d; return; }\n"
         "   if ((uMode >= 6 && uMode <= 12) || uMode >= 22) {\n"
         "      vec2 px = vUv / uTexel;\n"
         "      vec2 src = (uAnchor + uOff + uSrcM * (px - uAnchor)) * uTexel;\n"
         "      if (uMode == 10) { vec4 s = srcAt(src); if (uSrcBlend > 0) s.rgb = mix(s.rgb, cloneBlend(d.rgb, s.rgb), d.a);\n"
         "         fragColor = mix(d, s, a); return; }\n"
         "      if (uMode == 29) { vec2 ts = vec2(textureSize(uSrcTex, 0)); ivec2 q = ivec2(mod(floor((px + uPatPhase) / uPatScale), ts));\n"
         "         vec4 s = uHasSrc == 1 ? texelFetch(uSrcTex, q, 0) : vec4(uColor, 1.0);\n"   // pattern stamp: the tile, repeated
         "         if (uSrcBlend == 7) { if (uLock == 1) { fragColor = d; return; } float k = a * (1.0 - d.a); float oa = d.a + k;\n"   // Behind
         "            fragColor = oa > 0.0 ? vec4((d.rgb * d.a + s.rgb * k) / oa, oa) : d; return; }\n"
         "         if (uSrcBlend > 0) s.rgb = mix(s.rgb, cloneBlend(d.rgb, s.rgb), d.a);\n"   // the other modes paint over the pixels
         "         fragColor = uLock == 1 ? vec4(mix(d.rgb, s.rgb, a), d.a) : mix(d, s, a); return; }\n"
         "      if (uMode == 24) { vec4 s = srcAt(vUv); if (uSrcBlend > 0) s.rgb = mix(s.rgb, cloneBlend(d.rgb, s.rgb), d.a);\n"
         "         fragColor = uLock == 1 ? vec4(mix(d.rgb, s.rgb, a), d.a) : mix(d, s, a); return; }\n"   // undo brush: the earlier state
         "      if (uMode == 26) { float tol = uKeyTol;\n"   // background erase; Protect Foreground narrows the tolerance
         "         if (uProtectFg == 1) { float lim = distance(uKey, uColor); if (lim <= 1e-4) { fragColor = d; return; } tol = min(tol, lim); }\n"
         "         float k = 1.0 - smoothstep(tol * 0.6, tol, distance(d.rgb, uKey));\n"
         "         fragColor = vec4(d.rgb, d.a * (1.0 - a * k)); return; }\n"
         "      if (uMode == 28) { fragColor = uFinal == 1 ? d : vec4(mix(d.rgb, vec3(1.0, 0.25, 0.3), a * 0.55), max(d.a, a * 0.55)); return; }\n"
         "      if ((uMode == 6 || uMode == 7) && uHasSrc == 1) {\n"   // Sample All Layers: the picture's blur / sharpen lands here
         "         vec4 b = srcAt(vUv); vec3 bl = softSrc(vUv), o = bl;\n"
         "         if (uMode == 7) { if (uSharpMode == 3) o = protectPD(b.rgb, gaussPD(uSrcTex, vUv));\n"
         "            else if (uSharpMode == 0) { float l = lum(b.rgb); o = b.rgb + (b.rgb - bl) * 2.0 * (1.0 - abs(2.0 * l - 1.0)); }\n"
         "            else o = b.rgb + (b.rgb - bl) * (uSharpMode == 2 ? 3.5 : 1.5); o = clamp(o, 0.0, 1.0); }\n"
         "         vec4 s = vec4(o, b.a);\n"
         "         fragColor = uLock == 1 ? vec4(mix(d.rgb, s.rgb, a), d.a) : mix(d, s, a); return; }\n"
         "      vec3 r = d.rgb;\n"
         "      if (uMode == 6) r = soft(vUv);\n"
         "      if (uMode == 22) { float l = lum(d.rgb); float sat = max(d.r, max(d.g, d.b)) - min(d.r, min(d.g, d.b));\n"
         "         r = clamp(mix(vec3(l), d.rgb, uSponge == 0 ? 1.6 : uSponge == 1 ? 0.35 : 1.0 + 0.8 * (1.0 - sat)), 0.0, 1.0);\n"
         "         if (uProtectHue == 1) r = keepHue(d.rgb, r); }\n"
         "      if (uMode == 23) {\n"   // median of the 3x3 around, spread with the brush
         "         vec3 v[9]; int n = 0;\n"
         "         for (int i = -1; i <= 1; i++) for (int j = -1; j <= 1; j++) { v[n] = texture(uSurf, vUv + vec2(float(i), float(j)) * uTexel * uSpread).rgb; n++; }\n"
         "         for (int i = 0; i < 9; i++) for (int j = 0; j < 8; j++) { vec3 lo = min(v[j], v[j + 1]), hi = max(v[j], v[j + 1]); v[j] = lo; v[j + 1] = hi; }\n"
         "         r = v[4];\n"
         "      }\n"
         "      if (uMode == 25) { float k = 1.0 - smoothstep(uKeyTol * 0.6, uKeyTol, distance(d.rgb, uKey));\n"   // colour replace
         "         r = mix(d.rgb, keepHue(uColor, d.rgb), k); }\n"
         "      if (uMode == 27) { float red = d.r - max(d.g, d.b);\n"   // red eye: red pupils go dark grey
         "         float k = smoothstep(0.05, 0.25, red) * step(0.25, d.r); float g = (d.g + d.b) * 0.5;\n"
         "         r = mix(d.rgb, vec3(g * 0.8, d.g * 0.9, d.b * 0.9), k); }\n"
         "      if (uMode == 7) {\n"   // clarity: local contrast, midtones only; unsharp mask; harsh: strong and wide
         "         vec3 bl = soft(vUv);\n"
         "         if (uSharpMode == 3) r = protectPD(d.rgb, gaussPD(uSurf, vUv));\n"
         "         else if (uSharpMode == 0) { float l = lum(d.rgb); r = d.rgb + (d.rgb - bl) * 2.0 * (1.0 - abs(2.0 * l - 1.0)); }\n"
         "         else r = d.rgb + (d.rgb - bl) * (uSharpMode == 2 ? 3.5 : 1.5);\n"
         "         r = clamp(r, 0.0, 1.0);\n"
         "      }\n"
         "      if (uMode == 8) r = pow(max(d.rgb, 0.0), vec3(0.7));\n"
         "      if (uMode == 9) r = pow(max(d.rgb, 0.0), vec3(1.45));\n"
         "      if (uMode == 11) r = clamp(srcAt(src).rgb + soft(vUv) - softSrc(src), 0.0, 1.0);\n"
         "      if (uMode == 8 || uMode == 9) {\n"   // dodge / burn reach shadows, midtones or highlights
         "         float l = dot(d.rgb, vec3(0.299, 0.587, 0.114));\n"
         "         float w = uRange == 0 ? 1.0 - smoothstep(0.15, 0.6, l) : uRange == 2 ? smoothstep(0.4, 0.85, l) : 1.0;\n"
         "         if (uProtectHue == 1) r = keepHue(d.rgb, r);\n"
         "         r = mix(d.rgb, r, w);\n"
         "      }\n"
         "      fragColor = vec4(mix(d.rgb, r, a), d.a);\n"
         "      return;\n"
         "   }\n"
         "   if (uMode == 13) {\n"   // stack: the stroke's height lands on top of what is there
         "      float oa = min(1.0, d.a + a);\n"
         "      fragColor = vec4(mix(d.rgb, col, a / max(oa, 1e-4)), oa);\n"
         "      return;\n"
         "   }\n"
         "   if (uMode == 14) { fragColor = vec4(d.rgb, d.a * (1.0 - 0.7 * a)); return; }\n"   // press: dents
         "   if (uMode == 3) { fragColor = vec4(d.rgb, d.a * (1.0 - a)); return; }\n"
         "   if (uMode == 1) { fragColor = uLock == 1 ? d : vec4(d.rgb, d.a * (1.0 - a)); return; }\n"
         "   if (uMode == 2) {\n"
         "      if (uLock == 1) { fragColor = d; return; }\n"
         "      float oa = d.a + a * (1.0 - d.a);\n"
         "      vec3 rgb = (d.rgb * d.a + col * a * (1.0 - d.a)) / max(oa, 1e-4);\n"
         "      fragColor = vec4(rgb, oa);\n"
         "      return;\n"
         "   }\n"
         "   vec3 src = col;\n"
         "   if (uMode == 4) src = mix(col, col * d.rgb, d.a);\n"
         "   if (uMode == 5) src = mix(col, 1.0 - (1.0 - col) * (1.0 - d.rgb), d.a);\n"
         "   if (uLock == 1) { fragColor = vec4(mix(d.rgb, src, a), d.a); return; }\n"
         "   float oa = a + d.a * (1.0 - a);\n"
         "   vec3 rgb = (src * a + d.rgb * d.a * (1.0 - a)) / max(oa, 1e-4);\n"
         // Only where the stroke blends part way: full paint keeps its exact colour, untouched pixels stay.
         "   if (uDither == 1 && a > 0.0 && a < 1.0) {\n"
         "      float n = (h1(gl_FragCoord.xy) + h1(gl_FragCoord.xy + vec2(17.31, 5.77)) - 1.0) / 255.0;\n"
         // Colour only where the blend makes an in-between value (paint over the same colour, or over nothing, is exact);
         // alpha only where the stroke reaches a visible 8-bit step, so the faint tail past the dab stays empty.
         "      vec3 k = step(0.5 / 255.0, abs(src - d.rgb)) * step(0.001, d.a);\n"
         "      rgb = clamp(rgb + n * k, 0.0, 1.0); oa = clamp(oa + n * step(d.a, 0.999) * step(1.0 / 255.0, a), 0.0, 1.0);\n"
         "   }\n"
         "   fragColor = vec4(rgb, oa);\n"
         "}\n";

      // Marbling: each pixel reads from where the float carried it from (inverse maps, uSrc is a copy of the surface).
      // Drop (Jaffer's marbling): outside a drop of radius r, a point at distance d came from distance sqrt(d^2 - r^2).
      const char* kWarpFrag =
         "#version 150\n"
         "out vec4 fragColor;\n"
         "uniform sampler2D uSrc;\n"
         "uniform sampler2D uSel;\n"
         "uniform int uHasSel;\n"
         "uniform vec2 uTexel;\n"
         "uniform int uKind;\n"
         "uniform vec2 uCenter;\n"
         "uniform float uRadius;\n"
         "uniform float uReach;\n"
         "uniform float uStrength;\n"
         "uniform vec3 uCol;\n"
         "uniform vec2 uDir;\n"
         "uniform float uStep;\n"
         "uniform vec2 uTines[16];\n"
         "uniform int uTineN;\n"
         "uniform float uLambda;\n"
         "uniform int uLock;\n"
         "void main() {\n"
         "   vec2 p = gl_FragCoord.xy;\n"
         "   vec2 uv = p * uTexel;\n"
         "   vec4 d = texture(uSrc, uv);\n"
         "   vec2 from = p;\n"
         "   vec4 r;\n"
         "   if (uKind == 0) {\n"
         "      vec2 v = p - uCenter;\n"
         "      float dist = length(v);\n"
         "      float k = sqrt(max(0.0, 1.0 - uRadius * uRadius / max(dist * dist, 1e-4)));\n"
         "      from = mix(uCenter + v * k, p, smoothstep(0.6 * uReach, uReach, dist));\n"
         "      r = texture(uSrc, from * uTexel);\n"
         "      r = mix(r, vec4(uCol, 1.0), (1.0 - smoothstep(uRadius - 1.0, uRadius + 0.5, dist)) * uStrength);\n"
         "   } else if (uKind == 1) {\n"
         "      float w = 0.0;\n"
         "      for (int i = 0; i < 16; i++) {\n"
         "         if (i >= uTineN) break;\n"
         "         float t = length(p - uTines[i]) / uLambda;\n"
         "         w += exp(-t * t);\n"
         "      }\n"
         "      from = p - uDir * uStep * 0.8 * min(w, 1.0) * uStrength;\n"
         "      r = texture(uSrc, from * uTexel);\n"
         "   } else if (uKind == 2) {\n"
         "      vec2 v = p - uCenter;\n"
         "      float f = 1.0 - smoothstep(0.0, uRadius, length(v));\n"
         "      float a = -uStrength * 1.2 * f * f;\n"
         "      from = uCenter + mat2(cos(a), sin(a), -sin(a), cos(a)) * v;\n"
         "      r = texture(uSrc, from * uTexel);\n"
         "   } else {\n"
         // Bloat reads from nearer the centre (the paint swells), Pinch from further out (it draws in); both fade to
         // nothing at the rim so the edge of the brush leaves no seam.
         "      vec2 v = p - uCenter;\n"
         "      float t = clamp(length(v) / max(uRadius, 1.0), 0.0, 1.0);\n"
         "      float f = (1.0 - t * t) * (1.0 - t * t);\n"
         "      float s = (uKind == 3 ? -0.5 : 0.5) * uStrength * f;\n"
         "      from = uCenter + v * (1.0 + s);\n"
         "      r = texture(uSrc, from * uTexel);\n"
         "   }\n"
         "   float m = uHasSel == 1 ? texture(uSel, uv).r : 1.0;\n"
         "   vec4 o = mix(d, r, m);\n"
         "   fragColor = uLock == 1 ? vec4(o.rgb, d.a) : o;\n"
         "}\n";

      // Wet mix: the colour the brush carries takes a share of the paint under the dab, then refills toward its own.
      const char* kCarryFrag =
         "#version 150\n"
         "out vec4 fragColor;\n"
         "uniform sampler2D uPrev;\n"
         "uniform sampler2D uSurf;\n"
         "uniform vec2 uUv;\n"
         "uniform vec2 uSpan;\n"
         "uniform float uPick;\n"
         "uniform float uRefill;\n"
         "uniform vec3 uCol;\n"
         "void main() {\n"
         "   vec4 p = texture(uPrev, vec2(0.5)); vec3 c = p.rgb;\n"
         "   vec3 rgb = vec3(0.0); float al = 0.0;\n"
         "   for (int i = -1; i <= 1; i++) for (int j = -1; j <= 1; j++) {\n"
         "      vec4 t = texture(uSurf, uUv + vec2(float(i), float(j)) * uSpan);\n"
         "      rgb += t.rgb * t.a; al += t.a;\n"
         "   }\n"
         "   vec3 under = al > 1e-4 ? rgb / al : c;\n"
         "   vec3 o = mix(c, under, p.a < 0.5 ? 1.0 : uPick * al / 9.0);\n"
         "   fragColor = vec4(mix(o, uCol, uRefill), 1.0);\n"
         "}\n";

      unsigned sDab = 0, sMerge = 0, sSmudge = 0, sWarp = 0, sCarry = 0;
      bool sTried = false;
      struct DabLoc
      {
         int center, radius, hard, flow, tip, seed, angle, round, pencil, col;
         int dir, along, mat, profile, depth, light, grain, grainScale, grainDepth, grainMode;
         int dual, dualScale, dualOff, colB, colAlong, colPeriod, carry, wet;
         int tipTex, hasTipTex, grainTex, hasGrainTex, grainTexSize, colTex, hasColTex, surfSize;
      } sDabLoc;
      uint32_t sStrokeSeed = 0x9E3779B9u;

      float Rand(uint32_t& s)
      {
         s ^= s << 13;
         s ^= s >> 17;
         s ^= s << 5;
         return (float)(s & 0xFFFFFF) / (float)0x1000000;
      }

      Brush Make(int tip, float size, float hard, float opacity, float flow, float spacing)
      {
         Brush b;
         b.tip = tip;
         b.size = size;
         b.hardness = hard;
         b.opacity = opacity;
         b.flow = flow;
         b.spacing = spacing;
         b.pressureSize = true;   // the shipped presets follow the pen unless they turn it off
         return b;
      }

      // Hue (0..1 of a turn), saturation and value nudged; colour in and out as RGB 0..1.
      void JitterColour(const float in[3], float dh, float ds, float dv, float out[3])
      {
         const float r = in[0], g = in[1], b = in[2];
         const float mx = std::max({ r, g, b }), mn = std::min({ r, g, b }), d = mx - mn;
         float h = 0.0f;
         if (d > 1e-6f)
            h = mx == r ? std::fmod((g - b) / d + 6.0f, 6.0f) : mx == g ? (b - r) / d + 2.0f : (r - g) / d + 4.0f;
         h = h / 6.0f + dh;
         h -= std::floor(h);
         const float s = std::clamp((mx > 1e-6f ? d / mx : 0.0f) + ds, 0.0f, 1.0f), v = std::clamp(mx + dv, 0.0f, 1.0f);
         const float k = h * 6.0f, f = k - std::floor(k);
         const float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
         switch ((int)std::floor(k) % 6)
         {
            case 0: out[0] = v; out[1] = t; out[2] = p; break;
            case 1: out[0] = q; out[1] = v; out[2] = p; break;
            case 2: out[0] = p; out[1] = v; out[2] = t; break;
            case 3: out[0] = p; out[1] = q; out[2] = v; break;
            case 4: out[0] = t; out[1] = p; out[2] = v; break;
            default: out[0] = v; out[1] = p; out[2] = q; break;
         }
      }
   }

   const std::vector<std::string>& TipNames()
   {
      static const std::vector<std::string> names = { "Round", "Soft Round", "Hard Round", "Square", "Spray", "Chalk", "Ink Bleed",
                                                               "Bristle", "Paper Grain", "Halftone", "Watercolour", "Brushed Metal", "Cloth",
                                                               "Leaf", "Grass", "Sparkle", "Sponge" };
      return names;
   }

   bool Snapshot(unsigned tex, int w, int h, GLUtil::Fbo& out)
   {
      static unsigned sCopy = 0;
      if (sCopy == 0)
         sCopy = GLUtil::CompileProgram(
            "#version 150\n"
            "in vec2 vUv;\n"
            "out vec4 fragColor;\n"
            "uniform sampler2D uTex;\n"
            "void main() { fragColor = texture(uTex, vUv); }\n");
      if (tex == 0 || w <= 0 || h <= 0 || sCopy == 0 || !GLUtil::EnsureFbo(out, w, h, GL_RGBA8, "BrushSource"))
         return false;
      GLboolean blendOn = glIsEnabled(GL_BLEND);
      glDisable(GL_BLEND);
      GLUtil::RunShaderPass(out, sCopy, [&]()
      {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, tex);
         glUniform1i(GLUtil::UniformLoc(sCopy, "uTex"), 0);
      });
      if (blendOn)
         glEnable(GL_BLEND);
      return true;
   }

   const std::vector<std::string>& MaterialNames()
   {
      static const std::vector<std::string> names = { "Plasticine", "Metal", "Chrome", "Stipple", "Satin", "Pearl" };
      return names;
   }

   const std::vector<Preset>& Presets()
   {
      static const std::vector<Preset> presets = []()
      {
         std::vector<Preset> p;
         auto add = [&](const char* group, const char* name, const Brush& b) { p.push_back({ name, b, group }); };
         // Basic
         Brush b = Make(kRound, 3, 1, 1, 1, 0.05f);
         b.pencil = true;
         b.pressureSize = false;
         add("Basic", "Pencil", b);
         b = Make(kRound, 6, 0.9f, 1, 1, 0.05f);
         b.smoothing = 0.3f;
         add("Basic", "Ink", b);
         b = Make(kHardRound, 2, 1, 1, 1, 0.05f);
         b.pressureSize = false;
         b.smoothing = 0.2f;
         add("Basic", "Fine liner", b);
         b = Make(kRound, 24, 0.7f, 0.6f, 1, 0.05f);
         b.pressureSize = false;
         add("Basic", "Marker", b);
         b = Make(kSoftRound, 120, 0, 1, 0.08f, 0.05f);
         b.pressureSize = false;
         b.pressureOpacity = true;
         b.airbrushRate = AirbrushRateFor(10.0f);   // Photoshop's airbrush default: ten dabs a second
         add("Basic", "Soft airbrush", b);
         b = Make(kRound, 30, 1, 1, 1, 0.05f);
         b.pressureSize = false;
         add("Basic", "Hard round", b);
         b = Make(kRound, 40, 0.9f, 1, 1, 0.05f);
         b.roundness = 0.2f;
         b.angle = 45.0f;
         add("Basic", "Flat", b);
         // Creative
         b = Make(kSpray, 80, 0, 1, 0.5f, 0.2f);
         b.jitter = 0.1f;
         add("Creative", "Spray", b);
         b = Make(kRound, 14, 0.9f, 1, 1, 1.5f);
         b.jitter = 2.0f;
         b.pressureSize = false;
         add("Creative", "Splatter", b);
         b = Make(kLeaf, 46, 0.9f, 1, 1, 0.7f);
         b.jitter = 0.7f;
         b.pressureSize = false;
         add("Creative", "Leaves", b);
         b = Make(kGrass, 70, 0.9f, 1, 1, 0.12f);
         b.jitter = 0.35f;
         b.pressureSize = false;
         add("Creative", "Grass", b);
         b = Make(kSparkle, 34, 0.9f, 1, 1, 1.4f);
         b.jitter = 1.6f;
         b.pressureSize = false;
         add("Creative", "Sparkle", b);
         b = Make(kHalftone, 140, 0.2f, 1, 1, 0.08f);
         b.pressureSize = false;
         add("Creative", "Halftone", b);
         b = Make(kSponge, 80, 0.5f, 1, 0.8f, 0.35f);
         b.pressureSize = false;
         add("Creative", "Sponge", b);
         // Texture
         add("Texture", "Chalk", Make(kChalk, 40, 0.5f, 1, 0.6f, 0.15f));
         b = Make(kInkBleed, 18, 0.4f, 0.9f, 0.7f, 0.12f);
         b.jitter = 0.05f;
         add("Texture", "Charcoal", b);
         b = Make(kBristle, 44, 0.7f, 1, 0.9f, 0.04f);
         b.follow = true;
         add("Texture", "Bristle", b);
         b = Make(kWatercolor, 90, 0.5f, 0.85f, 0.35f, 0.08f);
         b.pressureSize = false;
         b.pressureOpacity = true;
         add("Texture", "Watercolour", b);
         b = Make(kGrain, 50, 0.6f, 1, 0.8f, 0.08f);
         add("Texture", "Paper grain", b);
         b = Make(kMetal, 80, 0.8f, 1, 0.9f, 0.03f);
         b.follow = true;
         b.pressureSize = false;
         add("Texture", "Brushed metal", b);
         b = Make(kCloth, 70, 0.6f, 1, 0.9f, 0.08f);
         b.pressureSize = false;
         add("Texture", "Cloth", b);
         // Eraser
         b = Make(kSoftRound, 60, 0, 1, 0.5f, 0.05f);
         b.mode = kErase;
         add("Eraser", "Eraser soft", b);
         b = Make(kRound, 30, 1, 1, 1, 0.05f);
         b.mode = kErase;
         b.pressureSize = false;
         add("Eraser", "Eraser hard", b);
         b = Make(kGrain, 50, 0.6f, 1, 0.7f, 0.08f);
         b.mode = kErase;
         add("Eraser", "Eraser grain", b);
         // Relief: height in the layer's alpha, lit; the layer takes the preset's material.
         b = Make(kRound, 34, 0.1f, 0.6f, 1, 0.08f);
         b.relief = 0;
         add("Relief", "Plasticine", b);
         b = Make(kRound, 30, 0.3f, 0.7f, 1, 0.06f);
         b.relief = 1;
         b.follow = true;
         add("Relief", "Metal", b);
         b = Make(kRound, 26, 0.0f, 1, 1, 1.3f);
         b.relief = 2;
         b.jitter = 0.8f;
         b.sizeJitter = 0.6f;
         b.pressureSize = false;
         add("Relief", "Chrome drops", b);
         b = Make(kRound, 40, 0.1f, 0.6f, 1, 0.08f);
         b.relief = 3;
         add("Relief", "Ink stipple", b);
         // Marbling: the paint floats on the layer.
         b = Make(kRound, 50, 1, 1, 1, 1.6f);
         b.mode = kDrop;
         b.pressureSize = false;
         add("Marbling", "Drops", b);
         b = Make(kRound, 160, 1, 1, 1, 0.04f);
         b.mode = kComb;
         b.pressureSize = false;
         add("Marbling", "Comb", b);
         b = Make(kRound, 140, 1, 1, 1, 0.06f);
         b.mode = kSwirl;
         b.pressureSize = false;
         add("Marbling", "Swirl", b);
         // Liquify: pushes the pixels already on the layer around.
         b = Make(kRound, 120, 1, 1, 1, 0.05f);
         b.pressureSize = false;
         b.mode = kPush;
         add("Liquify", "Push", b);
         b.mode = kTwirl;
         b.flow = 0.5f;
         add("Liquify", "Twirl", b);
         b.mode = kBloat;
         b.flow = 0.35f;
         add("Liquify", "Bloat", b);
         b.mode = kPinch;
         add("Liquify", "Pinch", b);
         // Material: the stroke carries height, light and texture. Each is only a Brush value.
         auto mat = [&](const char* name, Brush m) { add("Material", name, m); };
         b = Make(kRound, 36, 0.9f, 1, 1, 0.04f);
         b.pressureSize = false;
         b.material = 4;
         b.profile = 3;
         b.colorAlong = 1;
         b.color[0] = 0.75f; b.color[1] = 0.12f; b.color[2] = 0.3f;
         b.colorB[0] = 1.0f; b.colorB[1] = 0.62f; b.colorB[2] = 0.72f;
         b.colorPeriod = 260.0f;
         mat("Silk ribbon", b);
         b = Make(kRound, 30, 0.8f, 1, 1, 0.05f);
         b.material = 1;
         b.profile = 1;
         b.depth = 1.4f;
         b.grain = 3;
         b.grainScale = 2.0f;
         b.grainDepth = 0.35f;
         b.grainMode = 2;
         b.color[0] = 0.86f; b.color[1] = 0.66f; b.color[2] = 0.24f;
         mat("Gold leaf", b);
         b = Make(kMetal, 60, 0.85f, 1, 1, 0.03f);
         b.pressureSize = false;
         b.follow = true;
         b.material = 1;
         b.profile = 1;
         b.depth = 1.5f;
         b.color[0] = 0.62f; b.color[1] = 0.64f; b.color[2] = 0.68f;
         mat("Brushed steel", b);
         b = Make(kCloth, 56, 0.7f, 1, 1, 0.06f);
         b.pressureSize = false;
         b.material = 0;
         b.profile = 0;
         b.depth = 0.8f;
         b.grain = 2;
         b.grainMode = 2;
         b.grainDepth = 0.5f;
         b.color[0] = 0.18f; b.color[1] = 0.27f; b.color[2] = 0.48f;
         mat("Denim", b);
         b = Make(kSoftRound, 60, 0.3f, 1, 0.8f, 0.06f);
         b.pressureSize = false;
         b.material = 4;
         b.profile = 1;
         b.depth = 0.7f;
         b.grain = 3;
         b.grainScale = 0.6f;
         b.grainDepth = 0.25f;
         b.color[0] = 0.42f; b.color[1] = 0.05f; b.color[2] = 0.16f;
         mat("Velvet", b);
         b = Make(kRound, 34, 0.6f, 1, 1, 0.06f);
         b.wet = 0.6f;
         b.refill = 0.12f;
         b.color[0] = 1.0f; b.color[1] = 1.0f; b.color[2] = 1.0f;
         mat("Wet gouache", b);
         b = Make(kBristle, 46, 0.7f, 1, 1, 0.04f);
         b.follow = true;
         b.material = 0;
         b.profile = 1;
         b.depth = 1.8f;
         b.wet = 0.35f;
         b.refill = 0.2f;
         b.color[0] = 0.85f; b.color[1] = 0.55f; b.color[2] = 0.2f;
         mat("Oil impasto", b);
         b = Make(kRound, 24, 0.85f, 1, 1, 0.05f);
         b.material = 5;
         b.profile = 1;
         b.color[0] = 0.85f; b.color[1] = 0.85f; b.color[2] = 0.9f;
         mat("Pearl ink", b);
         b = Make(kHardRound, 10, 1, 1, 1, 0.05f);
         b.pressureSize = false;
         b.smoothing = 0.3f;
         b.material = 1;
         b.profile = 1;
         b.depth = 2.0f;
         b.color[0] = 0.78f; b.color[1] = 0.42f; b.color[2] = 0.24f;
         mat("Copper wire", b);
         b = Make(kSoftRound, 50, 0.4f, 1, 0.9f, 0.08f);
         b.pressureSize = false;
         b.material = 0;
         b.profile = 0;
         b.grain = 3;
         b.grainScale = 1.2f;
         b.grainDepth = 0.55f;
         b.grainMode = 1;
         b.dualTip = kSponge;
         b.dualScale = 1.2f;
         b.dualScatter = 0.3f;
         b.color[0] = 0.3f; b.color[1] = 0.5f; b.color[2] = 0.35f;
         mat("Felt", b);
         return p;
      }();
      return presets;
   }

   Stroke::~Stroke()
   {
      GLUtil::DestroyFbo(mCoverage);
      GLUtil::DestroyFbo(mDisplay);
      GLUtil::DestroyFbo(mMerge);
      GLUtil::DestroyFbo(mWetCarry[0]);
      GLUtil::DestroyFbo(mWetCarry[1]);
   }

   bool Stroke::EnsurePrograms()
   {
      if (!sTried)
      {
         sTried = true;
         sDab = GLUtil::CompileProgram(kDabFrag);
         sMerge = GLUtil::CompileProgram(kMergeFrag);
         sSmudge = GLUtil::CompileProgram(kSmudgeFrag);
         sWarp = GLUtil::CompileProgram(kWarpFrag);
         sCarry = GLUtil::CompileProgram(kCarryFrag);
         if (sDab != 0)
         {
            sDabLoc.center = glGetUniformLocation(sDab, "uCenter");
            sDabLoc.radius = glGetUniformLocation(sDab, "uRadius");
            sDabLoc.hard = glGetUniformLocation(sDab, "uHard");
            sDabLoc.flow = glGetUniformLocation(sDab, "uFlow");
            sDabLoc.tip = glGetUniformLocation(sDab, "uTip");
            sDabLoc.seed = glGetUniformLocation(sDab, "uSeed");
            sDabLoc.angle = glGetUniformLocation(sDab, "uAngle");
            sDabLoc.round = glGetUniformLocation(sDab, "uRound");
            sDabLoc.pencil = glGetUniformLocation(sDab, "uPencil");
            sDabLoc.col = glGetUniformLocation(sDab, "uCol");
            sDabLoc.dir = glGetUniformLocation(sDab, "uDir");
            sDabLoc.along = glGetUniformLocation(sDab, "uAlong");
            sDabLoc.mat = glGetUniformLocation(sDab, "uMat");
            sDabLoc.profile = glGetUniformLocation(sDab, "uProfile");
            sDabLoc.depth = glGetUniformLocation(sDab, "uDepth");
            sDabLoc.light = glGetUniformLocation(sDab, "uLight");
            sDabLoc.grain = glGetUniformLocation(sDab, "uGrain");
            sDabLoc.grainScale = glGetUniformLocation(sDab, "uGrainScale");
            sDabLoc.grainDepth = glGetUniformLocation(sDab, "uGrainDepth");
            sDabLoc.grainMode = glGetUniformLocation(sDab, "uGrainMode");
            sDabLoc.dual = glGetUniformLocation(sDab, "uDual");
            sDabLoc.dualScale = glGetUniformLocation(sDab, "uDualScale");
            sDabLoc.dualOff = glGetUniformLocation(sDab, "uDualOff");
            sDabLoc.colB = glGetUniformLocation(sDab, "uColB");
            sDabLoc.colAlong = glGetUniformLocation(sDab, "uColAlong");
            sDabLoc.colPeriod = glGetUniformLocation(sDab, "uColPeriod");
            sDabLoc.carry = glGetUniformLocation(sDab, "uCarry");
            sDabLoc.wet = glGetUniformLocation(sDab, "uWet");
            sDabLoc.tipTex = glGetUniformLocation(sDab, "uTipTex");
            sDabLoc.hasTipTex = glGetUniformLocation(sDab, "uHasTipTex");
            sDabLoc.grainTex = glGetUniformLocation(sDab, "uGrainTex");
            sDabLoc.hasGrainTex = glGetUniformLocation(sDab, "uHasGrainTex");
            sDabLoc.grainTexSize = glGetUniformLocation(sDab, "uGrainTexSize");
            sDabLoc.colTex = glGetUniformLocation(sDab, "uColTex");
            sDabLoc.hasColTex = glGetUniformLocation(sDab, "uHasColTex");
            sDabLoc.surfSize = glGetUniformLocation(sDab, "uSurfSize");
         }
      }
      return sDab != 0 && sMerge != 0 && sSmudge != 0 && sWarp != 0 && sCarry != 0;
   }

   void Stroke::Begin(const GLUtil::Fbo& surface, const Brush& b, float x, float y, float pressure, unsigned selection)
   {
      mActive = false;
      if (surface.fbo == 0 || !EnsurePrograms())
         return;
      mW = surface.w;
      mH = surface.h;
      if (!GLUtil::EnsureFbo(mCoverage, mW, mH, GL_RGBA16F, "BrushCoverage") ||
          !GLUtil::EnsureFbo(mDisplay, mW, mH, surface.internalFormat, "BrushDisplay") ||
          !GLUtil::EnsureFbo(mMerge, mW, mH, surface.internalFormat, "BrushMerge"))
         return;
      GLUtil::ClearFbo(mCoverage);
      mBrush = b;
      mBrush.size = std::max(0.5f, b.size);
      if (b.wet > 0.0f && !DirectMode(b.mode))
      {
         if (!GLUtil::EnsureFbo(mWetCarry[0], 1, 1, GL_RGBA16F, "BrushCarry") || !GLUtil::EnsureFbo(mWetCarry[1], 1, 1, GL_RGBA16F, "BrushCarry"))
            mBrush.wet = 0.0f;
         else
         {
            // A new colour loads the reservoir; a fresh load (no pickup) or a first stroke starts from it, a pickup
            // kept by Clean off carries on, and a clean, unloaded brush starts empty.
            const bool fresh = !mMixer.hasColor || !MixerState::Same(mMixer.color, b.color);
            mMixer.BeginStroke(b.color);
            mEmpty = !mMixer.hasRes && !mMixer.hasPick;
            if (mEmpty || fresh || !mMixer.hasPick || !mCarryReady)
            {
               const float c[4] = { mMixer.res[0], mMixer.res[1], mMixer.res[2], mEmpty ? 0.0f : 1.0f };
               GLUtil::ClearFbo(mWetCarry[0], c);
               mCarryIdx = 0;
            }
            mCarryReady = true;
            mMixer.hasPick = true;   // the stroke picks paint up as it goes
         }
      }
      mSelection = selection;
      mSurface = &surface;
      mEnding = false;
      mActive = true;
      mAny = false;
      mFrameAny = false;
      mMergeFullDirty = false;
      mDabCount = 0;
      if (!DirectMode(mBrush.mode))
      {
         GLint prevR = 0, prevD = 0;
         glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevR);
         glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevD);
         glBindFramebuffer(GL_READ_FRAMEBUFFER, surface.fbo);
         glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mDisplay.fbo);
         glBlitFramebuffer(0, 0, mW, mH, 0, 0, mW, mH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
         glBindFramebuffer(GL_READ_FRAMEBUFFER, prevR);
         glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevD);
      }
      mQueue.clear();
      sStrokeSeed = sStrokeSeed * 1664525u + 1013904223u;
      mRng = (b.seed != 0 ? b.seed : sStrokeSeed) | 1u;
      {
         float tx = 0, ty = 0, r = 0;
         ApplyPose(b, pressure, tx, ty, r);
      }
      const float p = ApplyPressureCurve(pressure);
      mLastX = mPenX = x;
      mLastY = mPenY = y;
      mLastP = mPenP = p;
      mStab.assign(1, { x, y, p });
      mEuroX = { x, 0.0f, true };
      mEuroY = { y, 0.0f, true };
      mEuroP = { p, 0.0f, true };
      mEuroLastTime = mPenTime;
      mAirAccum = 0.0f;
      mDabN = 0;
      mCarry = 0.0f;
      mDabX = x;
      mDabY = y;
      mDir = 0.0f;
      mDist = 0.0f;
      mPrevX = x;
      mPrevY = y;
      Dab(x, y, p, Rand(mRng) * 100.0f);
      // An airbrush click lays two dabs, as Photoshop's does, before the hold builds up.
      if (mBrush.airbrushRate > 0.0f && !DirectMode(mBrush.mode))
         Dab(x, y, p, Rand(mRng) * 100.0f);
      mDisplayStale = true;
   }

   float DabShare(float base, float jitter, float seed)
   {
      if (jitter <= 0.0f)
         return base;
      const float u = std::fmod(std::fabs(std::sin(seed * 39.3468f) * 15731.743f), 1.0f);
      return base * (1.0f - u * std::clamp(jitter, 0.0f, 1.0f));
   }

   void Stroke::Dab(float x, float y, float pressure, float seed)
   {
      if (!mActive)
         return;
      // The heading comes from the dabs themselves, so a recorded replay turns the tip exactly as the live stroke did.
      if (std::hypot(x - mDabX, y - mDabY) > 0.5f)
      {
         mDir = std::atan2(y - mDabY, x - mDabX);
         mDabX = x;
         mDabY = y;
      }
      if (mBrush.jitter > 0.0f || mBrush.scatterY > 0.0f)
      {
         const float sy = mBrush.scatterY >= 0.0f ? mBrush.scatterY : mBrush.jitter;
         x += (Rand(mRng) - 0.5f) * mBrush.jitter * mBrush.size;
         y += (Rand(mRng) - 0.5f) * sy * mBrush.size;
      }
      float tX = mTiltX, tY = mTiltY, penRot = mPenRot;
      ApplyPose(mBrush, pressure, tX, tY, penRot);
      const float tiltMag = std::min(1.0f, std::hypot(tX, tY));
      const float tiltAz = std::atan2(tY, tX);
      const float p = ApplyPressureCurve(pressure);
      mDist += std::hypot(x - mPrevX, y - mPrevY);
      const float fx = mPrevX, fy = mPrevY;
      mPrevX = x;
      mPrevY = y;
      // Per-dab dynamics, drawn here so mirrored copies match their original.
      const Brush& b = mBrush;
      mSize = b.sizeJitter > 0.0f ? 1.0f - Rand(mRng) * std::clamp(b.sizeJitter, 0.0f, 1.0f) : 1.0f;
      if (b.tiltSize)
         mSize *= 1.0f + 1.5f * tiltMag;
      if (b.taperIn > 0.0f)
         mSize *= std::clamp(mDist / b.taperIn, 0.05f, 1.0f);
      mFlow = b.opacityJitter > 0.0f ? 1.0f - Rand(mRng) * std::clamp(b.opacityJitter, 0.0f, 1.0f) : 1.0f;
      if (b.accumJitter > 0.0f)
         mFlow *= 1.0f - Rand(mRng) * std::clamp(b.accumJitter, 0.0f, 1.0f);
      mHard = b.hardJitter > 0.0f ? 1.0f - Rand(mRng) * std::clamp(b.hardJitter, 0.0f, 1.0f) : 1.0f;
      mRot = b.angleJitter > 0.0f ? (Rand(mRng) - 0.5f) * 6.2831853f * std::clamp(b.angleJitter, 0.0f, 1.0f) : 0.0f;
      if (b.tiltAngle && tiltMag > 0.05f)
         mRot += tiltAz;
      mRot += penRot * (3.14159265f / 180.0f);
      mRound = b.roundJitter > 0.0f ? 1.0f - Rand(mRng) * std::clamp(b.roundJitter, 0.0f, 0.95f) : 1.0f;
      if (b.tiltRound)
         mRound = std::max(0.05f, mRound * (1.0f - 0.75f * tiltMag));
      if (b.projection && tiltMag > 1e-4f)
      {
         // A tip leaning tiltMag of the way to flat (90 degrees) shows cos of that lean across the lean's direction.
         mRound = std::max(0.05f, mRound * std::cos(tiltMag * 1.5707963f));
         mRot += tiltAz + 1.5707963f;   // the tip squashes along its own y: turn that onto the lean
      }
      if (b.hueJitter > 0.0f || b.satJitter > 0.0f || b.valJitter > 0.0f)
      {
         const float dh = (Rand(mRng) - 0.5f) * b.hueJitter, ds = (Rand(mRng) - 0.5f) * 2.0f * b.satJitter, dv = (Rand(mRng) - 0.5f) * 2.0f * b.valJitter;
         JitterColour(b.color, dh, ds, dv, mRgb);
      }
      else
         std::copy(b.color, b.color + 3, mRgb);
      if (b.impressionist && b.patternRgba != nullptr && b.patternW > 0 && b.patternH > 0)
      {
         // Impressionist: the tile under the dab's centre, its phase jittered by up to one tile per dab.
         const float jx = (Rand(mRng) - 0.5f) * 2.0f * (float)b.patternW, jy = (Rand(mRng) - 0.5f) * 2.0f * (float)b.patternH;
         const float k = std::max(0.05f, b.patternScale);
         auto wrapI = [](float v, int n) { const int i = (int)std::floor(v); return ((i % n) + n) % n; };
         const int qx = wrapI((x + b.patternPhase[0] + jx) / k, b.patternW), qy = wrapI((y + b.patternPhase[1] + jy) / k, b.patternH);
         const uint8_t* t = b.patternRgba + ((size_t)qy * b.patternW + qx) * 4;
         for (int c = 0; c < 3; c++)
            mRgb[c] = t[c] / 255.0f;
      }
      Queue(x, y, p, seed, mDir, fx, fy);
      struct NextDab { int& n; ~NextDab() { n++; } } nextDab{ mDabN };   // mirrored copies share the index
      for (const auto& m : mMirrors)
         Queue(m[0] * x + m[1] * y + m[2], m[3] * x + m[4] * y + m[5], p, seed, std::atan2(m[3] * std::cos(mDir) + m[4] * std::sin(mDir), m[0] * std::cos(mDir) + m[1] * std::sin(mDir)),
               m[0] * fx + m[1] * fy + m[2], m[3] * fx + m[4] * fy + m[5]);
   }

   void Stroke::Queue(float x, float y, float p, float seed, float dir, float fx, float fy)
   {
      Pending q{ x, y, p, seed, dir, mDist, mSize, mFlow, mHard, mRot, mRound, { mRgb[0], mRgb[1], mRgb[2] }, fx, fy,
                 DabShare(mBrush.wet, mBrush.wetJitter, seed), DabShare(mBrush.refill, mBrush.refillJitter, seed + 17.0f) };
      q.index = mDabN;
      mQueue.push_back(q);
      // A drop pushes paint out to five radii; a comb's tines drag a little past its width.
      const float sz = mBrush.size * std::max(1.0f, mSize);
      const float r = (mBrush.mode == kDrop ? 2.5f * sz : mBrush.mode == kComb || mBrush.mode == kPush ? 0.75f * sz : 0.5f * sz) + 2.0f;
      if (!mAny)
      {
         mDirty[0] = x - r;
         mDirty[1] = y - r;
         mDirty[2] = x + r;
         mDirty[3] = y + r;
         mAny = true;
      }
      else
      {
         mDirty[0] = std::min(mDirty[0], x - r);
         mDirty[1] = std::min(mDirty[1], y - r);
         mDirty[2] = std::max(mDirty[2], x + r);
         mDirty[3] = std::max(mDirty[3], y + r);
      }
      // Wrapped: a dab over an edge also lands on the far side (DrawDab widens its clip the same way).
      if (mBrush.wrap && !WarpMode(mBrush.mode) && mBrush.mode != kSmudge)
      {
         if (x - r < 0.0f || x + r > (float)mW)
         {
            mDirty[0] = 0.0f;
            mDirty[2] = (float)mW;
         }
         if (y - r < 0.0f || y + r > (float)mH)
         {
            mDirty[1] = 0.0f;
            mDirty[3] = (float)mH;
         }
      }
   }

   void Stroke::Walk(float x, float y, float pressure)
   {
      const float dx = x - mLastX, dy = y - mLastY;
      const float d = std::sqrt(dx * dx + dy * dy);
      if (d <= 1e-4f)
         return;
      if (mBrush.spacingOff)
      {
         // Spacing off: a dab where each pointer sample lands, so the pointer's speed sets the gaps.
         Dab(x, y, pressure, Rand(mRng) * 100.0f);
         mCarry = 0.0f;
         mLastX = x;
         mLastY = y;
         mLastP = pressure;
         return;
      }
      const float scale = mBrush.pressureSize ? std::max(0.05f, ResponseAt(mBrush.sizeCurveMid, std::max(mLastP, pressure))) : 1.0f;
      const float step = std::max(mBrush.pencil ? 1.0f : 0.5f, mBrush.size * scale * std::max(0.01f, mBrush.spacing));
      float t = step - mCarry;
      int guard = 0;
      while (t <= d && guard++ < 20000)
      {
         const float f = t / d;
         Dab(mLastX + dx * f, mLastY + dy * f, mLastP + (pressure - mLastP) * f, Rand(mRng) * 100.0f);
         t += step;
      }
      mCarry = d - (t - step);
      mLastX = x;
      mLastY = y;
      mLastP = pressure;
   }

   float Stroke::ApplyPressureCurve(float pressure) const
   {
      if (pressure < 0.0f)
         return 1.0f;
      const float g = std::clamp(mBrush.pressureCurve, 0.25f, 3.0f);
      return std::pow(std::clamp(pressure, 0.0f, 1.0f), g);
   }

   void Stroke::MoveTo(float x, float y, float pressure)
   {
      if (!mActive)
         return;
      const float p = ApplyPressureCurve(pressure);
      // Catch-up off: a pen held still (the same sample fed again each frame) does not pull the smoothed brush
      // on toward it (PhotoCraft smoothing_without_catch_up_stops_while_the_pointer_pauses).
      const bool still = x == mPenX && y == mPenY && p == mPenP;
      mPenX = x;
      mPenY = y;
      mPenP = p;
      if (still && !mBrush.catchUp && (mBrush.stabiliser > 0 || mBrush.smoothing > 0.0f))
         return;
      if (mBrush.stabiliser > 0)
      {
         StabWalk(x, y, p);
         return;
      }
      if (mBrush.smoothing > 0.0f && mBrush.oneEuro)
      {
         const float dt = mEuroLastTime > 0.0 && mPenTime > mEuroLastTime
                             ? std::clamp((float)(mPenTime - mEuroLastTime), 1e-4f, 0.25f)
                             : (1.0f / 120.0f);
         mEuroLastTime = mPenTime;
         const float s = std::clamp(mBrush.smoothing, 0.0f, 0.99f);
         const float minCut = std::max(0.4f, 14.0f * (1.0f - s));
         const float beta = 0.015f;
         auto step = [&](EuroAxis& st, float v, float fcMul)
         {
            if (!st.init) { st = { v, 0.0f, true }; return v; }
            auto alpha = [&](float fc)
            {
               const float tau = 1.0f / (6.2831853f * fc);
               return 1.0f / (1.0f + tau / dt);
            };
            const float d = (v - st.x) / dt;
            const float ad = alpha(1.0f);
            st.dx += ad * (d - st.dx);
            const float fc = minCut * fcMul + beta * std::fabs(st.dx);
            const float ax = alpha(fc);
            st.x += ax * (v - st.x);
            return st.x;
         };
         Walk(step(mEuroX, x, 1.0f), step(mEuroY, y, 1.0f), std::clamp(step(mEuroP, p, 1.5f), 0.0f, 1.0f));
         return;
      }
      if (mBrush.smoothing > 0.0f)
      {
         // The brush hangs on a string behind the pen and only moves once the string is taut.
         const float len = mBrush.smoothing * std::max(8.0f, mBrush.size) * 1.5f;
         const float dx = x - mLastX, dy = y - mLastY;
         const float d = std::sqrt(dx * dx + dy * dy);
         if (d <= len)
            return;
         const float f = (d - len) / d;
         Walk(mLastX + dx * f, mLastY + dy * f, p);
         return;
      }
      Walk(x, y, p);
   }

   void Stroke::Tick(float dt)
   {
      if (!mActive || mBrush.airbrushRate <= 0.0f || DirectMode(mBrush.mode) || dt <= 0.0f)
         return;
      const float rate = 4.0f + std::clamp(mBrush.airbrushRate, 0.0f, 1.0f) * 36.0f;
      mAirAccum += std::min(dt, 0.25f) * rate;
      int guard = 0;
      while (mAirAccum >= 1.0f && guard++ < 8)
      {
         Dab(mLastX, mLastY, mLastP, Rand(mRng) * 100.0f);
         mAirAccum -= 1.0f;
      }
      if (guard > 0)
         mDisplayStale = true;
   }

   // The stabiliser: the brush goes to a weighted average of the last N pen samples, newer ones weighing more
   // (weights 1..N), so a shaky hand comes out as a steady line that trails the pen a little.
   void Stroke::StabWalk(float x, float y, float p)
   {
      const size_t n = (size_t)std::clamp(mBrush.stabiliser, 1, 64);
      mStab.push_back({ x, y, p });
      if (mStab.size() > n)
         mStab.erase(mStab.begin(), mStab.end() - n);
      float sx = 0, sy = 0, sp = 0, sw = 0;
      for (size_t i = 0; i < mStab.size(); i++)
      {
         const float w = (float)(i + 1);
         sx += mStab[i][0] * w;
         sy += mStab[i][1] * w;
         sp += mStab[i][2] * w;
         sw += w;
      }
      Walk(sx / sw, sy / sw, sp / sw);
   }

   bool Stroke::Predict(const float a[3], const float b[3], float aheadMs, float maxPx, float out[2])
   {
      const float dt = b[2] - a[2];
      const float dx = b[0] - a[0], dy = b[1] - a[1];
      if (!(dt > 0.0f) || dx * dx + dy * dy < 1e-6f)
         return false;
      float k = aheadMs / dt;
      const float d = std::sqrt(dx * dx + dy * dy) * k;
      if (d > maxPx)
         k *= maxPx / d;
      out[0] = b[0] + dx * k;
      out[1] = b[1] + dy * k;
      return true;
   }

   void Stroke::Flush()
   {
      if (!mActive || mQueue.empty())
         return;
      GLboolean blendOn = glIsEnabled(GL_BLEND);
      GLint srcRgb = 0, dstRgb = 0, srcA = 0, dstA = 0;
      glGetIntegerv(GL_BLEND_SRC_RGB, &srcRgb);
      glGetIntegerv(GL_BLEND_DST_RGB, &dstRgb);
      glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcA);
      glGetIntegerv(GL_BLEND_DST_ALPHA, &dstA);
      glEnable(GL_BLEND);
      glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      const Brush& b = mBrush;
      // Taper out: the dabs within its reach of the pen wait until the pen lifts, then shrink toward the end.
      size_t n = mQueue.size();
      if (b.taperOut > 0.0f && !mEnding)
         while (n > 0 && mQueue[n - 1].dist > mDist - b.taperOut)
            n--;
      for (size_t i = 0; i < n; i++)
      {
         const Pending& q = mQueue[i];
         const float taper = b.taperOut > 0.0f ? std::clamp((mDist - q.dist) / b.taperOut, 0.05f, 1.0f) : 1.0f;
         DrawDab(q, taper);
      }
      mQueue.erase(mQueue.begin(), mQueue.begin() + (long)n);
      glBlendFuncSeparate(srcRgb, dstRgb, srcA, dstA);
      if (!blendOn)
         glDisable(GL_BLEND);
      mDisplayStale = true;
   }

   void Stroke::DrawDab(const Pending& q, float taper)
   {
      const Brush& b = mBrush;
      const float fadeS = FadeAt(b.fadeSize, q.index), fadeO = FadeAt(b.fadeOpacity, q.index);
      if (fadeS <= 0.0f || fadeO <= 0.0f)
         return;   // faded out
      const float scale = (b.pressureSize ? std::max(0.05f, ResponseAt(b.sizeCurveMid, q.pressure)) : 1.0f) * q.size * taper * fadeS;
      float r = 0.5f * b.size * scale;
      float cx = q.x, cy = q.y;
      // A hard aliased tip sits on the pixel grid; a soft one with anti-alias off keeps the soft tip's centre, so both
      // reach the same pixels (PhotoCraft replace_anti_alias_tests).
      if (b.pencil && b.hardness >= 0.999f)
      {
         r = std::max(0.5f, std::round(r * 2.0f) * 0.5f);
         cx = std::floor(cx) + 0.5f;
         cy = std::floor(cy) + 0.5f;
      }
      const float flow = b.flow * q.flow * (b.pressureOpacity ? ResponseAt(b.opacityCurveMid, q.pressure) : 1.0f) * fadeO;
      if (b.mode == kSmudge)
      {
         SmudgeDab(q, r, std::clamp(flow * b.opacity, 0.0f, 1.0f));
         mDabCount++;
         return;
      }
      if (WarpMode(b.mode))
      {
         WarpDab(q, r, std::clamp(flow * b.opacity, 0.0f, 1.0f));
         mDabCount++;
         return;
      }
      int sc[4] = { (int)std::floor(cx - r - 2), (int)std::floor(cy - r - 2), 0, 0 };
      sc[2] = (int)std::ceil(cx + r + 2) - sc[0];
      sc[3] = (int)std::ceil(cy + r + 2) - sc[1];
      int x0 = std::max(0, sc[0]), y0 = std::max(0, sc[1]);
      int x1 = std::min(mW, sc[0] + sc[2]), y1 = std::min(mH, sc[1] + sc[3]);
      // Wrapped: a dab that leaves the surface comes back on the far side, so it may touch any row or column it spans
      // modulo the size. Widen the clip to the whole span on that axis (the shader picks the nearest copy per pixel).
      if (b.wrap)
      {
         if (sc[0] < 0 || sc[0] + sc[2] > mW)
         {
            x0 = 0;
            x1 = mW;
         }
         if (sc[1] < 0 || sc[1] + sc[3] > mH)
         {
            y0 = 0;
            y1 = mH;
         }
      }
      if (x1 <= x0 || y1 <= y0)
         return;
      const int clip[4] = { x0, y0, x1 - x0, y1 - y0 };
      if (!mFrameAny)
      {
         mFrameDirty[0] = x0;
         mFrameDirty[1] = y0;
         mFrameDirty[2] = x1;
         mFrameDirty[3] = y1;
         mFrameAny = true;
      }
      else
      {
         mFrameDirty[0] = std::min(mFrameDirty[0], x0);
         mFrameDirty[1] = std::min(mFrameDirty[1], y0);
         mFrameDirty[2] = std::max(mFrameDirty[2], x1);
         mFrameDirty[3] = std::max(mFrameDirty[3], y1);
      }
      const float angle = b.angle * 3.14159265f / 180.0f;
      const bool wet = b.wet > 0.0f;
      if (wet)
         UpdateCarry(q, r);
      // The dual tip sits off the main one by a scatter fixed by the dab's seed, so replays match.
      const float u1 = std::fmod(std::fabs(std::sin(q.seed * 12.9898f) * 43758.5453f), 1.0f);
      const float u2 = std::fmod(std::fabs(std::sin(q.seed * 78.233f) * 24634.6345f), 1.0f);
      const float scatter = b.dualScatter * b.size;
      const float az = b.lightAz * 3.14159265f / 180.0f, el = std::clamp(b.lightEl, 1.0f, 90.0f) * 3.14159265f / 180.0f;
      GLUtil::DrawPass(mCoverage, sDab, [&]()
      {
         glUniform2f(sDabLoc.center, cx, cy);
         glUniform1f(sDabLoc.radius, r);
         glUniform1f(sDabLoc.hard, std::clamp(b.hardness * q.hard, 0.0f, 0.98f));
         glUniform1f(sDabLoc.flow, std::clamp(flow, 0.0f, 1.0f));
         glUniform1i(sDabLoc.tip, b.tip);
         glUniform1f(sDabLoc.seed, q.seed);
         glUniform1f(sDabLoc.angle, angle + q.rot + (b.follow ? q.dir : 0.0f));
         glUniform1f(sDabLoc.round, std::clamp(b.roundness * q.round, 0.05f, 1.0f));
         glUniform1i(sDabLoc.pencil, b.pencil ? 1 : 0);
         glUniform3f(sDabLoc.col, q.rgb[0], q.rgb[1], q.rgb[2]);
         glUniform2f(sDabLoc.dir, std::cos(q.dir), std::sin(q.dir));
         glUniform1f(sDabLoc.along, q.dist);
         glUniform1i(sDabLoc.mat, b.material);
         glUniform1i(sDabLoc.profile, b.profile);
         glUniform1f(sDabLoc.depth, std::clamp(b.depth, 0.0f, 3.0f));
         glUniform3f(sDabLoc.light, std::cos(el) * std::cos(az), std::cos(el) * std::sin(az), std::sin(el));
         glUniform1i(sDabLoc.grain, b.grainTex != 0 ? std::max(1, b.grain) : b.grain);
         glUniform1f(sDabLoc.grainScale, std::max(0.05f, b.grainScale));
         glUniform1f(sDabLoc.grainDepth, std::clamp(b.grainDepth, 0.0f, 1.0f));
         glUniform1i(sDabLoc.grainMode, b.grainMode);
         glUniform1i(sDabLoc.dual, b.dualTip >= 0 && b.dualTip < kTipCount ? b.dualTip : -1);
         glUniform1f(sDabLoc.dualScale, std::clamp(b.dualScale, 0.05f, 2.0f));
         glUniform2f(sDabLoc.dualOff, (u1 - 0.5f) * scatter, (u2 - 0.5f) * scatter);
         glUniform3f(sDabLoc.colB, b.colorB[0], b.colorB[1], b.colorB[2]);
         glUniform1i(sDabLoc.colAlong, b.colorAlong);
         glUniform1f(sDabLoc.colPeriod, std::max(1.0f, b.colorPeriod));
         glUniform1i(sDabLoc.wet, wet ? 1 : 0);
         glUniform1i(sDabLoc.carry, 0);
         glUniform1i(sDabLoc.tipTex, 1);
         glUniform1i(sDabLoc.hasTipTex, b.tipTex != 0 ? 1 : 0);
         glUniform1i(sDabLoc.grainTex, 2);
         glUniform1i(sDabLoc.hasGrainTex, b.grainTex != 0 ? 1 : 0);
         glUniform2f(sDabLoc.grainTexSize, (float)std::max(1, b.grainTexW), (float)std::max(1, b.grainTexH));
         glUniform1i(sDabLoc.colTex, 3);
         glUniform1i(sDabLoc.hasColTex, b.colorTex != 0 ? 1 : 0);
         glUniform2f(sDabLoc.surfSize, (float)std::max(1, mW), (float)std::max(1, mH));
         glUniform1i(GLUtil::UniformLoc(sDab, "uWrap"), b.wrap ? 1 : 0);
         glActiveTexture(GL_TEXTURE1);
         glBindTexture(GL_TEXTURE_2D, b.tipTex);
         glActiveTexture(GL_TEXTURE2);
         glBindTexture(GL_TEXTURE_2D, b.grainTex);
         glActiveTexture(GL_TEXTURE3);
         glBindTexture(GL_TEXTURE_2D, b.colorTex);
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, wet ? mWetCarry[mCarryIdx].tex : 0);
      }, clip);
      mDabCount++;
   }

   void Stroke::UpdateCarry(const Pending& q, float r)
   {
      if (mSurface == nullptr || sCarry == 0)
         return;
      GLboolean blendOn = glIsEnabled(GL_BLEND);
      glDisable(GL_BLEND);
      const int from = mCarryIdx, to = 1 - mCarryIdx;
      const Brush& b = mBrush;
      // Pick-up and refill are shares per brush width travelled, so the spacing does not change the look.
      const float per = std::clamp(b.spacing, 0.01f, 2.0f);
      const float pick = 1.0f - std::pow(1.0f - std::clamp(q.wet, 0.0f, 0.999f), per);
      const float refill = mEmpty ? 0.0f : 1.0f - std::pow(1.0f - std::clamp(q.refill, 0.0f, 0.999f), per);
      GLUtil::DrawPass(mWetCarry[to], sCarry, [&]()
      {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, mWetCarry[from].tex);
         glUniform1i(GLUtil::UniformLoc(sCarry, "uPrev"), 0);
         glActiveTexture(GL_TEXTURE1);
         glBindTexture(GL_TEXTURE_2D, mSurface->tex);
         glUniform1i(GLUtil::UniformLoc(sCarry, "uSurf"), 1);
         glUniform2f(GLUtil::UniformLoc(sCarry, "uUv"), q.x / std::max(1, mW), q.y / std::max(1, mH));
         glUniform2f(GLUtil::UniformLoc(sCarry, "uSpan"), 0.5f * r / std::max(1, mW), 0.5f * r / std::max(1, mH));
         glUniform1f(GLUtil::UniformLoc(sCarry, "uPick"), pick);
         glUniform1f(GLUtil::UniformLoc(sCarry, "uRefill"), refill);
         glUniform3f(GLUtil::UniformLoc(sCarry, "uCol"), q.rgb[0], q.rgb[1], q.rgb[2]);
         glActiveTexture(GL_TEXTURE0);
      });
      mCarryIdx = to;
      if (blendOn)
         glEnable(GL_BLEND);
   }

   void Stroke::SmudgeDab(const Pending& q, float r, float strength)
   {
      if (mSurface == nullptr || strength <= 0.0f)
         return;
      const float ox = q.fromX - q.x, oy = q.fromY - q.y;
      const int x0 = std::max(0, (int)std::floor(q.x - r - 2)), y0 = std::max(0, (int)std::floor(q.y - r - 2));
      const int x1 = std::min(mW, (int)std::ceil(q.x + r + 2)), y1 = std::min(mH, (int)std::ceil(q.y + r + 2));
      if (x1 <= x0 || y1 <= y0)
         return;
      // The dab's square and the square it reads from, copied off the surface so the pass never reads what it writes.
      const int cx0 = std::max(0, std::min(x0, (int)std::floor(x0 + ox) - 1)), cy0 = std::max(0, std::min(y0, (int)std::floor(y0 + oy) - 1));
      const int cx1 = std::min(mW, std::max(x1, (int)std::ceil(x1 + ox) + 1)), cy1 = std::min(mH, std::max(y1, (int)std::ceil(y1 + oy) + 1));
      GLint prevRead = 0;
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, mSurface->fbo);
      glBindTexture(GL_TEXTURE_2D, mMerge.tex);
      glCopyTexSubImage2D(GL_TEXTURE_2D, 0, cx0, cy0, cx0, cy0, cx1 - cx0, cy1 - cy0);
      glBindTexture(GL_TEXTURE_2D, 0);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, prevRead);
      GLboolean blendOn = glIsEnabled(GL_BLEND);
      glDisable(GL_BLEND);
      const int clip[4] = { x0, y0, x1 - x0, y1 - y0 };
      GLUtil::DrawPass(*mSurface, sSmudge, [&]()
      {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, mMerge.tex);
         glUniform1i(GLUtil::UniformLoc(sSmudge, "uSrc"), 0);
         glActiveTexture(GL_TEXTURE1);
         glBindTexture(GL_TEXTURE_2D, mSelection);
         glUniform1i(GLUtil::UniformLoc(sSmudge, "uSel"), 1);
         glUniform1i(GLUtil::UniformLoc(sSmudge, "uHasSel"), mSelection != 0 ? 1 : 0);
         glUniform2f(GLUtil::UniformLoc(sSmudge, "uTexel"), 1.0f / std::max(1, mW), 1.0f / std::max(1, mH));
         glUniform2f(GLUtil::UniformLoc(sSmudge, "uCenter"), q.x, q.y);
         glUniform2f(GLUtil::UniformLoc(sSmudge, "uOff"), ox, oy);
         glUniform1f(GLUtil::UniformLoc(sSmudge, "uRadius"), r);
         glUniform1f(GLUtil::UniformLoc(sSmudge, "uHard"), std::clamp(mBrush.hardness, 0.0f, 0.98f));
         glUniform1f(GLUtil::UniformLoc(sSmudge, "uStrength"), strength);
         glUniform1i(GLUtil::UniformLoc(sSmudge, "uLock"), mBrush.lockAlpha ? 1 : 0);
         glUniform1i(GLUtil::UniformLoc(sSmudge, "uSmudgeMode"), std::clamp(mBrush.smudgeMode, 0, 6));
         glActiveTexture(GL_TEXTURE2);
         glBindTexture(GL_TEXTURE_2D, mBrush.sourceTex);
         glUniform1i(GLUtil::UniformLoc(sSmudge, "uComp"), 2);
         glUniform1i(GLUtil::UniformLoc(sSmudge, "uHasComp"), mBrush.sourceTex != 0 ? 1 : 0);
         glActiveTexture(GL_TEXTURE0);
      }, clip);
      if (blendOn)
         glEnable(GL_BLEND);
   }

   void Stroke::WarpDab(const Pending& q, float r, float strength)
   {
      if (mSurface == nullptr || strength <= 0.0f)
         return;
      const int m = mBrush.mode;
      const int kind = m == kDrop ? 0 : m == kComb || m == kPush ? 1 : m == kSwirl || m == kTwirl ? 2 : m == kBloat ? 3 : 4;
      float ux = q.x - q.fromX, uy = q.y - q.fromY;
      const float step = std::sqrt(ux * ux + uy * uy);
      if (kind == 1)
      {
         if (step < 1e-3f)
            return;
         ux /= step;
         uy /= step;
      }
      const int n = m == kPush ? 1 : std::clamp(mBrush.tines, 1, 16);
      const float width = 2.0f * r;
      const float lambda = n > 1 ? std::max(2.0f, width / (n - 1) * 0.3f) : std::max(2.0f, r * 0.5f);
      const float reach = kind == 0 ? 5.0f * r : kind == 1 ? r + 3.0f * lambda + step : r;
      const int x0 = std::max(0, (int)std::floor(q.x - reach - 2)), y0 = std::max(0, (int)std::floor(q.y - reach - 2));
      const int x1 = std::min(mW, (int)std::ceil(q.x + reach + 2)), y1 = std::min(mH, (int)std::ceil(q.y + reach + 2));
      if (x1 <= x0 || y1 <= y0)
         return;
      // Copy the square plus the step it may read from, so the pass never reads what it writes.
      const int pad = (int)std::ceil(step) + 2;
      const int cx0 = std::max(0, x0 - pad), cy0 = std::max(0, y0 - pad), cx1 = std::min(mW, x1 + pad), cy1 = std::min(mH, y1 + pad);
      GLint prevRead = 0;
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, mSurface->fbo);
      glBindTexture(GL_TEXTURE_2D, mMerge.tex);
      glCopyTexSubImage2D(GL_TEXTURE_2D, 0, cx0, cy0, cx0, cy0, cx1 - cx0, cy1 - cy0);
      glBindTexture(GL_TEXTURE_2D, 0);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, prevRead);
      float tines[32] = {};
      for (int i = 0; i < n; i++)
      {
         const float t = n > 1 ? (float)i / (n - 1) - 0.5f : 0.0f;
         tines[i * 2] = q.x - uy * t * width;
         tines[i * 2 + 1] = q.y + ux * t * width;
      }
      GLboolean blendOn = glIsEnabled(GL_BLEND);
      glDisable(GL_BLEND);
      const int clip[4] = { x0, y0, x1 - x0, y1 - y0 };
      GLUtil::DrawPass(*mSurface, sWarp, [&]()
      {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, mMerge.tex);
         glUniform1i(GLUtil::UniformLoc(sWarp, "uSrc"), 0);
         glActiveTexture(GL_TEXTURE1);
         glBindTexture(GL_TEXTURE_2D, mSelection);
         glUniform1i(GLUtil::UniformLoc(sWarp, "uSel"), 1);
         glUniform1i(GLUtil::UniformLoc(sWarp, "uHasSel"), mSelection != 0 ? 1 : 0);
         glUniform2f(GLUtil::UniformLoc(sWarp, "uTexel"), 1.0f / std::max(1, mW), 1.0f / std::max(1, mH));
         glUniform1i(GLUtil::UniformLoc(sWarp, "uKind"), kind);
         glUniform2f(GLUtil::UniformLoc(sWarp, "uCenter"), q.x, q.y);
         glUniform1f(GLUtil::UniformLoc(sWarp, "uRadius"), r);
         glUniform1f(GLUtil::UniformLoc(sWarp, "uReach"), reach);
         glUniform1f(GLUtil::UniformLoc(sWarp, "uStrength"), strength);
         glUniform3f(GLUtil::UniformLoc(sWarp, "uCol"), q.rgb[0], q.rgb[1], q.rgb[2]);
         glUniform2f(GLUtil::UniformLoc(sWarp, "uDir"), ux, uy);
         glUniform1f(GLUtil::UniformLoc(sWarp, "uStep"), step);
         glUniform2fv(GLUtil::UniformLoc(sWarp, "uTines"), 16, tines);
         glUniform1i(GLUtil::UniformLoc(sWarp, "uTineN"), n);
         glUniform1f(GLUtil::UniformLoc(sWarp, "uLambda"), lambda);
         glUniform1i(GLUtil::UniformLoc(sWarp, "uLock"), mBrush.lockAlpha ? 1 : 0);
         glActiveTexture(GL_TEXTURE0);
      }, clip);
      if (blendOn)
         glEnable(GL_BLEND);
   }

   void Stroke::Merge(const GLUtil::Fbo& surface, const GLUtil::Fbo& out, const int* scissor)
   {
      GLboolean blendOn = glIsEnabled(GL_BLEND);
      glDisable(GL_BLEND);
      auto setup = [&]()
      {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, surface.tex);
         glUniform1i(GLUtil::UniformLoc(sMerge, "uSurf"), 0);
         glActiveTexture(GL_TEXTURE1);
         glBindTexture(GL_TEXTURE_2D, mCoverage.tex);
         glUniform1i(GLUtil::UniformLoc(sMerge, "uCov"), 1);
         glActiveTexture(GL_TEXTURE2);
         glBindTexture(GL_TEXTURE_2D, mSelection);
         glUniform1i(GLUtil::UniformLoc(sMerge, "uSel"), 2);
         glUniform1i(GLUtil::UniformLoc(sMerge, "uHasSel"), mSelection != 0 ? 1 : 0);
         glUniform3f(GLUtil::UniformLoc(sMerge, "uColor"), mBrush.color[0], mBrush.color[1], mBrush.color[2]);
         glUniform1f(GLUtil::UniformLoc(sMerge, "uOpacity"), std::clamp(mBrush.opacity, 0.0f, 1.0f));
         glUniform1i(GLUtil::UniformLoc(sMerge, "uMode"), mBrush.mode);
         glUniform1i(GLUtil::UniformLoc(sMerge, "uLock"), mBrush.lockAlpha ? 1 : 0);
         glUniform2f(GLUtil::UniformLoc(sMerge, "uTexel"), 1.0f / std::max(1, surface.w), 1.0f / std::max(1, surface.h));
         glUniform2f(GLUtil::UniformLoc(sMerge, "uOff"), mBrush.sourceOffset[0], mBrush.sourceOffset[1]);
         glUniform1i(GLUtil::UniformLoc(sMerge, "uRange"), mBrush.range);
         glUniform1i(GLUtil::UniformLoc(sMerge, "uProtectHue"), mBrush.protectHue ? 1 : 0);
         glUniform1i(GLUtil::UniformLoc(sMerge, "uSponge"), mBrush.spongeMode);
         glUniform3f(GLUtil::UniformLoc(sMerge, "uKey"), mBrush.key[0], mBrush.key[1], mBrush.key[2]);
         glUniform1f(GLUtil::UniformLoc(sMerge, "uKeyTol"), std::max(0.005f, mBrush.keyTolerance * 1.7320508f));
         glUniform1i(GLUtil::UniformLoc(sMerge, "uProtectFg"), mBrush.protectFg && mBrush.mode == kBgErase ? 1 : 0);
         glUniform1i(GLUtil::UniformLoc(sMerge, "uFinal"), mFinal ? 1 : 0);
         glUniform1i(GLUtil::UniformLoc(sMerge, "uDither"), !mBrush.pencil && mBrush.mode == 0 ? 1 : 0);
         glUniform1i(GLUtil::UniformLoc(sMerge, "uSharpMode"), mBrush.sharpMode);
         glUniform1i(GLUtil::UniformLoc(sMerge, "uSrcBlend"), mBrush.sourceBlend);
         glUniform1i(GLUtil::UniformLoc(sMerge, "uWetEdges"), mBrush.wetEdges ? 1 : 0);
         glUniform1f(GLUtil::UniformLoc(sMerge, "uWetReach"), std::max(1.5f, mBrush.size * 0.12f));
         glUniform1f(GLUtil::UniformLoc(sMerge, "uAccum"), std::clamp(mBrush.accumulation, 0.0f, 1.0f));
         glUniform2f(GLUtil::UniformLoc(sMerge, "uAnchor"), mBrush.sourceAnchor[0], mBrush.sourceAnchor[1]);
         {
            // Destination to source about the anchor: flip, then scale, then turn (column-major for GL).
            const float t = mBrush.sourceRotate * 3.14159265f / 180.0f, k = 1.0f / std::max(0.05f, mBrush.sourceScale);
            const float fx = mBrush.sourceFlipX ? -1.0f : 1.0f, fy = mBrush.sourceFlipY ? -1.0f : 1.0f;
            const float c = std::cos(t) * k, s = std::sin(t) * k;
            const float m[4] = { c * fx, s * fx, -s * fy, c * fy };
            glUniformMatrix2fv(GLUtil::UniformLoc(sMerge, "uSrcM"), 1, GL_FALSE, m);
         }
         glActiveTexture(GL_TEXTURE3);
         glBindTexture(GL_TEXTURE_2D, mBrush.sourceTex);
         glUniform1i(GLUtil::UniformLoc(sMerge, "uSrcTex"), 3);
         glUniform1i(GLUtil::UniformLoc(sMerge, "uHasSrc"), mBrush.sourceTex != 0 ? 1 : 0);
         glUniform1i(GLUtil::UniformLoc(sMerge, "uWrap"), mBrush.wrap && !WarpMode(mBrush.mode) && mBrush.mode != kSmudge ? 1 : 0);
         glUniform1f(GLUtil::UniformLoc(sMerge, "uSpread"), std::max(1.0f, mBrush.size * 0.06f));
         glUniform1i(GLUtil::UniformLoc(sMerge, "uGrain"), mBrush.grainTex != 0 ? std::max(1, mBrush.grain) : mBrush.grain);
         glActiveTexture(GL_TEXTURE4);
         glBindTexture(GL_TEXTURE_2D, mBrush.grainTex);
         glUniform1i(GLUtil::UniformLoc(sMerge, "uGrainTex"), 4);
         glUniform1i(GLUtil::UniformLoc(sMerge, "uHasGrainTex"), mBrush.grainTex != 0 ? 1 : 0);
         glUniform1f(GLUtil::UniformLoc(sMerge, "uPatScale"), std::max(0.05f, mBrush.patternScale));
         glUniform2f(GLUtil::UniformLoc(sMerge, "uPatPhase"), mBrush.patternPhase[0], mBrush.patternPhase[1]);
         glUniform2f(GLUtil::UniformLoc(sMerge, "uGrainTexSize"), (float)std::max(1, mBrush.grainTexW), (float)std::max(1, mBrush.grainTexH));
         glUniform1f(GLUtil::UniformLoc(sMerge, "uGrainScale"), std::max(0.05f, mBrush.grainScale));
         glUniform1f(GLUtil::UniformLoc(sMerge, "uGrainDepth"), std::clamp(mBrush.grainDepth, 0.0f, 1.0f));
         glUniform1i(GLUtil::UniformLoc(sMerge, "uGrainMode"), mBrush.grainMode);
         glActiveTexture(GL_TEXTURE0);
      };
      if (scissor != nullptr)
         GLUtil::DrawPass(out, sMerge, setup, scissor);
      else
         GLUtil::RunShaderPass(out, sMerge, setup);
      if (blendOn)
         glEnable(GL_BLEND);
   }

   unsigned Stroke::DisplayTexture(const GLUtil::Fbo& surface)
   {
      if (!mActive)
         return surface.tex;
      Flush();
      if (DirectMode(mBrush.mode))
         return surface.tex;   // smudge and marbling already paint the surface itself
      if (mDisplayStale)
      {
         const int pad = mBrush.wetEdges ? (int)std::ceil(std::max(1.5f, mBrush.size * 0.12f)) + 2 : 0;
         int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
         if (mMergeFullDirty && mAny)
         {
            x0 = std::clamp((int)std::floor(mDirty[0]) - pad, 0, mW);
            y0 = std::clamp((int)std::floor(mDirty[1]) - pad, 0, mH);
            x1 = std::clamp((int)std::ceil(mDirty[2]) + pad, 0, mW);
            y1 = std::clamp((int)std::ceil(mDirty[3]) + pad, 0, mH);
         }
         else if (mFrameAny)
         {
            x0 = std::clamp(mFrameDirty[0] - pad, 0, mW);
            y0 = std::clamp(mFrameDirty[1] - pad, 0, mH);
            x1 = std::clamp(mFrameDirty[2] + pad, 0, mW);
            y1 = std::clamp(mFrameDirty[3] + pad, 0, mH);
         }
         if (x1 > x0 && y1 > y0)
         {
            const int clip[4] = { x0, y0, x1 - x0, y1 - y0 };
            Merge(surface, mDisplay, clip);
         }
         mFrameAny = false;
         mMergeFullDirty = false;
         mDisplayStale = false;
      }
      return mDisplay.tex;
   }

   void Stroke::End(GLUtil::Fbo& surface, int dirty[4])
   {
      dirty[0] = dirty[1] = dirty[2] = dirty[3] = 0;
      if (!mActive)
         return;
      if (mBrush.wet > 0.0f && !DirectMode(mBrush.mode))
         mMixer.EndStroke(mBrush.color, mBrush.autoClean, mBrush.autoLoad);
      // A smoothed stroke catches up to where the pen let go when catchUp is on: the stabiliser by feeding
      // the last pen point until every sample in the window is it, smoothing/string by walking straight there.
      if (mBrush.catchUp)
      {
         if (mBrush.stabiliser > 0)
            for (int k = 0; k < std::clamp(mBrush.stabiliser, 1, 64); k++)
               StabWalk(mPenX, mPenY, mPenP);
         else if (mBrush.smoothing > 0.0f)
            Walk(mPenX, mPenY, mPenP);
      }
      mEnding = true;
      Flush();
      mActive = false;
      if (!mAny)
         return;
      const int x0 = std::clamp((int)std::floor(mDirty[0]), 0, mW), y0 = std::clamp((int)std::floor(mDirty[1]), 0, mH);
      const int x1 = std::clamp((int)std::ceil(mDirty[2]), 0, mW), y1 = std::clamp((int)std::ceil(mDirty[3]), 0, mH);
      if (x1 <= x0 || y1 <= y0)
         return;
      dirty[0] = x0;
      dirty[1] = y0;
      dirty[2] = x1;
      dirty[3] = y1;
      if (DirectMode(mBrush.mode))
         return;   // already on the surface
      const int clip[4] = { x0, y0, x1 - x0, y1 - y0 };
      mFinal = true;
      Merge(surface, mMerge, clip);
      mFinal = false;
      // Only the touched rectangle goes back, so the surface keeps its handle.
      GLint prevRead = 0;
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, mMerge.fbo);
      glBindTexture(GL_TEXTURE_2D, surface.tex);
      glCopyTexSubImage2D(GL_TEXTURE_2D, 0, x0, y0, x0, y0, x1 - x0, y1 - y0);
      glBindTexture(GL_TEXTURE_2D, 0);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, prevRead);
      dirty[0] = x0;
      dirty[1] = y0;
      dirty[2] = x1;
      dirty[3] = y1;
   }

   void Stroke::Cancel()
   {
      mActive = false;
      mQueue.clear();
   }
}
