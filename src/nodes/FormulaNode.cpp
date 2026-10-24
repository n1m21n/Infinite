#include "FormulaNode.h"

#include "gl3.h"
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <regex>
#include <sstream>

#include "Transport.h"

namespace
{
   // The user writes the body of shape(); everything above is provided for them.
   const char* kPreamble =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform float uTime;\n"
      "uniform float uA;\n"
      "uniform float uB;\n"
      "uniform float uC;\n"
      "uniform float uD;\n"
      "uniform float uAspect;\n"
      "\n"
      "#define PI 3.14159265359\n"
      "#define TAU 6.28318530718\n"
      "float rand(vec2 co) { return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453); }\n"
      "mat2 rot(float a) { float s = sin(a), c = cos(a); return mat2(c, -s, s, c); }\n"
      "float sdCircle(vec2 p, float r) { return length(p) - r; }\n"
      "float sdBox(vec2 p, vec2 b) { vec2 d = abs(p) - b; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0); }\n"
      "\n"
      "// uv is 0..1, p is centered and aspect-corrected, t is seconds, knobs uA..uD are 0..1.\n"
      "// Return the final RGBA for this pixel.\n"
      "vec4 shape(vec2 uv, vec2 p, float t) {\n";

   const char* kEpilogue =
      "\n}\n"
      "void main() {\n"
      "   vec2 p = vUv - vec2(0.5);\n"
      "   p.x *= uAspect;\n"
      "   fragColor = shape(vUv, p, uTime);\n"
      "}\n";

   const char* kDefaultFormula =
      "   float d = sdCircle(p, 0.2 + uA * 0.2);\n"
      "   d += sin(atan(p.y, p.x) * 6.0 + t) * uB * 0.08;\n"
      "   float mask = smoothstep(0.005, -0.005, d);\n"
      "   vec3 col = mix(vec3(0.05), vec3(uC, 0.6, 1.0 - uC), mask);\n"
      "   return vec4(col, 1.0);";
}

namespace
{
   // Everything shadertoy.com provides to a pass. The user's code (Common, then the
   // pass) follows, then main() calls mainImage.
   const char* kShadertoyPreamble =
      "#version 150\n"
      "out vec4 infFragColor;\n"
      "uniform vec3 iResolution;\n"
      "uniform float iTime;\n"
      "uniform float iTimeDelta;\n"
      "uniform float iFrameRate;\n"
      "uniform int iFrame;\n"
      "uniform vec4 iMouse;\n"
      "uniform vec4 iDate;\n"
      "uniform float iSampleRate;\n"
      "uniform float iChannelTime[4];\n"
      "uniform vec3 iChannelResolution[4];\n"
      "uniform sampler2D iChannel0;\n"
      "uniform sampler2D iChannel1;\n"
      "uniform sampler2D iChannel2;\n"
      "uniform sampler2D iChannel3;\n"
      "#define texture2D texture\n"
      "#define HW_PERFORMANCE 1\n";

   // The image pass is opaque, as on the site; a buffer keeps its alpha.
   const char* kShadertoyMainImage =
      "\nvoid main() { vec4 c = vec4(0.0, 0.0, 0.0, 1.0); mainImage(c, gl_FragCoord.xy); infFragColor = vec4(c.rgb, 1.0); }\n";
   const char* kShadertoyMainBuffer =
      "\nvoid main() { vec4 c = vec4(0.0); mainImage(c, gl_FragCoord.xy); infFragColor = c; }\n";

   // Shadertoy's own "new shader" template, so the picture moves at once.
   const char* kShadertoyDefault =
      "void mainImage( out vec4 fragColor, in vec2 fragCoord )\n"
      "{\n"
      "    // Normalized pixel coordinates (from 0 to 1)\n"
      "    vec2 uv = fragCoord/iResolution.xy;\n"
      "\n"
      "    // Time varying pixel color\n"
      "    vec3 col = 0.5 + 0.5*cos(iTime+uv.xyx+vec3(0,2,4));\n"
      "\n"
      "    // Output to screen\n"
      "    fragColor = vec4(col,1.0);\n"
      "}\n";

   int CountLines(const char* text)
   {
      int n = 0;
      for (const char* c = text; *c != '\0'; c++)
         n += *c == '\n' ? 1 : 0;
      return n;
   }
}

