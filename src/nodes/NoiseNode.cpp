#include "NoiseNode.h"

#include <OpenGL/gl3.h>
#include <algorithm>

#include "Transport.h"

namespace
{
   const std::vector<std::string> kTypeNames = {
      "Value", "fBm", "Ridged", "Voronoi", "Worley Edges", "White"
   };

   const char* kFragSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform int uType;\n"
      "uniform float uScale;\n"
      "uniform float uOctaves;\n"
      "uniform float uLacunarity;\n"
      "uniform float uGain;\n"
      "uniform float uWarp;\n"
      "uniform float uTime;\n"
      "uniform float uContrast;\n"
      "uniform float uBrightness;\n"
      "uniform float uSeed;\n"
      "uniform int uColorNoise;\n"
      "uniform vec3 uLowColor;\n"
      "uniform vec3 uHighColor;\n"
      "uniform float uAspect;\n"
      "\n"
      "float hash(vec2 p) {\n"
      "   p += uSeed;\n"
      "   return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);\n"
      "}\n"
      "vec2 hash2(vec2 p) {\n"
      "   p += uSeed;\n"
      "   return fract(sin(vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)))) * 43758.5453);\n"
      "}\n"
      "float valueNoise(vec2 p) {\n"
      "   vec2 i = floor(p), f = fract(p);\n"
      "   vec2 u = f * f * (3.0 - 2.0 * f);\n"
      "   float a = hash(i), b = hash(i + vec2(1,0));\n"
      "   float c = hash(i + vec2(0,1)), d = hash(i + vec2(1,1));\n"
      "   return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);\n"
      "}\n"
      "float fbm(vec2 p, bool ridged) {\n"
      "   float sum = 0.0, amp = 0.5, norm = 0.0;\n"
      "   int oct = int(clamp(uOctaves, 1.0, 8.0));\n"
      "   for (int i = 0; i < 8; i++) {\n"
      "      if (i >= oct) break;\n"
      "      float n = valueNoise(p);\n"
      "      if (ridged) n = 1.0 - abs(n * 2.0 - 1.0);\n"
      "      sum += n * amp; norm += amp;\n"
      "      p *= uLacunarity; amp *= uGain;\n"
      "   }\n"
      "   return sum / max(norm, 1e-4);\n"
      "}\n"
      "vec2 voronoi(vec2 p) {\n"
      "   vec2 i = floor(p), f = fract(p);\n"
      "   float best = 8.0, second = 8.0;\n"
      "   for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) {\n"
      "      vec2 g = vec2(float(x), float(y));\n"
      "      vec2 o = hash2(i + g);\n"
      "      o = 0.5 + 0.5 * sin(uTime + 6.2831 * o);\n"
      "      float d = length(g + o - f);\n"
      "      if (d < best) { second = best; best = d; }\n"
      "      else if (d < second) second = d;\n"
      "   }\n"
      "   return vec2(best, second);\n"
      "}\n"
      "void main() {\n"
      "   vec2 uv = vUv;\n"
      "   uv.x *= uAspect;\n"
      "   vec2 p = uv * uScale + vec2(uTime * 0.3, 0.0);\n"
      "   if (uWarp > 0.0) {\n"
      "      vec2 q = vec2(fbm(p, false), fbm(p + 5.2, false));\n"
      "      p += (q - 0.5) * uWarp * 4.0;\n"
      "   }\n"
      "   float n;\n"
      "   if (uType == 0) n = valueNoise(p);\n"
      "   else if (uType == 1) n = fbm(p, false);\n"
      "   else if (uType == 2) n = fbm(p, true);\n"
      "   else if (uType == 3) n = voronoi(p).x;\n"
      "   else if (uType == 4) { vec2 v = voronoi(p); n = clamp(v.y - v.x, 0.0, 1.0); }\n"
      "   else n = hash(floor(p * 64.0) + floor(uTime * 30.0));\n"
      "\n"
      "   n = clamp((n - 0.5) * uContrast + 0.5 + uBrightness, 0.0, 1.0);\n"
      "   vec3 col;\n"
      "   if (uColorNoise == 1) {\n"
      "      col = vec3(n,\n"
      "                 clamp(((uType == 5 ? hash(floor(p*64.0)+11.0) : fbm(p + 11.0, false)) - 0.5) * uContrast + 0.5 + uBrightness, 0.0, 1.0),\n"
      "                 clamp(((uType == 5 ? hash(floor(p*64.0)+23.0) : fbm(p + 23.0, false)) - 0.5) * uContrast + 0.5 + uBrightness, 0.0, 1.0));\n"
      "   } else {\n"
      "      col = mix(uLowColor, uHighColor, n);\n"
      "   }\n"
      "   fragColor = vec4(col, 1.0);\n"
      "}\n";
}

const std::vector<std::string>& NoiseNode::TypeNames()
{
   return kTypeNames;
}

NoiseNode::~NoiseNode()
{
   GLUtil::DestroyFbo(mOut);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

bool NoiseNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;
   mProgram = GLUtil::CompileProgram(kFragSrc);
   return mProgram != 0;
}

void NoiseNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (!EnsureShader())
      return;

   const int w = std::max(4, (int)width);
   const int h = std::max(4, (int)height);
   if (!GLUtil::EnsureFbo(mOut, w, h))
      return;

   GLUtil::RunShaderPass(mOut, mProgram, [this]()
   {
      glUniform1i(glGetUniformLocation(mProgram, "uType"), noiseType);
      glUniform1f(glGetUniformLocation(mProgram, "uScale"), scale);
      glUniform1f(glGetUniformLocation(mProgram, "uOctaves"), octaves);
      glUniform1f(glGetUniformLocation(mProgram, "uLacunarity"), lacunarity);
      glUniform1f(glGetUniformLocation(mProgram, "uGain"), gain);
      glUniform1f(glGetUniformLocation(mProgram, "uWarp"), warp);
      glUniform1f(glGetUniformLocation(mProgram, "uTime"), (float)Transport::Instance().Seconds() * speed);
      glUniform1f(glGetUniformLocation(mProgram, "uContrast"), contrast);
      glUniform1f(glGetUniformLocation(mProgram, "uBrightness"), brightness);
      glUniform1f(glGetUniformLocation(mProgram, "uSeed"), seed);
      glUniform1i(glGetUniformLocation(mProgram, "uColorNoise"), colorNoise ? 1 : 0);
      glUniform3f(glGetUniformLocation(mProgram, "uLowColor"), lowColor[0], lowColor[1], lowColor[2]);
      glUniform3f(glGetUniformLocation(mProgram, "uHighColor"), highColor[0], highColor[1], highColor[2]);
      glUniform1f(glGetUniformLocation(mProgram, "uAspect"), (float)mOut.w / (float)mOut.h);
   });
}
