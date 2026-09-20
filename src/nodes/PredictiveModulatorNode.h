#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "INode.h"
#include "Modulation.h"
#include "core/MovementStats.h"

// Predictive Modulator (Prediction Step 8 item 5). Learns a short open-loop dynamical model
// (Dynamic Mode Decomposition - MovementStats::DMDFit, the same fit Play Like Me/DMD groundwork
// from step 7 introduced) of whatever's patched into its single modulator input, then free-runs
// that model once Learn stops: "play back the shape of the pattern", not the exact recording,
// and not driven by the live cable at all once it's playing.
//
// This is deliberately its own IModulator, not an IPredictor: it drives exactly one destination
// (whatever it's patched into), the same single-slot shape SmoothNode/RangeToRangeNode already
// use, unlike Drift/Moves which fan out to many bound knobs at once.
//
// It owns a PRIVATE MovementStats::Engine rather than reading MovementStats::Live() - it has
// nothing to do with real user moves across the patch, only the one signal wired into its own
// input. DMD itself needs >= 2 parallel time series to fit a transition matrix (Engine::FitDMD
// requires at least 2 registered continuous keys), so a single scalar signal is learned via a
// small Takens delay embedding: `rank` synthetic keys, each the same input signal lagged by one
// more capture tick than the last.
class PredictiveModulatorNode : public INode, public IModulator
{
public:
   static INode* Create() { return new PredictiveModulatorNode(); }
   PredictiveModulatorNode();

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override;
   const char* InputLabel(int) const override { return "in"; }

   float Value01() override;

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }
   IModulator* input = nullptr;
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   int ModulatorInputCount() const override { return 1; }

   // Saved params. Names are patch keys.
   float constantIn = 0.5f; // used when nothing is patched - during Learn and as the pre-fit fallback
   int rank = 3;             // delay-embedding dimension / DMD rank: 2, 3, 4 or 8
   std::string fitData;      // base64: the fitted DMDFit (rank, spectral radius, A matrix)
   float speed = 1.0f;       // playback rate multiplier (0.1x .. 10.0x)
   float low = 0.0f;         // output low bound
   float high = 1.0f;        // output high bound

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("constantIn", constantIn);
      v.Int("rank", rank);
      v.Text("fitData", fitData);
      v.Float("speed", speed);
      v.Float("low", low);
      v.Float("high", high);
   }

   // --- UI / main-thread helpers ---
   bool IsLearning() const { return mLearning; }
   void SetLearning(bool on);
   int SamplesCaptured() const { return mSamplesCaptured; }
   bool HasFit() const { return mFit.valid; }
   // Mirrors Engine::FitDMD's own N < 20 rejection (MovementStats.cpp) so the UI can tell
   // "never tried" apart from "tried, but Learn didn't run long enough" - HasLearnAttempt()
   // is true as soon as Stop has run FitDMD at least once, valid or not.
   static constexpr int kMinFitSamples = 20;
   bool HasLearnAttempt() const { return mHasLearnAttempt; }
   // Read-only honest readout: the fitted model's own spectral radius (<=1 by construction -
   // MovementStats::DMDFit's stability ceiling). 0 when there is no fit yet - never fabricate a
   // confident-looking number before Learn has actually produced one.
   float SpectralRadius() const { return mFit.valid ? mFit.spectralRadius : 0.0f; }
   // 0-100: how much of FitDMD's own N >= kMinFitSamples requirement the engine's real grid
   // history has actually reached, straight from the Engine (not mSamplesCaptured, which counts
   // capture ticks - a number FitDMD never looks at). This is the number the UI shows as
   // "Learning %".
   int LearningPercent() const;

private:
   void CaptureTick();      // called from Value01() while learning, at most once per transport tick
   void FinishLearn();      // Stop: FitDMD, serialize into fitData, seed the free-run state
   void LoadFitFromSaved(); // deserialize fitData into mFit when it doesn't match what's applied

   bool mLearning = false;
   double mLastCaptureSeconds = -1.0;
   std::vector<float> mDelayLine; // most-recent-first; primed once it reaches `rank` entries
   int mSamplesCaptured = 0;

   MovementStats::Engine mEngine; // private: only this node's own input ever feeds it
   MovementStats::DMDFit mFit;
   std::vector<float> mFreeState; // current free-run state vector, size == mFit.rank
   double mLastStepSeconds = -1.0;
   uint64_t mFreeRng = 0x9E3779B97F4A7C15ull; // free-run noise seed - see DMDFit::Step
   std::string mAppliedFitData; // last fitData string actually parsed into mFit
   bool mHasLearnAttempt = false; // set once by FinishLearn(), regardless of outcome
};
