#include "ResynthNode.h"

#include "gl3.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "Transport.h"

namespace
{
   const std::vector<std::string> kModeNames = {
      "Drift", "Quilt", "Cellular", "Spectral", "Shatter"
   };

   // Index order must match the uAmt[] slots the shader reads.
   const std::vector<std::string> kEffectNames = {
      "Flow Warp", "Patch Jump", "Cell Pull", "Ring Warp",
      "Shatter", "Hue Drift", "Posterize", "Grain"
   };

   // One generation of the mutation. Reads the previous generation (uPrev) and
   // the untouched source (uSrc), and writes the next generation.
   const char* kFragSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uPrev;\n"
      "uniform sampler2D uSrc;\n"
      "uniform vec2 uTexelSize;\n"
      "uniform int uMode;\n"
      "uniform int uFirst;\n"
      "uniform float uChaos;\n"
      "uniform float uMutation;\n"
      "uniform float uFeedback;\n"
      "uniform float uSourcePull;\n"
      "uniform float uGeneration;\n"
      "uniform float uSeed;\n"
      "uniform float uAmt[8];\n"
      "\n"
      "float hash(vec2 p) {\n"
      "   p += uSeed;\n"
      "   return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);\n"
      "}\n"
      "vec2 hash2(vec2 p) {\n"
      "   p += uSeed;\n"
      "   return fract(sin(vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)))) * 43758.5453);\n"
      "}\n"
      "float vnoise(vec2 p) {\n"
      "   vec2 i = floor(p), f = fract(p);\n"
      "   vec2 u = f * f * (3.0 - 2.0 * f);\n"
      "   return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),\n"
      "              mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);\n"
      "}\n"
      "vec3 rgb2hsv(vec3 c) {\n"
      "   vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);\n"
      "   vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));\n"
      "   vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));\n"
      "   float d = q.x - min(q.w, q.y);\n"
      "   return vec3(abs(q.z + (q.w - q.y) / (6.0*d + 1e-10)), d / (q.x + 1e-10), q.x);\n"
      "}\n"
      "vec3 hsv2rgb(vec3 c) {\n"
      "   vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);\n"
      "   vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);\n"
      "   return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);\n"
      "}\n"
      "\n"
      "void main() {\n"
      "   vec4 src = texture(uSrc, vUv);\n"
      "   if (uFirst == 1) { fragColor = src; return; }\n"
      "\n"
      "   float g = uGeneration;\n"
      "   vec2 uv = vUv;\n"
      "\n"
      "   // uAmt[i] is how strongly effect i is being asked for by the pad.\n"
      "   // 0 Flow Warp\n"
      "   if (uAmt[0] > 0.001) {\n"
      "      vec2 flow = vec2(vnoise(uv * 8.0 + g * 0.07), vnoise(uv * 8.0 - g * 0.05)) - 0.5;\n"
      "      uv += flow * uAmt[0] * uMutation * uChaos * 0.12;\n"
      "   }\n"
      "   // 1 Patch Jump\n"
      "   if (uAmt[1] > 0.001) {\n"
      "      float patch = 16.0;\n"
      "      vec2 cell = floor(uv * patch);\n"
      "      uv += (hash2(cell + floor(g)) - 0.5) * uAmt[1] * uChaos * 0.2;\n"
      "   }\n"
      "   // 2 Cell Pull\n"
      "   if (uAmt[2] > 0.001) {\n"
      "      float scale = 9.0;\n"
      "      vec2 i2 = floor(uv * scale), f2 = fract(uv * scale);\n"
      "      float best = 8.0; vec2 bestOff = vec2(0.0);\n"
      "      for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) {\n"
      "         vec2 gg = vec2(float(x), float(y));\n"
      "         vec2 o = hash2(i2 + gg + floor(g * 0.5));\n"
      "         float d = length(gg + o - f2);\n"
      "         if (d < best) { best = d; bestOff = gg + o - f2; }\n"
      "      }\n"
      "      uv += bestOff / scale * uAmt[2] * uChaos * 0.6;\n"
      "   }\n"
      "   // 3 Ring Warp\n"
      "   if (uAmt[3] > 0.001) {\n"
      "      vec2 d = uv - 0.5;\n"
      "      float ring = sin(length(d) * 40.0 - g * 0.4);\n"
      "      uv += normalize(d + 1e-5) * ring * uAmt[3] * uChaos * 0.03;\n"
      "   }\n"
      "   // 4 Shatter\n"
      "   if (uAmt[4] > 0.001) {\n"
      "      vec2 cell = floor(uv * 14.0);\n"
      "      if (hash(cell + floor(g)) > 1.0 - uAmt[4] * uChaos)\n"
      "         uv += (hash2(cell + 3.0) - 0.5) * 0.25 * uAmt[4] * uChaos;\n"
      "   }\n"
      "\n"
      "   uv = clamp(uv, 0.0, 1.0);\n"
      "   vec4 prev = texture(uPrev, uv);\n"
      "   vec3 col = prev.rgb;\n"
      "\n"
      "   // 5 Hue Drift\n"
      "   if (uAmt[5] > 0.001) {\n"
      "      vec3 hsv = rgb2hsv(col);\n"
      "      hsv.x = fract(hsv.x + uAmt[5] * uMutation * 0.12);\n"
      "      hsv.y = clamp(hsv.y * (1.0 + uAmt[5] * uMutation * 0.5), 0.0, 1.0);\n"
      "      col = hsv2rgb(hsv);\n"
      "   }\n"
      "   // 6 Posterize\n"
      "   if (uAmt[6] > 0.001) {\n"
      "      float levels = mix(255.0, 3.0, uAmt[6] * uMutation);\n"
      "      col = floor(col * levels + 0.5) / levels;\n"
      "   }\n"
      "   // 7 Grain\n"
      "   if (uAmt[7] > 0.001)\n"
      "      col += (hash(vUv * 997.0 + g) - 0.5) * uAmt[7] * uChaos * 0.35;\n"
      "\n"
      "   vec3 outCol = mix(prev.rgb, col, clamp(uMutation, 0.0, 1.0));\n"
      "   outCol = mix(outCol, prev.rgb, 1.0 - clamp(uFeedback, 0.0, 1.0));\n"
      "   outCol = mix(outCol, src.rgb, clamp(uSourcePull, 0.0, 1.0));\n"
      "   fragColor = vec4(clamp(outCol, 0.0, 1.0), max(prev.a, src.a));\n"
      "}\n";
}

