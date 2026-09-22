#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/INode.h"
#include "core/NoteCable.h"

class AudioPredictiveQuantizeNode;

// Predictive Quantize (docs/plans/prediction/step-09). Wire a note chain in and press Learn: it
// captures incoming note onsets, passing them through untouched while it listens, and builds a
// small histogram of inter-onset spacings. Stop learning and it corrects future note-on timing by
// pulling each onset's spacing toward the nearest spacing it actually heard - a groove/template
// quantizer, not the fixed-grid QuantizerNode (NoteNodes.h) sitting next to it in the palette.
//
// Two objects, matching PredictiveNotesNode's split: this INode owns Learn state and the
// histogram/mode fit (synchronous - small enough it needs no worker thread); the audio object
// captures onsets and applies the correction. The learned mode set crosses to the audio thread by
// atomic pointer swap, freed on the main thread once the audio thread has moved past it.
class PredictiveQuantizeNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new PredictiveQuantizeNode(); }
   PredictiveQuantizeNode();
   ~PredictiveQuantizeNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;
   void SweepPrepare() override;
   bool SweepNeedsClock() const override { return true; }

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   INode* BypassSource() override { return noteInput.GetSource(); }
   AudioNode* GetAudioNode() override;

   // Saved params. Names are patch keys.
   float mix = 0.75f;      // 0 = untouched pass-through, 1 = fully snapped to the nearest learned spacing
   std::string modeSet;    // encoded learned spacings: "tick:weight;tick:weight;..." (step-09 §4)

   NoteCable noteInput;

   // --- main-thread UI / test helpers ---
   // One captured note-on: absolute beat and its onset's IOI-viewpoint tick (NoteModel::kTicksPerBeat).
   struct Cap
   {
      double beat;
   };
   bool IsLearning() const { return mLearning; }
   void SetLearning(bool on);
   int OnsetsCaptured() const { return mOnsetsCaptured; }
   int ModeCount() const;
   float Confidence01() const;
   int Dropped() const;

   AudioPredictiveQuantizeNode* Audio() { return mAudioNode.get(); }
   // Tests: install a mode set through the same swap path a finished Learn uses.
   void TestSwapModes(const std::string& encoded);

private:
   void DrainCaptures();
   void FinishLearn();
   void FitModes();

   std::unique_ptr<AudioPredictiveQuantizeNode> mAudioNode;
   int mLastCookFrame = -1;

   bool mLearning = false;
   std::vector<double> mCapturedBeats;
   int mOnsetsCaptured = 0;
   std::string mAppliedModeSet;
};

namespace PredictiveQuantize
{
   // INFINITE_PREDQUANTIZETEST, headless: identity at mix=0, exact snap to a learned mode at
   // mix=1, chord grouping doesn't arpeggiate, cold start passes input through unchanged, save/load
   // round-trips the encoded mode set.
   bool RunPredQuantizeTest();
}
