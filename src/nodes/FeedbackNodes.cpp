#include "FeedbackNodes.h"

#include "platform/OpenGLHeaders.h"
#include <algorithm>
#include <cmath>

#include "Transport.h"

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

void FeedbackNode::Clear()
{
   GLUtil::DestroyFbo(mBuffers[0]);
   GLUtil::DestroyFbo(mBuffers[1]);
   mWrite = 0;
   mLastCookFrame = -1;
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
   if (!GLUtil::EnsureFbo(mBuffers[mWrite], w, h, GL_RGBA16F))
      return;
   GLUtil::EnsureFbo(mBuffers[1 - mWrite], w, h, GL_RGBA16F);

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
   if (!GLUtil::EnsureFbo(mBuffers[back], w, h, GL_RGBA16F))
      return;
   GLUtil::EnsureFbo(mBuffers[mFront], w, h, GL_RGBA16F);

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
   GLUtil::EnsureFbo(mState[0], w, h, GL_RGBA16F);
   GLUtil::EnsureFbo(mState[1], w, h, GL_RGBA16F);
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
   if (!GLUtil::EnsureFbo(mState[0], w, h, GL_RGBA16F))
      return;
   GLUtil::EnsureFbo(mState[1], w, h, GL_RGBA16F);
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

// ================================================================ Datamosh

namespace
{
   const char* kDmHash =
      "uint pcg(uint v) {\n"
      "   uint s = v * 747796405u + 2891336453u;\n"
      "   uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;\n"
      "   return (w >> 22u) ^ w;\n"
      "}\n"
      "uint hash2(uint a, uint b) { return pcg(a + pcg(b)); }\n"
      "uint hash3(uint a, uint b, uint c) { return pcg(a + pcg(b + pcg(c))); }\n"
      "float h01(uint h) { return float(h >> 8u) * (1.0 / 16777216.0); }\n";

   // Block motion estimation, one fragment per 16 px block. SAD over a 4x4 sample
   // grid inside the block, +-12 px search in 2 px steps, biased toward zero.
   // Output = accumulated vector (state * bloom + new vector), in uv units.
   const char* kDmMeFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uCur;\n"
      "uniform sampler2D uPrevIn;\n"
      "uniform sampler2D uState;\n"
      "uniform vec2 uTexel;\n"     // texel size of the motion source
      "uniform float uBloom;\n"
      "uniform float uThreshold;\n" // pixels
      "uniform int uReset;\n"
      "float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }\n"
      "void main() {\n"
      "   vec2 center = vUv;\n"
      "   float cur[16];\n"
      "   for (int i = 0; i < 16; i++) {\n"
      "      vec2 o = (vec2(float(i & 3), float(i >> 2)) - 1.5) * 4.0;\n"
      "      cur[i] = luma(texture(uCur, center + o * uTexel).rgb);\n"
      "   }\n"
      "   float best = 1e9; vec2 bestD = vec2(0.0);\n"
      "   for (int j = -6; j <= 6; j++) {\n"
      "      for (int k = -6; k <= 6; k++) {\n"
      "         vec2 d = vec2(float(k), float(j)) * 2.0;\n"
      "         float sad = 0.0;\n"
      "         for (int i = 0; i < 16; i++) {\n"
      "            vec2 o = (vec2(float(i & 3), float(i >> 2)) - 1.5) * 4.0;\n"
      "            sad += abs(cur[i] - luma(texture(uPrevIn, center + (o + d) * uTexel).rgb));\n"
      "         }\n"
      "         sad += 0.01 * length(d);\n"
      "         if (sad < best) { best = sad; bestD = d; }\n"
      "      }\n"
      "   }\n"
      "   if (length(bestD) < uThreshold) bestD = vec2(0.0);\n"
      "   vec2 mv = bestD * uTexel;\n"
      "   vec2 st = (uReset == 1) ? vec2(0.0) : texture(uState, vUv).rg;\n"
      "   fragColor = vec4(clamp(st * uBloom + mv, vec2(-0.5), vec2(0.5)), 0.0, 1.0);\n"
      "}\n";

   // Mosh pass. Block layout is static (depends on the seed only): a uniform grid,
   // or a weighted random split when Block Var > 0. A held block samples the
   // previous output displaced by its vector; the rest show the live image.
   const char* kDmMoshFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "uniform sampler2D uPrevOut;\n"
      "uniform sampler2D uMv;\n"
      "uniform vec2 uRes;\n"
      "uniform float uMosh;\n"
      "uniform float uGain;\n"
      "uniform float uBlock;\n"
      "uniform float uBlockVar;\n"
      "uniform float uLeak;\n"
      "uniform float uChance;\n"
      "uniform int uRefresh;\n"
      "uniform int uInit;\n"
      "uniform int uSeedI;\n"
      "uniform int uEpochI;\n"
      "%HASH%"
      "void main() {\n"
      "   vec4 cur = texture(uSrc, vUv);\n"
      "   if (uInit == 1) { fragColor = cur; return; }\n"
      "   uint seedU = uint(uSeedI);\n"
      "   vec2 px = vUv * uRes;\n"
      "   uint id; vec2 ctr;\n"
      "   if (uBlockVar <= 0.0) {\n"
      "      vec2 bi = floor(px / uBlock);\n"
      "      id = hash2(uint(bi.x), uint(bi.y) + 4096u);\n"
      "      ctr = (bi + 0.5) * uBlock / uRes;\n"
      "   } else {\n"
      "      vec4 r = vec4(0.0, 0.0, 1.0, 1.0);\n"
      "      id = seedU;\n"
      "      for (int i = 0; i < 14; i++) {\n"
      "         vec2 sz = (r.zw - r.xy) * uRes;\n"
      "         bool sx = sz.x >= sz.y;\n"
      "         float len = sx ? sz.x : sz.y;\n"
      "         if (len < 2.0 * uBlock) break;\n"
      "         uint h = pcg(id + 0x9e3779b9u);\n"
      "         if (i > 1 && h01(pcg(h)) < 0.25 * uBlockVar) break;\n"
      "         float w = mix(0.5, 0.12 + 0.76 * h01(h), uBlockVar);\n"
      "         if (sx) {\n"
      "            float m = r.x + (r.z - r.x) * w;\n"
      "            if (vUv.x < m) { r.z = m; id = pcg(id * 2u + 1u); } else { r.x = m; id = pcg(id * 2u + 2u); }\n"
      "         } else {\n"
      "            float m = r.y + (r.w - r.y) * w;\n"
      "            if (vUv.y < m) { r.w = m; id = pcg(id * 2u + 1u); } else { r.y = m; id = pcg(id * 2u + 2u); }\n"
      "         }\n"
      "      }\n"
      "      ctr = (r.xy + r.zw) * 0.5;\n"
      "   }\n"
      "   bool refreshed = uRefresh == 1 && h01(hash3(id, uint(uEpochI), seedU + 202u)) < uChance;\n"
      "   bool held = !refreshed && h01(hash2(id, seedU + 101u)) < uMosh;\n"
      "   vec2 mv = texture(uMv, ctr).rg * uGain;\n"
      "   vec4 moshed = texture(uPrevOut, clamp(vUv + mv, 0.0, 1.0));\n"
      "   if (refreshed) fragColor = cur;\n"
      "   else if (held) fragColor = moshed;\n"
      "   else fragColor = mix(cur, moshed, uLeak);\n"
      "}\n";

   const char* kDmCopyFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "void main() { fragColor = texture(uSrc, vUv); }\n";
}

const std::vector<std::string>& DatamoshNode::BlockNames()
{
   static const std::vector<std::string> n = { "8", "16", "32" };
   return n;
}

const std::vector<std::string>& DatamoshNode::RefreshNames()
{
   static const std::vector<std::string> n = { "Off", "1 bar", "1 beat", "1/2 beat", "1/4 beat", "1/8 beat" };
   return n;
}

DatamoshNode::~DatamoshNode()
{
   GLUtil::DestroyFbo(mOut[0]);
   GLUtil::DestroyFbo(mOut[1]);
   GLUtil::DestroyFbo(mMv[0]);
   GLUtil::DestroyFbo(mMv[1]);
   GLUtil::DestroyFbo(mPrevIn);
   if (mMeProgram != 0) glDeleteProgram(mMeProgram);
   if (mMoshProgram != 0) glDeleteProgram(mMoshProgram);
   if (mCopyProgram != 0) glDeleteProgram(mCopyProgram);
}

bool DatamoshNode::EnsureShaders()
{
   if (mShaderTried)
      return mMeProgram != 0 && mMoshProgram != 0 && mCopyProgram != 0;
   mShaderTried = true;
   std::string mosh = kDmMoshFrag;
   const size_t at = mosh.find("%HASH%");
   mosh.replace(at, 6, kDmHash);
   mMeProgram = GLUtil::CompileProgram(kDmMeFrag);
   mMoshProgram = GLUtil::CompileProgram(mosh.c_str());
   mCopyProgram = GLUtil::CompileProgram(kDmCopyFrag);
   return mMeProgram != 0 && mMoshProgram != 0 && mCopyProgram != 0;
}

void DatamoshNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   unsigned int srcTex = mInput.Pull(frameId);
   if (srcTex == 0)
      return;
   unsigned int motionTex = mMotion.Pull(frameId);
   if (motionTex == 0)
      motionTex = srcTex;
   const int motionW = (mMotion.GetSource() != nullptr && mMotion.Width() > 0) ? mMotion.Width() : mInput.Width();
   const int motionH = (mMotion.GetSource() != nullptr && mMotion.Height() > 0) ? mMotion.Height() : mInput.Height();
   if (!EnsureShaders())
      return;

   const int w = std::max(1, mInput.Width());
   const int h = std::max(1, mInput.Height());
   const int mw = std::max(1, (w + 15) / 16);
   const int mh = std::max(1, (h + 15) / 16);

   // Resize: reallocating drops the history, so restart from the live image.
   if (mOut[0].w != w || mOut[0].h != h || mMv[0].w != mw || mMv[0].h != mh)
      mNeedsInit = true;
   if (mPrevIn.w != motionW || mPrevIn.h != motionH)
      mHavePrevIn = false; // reallocated below, contents undefined
   if (!GLUtil::EnsureFbo(mOut[0], w, h, GL_RGBA16F) || !GLUtil::EnsureFbo(mOut[1], w, h, GL_RGBA16F) ||
       !GLUtil::EnsureFbo(mMv[0], mw, mh, GL_RG16F) || !GLUtil::EnsureFbo(mMv[1], mw, mh, GL_RG16F) ||
       !GLUtil::EnsureFbo(mPrevIn, std::max(1, motionW), std::max(1, motionH), GL_RGBA8))
      return;

   // Refresh ticks: tempo grid (transport beats) or the trigger. Both reset a
   // random share (Refresh Chance) of blocks to the live image on this frame.
   bool refreshNow = mRefreshPending;
   mRefreshPending = false;
   if (refresh > 0)
   {
      static const double kBeats[6] = { 0.0, 0.0, 1.0, 0.5, 0.25, 0.125 };
      const Transport& tp = Transport::Instance();
      const double period = (refresh == 1) ? tp.BeatsPerBar() : kBeats[refresh];
      const long long tick = (long long)std::floor(tp.Beats() / std::max(period, 1e-6));
      if (tick != mLastTick)
      {
         refreshNow = refreshNow || mLastTick >= 0;
         mLastTick = tick;
      }
   }
   else
      mLastTick = -1;
   if (refreshNow)
      mEpoch++;

   const bool init = mNeedsInit;
   const int blockPx = (block == 0) ? 8 : (block == 2 ? 32 : 16);
   const int seedI = (int)(seed * 131.0f) + 12345;

   // 1. Block motion estimation (needs the previous motion-source frame).
   const int mvBack = 1 - mMvFront;
   const bool noPrev = init || !mHavePrevIn;
   GLUtil::RunShaderPass(mMv[mvBack], mMeProgram, [&]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, motionTex);
      glUniform1i(glGetUniformLocation(mMeProgram, "uCur"), 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, noPrev ? motionTex : GLUtil::FboTexture(mPrevIn));
      glUniform1i(glGetUniformLocation(mMeProgram, "uPrevIn"), 1);
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, GLUtil::FboTexture(mMv[mMvFront]));
      glUniform1i(glGetUniformLocation(mMeProgram, "uState"), 2);
      glUniform2f(glGetUniformLocation(mMeProgram, "uTexel"), 1.0f / motionW, 1.0f / motionH);
      glUniform1f(glGetUniformLocation(mMeProgram, "uBloom"), std::clamp(bloom, 0.0f, 0.98f));
      glUniform1f(glGetUniformLocation(mMeProgram, "uThreshold"), threshold * 8.0f);
      glUniform1i(glGetUniformLocation(mMeProgram, "uReset"), init ? 1 : 0);
      glActiveTexture(GL_TEXTURE0);
   });
   mMvFront = mvBack;

   // 2. Mosh pass: previous output + vectors + live image -> new output.
   const int back = 1 - mFront;
   GLUtil::RunShaderPass(mOut[back], mMoshProgram, [&]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, srcTex);
      glUniform1i(glGetUniformLocation(mMoshProgram, "uSrc"), 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, GLUtil::FboTexture(mOut[mFront]));
      glUniform1i(glGetUniformLocation(mMoshProgram, "uPrevOut"), 1);
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, GLUtil::FboTexture(mMv[mMvFront]));
      glUniform1i(glGetUniformLocation(mMoshProgram, "uMv"), 2);
      glUniform2f(glGetUniformLocation(mMoshProgram, "uRes"), (float)w, (float)h);
      glUniform1f(glGetUniformLocation(mMoshProgram, "uMosh"), mosh);
      glUniform1f(glGetUniformLocation(mMoshProgram, "uGain"), gain);
      glUniform1f(glGetUniformLocation(mMoshProgram, "uBlock"), (float)blockPx);
      glUniform1f(glGetUniformLocation(mMoshProgram, "uBlockVar"), blockVar);
      glUniform1f(glGetUniformLocation(mMoshProgram, "uLeak"), leak);
      glUniform1f(glGetUniformLocation(mMoshProgram, "uChance"), refreshChance);
      glUniform1i(glGetUniformLocation(mMoshProgram, "uRefresh"), refreshNow ? 1 : 0);
      glUniform1i(glGetUniformLocation(mMoshProgram, "uInit"), init ? 1 : 0);
      glUniform1i(glGetUniformLocation(mMoshProgram, "uSeedI"), seedI);
      glUniform1i(glGetUniformLocation(mMoshProgram, "uEpochI"), (int)(mEpoch & 0x7fffffffu));
      glActiveTexture(GL_TEXTURE0);
   });
   mFront = back;

   // 3. Keep this frame of the motion source for the next estimation.
   GLUtil::RunShaderPass(mPrevIn, mCopyProgram, [&]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, motionTex);
      glUniform1i(glGetUniformLocation(mCopyProgram, "uSrc"), 0);
   });
   mHavePrevIn = true;
   mNeedsInit = false;
}
