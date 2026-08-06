#include "ResynthNode.h"

#include <OpenGL/gl3.h>
#include <algorithm>
#include <cmath>

#include "Transport.h"

namespace
{
   const std::vector<std::string> kModeNames = {
      "Drift", "Quilt", "Cellular", "Spectral", "Shatter"
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
      "uniform float uW[8];\n"
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
      "   return mix(mix(hash(i), hash(i + vec2(1,0)), u.x),\n"
      "              mix(hash(i + vec2(0,1)), hash(i + vec2(1,1)), u.x), u.y);\n"
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
      "   // --- displacement: how the sampling grid is perturbed each generation\n"
      "   if (uMode == 0) {\n"
      "      vec2 flow = vec2(vnoise(uv * (2.0 + uW[0] * 14.0) + g * 0.07),\n"
      "                       vnoise(uv * (2.0 + uW[1] * 14.0) - g * 0.05)) - 0.5;\n"
      "      uv += flow * uMutation * uChaos * 0.06;\n"
      "   } else if (uMode == 1) {\n"
      "      // quilt: snap to patch centres and resample a neighbouring patch\n"
      "      float patch = 4.0 + uW[0] * 40.0;\n"
      "      vec2 cell = floor(uv * patch);\n"
      "      vec2 jump = (hash2(cell + floor(g)) - 0.5) * uChaos * 0.25;\n"
      "      uv = (cell + fract(uv * patch)) / patch + jump;\n"
      "   } else if (uMode == 2) {\n"
      "      // cellular: pull each pixel toward its nearest wandering site\n"
      "      float scale = 3.0 + uW[1] * 20.0;\n"
      "      vec2 i = floor(uv * scale), f = fract(uv * scale);\n"
      "      float best = 8.0; vec2 bestOff = vec2(0.0);\n"
      "      for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) {\n"
      "         vec2 gg = vec2(float(x), float(y));\n"
      "         vec2 o = hash2(i + gg + floor(g * 0.5));\n"
      "         float d = length(gg + o - f);\n"
      "         if (d < best) { best = d; bestOff = gg + o - f; }\n"
      "      }\n"
      "      uv += bestOff / scale * uChaos * 0.5;\n"
      "   } else if (uMode == 3) {\n"
      "      // spectral: radial ring-shaped resampling, structure vs residual\n"
      "      vec2 d = uv - 0.5;\n"
      "      float r = length(d);\n"
      "      float ring = sin(r * (10.0 + uW[2] * 90.0) - g * 0.4);\n"
      "      uv += normalize(d + 1e-5) * ring * uChaos * 0.02;\n"
      "   } else {\n"
      "      // shatter: hard blocky offsets, most violent of the five\n"
      "      float blocks = 6.0 + uW[3] * 40.0;\n"
      "      vec2 cell = floor(uv * blocks);\n"
      "      float r = hash(cell + floor(g));\n"
      "      if (r > 1.0 - uChaos * 0.6)\n"
      "         uv += (hash2(cell + 3.0) - 0.5) * 0.2 * uChaos;\n"
      "   }\n"
      "\n"
      "   uv = clamp(uv, 0.0, 1.0);\n"
      "   vec4 prev = texture(uPrev, uv);\n"
      "\n"
      "   // --- colour mutation, weighted by the pad\n"
      "   vec3 col = prev.rgb;\n"
      "   vec3 hsv = rgb2hsv(col);\n"
      "   hsv.x = fract(hsv.x + (uW[4] - 0.5) * uMutation * 0.08);\n"
      "   hsv.y = clamp(hsv.y * (1.0 + (uW[5] - 0.5) * uMutation * 0.6), 0.0, 1.0);\n"
      "   col = hsv2rgb(hsv);\n"
      "\n"
      "   // posterise toward a shrinking palette as W6 rises\n"
      "   float levels = mix(255.0, 3.0, uW[6] * uMutation);\n"
      "   col = floor(col * levels + 0.5) / levels;\n"
      "\n"
      "   // grain proportional to chaos\n"
      "   float n = (hash(vUv * 997.0 + g) - 0.5) * uChaos * uW[7] * 0.25;\n"
      "   col += n;\n"
      "\n"
      "   // --- recombine: keep some of the previous generation, pull back toward\n"
      "   //     the source so it never fully disintegrates\n"
      "   vec3 outCol = mix(prev.rgb, col, clamp(uMutation, 0.0, 1.0));\n"
      "   outCol = mix(outCol, prev.rgb, 1.0 - clamp(uFeedback, 0.0, 1.0));\n"
      "   outCol = mix(outCol, src.rgb, clamp(uSourcePull, 0.0, 1.0));\n"
      "\n"
      "   fragColor = vec4(clamp(outCol, 0.0, 1.0), max(prev.a, src.a));\n"
      "}\n";
}

const std::vector<std::string>& ResynthNode::ModeNames()
{
   return kModeNames;
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
   // Deterministic re-roll from the seed so a given seed always gives the same
   // pad behaviour; nudging the seed is how you audition variations.
   seed = std::fmod(seed * 1.618f + 7.31f, 100.0f);
   for (int i = 0; i < 8; i++)
   {
      const float x = std::sin((seed + 1.0f) * (float)(i + 1) * 12.9898f) * 43758.5453f;
      mWeights[i] = x - std::floor(x);
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

   // Pad position cross-fades the eight rolled weights: X drives the first four,
   // Y the second four, so moving the orb sweeps between mutation characters.
   float blended[8];
   for (int i = 0; i < 8; i++)
   {
      const float axis = (i < 4) ? padX : padY;
      blended[i] = mWeights[i] * axis + (1.0f - axis) * (1.0f - mWeights[i]);
   }

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
      glUniform1fv(glGetUniformLocation(mProgram, "uW"), 8, blended);
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
