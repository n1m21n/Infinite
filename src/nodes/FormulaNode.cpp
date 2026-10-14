#include "FormulaNode.h"

#include "platform/OpenGLHeaders.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <regex>
#include <set>
#include <sstream>
#include <utility>

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
      // Turbo (TOP parity): patched image inputs, sampled with texture(uIn0, uv).
      "uniform sampler2D uIn0;\n"
      "uniform sampler2D uIn1;\n"
      "uniform sampler2D uIn2;\n"
      "uniform sampler2D uIn3;\n"
      "uniform int uHasIn0;\n"
      "uniform int uHasIn1;\n"
      "uniform int uHasIn2;\n"
      "uniform int uHasIn3;\n"
      "uniform vec2 uInRes0;\n"
      "uniform vec2 uInRes1;\n"
      "uniform vec2 uInRes2;\n"
      "uniform vec2 uInRes3;\n"
      "\n"
      "#define PI 3.14159265359\n"
      "#define TAU 6.28318530718\n"
      "float rand(vec2 co) { return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453); }\n"
      "mat2 rot(float a) { float s = sin(a), c = cos(a); return mat2(c, -s, s, c); }\n"
      "float sdCircle(vec2 p, float r) { return length(p) - r; }\n"
      "float sdBox(vec2 p, vec2 b) { vec2 d = abs(p) - b; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0); }\n"
      "\n"
      "// uv is 0..1, p is centered and aspect-corrected, t is seconds, knobs uA..uD are 0..1.\n"
      "// Patched images: texture(uIn0, uv) .. uIn3; uHasIn0 == 1 when in0 is connected.\n"
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
   // Turbo 0.56: Shadertoy mode. The user's file goes between these two, after
   // a #line 1 so compile errors point at the user's own line numbers.
   const char* kShadertoyPreamble =
      "in vec2 vUv;\n"
      "out vec4 infiniteFragOut;\n"
      "uniform vec3 iResolution;\n"
      "uniform float iTime;\n"
      "uniform float iTimeDelta;\n"
      "uniform float iFrameRate;\n"
      "uniform int iFrame;\n"
      "uniform vec4 iMouse;\n"
      "uniform vec4 iDate;\n"
      "uniform float iSampleRate;\n"
      "uniform vec3 iChannelResolution[4];\n"
      "uniform float iChannelTime[4];\n"
      "uniform sampler2D iChannel0;\n"
      "uniform sampler2D iChannel1;\n"
      "uniform sampler2D iChannel2;\n"
      "uniform sampler2D iChannel3;\n"
      "uniform float uTime;\n"
      "uniform float uA;\n"
      "uniform float uB;\n"
      "uniform float uC;\n"
      "uniform float uD;\n"
      "uniform float uAspect;\n"
      "uniform int uKeepAlpha;\n"
      "#define texture2D texture\n";

   const char* kShadertoyEpilogue =
      "\nvoid main() {\n"
      "   vec4 c = vec4(0.0, 0.0, 0.0, 1.0);\n"
      "   mainImage(c, gl_FragCoord.xy);\n"
      "   infiniteFragOut = uKeepAlpha != 0 ? c : vec4(c.rgb, 1.0);\n"
      "}\n";

   // 330 where the context has it (floatBitsToUint and friends, common in
   // Shadertoy code); the vertex stage stays 150, which links with it.
   const char* ShadertoyVersionLine()
   {
      static int version = 0;
      if (version == 0)
      {
         GLint major = 0, minor = 0;
         glGetIntegerv(GL_MAJOR_VERSION, &major);
         glGetIntegerv(GL_MINOR_VERSION, &minor);
         version = (major * 10 + minor >= 33) ? 330 : 150;
      }
      return version == 330 ? "#version 330\n" : "#version 150\n";
   }

   // Names the node already declares; a uniform line with one of these is
   // dropped from the code (no redeclaration) and gets no knob.
   bool IsReservedUniform(const std::string& name)
   {
      static const std::set<std::string> kReserved = {
         "uTime", "uA", "uB", "uC", "uD", "uAspect", "uKeepAlpha",
         "uIn0", "uIn1", "uIn2", "uIn3", "uHasIn0", "uHasIn1", "uHasIn2", "uHasIn3",
         "uInRes0", "uInRes1", "uInRes2", "uInRes3",
         "iResolution", "iTime", "iTimeDelta", "iFrameRate", "iFrame", "iMouse", "iDate",
         "iSampleRate", "iChannelResolution", "iChannelTime",
         "iChannel0", "iChannel1", "iChannel2", "iChannel3",
      };
      return kReserved.count(name) != 0;
   }

   const std::regex& UniformLineRegex()
   {
      static const std::regex re(R"(^[ \t]*uniform[ \t]+float[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*;(.*)$)");
      return re;
   }

   // "// lo..hi = default" after the declaration; all parts optional.
   void ParseUniformComment(const std::string& rest, float& lo, float& hi, float& def)
   {
      static const std::regex re(
         R"(//\s*(-?[0-9]*\.?[0-9]+)\s*\.\.\s*(-?[0-9]*\.?[0-9]+)(?:\s*=\s*(-?[0-9]*\.?[0-9]+))?)");
      lo = 0.0f;
      hi = 1.0f;
      bool hasDef = false;
      std::smatch m;
      if (std::regex_search(rest, m, re))
      {
         lo = std::stof(m[1].str());
         hi = std::stof(m[2].str());
         if (m[3].matched)
         {
            def = std::stof(m[3].str());
            hasDef = true;
         }
      }
      if (hi < lo)
         std::swap(lo, hi);
      if (!hasDef)
         def = 0.5f * (lo + hi);
      def = std::clamp(def, lo, hi);
   }

   // The code with every uniform-float line blanked (same line count), and the
   // declarations to put in front of it.
   void SplitUniforms(const std::string& code, std::string& body, std::string& decls)
   {
      body.clear();
      decls.clear();
      size_t start = 0;
      while (start <= code.size())
      {
         size_t end = code.find('\n', start);
         if (end == std::string::npos)
            end = code.size();
         std::string line = code.substr(start, end - start);
         if (!line.empty() && line.back() == '\r')
            line.pop_back();
         std::smatch m;
         if (std::regex_match(line, m, UniformLineRegex()))
         {
            if (!IsReservedUniform(m[1].str()))
               decls += "uniform float " + m[1].str() + ";\n";
         }
         else
            body += line;
         if (end < code.size())
            body += '\n';
         start = end + 1;
      }
   }

   bool HasMainImage(const std::string& code)
   {
      // The 0.55 adapter (native mode closing shape() by hand) also has a
      // mainImage; it defines `shape` itself, so it stays in native mode.
      static const std::regex re(R"(\bvoid\s+mainImage\s*\()");
      return std::regex_search(code, re) && code.find("#define shape") == std::string::npos;
   }

   void ClearFbo(const GLUtil::Fbo& fbo)
   {
      GLint previous = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo.fbo);
      glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous);
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

      // Turbo 0.56: Shadertoy-mode examples (MIT, written for Infinite-Turbo).
      // Whole files with mainImage(); their uniform lines become knobs.
      { "Shadertoy: fractal tunnel",
        "// Fractal tunnel. Infinite-Turbo example (MIT), written for the Formula's Shadertoy mode.\n"
        "// The uniforms below become knobs on the node: name, range and default come from the comment.\n"
        "uniform float uSpeed; // 0..4 = 1\n"
        "uniform float uFold;  // 0..1 = 0.5\n"
        "uniform float uHue;   // 0..1 = 0.6\n"
        "uniform float uGlow;  // 0..2 = 1\n"
        "\n"
        "mat2 rot2(float a) { float c = cos(a), s = sin(a); return mat2(c, -s, s, c); }\n"
        "\n"
        "// kaleidoscopic IFS, repeated in space; trap keeps the closest orbit for colour\n"
        "float fractalDE(vec3 p, out float trap)\n"
        "{\n"
        "    p = mod(p + 2.0, 4.0) - 2.0;\n"
        "    float s = 1.0;\n"
        "    trap = 10.0;\n"
        "    for (int i = 0; i < 7; i++)\n"
        "    {\n"
        "        p = abs(p);\n"
        "        if (p.x < p.y) p.xy = p.yx;\n"
        "        if (p.x < p.z) p.xz = p.zx;\n"
        "        if (p.y < p.z) p.yz = p.zy;\n"
        "        p.xy = rot2(0.15 + uFold * 0.5) * p.xy;\n"
        "        p = p * 2.0 - vec3(1.4, 1.0, 0.6 + uFold * 0.6);\n"
        "        s *= 2.0;\n"
        "        trap = min(trap, dot(p, p));\n"
        "    }\n"
        "    return (length(p) - 1.2) / s;\n"
        "}\n"
        "\n"
        "float map(vec3 p, out float trap)\n"
        "{\n"
        "    float f = fractalDE(p, trap);\n"
        "    float tunnel = 1.0 - length(p.xy); // carve a free path for the camera\n"
        "    return max(f, tunnel);\n"
        "}\n"
        "\n"
        "void mainImage(out vec4 fragColor, in vec2 fragCoord)\n"
        "{\n"
        "    vec2 uv = (2.0 * fragCoord - iResolution.xy) / iResolution.y;\n"
        "    float fly = iTime * uSpeed;\n"
        "    vec3 ro = vec3(0.0, 0.0, fly);\n"
        "    vec3 rd = normalize(vec3(uv, 1.3));\n"
        "    rd.xy = rot2(0.25 * sin(iTime * 0.2)) * rd.xy;\n"
        "\n"
        "    float d = 0.0, trap = 0.0, glow = 0.0;\n"
        "    int steps = 0;\n"
        "    for (int i = 0; i < 110; i++)\n"
        "    {\n"
        "        float h = map(ro + rd * d, trap);\n"
        "        glow += 0.01 / (0.02 + abs(h));\n"
        "        steps = i;\n"
        "        if (h < 0.0008 * d || d > 25.0) break;\n"
        "        d += h * 0.8;\n"
        "    }\n"
        "\n"
        "    vec3 p = ro + rd * d;\n"
        "    vec2 e = vec2(0.0015, -0.0015);\n"
        "    float tk;\n"
        "    vec3 n = normalize(e.xyy * map(p + e.xyy, tk) + e.yyx * map(p + e.yyx, tk) +\n"
        "                       e.yxy * map(p + e.yxy, tk) + e.xxx * map(p + e.xxx, tk));\n"
        "    vec3 lightDir = normalize(vec3(0.4, 0.6, -0.5));\n"
        "    float dif = 0.35 + 0.65 * max(dot(n, lightDir), 0.0);\n"
        "    float rim = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);\n"
        "    vec3 base = 0.5 + 0.5 * cos(6.28318 * (uHue + 0.25 * log(1.0 + trap) + vec3(0.0, 0.33, 0.67)));\n"
        "    float ao = 1.0 - float(steps) / 110.0;\n"
        "    float fog = exp(-0.11 * d);\n"
        "    vec3 col = (base * dif * ao + rim * 0.3 * base) * fog;\n"
        "    col += uGlow * 0.0008 * glow * vec3(0.4, 0.7, 1.0) * (1.0 - fog);\n"
        "    col = pow(clamp(col, 0.0, 1.0), vec3(0.4545));\n"
        "    fragColor = vec4(col, 1.0);\n"
        "}\n" },

      { "Shadertoy: terrain and clouds",
        "// Terrain and clouds. Infinite-Turbo example (MIT), written for the Formula's Shadertoy mode.\n"
        "// The uniforms below become knobs on the node: name, range and default come from the comment.\n"
        "uniform float uFly;   // 0..4 = 1\n"
        "uniform float uSun;   // 0..1 = 0.35\n"
        "uniform float uCloud; // 0..1 = 0.5\n"
        "uniform float uHaze;  // 0..1 = 0.4\n"
        "\n"
        "float hash12(vec2 p)\n"
        "{\n"
        "    vec3 q = fract(vec3(p.xyx) * 0.1031);\n"
        "    q += dot(q, q.yzx + 33.33);\n"
        "    return fract((q.x + q.y) * q.z);\n"
        "}\n"
        "\n"
        "float vnoise(vec2 p)\n"
        "{\n"
        "    vec2 i = floor(p), f = fract(p);\n"
        "    vec2 u = f * f * (3.0 - 2.0 * f);\n"
        "    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x),\n"
        "               mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);\n"
        "}\n"
        "\n"
        "const mat2 kRot = mat2(1.6, 1.2, -1.2, 1.6);\n"
        "\n"
        "float fbm(vec2 p, int octaves)\n"
        "{\n"
        "    float a = 0.0, b = 0.5;\n"
        "    for (int i = 0; i < 9; i++)\n"
        "    {\n"
        "        if (i >= octaves) break;\n"
        "        a += b * vnoise(p);\n"
        "        p = kRot * p;\n"
        "        b *= 0.5;\n"
        "    }\n"
        "    return a;\n"
        "}\n"
        "\n"
        "const float kWater = 1.2;\n"
        "\n"
        "float terrain(vec2 p, int octaves)\n"
        "{\n"
        "    float h = fbm(p * 0.018, octaves);\n"
        "    return max(h * h * 34.0, kWater);\n"
        "}\n"
        "\n"
        "vec3 sunDir() { return normalize(vec3(0.6, 0.08 + uSun * 0.9, -0.5)); }\n"
        "\n"
        "vec3 sky(vec3 rd)\n"
        "{\n"
        "    vec3 sd = sunDir();\n"
        "    vec3 col = mix(vec3(0.75, 0.82, 0.92), vec3(0.25, 0.45, 0.8), clamp(rd.y * 2.0, 0.0, 1.0));\n"
        "    float sun = max(dot(rd, sd), 0.0);\n"
        "    col += vec3(1.0, 0.75, 0.45) * (pow(sun, 400.0) * 2.0 + pow(sun, 8.0) * 0.25);\n"
        "    if (rd.y > 0.0)\n"
        "    {\n"
        "        vec2 cp = rd.xz / rd.y * 30.0 + vec2(iTime * 0.6, 0.0);\n"
        "        float c = fbm(cp * 0.03, 6);\n"
        "        float cover = smoothstep(0.75 - uCloud * 0.5, 0.95, c);\n"
        "        col = mix(col, vec3(1.0) * (0.85 + 0.15 * sun), cover * smoothstep(0.0, 0.15, rd.y));\n"
        "    }\n"
        "    return col;\n"
        "}\n"
        "\n"
        "float softShadow(vec3 ro, vec3 rd)\n"
        "{\n"
        "    float res = 1.0, t = 0.5;\n"
        "    for (int i = 0; i < 28; i++)\n"
        "    {\n"
        "        vec3 p = ro + rd * t;\n"
        "        float h = p.y - terrain(p.xz, 5);\n"
        "        res = min(res, 8.0 * h / t);\n"
        "        if (res < 0.01) break;\n"
        "        t += clamp(h, 0.4, 4.0);\n"
        "    }\n"
        "    return clamp(res, 0.0, 1.0);\n"
        "}\n"
        "\n"
        "void mainImage(out vec4 fragColor, in vec2 fragCoord)\n"
        "{\n"
        "    vec2 uv = (2.0 * fragCoord - iResolution.xy) / iResolution.y;\n"
        "    float z = iTime * uFly * 3.0;\n"
        "    vec3 ro = vec3(0.0, 0.0, z);\n"
        "    ro.y = max(terrain(ro.xz, 4), 8.0) + 4.0;\n"
        "    vec3 ta = vec3(0.0, ro.y - 2.5, z + 12.0);\n"
        "    vec3 fw = normalize(ta - ro);\n"
        "    vec3 rt = normalize(cross(fw, vec3(0, 1, 0)));\n"
        "    vec3 up = cross(rt, fw);\n"
        "    vec3 rd = normalize(uv.x * rt + uv.y * up + 1.6 * fw);\n"
        "\n"
        "    float t = 0.5;\n"
        "    bool hit = false;\n"
        "    for (int i = 0; i < 220; i++)\n"
        "    {\n"
        "        vec3 p = ro + rd * t;\n"
        "        float h = p.y - terrain(p.xz, 6);\n"
        "        if (h < 0.002 * t) { hit = true; break; }\n"
        "        t += h * 0.45;\n"
        "        if (t > 220.0) break;\n"
        "    }\n"
        "\n"
        "    vec3 col = sky(rd);\n"
        "    if (hit)\n"
        "    {\n"
        "        vec3 p = ro + rd * t;\n"
        "        vec2 e = vec2(0.05, 0.0);\n"
        "        vec3 n = normalize(vec3(terrain(p.xz - e.xy, 8) - terrain(p.xz + e.xy, 8), 2.0 * e.x,\n"
        "                                terrain(p.xz - e.yx, 8) - terrain(p.xz + e.yx, 8)));\n"
        "        vec3 sd = sunDir();\n"
        "        float dif = max(dot(n, sd), 0.0) * softShadow(p + n * 0.05, sd);\n"
        "        vec3 mat;\n"
        "        if (p.y <= kWater + 0.001)\n"
        "        {\n"
        "            mat = vec3(0.05, 0.15, 0.2);\n"
        "            vec3 r = reflect(rd, vec3(0, 1, 0));\n"
        "            mat += sky(r) * 0.35;\n"
        "        }\n"
        "        else\n"
        "        {\n"
        "            float slope = n.y;\n"
        "            mat = mix(vec3(0.35, 0.3, 0.25), vec3(0.2, 0.32, 0.12), smoothstep(0.75, 0.9, slope));\n"
        "            mat = mix(mat, vec3(0.95), smoothstep(17.0, 21.0, p.y + slope * 3.0));\n"
        "        }\n"
        "        vec3 light = vec3(1.0, 0.92, 0.8) * dif * 1.6 + vec3(0.35, 0.45, 0.6) * (0.5 + 0.5 * n.y) * 0.5;\n"
        "        col = mat * light;\n"
        "        float fog = 1.0 - exp(-t * (0.004 + uHaze * 0.02));\n"
        "        col = mix(col, sky(normalize(vec3(rd.x, 0.02, rd.z))), fog);\n"
        "    }\n"
        "    col = pow(clamp(col, 0.0, 1.0), vec3(0.4545));\n"
        "    col *= 0.6 + 0.4 * pow(16.0 * (fragCoord.x / iResolution.x) * (fragCoord.y / iResolution.y)\n"
        "                          * (1.0 - fragCoord.x / iResolution.x) * (1.0 - fragCoord.y / iResolution.y), 0.1);\n"
        "    fragColor = vec4(col, 1.0);\n"
        "}\n" },
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

