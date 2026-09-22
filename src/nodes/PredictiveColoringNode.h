#pragma once

#include <string>
#include <vector>

#include "GLUtil.h"
#include "INode.h"
#include "ImageCable.h"
#include "core/ColorStats.h"

// =========================================================================
// PredictiveColoringNode — Photographic Color Predictor
// (docs/plans/prediction/step-08-predictive-coloring.md)
//
// Learns what "graded" looks like for the user's footage live across sessions,
// and grades incoming frames toward the learned target via a 13-operator GPU
// shader pipeline.
// =========================================================================

class PredictiveColoringNode : public INode
{
public:
   static INode* Create() { return new PredictiveColoringNode(); }

   PredictiveColoringNode();
   ~PredictiveColoringNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;

   ImageCable& Input() { return mInput; }
   INode* BypassSource() override { return mInput.GetSource(); }

   bool IsLearning() const { return mLearning; }
   void SetLearning(bool on);
   void ResetProfile();

   float Confidence01() const;
   // This instance's own progress only, ignoring the shared engine entirely - what the UI shows
   // as "learning - N%" while mLearning is true, distinct from the blended Confidence01() shown
   // once stopped (step-10 review: the badge must move as THIS take learns, not just report the
   // mostly-static shared confidence).
   float LocalConfidence01() const;
   uint64_t TotalSamples() const;

   const ColorStats::HistogramSet& LiveHistogram() const { return mLiveHist; }
   const ColorStats::Profile& LearnedProfile() const { return mProfile; }
   const ColorStats::ColorGradeParams& CurrentParams() const { return mCurrentParams; }

   float mix = 1.0f;               // 0 = untouched pass-through, 1 = fully graded
   bool selfNormalize = true;      // true = auto-levels/contrast, false = match learned target
   // OU wander around Fit()'s equilibrium (step-10 Task A), the "generative" property the other
   // four Prediction nodes have and this one lacked: 0 = old deterministic closed-form behavior
   // exactly, unchanged; higher = the grade gently drifts among looks the profile has actually
   // seen, scaled by the profile's own learned spread and confidence (see RecomputeFit).
   float wander = 0.15f;
   int seed = 1;                   // wander RNG seed, so a take is reproducible (INFINITE_PREDCOLORTEST)

   void VisitParams(ParamVisitor& v) override;

private:
   bool EnsureShader();
   void AnalyzeInput(unsigned int srcTex, double t);
   void RecomputeFit(double dt);
   void StepWander(double dt);

   ImageCable mInput;
   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;

   bool mLearning = false;

   // Downsampling & GPU readback
   unsigned int mDownsampleFbo = 0;
   unsigned int mDownsampleTex = 0;
   unsigned int mDownsampleProgram = 0;
   bool mDownsampleShaderTried = false;
   int mSampleSize = 64;
   std::vector<unsigned char> mPixels;

   double mLastSampleSeconds = -1.0;
   double mLastFitSeconds = -1.0;
   double mLastWanderSeconds = -1.0;

   ColorStats::HistogramSet mLiveHist;
   ColorStats::Profile mProfile;
   // This instance's own accumulated samples only (never seeded from the shared engine). Drives
   // TotalSamples()/hasLearned so a freshly-added node shows "press Learn", not "Learn Again" with
   // someone else's confidence — see Confidence01()'s local/shared blend.
   ColorStats::Profile mLocalProfile;
   ColorStats::ColorGradeParams mTargetParams;    // this tick's raw Fit() output
   ColorStats::ColorGradeParams mFitEquilibrium;  // jitter-smoothed mTargetParams - the wander's home
   ColorStats::ColorGradeParams mCurrentParams;   // mFitEquilibrium + wander offset, clamped: what's actually applied
   ColorStats::ColorGradeParams mWanderOffset;    // OU state, mean-reverts to zero
   uint64_t mWanderRng = 1;
   int mSeedApplied = 0;
};

namespace PredictiveColoring
{
   bool RunPredColorTest();
}
