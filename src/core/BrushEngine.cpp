#include "BrushEngine.h"

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
         "float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7)) + uSeed) * 43758.5453); }\n"
         "float h1(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n"
         "float vn(vec2 p) { vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
         "   return mix(mix(h1(i), h1(i + vec2(1.0, 0.0)), f.x), mix(h1(i + vec2(0.0, 1.0)), h1(i + vec2(1.0, 1.0)), f.x), f.y); }\n"
         "float rnd(float k) { return fract(sin(uSeed * 12.9898 + k * 78.233) * 43758.5453); }\n"
         "vec2 turn(vec2 p, float t) { float c = cos(t), s = sin(t); return vec2(c * p.x + s * p.y, -s * p.x + c * p.y); }\n"
         "void main() {\n"
         "   vec2 p = gl_FragCoord.xy - uCenter;\n"
         "   float c = cos(uAngle), s = sin(uAngle);\n"
         "   p = vec2(c * p.x + s * p.y, -s * p.x + c * p.y);\n"
         "   p.y /= max(uRound, 0.05);\n"
         "   float rad = max(uRadius, 0.5);\n"
         "   float r = length(p) / rad;\n"
         "   float aa = min(1.0, 1.0 / rad);\n"
         "   float a = 0.0;\n"
         "   if (uPencil == 1) {\n"
         "      a = (uTip == 3) ? step(max(abs(p.x), abs(p.y)), rad) : step(length(p), rad);\n"
         "   } else if (uTip == 0 || uTip == 1) {\n"
         "      a = 1.0 - smoothstep(min(uHard, 1.0 - aa), 1.0, r);\n"
         "   } else if (uTip == 2) {\n"
         "      a = 1.0 - smoothstep(min(0.94, 1.0 - aa), 1.0, r);\n"
         "   } else if (uTip == 3) {\n"
         "      float m = max(abs(p.x), abs(p.y)) / rad;\n"
         "      a = 1.0 - smoothstep(min(uHard, 1.0 - aa), 1.0, m);\n"
         "   } else if (uTip == 4) {\n"
         "      a = (1.0 - smoothstep(0.0, 1.0, r)) * step(0.55, hash(floor(gl_FragCoord.xy)));\n"
         "   } else if (uTip == 5) {\n"
         "      float n = hash(floor(gl_FragCoord.xy * 0.5)) * 0.55 + 0.45;\n"
         "      a = (1.0 - smoothstep(uHard * 0.6, 1.0, r)) * n;\n"
         "   } else if (uTip == 6) {\n"
         "      float n = hash(floor(gl_FragCoord.xy * 0.125)) * 0.35;\n"
         "      a = 1.0 - smoothstep(uHard - n, 1.0 + n, r);\n"
         "   } else {\n"
         "      vec2 fc = gl_FragCoord.xy;\n"
         "      float disc = 1.0 - smoothstep(max(uHard, 0.55), 1.0, r);\n"
         "      if (uTip == 7) {\n"            // bristle: hair lines along the stroke
         "         float y = p.y / rad;\n"
         "         float hair = h1(vec2(floor(y * 16.0), 3.0)) * 0.55 + vn(vec2(y * 7.0, 0.5)) * 0.45;\n"
         "         a = (1.0 - smoothstep(0.7, 1.0, r)) * smoothstep(0.38, 0.62, hair);\n"
         "      } else if (uTip == 8) {\n"     // paper grain: the tooth stays put on the page
         "         float g = vn(fc * 0.35) * 0.6 + vn(fc * 0.9) * 0.4;\n"
         "         a = (1.0 - smoothstep(uHard * 0.8, 1.0, r)) * smoothstep(0.3, 0.72, g);\n"
         "      } else if (uTip == 9) {\n"     // halftone: dots on a fixed grid grow with coverage
         "         float cov = 1.0 - smoothstep(uHard * 0.7, 1.0, r);\n"
         "         float d = length(fract(fc / 9.0) - 0.5);\n"
         "         float R = sqrt(cov) * 0.62;\n"
         "         a = (1.0 - smoothstep(R - 0.07, R + 0.03, d)) * step(0.02, cov);\n"
         "      } else if (uTip == 10) {\n"    // watercolour: ragged edge, pigment pooled at the rim
         "         float n = vn(fc * 0.05 + uSeed) * 0.5 + vn(fc * 0.2) * 0.5;\n"
         "         float rr = r * (0.82 + 0.36 * n);\n"
         "         a = (1.0 - smoothstep(0.86, 1.0, rr)) * (0.3 + 0.7 * smoothstep(0.5, 0.95, rr)) * (0.75 + 0.25 * vn(fc * 0.6));\n"
         "      } else if (uTip == 11) {\n"    // brushed metal: fine streaks along the stroke
         "         float s = h1(vec2(floor(p.y * 1.3), 7.0)) * 0.6 + vn(vec2(p.y * 0.5, 1.0)) * 0.4;\n"
         "         a = disc * (0.25 + 0.75 * s * s);\n"
         "      } else if (uTip == 12) {\n"    // cloth: an over-under weave fixed on the page
         "         vec2 w = fc / 6.0;\n"
         "         float chk = mod(floor(w.x) + floor(w.y), 2.0);\n"
         "         float thread = mix(0.5 + 0.5 * sin(w.y * 6.2832), 0.5 + 0.5 * sin(w.x * 6.2832), chk);\n"
         "         a = (1.0 - smoothstep(uHard * 0.8, 1.0, r)) * (0.25 + 0.75 * thread) * (0.85 + 0.15 * vn(fc * 0.5));\n"
         "      } else if (uTip == 13) {\n"    // leaf: a turned, sized leaf with a vein per dab
         "         vec2 q = turn(p, rnd(1.0) * 6.2832) / (rad * (0.55 + 0.45 * rnd(2.0)));\n"
         "         float wdt = 0.42 * (1.0 - q.x * q.x);\n"
         "         a = (1.0 - smoothstep(wdt - 0.05, wdt, abs(q.y))) * step(abs(q.x), 1.0);\n"
         "         a *= 1.0 - 0.4 * (1.0 - smoothstep(0.0, 0.04, abs(q.y))) * step(q.x, 0.85);\n"
         "         a *= 0.75 + 0.25 * rnd(3.0);\n"
         "      } else if (uTip == 14) {\n"    // grass: a blade rising from the dab, leaning and bending
         "         vec2 q = turn(p, (rnd(1.0) - 0.5) * 0.9) / rad;\n"
         "         float y = q.y / (0.6 + 0.4 * rnd(2.0));\n"
         "         float bend = (rnd(3.0) - 0.5) * 0.7 * y * y;\n"
         "         float wdt = 0.1 * (1.0 - y) + 0.005;\n"
         "         a = step(0.0, y) * step(y, 1.0) * (1.0 - smoothstep(wdt * 0.6, wdt, abs(q.x - bend)));\n"
         "      } else if (uTip == 15) {\n"    // sparkle: a four-point star with a soft core
         "         vec2 q = turn(p, rnd(1.0) * 1.5708) / (rad * (0.35 + 0.65 * rnd(2.0)));\n"
         "         float d = sqrt(abs(q.x)) + sqrt(abs(q.y));\n"
         "         a = max(1.0 - smoothstep(0.8, 1.0, d), 0.6 * (1.0 - smoothstep(0.0, 0.3, length(q))));\n"
         "      } else {\n"                    // sponge: open cells that change every dab
         "         float c = vn(fc * 0.12 + uSeed * 3.1) * 0.65 + vn(fc * 0.33) * 0.35;\n"
         "         a = (1.0 - smoothstep(0.7, 1.0, r)) * smoothstep(0.42, 0.56, c);\n"
         "      }\n"
         "   }\n"
         "   a = clamp(a, 0.0, 1.0) * uFlow;\n"
         "   fragColor = vec4(uCol * a, a);\n"
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
         "void main() {\n"
         "   vec2 uv = gl_FragCoord.xy * uTexel;\n"
         "   vec4 d = texture(uSrc, uv);\n"
         "   vec4 s = texture(uSrc, uv + uOff * uTexel);\n"
         "   float r = length(gl_FragCoord.xy - uCenter) / max(uRadius, 0.5);\n"
         "   float m = (1.0 - smoothstep(uHard, 1.0, r)) * uStrength;\n"
         "   if (uHasSel == 1) m *= texture(uSel, uv).r;\n"
         "   float a = mix(d.a, s.a, m);\n"
         "   vec3 rgb = mix(d.rgb * d.a, s.rgb * s.a, m) / max(a, 1e-4);\n"
         "   if (a <= 1e-4) rgb = d.rgb;\n"
         "   fragColor = uLock == 1 ? vec4(mix(d.rgb, s.rgb, m * s.a), d.a) : vec4(rgb, a);\n"
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
         "uniform sampler2D uSrcTex;\n"
         "uniform int uHasSrc;\n"
         "vec4 srcAt(vec2 uv) { return uHasSrc == 1 ? texture(uSrcTex, uv) : texture(uSurf, uv); }\n"
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
         "void main() {\n"
         "   vec4 d = texture(uSurf, vUv);\n"
         "   vec4 c = texture(uCov, vUv);\n"
         "   float a = clamp(c.a * uOpacity, 0.0, 1.0);\n"
         "   vec3 col = c.a > 1e-5 ? clamp(c.rgb / c.a, 0.0, 1.0) : uColor;\n"
         "   if (uHasSel == 1) a *= texture(uSel, vUv).r;\n"
         "   if (a <= 0.0) { fragColor = d; return; }\n"
         "   if (uMode >= 6) {\n"
         "      vec2 src = vUv + uOff * uTexel;\n"
         "      if (uMode == 10) { fragColor = mix(d, srcAt(src), a); return; }\n"
         "      vec3 r = d.rgb;\n"
         "      if (uMode == 6) r = soft(vUv);\n"
         "      if (uMode == 7) r = clamp(d.rgb + (d.rgb - soft(vUv)) * 1.5, 0.0, 1.0);\n"
         "      if (uMode == 8) r = pow(max(d.rgb, 0.0), vec3(0.7));\n"
         "      if (uMode == 9) r = pow(max(d.rgb, 0.0), vec3(1.45));\n"
         "      if (uMode == 11) r = clamp(srcAt(src).rgb + soft(vUv) - softSrc(src), 0.0, 1.0);\n"
         "      if (uMode == 8 || uMode == 9) {\n"   // dodge / burn reach shadows, midtones or highlights
         "         float l = dot(d.rgb, vec3(0.299, 0.587, 0.114));\n"
         "         float w = uRange == 0 ? 1.0 - smoothstep(0.15, 0.6, l) : uRange == 2 ? smoothstep(0.4, 0.85, l) : 1.0;\n"
         "         r = mix(d.rgb, r, w);\n"
         "      }\n"
         "      fragColor = vec4(mix(d.rgb, r, a), d.a);\n"
         "      return;\n"
         "   }\n"
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
         "   fragColor = vec4(rgb, oa);\n"
         "}\n";

      unsigned sDab = 0, sMerge = 0, sSmudge = 0;
      bool sTried = false;
      struct DabLoc
      {
         int center, radius, hard, flow, tip, seed, angle, round, pencil, col;
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
         b = Make(kRound, 6, 0.9f, 1, 1, 0.08f);
         b.smoothing = 0.3f;
         add("Basic", "Ink", b);
         b = Make(kHardRound, 2, 1, 1, 1, 0.08f);
         b.pressureSize = false;
         b.smoothing = 0.2f;
         add("Basic", "Fine liner", b);
         b = Make(kRound, 24, 0.7f, 0.6f, 1, 0.1f);
         b.pressureSize = false;
         add("Basic", "Marker", b);
         b = Make(kSoftRound, 120, 0, 1, 0.08f, 0.1f);
         b.pressureSize = false;
         b.pressureOpacity = true;
         add("Basic", "Soft airbrush", b);
         add("Basic", "Hard round", Make(kRound, 30, 1, 1, 1, 0.12f));
         b = Make(kRound, 40, 0.9f, 1, 1, 0.06f);
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
         b = Make(kSoftRound, 60, 0, 1, 0.5f, 0.1f);
         b.mode = kErase;
         add("Eraser", "Eraser soft", b);
         b = Make(kRound, 30, 1, 1, 1, 0.1f);
         b.mode = kErase;
         b.pressureSize = false;
         add("Eraser", "Eraser hard", b);
         b = Make(kGrain, 50, 0.6f, 1, 0.7f, 0.08f);
         b.mode = kErase;
         add("Eraser", "Eraser grain", b);
         return p;
      }();
      return presets;
   }

   Stroke::~Stroke()
   {
      GLUtil::DestroyFbo(mCoverage);
      GLUtil::DestroyFbo(mDisplay);
      GLUtil::DestroyFbo(mMerge);
   }

   bool Stroke::EnsurePrograms()
   {
      if (!sTried)
      {
         sTried = true;
         sDab = GLUtil::CompileProgram(kDabFrag);
         sMerge = GLUtil::CompileProgram(kMergeFrag);
         sSmudge = GLUtil::CompileProgram(kSmudgeFrag);
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
         }
      }
      return sDab != 0 && sMerge != 0 && sSmudge != 0;
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
      mSelection = selection;
      mSurface = &surface;
      mEnding = false;
      mActive = true;
      mAny = false;
      mDabCount = 0;
      mQueue.clear();
      sStrokeSeed = sStrokeSeed * 1664525u + 1013904223u;
      mRng = sStrokeSeed | 1u;
      const float p = pressure < 0.0f ? 1.0f : pressure;
      mLastX = mPenX = x;
      mLastY = mPenY = y;
      mLastP = mPenP = p;
      mCarry = 0.0f;
      mDabX = x;
      mDabY = y;
      mDir = 0.0f;
      mDist = 0.0f;
      mPrevX = x;
      mPrevY = y;
      Dab(x, y, p, Rand(mRng) * 100.0f);
      mDisplayStale = true;
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
      if (mBrush.jitter > 0.0f)
      {
         x += (Rand(mRng) - 0.5f) * mBrush.jitter * mBrush.size;
         y += (Rand(mRng) - 0.5f) * mBrush.jitter * mBrush.size;
      }
      const float p = pressure < 0.0f ? 1.0f : std::clamp(pressure, 0.0f, 1.0f);
      mDist += std::hypot(x - mPrevX, y - mPrevY);
      const float fx = mPrevX, fy = mPrevY;
      mPrevX = x;
      mPrevY = y;
      // Per-dab dynamics, drawn here so mirrored copies match their original.
      const Brush& b = mBrush;
      mSize = b.sizeJitter > 0.0f ? 1.0f - Rand(mRng) * std::clamp(b.sizeJitter, 0.0f, 1.0f) : 1.0f;
      if (b.taperIn > 0.0f)
         mSize *= std::clamp(mDist / b.taperIn, 0.05f, 1.0f);
      mFlow = b.opacityJitter > 0.0f ? 1.0f - Rand(mRng) * std::clamp(b.opacityJitter, 0.0f, 1.0f) : 1.0f;
      if (b.hueJitter > 0.0f || b.satJitter > 0.0f || b.valJitter > 0.0f)
      {
         const float dh = (Rand(mRng) - 0.5f) * b.hueJitter, ds = (Rand(mRng) - 0.5f) * 2.0f * b.satJitter, dv = (Rand(mRng) - 0.5f) * 2.0f * b.valJitter;
         JitterColour(b.color, dh, ds, dv, mRgb);
      }
      else
         std::copy(b.color, b.color + 3, mRgb);
      Queue(x, y, p, seed, mDir, fx, fy);
      for (const auto& m : mMirrors)
         Queue(m[0] * x + m[1] * y + m[2], m[3] * x + m[4] * y + m[5], p, seed, std::atan2(m[3] * std::cos(mDir) + m[4] * std::sin(mDir), m[0] * std::cos(mDir) + m[1] * std::sin(mDir)),
               m[0] * fx + m[1] * fy + m[2], m[3] * fx + m[4] * fy + m[5]);
   }

   void Stroke::Queue(float x, float y, float p, float seed, float dir, float fx, float fy)
   {
      Pending q{ x, y, p, seed, dir, mDist, mSize, mFlow, { mRgb[0], mRgb[1], mRgb[2] }, fx, fy };
      mQueue.push_back(q);
      const float r = 0.5f * mBrush.size + 2.0f;
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
   }

   void Stroke::Walk(float x, float y, float pressure)
   {
      const float dx = x - mLastX, dy = y - mLastY;
      const float d = std::sqrt(dx * dx + dy * dy);
      if (d <= 1e-4f)
         return;
      const float scale = mBrush.pressureSize ? std::max(0.05f, std::max(mLastP, pressure)) : 1.0f;
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

   void Stroke::MoveTo(float x, float y, float pressure)
   {
      if (!mActive)
         return;
      const float p = pressure < 0.0f ? 1.0f : pressure;
      mPenX = x;
      mPenY = y;
      mPenP = p;
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
      const float scale = (b.pressureSize ? std::max(0.05f, q.pressure) : 1.0f) * q.size * taper;
      float r = 0.5f * b.size * scale;
      float cx = q.x, cy = q.y;
      if (b.pencil)
      {
         r = std::max(0.5f, std::round(r * 2.0f) * 0.5f);
         cx = std::floor(cx) + 0.5f;
         cy = std::floor(cy) + 0.5f;
      }
      const float flow = b.flow * q.flow * (b.pressureOpacity ? q.pressure : 1.0f);
      if (b.mode == kSmudge)
      {
         SmudgeDab(q, r, std::clamp(flow * b.opacity, 0.0f, 1.0f));
         mDabCount++;
         return;
      }
      int sc[4] = { (int)std::floor(cx - r - 2), (int)std::floor(cy - r - 2), 0, 0 };
      sc[2] = (int)std::ceil(cx + r + 2) - sc[0];
      sc[3] = (int)std::ceil(cy + r + 2) - sc[1];
      const int x0 = std::max(0, sc[0]), y0 = std::max(0, sc[1]);
      const int x1 = std::min(mW, sc[0] + sc[2]), y1 = std::min(mH, sc[1] + sc[3]);
      if (x1 <= x0 || y1 <= y0)
         return;
      const int clip[4] = { x0, y0, x1 - x0, y1 - y0 };
      const float angle = b.angle * 3.14159265f / 180.0f;
      GLUtil::DrawPass(mCoverage, sDab, [&]()
      {
         glUniform2f(sDabLoc.center, cx, cy);
         glUniform1f(sDabLoc.radius, r);
         glUniform1f(sDabLoc.hard, std::clamp(b.hardness, 0.0f, 0.98f));
         glUniform1f(sDabLoc.flow, std::clamp(flow, 0.0f, 1.0f));
         glUniform1i(sDabLoc.tip, b.tip);
         glUniform1f(sDabLoc.seed, q.seed);
         glUniform1f(sDabLoc.angle, angle + (b.follow ? q.dir : 0.0f));
         glUniform1f(sDabLoc.round, std::clamp(b.roundness, 0.05f, 1.0f));
         glUniform1i(sDabLoc.pencil, b.pencil ? 1 : 0);
         glUniform3f(sDabLoc.col, q.rgb[0], q.rgb[1], q.rgb[2]);
      }, clip);
      mDabCount++;
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
         glUniform1i(glGetUniformLocation(sSmudge, "uSrc"), 0);
         glActiveTexture(GL_TEXTURE1);
         glBindTexture(GL_TEXTURE_2D, mSelection);
         glUniform1i(glGetUniformLocation(sSmudge, "uSel"), 1);
         glUniform1i(glGetUniformLocation(sSmudge, "uHasSel"), mSelection != 0 ? 1 : 0);
         glUniform2f(glGetUniformLocation(sSmudge, "uTexel"), 1.0f / std::max(1, mW), 1.0f / std::max(1, mH));
         glUniform2f(glGetUniformLocation(sSmudge, "uCenter"), q.x, q.y);
         glUniform2f(glGetUniformLocation(sSmudge, "uOff"), ox, oy);
         glUniform1f(glGetUniformLocation(sSmudge, "uRadius"), r);
         glUniform1f(glGetUniformLocation(sSmudge, "uHard"), std::clamp(mBrush.hardness, 0.0f, 0.98f));
         glUniform1f(glGetUniformLocation(sSmudge, "uStrength"), strength);
         glUniform1i(glGetUniformLocation(sSmudge, "uLock"), mBrush.lockAlpha ? 1 : 0);
         glActiveTexture(GL_TEXTURE0);
      }, clip);
      if (blendOn)
         glEnable(GL_BLEND);
   }

   void Stroke::Merge(const GLUtil::Fbo& surface, const GLUtil::Fbo& out)
   {
      GLboolean blendOn = glIsEnabled(GL_BLEND);
      glDisable(GL_BLEND);
      GLUtil::RunShaderPass(out, sMerge, [&]()
      {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, surface.tex);
         glUniform1i(glGetUniformLocation(sMerge, "uSurf"), 0);
         glActiveTexture(GL_TEXTURE1);
         glBindTexture(GL_TEXTURE_2D, mCoverage.tex);
         glUniform1i(glGetUniformLocation(sMerge, "uCov"), 1);
         glActiveTexture(GL_TEXTURE2);
         glBindTexture(GL_TEXTURE_2D, mSelection);
         glUniform1i(glGetUniformLocation(sMerge, "uSel"), 2);
         glUniform1i(glGetUniformLocation(sMerge, "uHasSel"), mSelection != 0 ? 1 : 0);
         glUniform3f(glGetUniformLocation(sMerge, "uColor"), mBrush.color[0], mBrush.color[1], mBrush.color[2]);
         glUniform1f(glGetUniformLocation(sMerge, "uOpacity"), std::clamp(mBrush.opacity, 0.0f, 1.0f));
         glUniform1i(glGetUniformLocation(sMerge, "uMode"), mBrush.mode);
         glUniform1i(glGetUniformLocation(sMerge, "uLock"), mBrush.lockAlpha ? 1 : 0);
         glUniform2f(glGetUniformLocation(sMerge, "uTexel"), 1.0f / std::max(1, surface.w), 1.0f / std::max(1, surface.h));
         glUniform2f(glGetUniformLocation(sMerge, "uOff"), mBrush.sourceOffset[0], mBrush.sourceOffset[1]);
         glUniform1i(glGetUniformLocation(sMerge, "uRange"), mBrush.range);
         glActiveTexture(GL_TEXTURE3);
         glBindTexture(GL_TEXTURE_2D, mBrush.sourceTex);
         glUniform1i(glGetUniformLocation(sMerge, "uSrcTex"), 3);
         glUniform1i(glGetUniformLocation(sMerge, "uHasSrc"), mBrush.sourceTex != 0 ? 1 : 0);
         glUniform1f(glGetUniformLocation(sMerge, "uSpread"), std::max(1.0f, mBrush.size * 0.06f));
         glActiveTexture(GL_TEXTURE0);
      });
      if (blendOn)
         glEnable(GL_BLEND);
   }

   unsigned Stroke::DisplayTexture(const GLUtil::Fbo& surface)
   {
      if (!mActive)
         return surface.tex;
      Flush();
      if (mBrush.mode == kSmudge)
         return surface.tex;   // smudge already paints the surface itself
      if (mDisplayStale)
      {
         Merge(surface, mDisplay);
         mDisplayStale = false;
      }
      return mDisplay.tex;
   }

   void Stroke::End(GLUtil::Fbo& surface, int dirty[4])
   {
      dirty[0] = dirty[1] = dirty[2] = dirty[3] = 0;
      if (!mActive)
         return;
      // A smoothed stroke catches up to where the pen let go.
      if (mBrush.smoothing > 0.0f)
         Walk(mPenX, mPenY, mPenP);
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
      if (mBrush.mode == kSmudge)
         return;   // already on the surface
      Merge(surface, mMerge);
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