FormulaNode::FormulaNode()
{
   formula = kDefaultFormula;
}

const char* FormulaNode::InputLabel(int slot) const
{
   static const char* kLabels[kInputs] = { "in0", "in1", "in2", "in3" };
   return (slot >= 0 && slot < kInputs) ? kLabels[slot] : nullptr;
}

FormulaNode::~FormulaNode()
{
   if (mProgram != 0)
      glDeleteProgram(mProgram);
   GLUtil::DestroyFbo(mOut);
   GLUtil::DestroyFbo(mPrev);
}

void FormulaNode::VisitParams(ParamVisitor& v)
{
   v.Text("formula", formula);
   v.Float("width", width); v.Float("height", height);
   v.Float("knobA", knobA); v.Float("knobB", knobB);
   v.Float("knobC", knobC); v.Float("knobD", knobD);
   v.Bool("animate", animate);
   v.Bool("matchInput", matchInput);
   // Turbo 0.56
   v.Bool("floatBuffer", floatBuffer);
   v.Int("selfChannel", selfChannel);
   v.Float("mouseX", mouseX);
   v.Float("mouseY", mouseY);
   v.Bool("mouseDown", mouseDown);
   ParseUserParams(); // the formula may have just been loaded
   for (UserParam& up : userParams)
      v.Float(("uniform_" + up.name).c_str(), up.value);
}