namespace
{
   // A starter library of classic procedural-shader forms. Each is the body of
   // shape(uv, p, t): `p` is centred (-0.5..0.5), `t` is transport seconds, and
   // uA..uD are the four knobs.
   const std::vector<FormulaNode::Preset> kPresets = {
      { "Circle (default)",
        "   float d = sdCircle(p, 0.2 + uA * 0.2);\n"
        "   d += sin(atan(p.y, p.x) * 6.0 + t) * uB * 0.08;\n"
        "   float mask = smoothstep(0.005, -0.005, d);\n"
        "   vec3 col = mix(vec3(0.05), vec3(uC, 0.6, 1.0 - uC), mask);\n"
        "   return vec4(col, 1.0);" },

      { "Rose curve",
        "   float a = atan(p.y, p.x);\n"
        "   float r = length(p);\n"
        "   float petals = floor(2.0 + uA * 10.0);\n"
        "   float rose = abs(cos(a * petals + t * 0.5)) * (0.15 + uB * 0.3);\n"
        "   float mask = smoothstep(0.01, -0.01, r - rose);\n"
        "   vec3 col = mix(vec3(0.03), vec3(1.0, uC, 1.0 - uC), mask);\n"
        "   return vec4(col, 1.0);" },

      { "Superformula",
        "   float a = atan(p.y, p.x);\n"
        "   float m = floor(3.0 + uA * 12.0);\n"
        "   float n1 = 0.3 + uB * 4.0;\n"
        "   float t1 = pow(abs(cos(m * a / 4.0)), n1);\n"
        "   float t2 = pow(abs(sin(m * a / 4.0)), n1);\n"
        "   float r = pow(t1 + t2, -1.0 / max(n1, 0.01)) * (0.1 + uC * 0.25);\n"
        "   float mask = smoothstep(0.01, -0.01, length(p) - r);\n"
        "   return vec4(mix(vec3(0.02), vec3(0.9, 0.5, uD), mask), 1.0);" },

      { "Plasma",
        "   vec2 q = p * (4.0 + uA * 16.0);\n"
        "   float v = sin(q.x + t) + sin(q.y + t * 1.3)\n"
        "           + sin((q.x + q.y) * 0.7 + t * 0.7)\n"
        "           + sin(length(q) * (1.0 + uB * 3.0) - t * 2.0);\n"
        "   v *= 0.25;\n"
        "   vec3 col = 0.5 + 0.5 * cos(6.2831 * (vec3(0.0, 0.33, 0.67) + v + uC));\n"
        "   return vec4(col, 1.0);" },

      { "Metaballs",
        "   float f = 0.0;\n"
        "   for (int i = 0; i < 5; i++) {\n"
        "      float fi = float(i);\n"
        "      vec2 c = 0.3 * vec2(sin(t * (0.4 + fi * 0.17) + fi), cos(t * (0.3 + fi * 0.23) + fi * 2.0));\n"
        "      f += (0.02 + uA * 0.05) / max(dot(p - c, p - c), 1e-4);\n"
        "   }\n"
        "   float mask = smoothstep(0.9, 1.4, f);\n"
        "   vec3 col = mix(vec3(0.02), vec3(uB, 1.0 - uB, uC), mask);\n"
        "   return vec4(col, 1.0);" },

      { "Concentric rings",
        "   float r = length(p);\n"
        "   float rings = sin(r * (20.0 + uA * 120.0) - t * (1.0 + uB * 6.0));\n"
        "   float mask = smoothstep(0.0, 0.05, rings);\n"
        "   vec3 col = mix(vec3(uC * 0.2), vec3(1.0), mask);\n"
        "   return vec4(col, 1.0);" },

      { "Spiral",
        "   float a = atan(p.y, p.x);\n"
        "   float r = length(p);\n"
        "   float arms = floor(1.0 + uA * 8.0);\n"
        "   float v = sin(a * arms + log(max(r, 1e-3)) * (4.0 + uB * 20.0) - t * 2.0);\n"
        "   float mask = smoothstep(0.0, 0.06, v);\n"
        "   return vec4(mix(vec3(0.02), vec3(1.0, uC, 0.4), mask), 1.0);" },

      { "Truchet tiles",
        "   vec2 q = p * (3.0 + uA * 12.0);\n"
        "   vec2 cell = floor(q);\n"
        "   vec2 f = fract(q) - 0.5;\n"
        "   float flip = step(0.5, fract(sin(dot(cell, vec2(127.1, 311.7)) + floor(t * uB)) * 43758.5453));\n"
        "   if (flip > 0.5) f.x = -f.x;\n"
        "   float d = abs(length(f - vec2(0.5, 0.5)) - 0.5);\n"
        "   d = min(d, abs(length(f + vec2(0.5, 0.5)) - 0.5));\n"
        "   float mask = smoothstep(0.02 + uC * 0.15, 0.0, d);\n"
        "   return vec4(vec3(mask), 1.0);" },

      { "Hex grid",
        "   vec2 q = p * (4.0 + uA * 16.0);\n"
        "   q.x *= 1.1547;\n"
        "   vec2 cell = floor(q + 0.5);\n"
        "   vec2 f = q - cell;\n"
        "   float d = max(abs(f.x) * 0.866 + abs(f.y) * 0.5, abs(f.y));\n"
        "   float mask = smoothstep(0.5, 0.5 - 0.02 - uB * 0.2, d);\n"
        "   float tint = fract(sin(dot(cell, vec2(12.9898, 78.233))) * 43758.5453);\n"
        "   return vec4(mix(vec3(0.03), vec3(tint, uC, 1.0 - tint), mask), 1.0);" },

      { "Mandelbrot",
        "   vec2 c = p * (3.0 - uA * 2.5) + vec2(-0.6, 0.0);\n"
        "   vec2 z = vec2(0.0);\n"
        "   float it = 0.0;\n"
        "   for (int i = 0; i < 80; i++) {\n"
        "      z = vec2(z.x*z.x - z.y*z.y, 2.0*z.x*z.y) + c;\n"
        "      if (dot(z, z) > 4.0) break;\n"
        "      it += 1.0;\n"
        "   }\n"
        "   float m = it / 80.0;\n"
        "   vec3 col = 0.5 + 0.5 * cos(6.2831 * (vec3(0.0, 0.33, 0.67) + m * (1.0 + uB * 4.0) + uC));\n"
        "   return vec4(col * step(m, 0.999), 1.0);" },

      { "Julia set",
        "   vec2 z = p * (3.0 - uA * 2.0);\n"
        "   vec2 c = vec2(-0.8 + sin(t * 0.2) * 0.2 * uB, 0.156 + cos(t * 0.17) * 0.2 * uB);\n"
        "   float it = 0.0;\n"
        "   for (int i = 0; i < 80; i++) {\n"
        "      z = vec2(z.x*z.x - z.y*z.y, 2.0*z.x*z.y) + c;\n"
        "      if (dot(z, z) > 4.0) break;\n"
        "      it += 1.0;\n"
        "   }\n"
        "   float m = it / 80.0;\n"
        "   return vec4(0.5 + 0.5 * cos(6.2831 * (vec3(0.0, 0.4, 0.7) + m * 3.0 + uC)), 1.0);" },

      { "Moire",
        "   float a = length(p - vec2(0.15 * sin(t * 0.4), 0.0)) * (40.0 + uA * 200.0);\n"
        "   float b = length(p + vec2(0.15 * sin(t * 0.4), 0.0)) * (40.0 + uA * 200.0);\n"
        "   float v = sin(a) * sin(b);\n"
        "   float mask = smoothstep(0.0, 0.02, v);\n"
        "   return vec4(mix(vec3(uB * 0.3), vec3(1.0, 1.0, uC), mask), 1.0);" },

      { "Voronoi cells",
        "   vec2 q = p * (3.0 + uA * 12.0);\n"
        "   vec2 i = floor(q), f = fract(q);\n"
        "   float best = 8.0; vec2 bestCell = vec2(0.0);\n"
        "   for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) {\n"
        "      vec2 g = vec2(float(x), float(y));\n"
        "      vec2 o = fract(sin(vec2(dot(i+g, vec2(127.1,311.7)), dot(i+g, vec2(269.5,183.3)))) * 43758.5453);\n"
        "      o = 0.5 + 0.5 * sin(t * uB + 6.2831 * o);\n"
        "      float d = length(g + o - f);\n"
        "      if (d < best) { best = d; bestCell = i + g; }\n"
        "   }\n"
        "   float tint = fract(sin(dot(bestCell, vec2(12.9898, 78.233))) * 43758.5453);\n"
        "   return vec4(mix(vec3(tint, uC, 1.0 - tint) * (1.0 - best), vec3(1.0), smoothstep(0.0, 0.03, best - 0.9)), 1.0);" },

      { "Interference waves",
        "   float v = 0.0;\n"
        "   for (int i = 0; i < 6; i++) {\n"
        "      float fi = float(i);\n"
        "      vec2 src = 0.4 * vec2(cos(fi * 1.7 + t * 0.2 * uB), sin(fi * 2.3 + t * 0.15 * uB));\n"
        "      v += sin(length(p - src) * (30.0 + uA * 120.0) - t * 3.0);\n"
        "   }\n"
        "   v /= 6.0;\n"
        "   vec3 col = 0.5 + 0.5 * cos(6.2831 * (vec3(0.0, 0.3, 0.6) + v + uC));\n"
        "   return vec4(col, 1.0);" },

      { "Star field",
        "   vec2 q = p * (6.0 + uA * 30.0);\n"
        "   vec2 cell = floor(q);\n"
        "   vec2 f = fract(q) - 0.5;\n"
        "   float r = fract(sin(dot(cell, vec2(127.1, 311.7))) * 43758.5453);\n"
        "   float twinkle = 0.5 + 0.5 * sin(t * (1.0 + r * 4.0) + r * 20.0);\n"
        "   float d = length(f);\n"
        "   float star = smoothstep(0.06 + uB * 0.1, 0.0, d) * step(0.82 - uC * 0.3, r) * twinkle;\n"
        "   return vec4(vec3(star), 1.0);" },

      { "Checker warp",
        "   vec2 q = p;\n"
        "   q += 0.1 * uA * vec2(sin(q.y * 10.0 + t), cos(q.x * 10.0 - t));\n"
        "   vec2 g = floor(q * (4.0 + uB * 20.0));\n"
        "   float c = mod(g.x + g.y, 2.0);\n"
        "   return vec4(mix(vec3(0.05), vec3(uC, 1.0 - uC, 0.8), c), 1.0);" },

      { "Phyllotaxis",
        "   const int N = 220;\n"
        "   float best = 1e9;\n"
        "   vec3 tint = vec3(0.0);\n"
        "   float golden = 2.39996323 + (uD - 0.5) * 0.02;\n"
        "   for (int i = 0; i < N; i++) {\n"
        "      float fi = float(i);\n"
        "      float a = fi * golden + t * 0.2;\n"
        "      float r = sqrt(fi / float(N)) * (0.18 + uA * 0.28);\n"
        "      float d = length(p - vec2(cos(a), sin(a)) * r)\n"
        "              - (0.004 + uB * 0.016) * (0.4 + r * 2.0);\n"
        "      if (d < best) {\n"
        "         best = d;\n"
        "         tint = 0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + fi * 0.05 + uC * 6.0);\n"
        "      }\n"
        "   }\n"
        "   return vec4(mix(vec3(0.02), tint, smoothstep(0.004, -0.004, best)), 1.0);" },

      { "Kaleidoscope",
        "   float a = atan(p.y, p.x);\n"
        "   float r = length(p);\n"
        "   float seg = TAU / floor(3.0 + uA * 12.0);\n"
        "   a = abs(mod(a + seg * 0.5, seg) - seg * 0.5);\n"
        "   vec2 q = vec2(cos(a), sin(a)) * r * (2.0 + uB * 6.0) + t * 0.15;\n"
        "   float v = sin(q.x * 3.0) * sin(q.y * 3.0 + t) + sin(length(q) * 5.0 - t);\n"
        "   vec3 col = 0.5 + 0.5 * cos(vec3(0.0, 2.1, 4.2) + v * 2.0 + uC * 6.0);\n"
        "   return vec4(col * smoothstep(0.55, 0.0, r), 1.0);" },

      { "Domain warp",
        "   vec2 w = p * (3.0 + uA * 5.0);\n"
        "   float acc = 0.0, amp = 0.5;\n"
        "   for (int i = 0; i < 5; i++) {\n"
        "      vec2 g = floor(w), f = fract(w);\n"
        "      f = f * f * (3.0 - 2.0 * f);\n"
        "      float n = mix(mix(rand(g), rand(g + vec2(1.0, 0.0)), f.x),\n"
        "                    mix(rand(g + vec2(0.0, 1.0)), rand(g + vec2(1.0, 1.0)), f.x), f.y);\n"
        "      acc += n * amp;\n"
        "      amp *= 0.5;\n"
        // Feeding the accumulator back into the sample position is what makes
        // this a warp rather than plain fbm - each octave drags the next.
        "      w = w * 2.0 + vec2(acc * uB * 3.0 + t * 0.1, acc * uB * 3.0);\n"
        "   }\n"
        "   return vec4(0.5 + 0.5 * cos(vec3(0.0, 1.8, 3.6) + acc * 4.0 + uC * 6.0), 1.0);" },

      { "Apollonian gasket",
        "   vec2 z = p * (1.0 + uA * 2.0);\n"
        "   float scale = 1.0;\n"
        "   float best = 1e9;\n"
        "   for (int i = 0; i < 8; i++) {\n"
        "      z = -1.0 + 2.0 * fract(0.5 * z + 0.5);\n"
        "      float k = (1.0 + uB * 0.6) / max(dot(z, z), 1e-4);\n"
        "      z *= k;\n"
        "      scale *= k;\n"
        "      best = min(best, abs(z.y) / scale);\n"
        "   }\n"
        "   vec3 col = 0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + log(scale) * 0.8 + uC * 6.0 + t * 0.2);\n"
        "   return vec4(col * smoothstep(0.008, 0.0, best), 1.0);" },

      { "Newton fractal",
        "   vec2 z = p * (3.0 - uA * 2.0);\n"
        "   float it = 0.0;\n"
        "   for (int i = 0; i < 24; i++) {\n"
        "      vec2 z2 = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y);\n"
        "      vec2 z3 = vec2(z2.x * z.x - z2.y * z.y, z2.x * z.y + z2.y * z.x);\n"
        "      vec2 num = z3 - vec2(1.0, 0.0);\n"
        "      vec2 den = 3.0 * z2;\n"
        "      float dd = dot(den, den) + 1e-6;\n"
        // Complex division: num/den = num * conj(den) / |den|^2.
        "      z -= vec2(num.x * den.x + num.y * den.y, num.y * den.x - num.x * den.y) / dd;\n"
        "      if (dot(num, num) < 1e-5) break;\n"
        "      it += 1.0;\n"
        "   }\n"
        "   vec3 col = 0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + atan(z.y, z.x) * 1.5 + uC * 6.0 + t * 0.2);\n"
        "   return vec4(col * (1.0 - it / 24.0 * (0.2 + uB * 0.8)), 1.0);" },

      { "Burning ship",
        "   vec2 c = p * (3.0 - uA * 2.5) + vec2(-0.5, -0.5);\n"
        "   vec2 z = vec2(0.0);\n"
        "   float it = 0.0;\n"
        "   for (int i = 0; i < 80; i++) {\n"
        // The abs() before squaring is the whole difference from Mandelbrot.
        "      z = abs(z);\n"
        "      z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;\n"
        "      if (dot(z, z) > 16.0) break;\n"
        "      it += 1.0;\n"
        "   }\n"
        "   float f = it / 80.0;\n"
        "   vec3 col = 0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + f * (6.0 + uB * 12.0) + uC * 6.0);\n"
        "   return vec4(col * step(f, 0.999), 1.0);" },

      { "Flow field",
        "   vec2 q = p;\n"
        "   float k = 6.0 + uA * 10.0;\n"
        "   for (int i = 0; i < 24; i++) {\n"
        "      float a = sin(q.x * k + t * 0.3) + cos(q.y * k - t * 0.2);\n"
        "      q += vec2(cos(a), sin(a)) * 0.008 * (0.5 + uB);\n"
        "   }\n"
        "   float v = sin(length(q) * 20.0 + atan(q.y, q.x) * 3.0);\n"
        "   return vec4(0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + v * 2.0 + uC * 6.0), 1.0);" },

      { "Circle packing",
        "   vec2 q = p * floor(3.0 + uA * 12.0);\n"
        "   vec2 id = floor(q);\n"
        "   vec2 f = fract(q) - 0.5;\n"
        "   float r = (0.15 + 0.32 * rand(id)) * (0.6 + 0.4 * sin(t + rand(id + vec2(3.0)) * TAU));\n"
        "   float ring = abs(length(f) - r) - (0.01 + uB * 0.06);\n"
        "   vec3 col = 0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + rand(id) * 6.0 + uC * 6.0);\n"
        "   return vec4(mix(vec3(0.03), col, smoothstep(0.02, -0.02, ring)), 1.0);" },

      { "Gyroid slice",
        // A 2D cut through the 3D gyroid minimal surface; uD slides the cut
        // plane, so animating it walks through the solid.
        "   vec3 q = vec3(p * (6.0 + uA * 18.0), t * 0.4 + uD * 10.0);\n"
        "   float g = sin(q.x) * cos(q.y) + sin(q.y) * cos(q.z) + sin(q.z) * cos(q.x);\n"
        "   float mask = smoothstep(0.05 + uB * 0.5, 0.0, abs(g));\n"
        "   vec3 col = 0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + g * 1.5 + uC * 6.0);\n"
        "   return vec4(mix(vec3(0.02), col, mask), 1.0);" },

      { "Terrain contours",
        "   vec2 q = p * (3.0 + uA * 6.0) + vec2(t * 0.05, 0.0);\n"
        "   float h = 0.0, amp = 0.5;\n"
        "   for (int i = 0; i < 6; i++) {\n"
        "      vec2 g = floor(q), f = fract(q);\n"
        "      f = f * f * (3.0 - 2.0 * f);\n"
        "      float n = mix(mix(rand(g), rand(g + vec2(1.0, 0.0)), f.x),\n"
        "                    mix(rand(g + vec2(0.0, 1.0)), rand(g + vec2(1.0, 1.0)), f.x), f.y);\n"
        // Ridged rather than plain fbm: folding the noise at its midpoint turns
        // smooth hills into creases, which is what makes it read as terrain.
        "      h += (1.0 - abs(n * 2.0 - 1.0)) * amp;\n"
        "      amp *= 0.5;\n"
        "      q *= 2.0;\n"
        "   }\n"
        "   float lines = fract(h * (4.0 + uB * 24.0));\n"
        "   float contour = smoothstep(0.06, 0.0, min(lines, 1.0 - lines));\n"
        "   vec3 col = mix(vec3(0.03, 0.05, 0.08),\n"
        "                  0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + h * 3.0 + uC * 6.0), h);\n"
        "   return vec4(mix(col, vec3(1.0), contour * 0.8), 1.0);" },
   };

