#include "TextureNode.h"

#include "gl3.h"
#include <algorithm>

namespace
{
   const std::vector<std::string> kTypeNames = { "Voronoi", "Brick", "Magic", "Wave", "Musgrave", "Checker", "Gradient", "Clouds", "Marble", "Wood" };
   const std::vector<std::string> kVoronoiDistanceNames = { "Euclidean", "Manhattan", "Chebyshev", "Minkowski" };
   const std::vector<std::string> kVoronoiFeatureNames = { "F1", "F2", "Smooth F1", "Distance to Edge", "N-Sphere Radius" };
   const std::vector<std::string> kWaveTypeNames = { "Bands", "Rings" };
   const std::vector<std::string> kWaveProfileNames = { "Sine", "Saw", "Triangle" };
   const std::vector<std::string> kWaveBandsDirectionNames = { "X", "Y", "Diagonal" };
   const std::vector<std::string> kMusgraveTypeNames = { "fBm", "Multifractal", "Hybrid Multifractal", "Ridged Multifractal", "Hetero Terrain" };
   const std::vector<std::string> kGradientTypeNames = { "Linear", "Quadratic", "Easing", "Diagonal", "Radial", "Spherical", "Quadratic Sphere" };
   const std::vector<std::string> kMarbleTypeNames = { "Soft", "Sharp", "Sharper" };
   const std::vector<std::string> kWoodTypeNames = { "Bands", "Rings", "Band Noise", "Ring Noise" };