void FormulaNode::ParseUserParams()
{
   if (formula == mParsedFormula)
      return;
   mParsedFormula = formula;
   std::vector<UserParam> next;
   std::istringstream in(formula);
   std::string line;
   while (std::getline(in, line) && (int)next.size() < kMaxUserParams)
   {
      if (!line.empty() && line.back() == '\r')
         line.pop_back();
      std::smatch m;
      if (!std::regex_match(line, m, UniformLineRegex()) || IsReservedUniform(m[1].str()))
         continue;
      UserParam up;
      up.name = m[1].str();
      bool dup = false;
      for (const UserParam& e : next)
         dup = dup || e.name == up.name;
      if (dup)
         continue;
      ParseUniformComment(m[2].str(), up.lo, up.hi, up.def);
      up.value = up.def;
      for (const UserParam& old : userParams)
         if (old.name == up.name)
            up.value = std::clamp(old.value, up.lo, up.hi);
      next.push_back(up);
   }
   userParams = std::move(next);
}

bool FormulaNode::Apply()
{
   ParseUserParams();
   std::string body, decls;
   SplitUniforms(formula, body, decls);
   const bool shadertoy = HasMainImage(body);
   std::string src;
   if (shadertoy)
   {
      src = std::string(ShadertoyVersionLine()) + kShadertoyPreamble + decls + "#line 1\n" + body +
            kShadertoyEpilogue;
   }
   else
   {
      std::string pre = kPreamble;
      const size_t at = pre.find("vec4 shape(");
      pre.insert(at == std::string::npos ? pre.size() : at, decls);
      src = pre + body + kEpilogue;
   }

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
   mShadertoy = shadertoy;
   mFrame = 0;
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
   const float dt = std::clamp(mClock - mLastClock, 0.0f, 0.25f);
   mLastClock = mClock;

   unsigned int inTex[kInputs] = {};
   unsigned int anyTex = 0;
   for (int i = 0; i < kInputs; i++)
   {
      inTex[i] = mInputs[i].Pull(frameId);
      if (anyTex == 0)
         anyTex = inTex[i];
   }

   int w = std::max(4, (int)width);
   int h = std::max(4, (int)height);
   if (matchInput && inTex[0] != 0)
   {
      w = std::max(1, mInputs[0].Width());
      h = std::max(1, mInputs[0].Height());
   }

   // Turbo 0.56: float buffers and the node's own last frame (ping-pong).
   const unsigned int format = floatBuffer ? GL_RGBA32F : GL_RGBA8;
   const bool self = selfChannel >= 0 && selfChannel < kInputs;
   if (self)
      std::swap(mOut, mPrev);
   const bool outFresh = mOut.fbo == 0 || mOut.w != w || mOut.h != h || mOut.internalFormat != format;
   if (!GLUtil::EnsureFbo(mOut, w, h, format))
      return;
   if (outFresh)
      mFrame = 0;
   unsigned int selfTex = 0;
   if (self)
   {
      const bool prevFresh = mPrev.fbo == 0 || mPrev.w != w || mPrev.h != h || mPrev.internalFormat != format;
      if (!GLUtil::EnsureFbo(mPrev, w, h, format))
         return;
      if (prevFresh)
      {
         ClearFbo(mPrev);
         mFrame = 0;
      }
      selfTex = mPrev.tex;
   }
   const unsigned int fallbackTex = anyTex != 0 ? anyTex : selfTex;

   GLUtil::RunShaderPass(mOut, mProgram, [this, &inTex, fallbackTex, self, selfTex, dt]()
   {
      static const char* kSampler[kInputs] = { "uIn0", "uIn1", "uIn2", "uIn3" };
      static const char* kChannel[kInputs] = { "iChannel0", "iChannel1", "iChannel2", "iChannel3" };
      static const char* kHas[kInputs] = { "uHasIn0", "uHasIn1", "uHasIn2", "uHasIn3" };
      static const char* kRes[kInputs] = { "uInRes0", "uInRes1", "uInRes2", "uInRes3" };
      float channelRes[kInputs * 3] = {};
      float channelTime[kInputs] = {};
      for (int i = 0; i < kInputs; i++)
      {
         const bool isSelf = self && i == selfChannel;
         const unsigned int tex = isSelf ? selfTex : (inTex[i] != 0 ? inTex[i] : fallbackTex);
         const float cw = isSelf ? (float)mOut.w : (float)mInputs[i].Width();
         const float ch = isSelf ? (float)mOut.h : (float)mInputs[i].Height();
         // An unpatched input aliases a patched one (or nothing at all, when
         // none is): sampling an unbound unit is undefined.
         glActiveTexture(GL_TEXTURE0 + i);
         glBindTexture(GL_TEXTURE_2D, tex);
         glUniform1i(glGetUniformLocation(mProgram, kSampler[i]), i);
         glUniform1i(glGetUniformLocation(mProgram, kChannel[i]), i);
         glUniform1i(glGetUniformLocation(mProgram, kHas[i]), (isSelf || inTex[i] != 0) ? 1 : 0);
         glUniform2f(glGetUniformLocation(mProgram, kRes[i]), cw, ch);
         channelRes[i * 3 + 0] = cw;
         channelRes[i * 3 + 1] = ch;
         channelRes[i * 3 + 2] = 1.0f;
         channelTime[i] = mClock;
      }
      glActiveTexture(GL_TEXTURE0);
      glUniform1f(glGetUniformLocation(mProgram, "uTime"), mClock);
      glUniform1f(glGetUniformLocation(mProgram, "uA"), knobA);
      glUniform1f(glGetUniformLocation(mProgram, "uB"), knobB);
      glUniform1f(glGetUniformLocation(mProgram, "uC"), knobC);
      glUniform1f(glGetUniformLocation(mProgram, "uD"), knobD);
      glUniform1f(glGetUniformLocation(mProgram, "uAspect"), (float)mOut.w / (float)mOut.h);
      for (const UserParam& up : userParams)
         glUniform1f(glGetUniformLocation(mProgram, up.name.c_str()), up.value);
      if (mShadertoy)
      {
         const float fw = (float)mOut.w, fh = (float)mOut.h;
         glUniform3f(glGetUniformLocation(mProgram, "iResolution"), fw, fh, 1.0f);
         glUniform1f(glGetUniformLocation(mProgram, "iTime"), mClock);
         glUniform1f(glGetUniformLocation(mProgram, "iTimeDelta"), dt);
         glUniform1f(glGetUniformLocation(mProgram, "iFrameRate"), dt > 0.0f ? 1.0f / dt : 0.0f);
         glUniform1i(glGetUniformLocation(mProgram, "iFrame"), mFrame);
         const float mx = std::clamp(mouseX, 0.0f, 1.0f) * fw, my = std::clamp(mouseY, 0.0f, 1.0f) * fh;
         glUniform4f(glGetUniformLocation(mProgram, "iMouse"), mx, my, mouseDown ? mx : -mx, mouseDown ? my : -my);
         const auto now = std::chrono::system_clock::now();
         const std::time_t tt = std::chrono::system_clock::to_time_t(now);
         std::tm lt = {};
#ifdef _WIN32
         localtime_s(&lt, &tt);
#else
         localtime_r(&tt, &lt);
#endif
         const double ms = (double)(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
         glUniform4f(glGetUniformLocation(mProgram, "iDate"), (float)(lt.tm_year + 1900), (float)lt.tm_mon,
                     (float)lt.tm_mday, (float)(lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec + ms / 1000.0));
         glUniform1f(glGetUniformLocation(mProgram, "iSampleRate"), 44100.0f);
         glUniform3fv(glGetUniformLocation(mProgram, "iChannelResolution"), kInputs, channelRes);
         glUniform1fv(glGetUniformLocation(mProgram, "iChannelTime"), kInputs, channelTime);
         glUniform1i(glGetUniformLocation(mProgram, "uKeepAlpha"), floatBuffer ? 1 : 0);
      }
   });
   mFrame++;
}