const std::vector<std::string>& ResynthNode::ModeNames()
{
   return kModeNames;
}

const std::vector<std::string>& ResynthNode::EffectNames()
{
   return kEffectNames;
}

const char* ResynthNode::CornerLabel(int corner) const
{
   const int e = std::max(0, std::min(cornerEffect[corner], kEffectCount - 1));
   return kEffectNames[e].c_str();
}

std::string ResynthNode::EncodePath(const std::vector<PadPoint>& path)
{
   std::string out;
   for (size_t i = 0; i < path.size(); i++)
   {
      if (i > 0)
         out += ";";
      char buf[64];
      snprintf(buf, sizeof(buf), "%.6g,%.6g,%.9g",
               (double)path[i].x, (double)path[i].y, path[i].beat);
      out += buf;
   }
   return out;
}

std::vector<ResynthNode::PadPoint> ResynthNode::DecodePath(const std::string& s)
{
   std::vector<PadPoint> path;
   size_t start = 0;
   while (start < s.size())
   {
      size_t sep = s.find(';', start);
      std::string term = s.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
      const size_t c1 = term.find(',');
      const size_t c2 = c1 == std::string::npos ? std::string::npos : term.find(',', c1 + 1);
      if (c1 != std::string::npos && c2 != std::string::npos)
      {
         PadPoint p;
         p.x = (float)atof(term.substr(0, c1).c_str());
         p.y = (float)atof(term.substr(c1 + 1, c2 - c1 - 1).c_str());
         p.beat = atof(term.substr(c2 + 1).c_str());
         path.push_back(p);
      }
      if (sep == std::string::npos)
         break;
      start = sep + 1;
   }
   return path;
}