   std::vector<std::string> BuildPresetNames()
   {
      std::vector<std::string> names;
      for (const FormulaNode::Preset& preset : kPresets)
         names.push_back(preset.name);
      return names;
   }
}

const std::vector<FormulaNode::Preset>& FormulaNode::Presets()
{
   return kPresets;
}

const std::vector<std::string>& FormulaNode::PresetNames()
{
   static const std::vector<std::string> names = BuildPresetNames();
   return names;
}

void FormulaNode::LoadPreset(int index)
{
   if (index < 0 || index >= (int)kPresets.size())
      return;
   formula = kPresets[index].code;
   Apply();
}

Field::DeviceFile FormulaNode::ToDeviceFile() const
{
   Field::DeviceFile device;
   device.domain = "formula";
   device.code = formula;
   // No params - see the header comment and plan §7.
   return device;
}

void FormulaNode::LoadDeviceFile(const Field::DeviceFile& device)
{
   formula = device.code;
   Apply();
}

FormulaNode::FormulaNode()
{
   formula = kDefaultFormula;
   passCode[kImage] = kShadertoyDefault;
}

const std::vector<std::string>& FormulaNode::ModeNames()
{
   static const std::vector<std::string> names = { "Expression", "Shadertoy" };
   return names;
}

const std::vector<std::string>& FormulaNode::BufferNames()
{
   static const std::vector<std::string> names = { "no buffers", "buffer A", "buffers A + B", "buffers A - D" };
   return names;
}

