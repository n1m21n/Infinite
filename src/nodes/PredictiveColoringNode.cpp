#include "PredictiveColoringNode.h"
#include "platform/AppPaths.h"

#include "Transport.h"
#include "gl3.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>

namespace
{
   const char* kDownsampleFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "void main() { fragColor = texture(uSrc, vUv); }\n";

   const char* kColorGradeFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "uniform float uMix;\n"
      "uniform vec3  uRgbGain;\n"
      "uniform float uExposure;\n"
      "uniform float uBlackPoint;\n"
      "uniform float uWhitePoint;\n"
      "uniform float uContrast;\n"
      "uniform float uPivot;\n"
      "uniform float uHighlights;\n"
      "uniform float uShadows;\n"
      "uniform float uMidtones;\n"
      "uniform float uWhites;\n"
      "uniform float uBlacks;\n"
      "uniform float uSaturation;\n"
      "uniform float uVibrance;\n"
      "uniform float uHueShift;\n"
      "\n"
      "vec3 rgb2hsv(vec3 c) {\n"
      "    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);\n"
      "    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));\n"
      "    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));\n"
      "    float d = q.x - min(q.w, q.y);\n"
      "    float e = 1.0e-10;\n"
      "    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);\n"
      "}\n"
      "\n"
      "vec3 hsv2rgb(vec3 c) {\n"
      "    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);\n"
      "    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);\n"
      "    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);\n"
      "}\n"
      "\n"
      "void main() {\n"
      "    vec4 inCol = texture(uSrc, vUv);\n"
      "    vec3 c = inCol.rgb;\n"
      "\n"
      "    // 1. Decode sRGB to linear (photographic light domain)\n"
      "    c = pow(max(c, vec3(0.0)), vec3(2.2));\n"
      "\n"
      "    // 2. White balance / Per-channel gain\n"
      "    c = c * uRgbGain;\n"
      "\n"
      "    // 3. Exposure (multiplicative 2^stops)\n"
      "    c = c * exp2(uExposure);\n"
      "\n"
      "    // 4. Black / White Levels point clamp\n"
      "    float span = max(uWhitePoint - uBlackPoint, 0.0001);\n"
      "    c = clamp((c - vec3(uBlackPoint)) / span, vec3(0.0), vec3(1.0));\n"
      "\n"
      "    // 5. Pivot Contrast\n"
      "    c = (c - vec3(uPivot)) * uContrast + vec3(uPivot);\n"
      "    c = clamp(c, vec3(0.0), vec3(1.0));\n"
      "\n"
      "    // 6. Highlights, Shadows, Midtones, Whites, Blacks (Luma-weighted tone zones)\n"
      "    float Y = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
      "    float w_hi = smoothstep(0.5, 1.0, Y);\n"
      "    float w_sh = 1.0 - smoothstep(0.0, 0.5, Y);\n"
      "    float w_mid = max(1.0 - w_hi - w_sh, 0.0);\n"
      "    float w_wh = smoothstep(0.8, 1.0, Y);\n"
      "    float w_bl = 1.0 - smoothstep(0.0, 0.2, Y);\n"
      "\n"
      "    c = c + vec3(uHighlights) * w_hi * (vec3(1.0) - c) + vec3(uShadows) * w_sh * c;\n"
      "    c = c + vec3(uWhites) * w_wh * (vec3(1.0) - c) + vec3(uBlacks) * w_bl * c;\n"
      "\n"
      "    if (abs(uMidtones - 1.0) > 0.001) {\n"
      "        vec3 midAdjust = pow(max(c, vec3(0.0)), vec3(1.0 / max(uMidtones, 0.01)));\n"
      "        c = mix(c, midAdjust, w_mid);\n"
      "    }\n"
      "    c = clamp(c, vec3(0.0), vec3(1.0));\n"
      "\n"
      "    // 7. HSV Saturation, Vibrance, Hue Shift\n"
      "    vec3 hsv = rgb2hsv(c);\n"
      "    hsv.x = fract(hsv.x + uHueShift + 1.0);\n"
      "    hsv.y = clamp(hsv.y * uSaturation, 0.0, 1.0);\n"
      "    hsv.y = clamp(hsv.y + uVibrance * (1.0 - hsv.y) * hsv.y, 0.0, 1.0);\n"
      "    vec3 graded = hsv2rgb(hsv);\n"
      "\n"
      "    // 8. Re-encode linear to sRGB\n"
      "    graded = pow(max(graded, vec3(0.0)), vec3(1.0 / 2.2));\n"
      "\n"
      "    // 9. Output mix blend\n"
      "    fragColor = vec4(mix(inCol.rgb, graded, clamp(uMix, 0.0, 1.0)), inCol.a);\n"
      "}\n";
   static std::string BytesToHex(const std::vector<uint8_t>& bytes)
   {
      static const char hexChars[] = "0123456789abcdef";
      std::string s;
      s.reserve(bytes.size() * 2);
      for (uint8_t b : bytes)
      {
         s.push_back(hexChars[(b >> 4) & 0x0F]);
         s.push_back(hexChars[b & 0x0F]);
      }
      return s;
   }