ResynthNode::~ResynthNode()
{
   GLUtil::DestroyFbo(mBuffers[0]);
   GLUtil::DestroyFbo(mBuffers[1]);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

unsigned int ResynthNode::GetOutputTexture()
{
   return GLUtil::FboTexture(mBuffers[mFront]);
}

bool ResynthNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;
   mProgram = GLUtil::CompileProgram(kFragSrc);
   return mProgram != 0;
}

void ResynthNode::Randomise()
{
   // Re-rolls which effect sits at each corner (all four distinct) and how hard
   // it pushes. Deterministic from the seed so a look can be recovered.
   seed = std::fmod(seed * 1.618f + 7.31f, 100.0f);

   bool used[kEffectCount] = { false, false, false, false, false, false, false, false };
   for (int c = 0; c < kCorners; c++)
   {
      const float r = std::fmod(std::fabs(std::sin((seed + 1.0f) * (float)(c + 3) * 12.9898f) * 43758.5453f), 1.0f);
      int pick = (int)(r * kEffectCount) % kEffectCount;
      for (int guard = 0; guard < kEffectCount && used[pick]; guard++)
         pick = (pick + 1) % kEffectCount;
      used[pick] = true;
      cornerEffect[c] = pick;

      const float a = std::fmod(std::fabs(std::sin((seed + 5.0f) * (float)(c + 7) * 78.233f) * 43758.5453f), 1.0f);
      cornerAmount[c] = 0.4f + a * 0.6f;
   }
}

void ResynthNode::StartRecording()
{
   mPath.clear();
   mRecordingPath = true;
   mPlayingPath = false;
   mPathStartBeat = Transport::Instance().Beats();
}

void ResynthNode::StopRecording()
{
   mRecordingPath = false;
}

void ResynthNode::PlayPath()
{
   if (mPath.empty())
      return;
   mPlayingPath = true;
   mRecordingPath = false;
   mPathStartBeat = Transport::Instance().Beats();
}

void ResynthNode::StopPath()
{
   mPlayingPath = false;
}

void ResynthNode::ClearPath()
{
   mPath.clear();
   mPlayingPath = false;
   mRecordingPath = false;
}

void ResynthNode::UpdatePathPlayback()
{
   const double beat = Transport::Instance().Beats();

   if (mRecordingPath)
   {
      PadPoint point;
      point.x = padX;
      point.y = padY;
      point.beat = beat - mPathStartBeat;
      // keep the path compact: only store real movement
      if (mPath.empty() ||
          std::fabs(mPath.back().x - point.x) > 0.002f ||
          std::fabs(mPath.back().y - point.y) > 0.002f ||
          point.beat - mPath.back().beat > 0.25)
      {
         mPath.push_back(point);
      }
      return;
   }

   if (!mPlayingPath || mPath.size() < 2)
      return;

   const double duration = mPath.back().beat;
   if (duration <= 0.0)
      return;

   double t = beat - mPathStartBeat;
   if (t > duration)
   {
      if (!loopPath)
      {
         mPlayingPath = false;
         return;
      }
      t = std::fmod(t, duration);
   }

   // walk to the segment containing t and interpolate
   for (size_t i = 1; i < mPath.size(); i++)
   {
      if (mPath[i].beat >= t)
      {
         const PadPoint& a = mPath[i - 1];
         const PadPoint& b = mPath[i];
         const double span = std::max(1e-6, b.beat - a.beat);
         const float f = (float)((t - a.beat) / span);
         padX = a.x + (b.x - a.x) * f;
         padY = a.y + (b.y - a.y) * f;
         return;
      }
   }
}