const char* FormulaNode::PassName(int pass)
{
   static const char* kNames[kPasses] = { "Image", "Buffer A", "Buffer B", "Buffer C", "Buffer D", "Common" };
   return pass >= 0 && pass < kPasses ? kNames[pass] : "";
}

const char* FormulaNode::InputLabel(int slot) const
{
   static const char* kLabels[kChannels] = { "iChannel0", "iChannel1", "iChannel2", "iChannel3" };
   return IsShadertoy() && slot >= 0 && slot < kChannels ? kLabels[slot] : nullptr;
}

FormulaNode::~FormulaNode()
{
   if (mProgram != 0)
      glDeleteProgram(mProgram);
   for (unsigned int& program : mPassProgram)
      if (program != 0)
         glDeleteProgram(program);
   for (auto& pair : mBuf)
      for (GLUtil::Fbo& fbo : pair)
         GLUtil::DestroyFbo(fbo);
   GLUtil::DestroyFbo(mOut);
}

bool FormulaNode::Apply()
{
   if (IsShadertoy())
      return ApplyShadertoy();
   std::string src = std::string(kPreamble) + formula + kEpilogue;

   std::string error;
   unsigned int program = GLUtil::CompileProgram(src.c_str(), &error);
   if (program == 0)
   {
      mLastError = error;
      return false;
   }

   if (mProgram != 0)
      glDeleteProgram(mProgram);
   mProgram = program;
   mLastError.clear();
   return true;
}

void FormulaNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (IsShadertoy())
   {
      CookShadertoy(frameId);
      return;
   }
   if (mProgram == 0 && mLastError.empty())
      Apply();
   if (mProgram == 0)
      return;

   if (animate)
      mClock = (float)Transport::Instance().Seconds();

   const int w = GLUtil::TargetSide(width, 4);
   const int h = GLUtil::TargetSide(height, 4);
   if (!GLUtil::EnsureFbo(mOut, w, h))
      return;

   GLUtil::RunShaderPass(mOut, mProgram, [this]()
   {
      glUniform1f(glGetUniformLocation(mProgram, "uTime"), mClock);
      glUniform1f(glGetUniformLocation(mProgram, "uA"), knobA);
      glUniform1f(glGetUniformLocation(mProgram, "uB"), knobB);
      glUniform1f(glGetUniformLocation(mProgram, "uC"), knobC);
      glUniform1f(glGetUniformLocation(mProgram, "uD"), knobD);
      glUniform1f(glGetUniformLocation(mProgram, "uAspect"), (float)mOut.w / (float)mOut.h);
   });
}

// ---- Shadertoy mode ----

void FormulaNode::ParseKnobs()
{
   // uniform float name; // lo..hi = default   (the "= default" part is optional)
   static const std::regex knobLine(
      R"(uniform\s+float\s+([A-Za-z_][A-Za-z0-9_]*)\s*;\s*//\s*(-?[0-9]*\.?[0-9]+)\s*\.\.\s*(-?[0-9]*\.?[0-9]+)(?:\s*=\s*(-?[0-9]*\.?[0-9]+))?)");
   std::vector<Knob> knobs;
   const int order[] = { kCommon, kBufA, kBufB, kBufC, kBufD, kImage };
   for (int pass : order)
   {
      if (pass >= kBufA && pass <= kBufD && pass > BufferCount())
         continue;
      const std::string& code = passCode[pass];
      for (auto it = std::sregex_iterator(code.begin(), code.end(), knobLine); it != std::sregex_iterator(); ++it)
      {
         Knob k;
         k.name = (*it)[1].str();
         k.lo = (float)std::atof((*it)[2].str().c_str());
         k.hi = (float)std::atof((*it)[3].str().c_str());
         if (k.hi < k.lo)
            std::swap(k.lo, k.hi);
         k.def = (*it)[4].matched ? (float)std::atof((*it)[4].str().c_str()) : k.lo;
         k.def = std::clamp(k.def, k.lo, k.hi);
         bool seen = false;
         for (const Knob& other : knobs)
            seen = seen || other.name == k.name;
         if (!seen && (int)knobs.size() < kMaxKnobs)
            knobs.push_back(k);
      }
   }

   // A knob keeps its value while its slot keeps its name (a reload, a recompile);
   // a new or renamed one starts at its default. The names are saved with the patch,
   // so a load keeps the saved values.
   std::vector<std::string> oldNames;
   {
      std::stringstream in(mKnobNames);
      std::string name;
      while (std::getline(in, name, '|'))
         oldNames.push_back(name);
   }
   std::string joined;
   for (int i = 0; i < (int)knobs.size(); i++)
   {
      const bool same = i < (int)oldNames.size() && oldNames[(size_t)i] == knobs[(size_t)i].name;
      if (!same)
         stKnob[i] = knobs[(size_t)i].def;
      joined += (i > 0 ? "|" : "") + knobs[(size_t)i].name;
   }
   mKnobs = knobs;
   mKnobNames = joined;
}

