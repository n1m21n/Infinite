#include "FeedbackNodes.h"

#include <OpenGL/gl3.h>
#include <algorithm>
#include <cmath>

namespace
{
   const char* kCopyFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "void main() { fragColor = texture(uSrc, vUv); }\n";
}

// ============================================================== Feedback

FeedbackNode::~FeedbackNode()
{
   GLUtil::DestroyFbo(mBuffers[0]);
   GLUtil::DestroyFbo(mBuffers[1]);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

bool FeedbackNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;
   mProgram = GLUtil::CompileProgram(kCopyFrag);
   return mProgram != 0;
}

void FeedbackNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId; // set before pulling, so a cycle through this node stops here

   unsigned int srcTex = mInput.Pull(frameId);
   if (srcTex == 0)
      return;
   if (!EnsureShader())
      return;

   const int w = std::max(1, mInput.Width());
   const int h = std::max(1, mInput.Height());
   if (!GLUtil::EnsureFbo(mBuffers[mWrite], w, h))
      return;
   GLUtil::EnsureFbo(mBuffers[1 - mWrite], w, h);

   // Capture this frame into the write buffer; readers keep seeing the other
   // one until the swap below, which is what produces the one-frame delay.
   GLUtil::RunShaderPass(mBuffers[mWrite], mProgram, [this, srcTex]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, srcTex);
      glUniform1i(glGetUniformLocation(mProgram, "uSrc"), 0);
   });

   mWrite = 1 - mWrite;
}

// ================================================================ Trails

namespace
{
   const char* kTrailsFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "uniform sampler2D uPrev;\n"
      "uniform float uDecay;\n"
      "uniform float uZoom;\n"
      "uniform float uRotate;\n"
      "uniform vec2 uDrift;\n"
      "uniform float uHueShift;\n"
      "uniform int uBlend;\n"
      "uniform int uClear;\n"
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
      "void main() {\n"
      "   vec4 src = texture(uSrc, vUv);\n"
      "   if (uClear == 1) { fragColor = src; return; }\n"
      "   vec2 uv = vUv - 0.5;\n"
      "   float s = sin(uRotate), c = cos(uRotate);\n"
      "   uv = vec2(c*uv.x - s*uv.y, s*uv.x + c*uv.y);\n"
      "   uv /= max(uZoom, 0.01);\n"
      "   uv += 0.5 + uDrift;\n"
      "   vec4 prev = texture(uPrev, clamp(uv, 0.0, 1.0)) * uDecay;\n"
      "   if (uHueShift != 0.0) {\n"
      "      vec3 hsv = rgb2hsv(prev.rgb);\n"
      "      hsv.x = fract(hsv.x + uHueShift);\n"
      "      prev.rgb = hsv2rgb(hsv);\n"
      "   }\n"
      "   vec3 outCol;\n"
      "   if (uBlend == 1) outCol = clamp(src.rgb + prev.rgb, 0.0, 1.0);\n"
      "   else if (uBlend == 2) outCol = 1.0 - (1.0 - src.rgb) * (1.0 - prev.rgb);\n"
      "   else outCol = max(src.rgb, prev.rgb);\n"
      "   fragColor = vec4(outCol, max(src.a, prev.a));\n"
      "}\n";
}

TrailsNode::~TrailsNode()
{
   GLUtil::DestroyFbo(mBuffers[0]);
   GLUtil::DestroyFbo(mBuffers[1]);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

bool TrailsNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;
   mProgram = GLUtil::CompileProgram(kTrailsFrag);
   return mProgram != 0;
}

void TrailsNode::CookIfNeeded(int frameId)
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
   const int back = 1 - mFront;
   if (mBuffers[mFront].w != w || mBuffers[mFront].h != h)
      mNeedsClear = true;
   if (!GLUtil::EnsureFbo(mBuffers[back], w, h))
      return;
   GLUtil::EnsureFbo(mBuffers[mFront], w, h);

   const unsigned int prevTex = GLUtil::FboTexture(mBuffers[mFront]);
   const bool clearNow = mNeedsClear;

   GLUtil::RunShaderPass(mBuffers[back], mProgram, [&]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, srcTex);
      glUniform1i(glGetUniformLocation(mProgram, "uSrc"), 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, prevTex != 0 ? prevTex : srcTex);
      glUniform1i(glGetUniformLocation(mProgram, "uPrev"), 1);
      glUniform1f(glGetUniformLocation(mProgram, "uDecay"), decay);
      glUniform1f(glGetUniformLocation(mProgram, "uZoom"), zoom);
      glUniform1f(glGetUniformLocation(mProgram, "uRotate"), rotate * (float)M_PI / 180.0f);
      glUniform2f(glGetUniformLocation(mProgram, "uDrift"), driftX, driftY);
      glUniform1f(glGetUniformLocation(mProgram, "uHueShift"), hueShift);
      glUniform1i(glGetUniformLocation(mProgram, "uBlend"), blendMode);
      glUniform1i(glGetUniformLocation(mProgram, "uClear"), clearNow ? 1 : 0);
   });

   mFront = back;
   mNeedsClear = false;
}