   static std::vector<uint8_t> HexToBytes(const std::string& hex)
   {
      std::vector<uint8_t> bytes;
      bytes.reserve(hex.size() / 2);
      for (size_t i = 0; i + 1 < hex.size(); i += 2)
      {
         auto hexVal = [](char c) -> int
         {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
         };
         uint8_t b = (uint8_t)((hexVal(hex[i]) << 4) | hexVal(hex[i + 1]));
         bytes.push_back(b);
      }
      return bytes;
   }

   // Same splitmix64 seed-spreading step DriftNode uses (SeedFor, src/nodes/PredictionNodes.cpp) -
   // turns a small user-facing int into a well-mixed 64-bit RNG state, never 0.
   uint64_t SeedColorRng(int seed)
   {
      uint64_t z = (uint64_t)(uint32_t)seed * 0x100000001B3ull ^ 0xC010C010C010C010ull;
      z += 0x9E3779B97F4A7C15ull;
      uint64_t r = z;
      r = (r ^ (r >> 30)) * 0xBF58476D1CE4E5B9ull;
      r = (r ^ (r >> 27)) * 0x94D049BB133111EBull;
      r ^= (r >> 31);
      return r != 0 ? r : 1;
   }

   // Internal OU tuning (step-10 Task A §A1: "theta/sigma stay internal constants tuned by eye,
   // same as Drift's internal timescales aren't exposed either"). ~5s relaxation time - slow
   // enough that a grade reads as "the same look, gently alive," not a flicker.
   constexpr float kWanderTheta = 0.2f;

   // ColorGradeParams' default constructor sets *neutral absolute grade* values (whitePoint=1,
   // contrast=1, midtones=1, saturation=1, the rest 0) - correct for a grade, wrong for an
   // *offset* vector, which must start every field at exactly zero. Every wander-offset value
   // (the OU state itself, and its reset on reseed) goes through this rather than `= {}`/default
   // construction, or the mismatched fields above would add a spurious +1 into the grade.
   ColorStats::ColorGradeParams ZeroOffset()
   {
      ColorStats::ColorGradeParams z;
      z.rgbGainR = z.rgbGainG = z.rgbGainB = 0.0f;
      z.exposure = z.blackPoint = z.whitePoint = 0.0f;
      z.contrast = z.pivot = 0.0f;
      z.highlights = z.shadows = z.midtones = 0.0f;
      z.whites = z.blacks = 0.0f;
      z.saturation = z.vibrance = z.hueShift = 0.0f;
      return z;
   }

   // Per-dimension base sigma at wander=1, spread=1, confidence=0: the widest this dimension may
   // wander from equilibrium. Small on purpose - these stack on top of an already-fitted grade,
   // not a random grade generator. hueShift's is deliberately the tightest: even a little hue
   // drift reads as wrong, where the others read as "alive."
   const ColorStats::ColorGradeParams& WanderBaseSigma()
   {
      static const ColorStats::ColorGradeParams s = []
      {
         ColorStats::ColorGradeParams p;
         p.rgbGainR = p.rgbGainG = p.rgbGainB = 0.05f;
         p.exposure = 0.15f;
         p.blackPoint = 0.02f;
         p.whitePoint = 0.02f;
         p.contrast = 0.06f;
         p.pivot = 0.03f;
         p.highlights = 0.08f;
         p.shadows = 0.08f;
         p.midtones = 0.05f;
         p.whites = 0.05f;
         p.blacks = 0.05f;
         p.saturation = 0.06f;
         p.vibrance = 0.06f;
         p.hueShift = 0.006f;
         return p;
      }();
      return s;
   }
}

