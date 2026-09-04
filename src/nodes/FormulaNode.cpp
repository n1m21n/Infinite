#include "FormulaNode.h"

#include "gl3.h"
#include <algorithm>

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
}

FormulaNode::~FormulaNode()
{
   if (mProgram != 0)
      glDeleteProgram(mProgram);
   GLUtil::DestroyFbo(mOut);
}

bool FormulaNode::Apply()
{
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

   if (mProgram == 0 && mLastError.empty())
      Apply();
   if (mProgram == 0)
      return;

   if (animate)
      mClock = (float)Transport::Instance().Seconds();

   const int w = std::max(4, (int)width);
   const int h = std::max(4, (int)height);
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