std::string FormulaNode::MapError(const std::string& error, int pass, int preambleLines) const
{
   // Drivers write "0:12(3)", "0(12)" or "ERROR: 0:12:" - map the line back to the
   // pasted text, and name the tab when the line is in Common.
   // Common plus the newline that ends it; the pass's first line follows.
   const int commonLines = (int)std::count(passCode[kCommon].begin(), passCode[kCommon].end(), '\n') + 1;
   static const std::regex lineRef(R"(\b0[:(]([0-9]+))");
   std::string out;
   std::string::const_iterator from = error.begin();
   for (auto it = std::sregex_iterator(error.begin(), error.end(), lineRef); it != std::sregex_iterator(); ++it)
   {
      const std::smatch& m = *it;
      out.append(from, m[0].first);
      int line = std::atoi(m[1].str().c_str()) - preambleLines;
      const char* where = PassName(pass);
      if (!passCode[kCommon].empty() && line <= commonLines)
         where = PassName(kCommon);
      else
         line -= commonLines;
      out += std::string(where) + " line " + std::to_string(std::max(1, line));
      from = m[0].second;
   }
   out.append(from, error.end());
   return out;
}

bool FormulaNode::ApplyShadertoy()
{
   ParseKnobs();
   for (int pass = 0; pass <= kBufD; pass++)
   {
      const std::string& code = passCode[pass];
      if (code.find("mainSound") != std::string::npos || code.find("mainCubemap") != std::string::npos ||
          code.find("samplerCube") != std::string::npos || code.find("mainVR") != std::string::npos)
      {
         mLastError = std::string(PassName(pass)) +
                      ": sound, cubemap and VR passes are not supported. Paste the Image and Buffer A-D tabs.";
         return false;
      }
   }

   unsigned int built[kBufD + 1] = {};
   const int preambleLines = CountLines(kShadertoyPreamble);
   for (int pass = 0; pass <= kBufD; pass++)
   {
      if (pass != kImage && pass > BufferCount())
         continue;
      // A buffer tab left empty stays black instead of failing the whole shader.
      if (pass != kImage && passCode[pass].find_first_not_of(" \t\r\n") == std::string::npos)
         continue;
      const std::string src = std::string(kShadertoyPreamble) + passCode[kCommon] + "\n" + passCode[pass] +
                              (pass == kImage ? kShadertoyMainImage : kShadertoyMainBuffer);
      std::string error;
      built[pass] = GLUtil::CompileProgram(src.c_str(), &error);
      if (built[pass] == 0)
      {
         for (unsigned int program : built)
            if (program != 0)
               glDeleteProgram(program);
         mLastError = MapError(error, pass, preambleLines);
         return false;
      }
   }
   for (int pass = 0; pass <= kBufD; pass++)
   {
      if (mPassProgram[pass] != 0)
         glDeleteProgram(mPassProgram[pass]);
      mPassProgram[pass] = built[pass];
   }
   mLastError.clear();
   mFrame = 0;
   return true;
}