PredictiveColoringNode::PredictiveColoringNode()
{
   // Inherit the global cross-session photographic profile by default
   mProfile = ColorStats::Engine::Instance().GlobalProfile();
   mWanderRng = SeedColorRng(seed);
   mSeedApplied = seed;
   mWanderOffset = ZeroOffset();
}

PredictiveColoringNode::~PredictiveColoringNode()
{
   if (mDownsampleTex != 0)
      glDeleteTextures(1, &mDownsampleTex);
   if (mDownsampleFbo != 0)
      glDeleteFramebuffers(1, &mDownsampleFbo);
   if (mDownsampleProgram != 0)
      glDeleteProgram(mDownsampleProgram);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
   GLUtil::DestroyFbo(mOut);
}

void PredictiveColoringNode::SetLearning(bool on)
{
   if (mLearning && !on)
   {
      // Save updated cross-session global profile
      ColorStats::Engine::Instance().Save(AppPaths::AppSupportDir() + "/prediction");
   }
   mLearning = on;
}

void PredictiveColoringNode::ResetProfile()
{
   mProfile = ColorStats::Engine::Instance().GlobalProfile();
   // Zero the wander's actual state, not just mCurrentParams: RecomputeFit/StepWander both run
   // unconditionally on the next CookIfNeeded and would otherwise smooth mFitEquilibrium in from
   // its stale pre-reset value and carry the old mWanderOffset forward, silently undoing this
   // reset within a few frames (invariant-interaction-audit, step-10 Task A).
   mFitEquilibrium = ColorStats::ColorGradeParams();
   mWanderOffset = ZeroOffset(); // NOT ColorGradeParams() - default ctor has non-zero neutral fields
   mCurrentParams = ColorStats::ColorGradeParams();
}

float PredictiveColoringNode::Confidence01() const
{
   return mProfile.Confidence01();
}

uint64_t PredictiveColoringNode::TotalSamples() const
{
   return mProfile.TotalSamples();
}

void PredictiveColoringNode::VisitParams(ParamVisitor& v)
{
   v.Float("mix", mix);
   v.Bool("learning", mLearning);
   v.Bool("selfNormalize", selfNormalize);
   v.Float("wander", wander);
   v.Int("seed", seed);

   std::string profileHex;
   if (mProfile.TotalSamples() > 0)
   {
      profileHex = BytesToHex(mProfile.Serialize());
   }
   v.Text("profile", profileHex);
   if (!profileHex.empty())
   {
      auto bytes = HexToBytes(profileHex);
      mProfile.Deserialize(bytes.data(), bytes.size());
   }
}

bool PredictiveColoringNode::EnsureShader()
{
   if (!mShaderTried)
   {
      mShaderTried = true;
      mProgram = GLUtil::CompileProgram(kColorGradeFrag);
   }
   if (!mDownsampleShaderTried)
   {
      mDownsampleShaderTried = true;
      mDownsampleProgram = GLUtil::CompileProgram(kDownsampleFrag);
   }
   return mProgram != 0 && mDownsampleProgram != 0;
}