   const char* kFragSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform int uType;\n"
      "uniform float uScale;\n"
      "uniform float uSeed;\n"
      "uniform float uContrast;\n"
      "uniform float uBrightness;\n"
      "uniform vec3 uLowColor;\n"
      "uniform vec3 uHighColor;\n"
      "uniform float uAspect;\n"
      "uniform int uVDistance;\n"
      "uniform int uVFeature;\n"
      "uniform float uVRandomness;\n"
      "uniform float uVMinkowski;\n"
      "uniform float uVSmoothness;\n"
      "uniform int uVCellColor;\n"
      "uniform float uBrickWidth;\n"
      "uniform float uBrickHeight;\n"
      "uniform float uBrickRowOffset;\n"
      "uniform float uBrickMortarSize;\n"
      "uniform float uBrickMortarSmooth;\n"
      "uniform float uBrickBias;\n"
      "uniform vec3 uMortarColor;\n"
      "uniform float uMagicDepth;\n"
      "uniform float uMagicDistortion;\n"
      "uniform int uWaveType;\n"
      "uniform int uWaveProfile;\n"
      "uniform int uWaveBandsDir;\n"
      "uniform float uWaveDistortion;\n"
      "uniform float uWaveDetail;\n"
      "uniform float uWaveDetailScale;\n"
      "uniform float uWavePhase;\n"
      "uniform int uMusType;\n"
      "uniform float uMusDimension;\n"
      "uniform float uMusLacunarity;\n"
      "uniform float uMusOctaves;\n"
      "uniform float uMusGain;\n"
      "uniform float uMusOffset;\n"
      "uniform int uGradType;\n"
      "uniform float uCloudsDepth;\n"
      "uniform int uCloudsHard;\n"
      "uniform int uMarbleType;\n"
      "uniform float uMarbleTurbulence;\n"
      "uniform float uMarbleNoiseScale;\n"
      "uniform float uMarbleNoiseDepth;\n"
      "uniform int uWoodType;\n"
      "uniform float uWoodTurbulence;\n"
      "uniform float uWoodNoiseScale;\n"
      "\n"
      "float hash(vec2 p) {\n"
      "   p += uSeed;\n"
      "   return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);\n"
      "}\n"
      "vec2 hash2(vec2 p) {\n"
      "   p += uSeed;\n"
      "   return fract(sin(vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)))) * 43758.5453);\n"
      "}\n"
      "vec3 hash3(vec2 p) {\n"
      "   p += uSeed;\n"
      "   vec3 q = vec3(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)), dot(p, vec2(419.2, 371.9)));\n"
      "   return fract(sin(q) * 43758.5453);\n"
      "}\n"
      "float valueNoise(vec2 p) {\n"
      "   vec2 i = floor(p), f = fract(p);\n"
      "   vec2 u = f * f * (3.0 - 2.0 * f);\n"
      "   float a = hash(i), b = hash(i + vec2(1.0, 0.0));\n"
      "   float c = hash(i + vec2(0.0, 1.0)), d = hash(i + vec2(1.0, 1.0));\n"
      "   return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);\n"
      "}\n"
      "float fbmBasic(vec2 p, int oct, float lac, float gain) {\n"
      "   float sum = 0.0, amp = 0.5, norm = 0.0;\n"
      "   for (int i = 0; i < 8; i++) {\n"
      "      if (i >= oct) break;\n"
      "      sum += valueNoise(p) * amp; norm += amp;\n"
      "      p *= lac; amp *= gain;\n"
      "   }\n"
      "   return norm > 1e-4 ? sum / norm : 0.0;\n"
      "}\n"
      "float fbmTurbulence(vec2 p, int oct, float lac, float gain) {\n"
      "   float sum = 0.0, amp = 0.5, norm = 0.0;\n"
      "   for (int i = 0; i < 8; i++) {\n"
      "      if (i >= oct) break;\n"
      "      sum += abs(valueNoise(p) * 2.0 - 1.0) * amp; norm += amp;\n"
      "      p *= lac; amp *= gain;\n"
      "   }\n"
      "   return norm > 1e-4 ? sum / norm : 0.0;\n"
      "}\n"
      "float smin(float a, float b, float k) {\n"
      "   float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);\n"
      "   return mix(b, a, h) - k * h * (1.0 - h);\n"
      "}\n"
      "\n"
      "float cellDist(vec2 a, int metric, float mink) {\n"
      "   if (metric == 0) return length(a);\n"
      "   if (metric == 1) return abs(a.x) + abs(a.y);\n"
      "   if (metric == 2) return max(abs(a.x), abs(a.y));\n"
      "   float e = max(mink, 0.1);\n"
      "   return pow(pow(abs(a.x), e) + pow(abs(a.y), e), 1.0 / e);\n"
      "}\n"
      "struct VoronoiResult { float f1; float f2; vec2 f1cell; vec2 f1off; float edge; };\n"
      "VoronoiResult voronoiCalc(vec2 p) {\n"
      "   VoronoiResult r;\n"
      "   r.f1 = 8.0; r.f2 = 8.0; r.edge = 8.0; r.f1cell = vec2(0.0); r.f1off = vec2(0.0);\n"
      "   vec2 i = floor(p), f = fract(p);\n"
      "   for (int y = -2; y <= 2; y++) for (int x = -2; x <= 2; x++) {\n"
      "      vec2 g = vec2(float(x), float(y));\n"
      "      vec2 o = hash2(i + g) * uVRandomness;\n"
      "      vec2 diff = g + o - f;\n"
      "      float d = cellDist(diff, uVDistance, uVMinkowski);\n"
      "      if (d < r.f1) { r.f2 = r.f1; r.f1 = d; r.f1cell = i + g; r.f1off = diff; }\n"
      "      else if (d < r.f2) r.f2 = d;\n"
      "   }\n"
      "   for (int y = -2; y <= 2; y++) for (int x = -2; x <= 2; x++) {\n"
      "      vec2 g = vec2(float(x), float(y));\n"
      "      vec2 o = hash2(i + g) * uVRandomness;\n"
      "      vec2 diff = g + o - f;\n"
      "      if (abs(diff.x - r.f1off.x) < 1e-5 && abs(diff.y - r.f1off.y) < 1e-5) continue;\n"
      "      vec2 toCenter = (diff + r.f1off) * 0.5;\n"
      "      vec2 dir = normalize(diff - r.f1off);\n"
      "      float e = dot(toCenter, dir);\n"
      "      r.edge = min(r.edge, e);\n"
      "   }\n"
      "   return r;\n"
      "}\n"
      "\n"
      "float brickShade(vec2 p, out float mortarMask) {\n"
      "   float rowH = max(uBrickHeight, 0.01);\n"
      "   float bw = max(uBrickWidth, 0.01);\n"
      "   float row = floor(p.y / rowH);\n"
      "   float offset = mod(row, 2.0) * uBrickRowOffset;\n"
      "   float bx = (p.x / bw) + offset;\n"
      "   float by = p.y / rowH;\n"
      "   float cellX = floor(bx);\n"
      "   float cellY = floor(by);\n"
      "   float fx = fract(bx);\n"
      "   float fy = fract(by);\n"
      "   float mortarX = clamp(uBrickMortarSize / bw, 0.0, 0.49);\n"
      "   float mortarY = clamp(uBrickMortarSize / rowH, 0.0, 0.49);\n"
      "   float edgeX = min(fx, 1.0 - fx);\n"
      "   float edgeY = min(fy, 1.0 - fy);\n"
      "   float smoothAmt = max(uBrickMortarSmooth, 0.001);\n"
      "   float sx = 1.0 - smoothstep(mortarX, mortarX * (1.0 + smoothAmt), edgeX);\n"
      "   float sy = 1.0 - smoothstep(mortarY, mortarY * (1.0 + smoothAmt), edgeY);\n"
      "   mortarMask = max(sx, sy);\n"
      "   float h = hash(vec2(cellX, cellY));\n"
      "   return clamp(mix(0.5 - uBrickBias, 0.5 + uBrickBias, h) + (h - 0.5) * 0.3, 0.0, 1.0);\n"
      "}\n"
      "\n"
      "vec3 magicTexture(vec2 p, float depthF, float distortion) {\n"
      "   int depth = int(clamp(depthF, 1.0, 10.0));\n"
      "   float px = p.x, py = p.y, pz = 0.0;\n"
      "   float x = sin((px + py + pz) * 5.0);\n"
      "   float y = cos((-px + py - pz) * 5.0);\n"
      "   float z = -cos((-px - py + pz) * 5.0);\n"
      "   if (depth > 1) { x *= distortion; y *= distortion; z *= distortion; y = -cos(x - y + z); y *= distortion; }\n"
      "   if (depth > 2) { x = cos(x - y - z); x *= distortion; }\n"
      "   if (depth > 3) { z = sin(-x - y - z); z *= distortion; }\n"
      "   if (depth > 4) { x = -cos(-x + y + z); x *= distortion; }\n"
      "   if (depth > 5) { y = -sin(-x + y + z); y *= distortion; }\n"
      "   if (depth > 6) { y = -cos(-x + y + z); y *= distortion; }\n"
      "   if (depth > 7) { x = cos(x + y + z); x *= distortion; }\n"
      "   if (depth > 8) { z = sin(x + y + z); z *= distortion; }\n"
      "   if (depth > 9) { x = -cos(-x - y + z); x *= distortion; }\n"
      "   if (distortion != 0.0) { float d2 = distortion * 2.0; x /= d2; y /= d2; z /= d2; }\n"
      "   return vec3(0.5 - x, 0.5 - y, 0.5 - z);\n"
      "}\n"
      "\n"
      "float waveValue(vec2 p) {\n"
      "   float n;\n"
      "   if (uWaveType == 0) {\n"
      "      if (uWaveBandsDir == 0) n = p.x;\n"
      "      else if (uWaveBandsDir == 1) n = p.y;\n"
      "      else n = (p.x + p.y) * 0.70710678;\n"
      "   } else {\n"
      "      n = length(p);\n"
      "   }\n"
      "   n *= 6.2831853;\n"
      "   n += uWavePhase * 6.2831853;\n"
      "   if (uWaveDistortion != 0.0) {\n"
      "      int oct = int(clamp(uWaveDetail, 1.0, 8.0));\n"
      "      float d = fbmBasic(p * uWaveDetailScale, oct, 2.0, 0.5) * 2.0 - 1.0;\n"
      "      n += d * uWaveDistortion * 6.2831853;\n"
      "   }\n"
      "   float t = fract(n / 6.2831853);\n"
      "   if (uWaveProfile == 0) return 0.5 + 0.5 * sin(n);\n"
      "   else if (uWaveProfile == 1) return t;\n"
      "   else return 1.0 - abs(t * 2.0 - 1.0);\n"
      "}\n"
      "\n"
      "float musgrave(vec2 p) {\n"
      "   int oct = int(clamp(uMusOctaves, 1.0, 8.0));\n"
      "   float freq = 1.0, amp = 1.0;\n"
      "   float lac = max(uMusLacunarity, 1.001);\n"
      "   float dim = uMusDimension;\n"
      "   if (uMusType == 0) {\n"
      "      float sum = 0.0;\n"
      "      for (int i = 0; i < 8; i++) {\n"
      "         if (i >= oct) break;\n"
      "         sum += (valueNoise(p * freq) * 2.0 - 1.0) * amp;\n"
      "         freq *= lac; amp *= pow(lac, -dim);\n"
      "      }\n"
      "      return sum;\n"
      "   } else if (uMusType == 1) {\n"
      "      float value = 1.0;\n"
      "      for (int i = 0; i < 8; i++) {\n"
      "         if (i >= oct) break;\n"
      "         value *= (amp * (valueNoise(p * freq) * 2.0 - 1.0) + 1.0);\n"
      "         freq *= lac; amp *= pow(lac, -dim);\n"
      "      }\n"
      "      return value - 1.0;\n"
      "   } else if (uMusType == 2) {\n"
      "      float value = (valueNoise(p) * 2.0 - 1.0) + uMusOffset;\n"
      "      float weight = value;\n"
      "      freq *= lac; amp *= pow(lac, -dim);\n"
      "      for (int i = 1; i < 8; i++) {\n"
      "         if (i >= oct) break;\n"
      "         weight = clamp(weight, 0.0, 1.0);\n"
      "         float signal = amp * ((valueNoise(p * freq) * 2.0 - 1.0) + uMusOffset);\n"
      "         value += weight * signal;\n"
      "         weight *= signal;\n"
      "         freq *= lac; amp *= pow(lac, -dim);\n"
      "      }\n"
      "      return value;\n"
      "   } else if (uMusType == 3) {\n"
      "      float signal = uMusOffset - abs(valueNoise(p) * 2.0 - 1.0);\n"
      "      signal *= signal;\n"
      "      float value = signal;\n"
      "      float weight = 1.0;\n"
      "      freq *= lac; amp *= pow(lac, -dim);\n"
      "      for (int i = 1; i < 8; i++) {\n"
      "         if (i >= oct) break;\n"
      "         weight = clamp(signal * uMusGain, 0.0, 1.0);\n"
      "         signal = uMusOffset - abs(valueNoise(p * freq) * 2.0 - 1.0);\n"
      "         signal *= signal;\n"
      "         signal *= weight;\n"
      "         value += signal * amp;\n"
      "         freq *= lac; amp *= pow(lac, -dim);\n"
      "      }\n"
      "      return value;\n"
      "   } else {\n"
      "      float value = uMusOffset + (valueNoise(p) * 2.0 - 1.0);\n"
      "      freq *= lac; amp *= pow(lac, -dim);\n"
      "      for (int i = 1; i < 8; i++) {\n"
      "         if (i >= oct) break;\n"
      "         float increment = ((valueNoise(p * freq) * 2.0 - 1.0) + uMusOffset) * amp * value;\n"
      "         value += increment;\n"
      "         freq *= lac; amp *= pow(lac, -dim);\n"
      "      }\n"
      "      return value;\n"
      "   }\n"
      "}\n"
      "\n"
      "float checkerValue(vec2 p) {\n"
      "   vec2 c = floor(p);\n"
      "   return mod(c.x + c.y, 2.0) < 1.0 ? 0.0 : 1.0;\n"
      "}\n"
      "\n"
      "float gradientValue(vec2 gp) {\n"
      "   if (uGradType == 0) return gp.x * 0.5 + 0.5;\n"
      "   else if (uGradType == 1) { float t = max(gp.x, 0.0); return t * t; }\n"
      "   else if (uGradType == 2) { float t = clamp(gp.x * 0.5 + 0.5, 0.0, 1.0); return t * t * (3.0 - 2.0 * t); }\n"
      "   else if (uGradType == 3) return (gp.x + gp.y) * 0.25 + 0.5;\n"
      "   else if (uGradType == 4) return atan(gp.y, gp.x) / 6.2831853 + 0.5;\n"
      "   else if (uGradType == 5) return clamp(1.0 - length(gp), 0.0, 1.0);\n"
      "   else return clamp(sqrt(max(0.0, 1.0 - dot(gp, gp))), 0.0, 1.0);\n"
      "}\n"
      "\n"
      "float cloudsValue(vec2 p) {\n"
      "   int oct = int(clamp(uCloudsDepth, 1.0, 8.0));\n"
      "   return uCloudsHard == 1 ? fbmTurbulence(p, oct, 2.0, 0.5) : fbmBasic(p, oct, 2.0, 0.5);\n"
      "}\n"
      "\n"
      "float marbleValue(vec2 p) {\n"
      "   int depth = int(clamp(uMarbleNoiseDepth, 1.0, 8.0));\n"
      "   float turb = fbmTurbulence(p * uMarbleNoiseScale, depth, 2.0, 0.5);\n"
      "   float w = sin((p.x + p.y) * 5.0 + turb * uMarbleTurbulence);\n"
      "   float v = clamp(w * 0.5 + 0.5, 0.0, 1.0);\n"
      "   if (uMarbleType == 1) v = pow(v, 2.0);\n"
      "   else if (uMarbleType == 2) v = pow(v, 6.0);\n"
      "   return v;\n"
      "}\n"
      "\n"
      "float woodValue(vec2 p) {\n"
      "   float turb = fbmTurbulence(p * uWoodNoiseScale, 4, 2.0, 0.5) * uWoodTurbulence;\n"
      "   float grain;\n"
      "   if (uWoodType == 0 || uWoodType == 2) grain = p.x + turb;\n"
      "   else grain = length(p) + turb;\n"
      "   float v = 0.5 + 0.5 * sin(grain * 6.2831853);\n"
      "   if (uWoodType >= 2) v = mix(v, valueNoise(p * 6.0), 0.25);\n"
      "   return v;\n"
      "}\n"
      "\n"
      "void main() {\n"
      "   vec2 uv = vUv;\n"
      "   uv.x *= uAspect;\n"
      "   vec2 p = uv * uScale;\n"
      "   vec3 outColor;\n"
      "   if (uType == 1) {\n"
      "      float mortarMask;\n"
      "      float shade = brickShade(p, mortarMask);\n"
      "      shade = clamp((shade - 0.5) * uContrast + 0.5 + uBrightness, 0.0, 1.0);\n"
      "      vec3 brickCol = mix(uLowColor, uHighColor, shade);\n"
      "      outColor = mix(brickCol, uMortarColor, mortarMask);\n"
      "   } else {\n"
      "      float n = 0.0;\n"
      "      vec3 nativeColor = vec3(0.0);\n"
      "      bool useNativeColor = false;\n"
      "      if (uType == 0) {\n"
      "         VoronoiResult r = voronoiCalc(p);\n"
      "         float val;\n"
      "         if (uVFeature == 0) val = r.f1;\n"
      "         else if (uVFeature == 1) val = r.f2;\n"
      "         else if (uVFeature == 2) val = smin(r.f1, r.f2, max(uVSmoothness, 0.001));\n"
      "         else if (uVFeature == 3) val = r.edge;\n"
      "         else val = r.f1 * 0.5;\n"
      "         n = val;\n"
      "         if (uVCellColor == 1) { nativeColor = hash3(r.f1cell); useNativeColor = true; }\n"
      "      } else if (uType == 2) {\n"
      "         nativeColor = magicTexture(p, uMagicDepth, uMagicDistortion);\n"
      "         useNativeColor = true;\n"
      "      } else if (uType == 3) {\n"
      "         n = waveValue(p);\n"
      "      } else if (uType == 4) {\n"
      "         n = musgrave(p) * 0.5 + 0.5;\n"
      "      } else if (uType == 5) {\n"
      "         n = checkerValue(p);\n"
      "      } else if (uType == 6) {\n"
      "         vec2 gp = uv - vec2(0.5 * uAspect, 0.5);\n"
      "         n = clamp(gradientValue(gp), 0.0, 1.0);\n"
      "      } else if (uType == 7) {\n"
      "         n = cloudsValue(p);\n"
      "      } else if (uType == 8) {\n"
      "         n = marbleValue(p);\n"
      "      } else {\n"
      "         n = woodValue(p);\n"
      "      }\n"
      "      if (useNativeColor) {\n"
      "         outColor = clamp((nativeColor - 0.5) * uContrast + 0.5 + uBrightness, 0.0, 1.0);\n"
      "      } else {\n"
      "         n = clamp((n - 0.5) * uContrast + 0.5 + uBrightness, 0.0, 1.0);\n"
      "         outColor = mix(uLowColor, uHighColor, n);\n"
      "      }\n"
      "   }\n"
      "   fragColor = vec4(outColor, 1.0);\n"
      "}\n";
}