void FormulaNode::CookShadertoy(int frameId)
{
   if (mPassProgram[kImage] == 0 && mLastError.empty())
      ApplyShadertoy();
   if (mPassProgram[kImage] == 0)
      return;

   if (animate)
      mClock = (float)Transport::Instance().Seconds();
   const float delta = mFrame == 0 ? 0.0f : std::max(0.0f, mClock - mLastTime);
   mLastTime = mClock;

   const int w = GLUtil::TargetSide(width, 4);
   const int h = GLUtil::TargetSide(height, 4);
   if (!GLUtil::EnsureFbo(mOut, w, h))
      return;

   const int numBuffers = BufferCount();
   for (int b = numBuffers; b < 4; b++)   // buffers the shader no longer uses give their memory back
      for (int k = 0; k < 2; k++)
         if (mBuf[b][k].fbo != 0)
            GLUtil::DestroyFbo(mBuf[b][k]);
   for (int b = 0; b < numBuffers; b++)
      for (int k = 0; k < 2; k++)
      {
         const bool fresh = mBuf[b][k].w != w || mBuf[b][k].h != h || mBuf[b][k].fbo == 0;
         if (!GLUtil::EnsureFbo(mBuf[b][k], w, h, GL_RGBA16F))
            return;
         if (fresh)
         {
            // A new buffer starts black, as on the site, not with whatever the driver left.
            GLint prev = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
            glBindFramebuffer(GL_FRAMEBUFFER, mBuf[b][k].fbo);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            glBindFramebuffer(GL_FRAMEBUFFER, prev);
            mFrame = 0;
         }
      }

   // Wired pins first; an unwired channel reads the buffer with its number.
   unsigned int channelTex[kChannels] = {};
   float channelRes[kChannels * 3] = {};
   for (int c = 0; c < kChannels; c++)
   {
      channelTex[c] = mChannel[c].Pull(frameId);
      if (channelTex[c] != 0)
      {
         INode* src = mChannel[c].Resolved();
         channelRes[c * 3 + 0] = src != nullptr ? (float)src->GetOutputWidth() : 0.0f;
         channelRes[c * 3 + 1] = src != nullptr ? (float)src->GetOutputHeight() : 0.0f;
         channelRes[c * 3 + 2] = 1.0f;
      }
   }

   std::time_t now = std::time(nullptr);
   std::tm local {};
   if (const std::tm* t = std::localtime(&now))   // GL thread only; nothing else here calls it
      local = *t;
   const float date[4] = { (float)(local.tm_year + 1900), (float)local.tm_mon, (float)local.tm_mday,
                           (float)(local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec) };

   auto runPass = [&](unsigned int program, const GLUtil::Fbo& target)
   {
      GLUtil::RunShaderPass(target, program, [&]()
      {
         glUniform3f(glGetUniformLocation(program, "iResolution"), (float)w, (float)h, 1.0f);
         glUniform1f(glGetUniformLocation(program, "iTime"), mClock);
         glUniform1f(glGetUniformLocation(program, "iTimeDelta"), delta);
         glUniform1f(glGetUniformLocation(program, "iFrameRate"), delta > 0.0f ? 1.0f / delta : 60.0f);
         glUniform1i(glGetUniformLocation(program, "iFrame"), mFrame);
         glUniform4f(glGetUniformLocation(program, "iMouse"), mouse[0], mouse[1], mouse[2], mouse[3]);
         glUniform4f(glGetUniformLocation(program, "iDate"), date[0], date[1], date[2], date[3]);
         glUniform1f(glGetUniformLocation(program, "iSampleRate"), 48000.0f);
         static const char* kSamplers[kChannels] = { "iChannel0", "iChannel1", "iChannel2", "iChannel3" };
         float res[kChannels * 3];
         for (int c = 0; c < kChannels; c++)
         {
            unsigned int tex = channelTex[c];
            res[c * 3 + 0] = channelRes[c * 3 + 0];
            res[c * 3 + 1] = channelRes[c * 3 + 1];
            res[c * 3 + 2] = channelRes[c * 3 + 2];
            if (tex == 0 && c < numBuffers)
            {
               // The buffer's newest frame: this frame's if it already ran, else the last one.
               tex = GLUtil::FboTexture(mBuf[c][mBufCur[c]]);
               res[c * 3 + 0] = (float)w;
               res[c * 3 + 1] = (float)h;
               res[c * 3 + 2] = 1.0f;
            }
            glActiveTexture(GL_TEXTURE0 + c);
            glBindTexture(GL_TEXTURE_2D, tex);
            glUniform1i(glGetUniformLocation(program, kSamplers[c]), c);
         }
         glUniform3fv(glGetUniformLocation(program, "iChannelResolution"), kChannels, res);
         for (int i = 0; i < (int)mKnobs.size(); i++)
            glUniform1f(glGetUniformLocation(program, mKnobs[(size_t)i].name.c_str()), stKnob[i]);
      });
      glActiveTexture(GL_TEXTURE0);
   };

   for (int b = 0; b < numBuffers; b++)
   {
      if (mPassProgram[kBufA + b] == 0)
         continue;
      const int next = 1 - mBufCur[b];
      runPass(mPassProgram[kBufA + b], mBuf[b][next]);
      mBufCur[b] = next;
   }
   runPass(mPassProgram[kImage], mOut);
   mFrame++;
}