void PredictiveColoringNode::AnalyzeInput(unsigned int srcTex, double t)
{
   const int size = mSampleSize;
   if (mDownsampleFbo == 0)
   {
      glGenTextures(1, &mDownsampleTex);
      glBindTexture(GL_TEXTURE_2D, mDownsampleTex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

      glGenFramebuffers(1, &mDownsampleFbo);
      glBindFramebuffer(GL_FRAMEBUFFER, mDownsampleFbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mDownsampleTex, 0);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glBindTexture(GL_TEXTURE_2D, 0);
   }

   GLUtil::Fbo wrapper;
   wrapper.fbo = mDownsampleFbo;
   wrapper.tex = mDownsampleTex;
   wrapper.w = size;
   wrapper.h = size;

   GLUtil::RunShaderPass(wrapper, mDownsampleProgram, [this, srcTex]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, srcTex);
      glUniform1i(glGetUniformLocation(mDownsampleProgram, "uSrc"), 0);
   });

   mPixels.assign((size_t)size * size * 4, 0);
   GLint prevFbo = 0;
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
   glBindFramebuffer(GL_FRAMEBUFFER, mDownsampleFbo);
   glPixelStorei(GL_PACK_ALIGNMENT, 1);
   glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, mPixels.data());
   glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);

   mLiveHist.Clear();
   const int numPixels = size * size;
   for (int i = 0; i < numPixels; i++)
   {
      const float r = mPixels[i * 4 + 0] / 255.0f;
      const float g = mPixels[i * 4 + 1] / 255.0f;
      const float b = mPixels[i * 4 + 2] / 255.0f;
      mLiveHist.AddPixel(r, g, b);
   }

   if (mLearning)
   {
      double dt = (mLastSampleSeconds >= 0.0) ? (t - mLastSampleSeconds) : 0.033;
      dt = std::clamp(dt, 0.001, 0.5);
      mProfile.Accumulate(mLiveHist, dt);
      ColorStats::Engine::Instance().Accumulate(mLiveHist, dt);
   }
   mLastSampleSeconds = t;
}

void PredictiveColoringNode::RecomputeFit(double dt)
{
   mTargetParams = ColorStats::Fit(mLiveHist, mProfile, selfNormalize);

   // Exponential smoothing (alpha ~ 0.15) to eliminate video noise jitter. This is the wander's
   // equilibrium (step-10 Task A) - the same role the learned mean plays in Drift's OU process.
   const float alpha = (float)std::clamp(dt * 5.0, 0.05, 1.0);
   auto lerpF = [](float cur, float tgt, float a) { return cur + (tgt - cur) * a; };

   mFitEquilibrium.rgbGainR   = lerpF(mFitEquilibrium.rgbGainR,   mTargetParams.rgbGainR,   alpha);
   mFitEquilibrium.rgbGainG   = lerpF(mFitEquilibrium.rgbGainG,   mTargetParams.rgbGainG,   alpha);
   mFitEquilibrium.rgbGainB   = lerpF(mFitEquilibrium.rgbGainB,   mTargetParams.rgbGainB,   alpha);
   mFitEquilibrium.exposure   = lerpF(mFitEquilibrium.exposure,   mTargetParams.exposure,   alpha);
   mFitEquilibrium.blackPoint = lerpF(mFitEquilibrium.blackPoint, mTargetParams.blackPoint, alpha);
   mFitEquilibrium.whitePoint = lerpF(mFitEquilibrium.whitePoint, mTargetParams.whitePoint, alpha);
   mFitEquilibrium.contrast   = lerpF(mFitEquilibrium.contrast,   mTargetParams.contrast,   alpha);
   mFitEquilibrium.pivot      = lerpF(mFitEquilibrium.pivot,      mTargetParams.pivot,      alpha);
   mFitEquilibrium.highlights = lerpF(mFitEquilibrium.highlights, mTargetParams.highlights, alpha);
   mFitEquilibrium.shadows    = lerpF(mFitEquilibrium.shadows,    mTargetParams.shadows,    alpha);
   mFitEquilibrium.midtones   = lerpF(mFitEquilibrium.midtones,   mTargetParams.midtones,   alpha);
   mFitEquilibrium.whites     = lerpF(mFitEquilibrium.whites,     mTargetParams.whites,     alpha);
   mFitEquilibrium.blacks     = lerpF(mFitEquilibrium.blacks,     mTargetParams.blacks,     alpha);
   mFitEquilibrium.saturation = lerpF(mFitEquilibrium.saturation, mTargetParams.saturation, alpha);
   mFitEquilibrium.vibrance   = lerpF(mFitEquilibrium.vibrance,   mTargetParams.vibrance,   alpha);
   mFitEquilibrium.hueShift   = lerpF(mFitEquilibrium.hueShift,   mTargetParams.hueShift,   alpha);
}

