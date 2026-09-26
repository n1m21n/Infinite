#include "NoiseNode.h"

#include "platform/OpenGLHeaders.h"
#include <algorithm>

#include "Transport.h"

namespace
{
   const std::vector<std::string> kTypeNames = {
      "Value", "fBm", "Ridged", "Voronoi", "Worley Edges", "White",
      "Simplex 4D", "Perlin 4D", "Ridged 4D", "Turbulence 4D", "Billow 4D"
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
      "uniform vec2 uTranslate;\n"
      "uniform vec2 uDrift;\n"
      "uniform float uSeconds;\n"
      "uniform float uZ;\n"
      "uniform float uRot;\n"
      "uniform float uExponent;\n"
      "\n"
      // 4D simplex noise: Ashima Arts / Stefan Gustavson webgl-noise (MIT).
      "vec4 mod289(vec4 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }\n"
      "float mod289(float x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }\n"
      "vec4 permute(vec4 x) { return mod289(((x * 34.0) + 1.0) * x); }\n"
      "float permute(float x) { return mod289(((x * 34.0) + 1.0) * x); }\n"
      "vec4 taylorInvSqrt(vec4 r) { return 1.79284291400159 - 0.85373472095314 * r; }\n"
      "float taylorInvSqrt(float r) { return 1.79284291400159 - 0.85373472095314 * r; }\n"
      "vec4 grad4(float j, vec4 ip) {\n"
      "   const vec4 ones = vec4(1.0, 1.0, 1.0, -1.0);\n"
      "   vec4 p, s;\n"
      "   p.xyz = floor(fract(vec3(j) * ip.xyz) * 7.0) * ip.z - 1.0;\n"
      "   p.w = 1.5 - dot(abs(p.xyz), ones.xyz);\n"
      "   s = vec4(lessThan(p, vec4(0.0)));\n"
      "   p.xyz = p.xyz + (s.xyz * 2.0 - 1.0) * s.www;\n"
      "   return p;\n"
      "}\n"
      "float snoise4(vec4 v) {\n"
      "   const vec4 C = vec4(0.138196601125011, 0.276393202250021, 0.414589803375032, -0.447213595499958);\n"
      "   vec4 i = floor(v + dot(v, vec4(0.309016994374947451)));\n"
      "   vec4 x0 = v - i + dot(i, C.xxxx);\n"
      "   vec4 i0;\n"
      "   vec3 isX = step(x0.yzw, x0.xxx);\n"
      "   vec3 isYZ = step(x0.zww, x0.yyz);\n"
      "   i0.x = isX.x + isX.y + isX.z;\n"
      "   i0.yzw = 1.0 - isX;\n"
      "   i0.y += isYZ.x + isYZ.y;\n"
      "   i0.zw += 1.0 - isYZ.xy;\n"
      "   i0.z += isYZ.z;\n"
      "   i0.w += 1.0 - isYZ.z;\n"
      "   vec4 i3 = clamp(i0, 0.0, 1.0);\n"
      "   vec4 i2 = clamp(i0 - 1.0, 0.0, 1.0);\n"
      "   vec4 i1 = clamp(i0 - 2.0, 0.0, 1.0);\n"
      "   vec4 x1 = x0 - i1 + C.xxxx;\n"
      "   vec4 x2 = x0 - i2 + C.yyyy;\n"
      "   vec4 x3 = x0 - i3 + C.zzzz;\n"
      "   vec4 x4 = x0 + C.wwww;\n"
      "   i = mod289(i);\n"
      "   float j0 = permute(permute(permute(permute(i.w) + i.z) + i.y) + i.x);\n"
      "   vec4 j1 = permute(permute(permute(permute(\n"
      "              i.w + vec4(i1.w, i2.w, i3.w, 1.0))\n"
      "            + i.z + vec4(i1.z, i2.z, i3.z, 1.0))\n"
      "            + i.y + vec4(i1.y, i2.y, i3.y, 1.0))\n"
      "            + i.x + vec4(i1.x, i2.x, i3.x, 1.0));\n"
      "   vec4 ip = vec4(1.0 / 294.0, 1.0 / 49.0, 1.0 / 7.0, 0.0);\n"
      "   vec4 p0 = grad4(j0, ip);\n"
      "   vec4 p1 = grad4(j1.x, ip);\n"
      "   vec4 p2 = grad4(j1.y, ip);\n"
      "   vec4 p3 = grad4(j1.z, ip);\n"
      "   vec4 p4 = grad4(j1.w, ip);\n"
      "   vec4 norm = taylorInvSqrt(vec4(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));\n"
      "   p0 *= norm.x; p1 *= norm.y; p2 *= norm.z; p3 *= norm.w;\n"
      "   p4 *= taylorInvSqrt(dot(p4, p4));\n"
      "   vec3 m0 = max(0.6 - vec3(dot(x0, x0), dot(x1, x1), dot(x2, x2)), 0.0);\n"
      "   vec2 m1 = max(0.6 - vec2(dot(x3, x3), dot(x4, x4)), 0.0);\n"
      "   m0 = m0 * m0; m1 = m1 * m1;\n"
      "   return 49.0 * (dot(m0 * m0, vec3(dot(p0, x0), dot(p1, x1), dot(p2, x2)))\n"
      "                + dot(m1 * m1, vec2(dot(p3, x3), dot(p4, x4))));\n"
      "}\n"
      // 4D gradient (Perlin) noise: quintic fade over the 16 hypercube
      // corners, gradients from Dave Hoskins' hash44 (MIT).
      "vec4 hash44(vec4 p) {\n"
      "   p = fract(p * vec4(0.1031, 0.1030, 0.0973, 0.1099));\n"
      "   p += dot(p, p.wzxy + 33.33);\n"
      "   return fract((p.xxyz + p.yzzw) * p.zywx) * 2.0 - 1.0;\n"
      "}\n"
      "float perlin4(vec4 p) {\n"
      "   vec4 i = floor(p), f = fract(p);\n"
      "   vec4 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);\n"
      "   float sum = 0.0;\n"
      "   for (int c = 0; c < 16; c++) {\n"
      "      vec4 o = vec4(float(c & 1), float((c >> 1) & 1), float((c >> 2) & 1), float((c >> 3) & 1));\n"
      "      vec4 wv = mix(1.0 - u, u, o);\n"
      "      sum += wv.x * wv.y * wv.z * wv.w * dot(hash44(i + o), f - o);\n"
      "   }\n"
      "   return sum * 1.6;\n"
      "}\n"
      // Octaves of a 4D basis (0 simplex, 1 perlin); mode 0 plain, 1 ridged,
      // 2 turbulence, 3 billow. Each octave is rotated so the lattice
      // directions do not line up.
      "float fbm4(vec4 p, int basis, int mode) {\n"
      "   float sum = 0.0, amp = 1.0, norm = 0.0;\n"
      "   int oct = int(clamp(uOctaves, 1.0, 8.0));\n"
      "   const mat2 r = mat2(0.8, 0.6, -0.6, 0.8);\n"
      "   for (int k = 0; k < 8; k++) {\n"
      "      if (k >= oct) break;\n"
      "      float n = basis == 0 ? snoise4(p) : perlin4(p);\n"
      "      n = clamp(n, -1.0, 1.0);\n"
      "      if (mode == 1) { n = 1.0 - abs(n); n *= n; }\n"
      "      else if (mode == 2) n = abs(n);\n"
      "      else if (mode == 3) n = pow(abs(n), 0.6);\n"
      "      else n = n * 0.5 + 0.5;\n"
      "      sum += n * amp; norm += amp;\n"
      "      p.xy = r * p.xy;\n"
      "      p = p * uLacunarity + vec4(1.7, 9.2, 3.1, 5.3);\n"
      "      amp *= uGain;\n"
      "   }\n"
      "   return sum / max(norm, 1e-4);\n"
      "}\n"
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
      "vec3 finishColor(float n, vec4 p, int basis, int mode) {\n"
      "   n = pow(clamp(n, 0.0, 1.0), max(uExponent, 0.01));\n"
      "   n = clamp((n - 0.5) * uContrast + 0.5 + uBrightness, 0.0, 1.0);\n"
      "   if (uColorNoise == 1) {\n"
      "      float g = pow(clamp(fbm4(p + vec4(0.0, 0.0, 11.3, 0.0), basis, mode), 0.0, 1.0), max(uExponent, 0.01));\n"
      "      float b = pow(clamp(fbm4(p + vec4(0.0, 0.0, 23.7, 0.0), basis, mode), 0.0, 1.0), max(uExponent, 0.01));\n"
      "      g = clamp((g - 0.5) * uContrast + 0.5 + uBrightness, 0.0, 1.0);\n"
      "      b = clamp((b - 0.5) * uContrast + 0.5 + uBrightness, 0.0, 1.0);\n"
      "      return vec3(n, g, b);\n"
      "   }\n"
      "   return mix(uLowColor, uHighColor, n);\n"
      "}\n"
      "\n"
      "void main() {\n"
      "   if (uType >= 6) {\n"
      "      vec2 q = vUv - 0.5;\n"
      "      q.x *= uAspect;\n"
      "      float a = radians(uRot);\n"
      "      q = mat2(cos(a), sin(a), -sin(a), cos(a)) * q;\n"
      "      vec2 xy = q * uScale + uTranslate + uDrift * uSeconds;\n"
      "      vec4 p = vec4(xy, uZ, uTime) + vec4(uSeed * 1.37, uSeed * 2.71, uSeed * 0.61, uSeed * 3.17);\n"
      "      int basis = uType == 7 ? 1 : 0;\n"
      "      int mode = uType == 8 ? 1 : (uType == 9 ? 2 : (uType == 10 ? 3 : 0));\n"
      "      if (uWarp > 0.0) {\n"
      "         vec2 w = vec2(fbm4(p, basis, 0), fbm4(p + vec4(5.2, 1.3, 2.8, 0.0), basis, 0));\n"
      "         p.xy += (w - 0.5) * uWarp * 4.0;\n"
      "      }\n"
      "      fragColor = vec4(finishColor(fbm4(p, basis, mode), p, basis, mode), 1.0);\n"
      "      return;\n"
      "   }\n"
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

   ParamSnapshot params;
   VisitParams(params);
   const float seconds = (float)Transport::Instance().Seconds();
   const float time = seconds * speed;
   // Raw seconds, not time: Turbo's drift (uSeconds) animates even at speed 0.
   if (mHasBuilt && params == mBuiltParams && seconds == mBuiltTime)
      return; // nothing changed since the last cook - reuse mOut as-is

   NodeWorkCounter()++;
   GLUtil::RunShaderPass(mOut, mProgram, [this, time, seconds]()
   {
      glUniform1i(glGetUniformLocation(mProgram, "uType"), noiseType);
      glUniform1f(glGetUniformLocation(mProgram, "uScale"), scale);
      glUniform1f(glGetUniformLocation(mProgram, "uOctaves"), octaves);
      glUniform1f(glGetUniformLocation(mProgram, "uLacunarity"), lacunarity);
      glUniform1f(glGetUniformLocation(mProgram, "uGain"), gain);
      glUniform1f(glGetUniformLocation(mProgram, "uWarp"), warp);
      glUniform1f(glGetUniformLocation(mProgram, "uTime"), time);
      glUniform1f(glGetUniformLocation(mProgram, "uContrast"), contrast);
      glUniform1f(glGetUniformLocation(mProgram, "uBrightness"), brightness);
      glUniform1f(glGetUniformLocation(mProgram, "uSeed"), seed);
      glUniform1i(glGetUniformLocation(mProgram, "uColorNoise"), colorNoise ? 1 : 0);
      glUniform3f(glGetUniformLocation(mProgram, "uLowColor"), lowColor[0], lowColor[1], lowColor[2]);
      glUniform3f(glGetUniformLocation(mProgram, "uHighColor"), highColor[0], highColor[1], highColor[2]);
      glUniform1f(glGetUniformLocation(mProgram, "uAspect"), (float)mOut.w / (float)mOut.h);
      glUniform2f(glGetUniformLocation(mProgram, "uTranslate"), translateX, translateY);
      glUniform2f(glGetUniformLocation(mProgram, "uDrift"), driftX, driftY);
      glUniform1f(glGetUniformLocation(mProgram, "uSeconds"), seconds);
      glUniform1f(glGetUniformLocation(mProgram, "uZ"), zOffset);
      glUniform1f(glGetUniformLocation(mProgram, "uRot"), rotate);
      glUniform1f(glGetUniformLocation(mProgram, "uExponent"), exponent);
   });
   mBuiltParams = std::move(params);
   mBuiltTime = seconds;
   mHasBuilt = true;
   mRevision = NextTextureRevision();
}