void ResynthNode::RunGeneration(unsigned int srcTex, int w, int h)
{
   const int back = 1 - mFront;
   if (!GLUtil::EnsureFbo(mBuffers[back], w, h))
      return;

   const bool first = mNeedsReset;
   const unsigned int prevTex = GLUtil::FboTexture(mBuffers[mFront]);

   // Bilinear blend of the four corners into per-effect amounts. An effect only
   // contributes where its corner has weight, so the pad reads as a real map:
   // bottom-left is corner 0, bottom-right 1, top-left 2, top-right 3.
   float blended[kEffectCount] = { 0, 0, 0, 0, 0, 0, 0, 0 };
   const float wx = std::min(1.0f, std::max(0.0f, padX));
   const float wy = std::min(1.0f, std::max(0.0f, padY));
   const float cornerWeight[kCorners] = {
      (1.0f - wx) * (1.0f - wy),
      wx * (1.0f - wy),
      (1.0f - wx) * wy,
      wx * wy
   };
   for (int c = 0; c < kCorners; c++)
   {
      const int e = std::max(0, std::min(cornerEffect[c], kEffectCount - 1));
      blended[e] += cornerWeight[c] * cornerAmount[c];
   }
   for (int i = 0; i < kEffectCount; i++)
      blended[i] = std::min(1.0f, blended[i]);

   GLUtil::RunShaderPass(mBuffers[back], mProgram, [&]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, prevTex != 0 ? prevTex : srcTex);
      glUniform1i(glGetUniformLocation(mProgram, "uPrev"), 0);

      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, srcTex);
      glUniform1i(glGetUniformLocation(mProgram, "uSrc"), 1);

      glUniform2f(glGetUniformLocation(mProgram, "uTexelSize"), 1.0f / w, 1.0f / h);
      glUniform1i(glGetUniformLocation(mProgram, "uMode"), mode);
      glUniform1i(glGetUniformLocation(mProgram, "uFirst"), first ? 1 : 0);
      glUniform1f(glGetUniformLocation(mProgram, "uChaos"), chaos);
      glUniform1f(glGetUniformLocation(mProgram, "uMutation"), mutation);
      glUniform1f(glGetUniformLocation(mProgram, "uFeedback"), feedback);
      glUniform1f(glGetUniformLocation(mProgram, "uSourcePull"), sourcePull);
      glUniform1f(glGetUniformLocation(mProgram, "uGeneration"), (float)mGeneration);
      glUniform1f(glGetUniformLocation(mProgram, "uSeed"), seed);
      glUniform1fv(glGetUniformLocation(mProgram, "uAmt"), kEffectCount, blended);
   });

   mFront = back;
   if (first)
   {
      mNeedsReset = false;
      mGeneration = 0;
   }
   else
   {
      mGeneration++;
   }
}

void ResynthNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   unsigned int srcTex = mInput.Pull(frameId);
   if (srcTex == 0)
      return;
   if (!EnsureShader())
      return;

   const int w = std::max(1, mInput.Width());
   const int h = std::max(1, mInput.Height());

   // resizing invalidates the accumulated generations
   if (mBuffers[mFront].w != w || mBuffers[mFront].h != h)
   {
      GLUtil::EnsureFbo(mBuffers[0], w, h);
      GLUtil::EnsureFbo(mBuffers[1], w, h);
      mNeedsReset = true;
   }

   UpdatePathPlayback();

   if (mNeedsReset)
      RunGeneration(srcTex, w, h);

   if (autoIterate)
   {
      const double beat = Transport::Instance().Beats();
      const double interval = 1.0 / std::max(0.01f, stepsPerBeat);
      if (beat < mLastStepBeat)
         mLastStepBeat = beat; // transport rewound
      while (beat - mLastStepBeat >= interval)
      {
         mPendingSteps++;
         mLastStepBeat += interval;
      }
   }

   // cap per-frame work so a long pause can't stall the UI catching up
   int steps = std::min(mPendingSteps, 8);
   mPendingSteps = 0;
   for (int i = 0; i < steps; i++)
      RunGeneration(srcTex, w, h);
}