// ==================================================== Reaction-Diffusion

namespace
{
   const std::vector<std::string> kRdPresets = {
      "Coral", "Mitosis", "Maze", "Spots", "Worms", "Holes"
   };

   const char* kRdSeedFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform float uSeed;\n"
      "float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7)) + uSeed) * 43758.5453); }\n"
      "void main() {\n"
      "   // A is saturated everywhere, B seeded in a few random blobs\n"
      "   float b = 0.0;\n"
      "   for (int i = 0; i < 12; i++) {\n"
      "      vec2 c = vec2(hash(vec2(float(i), 1.0)), hash(vec2(float(i), 2.0)));\n"
      "      if (length(vUv - c) < 0.03) b = 1.0;\n"
      "   }\n"
      "   fragColor = vec4(1.0, b, 0.0, 1.0);\n"
      "}\n";

   const char* kRdSimFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uState;\n"
      "uniform sampler2D uSrc;\n"
      "uniform int uHasSrc;\n"
      "uniform vec2 uTexel;\n"
      "uniform float uFeed;\n"
      "uniform float uKill;\n"
      "uniform float uDa;\n"
      "uniform float uDb;\n"
      "uniform float uSourceInfluence;\n"
      "void main() {\n"
      "   vec2 c = texture(uState, vUv).rg;\n"
      "   // 9-point laplacian, the standard Gray-Scott stencil\n"
      "   vec2 lap = vec2(0.0);\n"
      "   lap += texture(uState, vUv + vec2(-uTexel.x, 0.0)).rg * 0.2;\n"
      "   lap += texture(uState, vUv + vec2( uTexel.x, 0.0)).rg * 0.2;\n"
      "   lap += texture(uState, vUv + vec2(0.0, -uTexel.y)).rg * 0.2;\n"
      "   lap += texture(uState, vUv + vec2(0.0,  uTexel.y)).rg * 0.2;\n"
      "   lap += texture(uState, vUv + vec2(-uTexel.x, -uTexel.y)).rg * 0.05;\n"
      "   lap += texture(uState, vUv + vec2( uTexel.x, -uTexel.y)).rg * 0.05;\n"
      "   lap += texture(uState, vUv + vec2(-uTexel.x,  uTexel.y)).rg * 0.05;\n"
      "   lap += texture(uState, vUv + vec2( uTexel.x,  uTexel.y)).rg * 0.05;\n"
      "   lap -= c;\n"
      "\n"
      "   float feed = uFeed;\n"
      "   if (uHasSrc == 1 && uSourceInfluence > 0.0) {\n"
      "      float lum = dot(texture(uSrc, vUv).rgb, vec3(0.299, 0.587, 0.114));\n"
      "      feed += (lum - 0.5) * uSourceInfluence * 0.04;\n"
      "   }\n"
      "\n"
      "   float reaction = c.r * c.g * c.g;\n"
      "   float a = c.r + (uDa * lap.r - reaction + feed * (1.0 - c.r));\n"
      "   float b = c.g + (uDb * lap.g + reaction - (uKill + feed) * c.g);\n"
      "   fragColor = vec4(clamp(a, 0.0, 1.0), clamp(b, 0.0, 1.0), 0.0, 1.0);\n"
      "}\n";

   const char* kRdDrawFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uState;\n"
      "uniform vec3 uLow;\n"
      "uniform vec3 uHigh;\n"
      "void main() {\n"
      "   vec2 c = texture(uState, vUv).rg;\n"
      "   float v = clamp(c.r - c.g, 0.0, 1.0);\n"
      "   fragColor = vec4(mix(uHigh, uLow, v), 1.0);\n"
      "}\n";
}

const std::vector<std::string>& ReactionDiffusionNode::PresetNames()
{
   return kRdPresets;
}

ReactionDiffusionNode::~ReactionDiffusionNode()
{
   GLUtil::DestroyFbo(mState[0]);
   GLUtil::DestroyFbo(mState[1]);
   GLUtil::DestroyFbo(mDisplay);
   if (mSimProgram != 0)
      glDeleteProgram(mSimProgram);
   if (mSeedProgram != 0)
      glDeleteProgram(mSeedProgram);
   if (mDrawProgram != 0)
      glDeleteProgram(mDrawProgram);
}