void PredictiveColoringNode::StepWander(double dt)
{
   if (seed != mSeedApplied)
   {
      mWanderRng = SeedColorRng(seed);
      mWanderOffset = ZeroOffset(); // fresh state, one clean rung on reseed
      mSeedApplied = seed;
   }

   // Cold start (A3): no learned data yet -> zero amplitude, not just a small one. hasLearnedData
   // also gates the confidence-scaled amplitude below to (effectively) zero once real data
   // exists but is still thin, since Confidence01() starts at 0 too - but the explicit gate here
   // is what guarantees an exact identity/Fit()-only output with nothing learned at all, per A3.
   const bool hasLearnedData = mProfile.TotalSamples() > 0;
   float amplitude = 0.0f;
   if (hasLearnedData)
   {
      // Spread factor: a profile that's only ever seen a flat grey card (TargetStdY near its
      // ResetToSelfNormalize default's low end) should wander less than one that's seen a wide
      // variety of footage. Confidence factor: more confident profiles wander tighter (A1).
      const float spreadFactor = std::clamp(mProfile.TargetStdY() / 0.3f, 0.0f, 1.0f);
      const float confidenceFactor = 1.0f - mProfile.Confidence01();
      amplitude = std::clamp(wander, 0.0f, 1.0f) * spreadFactor * confidenceFactor;
   }

   ColorStats::ColorGradeParams sigma = WanderBaseSigma();
   sigma.rgbGainR *= amplitude; sigma.rgbGainG *= amplitude; sigma.rgbGainB *= amplitude;
   sigma.exposure *= amplitude; sigma.blackPoint *= amplitude; sigma.whitePoint *= amplitude;
   sigma.contrast *= amplitude; sigma.pivot *= amplitude;
   sigma.highlights *= amplitude; sigma.shadows *= amplitude; sigma.midtones *= amplitude;
   sigma.whites *= amplitude; sigma.blacks *= amplitude;
   sigma.saturation *= amplitude; sigma.vibrance *= amplitude; sigma.hueShift *= amplitude;

   ColorStats::StepColorWander(mWanderOffset, mWanderRng, kWanderTheta, sigma, dt);

   mCurrentParams = mFitEquilibrium;
   mCurrentParams.rgbGainR   += mWanderOffset.rgbGainR;
   mCurrentParams.rgbGainG   += mWanderOffset.rgbGainG;
   mCurrentParams.rgbGainB   += mWanderOffset.rgbGainB;
   mCurrentParams.exposure   += mWanderOffset.exposure;
   mCurrentParams.blackPoint += mWanderOffset.blackPoint;
   mCurrentParams.whitePoint += mWanderOffset.whitePoint;
   mCurrentParams.contrast   += mWanderOffset.contrast;
   mCurrentParams.pivot      += mWanderOffset.pivot;
   mCurrentParams.highlights += mWanderOffset.highlights;
   mCurrentParams.shadows    += mWanderOffset.shadows;
   mCurrentParams.midtones   += mWanderOffset.midtones;
   mCurrentParams.whites     += mWanderOffset.whites;
   mCurrentParams.blacks     += mWanderOffset.blacks;
   mCurrentParams.saturation += mWanderOffset.saturation;
   mCurrentParams.vibrance   += mWanderOffset.vibrance;
   mCurrentParams.hueShift   += mWanderOffset.hueShift;
   ColorStats::ClampColorGradeParams(mCurrentParams);
}

void PredictiveColoringNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   unsigned int srcTex = mInput.Pull(frameId);
   if (srcTex == 0)
   {
      GLUtil::DestroyFbo(mOut);
      return;
   }
   if (!EnsureShader())
      return;

   const double now = Transport::Instance().Seconds();

   // Rate-limit analysis to 30 Hz (0.033s)
   if (mLastSampleSeconds < 0.0 || (now - mLastSampleSeconds) >= (1.0 / 30.0))
   {
      AnalyzeInput(srcTex, now);
   }

   // Rate-limit fitting recalculation to 4 Hz
   double dt = (mLastFitSeconds >= 0.0) ? (now - mLastFitSeconds) : 0.25;
   if (mLastFitSeconds < 0.0 || dt >= 0.25)
   {
      RecomputeFit(dt);
      mLastFitSeconds = now;
   }

   // Wander advances every cook (step-10 Task A): holding on one frame must not freeze the
   // output, so this runs off wall-clock dt independent of the fit's own 4 Hz rate limit.
   {
      const double wanderDt = (mLastWanderSeconds >= 0.0) ? std::clamp(now - mLastWanderSeconds, 0.0, 0.5) : 0.0;
      StepWander(wanderDt);
      mLastWanderSeconds = now;
   }

   if (!GLUtil::EnsureFbo(mOut, mInput.Width(), mInput.Height()))
      return;

   GLUtil::RunShaderPass(mOut, mProgram, [this, srcTex]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, srcTex);
      glUniform1i(glGetUniformLocation(mProgram, "uSrc"), 0);
      glUniform1f(glGetUniformLocation(mProgram, "uMix"), mix);

      glUniform3f(glGetUniformLocation(mProgram, "uRgbGain"),
                  mCurrentParams.rgbGainR, mCurrentParams.rgbGainG, mCurrentParams.rgbGainB);
      glUniform1f(glGetUniformLocation(mProgram, "uExposure"), mCurrentParams.exposure);
      glUniform1f(glGetUniformLocation(mProgram, "uBlackPoint"), mCurrentParams.blackPoint);
      glUniform1f(glGetUniformLocation(mProgram, "uWhitePoint"), mCurrentParams.whitePoint);
      glUniform1f(glGetUniformLocation(mProgram, "uContrast"), mCurrentParams.contrast);
      glUniform1f(glGetUniformLocation(mProgram, "uPivot"), mCurrentParams.pivot);
      glUniform1f(glGetUniformLocation(mProgram, "uHighlights"), mCurrentParams.highlights);
      glUniform1f(glGetUniformLocation(mProgram, "uShadows"), mCurrentParams.shadows);
      glUniform1f(glGetUniformLocation(mProgram, "uMidtones"), mCurrentParams.midtones);
      glUniform1f(glGetUniformLocation(mProgram, "uWhites"), mCurrentParams.whites);
      glUniform1f(glGetUniformLocation(mProgram, "uBlacks"), mCurrentParams.blacks);
      glUniform1f(glGetUniformLocation(mProgram, "uSaturation"), mCurrentParams.saturation);
      glUniform1f(glGetUniformLocation(mProgram, "uVibrance"), mCurrentParams.vibrance);
      glUniform1f(glGetUniformLocation(mProgram, "uHueShift"), mCurrentParams.hueShift);
   });
}

// --- Self-Test Harness ------------------------------------------------------