const std::vector<std::string>& TextureNode::TypeNames() { return kTypeNames; }
const std::vector<std::string>& TextureNode::VoronoiDistanceNames() { return kVoronoiDistanceNames; }
const std::vector<std::string>& TextureNode::VoronoiFeatureNames() { return kVoronoiFeatureNames; }
const std::vector<std::string>& TextureNode::WaveTypeNames() { return kWaveTypeNames; }
const std::vector<std::string>& TextureNode::WaveProfileNames() { return kWaveProfileNames; }
const std::vector<std::string>& TextureNode::WaveBandsDirectionNames() { return kWaveBandsDirectionNames; }
const std::vector<std::string>& TextureNode::MusgraveTypeNames() { return kMusgraveTypeNames; }
const std::vector<std::string>& TextureNode::GradientTypeNames() { return kGradientTypeNames; }
const std::vector<std::string>& TextureNode::MarbleTypeNames() { return kMarbleTypeNames; }
const std::vector<std::string>& TextureNode::WoodTypeNames() { return kWoodTypeNames; }

TextureNode::~TextureNode()
{
   GLUtil::DestroyFbo(mOut);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

bool TextureNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;
   mProgram = GLUtil::CompileProgram(kFragSrc);
   return mProgram != 0;
}

void TextureNode::CookIfNeeded(int frameId)
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
      glUniform1i(glGetUniformLocation(mProgram, "uType"), textureType);
      glUniform1f(glGetUniformLocation(mProgram, "uScale"), scale);
      glUniform1f(glGetUniformLocation(mProgram, "uSeed"), seed);
      glUniform1f(glGetUniformLocation(mProgram, "uContrast"), contrast);
      glUniform1f(glGetUniformLocation(mProgram, "uBrightness"), brightness);
      glUniform3f(glGetUniformLocation(mProgram, "uLowColor"), lowColor[0], lowColor[1], lowColor[2]);
      glUniform3f(glGetUniformLocation(mProgram, "uHighColor"), highColor[0], highColor[1], highColor[2]);
      glUniform1f(glGetUniformLocation(mProgram, "uAspect"), (float)mOut.w / (float)mOut.h);

      glUniform1i(glGetUniformLocation(mProgram, "uVDistance"), voronoiDistance);
      glUniform1i(glGetUniformLocation(mProgram, "uVFeature"), voronoiFeature);
      glUniform1f(glGetUniformLocation(mProgram, "uVRandomness"), voronoiRandomness);
      glUniform1f(glGetUniformLocation(mProgram, "uVMinkowski"), voronoiMinkowskiExponent);
      glUniform1f(glGetUniformLocation(mProgram, "uVSmoothness"), voronoiSmoothness);
      glUniform1i(glGetUniformLocation(mProgram, "uVCellColor"), voronoiCellColor ? 1 : 0);

      glUniform1f(glGetUniformLocation(mProgram, "uBrickWidth"), brickWidth);
      glUniform1f(glGetUniformLocation(mProgram, "uBrickHeight"), brickHeight);
      glUniform1f(glGetUniformLocation(mProgram, "uBrickRowOffset"), brickRowOffset);
      glUniform1f(glGetUniformLocation(mProgram, "uBrickMortarSize"), brickMortarSize);
      glUniform1f(glGetUniformLocation(mProgram, "uBrickMortarSmooth"), brickMortarSmooth);
      glUniform1f(glGetUniformLocation(mProgram, "uBrickBias"), brickBias);
      glUniform3f(glGetUniformLocation(mProgram, "uMortarColor"), mortarColor[0], mortarColor[1], mortarColor[2]);

      glUniform1f(glGetUniformLocation(mProgram, "uMagicDepth"), magicDepth);
      glUniform1f(glGetUniformLocation(mProgram, "uMagicDistortion"), magicDistortion);

      glUniform1i(glGetUniformLocation(mProgram, "uWaveType"), waveType);
      glUniform1i(glGetUniformLocation(mProgram, "uWaveProfile"), waveProfile);
      glUniform1i(glGetUniformLocation(mProgram, "uWaveBandsDir"), waveBandsDirection);
      glUniform1f(glGetUniformLocation(mProgram, "uWaveDistortion"), waveDistortion);
      glUniform1f(glGetUniformLocation(mProgram, "uWaveDetail"), waveDetail);
      glUniform1f(glGetUniformLocation(mProgram, "uWaveDetailScale"), waveDetailScale);
      glUniform1f(glGetUniformLocation(mProgram, "uWavePhase"), wavePhaseOffset);

      glUniform1i(glGetUniformLocation(mProgram, "uMusType"), musgraveType);
      glUniform1f(glGetUniformLocation(mProgram, "uMusDimension"), musgraveDimension);
      glUniform1f(glGetUniformLocation(mProgram, "uMusLacunarity"), musgraveLacunarity);
      glUniform1f(glGetUniformLocation(mProgram, "uMusOctaves"), musgraveOctaves);
      glUniform1f(glGetUniformLocation(mProgram, "uMusGain"), musgraveGain);
      glUniform1f(glGetUniformLocation(mProgram, "uMusOffset"), musgraveOffset);

      glUniform1i(glGetUniformLocation(mProgram, "uGradType"), gradientType);

      glUniform1f(glGetUniformLocation(mProgram, "uCloudsDepth"), cloudsDepth);
      glUniform1i(glGetUniformLocation(mProgram, "uCloudsHard"), cloudsHard ? 1 : 0);

      glUniform1i(glGetUniformLocation(mProgram, "uMarbleType"), marbleType);
      glUniform1f(glGetUniformLocation(mProgram, "uMarbleTurbulence"), marbleTurbulence);
      glUniform1f(glGetUniformLocation(mProgram, "uMarbleNoiseScale"), marbleNoiseScale);
      glUniform1f(glGetUniformLocation(mProgram, "uMarbleNoiseDepth"), marbleNoiseDepth);

      glUniform1i(glGetUniformLocation(mProgram, "uWoodType"), woodType);
      glUniform1f(glGetUniformLocation(mProgram, "uWoodTurbulence"), woodTurbulence);
      glUniform1f(glGetUniformLocation(mProgram, "uWoodNoiseScale"), woodNoiseScale);
   });
}