void ReactionDiffusionNode::ApplyPreset(int index)
{
   // classic Gray-Scott feed/kill pairs
   switch (index)
   {
      case 0: feed = 0.0545f; kill = 0.0620f; break; // Coral
      case 1: feed = 0.0367f; kill = 0.0649f; break; // Mitosis
      case 2: feed = 0.0290f; kill = 0.0570f; break; // Maze
      case 3: feed = 0.0350f; kill = 0.0650f; break; // Spots
      case 4: feed = 0.0580f; kill = 0.0650f; break; // Worms
      default: feed = 0.0390f; kill = 0.0580f; break; // Holes
   }
   preset = index;
   mNeedsSeed = true;
}

bool ReactionDiffusionNode::EnsureShaders()
{
   if (mShaderTried)
      return mSimProgram != 0 && mSeedProgram != 0 && mDrawProgram != 0;
   mShaderTried = true;
   mSeedProgram = GLUtil::CompileProgram(kRdSeedFrag);
   mSimProgram = GLUtil::CompileProgram(kRdSimFrag);
   mDrawProgram = GLUtil::CompileProgram(kRdDrawFrag);
   return mSimProgram != 0 && mSeedProgram != 0 && mDrawProgram != 0;
}

void ReactionDiffusionNode::Seed(int w, int h)
{
   GLUtil::EnsureFbo(mState[0], w, h);
   GLUtil::EnsureFbo(mState[1], w, h);
   static float sSeedCounter = 0.0f;
   sSeedCounter += 7.13f;
   const float seedValue = sSeedCounter;
   GLUtil::RunShaderPass(mState[mFront], mSeedProgram, [this, seedValue]()
   {
      glUniform1f(glGetUniformLocation(mSeedProgram, "uSeed"), seedValue);
   });
   mNeedsSeed = false;
}

void ReactionDiffusionNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (!EnsureShaders())
      return;

   unsigned int srcTex = mInput.IsConnected() ? mInput.Pull(frameId) : 0;

   int w, h;
   if (srcTex != 0)
   {
      w = std::max(8, mInput.Width());
      h = std::max(8, mInput.Height());
   }
   else
   {
      w = std::max(8, (int)width);
      h = std::max(8, (int)height);
   }

   if (mState[mFront].w != w || mState[mFront].h != h)
      mNeedsSeed = true;
   if (!GLUtil::EnsureFbo(mState[0], w, h))
      return;
   GLUtil::EnsureFbo(mState[1], w, h);
   GLUtil::EnsureFbo(mDisplay, w, h);

   if (mNeedsSeed)
      Seed(w, h);

   const int steps = std::max(1, std::min(32, (int)stepsPerFrame));
   for (int i = 0; i < steps; i++)
   {
      const int back = 1 - mFront;
      const unsigned int stateTex = GLUtil::FboTexture(mState[mFront]);
      GLUtil::RunShaderPass(mState[back], mSimProgram, [&]()
      {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, stateTex);
         glUniform1i(glGetUniformLocation(mSimProgram, "uState"), 0);
         glActiveTexture(GL_TEXTURE1);
         glBindTexture(GL_TEXTURE_2D, srcTex != 0 ? srcTex : stateTex);
         glUniform1i(glGetUniformLocation(mSimProgram, "uSrc"), 1);
         glUniform1i(glGetUniformLocation(mSimProgram, "uHasSrc"), srcTex != 0 ? 1 : 0);
         glUniform2f(glGetUniformLocation(mSimProgram, "uTexel"), 1.0f / w, 1.0f / h);
         glUniform1f(glGetUniformLocation(mSimProgram, "uFeed"), feed);
         glUniform1f(glGetUniformLocation(mSimProgram, "uKill"), kill);
         glUniform1f(glGetUniformLocation(mSimProgram, "uDa"), diffuseA);
         glUniform1f(glGetUniformLocation(mSimProgram, "uDb"), diffuseB);
         glUniform1f(glGetUniformLocation(mSimProgram, "uSourceInfluence"), sourceInfluence);
      });
      mFront = back;
   }

   const unsigned int stateTex = GLUtil::FboTexture(mState[mFront]);
   GLUtil::RunShaderPass(mDisplay, mDrawProgram, [this, stateTex]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, stateTex);
      glUniform1i(glGetUniformLocation(mDrawProgram, "uState"), 0);
      glUniform3f(glGetUniformLocation(mDrawProgram, "uLow"), lowColor[0], lowColor[1], lowColor[2]);
      glUniform3f(glGetUniformLocation(mDrawProgram, "uHigh"), highColor[0], highColor[1], highColor[2]);
   });
}