namespace PredictiveColoring
{
   bool RunPredColorTest()
   {
      std::cout << "Running Predictive Coloring Self-Tests (INFINITE_PREDCOLORTEST)...\n";

      // 1. Synthetic Gradient Moment Fit Test
      {
         ColorStats::HistogramSet h;
         h.Clear();
         // Generate smooth gradient with mean 0.5, known std
         for (int i = 0; i < 1000; i++)
         {
            float v = (float)i / 999.0f;
            h.AddPixel(v, v, v);
         }
         if (std::abs(h.MeanY() - 0.5f) > 0.05f)
         {
            std::cerr << "FAIL: Synthetic gradient mean mismatch " << h.MeanY() << "\n";
            return false;
         }
         ColorStats::Profile prof;
         ColorStats::ColorGradeParams params = ColorStats::Fit(h, prof, true);
         if (std::abs(params.exposure) > 0.3f)
         {
            std::cerr << "FAIL: Balanced input yielded non-zero exposure " << params.exposure << "\n";
            return false;
         }
      }

      // 2. Self-Normalize on underexposed frame
      {
         ColorStats::HistogramSet darkH;
         darkH.Clear();
         for (int i = 0; i < 1000; i++)
         {
            float v = (float)i / 999.0f * 0.2f; // Max 0.2
            darkH.AddPixel(v, v, v);
         }
         ColorStats::Profile prof;
         ColorStats::ColorGradeParams params = ColorStats::Fit(darkH, prof, true);
         if (params.exposure <= 0.5f)
         {
            std::cerr << "FAIL: Underexposed frame did not get positive exposure boost: " << params.exposure << "\n";
            return false;
         }
      }

      // 3. Match-reference mode statistical color transfer
      {
         ColorStats::HistogramSet src;
         src.Clear();
         for (int i = 0; i < 1000; i++)
         {
            float v = (float)i / 999.0f * 0.4f;
            src.AddPixel(v, v, v);
         }
         ColorStats::Profile targetProf;
         ColorStats::HistogramSet ref;
         ref.Clear();
         for (int i = 0; i < 1000; i++)
         {
            float v = 0.3f + ((float)i / 999.0f) * 0.6f;
            ref.AddPixel(v, v, v);
         }
         targetProf.Accumulate(ref, 1.0);
         ColorStats::ColorGradeParams params = ColorStats::Fit(src, targetProf, false);
         if (params.exposure <= 0.0f)
         {
            std::cerr << "FAIL: Target matching failed to boost exposure\n";
            return false;
         }
      }

      // 4. Persistence round-trip
      {
         ColorStats::Profile p1;
         ColorStats::HistogramSet h;
         h.Clear();
         for (int i = 0; i < 500; i++) h.AddPixel(0.8f, 0.2f, 0.4f);
         p1.Accumulate(h, 0.5);

         std::vector<uint8_t> blob = p1.Serialize();
         ColorStats::Profile p2;
         if (!p2.Deserialize(blob.data(), blob.size()))
         {
            std::cerr << "FAIL: Profile deserialization failed\n";
            return false;
         }
         if (std::abs(p1.TargetMeanR() - p2.TargetMeanR()) > 1e-4f)
         {
            std::cerr << "FAIL: Profile serialized value mismatch\n";
            return false;
         }
      }

      // 5. Node instantiation and confidence evaluation
      {
         PredictiveColoringNode node;
         node.mix = 0.8f;
         node.SetLearning(true);
         if (!node.IsLearning())
         {
            std::cerr << "FAIL: Node learning state toggle failed\n";
            return false;
         }
      }

      // 6. Wander determinism: two independently-seeded RNGs from the same seed value, stepped
      // through the same sigma/dt sequence, must land on the identical offset (step-10 Task A4).
      // Runs the ColorStats-level primitives directly since CookIfNeeded needs a GL context this
      // headless test harness doesn't have (test 5 avoids it for the same reason).
      {
         ColorStats::ColorGradeParams sigma = WanderBaseSigma();
         uint64_t rngA = SeedColorRng(7);
         uint64_t rngB = SeedColorRng(7);
         ColorStats::ColorGradeParams offA = ZeroOffset();
         ColorStats::ColorGradeParams offB = ZeroOffset();
         for (int i = 0; i < 50; i++)
         {
            ColorStats::StepColorWander(offA, rngA, kWanderTheta, sigma, 1.0 / 30.0);
            ColorStats::StepColorWander(offB, rngB, kWanderTheta, sigma, 1.0 / 30.0);
         }
         if (offA.exposure != offB.exposure || offA.hueShift != offB.hueShift ||
             offA.saturation != offB.saturation)
         {
            std::cerr << "FAIL: same seed produced different wander trajectories\n";
            return false;
         }
         // A different seed must (overwhelmingly likely) diverge - guards against SeedColorRng
         // degenerating to a constant.
         uint64_t rngC = SeedColorRng(8);
         ColorStats::ColorGradeParams offC = ZeroOffset();
         for (int i = 0; i < 50; i++)
            ColorStats::StepColorWander(offC, rngC, kWanderTheta, sigma, 1.0 / 30.0);
         if (offC.exposure == offA.exposure && offC.hueShift == offA.hueShift)
         {
            std::cerr << "FAIL: different seeds produced identical wander trajectories\n";
            return false;
         }
      }

      // 7. Cold start: zero sigma (what StepWander computes when !hasLearnedData) keeps the
      // offset at exactly zero forever, regardless of how long it runs.
      {
         ColorStats::ColorGradeParams zeroSigma = ZeroOffset();
         uint64_t rng = SeedColorRng(3);
         ColorStats::ColorGradeParams off = ZeroOffset();
         for (int i = 0; i < 500; i++)
            ColorStats::StepColorWander(off, rng, kWanderTheta, zeroSigma, 1.0 / 30.0);
         ColorStats::ColorGradeParams zero = ZeroOffset();
         if (std::memcmp(&off, &zero, sizeof(off)) != 0)
         {
            std::cerr << "FAIL: zero-sigma wander drifted away from zero (cold start)\n";
            return false;
         }
      }

      // 8. Clamp correctness: even with sigma driven far past its normal range, the equilibrium
      // (identity grade) plus offset always lands inside ClampColorGradeParams' bounds, and
      // hueShift always wraps into [0,1) rather than saturating at an edge.
      {
         ColorStats::ColorGradeParams bigSigma = WanderBaseSigma();
         bigSigma.rgbGainR *= 50.0f; bigSigma.rgbGainG *= 50.0f; bigSigma.rgbGainB *= 50.0f;
         bigSigma.exposure *= 50.0f; bigSigma.blackPoint *= 50.0f; bigSigma.whitePoint *= 50.0f;
         bigSigma.contrast *= 50.0f; bigSigma.pivot *= 50.0f;
         bigSigma.highlights *= 50.0f; bigSigma.shadows *= 50.0f; bigSigma.midtones *= 50.0f;
         bigSigma.whites *= 50.0f; bigSigma.blacks *= 50.0f;
         bigSigma.saturation *= 50.0f; bigSigma.vibrance *= 50.0f; bigSigma.hueShift *= 50.0f;
         uint64_t rng = SeedColorRng(9);
         ColorStats::ColorGradeParams off = ZeroOffset();
         for (int i = 0; i < 2000; i++)
         {
            ColorStats::StepColorWander(off, rng, kWanderTheta, bigSigma, 1.0 / 30.0);
            ColorStats::ColorGradeParams applied; // identity equilibrium + offset
            applied.rgbGainR = 1.0f + off.rgbGainR; applied.rgbGainG = 1.0f + off.rgbGainG;
            applied.rgbGainB = 1.0f + off.rgbGainB; applied.exposure = off.exposure;
            applied.blackPoint = off.blackPoint; applied.whitePoint = 1.0f + off.whitePoint;
            applied.contrast = 1.0f + off.contrast; applied.pivot = 0.5f + off.pivot;
            applied.highlights = off.highlights; applied.shadows = off.shadows;
            applied.midtones = 1.0f + off.midtones; applied.whites = off.whites;
            applied.blacks = off.blacks; applied.saturation = 1.0f + off.saturation;
            applied.vibrance = off.vibrance; applied.hueShift = off.hueShift;
            ColorStats::ClampColorGradeParams(applied);
            if (applied.contrast <= 0.0f || applied.saturation < 0.0f ||
                applied.hueShift < 0.0f || applied.hueShift >= 1.0f)
            {
               std::cerr << "FAIL: clamped params left the valid range at iteration " << i << "\n";
               return false;
            }
         }
      }

      // 9. wander == 0 reproduces the pre-Task-A deterministic behavior exactly: sigma scaled by
      // amplitude 0 keeps the offset at zero, so equilibrium + offset == equilibrium.
      {
         ColorStats::ColorGradeParams zeroSigma = WanderBaseSigma();
         const float kAmplitudeZero = 0.0f;
         zeroSigma.rgbGainR *= kAmplitudeZero; zeroSigma.rgbGainG *= kAmplitudeZero;
         zeroSigma.rgbGainB *= kAmplitudeZero; zeroSigma.exposure *= kAmplitudeZero;
         zeroSigma.blackPoint *= kAmplitudeZero; zeroSigma.whitePoint *= kAmplitudeZero;
         zeroSigma.contrast *= kAmplitudeZero; zeroSigma.pivot *= kAmplitudeZero;
         zeroSigma.highlights *= kAmplitudeZero; zeroSigma.shadows *= kAmplitudeZero;
         zeroSigma.midtones *= kAmplitudeZero; zeroSigma.whites *= kAmplitudeZero;
         zeroSigma.blacks *= kAmplitudeZero; zeroSigma.saturation *= kAmplitudeZero;
         zeroSigma.vibrance *= kAmplitudeZero; zeroSigma.hueShift *= kAmplitudeZero;
         uint64_t rng = SeedColorRng(4);
         ColorStats::ColorGradeParams off = ZeroOffset();
         for (int i = 0; i < 10; i++)
            ColorStats::StepColorWander(off, rng, kWanderTheta, zeroSigma, 1.0 / 30.0);
         ColorStats::ColorGradeParams zero = ZeroOffset();
         if (std::memcmp(&off, &zero, sizeof(off)) != 0)
         {
            std::cerr << "FAIL: wander=0 still perturbed the offset\n";
            return false;
         }
      }

      std::cout << "PREDCOLOR TEST: PASS\n";
      return true;
   }
}
